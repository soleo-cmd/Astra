#pragma once

#include <memory>
#include <tuple>
#include "../Entity/Entity.hpp"
#include "ComponentStream.hpp"

namespace Astra
{
    /**
     * ComponentStreamView provides a user-friendly interface to cached component streams.
     * 
     * This class wraps a ComponentStream and provides iteration methods that operate
     * on the pre-built stream data for optimal performance.
     * 
     * Usage:
     * ```cpp
     * auto view = registry.GetView<Position, Velocity>();
     * auto stream = view.Stream();  // Build stream once
     * 
     * // Iterate multiple times with no rebuild cost
     * stream.ForEach([](Entity e, Position& p, Velocity& v) { ... });
     * stream.ForEach([](Entity e, Position& p, Velocity& v) { ... });
     * 
     * // Get statistics
     * auto stats = stream.GetStats();
     * std::cout << "Entities: " << stats.totalEntities << "\n";
     * ```
     * 
     * @tparam Components Component types in the stream
     */
    template<typename... Components>
    class ComponentStreamView
    {
    public:
        using StreamType = ComponentStream<Components...>;
        
        /**
         * Construct a stream view from pools.
         * Immediately builds the stream.
         * 
         * @param pools Component pools to stream from
         */
        template<typename... Pools>
        explicit ComponentStreamView(Pools*... pools)
            : m_stream(std::make_shared<StreamType>())
        {
            m_stream->FillStream(pools...);
            m_isBuilt = true;
        }
        
        /**
         * Construct from an existing stream.
         * Used internally by View::Stream().
         */
        explicit ComponentStreamView(std::shared_ptr<StreamType> stream, bool isBuilt = false)
            : m_stream(std::move(stream)), m_isBuilt(isBuilt)
        {
        }
        
        /**
         * Process all entities in the stream.
         * @param func Callback taking (Entity, Components&...)
         */
        template<typename Func>
        void ForEach(Func&& func)
        {
            if (!m_isBuilt || !m_stream)
            {
                return;
            }
            
            m_stream->ProcessStream(std::forward<Func>(func));
        }
        
        /**
         * Process entities in batches for SIMD operations.
         * @param func Callback taking (Entity*, size_t count, Components**...)
         * @param batchSize Size of batches (default 16)
         */
        template<typename Func>
        void ForEachBatch(Func&& func, size_t batchSize = 16)
        {
            if (!m_isBuilt || !m_stream)
            {
                return;
            }
            
            m_stream->ProcessStreamBatched(std::forward<Func>(func), batchSize);
        }
        
        /**
         * Get the number of entities in the stream.
         */
        [[nodiscard]] size_t Size() const noexcept
        {
            return m_stream ? m_stream->GetEntityCount() : 0;
        }
        
        /**
         * Check if the stream is empty.
         */
        [[nodiscard]] bool Empty() const noexcept
        {
            return Size() == 0;
        }
        
        /**
         * StreamIterator provides iteration over the ComponentStream.
         * This allows range-based for loops to use the optimized streaming approach.
         * 
         * @tparam IsConst Whether this is a const iterator
         */
        template<bool IsConst>
        class StreamIterator
        {
        public:
            using StreamType = ComponentStream<Components...>;
            
            // Iterator traits
            using iterator_category = std::forward_iterator_tag;
            using value_type = std::conditional_t<IsConst,
                std::tuple<Entity, const Components&...>,
                std::tuple<Entity, Components&...>>;
            using difference_type = std::ptrdiff_t;
            using pointer = value_type*;
            using reference = value_type;
            
        private:
            struct IteratorData
            {
                std::shared_ptr<StreamType> stream;
                size_t blockIndex = 0;
                size_t elementIndex = 0;
                size_t totalIndex = 0;
                
                // Cache current element for operator*
                Entity currentEntity;
                std::tuple<Components*...> currentComponents;
                bool atEnd = false;
                
