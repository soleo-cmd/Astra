#pragma once

#include <algorithm>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

#include "../Archetype/Archetype.hpp"
#include "../Archetype/ArchetypeStorage.hpp"
#include "../Component/Component.hpp"
#include "../Entity/Entity.hpp"
#include "Query.hpp"

namespace Astra
{
    template<typename... QueryArgs>
    class View
    {
        static_assert(ValidQuery<QueryArgs...>, "View template arguments must be valid components or query modifiers");
        
    public:
        explicit View(std::shared_ptr<ArchetypeStorage> storage) : m_storage(std::move(storage))
        {
            CollectMatchingArchetypes();
        }

        template<typename Func>
        ASTRA_FORCEINLINE void ForEach(Func&& func)
        {
            if (m_archetypes.empty()) ASTRA_UNLIKELY
                return;
            
            for (Archetype* archetype : m_archetypes)
            {
                ForEachImpl(archetype, std::forward<Func>(func), RequiredTuple{}, OptionalTuple{});
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
                    // Set to end position
                    m_archIdx = std::numeric_limits<size_t>::max();
                    m_chunkIdx = 0;
                    m_entityIdx = 0;
                }
            }

            auto operator*() const
            {
                return BuildTuple(std::make_index_sequence<ComponentCount>{});
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
                constexpr size_t RequiredCount = std::tuple_size_v<typename View::RequiredTuple>;
                constexpr bool isOptional = (I >= RequiredCount);

                auto* arch = (*m_archetypes)[m_archIdx];
                if (arch->HasComponent<Component>())
                {
                    // Get component from current chunk
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
                    return nullptr;  // Should never happen for required components
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
                return !m_archetypes || m_archIdx == std::numeric_limits<size_t>::max() || 
                    m_archIdx >= m_archetypes->size();
            }

            void AdvanceToValid()
            {
                while (m_archIdx < m_archetypes->size())
                {
                    // Check if within current chunk
                    if (m_entityIdx < m_currentCount)
                    {
                        return;  // Valid position
                    }

                    // Move to next chunk
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
                        // Move to next archetype
                        ++m_archIdx;
                        m_chunkIdx = 0;
                        m_entityIdx = 0;

                        if (m_archIdx < m_archetypes->size())
                        {
                            CacheCurrentChunk();
                        }
                    }
                }

                // Reached end - set to end state
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

        iterator begin() { return iterator(m_archetypes); }
        iterator end() { return iterator(); }
        const_iterator begin() const { return iterator(m_archetypes); }
        const_iterator end() const { return iterator(); }
        
    private:
        void CollectMatchingArchetypes()
        {
            auto archetypes = m_storage->GetAllArchetypes();
            const size_t queryComponentCount = QueryBuilder::GetRequiredMask().Count();
            
            m_archetypes.reserve(archetypes.size());
            
            for (Archetype* archetype : archetypes)
            {
                if (archetype->GetEntityCount() == 0) ASTRA_UNLIKELY
                {
                    continue;
                }
                if (archetype->GetComponentCount() < queryComponentCount) ASTRA_UNLIKELY
                {
                    continue;
                }
                if (QueryBuilder::Matches(archetype->GetMask()))
                {
                    m_archetypes.push_back(archetype);
                }
            }
            
            std::sort(m_archetypes.begin(), m_archetypes.end(),
                [](Archetype* a, Archetype* b)
                {
                    return a->GetEntityCount() > b->GetEntityCount();
                });
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
        
        template<typename... Components, typename Func, size_t... RequiredIs, size_t... OptionalIs>
        ASTRA_FORCEINLINE void ForEachWithOptional(Archetype* archetype, Func&& func, std::index_sequence<RequiredIs...>, std::index_sequence<OptionalIs...>)
        {
            using RequiredTypes = RequiredTuple;
            using OptionalTypes = OptionalTuple;
            
            constexpr size_t OptionalCount = sizeof...(OptionalIs);
            std::array<bool, OptionalCount> hasOptional =
            {
                archetype->HasComponent<std::tuple_element_t<OptionalIs, OptionalTypes>>()...
            };
            
            const auto& chunks = archetype->GetChunks();
            
            // Get chunks and iterate
            for (auto& chunk : chunks)
            {
                size_t count = chunk->GetCount();
                if (count == 0) ASTRA_UNLIKELY continue;
                
                // Get component arrays for required components
                std::tuple<std::tuple_element_t<RequiredIs, RequiredTypes>*...> requiredPtrs = {
                    chunk->template GetComponentArray<std::tuple_element_t<RequiredIs, RequiredTypes>>()...
                };
                
                // Get component arrays for optional components (may be nullptr)
                std::tuple<std::tuple_element_t<OptionalIs, OptionalTypes>*...> optionalPtrs = {
                    (hasOptional[OptionalIs] ? 
                     chunk->template GetComponentArray<std::tuple_element_t<OptionalIs, OptionalTypes>>() : 
                     nullptr)...
                };
                
                // Get entities
                const auto& entities = chunk->GetEntities();
                
                // Call function for each entity
                for (size_t i = 0; i < count; ++i)
                {
                    std::apply([&, i](auto*... reqPtrs) {
                        std::apply([&, i](auto*... optPtrs) {
                            func(entities[i], reqPtrs[i]..., (optPtrs ? &optPtrs[i] : nullptr)...);
                        }, optionalPtrs);
                    }, requiredPtrs);
                }
            }
        }
        
        template<size_t I, typename ChunkType>
        auto GetComponentPtrFromChunk(Archetype* arch, const ChunkType& chunk, size_t idx)
        {
            using Component = std::tuple_element_t<I, IterationComponents>;
            constexpr size_t RequiredCount = std::tuple_size_v<RequiredTuple>;
            constexpr bool isOptional = (I >= RequiredCount);
            
            if (arch->HasComponent<Component>())
            {
                auto* array = chunk->template GetComponentArray<Component>();
                return &array[idx];
            }
            else if constexpr (isOptional)
            {
                return static_cast<Component*>(nullptr);
            }
            else
            {
                ASTRA_ASSERT(false, "Required component missing from archetype");
                return static_cast<Component*>(nullptr);
            }
        }

        using QueryClassifier = Detail::QueryClassifier<QueryArgs...>;
        using RequiredTuple = typename QueryClassifier::RequiredComponents;
        using OptionalTuple = typename QueryClassifier::OptionalComponents;
        using QueryBuilder = QueryBuilder<QueryArgs...>;

        template<typename... Rs, typename... Os>
        static auto CombineTypes(std::tuple<Rs...>, std::tuple<Os...>) -> std::tuple<Rs..., Os...>;
        using IterationComponents = decltype(CombineTypes(RequiredTuple{}, OptionalTuple{}));

        static constexpr size_t ComponentCount = std::tuple_size_v<IterationComponents>;

        std::vector<Archetype*> m_archetypes;
        std::shared_ptr<ArchetypeStorage> m_storage;
    };
} // namespace Astra