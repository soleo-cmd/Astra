#pragma once

#include <vector>
#include <tuple>
#include <memory>
#include "../Entity/Entity.hpp"
#include "../Core/Base.hpp"
#include "../Core/Profile.hpp"
#include "../Core/Simd.hpp"

namespace Astra
{
    /**
     * ComponentStream provides cache-friendly intermediate storage for multi-component iteration.
     * 
     * Instead of jumping between different component pools causing cache misses,
     * ComponentStream gathers component pointers into contiguous blocks for efficient processing.
     * 
     * Memory layout (AOS - Array of Structures):
     * Block 0: [(E0,P0*,V0*), (E1,P1*,V1*), ..., (E63,P63*,V63*)]
     * Block 1: [(E64,P64*,V64*), (E65,P65*,V65*), ..., (E127,P127*,V127*)]
     * 
     * This layout ensures all component pointers for an entity are in the same cache line,
     * dramatically improving multi-component iteration performance.
     * 
     * @tparam Components Component types to stream together
     */
    template<typename... Components>
    class ComponentStream
    {
    public:
        // Match FlatMap's group size for optimal alignment
        static constexpr size_t GROUP_SIZE = 16;
        
        // Component pointer tuple for each entity
        using ComponentTuple = std::tuple<Components*...>;
        using ConstComponentTuple = std::tuple<const Components*...>;
        
    public:
        // Aligned storage group matching FlatMap's structure
        struct alignas(CACHE_LINE_SIZE) StreamGroup
        {
            Entity entities[GROUP_SIZE];
            ComponentTuple components[GROUP_SIZE];
            std::uint16_t validMask = 0;  // Bit mask of valid slots
            std::uint8_t count = 0;       // Number of valid entities
            
            StreamGroup() = default;
        };
        
    private:
        
        // Storage
        std::vector<StreamGroup> m_groups;
        size_t m_totalCount = 0;
        
        // Helper to check if entity has all components starting from index StartIdx
        template<std::size_t StartIdx, typename PoolTuple, std::size_t... Is>
        bool CheckAllComponents(Entity entity, const PoolTuple& poolTuple, std::index_sequence<Is...>) const
        {
            bool hasAll = true;
            ((Is >= StartIdx ? (hasAll = hasAll && std::get<Is>(poolTuple)->Contains(entity), void()) : void()), ...);
            return hasAll;
        }
        
        // Helper to gather all component pointers
        template<std::size_t StartIdx, typename ComponentTuple, typename FirstComponent, typename PoolTuple, std::size_t... Is>
        void GatherAllComponents(ComponentTuple& compTuple, Entity entity, FirstComponent* firstComp,
                                 const PoolTuple& poolTuple, std::index_sequence<Is...>)
        {
            // Process each component
            auto processComponent = [&]<std::size_t I>() -> void
            {
                if constexpr (I == 0)
                {
                    std::get<I>(compTuple) = firstComp;
                }
                else
                {
                    std::get<I>(compTuple) = std::get<I>(poolTuple)->TryGet(entity);
                }
            };
            
            (processComponent.template operator()<Is>(), ...);
        }
        
        
    public:
        ComponentStream() = default;
        
