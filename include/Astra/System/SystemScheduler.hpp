#pragma once

#include <limits>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../Archetype/Archetype.hpp"  // For MakeComponentMask
#include "../Component/Component.hpp"
#include "../Core/Base.hpp"
#include "../Core/Delegate.hpp"  // Use Delegate instead of std::function
#include "../Core/TypeID.hpp"
#include "../Registry/Registry.hpp"
#include "System.hpp"
#include "SystemExecutor.hpp"
#include "SystemMetadata.hpp"

namespace Astra
{
    /**
     * @brief System scheduler for organizing and executing ECS systems
     * 
     * The SystemScheduler provides:
     * - Type-safe system registration
     * - Sequential execution order based on registration order
     * - Automatic parallelization based on component access patterns
     * - Integration with custom job systems via executors
     * 
     * Key design principles:
     * - Systems are identified by their type (each type can only be registered once)
     * - Execution order is determined by registration order
     * - Read/Write hints are optional and only affect parallelization, not ordering
     * - Zero overhead when not used
     * 
     * @warning This class is not thread-safe. Do not call Execute() concurrently
     *          from multiple threads on the same scheduler instance. Systems themselves
     *          may use parallel execution internally via ParallelForEach.
     * 
     * Example usage:
     * @code
     * struct PhysicsSystem {
     *     void operator()(Registry& registry) {
     *         auto view = registry.View<Position, Velocity>();
     *         view.ForEach([](Entity e, Position& pos, Velocity& vel) {
     *             pos.x += vel.x;
     *             pos.y += vel.y;
     *         });
     *     }
     * };
     * 
     * SystemScheduler scheduler;
     * 
     * scheduler.AddSystem<PhysicsSystem>()
     *     .Reads<Velocity>()
     *     .Writes<Position>();
     * 
     * scheduler.AddSystem<RenderSystem>()
     *     .Reads<Position, Sprite>();
     * 
     * // Execute with default sequential executor
     * scheduler.Execute(registry);
     * 
     * // Or with custom executor
     * MyJobSystemExecutor executor;
     * scheduler.Execute(registry, &executor);
     * @endcode
     */
    class SystemScheduler
    {
    private:
        // Internal system entry
        struct SystemEntry
        {
            std::unique_ptr<void, void(*)(void*)> instance;  // Type-erased system instance
            Delegate<void(Registry&)> execute;               // Execution delegate (more efficient than std::function)
            SystemMetadata metadata;                         // System metadata
        };
    public:
        /**
         * Add a system to the scheduler
         * 
         * Systems are executed in the order they are added.
         * Each system type can only be registered once.
         * 
         * For functor systems:
         * - If the system inherits from SystemTraits, dependencies are auto-detected
         * - Otherwise, the system runs sequentially (safe default)
         * 
         * @note Systems are stored by value within the scheduler. If your system
         *       contains pointers or references to external resources, you must
         *       ensure those resources remain valid for the lifetime of the scheduler.
         * 
         * @tparam T System type (must satisfy System concept)
         * @tparam Args Constructor argument types
         * @param args Arguments to forward to the system constructor
         */
        template<System T, typename... Args>
        void AddSystem(Args&&... args)
        {
            size_t typeId = TypeID<T>::Value();
            
            // Check if system type is already registered
            if (m_systemIndices.Contains(typeId))
            {
                ASTRA_ASSERT(false, "System type already registered");
                return;
            }
            
            size_t index = m_systems.size();
            m_systemIndices[typeId] = index;
            
            // Create system instance with perfect forwarding
            auto* instance = new T(std::forward<Args>(args)...);
            
            // Create initial metadata
            SystemMetadata metadata{
                .reads = ComponentMask{},
                .writes = ComponentMask{},
                .typeId = typeId,
                .insertionOrder = index
            };
            
            // Auto-detect component dependencies if the system has traits
            if constexpr (HasSystemTraits_v<T>)
            {
                ExtractSystemTraits<T>(metadata);
            }
            else
            {
                // No traits = conservative approach: assume system touches everything
                // This forces sequential execution for safety
                // Leave reads and writes empty - this triggers conservative scheduling
            }
            
            // Create entry with type erasure
            m_systems.emplace_back(SystemEntry{
                .instance = std::unique_ptr<void, void(*)(void*)>(
                    instance,
                    [](void* ptr) { delete static_cast<T*>(ptr); }
                ),
                .execute = [instance](Registry& reg) { (*instance)(reg); },
                .metadata = metadata
            });
            
            m_needsRebuild = true;
        }
        
