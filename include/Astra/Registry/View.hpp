#pragma once

#include <algorithm>
#include <array>
#include <tuple>
#include <type_traits>
#include <chrono>
#include <memory>

#include "../Component/ComponentPool.hpp"
#include "../Component/ComponentStream.hpp"
#include "../Component/ComponentStreamView.hpp"
#include "../Core/Profile.hpp"
#include "../Entity/Entity.hpp"

namespace Astra
{
    /**
    * View provides efficient iteration over entities with specific component combinations.
    * 
    * Leverages the FlatMap's group-based architecture for optimal performance:
    * - Group-level metadata scanning for rapid filtering
    * - Cache-friendly iteration through 16-element groups
    * - SIMD-ready processing with aligned data access
    * - Natural parallelization boundaries at group level
    * 
    * Views are lightweight objects that can be created on-demand and don't
    * own any data - they simply provide a filtered iteration interface.
    * 
    * Supports both const and mutable iteration through proper iterator types.
    * 
    * @tparam IsConst Whether this is a const view
    * @tparam Components Component types to query
    */
    template<bool IsConst, typename... Components>
    class BasicView
    {
    public:
        static constexpr std::size_t ComponentCount = sizeof...(Components);

        // Choose const or non-const pool pointers based on IsConst
        template<typename T>
        using PoolPtr = std::conditional_t<IsConst, const ComponentPool<T>*, ComponentPool<T>*>;

        // Choose const or non-const component pointers based on IsConst
        template<typename T>
        using ComponentPtr = std::conditional_t<IsConst, const T*, T*>;

        using PoolTuple = std::tuple<PoolPtr<Components>...>;
        using PoolInterface = std::conditional_t<IsConst, const IComponentPool*, IComponentPool*>;
        using StreamType = ComponentStream<Components...>;
        using StreamViewType = ComponentStreamView<Components...>;

        /**
        * Construct a view from component pools.
        * @param pools Pointers to component pools (can be nullptr)
        */
        explicit BasicView(PoolPtr<Components>... pools) noexcept
            : m_pools(pools...)
        {
        }


        /**
        * Base iterator template for multi-component views.
        * Uses entity iteration from the base pool.
        */
        template<bool IsConstIter>
        class iterator_base
        {
        private:
            using ViewPtr = std::conditional_t<IsConstIter, const BasicView*, BasicView*>;
            using BasePoolPtr = std::conditional_t<IsConstIter, const IComponentPool*, IComponentPool*>;

            ViewPtr m_view;
            BasePoolPtr m_basePool;
            const Entity* m_current;
            const Entity* m_end;

            // Current entity and components
            Entity m_currentEntity;
            std::tuple<typename BasicView::template ComponentPtr<Components>...> m_currentComponents;
            bool m_valid = false;

            // Advance to next valid entity
            void AdvanceToValid() noexcept
            {
                ASTRA_PROFILE_ZONE_COLOR(Profile::ColorView);
                while (m_current != m_end)
                {
                    m_currentEntity = *m_current;

                    // Try to get all components
                    if (TryGetComponents())
                    {
                        m_valid = true;
                        return;
                    }

                    ++m_current;
                }

                m_valid = false;
                m_currentEntity = Entity::Null();
            }

            // Try to get all components for current entity
            bool TryGetComponents() noexcept
            {
                return TryGetComponentsImpl(std::index_sequence_for<Components...>{});
            }

            template<std::size_t... Is>
            bool TryGetComponentsImpl(std::index_sequence<Is...>) noexcept
            {
                // Helper lambda to check and get component
                auto check_component = [this](auto poolIndex) -> bool {
                    auto* pool = std::get<poolIndex.value>(m_view->m_pools);
                    if (!pool) return false;

                    std::get<poolIndex.value>(m_currentComponents) = pool->TryGet(m_currentEntity);
                    return std::get<poolIndex.value>(m_currentComponents) != nullptr;
                    };

                // Use simple fold expression with && operator
                return (... && check_component(std::integral_constant<std::size_t, Is>{}));
            }

        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = std::conditional_t<IsConstIter,
                std::tuple<Entity, const Components&...>,
                std::tuple<Entity, Components&...>>;
            using difference_type = std::ptrdiff_t;
            using pointer = value_type*;
            using reference = value_type;

            iterator_base() noexcept : m_view(nullptr), m_basePool(nullptr), m_current(nullptr), m_end(nullptr), m_valid(false) {}

