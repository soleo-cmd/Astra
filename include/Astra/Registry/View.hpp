#pragma once

#include <algorithm>
#include <atomic>
#include <future>
#include <memory>
#include <optional>
#include <thread>
#include <tuple>
#include <vector>

#include "../Archetype/Archetype.hpp"
#include "../Archetype/ArchetypeManager.hpp"
#include "../Component/Component.hpp"
#include "../Entity/Entity.hpp"
#include "Query.hpp"

namespace Astra
{
    template<typename... QueryArgs>
    class View
    {
        static_assert(ValidQuery<QueryArgs...>, "View template arguments must be valid components or query modifiers");
        
        // Parallel execution thresholds - based on empirical testing
        static constexpr size_t AVG_ENTITIES_PER_CHUNK = 256;                           // Typical for 16KB chunks with ~50 byte entities
        static constexpr size_t MIN_CHUNKS_PER_THREAD = 4;                              // Each thread should process at least 4 chunks (64KB)
        static constexpr size_t MIN_CHUNKS_FOR_PARALLEL = MIN_CHUNKS_PER_THREAD * 2;    // Need enough for at least 2 threads
        
        // Derive entity thresholds from chunk-based values
        static constexpr size_t MIN_ENTITIES_QUICK_CHECK = AVG_ENTITIES_PER_CHUNK / 2;  // Less than half a chunk = definitely sequential
        static constexpr size_t MIN_ENTITIES_FOR_PARALLEL = MIN_CHUNKS_FOR_PARALLEL * AVG_ENTITIES_PER_CHUNK / 2;  // ~4 chunks worth
        
    public:
        explicit View(std::shared_ptr<ArchetypeManager> manager) :
            m_archetypeManager(std::move(manager)),
            m_lastRefreshCounter(0),
            m_lastGeneration(0)
        {
            CollectArchetypes();
            m_lastRefreshCounter = m_archetypeManager->GetStructuralChangeCounter();
            m_lastGeneration = m_archetypeManager->GetCurrentGeneration();
        }

        template<typename Func>
        ASTRA_FORCEINLINE void ForEach(Func&& func)
        {
            EnsureArchetypes();
            
            if (m_archetypes.empty()) ASTRA_UNLIKELY
                return;
            
            for (Archetype* archetype : m_archetypes)
            {
                ForEachImpl(archetype, std::forward<Func>(func), RequiredTypes{}, OptionalTypes{});
            }
        }
        
        template<typename Func>
        ASTRA_FORCEINLINE void ParallelForEach(Func&& func)
        {
            EnsureArchetypes();
            
            if (m_archetypes.empty()) ASTRA_UNLIKELY
                return;
            
            // Quick check: if we have very few matching entities, don't even try parallel
            size_t quickCount = 0;
            for (Archetype* archetype : m_archetypes)
            {
                quickCount += archetype->GetEntityCount();
            }
            
            if (quickCount < MIN_ENTITIES_QUICK_CHECK)
            {
                return ForEach(std::forward<Func>(func));
            }
            
            std::vector<std::pair<Archetype*, size_t>> chunkWork;
            // Better estimation based on typical entities per 16KB chunk
            size_t estimatedChunks = (quickCount / AVG_ENTITIES_PER_CHUNK) + m_archetypes.size();
            chunkWork.reserve(estimatedChunks);
            size_t totalMatchingEntities = 0;
            
            for (Archetype* archetype : m_archetypes)
            {
                size_t chunkCount = archetype->GetChunkCount();
                for (size_t i = 0; i < chunkCount; ++i)
                {
                    size_t chunkEntityCount = archetype->GetChunkEntityCount(i);
                    if (chunkEntityCount > 0)
                    {
                        chunkWork.emplace_back(archetype, i);
                        totalMatchingEntities += chunkEntityCount;
                    }
                }
            }
            
            // Fall back to sequential for tiny workloads
            if (chunkWork.empty() || 
                totalMatchingEntities < MIN_ENTITIES_FOR_PARALLEL || 
                chunkWork.size() < MIN_CHUNKS_FOR_PARALLEL)
            {
                return ForEach(std::forward<Func>(func));
            }
            
            // Determine optimal thread count ensuring each thread gets meaningful work
            const size_t hardwareConcurrency = std::thread::hardware_concurrency();
            const size_t maxThreadsByWork = chunkWork.size() / MIN_CHUNKS_PER_THREAD;
            const size_t numWorkers = std::min(hardwareConcurrency, std::max(size_t(1), maxThreadsByWork));
            
            std::atomic<size_t> nextChunkIndex{0};
            std::vector<std::future<void>> futures;
            futures.reserve(numWorkers);
            
            for (size_t t = 0; t < numWorkers; ++t)
            {
                futures.push_back(std::async(std::launch::async,
                    [this, &func, &chunkWork, &nextChunkIndex]()
                    {
                        size_t chunkIdx;
                        while ((chunkIdx = nextChunkIndex.fetch_add(1, std::memory_order_relaxed)) < chunkWork.size())
                        {
                            auto [archetype, chunkIndex] = chunkWork[chunkIdx];
                            ParallelForEachChunkImpl(archetype, chunkIndex, func, RequiredTypes{}, OptionalTypes{});
                        }
                    }));
            }
            
            for (auto& future : futures)
            {
                future.wait();
            }
        }
        