        /**
         * Add a lambda system with automatic trait deduction
         * 
         * This overload handles lambdas with signature (Entity, Components...).
         * Component access patterns are automatically deduced from const-ness.
         * 
         * @tparam Lambda Lambda type
         * @param lambda Lambda function to use as system
         */
        template<typename Lambda>
        requires LambdaLike<Lambda>
        void AddSystem(Lambda&& lambda)
        {
            AddLambdaSystemImpl(
                std::forward<Lambda>(lambda),
                &std::decay_t<Lambda>::operator()
            );
        }
        
    private:
        // Helper to extract signature from const lambda
        template<typename Lambda, typename Ret, typename Class, typename... Args>
        void AddLambdaSystemImpl(Lambda&& lambda, Ret(Class::*)(Args...) const)
        {
            using Wrapper = LambdaSystemWrapper<std::decay_t<Lambda>, Args...>;
            AddSystemInternal<Wrapper>(Wrapper{std::forward<Lambda>(lambda)});
        }
        
        // Helper to extract signature from non-const lambda
        template<typename Lambda, typename Ret, typename Class, typename... Args>
        void AddLambdaSystemImpl(Lambda&& lambda, Ret(Class::*)(Args...))
        {
            using Wrapper = LambdaSystemWrapper<std::decay_t<Lambda>, Args...>;
            AddSystemInternal<Wrapper>(Wrapper{std::forward<Lambda>(lambda)});
        }
        
        // Internal method to add a system with known type
        template<typename SystemType>
        void AddSystemInternal(SystemType system)
        {
            size_t typeId = TypeID<SystemType>::Value();
            
            // Check if system type is already registered
            if (m_systemIndices.Contains(typeId))
            {
                ASTRA_ASSERT(false, "System type already registered");
                return;
            }
            
            size_t index = m_systems.size();
            m_systemIndices[typeId] = index;
            
            // Create system instance
            auto* instance = new SystemType(std::move(system));
            
            // Create initial metadata
            SystemMetadata metadata{
                .reads = ComponentMask{},
                .writes = ComponentMask{},
                .typeId = typeId,
                .insertionOrder = index
            };
            
            // Auto-detect component dependencies
            if constexpr (HasSystemTraits_v<SystemType>)
            {
                ExtractSystemTraits<SystemType>(metadata);
            }
            
            // Create entry with type erasure
            m_systems.emplace_back(SystemEntry{
                .instance = std::unique_ptr<void, void(*)(void*)>(
                    instance,
                    [](void* ptr) { delete static_cast<SystemType*>(ptr); }
                ),
                .execute = [instance](Registry& reg) { (*instance)(reg); },
                .metadata = metadata
            });
            
            m_needsRebuild = true;
        }
        
    public:
        /**
         * Remove a system by type
         * @tparam T System type to remove
         */
        template<System T>
        void RemoveSystem()
        {
            size_t typeId = TypeID<T>::Value();
            auto it = m_systemIndices.Find(typeId);
            if (it == m_systemIndices.end())
                return;
            
            size_t index = it->second;
            m_systems.erase(m_systems.begin() + index);
            m_systemIndices.Erase(it);
            
            // Update indices for systems after the removed one
            for (auto& [tid, idx] : m_systemIndices)
            {
                if (idx > index)
                    --idx;
            }
            
            m_needsRebuild = true;
        }
        
        /**
         * Check if a system type is registered
         * @tparam T System type to check
         * @return true if the system is registered
         */
        template<System T>
        ASTRA_NODISCARD bool HasSystem() const
        {
            return m_systemIndices.Contains(TypeID<T>::Value());
        }
        