                void Advance()
                {
                    if (atEnd || !stream) return;
                    
                    totalIndex++;
                    elementIndex++;
                    
                    // Check if we need to move to next group
                    auto& groups = stream->GetGroups();
                    if (blockIndex < groups.size())
                    {
                        if (elementIndex >= groups[blockIndex].count)
                        {
                            blockIndex++;
                            elementIndex = 0;
                        }
                        
                        // Check if we've reached the end
                        if (blockIndex >= groups.size())
                        {
                            atEnd = true;
                        }
                        else
                        {
                            // Update current element
                            currentEntity = groups[blockIndex].entities[elementIndex];
                            currentComponents = groups[blockIndex].components[elementIndex];
                        }
                    }
                    else
                    {
                        atEnd = true;
                    }
                }
                
                void Initialize()
                {
                    if (!stream || stream->IsEmpty())
                    {
                        atEnd = true;
                        return;
                    }
                    
                    auto& groups = stream->GetGroups();
                    if (groups.empty())
                    {
                        atEnd = true;
                        return;
                    }
                    
                    // Set to first element
                    blockIndex = 0;
                    elementIndex = 0;
                    totalIndex = 0;
                    currentEntity = groups[0].entities[0];
                    currentComponents = groups[0].components[0];
                    atEnd = false;
                }
            };
            
            std::shared_ptr<IteratorData> m_data;
            
        public:
            StreamIterator() = default;
            
            explicit StreamIterator(std::shared_ptr<StreamType> stream, bool isEnd = false)
            {
                if (!stream || isEnd)
                {
                    // End iterator
                    m_data = std::make_shared<IteratorData>();
                    m_data->atEnd = true;
                }
                else
                {
                    m_data = std::make_shared<IteratorData>();
                    m_data->stream = stream;
                    m_data->Initialize();
                }
            }
            
            reference operator*() const
            {
                return std::apply([this](auto*... comps) {
                    return std::forward_as_tuple(m_data->currentEntity, *comps...);
                }, m_data->currentComponents);
            }
            
            StreamIterator& operator++()
            {
                if (m_data)
                {
                    m_data->Advance();
                }
                return *this;
            }
            
            StreamIterator operator++(int)
            {
                StreamIterator tmp = *this;
                ++(*this);
                return tmp;
            }
            
            bool operator==(const StreamIterator& other) const
            {
                // Both default-constructed (end iterators)
                if (!m_data && !other.m_data) return true;
                
                // One is default-constructed (end), check if other is at end
                if (!m_data) return other.m_data && other.m_data->atEnd;
                if (!other.m_data) return m_data && m_data->atEnd;
                
                // Both have data
                if (m_data->atEnd && other.m_data->atEnd) return true;
                if (m_data->atEnd != other.m_data->atEnd) return false;
                
                // Compare position for non-end iterators
                return m_data->totalIndex == other.m_data->totalIndex;
            }
            
            bool operator!=(const StreamIterator& other) const
            {
                return !(*this == other);
            }
        };
        
        using iterator = StreamIterator<false>;
        using const_iterator = StreamIterator<true>;
        
        // Iterator methods for range-based for loops
        [[nodiscard]] iterator begin()
        {
            if (!m_isBuilt || !m_stream)
            {
                return iterator();
            }
            return iterator(m_stream);
        }
        
        [[nodiscard]] iterator end()
        {
            return iterator();
        }
        
        [[nodiscard]] const_iterator begin() const
        {
            if (!m_isBuilt || !m_stream)
            {
                return const_iterator();
            }
            return const_iterator(m_stream);
        }
        
        [[nodiscard]] const_iterator end() const
        {
            return const_iterator();
        }
        
    private:
        std::shared_ptr<StreamType> m_stream;
        bool m_isBuilt = false;
    };
    
    // Convenience factory function
    template<typename... Components, typename... Pools>
    auto MakeStreamView(Pools*... pools)
    {
        return ComponentStreamView<Components...>(pools...);
    }
}