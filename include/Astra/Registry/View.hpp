#pragma once

#include <algorithm>
#include <array>
#include <tuple>
#include <type_traits>

#include "../Component/ComponentPool.hpp"
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
        
        /**
         * Get iterator to first entity matching all components.
         */
        [[nodiscard]] auto begin() noexcept
        {
            auto smallest = GetSmallestPool();
            if (!smallest || smallest->Empty())
            {
                return end();
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
        
        [[nodiscard]] auto begin() const noexcept
        {
            auto smallest = GetSmallestPool();
            if (!smallest || smallest->Empty())
            {
                return end();
            }
            
            // Use the entity array from the smallest pool
            const Entity* entities = smallest->GetEntities();
            const Entity* entitiesEnd = entities + smallest->GetEntityCount();
            
            return const_iterator(this, smallest, entities, entitiesEnd);
        }
        
        /**
         * Get end iterator.
         */
        [[nodiscard]] auto end() noexcept
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
        
        [[nodiscard]] auto end() const noexcept
        {
            return const_iterator();
        }
        
        /**
         * Check if view has no matching entities.
         */
        [[nodiscard]] bool empty() const noexcept
        {
            return begin() == end();
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
         * Process all matching entities with a callback.
         * @param func Callback taking (Entity, Components&...)
         */
        template<typename Func>
        void ForEach(Func&& func)
        {
            for (auto it = begin(); it != end(); ++it)
            {
                std::apply([&func](Entity entity, Components&... comps) {
                    func(entity, comps...);
                }, *it);
            }
        }
        
        /**
         * Process matching entities in groups for optimal cache usage.
         * Uses batch operations to prefetch components from all pools.
         * @param func Callback taking arrays of entities and component pointers
         */
        template<typename Func>
        void ForEachGroup(Func&& func)
        {
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
            
            for (std::size_t i = 0; i < entityCount; i += BATCH_SIZE)
            {
                std::size_t batchSize = std::min(BATCH_SIZE, entityCount - i);
                
                // Arrays to hold the batch data
                Entity entityBatch[BATCH_SIZE];
                std::tuple<std::array<ComponentPtr<Components>, BATCH_SIZE>...> componentBatches;
                
                // Copy entities for this batch
                std::copy_n(entities + i, batchSize, entityBatch);
                
                // Use FindBatch to get components from all pools
                bool allValid = ProcessBatch(entityBatch, batchSize, componentBatches);
                
                if (allValid)
                {
                    // Call the function with the batch data
                    CallFuncWithBatch(std::forward<Func>(func), entityBatch, batchSize, 
                                      componentBatches, std::index_sequence_for<Components...>{});
                }
            }
        }
        
    private:
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
                // Note: We need to handle the case where batchSize might be less than 16
                if constexpr (IsConst)
                {
                    // For const views, we need to manually get components since GetBatch returns non-const
                    std::size_t found = 0;
                    for (std::size_t j = 0; j < batchSize; ++j)
                    {
                        batchArray[j] = pool->TryGet(entities[j]);
                        if (batchArray[j]) found++;
                    }
                    return found;
                }
                else
                {
                    // For non-const views, we can use GetBatch directly
                    Entity entityArray[16];
                    std::copy_n(entities, batchSize, entityArray);
                    
                    using ComponentType = typename std::tuple_element<poolIndex.value, std::tuple<Components...>>::type;
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