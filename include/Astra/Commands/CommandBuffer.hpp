#pragma once

#include "CommandTypes.hpp"
#include "CommandStorage.hpp"
#include "CommandExecutor.hpp"
#include "../Registry/Registry.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace Astra
{
    /**
     * Main interface for deferred command execution in Astra
     * Provides a high-performance, cache-friendly way to batch operations
     * 
     * Example usage:
     * @code
     * CommandBuffer buffer(&registry);
     * 
     * // Record commands
     * Entity e1 = buffer.CreateEntity();
     * buffer.AddComponent(e1, Position{10, 20});
     * buffer.AddComponent(e1, Velocity{1, 0});
     * 
     * // Execute all commands at once
     * buffer.Execute();
     * @endcode
     */
    class CommandBuffer
    {
    private:
        // Storage for non-templated commands
        using BaseStorage = Commands::CommandStorage<
            Commands::CreateEntity,
            Commands::DestroyEntity,
            Commands::CreateEntities,
            Commands::DestroyEntities,
            Commands::SetParent,
            Commands::RemoveParent,
            Commands::AddLink,
            Commands::RemoveLink
        >;
        
        // Executor for non-templated commands
        using BaseExecutor = Commands::CommandExecutor<
            Commands::CreateEntity,
            Commands::DestroyEntity,
            Commands::CreateEntities,
            Commands::DestroyEntities,
            Commands::SetParent,
            Commands::RemoveParent,
            Commands::AddLink,
            Commands::RemoveLink
        >;
        
        Registry* m_registry;
        BaseStorage m_baseStorage;
        BaseExecutor m_baseExecutor;
        
        // For component commands, we need type-erased storage
        // This is a simplified approach - in production you might want a more sophisticated system
        std::vector<std::function<void(Registry*)>> m_componentCommands;
        
        // Entity ID generator for temporary entities
        mutable Entity::IDType m_nextTempId = std::numeric_limits<Entity::IDType>::max();
        
    public:
        /**
         * Create a command buffer for a specific registry
         * @param registry The registry to execute commands on
         */
        explicit CommandBuffer(Registry* registry)
            : m_registry(registry)
            , m_baseExecutor(registry)
        {
            ASTRA_ASSERT(registry != nullptr, "Registry cannot be null");
        }
        
        // ============= Entity Commands =============
        
        /**
         * Record a command to create an entity
         * @return A temporary entity ID that will be mapped to the real entity on execution
         */
        Entity CreateEntity()
        {
            Entity tempEntity{m_nextTempId--};
            m_baseStorage.Add(Commands::CreateEntity{tempEntity});
            return tempEntity;
        }
        
        /**
         * Record a command to destroy an entity
         * @param entity The entity to destroy (can be temporary or real)
         */
        void DestroyEntity(Entity entity)
        {
            m_baseStorage.Add(Commands::DestroyEntity{entity});
        }
        
        /**
         * Record a command to create multiple entities
         * @param count Number of entities to create
         * @param outEntities Output buffer for entity IDs (must remain valid until Execute)
         */
        void CreateEntities(size_t count, Entity* outEntities)
        {
            m_baseStorage.Add(Commands::CreateEntities{count, outEntities});
        }
        
        /**
         * Record a command to destroy multiple entities
         * @param entities The entities to destroy
         */
        void DestroyEntities(std::span<const Entity> entities)
        {
            m_baseStorage.Add(Commands::DestroyEntities{
                std::vector<Entity>(entities.begin(), entities.end())
            });
        }
        
        // ============= Component Commands =============
        
        /**
         * Record a command to add a component to an entity
         * @tparam T The component type
         * @param entity The entity (can be temporary or real)
         * @param component The component value
         */
        template<Component T>
        void AddComponent(Entity entity, T&& component)
        {
            m_componentCommands.emplace_back(
                [entity, comp = std::forward<T>(component), this](Registry* registry) mutable
                {
                    // Resolve entity through executor's remapping
                    Entity resolvedEntity = m_baseExecutor.ResolveEntity(entity);
                    if (resolvedEntity != Entity::Invalid())
                    {
                        registry->AddComponent<T>(resolvedEntity, std::move(comp));
                    }
                }
            );
        }
        
        /**
         * Record a command to remove a component from an entity
         * @tparam T The component type
         * @param entity The entity (can be temporary or real)
         */
        template<Component T>
        void RemoveComponent(Entity entity)
        {
            m_componentCommands.emplace_back(
                [entity, this](Registry* registry)
                {
                    Entity resolvedEntity = m_baseExecutor.ResolveEntity(entity);
                    if (resolvedEntity != Entity::Invalid())
                    {
                        registry->RemoveComponent<T>(resolvedEntity);
                    }
                }
            );
        }
        
        /**
         * Record a command to set/update a component value
         * @tparam T The component type
         * @param entity The entity (can be temporary or real)
         * @param component The new component value
         */
        template<Component T>
        void SetComponent(Entity entity, T&& component)
        {
            m_componentCommands.emplace_back(
                [entity, comp = std::forward<T>(component), this](Registry* registry) mutable
                {
                    Entity resolvedEntity = m_baseExecutor.ResolveEntity(entity);
                    if (resolvedEntity != Entity::Invalid())
                    {
                        if (T* existing = registry->GetComponent<T>(resolvedEntity))
                        {
                            *existing = std::move(comp);
                        }
                        else
                        {
                            registry->AddComponent<T>(resolvedEntity, std::move(comp));
                        }
                    }
                }
            );
        }
        
        /**
         * Record a command to add components to multiple entities
         * @tparam T The component type
         * @param entities The entities to add components to
         * @param component The component value (same for all entities)
         */
        template<Component T>
        void AddComponents(std::span<const Entity> entities, const T& component)
        {
            std::vector<Entity> entityCopy(entities.begin(), entities.end());
            m_componentCommands.emplace_back(
                [entityCopy = std::move(entityCopy), component, this](Registry* registry)
                {
                    // Resolve all entity IDs
                    SmallVector<Entity, 256> resolvedEntities;
                    resolvedEntities.reserve(entityCopy.size());
                    for (Entity e : entityCopy)
                    {
                        Entity resolved = m_baseExecutor.ResolveEntity(e);
                        if (resolved != Entity::Invalid())
                        {
                            resolvedEntities.push_back(resolved);
                        }
                    }
                    registry->AddComponents<T>(resolvedEntities, component);
                }
            );
        }
        
        /**
         * Record a command to remove components from multiple entities
         * @tparam T The component type
         * @param entities The entities to remove components from
         */
        template<Component T>
        void RemoveComponents(std::span<const Entity> entities)
        {
            std::vector<Entity> entityCopy(entities.begin(), entities.end());
            m_componentCommands.emplace_back(
                [entityCopy = std::move(entityCopy), this](Registry* registry)
                {
                    // Resolve all entity IDs
                    SmallVector<Entity, 256> resolvedEntities;
                    resolvedEntities.reserve(entityCopy.size());
                    for (Entity e : entityCopy)
                    {
                        Entity resolved = m_baseExecutor.ResolveEntity(e);
                        if (resolved != Entity::Invalid())
                        {
                            resolvedEntities.push_back(resolved);
                        }
                    }
                    registry->RemoveComponents<T>(resolvedEntities);
                }
            );
        }
        
        // ============= Relationship Commands =============
        
        /**
         * Record a command to set a parent-child relationship
         * @param child Child entity (can be temporary or real)
         * @param parent Parent entity (can be temporary or real)
         */
        void SetParent(Entity child, Entity parent)
        {
            m_baseStorage.Add(Commands::SetParent{child, parent});
        }
        
        /**
         * Record a command to remove parent from a child
         * @param child Child entity (can be temporary or real)
         */
        void RemoveParent(Entity child)
        {
            m_baseStorage.Add(Commands::RemoveParent{child});
        }
        
        /**
         * Record a command to add a bidirectional link between entities
         * @param a First entity (can be temporary or real)
         * @param b Second entity (can be temporary or real)
         */
        void AddLink(Entity a, Entity b)
        {
            m_baseStorage.Add(Commands::AddLink{a, b});
        }
        
        /**
         * Record a command to remove a bidirectional link between entities
         * @param a First entity (can be temporary or real)
         * @param b Second entity (can be temporary or real)
         */
        void RemoveLink(Entity a, Entity b)
        {
            m_baseStorage.Add(Commands::RemoveLink{a, b});
        }
        
        // ============= Execution =============
        
        /**
         * Execute all recorded commands
         * Commands are executed in the order they were recorded
         * @param clearAfterExecution Whether to clear the buffer after execution (default: true)
         */
        void Execute(bool clearAfterExecution = true)
        {
            // Execute base commands first (entities, relationships)
            // Don't clear base storage yet since we need the remapping for component commands
            m_baseExecutor.Execute(m_baseStorage, false);
            
            // Execute component commands
            for (auto& cmd : m_componentCommands)
            {
                cmd(m_registry);
            }
            
            // Clear everything for reuse by default
            if (clearAfterExecution)
            {
                Clear();
            }
            else
            {
                // Still need to clear the remapping for consistency
                m_baseExecutor.ClearRemapping();
            }
        }
        
        /**
         * Clear all recorded commands without executing
         */
        void Clear()
        {
            m_baseStorage.Clear();
            m_componentCommands.clear();
            m_baseExecutor.ClearRemapping();
        }
        
        /**
         * Reserve space for expected number of commands
         * @param commandCount Expected total number of commands
         */
        void Reserve(size_t commandCount)
        {
            m_baseStorage.Reserve(commandCount / 2);  // Estimate half are base commands
            m_componentCommands.reserve(commandCount / 2);  // Other half are component commands
        }
        
        // ============= Statistics =============
        
        /**
         * Get total number of recorded commands
         */
        [[nodiscard]] size_t GetCommandCount() const noexcept
        {
            return m_baseStorage.GetTotalCommands() + m_componentCommands.size();
        }
        
        /**
         * Check if buffer is empty
         */
        [[nodiscard]] bool IsEmpty() const noexcept
        {
            return m_baseStorage.IsEmpty() && m_componentCommands.empty();
        }
        
        /**
         * Get estimated memory usage in bytes
         */
        [[nodiscard]] size_t GetMemoryUsage() const noexcept
        {
            return m_baseStorage.GetMemoryUsage() + 
                   (m_componentCommands.capacity() * sizeof(std::function<void(Registry*)>));
        }
        
        /**
         * Get the registry this buffer is associated with
         */
        [[nodiscard]] Registry* GetRegistry() const noexcept
        {
            return m_registry;
        }
        
        /**
         * Get access to internal storage (for merging)
         */
        [[nodiscard]] BaseStorage& GetStorage() noexcept
        {
            return m_baseStorage;
        }
        
        [[nodiscard]] const BaseStorage& GetStorage() const noexcept
        {
            return m_baseStorage;
        }
    };
    
    /**
     * Thread-safe command buffer for use in parallel execution contexts.
     * 
     * ParallelCommandBuffer provides a way to safely record structural changes
     * during parallel iteration. Each thread gets its own CommandBuffer
     * to avoid synchronization overhead during recording.
     * 
     * Usage:
     * @code
     * ParallelCommandBuffer commands(registry);
     * 
     * view.ParallelForEach([&commands](Entity e, Position& pos) {
     *     if (pos.x > 100) {
     *         commands.GetThreadBuffer().DestroyEntity(e);
     *     }
     * });
     * 
     * commands.Execute();  // Execute all commands from all threads
     * @endcode
     * 
     * @note ParallelCommandBuffer is designed to be used with lambda capture,
     *       not static access, to avoid issues with nested parallel regions.
     */
    class ParallelCommandBuffer
    {
    private:
        Registry& m_registry;
        mutable std::mutex m_mutex;
        mutable std::vector<std::unique_ptr<CommandBuffer>> m_buffers;
        mutable std::atomic<size_t> m_nextIndex{0};
        
        // Thread-local cache to avoid repeated lookups
        struct ThreadCache
        {
            ParallelCommandBuffer* context = nullptr;
            CommandBuffer* buffer = nullptr;
            size_t index = std::numeric_limits<size_t>::max();
        };
        
        static thread_local ThreadCache t_cache;
        
    public:
        /**
         * Construct a ParallelCommandBuffer context for the given registry.
         * 
         * @param registry The registry to execute commands on
         */
        explicit ParallelCommandBuffer(Registry& registry) 
            : m_registry(registry)
        {
            // Pre-reserve space for typical thread counts
            const size_t expectedThreads = std::thread::hardware_concurrency();
            m_buffers.reserve(expectedThreads);
        }
        
        /**
         * Get the thread-local CommandBuffer for the current thread.
         * 
         * This method is thread-safe and lazy-initializes a CommandBuffer
         * for each unique thread that calls it. The same buffer is returned
         * for subsequent calls from the same thread.
         * 
         * @return Reference to the thread-local CommandBuffer
         * 
         * @note This method should be called from within a parallel execution
         *       context where the ParallelCommandBuffer object is captured by reference.
         */
        CommandBuffer& GetThreadBuffer() const
        {
            // Fast path: check thread-local cache
            if (t_cache.context == this && t_cache.buffer != nullptr)
            {
                return *t_cache.buffer;
            }
            
            // Slow path: create new buffer for this thread
            return InitializeThreadBuffer();
        }
        
        /**
         * Execute all commands from all thread buffers.
         * 
         * Commands are executed in the order threads were initialized,
         * with all commands from a single thread executed together.
         * 
         * @note This should be called from a single thread after parallel execution.
         */
        void Execute()
        {
            for (const auto& buffer : m_buffers)
            {
                if (buffer && !buffer->IsEmpty())
                {
                    buffer->Execute();
                }
            }
        }
        
        /**
         * Merge all commands into a target CommandBuffer.
         * 
         * This allows deferred execution by merging all thread-local
         * commands into a single buffer for later execution.
         * 
         * @param target The CommandBuffer to merge into
         */
        void MergeInto(CommandBuffer& target)
        {
            for (auto& buffer : m_buffers)
            {
                if (buffer && !buffer->IsEmpty())
                {
                    target.GetStorage().Merge(std::move(buffer->GetStorage()));
                    buffer->Clear();
                }
            }
        }
        
        /**
         * Clear all commands from all thread buffers.
         */
        void Clear()
        {
            for (auto& buffer : m_buffers)
            {
                if (buffer)
                {
                    buffer->Clear();
                }
            }
        }
        
        /**
         * Get the total number of commands across all thread buffers.
         * 
         * @return Total command count
         */
        ASTRA_NODISCARD size_t GetCommandCount() const
        {
            size_t total = 0;
            for (const auto& buffer : m_buffers)
            {
                if (buffer)
                {
                    total += buffer->GetCommandCount();
                }
            }
            return total;
        }
        
        /**
         * Check if all thread buffers are empty.
         * 
         * @return true if no commands have been recorded
         */
        ASTRA_NODISCARD bool IsEmpty() const
        {
            for (const auto& buffer : m_buffers)
            {
                if (buffer && !buffer->IsEmpty())
                {
                    return false;
                }
            }
            return true;
        }
        
        /**
         * Get the number of thread buffers that have been created.
         * 
         * @return Number of unique threads that have called GetThreadBuffer()
         */
        ASTRA_NODISCARD size_t GetThreadCount() const
        {
            return m_buffers.size();
        }
        
    private:
        CommandBuffer& InitializeThreadBuffer() const
        {
            // Allocate a new index for this thread
            const size_t index = m_nextIndex.fetch_add(1, std::memory_order_relaxed);
            
            // Lock only for vector modification
            std::unique_lock lock(m_mutex);
            
            // Ensure vector is large enough
            if (index >= m_buffers.size())
            {
                m_buffers.resize(index + 1);
            }
            
            // Create the buffer if it doesn't exist
            if (!m_buffers[index])
            {
                m_buffers[index] = std::make_unique<CommandBuffer>(&m_registry);
            }
            
            CommandBuffer* buffer = m_buffers[index].get();
            
            // Unlock before updating thread-local cache
            lock.unlock();
            
            // Update thread-local cache
            t_cache.context = const_cast<ParallelCommandBuffer*>(this);
            t_cache.buffer = buffer;
            t_cache.index = index;
            
            return *buffer;
        }
    };
    
    // Thread-local storage definition
    inline thread_local ParallelCommandBuffer::ThreadCache ParallelCommandBuffer::t_cache;
    
} // namespace Astra