        /**
         * Fill the stream from component pools using optimized group iteration.
         * This gathers all entities that have all required components.
         * 
         * @param pools Component pools to gather from
         */
        template<typename... Pools>
        void FillStream(Pools*... pools)
        {
            ASTRA_PROFILE_ZONE_NAMED_COLOR("ComponentStream::FillStream", Profile::ColorView);
            
            // Clear previous data
            m_groups.clear();
            m_totalCount = 0;
            
            // Early exit if any pool is null
            if (((pools == nullptr) || ...))
            {
                return;
            }
            
            // Early exit if first pool is null or empty
            using FirstPool = typename std::tuple_element<0, std::tuple<Pools...>>::type;
            auto* firstPool = std::get<0>(std::make_tuple(pools...));
            if (!firstPool || firstPool->Empty())
            {
                return;
            }
            
            // Estimate and reserve space based on first pool
            size_t estimatedGroups = (firstPool->Size() + GROUP_SIZE - 1) / GROUP_SIZE;
            m_groups.reserve(estimatedGroups);
            
            // Store pools in a tuple once to avoid recreating it
            auto poolTuple = std::make_tuple(pools...);
            
            // Iterate through FlatMap groups directly
            firstPool->GetFlatMap().ForEachGroupDirect([&, poolTuple](const std::uint8_t* metadata,
                                                            const Entity* entities,
                                                            typename FirstPool::ComponentType* const* components,
                                                            std::size_t count,
                                                            std::uint16_t occupiedMask)
            {
                StreamGroup group;
                group.count = 0;
                group.validMask = 0;
                
                // For each entity in this FlatMap group
                for (size_t i = 0; i < count; ++i)
                {
                    Entity entity = entities[i];
                    
                    // Check if entity has all other components
                    bool hasAll = CheckAllComponents<1>(entity, poolTuple, 
                                                        std::index_sequence_for<Pools...>{});
                    
                    if (hasAll)
                    {
                        // Add to current stream group
                        group.entities[group.count] = entity;
                        
                        // Gather all component pointers
                        GatherAllComponents<0>(group.components[group.count], entity, 
                                               components[i], poolTuple,
                                               std::index_sequence_for<Pools...>{});
                        
                        group.validMask |= (1u << group.count);
                        group.count++;
                        m_totalCount++;
                    }
                }
                
                // Add group if it has any valid entities
                if (group.count > 0)
                {
                    m_groups.push_back(group);
                }
            });
        }
        
        
        /**
         * Process the stream with a callback function.
         * The callback receives entity and component references.
         * 
         * @param func Callback function (Entity, Components&...)
         */
        template<typename Func>
        void ProcessStream(Func&& func)
        {
            for (size_t groupIdx = 0; groupIdx < m_groups.size(); ++groupIdx)
            {
                auto& group = m_groups[groupIdx];
                
                // Prefetch next group
                if (groupIdx + 1 < m_groups.size())
                {
                    Simd::Ops::PrefetchT1(&m_groups[groupIdx + 1]);
                }
                
                // Process current group with perfect cache locality
                for (size_t i = 0; i < group.count; ++i)
                {
                    // Prefetch next elements within group
                    if (i + 4 < group.count)
                    {
                        Simd::Ops::PrefetchT0(&group.components[i + 4]);
                    }
                    
                    std::apply([&](auto*... comps) {
                        func(group.entities[i], *comps...);
                    }, group.components[i]);
                }
            }
        }
        
        /**
         * Process the stream in batches for SIMD operations.
         * The callback receives arrays of entities and component pointers.
         * 
         * @param func Callback function (Entity*, size_t count, Components**...)
         * @param batchSize Size of batches (default 16 for SIMD)
         */
        template<typename Func>
        void ProcessStreamBatched(Func&& func, size_t batchSize = 16)
        {
            // Temporary arrays for batch processing
            Entity batchEntities[64];
            std::tuple<std::array<Components*, 64>...> batchComponents;
            
            for (auto& group : m_groups)
            {
                size_t processed = 0;
                
                while (processed < group.count)
                {
                    size_t currentBatchSize = std::min(batchSize, group.count - processed);
                    
                    // Copy entities
                    std::copy_n(&group.entities[processed], currentBatchSize, batchEntities);
                    
                    // Extract component pointers
                    ExtractBatchPointers<0>(group, processed, currentBatchSize, batchComponents);
                    
                    // Call function with batch
                    CallBatchFunc(func, batchEntities, currentBatchSize, batchComponents,
                                  std::index_sequence_for<Components...>{});
                    
                    processed += currentBatchSize;
                }
            }
        }
        
        [[nodiscard]] size_t GetEntityCount() const noexcept { return m_totalCount; }
        [[nodiscard]] bool IsEmpty() const noexcept { return m_totalCount == 0; }
        
        // Get access to groups for iteration
        [[nodiscard]] const std::vector<StreamGroup>& GetGroups() const noexcept { return m_groups; }
        [[nodiscard]] std::vector<StreamGroup>& GetGroups() noexcept { return m_groups; }
        [[nodiscard]] size_t GetGroupCount() const noexcept { return m_groups.size(); }
        [[nodiscard]] const StreamGroup& GetGroup(size_t index) const noexcept { return m_groups[index]; }
        
    private:
        // Helper to extract batch pointers
        template<size_t I, typename BatchTuple>
        void ExtractBatchPointers(const StreamGroup& group, size_t start, size_t count, 
                                  BatchTuple& batchComponents)
        {
            if constexpr (I < sizeof...(Components))
            {
                auto& array = std::get<I>(batchComponents);
                for (size_t j = 0; j < count; ++j)
                {
                    array[j] = std::get<I>(group.components[start + j]);
                }
                ExtractBatchPointers<I + 1>(group, start, count, batchComponents);
            }
        }
        
        // Helper to call batch function
        template<typename Func, typename BatchTuple, size_t... Is>
        void CallBatchFunc(Func&& func, Entity* entities, size_t count, 
                           BatchTuple& components, std::index_sequence<Is...>)
        {
            func(entities, count, std::get<Is>(components).data()...);
        }
    };
}