        /**
         * Execute all systems with the default sequential executor
         * 
         * Systems are executed in registration order.
         * Without Read/Write hints, all systems run sequentially.
         * 
         * @param registry Registry to execute systems on
         */
        void Execute(Registry& registry)
        {
            static SequentialExecutor defaultExecutor;
            Execute(registry, &defaultExecutor);
        }
        
        /**
         * Execute all systems with a custom executor
         * 
         * This is the main integration point for custom job systems.
         * The executor receives pre-analyzed parallel groups and can
         * distribute work according to its threading model.
         * 
         * @param registry Registry to execute systems on
         * @param executor Custom executor implementation
         */
        void Execute(Registry& registry, ISystemExecutor* executor)
        {
            ASTRA_ASSERT(executor != nullptr, "Executor cannot be null");
            
            if (m_systems.empty())
                return;
            
            if (m_needsRebuild)
            {
                BuildExecutionPlan();
            }
            
            // Build execution context
            SystemExecutionContext context;
            context.registry = &registry;
            context.parallelGroups = m_executionPlan;
            context.systems.reserve(m_systems.size());
            context.metadata.reserve(m_systems.size());
            
            for (const auto& entry : m_systems)
            {
                context.systems.push_back(entry.execute);
                context.metadata.push_back(entry.metadata);
            }
            
            // Execute via the provided executor
            executor->Execute(context);
        }
        
        /**
         * Clear all registered systems
         */
        void Clear()
        {
            m_systems.clear();
            m_systemIndices.Clear();
            m_executionPlan.clear();
            m_needsRebuild = true;
        }
        
        /**
         * Get the number of registered systems
         */
        ASTRA_NODISCARD size_t Size() const noexcept
        {
            return m_systems.size();
        }
        
        /**
         * Check if the scheduler has any registered systems
         */
        ASTRA_NODISCARD bool Empty() const noexcept
        {
            return m_systems.empty();
        }
        
        /**
         * Get the execution plan for debugging/visualization
         * 
         * Returns the parallel groups that will be executed.
         * Each inner vector contains system indices that can run in parallel.
         * 
         * @return Vector of parallel groups
         */
        ASTRA_NODISCARD const std::vector<std::vector<size_t>>& GetExecutionPlan() const
        {
            if (m_needsRebuild)
            {
                const_cast<SystemScheduler*>(this)->BuildExecutionPlan();
            }
            return m_executionPlan;
        }
        
    private:
        /**
         * Extract component dependencies from a system with traits
         */
        template<typename T>
        void ExtractSystemTraits(SystemMetadata& metadata)
        {
            if constexpr (HasSystemTraits_v<T>)
            {
                // Extract read components
                ExtractComponentMask<typename T::ReadsComponents>(metadata.reads);
                
                // Extract write components
                ExtractComponentMask<typename T::WritesComponents>(metadata.writes);
            }
        }
        
        /**
         * Helper to convert a tuple of component types to a ComponentMask
         */
        template<typename Tuple>
        void ExtractComponentMask(ComponentMask& mask)
        {
            ExtractComponentMaskImpl<Tuple>(mask, std::make_index_sequence<std::tuple_size_v<Tuple>>{});
        }
        
        template<typename Tuple, size_t... Is>
        void ExtractComponentMaskImpl(ComponentMask& mask, std::index_sequence<Is...>)
        {
            ((mask |= MakeComponentMask<std::tuple_element_t<Is, Tuple>>()), ...);
        }
        