        ASTRA_NODISCARD size_t Size() const noexcept
        {
            size_t total = 0;
            for (const auto* archetype : m_archetypes)
            {
                total += archetype->GetEntityCount();
            }
            return total;
        }

        ASTRA_NODISCARD bool Empty() const noexcept
        {
            return m_archetypes.empty();
        }

        class iterator
        {
        public:
            using iterator_category = std::forward_iterator_tag;
            using difference_type = std::ptrdiff_t;

            iterator() = default;

            iterator(const std::vector<Archetype*>& archetypes) : m_archetypes(&archetypes)
            {
                if (!m_archetypes->empty() && !archetypes.empty())
                {
                    m_archIdx = 0;
                    m_chunkIdx = 0;
                    m_entityIdx = 0;
                    CacheCurrentChunk();
                    AdvanceToValid();
                }
                else
                {
                    m_archIdx = std::numeric_limits<size_t>::max();
                    m_chunkIdx = 0;
                    m_entityIdx = 0;
                }
            }

            auto operator*() const
            {
                return BuildTuple(std::make_index_sequence<COMPONENT_COUNT>{});
            }

            iterator& operator++()
            {
                ++m_entityIdx;
                AdvanceToValid();
                return *this;
            }

            bool operator==(const iterator& other) const
            {
                if (IsEnd() && other.IsEnd())
                    return true;
                if (IsEnd() != other.IsEnd())
                    return false;

                return m_archIdx == other.m_archIdx && m_chunkIdx == other.m_chunkIdx && m_entityIdx == other.m_entityIdx;
            }

            bool operator!=(const iterator& other) const { return !(*this == other); }

        private:
            template<size_t... Is>
            auto BuildTuple(std::index_sequence<Is...>) const
            {
                Entity entity = m_currentEntities[m_entityIdx];
                return std::tuple{entity, GetComponent<Is>()...};
            }

            template<size_t I>
            auto GetComponent() const -> std::tuple_element_t<I, typename View::IterationComponents>*
            {
                using Component = std::tuple_element_t<I, typename View::IterationComponents>;
                constexpr size_t RequiredCount = std::tuple_size_v<typename View::RequiredTypes>;
                constexpr bool isOptional = (I >= RequiredCount);

                auto* arch = (*m_archetypes)[m_archIdx];
                if (arch->HasComponent<Component>())
                {
                    auto& chunk = arch->GetChunks()[m_chunkIdx];
                    auto* array = chunk->template GetComponentArray<Component>();
                    return &array[m_entityIdx];
                }
                else if constexpr (isOptional)
                {
                    return nullptr;
                }
                else
                {
                    return nullptr;
                }
            }

            void CacheCurrentChunk()
            {
                if (m_archIdx >= m_archetypes->size())
                    return;

                auto* arch = (*m_archetypes)[m_archIdx];
                const auto& chunks = arch->GetChunks();

                if (m_chunkIdx < chunks.size())
                {
                    const auto& chunk = chunks[m_chunkIdx];
                    m_currentEntities = chunk->GetEntities().data();
                    m_currentCount = chunk->GetCount();
                }
                else
                {
                    m_currentEntities = nullptr;
                    m_currentCount = 0;
                }
            }

            bool IsEnd() const
            {
                return !m_archetypes || m_archIdx == std::numeric_limits<size_t>::max() || m_archIdx >= m_archetypes->size();
            }

            void AdvanceToValid()
            {
                while (m_archIdx < m_archetypes->size())
                {
                    if (m_entityIdx < m_currentCount)
                    {
                        return;
                    }

                    auto* arch = (*m_archetypes)[m_archIdx];
                    const auto& chunks = arch->GetChunks();

                    ++m_chunkIdx;
                    m_entityIdx = 0;

                    if (m_chunkIdx < chunks.size())
                    {
                        CacheCurrentChunk();
                    }
                    else
                    {
                        ++m_archIdx;
                        m_chunkIdx = 0;
                        m_entityIdx = 0;

                        if (m_archIdx < m_archetypes->size())
                        {
                            CacheCurrentChunk();
                        }
                    }
                }

                m_archIdx = std::numeric_limits<size_t>::max();
            }