            iterator_base(ViewPtr view, BasePoolPtr base_pool, const Entity* current, const Entity* end) noexcept
                : m_view(view), m_basePool(base_pool), m_current(current), m_end(end)
            {
                if (m_current != m_end)
                {
                    AdvanceToValid();
                }
            }

            [[nodiscard]] reference operator*() const noexcept
            {
                return std::apply([this](auto*... components) {
                    return std::forward_as_tuple(m_currentEntity, *components...);
                    }, m_currentComponents);
            }

            iterator_base& operator++() noexcept
            {
                if (m_current != m_end)
                {
                    ++m_current;
                    if (m_current != m_end)
                    {
                        AdvanceToValid();
                    }
                    else
                    {
                        m_valid = false;
                    }
                }
                return *this;
            }

            iterator_base operator++(int) noexcept
            {
                iterator_base tmp = *this;
                ++(*this);
                return tmp;
            }

            [[nodiscard]] bool operator==(const iterator_base& other) const noexcept
            {
                return (!m_valid && !other.m_valid) || 
                    (m_valid && other.m_valid && m_current == other.m_current);
            }

            [[nodiscard]] bool operator!=(const iterator_base& other) const noexcept
            {
                return !(*this == other);
            }
        };

        using iterator = iterator_base<false>;
        using const_iterator = iterator_base<true>;
        using stream_iterator = typename StreamViewType::iterator;
        using const_stream_iterator = typename StreamViewType::const_iterator;

        /**
        * Get streaming iterator (default for range-based for loops).
        * Automatically builds stream if needed.
        */
        [[nodiscard]] auto begin() noexcept
        {
            // For multi-component views, use streaming by default
            if constexpr (sizeof...(Components) > 1)
            {
                if (!m_stream || NeedsStreamRebuild())
                {
                    RebuildStream();
                }
                return stream_iterator(m_stream);
            }
            else
            {
                // Single component views use direct iteration
                return begin_traditional();
            }
        }

        [[nodiscard]] auto end() noexcept
        {
            if constexpr (sizeof...(Components) > 1)
            {
                // Return a default-constructed iterator which represents end
                return stream_iterator();
            }
            else
            {
                return end_traditional();
            }
        }

        [[nodiscard]] auto begin() const noexcept
        {
            if constexpr (sizeof...(Components) > 1)
            {
                if (!m_stream || NeedsStreamRebuild())
                {
                    const_cast<BasicView*>(this)->RebuildStream();
                }
                return const_stream_iterator(m_stream);
            }
            else
            {
                return begin_traditional();
            }
        }

        [[nodiscard]] auto end() const noexcept
        {
            if constexpr (sizeof...(Components) > 1)
            {
                // Return a default-constructed iterator which represents end
                return const_stream_iterator();
            }
            else
            {
                return end_traditional();
            }
        }

        /**
        * Get traditional iterator (uses hash lookups).
        * Useful when you only iterate once or components change frequently.
        */
        [[nodiscard]] auto begin_traditional() noexcept
        {
            auto smallest = GetSmallestPool();
            if (!smallest || smallest->Empty())
            {
                return end_traditional();
            }

            // Use the entity array from the smallest pool
            const Entity* entities = smallest->GetEntities();
            const Entity* entitiesEnd = entities + smallest->GetEntityCount();

            if constexpr (IsConst)
            {
                return const_iterator(this, smallest, entities, entitiesEnd);
            }
            else
            {
                return iterator(this, smallest, entities, entitiesEnd);
            }
        }

        [[nodiscard]] auto begin_traditional() const noexcept
        {
            auto smallest = GetSmallestPool();
            if (!smallest || smallest->Empty())
            {
                return end_traditional();
            }

            // Use the entity array from the smallest pool
            const Entity* entities = smallest->GetEntities();
            const Entity* entitiesEnd = entities + smallest->GetEntityCount();

            return const_iterator(this, smallest, entities, entitiesEnd);
        }

        /**
        * Get traditional end iterator.
        */
        [[nodiscard]] auto end_traditional() noexcept
        {
            if constexpr (IsConst)
            {
                return const_iterator();
            }
            else
            {
                return iterator();
            }
        }

        [[nodiscard]] auto end_traditional() const noexcept
        {
            return const_iterator();
        }

        /**
        * Check if view has no matching entities.
        */
        [[nodiscard]] bool empty() const noexcept
        {
            auto* smallest = GetSmallestPool();
            return !smallest || smallest->Empty();
        }

