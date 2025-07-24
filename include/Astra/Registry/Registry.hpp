#pragma once

#include <memory>
#include <type_traits>
#include <utility>

#include "../Component/Component.hpp"
#include "../Component/ComponentPool.hpp"
#include "../Component/ComponentStorage.hpp"
#include "../Core/Config.hpp"
#include "../Entity/EntityPool.hpp"
#include "View.hpp"

namespace Astra
{
    /**
     * Central registry for managing entities and components in the ECS.
     * 
     * The Registry coordinates entity lifecycle and component management,
     * leveraging the FlatMap-based architecture for high performance.
     * 
     * Features:
     * - Entity creation/destruction with automatic version management
     * - Component addition/removal with type safety
     * - Direct component access through entity lookup
     * - Batch operations for performance
     * - No views or complex queries in this phase
     * 
     * Thread Safety: NOT thread-safe. All operations must be externally synchronized.
     */
    class Registry
    {
    public:
        Registry() = default;
        
        // Disable copy
        Registry(const Registry&) = delete;
        Registry& operator=(const Registry&) = delete;
        
        // Enable move
        Registry(Registry&&) = default;
        Registry& operator=(Registry&&) = default;
        
        /**
         * Create a new entity or recycle a destroyed one.
         * @return Valid entity with appropriate version
         */
        [[nodiscard]] Entity CreateEntity()
        {
            return m_entities.Create();
        }
        
        /**
         * Destroy an entity and remove all its components.
         * @param entity Entity to destroy
         * @return true if entity was valid and destroyed
         */
        bool DestroyEntity(Entity entity)
        {
            if (!m_entities.IsValid(entity))
            {
                return false;
            }
            
            // Remove all components for this entity
            RemoveAllComponents(entity);
            
            // Destroy the entity
            m_entities.Destroy(entity);
            return true;
        }
        
        /**
         * Check if an entity is valid (alive with correct version).
         * @param entity Entity to check
         * @return true if entity is valid
         */
        [[nodiscard]] bool Valid(Entity entity) const noexcept
        {
            return m_entities.IsValid(entity);
        }
        
        /**
         * Get the number of alive entities.
         * @return Number of alive entities
         */
        [[nodiscard]] std::size_t EntityCount() const noexcept
        {
            return m_entities.Size();
        }
        
        /**
         * Add a component to an entity.
         * @tparam T Component type
         * @param entity Entity to add component to
         * @param args Arguments to construct component
         * @return Pointer to added component, or nullptr if entity is invalid or already has component
         */
        template<typename T, typename... Args>
        T* AddComponent(Entity entity, Args&&... args)
        {
            static_assert(Component<T>, "T must satisfy Component concept");
            
            if (!m_entities.IsValid(entity))
            {
                return nullptr;
            }
            
            auto* pool = GetOrCreatePool<T>();
            return pool->Add(entity, std::forward<Args>(args)...);
        }
        
        /**
         * Set (add or replace) a component on an entity.
         * @tparam T Component type
         * @param entity Entity to set component on
         * @param args Arguments to construct component
         * @return Pointer to component, or nullptr if entity is invalid
         */
        template<typename T, typename... Args>
        T* SetComponent(Entity entity, Args&&... args)
        {
            static_assert(Component<T>, "T must satisfy Component concept");
            
            if (!m_entities.IsValid(entity))
            {
                return nullptr;
            }
            
            auto* pool = GetOrCreatePool<T>();
            return pool->Set(entity, std::forward<Args>(args)...);
        }
        
        /**
         * Get a component from an entity.
         * @tparam T Component type
         * @param entity Entity to get component from
         * @return Pointer to component, or nullptr if not found
         */
        template<typename T>
        [[nodiscard]] T* GetComponent(Entity entity) noexcept
        {
            static_assert(Component<T>, "T must satisfy Component concept");
            
            auto* pool = GetPool<T>();
            return pool ? pool->TryGet(entity) : nullptr;
        }
        
        /**
         * Get a component from an entity (const version).
         * @tparam T Component type
         * @param entity Entity to get component from
         * @return Pointer to component, or nullptr if not found
         */
        template<typename T>
        [[nodiscard]] const T* GetComponent(Entity entity) const noexcept
        {
            static_assert(Component<T>, "T must satisfy Component concept");
            
            auto* pool = GetPool<T>();
            return pool ? pool->TryGet(entity) : nullptr;
        }
        
