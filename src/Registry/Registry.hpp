#pragma once

#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <tuple>
#include <vector>

#include "../Component/Component.hpp"
#include "../Component/ComponentRegistry.hpp"
#include "../Container/ChunkPool.hpp"
#include "../Core/Base.hpp"
#include "../Core/Signal.hpp"
#include "../Core/TypeID.hpp"
#include "../Entity/Entity.hpp"
#include "../Entity/EntityPool.hpp"
#include "Archetype.hpp"
#include "ArchetypeStorage.hpp"
#include "Query.hpp"
#include "Relations.hpp"
#include "RelationshipGraph.hpp"
#include "View.hpp"

namespace Astra
{
    class Registry
    {
    public:
        struct Config
        {
            ChunkPool::Config poolConfig;
            bool threadSafe = false;
        };
        
        explicit Registry(const Config& config = {}) :
            m_archetypeStorage(std::make_shared<ArchetypeStorage>(config.poolConfig)),
            m_threadSafe(config.threadSafe)
        {}
        
        explicit Registry(const ChunkPool::Config& poolConfig, bool threadSafe = false) :
            m_archetypeStorage(std::make_shared<ArchetypeStorage>(poolConfig)),
            m_threadSafe(threadSafe)
        {}
        
        Registry(std::shared_ptr<ComponentRegistry> componentRegistry, const Config& config = {}) :
            m_archetypeStorage(std::make_shared<ArchetypeStorage>(componentRegistry, config.poolConfig)),
            m_threadSafe(config.threadSafe)
        {}
        
        explicit Registry(const Registry& other, const Config& config = {}) :
            m_archetypeStorage(std::make_shared<ArchetypeStorage>(other.GetComponentRegistry(), config.poolConfig)),
            m_threadSafe(config.threadSafe)
        {}
        
        Entity CreateEntity()
        {
            std::unique_lock lock(m_mutex, std::defer_lock);
            if (m_threadSafe)
                lock.lock();
            
            Entity entity = m_entityPool.Create();
            m_archetypeStorage->AddEntity(entity);
            
            // Emit signal if enabled
            m_signalManager.EmitEntityCreated({entity});
            
            return entity;
        }
        
        template<Component... Components>
        Entity CreateEntity(Components&&... components)
        {
            std::unique_lock lock(m_mutex, std::defer_lock);
            if (m_threadSafe)
                lock.lock();
            
            Entity entity = m_entityPool.Create();
            Archetype* archetype = m_archetypeStorage->GetOrCreateArchetype<Components...>();
            PackedLocation packedLocation = archetype->AddEntity(entity);
            
            ((archetype->SetComponent<Components>(packedLocation, std::forward<Components>(components))), ...);
            
            m_archetypeStorage->SetEntityLocation(entity, archetype, packedLocation);
            
            // Emit entity created signal
            m_signalManager.EmitEntityCreated({entity});
            
            // Emit component added signals for each component
            if (m_signalManager.IsSignalEnabled(Signal::ComponentAdded))
            {
                ((m_signalManager.EmitComponentAdded({entity, TypeID<Components>::Value(), archetype->GetComponent<Components>(packedLocation)})), ...);
            }
            
            return entity;
        }
        
        template<Component... Components, typename Generator>
        void CreateEntities(size_t count, std::span<Entity> outEntities, Generator&& generator)
        {
            if (count == 0 || outEntities.size() < count)
                return;
                
            std::unique_lock lock(m_mutex, std::defer_lock);
            if (m_threadSafe) 
               lock.lock();
            
            m_entityPool.CreateBatch(count, outEntities.begin());
            m_archetypeStorage->AddEntities<Components...>(outEntities.subspan(0, count), std::forward<Generator>(generator));
        }

        void DestroyEntity(Entity entity)
        {
            if (!m_entityPool.IsValid(entity))
                return;
                
            std::unique_lock lock(m_mutex, std::defer_lock);
            if (m_threadSafe)
                lock.lock();
            
            // Emit destroy signal before actually destroying
            m_signalManager.EmitEntityDestroyed({entity});
            
            m_archetypeStorage->RemoveEntity(entity);
            m_relationshipGraph.OnEntityDestroyed(entity);
            m_entityPool.Destroy(entity);
        }
        