            const std::vector<Archetype*>* m_archetypes = nullptr;
            size_t m_archIdx = std::numeric_limits<size_t>::max();
            size_t m_chunkIdx = 0;
            size_t m_entityIdx = 0;
            const Entity* m_currentEntities = nullptr;
            size_t m_currentCount = 0;
        };

        using const_iterator = const iterator;

        iterator begin() 
        { 
            EnsureArchetypes();
            return iterator(m_archetypes); 
        }
        iterator end() { return iterator(); }
        const_iterator begin() const 
        { 
            const_cast<View*>(this)->EnsureArchetypes();
            return iterator(m_archetypes); 
        }
        const_iterator end() const { return iterator(); }
        
    private:
        using RequiredTypes = Detail::QueryClassifier<QueryArgs...>::RequiredComponents;
        using OptionalTypes = Detail::QueryClassifier<QueryArgs...>::OptionalComponents;
        using QueryBuilder = QueryBuilder<QueryArgs...>;
        
        template<typename... Rs, typename... Os>
        static auto CombineTypes(std::tuple<Rs...>, std::tuple<Os...>) -> std::tuple<Rs..., Os...>;
        using IterationComponents = decltype(CombineTypes(RequiredTypes{}, OptionalTypes{}));

        static constexpr size_t COMPONENT_COUNT = std::tuple_size_v<IterationComponents>;

        struct ArchetypeEntityCountComparator
        {
            bool operator()(Archetype* a, Archetype* b) const
            {
                return a->GetEntityCount() > b->GetEntityCount();
            }
        };

        void EnsureArchetypes()
        {
            uint32_t currentCounter = m_archetypeManager->GetStructuralChangeCounter();
            if (m_lastRefreshCounter == currentCounter)
            {
                return;
            }
            
            if (m_lastGeneration == 0)
            {
                CollectArchetypes();
            }
            else
            {
                auto newArchetypes = m_archetypeManager->GetArchetypesSince(m_lastGeneration);
                for (Archetype* arch : newArchetypes)
                {
                    if (arch->GetEntityCount() == 0) ASTRA_UNLIKELY
                    {
                        continue;
                    }
                    if (QueryBuilder::Matches(arch->GetMask()))
                    {
                        m_archetypes.push_back(arch);
                    }
                }
                
                std::sort(m_archetypes.begin(), m_archetypes.end(), ArchetypeEntityCountComparator{});
            }
            
            m_lastRefreshCounter = currentCounter;
            m_lastGeneration = m_archetypeManager->GetCurrentGeneration();
        }
        
        void CollectArchetypes()
        {
            auto archetypes = m_archetypeManager->GetAllArchetypes();
            const size_t queryComponentCount = QueryBuilder::GetRequiredMask().Count();
            
            m_archetypes.reserve(archetypes.size());
            
            for (Archetype* archetype : archetypes)
            {
                if (archetype->GetEntityCount() == 0 || archetype->GetComponentCount() < queryComponentCount) ASTRA_UNLIKELY
                {
                    continue;
                }
                if (QueryBuilder::Matches(archetype->GetMask()))
                {
                    m_archetypes.push_back(archetype);
                }
            }
            
            std::sort(m_archetypes.begin(), m_archetypes.end(), ArchetypeEntityCountComparator{});
        }

        template<typename Func, typename... Required, typename... Optional>
        ASTRA_FORCEINLINE void ForEachImpl(Archetype* archetype, Func&& func, std::tuple<Required...>, std::tuple<Optional...>)
        {
            if constexpr (sizeof...(Optional) == 0)
            {
                archetype->ForEach<Required...>(std::forward<Func>(func));
            }
            else
            {
                ForEachWithOptional<Required..., Optional...>(archetype, std::forward<Func>(func), std::make_index_sequence<sizeof...(Required)>{}, std::make_index_sequence<sizeof...(Optional)>{});
            }
        }
        
