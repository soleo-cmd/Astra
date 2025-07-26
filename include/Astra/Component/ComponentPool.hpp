#pragma once

#include <atomic>
#include <memory>
#include <string_view>
#include <type_traits>
#include <vector>

#include "../Container/FlatMap.hpp"
#include "../Core/Base.hpp"
#include "../Core/Profile.hpp"
#include "../Core/Simd.hpp"
#include "../Core/TypeID.hpp"
#include "../Entity/Entity.hpp"
#include "Component.hpp"

namespace Astra
{
    // Forward declaration for friend access
    template<bool, typename...>
    class BasicView;

    /**
    * Type-erased interface for component pools.
    * Allows runtime polymorphism for component storage management.
    */
    class IComponentPool
    {
    public:
        virtual ~IComponentPool() = default;

        // Core operations
        [[nodiscard]] virtual bool Contains(Entity entity) const noexcept = 0;
        virtual bool Remove(Entity entity) noexcept = 0;
        [[nodiscard]] virtual std::size_t Size() const noexcept = 0;
        [[nodiscard]] virtual bool Empty() const noexcept = 0;
        virtual void Clear() noexcept = 0;
        virtual void Reserve(std::size_t capacity) = 0;

        // Type information
        [[nodiscard]] virtual ComponentID GetComponentID() const noexcept = 0;
        [[nodiscard]] virtual std::string_view GetComponentName() const noexcept = 0;
        [[nodiscard]] virtual std::size_t GetComponentSize() const noexcept = 0;

        // Entity access
        [[nodiscard]] virtual const Entity* GetEntities() const noexcept = 0;
        [[nodiscard]] virtual std::size_t GetEntityCount() const noexcept = 0;

        // Debug/Tools support
        virtual void ShrinkToFit() = 0;

        // Generation tracking for stream invalidation
        [[nodiscard]] virtual std::uint64_t GetGeneration() const noexcept = 0;
    };

    template<typename T>
    class ComponentPool final : public IComponentPool
    {
        static_assert(Component<T>, "T must satisfy Component concept");

    public:
        using ComponentType = T;
        using SizeType = std::size_t;

        // Allow BasicView to access internals for efficient iteration
        template<bool, typename...>
        friend class BasicView;

        ComponentPool() 
            : m_componentId(TypeID<T>::Value())
        {
            // Pre-reserve some capacity for common use cases
            m_components.Reserve(1024);
        }

        /**
        * Add a component to an entity.
        * @return Pointer to the added component, or nullptr if entity already has component
        */
        template<typename... Args>
        T* Add(Entity entity, Args&&... args)
        {
            ASTRA_PROFILE_ZONE_COLOR(Profile::ColorComponent);
            auto [it, inserted] = m_components.Emplace(entity, std::forward<Args>(args)...);
            if (inserted)
            {
                m_cacheDirty = true;
                m_generation.fetch_add(1, std::memory_order_relaxed);
                return &it->second;
            }
            return nullptr;
        }

        /**
        * Replace or add a component to an entity.
        * @return Pointer to the component
        */
        template<typename... Args>
        T* Set(Entity entity, Args&&... args)
        {
            m_cacheDirty = true;
            m_generation.fetch_add(1, std::memory_order_relaxed);
            return &(m_components[entity] = T(std::forward<Args>(args)...));
        }

        /**
        * Get component for an entity.
        * @return Pointer to component or nullptr if not found
        */
        [[nodiscard]] T* TryGet(Entity entity) noexcept
        {
            return m_components.TryGet(entity);
        }

        [[nodiscard]] const T* TryGet(Entity entity) const noexcept
        {
            return m_components.TryGet(entity);
        }

        /**
        * Get component for an entity (debug builds assert on failure).
        */
        [[nodiscard]] T& Get(Entity entity) noexcept
        {
            ASTRA_ASSERT(Contains(entity), "Entity does not have component");
            return *m_components.TryGet(entity);
        }

        [[nodiscard]] const T& Get(Entity entity) const noexcept
        {
            ASTRA_ASSERT(Contains(entity), "Entity does not have component");
            return *m_components.TryGet(entity);
        }

        // IComponentPool interface implementation
        [[nodiscard]] bool Contains(Entity entity) const noexcept override
        {
            return m_components.Contains(entity);
        }