        void DestroyEntities(std::span<Entity> entities)
        {
            if (entities.empty())
                return;
                
            std::unique_lock lock(m_mutex, std::defer_lock);
            if (m_threadSafe)
                lock.lock();
            
            // Filter out invalid entities
            std::vector<Entity> validEntities;
            validEntities.reserve(entities.size());
            
            for (Entity entity : entities)
            {
                if (m_entityPool.IsValid(entity))
                {
                    validEntities.push_back(entity);
                }
            }
            
            if (validEntities.empty())
                return;
                
            // Emit destroy signals before destroying
            if (m_signalManager.IsSignalEnabled(Signal::EntityDestroyed))
            {
                for (Entity entity : validEntities)
                {
                    m_signalManager.EmitEntityDestroyed({entity});
                }
            }
                
            // Batch remove from storage
            m_archetypeStorage->RemoveEntities(validEntities);
            
            // Remove from relationships
            for (Entity entity : validEntities)
            {
                m_relationshipGraph.OnEntityDestroyed(entity);
            }
            
            // Destroy entities in pool
            for (Entity entity : validEntities)
            {
                m_entityPool.Destroy(entity);
            }
        }

        ASTRA_NODISCARD bool IsValid(Entity entity) const noexcept
        {
            std::shared_lock lock(m_mutex, std::defer_lock);
            if (m_threadSafe)
                lock.lock();
            
            return m_entityPool.IsValid(entity);
        }

        template<Component T, typename... Args>
        T* AddComponent(Entity entity, Args&&... args)
        {
            std::unique_lock lock(m_mutex, std::defer_lock);
            if (m_threadSafe)
                lock.lock();
            
            if (!m_entityPool.IsValid(entity))
                return nullptr;
                
            T* component = m_archetypeStorage->AddComponent<T>(entity, std::forward<Args>(args)...);
            
            // Emit component added signal
            if (component)
            {
                m_signalManager.EmitComponentAdded({entity, TypeID<T>::Value(), component});
            }
            
            return component;
        }

        template<Component T>
        bool RemoveComponent(Entity entity)
        {
            std::unique_lock lock(m_mutex, std::defer_lock);
            if (m_threadSafe)
                lock.lock();
            
            if (!m_entityPool.IsValid(entity))
                return false;
                
            // Get component pointer before removal for signal
            T* component = m_archetypeStorage->GetComponent<T>(entity);
            bool removed = m_archetypeStorage->RemoveComponent<T>(entity);
            
            // Emit component removed signal
            if (removed && component)
            {
                m_signalManager.EmitComponentRemoved({entity, TypeID<T>::Value(), component});
            }
            
            return removed;
        }

        template<Component T>
        ASTRA_NODISCARD T* GetComponent(Entity entity)
        {
            std::unique_lock lock(m_mutex, std::defer_lock);
            if (m_threadSafe)
                lock.lock();
            
            if (!m_entityPool.IsValid(entity))
                return nullptr;
            return m_archetypeStorage->GetComponent<T>(entity);
        }
        
        template<Component T>
        ASTRA_NODISCARD const T* GetComponent(Entity entity) const
        {
            std::shared_lock lock(m_mutex, std::defer_lock);
            if (m_threadSafe)
                lock.lock();
            
            if (!m_entityPool.IsValid(entity))
                return nullptr;
            return m_archetypeStorage->GetComponent<T>(entity);
        }

        template<ValidQueryArg... QueryArgs>
        ASTRA_NODISCARD auto CreateView()
        {
            std::shared_lock lock(m_mutex, std::defer_lock);
            if (m_threadSafe)
                lock.lock();
            
            return View<QueryArgs...>(m_archetypeStorage);
        }

        void Clear()
        {
            std::unique_lock lock(m_mutex, std::defer_lock);
            if (m_threadSafe)
                lock.lock();
            
            // Emit destroy signals for all entities if enabled
            if (m_signalManager.IsSignalEnabled(Signal::EntityDestroyed))
            {
                // TODO: Consider if we want to emit signals during Clear()
                // For now, we skip signals as Clear() is typically used for bulk cleanup
            }
            
            // Keep the component registry when clearing
            auto componentRegistry = m_archetypeStorage->GetComponentRegistry();
            // Create new storage with the same component registry
            m_archetypeStorage = std::make_shared<ArchetypeStorage>(componentRegistry);
            // Clear relationships
            m_relationshipGraph.Clear();
            // Then clear entity pool
            m_entityPool.Clear();
            
            // Note: We don't clear signal handlers here as they may still be valid
            // for future entities. Users can manually clear handlers if needed.
        }

        ASTRA_NODISCARD std::size_t Size() const noexcept
        {
            std::shared_lock lock(m_mutex, std::defer_lock);
            if (m_threadSafe)
                lock.lock();
            
            return m_entityPool.Size();
        }