        template<typename... Components, typename Func, size_t... RequiredTs, size_t... OptionalTs>
        ASTRA_FORCEINLINE void ForEachWithOptional(Archetype* archetype, Func&& func, std::index_sequence<RequiredTs...>, std::index_sequence<OptionalTs...>)
        {
            constexpr size_t OptionalCount = sizeof...(OptionalTs);
            std::array<bool, OptionalCount> hasOptional =
            {
                archetype->HasComponent<std::tuple_element_t<OptionalTs, OptionalTypes>>()...
            };

            const auto& chunks = archetype->GetChunks();

            for (auto& chunk : chunks)
            {
                size_t count = chunk->GetCount();
                if (count == 0) ASTRA_UNLIKELY
                {
                    continue;
                }

                std::tuple<std::tuple_element_t<RequiredTs, RequiredTypes>*...> requiredPtrs =
                {
                    chunk->GetComponentArray<std::tuple_element_t<RequiredTs, RequiredTypes>>()...
                };
                std::tuple<std::tuple_element_t<OptionalTs, OptionalTypes>*...> optionalPtrs =
                {
                    (hasOptional[OptionalTs] ? chunk->GetComponentArray<std::tuple_element_t<OptionalTs, OptionalTypes>>() : nullptr)...
                };

                const auto& entities = chunk->GetEntities();

                InvokeEntityCallback(entities, requiredPtrs, optionalPtrs, count, std::forward<Func>(func), std::make_index_sequence<sizeof...(RequiredTs)>{}, std::make_index_sequence<sizeof...(OptionalTs)>{});
            }
        }

        template<typename Func, typename... Required, typename... Optional>
        ASTRA_FORCEINLINE void ParallelForEachChunkImpl(Archetype* archetype, size_t chunkIndex, Func&& func, std::tuple<Required...>, std::tuple<Optional...>)
        {
            if constexpr (sizeof...(Optional) == 0)
            {
                archetype->ParallelForEachChunk<Required...>(chunkIndex, std::forward<Func>(func));
            }
            else
            {
                ParallelForEachChunkWithOptional<Required..., Optional...>(archetype, chunkIndex, std::forward<Func>(func), std::make_index_sequence<sizeof...(Required)>{}, std::make_index_sequence<sizeof...(Optional)>{});
            }
        }
        
        template<typename... Components, typename Func, size_t... RequiredTs, size_t... OptionalTs>
        ASTRA_FORCEINLINE void ParallelForEachChunkWithOptional(Archetype* archetype, size_t chunkIndex, Func&& func, std::index_sequence<RequiredTs...>, std::index_sequence<OptionalTs...>)
        {
            constexpr size_t OptionalCount = sizeof...(OptionalTs);
            std::array<bool, OptionalCount> hasOptional =
            {
                archetype->HasComponent<std::tuple_element_t<OptionalTs, OptionalTypes>>()...
            };
            
            const auto& chunks = archetype->GetChunks();
            if (chunkIndex >= chunks.size()) ASTRA_UNLIKELY
                return;
                
            auto& chunk = chunks[chunkIndex];
            size_t count = chunk->GetCount();
            if (count == 0) ASTRA_UNLIKELY
                return;
                
            std::tuple<std::tuple_element_t<RequiredTs, RequiredTypes>*...> requiredPtrs =
            {
                chunk->GetComponentArray<std::tuple_element_t<RequiredTs, RequiredTypes>>()...
            };
            std::tuple<std::tuple_element_t<OptionalTs, OptionalTypes>*...> optionalPtrs =
            {
                (hasOptional[OptionalTs] ? chunk->GetComponentArray<std::tuple_element_t<OptionalTs, OptionalTypes>>() : nullptr)...
            };
            
            const auto& entities = chunk->GetEntities();
            
            InvokeEntityCallback(entities, requiredPtrs, optionalPtrs, count, std::forward<Func>(func), std::make_index_sequence<sizeof...(RequiredTs)>{}, std::make_index_sequence<sizeof...(OptionalTs)>{});
        }

        template<typename EntitiesVec, typename ReqTuple, typename OptTuple, typename Func, size_t... ReqIs, size_t... OptIs>
        ASTRA_FORCEINLINE void InvokeEntityCallback(const EntitiesVec& entities, const ReqTuple& reqPtrs, const OptTuple& optPtrs, size_t count, Func&& func, std::index_sequence<ReqIs...>, std::index_sequence<OptIs...>)
        {
            for (size_t i = 0; i < count; ++i)
            {
                func(entities[i], std::get<ReqIs>(reqPtrs)[i]..., (std::get<OptIs>(optPtrs) ? &std::get<OptIs>(optPtrs)[i] : nullptr)...);
            }
        }

        std::vector<Archetype*> m_archetypes;
        std::shared_ptr<ArchetypeManager> m_archetypeManager;
        
        uint32_t m_lastRefreshCounter = 0;
        uint32_t m_lastGeneration = 0;
    };
} // namespace Astra