        /**
        * Get approximate size (upper bound).
        * Actual size may be smaller due to component filtering.
        */
        [[nodiscard]] std::size_t SizeHint() const noexcept
        {
            auto* smallest = GetSmallestPool();
            return smallest ? smallest->Size() : 0;
        }

        /**
        * Process all matching entities with a callback using traditional iteration.
        * Uses hash lookups for each component access.
        * @param func Callback taking (Entity, Components&...)
        */
        template<typename Func>
        void ForEach(Func&& func)
        {
            ASTRA_PROFILE_ZONE_COLOR(Profile::ColorView);
            for (auto it = begin_traditional(); it != end_traditional(); ++it)
            {
                std::apply([&func](Entity entity, Components&... comps) {
                    func(entity, comps...);
                    }, *it);
            }
        }

        /**
        * Process matching entities in groups for optimal cache usage.
        * Provides fixed-size batches of 16 for consistent API behavior.
        * @param func Callback taking arrays of entities and component pointers
        */
        template<typename Func>
        void ForEachGroup(Func&& func)
        {
            ASTRA_PROFILE_ZONE_COLOR(Profile::ColorView);
            auto smallest = GetSmallestPool();
            if (!smallest || smallest->Empty())
            {
                return;
            }

            // Get entities from the smallest pool
            const Entity* entities = smallest->GetEntities();
            const std::size_t entityCount = smallest->GetEntityCount();

            // Process in batches of 16 for SIMD alignment
            constexpr std::size_t BATCH_SIZE = 16;

            // Allocate aligned memory for better cache performance
            alignas(64) Entity entityBatch[BATCH_SIZE];
            alignas(64) std::tuple<std::array<ComponentPtr<Components>, BATCH_SIZE>...> componentBatches;

            // Track valid entities within each batch
            std::size_t validCount = 0;
            Entity validEntities[BATCH_SIZE];
            std::tuple<std::array<ComponentPtr<Components>, BATCH_SIZE>...> validComponents;

            for (std::size_t i = 0; i < entityCount; i += BATCH_SIZE)
            {
                std::size_t batchSize = std::min(BATCH_SIZE, entityCount - i);

                // Copy entities for this batch
                std::copy_n(entities + i, batchSize, entityBatch);

                // Prefetch all pools for this batch
                PrefetchAllPools(entityBatch, batchSize);

                // Process each entity in the batch
                validCount = 0;
                for (std::size_t j = 0; j < batchSize; ++j)
                {
                    if (CheckAndGetComponents(entityBatch[j], j, componentBatches, 
                        std::index_sequence_for<Components...>{}))
                    {
                        validEntities[validCount] = entityBatch[j];
                        CopyComponentPointers(validCount, j, validComponents, componentBatches,
                            std::index_sequence_for<Components...>{});
                        validCount++;
                    }

                    // Process when we have a full batch of valid entities
                    if (validCount == BATCH_SIZE)
                    {
                        CallFuncWithBatch(std::forward<Func>(func), validEntities, BATCH_SIZE, 
                            validComponents, std::index_sequence_for<Components...>{});
                        validCount = 0;
                    }
                }
            }

            // Process remaining valid entities
            if (validCount > 0)
            {
                CallFuncWithBatch(std::forward<Func>(func), validEntities, validCount, 
                    validComponents, std::index_sequence_for<Components...>{});
            }
        }

        /**
        * Create an optimized stream for multi-component iteration.
        * 
        * The stream pre-gathers component pointers into cache-friendly blocks,
        * eliminating cache misses during iteration. The stream build cost is
        * amortized over multiple iterations.
        * 
        * Usage:
        * ```cpp
        * auto view = registry.GetView<Position, Velocity>();
        * auto stream = view.Stream();
        * 
        * // Iterate multiple times efficiently
        * stream.ForEach([](Entity e, Position& p, Velocity& v) { ... });
        * stream.ForEach([](Entity e, Position& p, Velocity& v) { ... });
        * ```
        * 
        * @return StreamView object for iteration
        */
        [[nodiscard]] StreamViewType Stream()
        {
            // Create stream if needed or if generations changed
            if (!m_stream || NeedsStreamRebuild())
            {
                RebuildStream();
            }

            // Return a view that shares ownership of the stream
            return StreamViewType(m_stream, true);
        }