        bool Remove(Entity entity) noexcept override
        {
            ASTRA_PROFILE_ZONE_COLOR(Profile::ColorComponent);
            if (m_components.Erase(entity) > 0)
            {
                m_cacheDirty = true;
                m_generation.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
            return false;
        }

        [[nodiscard]] std::size_t Size() const noexcept override
        {
            return m_components.Size();
        }

        [[nodiscard]] bool Empty() const noexcept override
        {
            return m_components.Empty();
        }

        void Clear() noexcept override
        {
            m_components.Clear();
            m_entityCache.clear();
            m_cacheDirty = true;
            m_generation.fetch_add(1, std::memory_order_relaxed);
        }

        void Reserve(std::size_t capacity) override
        {
            m_components.Reserve(capacity);
            m_entityCache.reserve(capacity);
        }

        [[nodiscard]] ComponentID GetComponentID() const noexcept override
        {
            return m_componentId;
        }

        [[nodiscard]] std::string_view GetComponentName() const noexcept override
        {
            return TypeName_v<T>;
        }

        [[nodiscard]] std::size_t GetComponentSize() const noexcept override
        {
            return sizeof(T);
        }

        [[nodiscard]] const Entity* GetEntities() const noexcept override
        {
            UpdateEntityCache();
            return m_entityCache.data();
        }

        [[nodiscard]] std::size_t GetEntityCount() const noexcept override
        {
            return m_components.Size();
        }

        void ShrinkToFit() override
        {
            // FlatMap doesn't have shrink_to_fit yet
            m_entityCache.shrink_to_fit();
        }

        [[nodiscard]] std::uint64_t GetGeneration() const noexcept override
        {
            return m_generation.load(std::memory_order_relaxed);
        }

        // Direct iteration over components
        [[nodiscard]] auto begin() noexcept { return m_components.begin(); }
        [[nodiscard]] auto begin() const noexcept { return m_components.begin(); }
        [[nodiscard]] auto end() noexcept { return m_components.end(); }
        [[nodiscard]] auto end() const noexcept { return m_components.end(); }

        // Group-aware iteration (leverage SwissTable groups)
        template<typename Func>
        void ForEachGroup(Func&& func)
        {
            ASTRA_PROFILE_ZONE_COLOR(Profile::ColorQuery);
            m_components.ForEachGroup([&func](auto& group_view)
                {
                    while (group_view.HasNext())
                    {
                        auto& pair = group_view.NextValue();
                        func(pair.first, pair.second);
                    }
                });
        }
        // Batch operations leveraging FlatMap's batch lookup
        void PrefetchBatch(const Entity* entities, std::size_t count) const noexcept
        {
            // Prefetch up to 16 entities at a time
            constexpr size_t BATCH_SIZE = 16;
            for (size_t i = 0; i < count; i += BATCH_SIZE)
            {
                size_t batchCount = std::min(BATCH_SIZE, count - i);
                for (size_t j = 0; j < batchCount; ++j)
                {
                    PrefetchComponent(entities[i + j]);
                }
            }
        }

        // Get components for a batch of entities
        template<size_t N>
        std::size_t GetBatch(const Entity (&entities)[N], T* (&components)[N]) noexcept
        {
            typename FlatMap<Entity, T>::iterator results[N];
            m_components.FindBatch(entities, results);

            std::size_t found = 0;
            for (std::size_t i = 0; i < N; ++i)
            {
                if (results[i] != m_components.end())
                {
                    components[found++] = &results[i]->second;
                }
            }
            return found;
        }

        // Const version of GetBatch
        template<size_t N>
        std::size_t GetBatch(const Entity (&entities)[N], const T* (&components)[N]) const noexcept
        {
            typename FlatMap<Entity, T>::const_iterator results[N];
            m_components.FindBatch(entities, results);

            std::size_t found = 0;
            for (std::size_t i = 0; i < N; ++i)
            {
                if (results[i] != m_components.end())
                {
                    components[found++] = &results[i]->second;
                }
            }
            return found;
        }

        // Prefetch component data for an entity
        void PrefetchComponent(Entity entity) const noexcept
        {
            // Prefetch the component data if it exists
            auto* component = m_components.TryGet(entity);
            if (component)
            {
                Simd::Ops::PrefetchT1(component);
            }
        }

        // Get group statistics for performance analysis
        [[nodiscard]] auto GetGroupStats() const noexcept
        {
            return m_components.GetGroupStats();
        }

        // Access to FlatMap for advanced operations
        [[nodiscard]] const FlatMap<Entity, T>& GetFlatMap() const noexcept
        {
            return m_components;
        }

        [[nodiscard]] FlatMap<Entity, T>& GetFlatMap() noexcept
        {
            return m_components;
        }

    private:
        // Private member functions
        void UpdateEntityCache() const
        {
            if (m_cacheDirty)
            {
                m_entityCache.clear();
                m_entityCache.reserve(m_components.Size());

                for (const auto& [entity, component] : m_components)
                {
                    m_entityCache.push_back(entity);
                }


                m_cacheDirty = false;
            }
        }

        // Member variables (declared last in private section)
        FlatMap<Entity, T> m_components;
        mutable std::vector<Entity> m_entityCache;
        mutable bool m_cacheDirty = true;
        ComponentID m_componentId;
        mutable std::atomic<std::uint64_t> m_generation{0};
    };
}