        /**
         * Build the execution plan based on component access patterns
         * 
         * Preserves registration order while identifying safe parallelization
         * opportunities based on Read/Write hints.
         * 
         * Uses component usage tracking to reduce redundant conflict checks.
         */
        void BuildExecutionPlan()
        {
            m_executionPlan.clear();
            
            if (m_systems.empty())
            {
                m_needsRebuild = false;
                return;
            }
            
            std::vector<bool> scheduled(m_systems.size(), false);
            
            // Process systems in insertion order
            for (size_t i = 0; i < m_systems.size(); ++i)
            {
                if (scheduled[i])
                    continue;
                
                // Start new parallel group with this system
                std::vector<size_t> group;
                group.reserve(m_systems.size() - i);  // Reserve space for potential members
                group.push_back(i);
                scheduled[i] = true;
                
                // Track component usage for the entire group
                // This allows us to check conflicts with the group as a whole
                // rather than checking against each system in the group
                const auto& sysI = m_systems[i].metadata;
                ComponentMask groupReads = sysI.reads;
                ComponentMask groupWrites = sysI.writes;
                
                // If the first system has no hints, no other system can join this group
                // This ensures conservative safety
                const bool groupAcceptsMore = !(sysI.reads.None() && sysI.writes.None());
                
                // Look ahead for systems that can run in parallel
                for (size_t j = i + 1; j < m_systems.size() && groupAcceptsMore; ++j)
                {
                    if (scheduled[j])
                        continue;
                    
                    const auto& sysJ = m_systems[j].metadata;
                    
                    // Fast conflict check against group's aggregate component usage
                    // System j conflicts with the group if:
                    // - It writes to something the group reads or writes
                    // - It reads something the group writes
                    bool conflictsWithGroup = false;
                    
                    // Check if system has no hints (conservative approach)
                    if (sysJ.reads.None() && sysJ.writes.None())
                    {
                        conflictsWithGroup = true;
                    }
                    else
                    {
                        // Check actual component conflicts using bitmasks
                        conflictsWithGroup = 
                            (sysJ.writes & groupWrites).Any() ||  // Write-write conflict
                            (sysJ.writes & groupReads).Any() ||   // Write-read conflict  
                            (sysJ.reads & groupWrites).Any();     // Read-write conflict
                    }
                    
                    if (conflictsWithGroup)
                        continue;
                    
                    // Check if j depends on any unscheduled system before it
                    // This preserves relative ordering
                    bool dependsOnEarlier = false;
                    for (size_t k = i + 1; k < j; ++k)
                    {
                        if (!scheduled[k] && HasConflict(k, j))
                        {
                            dependsOnEarlier = true;
                            break;
                        }
                    }
                    
                    if (!dependsOnEarlier)
                    {
                        // Add system to group and update group's component usage
                        group.push_back(j);
                        scheduled[j] = true;
                        groupReads |= sysJ.reads;
                        groupWrites |= sysJ.writes;
                    }
                }
                
                m_executionPlan.push_back(std::move(group));
            }
            
            m_needsRebuild = false;
        }
        
        /**
         * Check if two systems have component access conflicts
         * 
         * Systems conflict if:
         * - Both write to the same component (write-write conflict)
         * - One reads and another writes the same component (read-write conflict)
         * - Either system has no hints (conservative approach for safety)
         * 
         * @return true if systems cannot run in parallel
         */
        ASTRA_NODISCARD bool HasConflict(size_t a, size_t b) const
        {
            const auto& sysA = m_systems[a].metadata;
            const auto& sysB = m_systems[b].metadata;
            
            // Conservative: if either system has no hints, assume conflict
            // This ensures safety when users don't provide Read/Write information
            if (sysA.reads.None() && sysA.writes.None())
                return true;
            if (sysB.reads.None() && sysB.writes.None())
                return true;
            
            // Check for write-write conflicts
            if ((sysA.writes & sysB.writes).Any())
                return true;
            
            // Check for read-write conflicts
            if ((sysA.reads & sysB.writes).Any())
                return true;
            if ((sysA.writes & sysB.reads).Any())
                return true;
            
            return false;
        }
        
        std::vector<SystemEntry> m_systems;                             // All registered systems
        FlatMap<size_t, size_t> m_systemIndices;                        // TypeID value to index mapping
        mutable std::vector<std::vector<size_t>> m_executionPlan;       // Cached parallel groups
        mutable bool m_needsRebuild = true;                             // Whether execution plan needs rebuild
    };
    
} // namespace Astra