    private:
        // Check if stream needs rebuilding by comparing pool generations
        [[nodiscard]] bool NeedsStreamRebuild() const noexcept
        {
            bool needsRebuild = false;
            std::size_t idx = 0;

            std::apply([&](auto*... pools) {
                (([&] {
                    if (pools && pools->GetGeneration() != m_poolGenerations[idx]) {
                        needsRebuild = true;
                    }
                    idx++;
                    }(), void()), ...);
                }, m_pools);

            return needsRebuild;
        }

        // Rebuild the component stream
        void RebuildStream()
        {
            ASTRA_PROFILE_ZONE_NAMED_COLOR("View::RebuildStream", Profile::ColorView);
            if (!m_stream)
            {
                m_stream = std::make_shared<StreamType>();
            }

            std::apply([this](auto*... pools) {
                m_stream->FillStream(pools...);
                }, m_pools);

            // Capture current generations
            std::size_t idx = 0;
            std::apply([&](auto*... pools) {
                (([&] {
                    if (pools) {
                        m_poolGenerations[idx] = pools->GetGeneration();
                    }
                    idx++;
                    }(), void()), ...);
                }, m_pools);
        }

    private:
        // New optimized group processing using direct FlatMap access
        template<typename Func, std::size_t... Is>
        void ProcessGroupsDirect(PoolInterface smallestPool, std::size_t smallestIdx, 
            Func&& func, std::index_sequence<Is...>)
        {
            // Helper to get the pool at a specific index
            auto getPool = [this](std::size_t idx) -> PoolInterface {
                PoolInterface result = nullptr;
                std::size_t currentIdx = 0;
                std::apply([&](auto*... pools) {
                    ((currentIdx++ == idx ? result = pools : nullptr), ...);
                    }, m_pools);
                return result;
                };

            // Get the typed pool for direct group access
            if (smallestIdx == 0)
            {
                using FirstComponent = typename std::tuple_element<0, std::tuple<Components...>>::type;
                auto* typedPool = static_cast<PoolPtr<FirstComponent>>(getPool(0));

                typedPool->ForEachGroupDirect([&](const std::uint8_t* metadata,
                    const Entity* entities,
                    const FirstComponent* const* components,
                    std::size_t count,
                    std::uint16_t occupiedMask)
                    {
                        ProcessGroupWithOtherPools<FirstComponent>(
                            entities, components, count, occupiedMask, 
                            std::forward<Func>(func), std::index_sequence_for<Components...>{});
                    });
            }
            // Handle other component types similarly...
            // For now, fall back to the old implementation for other cases
            else
            {
                // Fallback to entity-based iteration
                const Entity* entities = smallestPool->GetEntities();
                const std::size_t entityCount = smallestPool->GetEntityCount();

                constexpr std::size_t BATCH_SIZE = 16;
                for (std::size_t i = 0; i < entityCount; i += BATCH_SIZE)
                {
                    std::size_t batchSize = std::min(BATCH_SIZE, entityCount - i);
                    Entity entityBatch[BATCH_SIZE];
                    std::tuple<std::array<ComponentPtr<Components>, BATCH_SIZE>...> componentBatches;

                    std::copy_n(entities + i, batchSize, entityBatch);
                    bool allValid = ProcessBatch(entityBatch, batchSize, componentBatches);

                    if (allValid)
                    {
                        CallFuncWithBatch(std::forward<Func>(func), entityBatch, batchSize, 
                            componentBatches, std::index_sequence_for<Components...>{});
                    }
                }
            }
        }

        // Process a group with checking other pools
        template<typename FirstComponent, typename Func, std::size_t... Is>
        void ProcessGroupWithOtherPools(const Entity* entities,
            const FirstComponent* const* firstComponents,
            std::size_t count,
            std::uint16_t occupiedMask,
            Func&& func,
            std::index_sequence<Is...>)
        {
            // Arrays to store components from all pools
            std::tuple<std::array<ComponentPtr<Components>, 16>...> allComponents;

            // Set first component array
            auto& firstArray = std::get<0>(allComponents);
            for (std::size_t i = 0; i < count && i < 16; ++i)
            {
                firstArray[i] = const_cast<FirstComponent*>(firstComponents[i]);
            }

            // Check other pools and get components
            bool allValid = CheckOtherPools(entities, count, allComponents, 
                std::integral_constant<std::size_t, 1>{}, 
                std::index_sequence_for<Components...>{});

            if (allValid)
            {
                // Call user function with valid entities and components
                func(entities, count, std::get<Is>(allComponents).data()...);
            }
        }