        /**
         * Check if an entity has a component.
         * @tparam T Component type
         * @param entity Entity to check
         * @return true if entity has component
         */
        template<typename T>
        [[nodiscard]] bool HasComponent(Entity entity) const noexcept
        {
            static_assert(Component<T>, "T must satisfy Component concept");
            
            auto* pool = GetPool<T>();
            return pool && pool->Contains(entity);
        }
        
        /**
         * Remove a component from an entity.
         * @tparam T Component type
         * @param entity Entity to remove component from
         * @return true if component was removed
         */
        template<typename T>
        bool RemoveComponent(Entity entity) noexcept
        {
            static_assert(Component<T>, "T must satisfy Component concept");
            
            auto* pool = GetPool<T>();
            return pool && pool->Remove(entity);
        }
        
        /**
         * Get the component pool for a specific type.
         * @tparam T Component type
         * @return Pointer to component pool, or nullptr if not registered
         */
        template<typename T>
        [[nodiscard]] ComponentPool<T>* GetPool() noexcept
        {
            return m_components.Get<ComponentPool<T>>();
        }
        
        /**
         * Get the component pool for a specific type (const version).
         * @tparam T Component type
         * @return Pointer to component pool, or nullptr if not registered
         */
        template<typename T>
        [[nodiscard]] const ComponentPool<T>* GetPool() const noexcept
        {
            return m_components.Get<ComponentPool<T>>();
        }
        
        /**
         * Clear all entities and components.
         */
        void Clear()
        {
            // Clear all component pools
            m_components.ForEach([](ComponentID /*id*/, IComponentPool* pool) {
                pool->Clear();
            });
            
            // Clear entities
            m_entities.Clear();
        }
        
        /**
         * Reserve capacity for entities.
         * @param capacity Number of entities to reserve
         */
        void ReserveEntities(std::size_t capacity)
        {
            m_entities.Reserve(capacity);
        }
        
        /**
         * Reserve capacity for components of a specific type.
         * @tparam T Component type
         * @param capacity Number of components to reserve
         */
        template<typename T>
        void ReserveComponents(std::size_t capacity)
        {
            static_assert(Component<T>, "T must satisfy Component concept");
            
            auto* pool = GetOrCreatePool<T>();
            pool->Reserve(capacity);
        }
        
        /**
         * Create a view for entities with specific components.
         * @tparam Components Component types to query
         * @return View object for iteration
         */
        template<typename... Components>
        [[nodiscard]] View<Components...> GetView()
        {
            static_assert(sizeof...(Components) > 0, "View must query at least one component");
            static_assert((Component<Components> && ...), "All types must satisfy Component concept");
            
            return View<Components...>(GetPool<Components>()...);
        }
        
        /**
         * Create a const view for entities with specific components.
         * @tparam Components Component types to query
         * @return ConstView object for iteration
         */
        template<typename... Components>
        [[nodiscard]] ConstView<Components...> GetView() const
        {
            static_assert(sizeof...(Components) > 0, "View must query at least one component");
            static_assert((Component<Components> && ...), "All types must satisfy Component concept");
            
            // Now we properly return a ConstView with const pool pointers
            return ConstView<Components...>(GetPool<Components>()...);
        }
        
    private:
        /**
         * Get or create a component pool for type T.
         * @tparam T Component type
         * @return Non-null pointer to component pool
         */
        template<typename T>
        ComponentPool<T>* GetOrCreatePool()
        {
            return m_components.GetOrCreate<ComponentPool<T>>();
        }
        
        /**
         * Remove all components from an entity.
         * Called internally when destroying an entity.
         * @param entity Entity to remove components from
         */
        void RemoveAllComponents(Entity entity)
        {
            m_components.ForEach([entity](ComponentID /*id*/, IComponentPool* pool) {
                pool->Remove(entity);
            });
        }
        
    private:
        // Member variables (declared last in private section)
        EntityPool m_entities;
        ComponentStorage<> m_components;  // Uses default IComponentPool
    };
}