        ASTRA_NODISCARD bool IsEmpty() const noexcept
        {
            return Size() == 0;
        }
        
        ASTRA_NODISCARD const ArchetypeStorage& GetStorage() const { return *m_archetypeStorage; }
        ASTRA_NODISCARD ArchetypeStorage& GetStorage() { return *m_archetypeStorage; }

        std::shared_ptr<ComponentRegistry> GetComponentRegistry() const { return m_archetypeStorage->GetComponentRegistry(); }
        
        // ====================== Archetype Cleanup API ======================
        
        // Re-export types from ArchetypeStorage for convenience
        using CleanupOptions = ArchetypeStorage::CleanupOptions;
        using ArchetypeInfo = ArchetypeStorage::ArchetypeInfo;
        
        /**
         * Remove empty archetypes based on specified options
         * 
         * This is a lazy cleanup approach - you control when cleanup happens.
         * Call during loading screens, pause menus, or other convenient times.
         * 
         * @param options Configuration for cleanup behavior
         * @return Number of archetypes removed
         * 
         * Example usage:
         *   // Simple cleanup during loading
         *   registry.CleanupEmptyArchetypes();
         *   
         *   // Incremental cleanup during gameplay
         *   CleanupOptions options;
         *   options.maxArchetypesToRemove = 5;  // Limit per frame
         *   options.minEmptyDuration = 300;     // Empty for 5 seconds
         *   registry.CleanupEmptyArchetypes(options);
         */
        size_t CleanupEmptyArchetypes(const CleanupOptions& options = {})
        {
            std::unique_lock lock(m_mutex, std::defer_lock);
            if (m_threadSafe)
                lock.lock();
            
            // Update metrics before cleanup
            m_archetypeStorage->UpdateArchetypeMetrics();
            
            // Perform cleanup
            return m_archetypeStorage->CleanupEmptyArchetypes(options);
        }
        
        /**
         * Get statistics about all archetypes
         * Useful for debugging and monitoring fragmentation
         */
        ASTRA_NODISCARD std::vector<ArchetypeInfo> GetArchetypeStats() const
        {
            std::shared_lock lock(m_mutex, std::defer_lock);
            if (m_threadSafe)
                lock.lock();
            
            return m_archetypeStorage->GetArchetypeStats();
        }
        
        /**
         * Get current number of archetypes
         */
        ASTRA_NODISCARD size_t GetArchetypeCount() const
        {
            std::shared_lock lock(m_mutex, std::defer_lock);
            if (m_threadSafe)
                lock.lock();
            
            return m_archetypeStorage->GetArchetypeCount();
        }
        
        /**
         * Get approximate memory usage by all archetypes
         */
        ASTRA_NODISCARD size_t GetArchetypeMemoryUsage() const
        {
            std::shared_lock lock(m_mutex, std::defer_lock);
            if (m_threadSafe)
                lock.lock();
            
            return m_archetypeStorage->GetArchetypeMemoryUsage();
        }
        
        // ====================== Relationship API ======================
        
        /**
         * Get a Relations query object for the given entity
         * 
         * @tparam QueryArgs Query arguments (components and modifiers like Not<T>, AnyOf<T...>, etc.)
         * @param entity The entity to query relationships for
         * @return Relations object for querying relationships
         */
        template<typename... QueryArgs>
        ASTRA_NODISCARD Relations<QueryArgs...> GetRelations(Entity entity) const
        {
            std::shared_lock lock(m_mutex, std::defer_lock);
            if (m_threadSafe)
                lock.lock();
            
            return Relations<QueryArgs...>(this, entity, &m_relationshipGraph);
        }
        
        /**
         * Set the parent of an entity
         * 
         * @param child The child entity
         * @param parent The parent entity
         */
        void SetParent(Entity child, Entity parent)
        {
            std::unique_lock lock(m_mutex, std::defer_lock);
            if (m_threadSafe)
                lock.lock();
            
            if (m_entityPool.IsValid(child) && m_entityPool.IsValid(parent))
            {
                m_relationshipGraph.SetParent(child, parent);
                
                // Emit parent changed signal
                m_signalManager.EmitParentChanged({child, parent});
            }
        }
        