        // Check remaining pools starting from index StartIdx
        template<typename ComponentArrays, std::size_t StartIdx, std::size_t... Is>
        bool CheckOtherPools(const Entity* entities, std::size_t count, 
            ComponentArrays& arrays, std::integral_constant<std::size_t, StartIdx>,
            std::index_sequence<Is...>)
        {
            if constexpr (StartIdx >= sizeof...(Components))
            {
                return true;  // All pools checked
            }
            else
            {
                auto* pool = std::get<StartIdx>(m_pools);
                if (!pool) return false;

                auto& array = std::get<StartIdx>(arrays);

                // Get components for these entities
                Entity entityBatch[16];
                std::copy_n(entities, std::min(count, std::size_t(16)), entityBatch);

                if constexpr (IsConst)
                {
                    using ComponentType = typename std::tuple_element<StartIdx, std::tuple<Components...>>::type;
                    const ComponentType* components[16];
                    std::size_t found = pool->GetBatch(entityBatch, components);

                    if (found != count) return false;

                    for (std::size_t i = 0; i < count; ++i)
                    {
                        array[i] = components[i];
                    }
                }
                else
                {
                    using ComponentType = typename std::tuple_element<StartIdx, std::tuple<Components...>>::type;
                    ComponentType* components[16];
                    std::size_t found = pool->GetBatch(entityBatch, components);

                    if (found != count) return false;

                    for (std::size_t i = 0; i < count; ++i)
                    {
                        array[i] = components[i];
                    }
                }

                // Check next pool
                return CheckOtherPools(entities, count, arrays, 
                    std::integral_constant<std::size_t, StartIdx + 1>{},
                    std::index_sequence<Is...>{});
            }
        }

        // Helper to process a batch of entities and get their components
        template<typename ComponentBatches>
        bool ProcessBatch(const Entity* entities, std::size_t batchSize, ComponentBatches& batches) noexcept
        {
            return ProcessBatchImpl(entities, batchSize, batches, std::index_sequence_for<Components...>{});
        }

        template<typename ComponentBatches, std::size_t... Is>
        bool ProcessBatchImpl(const Entity* entities, std::size_t batchSize, 
            ComponentBatches& batches, std::index_sequence<Is...>) noexcept
        {
            // Helper to use the pool's optimized batch lookup
            auto processPool = [&](auto poolIndex) -> std::size_t {
                auto* pool = std::get<poolIndex.value>(m_pools);
                if (!pool) return 0;

                auto& batchArray = std::get<poolIndex.value>(batches);

                // Use pool's optimized GetBatch method which leverages FindBatch
                Entity entityArray[16];
                std::copy_n(entities, batchSize, entityArray);

                using ComponentType = typename std::tuple_element<poolIndex.value, std::tuple<Components...>>::type;

                if constexpr (IsConst)
                {
                    // For const views, use const GetBatch
                    const ComponentType* componentArray[16];
                    std::size_t found = pool->GetBatch(entityArray, componentArray);

                    // Copy results to our batch array
                    for (std::size_t j = 0; j < batchSize; ++j)
                    {
                        batchArray[j] = (j < found) ? componentArray[j] : nullptr;
                    }
                    return found;
                }
                else
                {
                    // For non-const views, use non-const GetBatch
                    ComponentType* componentArray[16];
                    std::size_t found = pool->GetBatch(entityArray, componentArray);

                    // Copy results to our batch array
                    for (std::size_t j = 0; j < batchSize; ++j)
                    {
                        batchArray[j] = (j < found) ? componentArray[j] : nullptr;
                    }
                    return found;
                }
                };

            // Check all pools - all must return batchSize for success
            std::size_t minFound = batchSize;
            ((minFound = std::min(minFound, processPool(std::integral_constant<std::size_t, Is>{}))), ...);

            return minFound == batchSize;
        }

        // Helper to call the user function with batch data
        template<typename Func, typename ComponentBatches, std::size_t... Is>
        void CallFuncWithBatch(Func&& func, const Entity* entities, std::size_t batchSize,
            ComponentBatches& batches, std::index_sequence<Is...>)
        {
            // Extract component arrays and call function
            func(entities, batchSize, std::get<Is>(batches).data()...);
        }

    private:
        // Helper methods for optimized group processing
        void PrefetchAllPools(const Entity* entities, std::size_t count)
        {
            std::apply([&](auto*... pools) {
                ((pools ? pools->PrefetchBatch(entities, count) : void()), ...);
                }, m_pools);
        }