        /**
         * Remove the parent of an entity
         * 
         * @param child The child entity
         */
        void RemoveParent(Entity child)
        {
            std::unique_lock lock(m_mutex, std::defer_lock);
            if (m_threadSafe)
                lock.lock();
            
            if (m_entityPool.IsValid(child))
            {
                // Get current parent before removal for signal
                Entity parent = m_relationshipGraph.GetParent(child);
                m_relationshipGraph.RemoveParent(child);
                
                // Emit parent changed signal (parent is now invalid)
                if (parent.IsValid())
                {
                    m_signalManager.EmitParentChanged({child, Entity()});
                }
            }
        }
        
        /**
         * Add a bidirectional link between two entities
         * 
         * @param a First entity
         * @param b Second entity
         */
        void AddLink(Entity a, Entity b)
        {
            std::unique_lock lock(m_mutex, std::defer_lock);
            if (m_threadSafe)
                lock.lock();
            
            if (m_entityPool.IsValid(a) && m_entityPool.IsValid(b))
            {
                m_relationshipGraph.AddLink(a, b);
                
                // Emit link added signal
                m_signalManager.EmitLinkAdded({a, b});
            }
        }
        
        /**
         * Remove a link between two entities
         * 
         * @param a First entity
         * @param b Second entity
         */
        void RemoveLink(Entity a, Entity b)
        {
            std::unique_lock lock(m_mutex, std::defer_lock);
            if (m_threadSafe)
                lock.lock();
            
            if (m_entityPool.IsValid(a) && m_entityPool.IsValid(b))
            {
                m_relationshipGraph.RemoveLink(a, b);
                
                // Emit link removed signal
                m_signalManager.EmitLinkRemoved({a, b});
            }
        }
        
        /**
         * Get direct access to the relationship graph (internal use)
         * 
         * @return Reference to the relationship graph
         */
        ASTRA_NODISCARD RelationshipGraph& GetRelationshipGraph() { return m_relationshipGraph; }
        ASTRA_NODISCARD const RelationshipGraph& GetRelationshipGraph() const { return m_relationshipGraph; }
        
        // ====================== Signal API ======================
        
        /**
         * Enable signals for specific events
         * 
         * @param signals Bitwise OR of Signal flags to enable
         * 
         * Example:
         *   registry.EnableSignals(Signal::EntityCreated | Signal::ComponentAdded);
         */
        void EnableSignals(Signal signals)
        {
            std::unique_lock lock(m_mutex, std::defer_lock);
            if (m_threadSafe)
                lock.lock();
            
            m_signalManager.EnableSignals(signals);
        }
        
        /**
         * Disable signals for specific events
         * 
         * @param signals Bitwise OR of Signal flags to disable
         */
        void DisableSignals(Signal signals)
        {
            std::unique_lock lock(m_mutex, std::defer_lock);
            if (m_threadSafe)
                lock.lock();
            
            m_signalManager.DisableSignals(signals);
        }
        
        /**
         * Set exact signal configuration
         * 
         * @param signals Exact set of signals to enable (all others will be disabled)
         */
        void SetEnabledSignals(Signal signals)
        {
            std::unique_lock lock(m_mutex, std::defer_lock);
            if (m_threadSafe)
                lock.lock();
            
            m_signalManager.SetEnabledSignals(signals);
        }
        
        /**
         * Get currently enabled signals
         * 
         * @return Bitwise OR of currently enabled Signal flags
         */
        ASTRA_NODISCARD Signal GetEnabledSignals() const
        {
            std::shared_lock lock(m_mutex, std::defer_lock);
            if (m_threadSafe)
                lock.lock();
            
            return m_signalManager.GetEnabledSignals();
        }
        
        /**
         * Get the signal manager for connecting handlers
         * 
         * Note: When thread safety is enabled, you should hold a lock
         * while connecting/disconnecting handlers to ensure thread safety.
         * 
         * Example:
         *   auto& signals = registry.GetSignalManager();
         *   signals.OnEntityCreated().Add([](const EntityEvent& e) { ... });
         * 
         * @return Reference to the signal manager
         */
        SignalManager& GetSignalManager()
        {
            // Note: We don't lock here as the caller may need to perform
            // multiple operations. The caller is responsible for thread safety
            // when connecting/disconnecting handlers in a multithreaded environment.
            return m_signalManager;
        }
        
        const SignalManager& GetSignalManager() const
        {
            return m_signalManager;
        }
        
    private:
        EntityPool m_entityPool;
        std::shared_ptr<ArchetypeStorage> m_archetypeStorage;
        RelationshipGraph m_relationshipGraph;
        SignalManager m_signalManager;

        mutable std::shared_mutex m_mutex;
        bool m_threadSafe;
    };
}