        template<typename ComponentBatches, std::size_t... Is>
        bool CheckAndGetComponents(Entity entity, std::size_t idx, ComponentBatches& batches,
            std::index_sequence<Is...>)
        {
            return (... && CheckSingleComponent<Is>(entity, idx, batches));
        }

        template<std::size_t I, typename ComponentBatches>
        bool CheckSingleComponent(Entity entity, std::size_t idx, ComponentBatches& batches)
        {
            auto* pool = std::get<I>(m_pools);
            if (!pool) return false;

            auto* comp = pool->TryGet(entity);
            if (!comp) return false;

            std::get<I>(batches)[idx] = comp;
            return true;
        }

        template<typename ValidComps, typename ComponentBatches, std::size_t... Is>
        void CopyComponentPointers(std::size_t validIdx, std::size_t srcIdx, 
            ValidComps& valid, const ComponentBatches& batches,
            std::index_sequence<Is...>)
        {
            ((std::get<Is>(valid)[validIdx] = std::get<Is>(batches)[srcIdx]), ...);
        }

        // Private member functions
        [[nodiscard]] PoolInterface GetSmallestPool() const noexcept
        {
            // Check if any pool is null - if so, return nullptr (no matches possible)
            bool anyNull = false;
            std::apply([&](auto*... pools) {
                ((pools == nullptr ? anyNull = true : false), ...);
                }, m_pools);

            if (anyNull)
            {
                return nullptr;
            }

            PoolInterface smallest = nullptr;
            std::size_t minSize = std::numeric_limits<std::size_t>::max();

            std::apply([&](auto*... pools) {
                ((pools && pools->Size() < minSize ? (smallest = pools, minSize = pools->Size()) : false), ...);
                }, m_pools);

            return smallest;
        }

        // Member variables (declared last in private section)
        PoolTuple m_pools;
        mutable std::shared_ptr<StreamType> m_stream;
        mutable std::array<std::uint64_t, sizeof...(Components)> m_poolGenerations{};
    };

    // Convenience aliases
    template<typename... Components>
    using View = BasicView<false, Components...>;

    template<typename... Components>
    using ConstView = BasicView<true, Components...>;

    // Single component specialization for optimal performance
    template<bool IsConst, typename Component>
    class BasicView<IsConst, Component>
    {
    public:
        using PoolPtr = std::conditional_t<IsConst, const ComponentPool<Component>*, ComponentPool<Component>*>;
        explicit BasicView(PoolPtr pool) noexcept : m_pool(pool) {}

        // For single component views, we can directly use the FlatMap's iterator
        using iterator = std::conditional_t<IsConst,
            typename FlatMap<Entity, Component>::const_iterator,
            typename FlatMap<Entity, Component>::iterator>;
        using const_iterator = typename FlatMap<Entity, Component>::const_iterator;

        [[nodiscard]] auto begin() noexcept
        {
            if constexpr (IsConst)
            {
                return m_pool ? m_pool->begin() : const_iterator();
            }
            else
            {
                return m_pool ? m_pool->begin() : iterator();
            }
        }

        [[nodiscard]] auto begin() const noexcept
        {
            return m_pool ? m_pool->begin() : const_iterator();
        }

        [[nodiscard]] auto end() noexcept
        {
            if constexpr (IsConst)
            {
                return m_pool ? m_pool->end() : const_iterator();
            }
            else
            {
                return m_pool ? m_pool->end() : iterator();
            }
        }

        [[nodiscard]] auto end() const noexcept
        {
            return m_pool ? m_pool->end() : const_iterator();
        }

        [[nodiscard]] bool empty() const noexcept
        {
            return !m_pool || m_pool->Empty();
        }

        [[nodiscard]] std::size_t size() const noexcept
        {
            return m_pool ? m_pool->Size() : 0;
        }

        template<typename Func>
        void ForEach(Func&& func)
        {
            ASTRA_PROFILE_ZONE_COLOR(Profile::ColorView);
            if (m_pool)
            {
                for (auto& [entity, component] : *m_pool)
                {
                    func(entity, component);
                }
            }
        }

        template<typename Func>
        void ForEachGroup(Func&& func)
        {
            if (m_pool)
            {
                m_pool->ForEachGroup(std::forward<Func>(func));
            }
        }

    private:
        // Member variables (declared last in private section)
        PoolPtr m_pool;
    };
}