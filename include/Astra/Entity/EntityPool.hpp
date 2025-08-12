#pragma once

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

#include "../Container/SmallVector.hpp"
#include "../Core/Base.hpp"
#include "../Core/Result.hpp"
#include "../Serialization/BinaryWriter.hpp"
#include "../Serialization/BinaryReader.hpp"
#include "../Serialization/SerializationError.hpp"
#include "Entity.hpp"

namespace Astra
{
    /**
    * EntityPool manages the lifecycle of entities in the ECS.
    * 
    * Features:
    * - O(1) entity creation and destruction
    * - Automatic entity recycling with version incrementation
    * - Segmented memory management with automatic release
    * - Cache-friendly 64KB segments
    * - Configurable memory policies
    * 
    * The pool uses segmented allocation where each segment manages 64K entities.
    * This allows memory to be returned to the OS when segments become empty.
    */
    class EntityPool : public std::enable_shared_from_this<EntityPool>
    {
    public:
        using VersionType = Entity::VersionType;
        using IDType = Entity::Type;
        
        struct Config
        {
            IDType entitiesPerSegment = 65536;      // 64K entities = 64KB per segment (must be power of 2)
            IDType entitiesPerSegmentShift = 16;    // log2(65536) for fast division
            IDType entitiesPerSegmentMask = 65535;  // 65536 - 1 for fast modulo
            float releaseThreshold = 0.1f;          // Release when <10% used
            bool autoRelease = true;                // Auto-release empty segments
            size_t maxEmptySegments = 2;            // Keep some empty segments ready
            
            // Constructor to calculate shift and mask
            Config(IDType segmentSize = 65536) 
            {
                entitiesPerSegment = segmentSize > 0 ? std::bit_floor(segmentSize) : 1;
                entitiesPerSegment = std::max(IDType(1024), entitiesPerSegment);
                entitiesPerSegmentShift = static_cast<IDType>(std::countr_zero(entitiesPerSegment));
                entitiesPerSegmentMask = entitiesPerSegment - 1;
            }
        };

        static constexpr VersionType NULL_VERSION = 0;      // Marks uninitialized/destroyed slots
        static constexpr VersionType INITIAL_VERSION = 1;   // First valid version
        static constexpr IDType INVALID_ID = Entity::ID_MASK;

    private:
        struct FreeListEntry
        {
            IDType id;
            VersionType nextVersion;
        };
        
        // Memory segment managing a range of entity IDs
        struct Segment
        {
            static constexpr size_t INVALID_SEGMENT = std::numeric_limits<size_t>::max();
            
            const IDType baseId;                    // First ID in this segment
            const IDType capacity;                  // Entities in this segment
            std::unique_ptr<VersionType[]> versions;
            SmallVector<FreeListEntry, 16> freeList;   // Local free list
            size_t aliveCount = 0;
            
            explicit Segment(IDType base, IDType cap) : baseId(base),
                capacity(cap),
                versions(std::make_unique<VersionType[]>(cap))
            {
                std::fill_n(versions.get(), capacity, NULL_VERSION);
            }
            
            bool Contains(IDType id) const noexcept
            {
                return id >= baseId && id < baseId + capacity;
            }
            
            size_t ToLocal(IDType id) const noexcept
            {
                ASTRA_ASSERT(Contains(id), "ID out of segment bounds");
                return id - baseId;
            }
            
            float Usage() const noexcept
            {
                return capacity > 0 ? static_cast<float>(aliveCount) / capacity : 0.0f;
            }
        };

        // Segments and lookup structures
        std::vector<std::unique_ptr<Segment>> m_segments;
        std::vector<size_t> m_segmentIndex;         // ID → segment lookup table
        SmallVector<FreeListEntry, 32> m_globalFreeList;  // Cross-segment free list
        
        // Configuration and state
        Config m_config;
        IDType m_nextId = 0;
        std::size_t m_aliveCount = 0;

    private:
        // Fast segment lookup with bit operations
        Segment* GetSegment(IDType id) noexcept
        {
            size_t segIdx = static_cast<size_t>(id >> m_config.entitiesPerSegmentShift);
            if (segIdx >= m_segmentIndex.size()) ASTRA_UNLIKELY return nullptr;
            
            size_t idx = m_segmentIndex[segIdx];
            if (idx == Segment::INVALID_SEGMENT) ASTRA_UNLIKELY return nullptr;
            
            ASTRA_ASSERT(idx < m_segments.size(), "Segment index out of bounds");
            auto* segment = m_segments[idx].get();
            ASTRA_ASSERT(segment, "Null segment in index");
            ASTRA_ASSERT(segment->Contains(id), "ID not in segment range");
            
            return segment;
        }
        
        // Find a free entry in existing segments
        std::pair<Segment*, FreeListEntry> FindFreeInSegments() noexcept
        {
            for (auto& segment : m_segments)
            {
                if (segment && !segment->freeList.empty()) ASTRA_LIKELY
                {
                    auto entry = segment->freeList.back();
                    segment->freeList.pop_back();
                    return {segment.get(), entry};
                }
            }
            return {nullptr, FreeListEntry{}};
        }
        
        // Allocate a new ID, creating a new segment if needed
        IDType AllocateNewId()
        {
            // Check if we need a new segment
            size_t segIdx = static_cast<size_t>(m_nextId >> m_config.entitiesPerSegmentShift);
            
            // Ensure segment index is large enough
            if (segIdx >= m_segmentIndex.size()) ASTRA_UNLIKELY
            {
                m_segmentIndex.resize(segIdx + 1, Segment::INVALID_SEGMENT);
            }
            
            // Create segment if it doesn't exist
            if (m_segmentIndex[segIdx] == Segment::INVALID_SEGMENT) ASTRA_UNLIKELY
            {
                IDType baseId = static_cast<IDType>(segIdx << m_config.entitiesPerSegmentShift);
                auto segment = std::make_unique<Segment>(baseId, m_config.entitiesPerSegment);
                
                m_segments.push_back(std::move(segment));
                m_segmentIndex[segIdx] = m_segments.size() - 1;
            }
            
            return m_nextId++;
        }
        
        // Rebuild segment index after compaction
        void RebuildSegmentIndex()
        {
            std::fill(m_segmentIndex.begin(), m_segmentIndex.end(), Segment::INVALID_SEGMENT);
            
            for (size_t i = 0; i < m_segments.size(); ++i)
            {
                if (m_segments[i]) ASTRA_LIKELY
                {
                    size_t segIdx = static_cast<size_t>(m_segments[i]->baseId >> m_config.entitiesPerSegmentShift);
                    if (segIdx < m_segmentIndex.size())
                    {
                        m_segmentIndex[segIdx] = i;
                    }
                }
            }
        }

    public:
        /**
        * Default constructor
        */
        EntityPool() = default;

        /**
        * Constructor with capacity reservation
        * @param capacity Initial capacity to reserve
        */
        explicit EntityPool(std::size_t capacity)
        {
            Reserve(capacity);
        }
        
        /**
        * Constructor with custom memory configuration
        * @param config Memory management configuration
        */
        explicit EntityPool(const Config& config)
            : m_config(config)
        {
        }

        /**
        * Creates a new entity or recycles a destroyed one
        * @return Valid entity with unique version
        */
        ASTRA_NODISCARD Entity Create() noexcept
        {
            IDType id;
            VersionType version;

            // Try global free list first (spans segments)
            if (!m_globalFreeList.empty()) ASTRA_UNLIKELY
            {
                auto entry = m_globalFreeList.back();
                m_globalFreeList.pop_back();
                
                id = entry.id;
                version = entry.nextVersion;
                
                if (auto* segment = GetSegment(id)) ASTRA_LIKELY
                {
                    segment->versions[segment->ToLocal(id)] = version;
                    ++segment->aliveCount;
                }
            }
            // Try segment-local free lists
            else if (auto [seg, entry] = FindFreeInSegments(); seg) ASTRA_LIKELY
            {
                id = entry.id;
                version = entry.nextVersion;
                seg->versions[seg->ToLocal(id)] = version;
                ++seg->aliveCount;
            }
            // Allocate new
            else
            {
                id = AllocateNewId();
                version = INITIAL_VERSION;
                
                assert(id <= Entity::ID_MASK && "Entity ID overflow");
                
                if (auto* segment = GetSegment(id)) ASTRA_LIKELY
                {
                    segment->versions[segment->ToLocal(id)] = version;
                    ++segment->aliveCount;
                }
            }

            ++m_aliveCount;
            return Entity(id, version);
        }

        /**
        * Creates multiple entities in batch for better performance
        * Optimized for large batches with contiguous allocation
        * @param count Number of entities to create
        * @param out Output iterator for created entities
        */
        template<typename OutputIt>
        void CreateBatch(std::size_t count, OutputIt out) noexcept
        {
            if (count == 0) ASTRA_UNLIKELY return;
            
            // For small batches, simple loop is fine
            if (count < 32) ASTRA_LIKELY
            {
                for (std::size_t i = 0; i < count; ++i)
                {
                    *out++ = Create();
                }
                return;
            }
            
            // Large batch optimization: Try contiguous allocation
            size_t freshAvailable = (Entity::ID_MASK + 1) - m_nextId;
            size_t fromFresh = std::min(count, freshAvailable);
            
            if (fromFresh > 0) ASTRA_LIKELY
            {
                // Pre-allocate all needed segments
                IDType endId = static_cast<IDType>(m_nextId + fromFresh);
                size_t startSegIdx = static_cast<size_t>(m_nextId >> m_config.entitiesPerSegmentShift);
                size_t endSegIdx = static_cast<size_t>((endId - 1) >> m_config.entitiesPerSegmentShift);
                
                // Ensure segment index array is large enough
                if (endSegIdx >= m_segmentIndex.size()) ASTRA_UNLIKELY
                {
                    m_segmentIndex.resize(endSegIdx + 1, Segment::INVALID_SEGMENT);
                }
                
                // Create all needed segments upfront
                for (size_t s = startSegIdx; s <= endSegIdx; ++s)
                {
                    if (m_segmentIndex[s] == Segment::INVALID_SEGMENT) ASTRA_UNLIKELY
                    {
                        IDType baseId = static_cast<IDType>(s << m_config.entitiesPerSegmentShift);
                        auto segment = std::make_unique<Segment>(baseId, m_config.entitiesPerSegment);
                        m_segments.push_back(std::move(segment));
                        m_segmentIndex[s] = m_segments.size() - 1;
                    }
                }
                
                // Batch allocate contiguous IDs
                IDType startId = m_nextId;
                m_nextId += static_cast<IDType>(fromFresh);
                m_aliveCount += fromFresh;
                
                // Initialize all entities in segments
                for (size_t i = 0; i < fromFresh; ++i)
                {
                    IDType id = startId + static_cast<IDType>(i);
                    auto* segment = GetSegment(id);
                    ASTRA_ASSERT(segment, "Failed to get segment for batch-allocated ID");
                    
                    size_t localIdx = segment->ToLocal(id);
                    ASTRA_ASSERT(localIdx < segment->capacity, "Local index out of bounds");
                    
                    segment->versions[localIdx] = INITIAL_VERSION;
                    ++segment->aliveCount;
                    
                    *out++ = Entity(id, INITIAL_VERSION);
                }
                
                count -= fromFresh;
            }
            
            // Handle remainder from free lists
            for (size_t i = 0; i < count; ++i)
            {
                *out++ = Create();
            }
        }

        /**
        * Destroys an entity, making it available for recycling
        * @param entity Entity to destroy
        * @return true if entity was valid and destroyed, false otherwise
        */
        bool Destroy(Entity entity) noexcept
        {
            if (!IsValid(entity)) ASTRA_UNLIKELY
            {
                return false;
            }

            const IDType id = entity.GetID();
            const VersionType currentVersion = entity.GetVersion();
            
            auto* segment = GetSegment(id);
            if (!segment) ASTRA_UNLIKELY
            {
                return false;
            }
            
            size_t localIdx = segment->ToLocal(id);
            assert(segment->versions[localIdx] == currentVersion && "Version mismatch in destroy");

            // Calculate next version with wraparound
            VersionType nextVersion = currentVersion + 1;
            if (nextVersion == NULL_VERSION) ASTRA_UNLIKELY  // Wrap from 255 to 1
            {
                nextVersion = INITIAL_VERSION;
            }

            // Mark as destroyed (NULL_VERSION = 0 indicates free slot)
            segment->versions[localIdx] = NULL_VERSION;

            // Add to segment's local free list for better locality
            segment->freeList.push_back({id, nextVersion});
            --segment->aliveCount;

            --m_aliveCount;
            
            // Smart release check: only when segment becomes empty
            if (m_config.autoRelease && segment->aliveCount == 0) ASTRA_UNLIKELY
            {
                MaybeReleaseSegments();
            }
            
            return true;
        }

        /**
        * Destroys multiple entities in batch
        * Optimized for cache locality by grouping operations by segment
        * @param first Iterator to first entity
        * @param last Iterator past last entity
        * @return Number of entities successfully destroyed
        */
        template<typename InputIt>
        std::size_t DestroyBatch(InputIt first, InputIt last) noexcept
        {
            const size_t estimatedCount = std::distance(first, last);
            
            // For small batches, use simple approach
            if (estimatedCount < 32) ASTRA_LIKELY
            {
                std::size_t destroyed = 0;
                for (auto it = first; it != last; ++it)
                {
                    if (Destroy(*it)) ASTRA_LIKELY
                    {
                        ++destroyed;
                    }
                }
                return destroyed;
            }
            
            // Large batch optimization: Group by segment
            // Use small vector optimization for common case
            struct BatchEntry {
                size_t localIdx;
                VersionType nextVersion;
            };
            
            // Most batches will hit only a few segments
            constexpr size_t SMALL_SEGMENT_COUNT = 8;
            std::pair<Segment*, SmallVector<BatchEntry, 8>> smallBatches[SMALL_SEGMENT_COUNT];
            size_t smallBatchCount = 0;
            
            // Fallback for many segments
            std::unordered_map<Segment*, SmallVector<BatchEntry, 8>> largeBatches;
            
            std::size_t destroyed = 0;
            
            // Group entities by segment
            for (auto it = first; it != last; ++it)
            {
                if (!IsValid(*it)) ASTRA_UNLIKELY continue;
                
                IDType id = it->GetID();
                auto* segment = GetSegment(id);
                if (!segment) ASTRA_UNLIKELY continue;
                
                size_t localIdx = segment->ToLocal(id);
                VersionType nextVersion = it->GetVersion() + 1;
                if (nextVersion == NULL_VERSION) ASTRA_UNLIKELY nextVersion = INITIAL_VERSION;
                
                // Try small batch array first
                bool found = false;
                for (size_t i = 0; i < smallBatchCount; ++i)
                {
                    if (smallBatches[i].first == segment) ASTRA_LIKELY
                    {
                        smallBatches[i].second.push_back({localIdx, nextVersion});
                        found = true;
                        break;
                    }
                }
                
                if (!found) ASTRA_UNLIKELY
                {
                    if (smallBatchCount < SMALL_SEGMENT_COUNT) ASTRA_LIKELY
                    {
                        smallBatches[smallBatchCount].first = segment;
                        smallBatches[smallBatchCount].second.push_back({localIdx, nextVersion});
                        ++smallBatchCount;
                    }
                    else
                    {
                        largeBatches[segment].push_back({localIdx, nextVersion});
                    }
                }
                
                ++destroyed;
            }
            
            // Process small batches
            for (size_t i = 0; i < smallBatchCount; ++i)
            {
                auto* segment = smallBatches[i].first;
                auto& entries = smallBatches[i].second;
                
                // Reserve space in segment's free list
                segment->freeList.reserve(segment->freeList.size() + entries.size());
                
                // Batch update
                for (const auto& [localIdx, nextVersion] : entries)
                {
                    segment->versions[localIdx] = NULL_VERSION;
                    segment->freeList.push_back({segment->baseId + static_cast<IDType>(localIdx), nextVersion});
                }
                
                segment->aliveCount -= entries.size();
            }
            
            // Process large batches (if any)
            for (auto& [segment, entries] : largeBatches)
            {
                segment->freeList.reserve(segment->freeList.size() + entries.size());
                
                for (const auto& [localIdx, nextVersion] : entries)
                {
                    segment->versions[localIdx] = NULL_VERSION;
                    segment->freeList.push_back({segment->baseId + static_cast<IDType>(localIdx), nextVersion});
                }
                
                segment->aliveCount -= entries.size();
            }
            
            m_aliveCount -= destroyed;
            
            // Single deferred memory check
            if (m_config.autoRelease && destroyed > 0) ASTRA_UNLIKELY
            {
                MaybeReleaseSegments();
            }
            
            return destroyed;
        }

        /**
        * Checks if an entity is valid (alive)
        * @param entity Entity to check
        * @return true if entity exists and version matches
        */
        ASTRA_NODISCARD bool IsValid(Entity entity) const noexcept
        {
            const IDType id = entity.GetID();
            const VersionType version = entity.GetVersion();

            // Get the segment containing this ID
            size_t segIdx = static_cast<size_t>(id >> m_config.entitiesPerSegmentShift);
            if (segIdx >= m_segmentIndex.size()) ASTRA_UNLIKELY return false;
            
            size_t idx = m_segmentIndex[segIdx];
            if (idx == Segment::INVALID_SEGMENT) ASTRA_UNLIKELY return false;
            
            auto* segment = m_segments[idx].get();
            if (!segment) ASTRA_UNLIKELY return false;
            
            // Check version match
            size_t localIdx = segment->ToLocal(id);
            return segment->versions[localIdx] == version && version != NULL_VERSION;
        }

        /**
        * Gets the current version of an entity ID
        * @param id Entity ID to query
        * @return Current version or NULL_VERSION if invalid
        */
        ASTRA_NODISCARD VersionType GetVersion(IDType id) const noexcept
        {
            size_t segIdx = static_cast<size_t>(id >> m_config.entitiesPerSegmentShift);
            if (segIdx >= m_segmentIndex.size()) ASTRA_UNLIKELY return NULL_VERSION;
            
            size_t idx = m_segmentIndex[segIdx];
            if (idx == Segment::INVALID_SEGMENT) ASTRA_UNLIKELY return NULL_VERSION;
            
            auto* segment = m_segments[idx].get();
            if (!segment) ASTRA_UNLIKELY return NULL_VERSION;
            
            size_t localIdx = segment->ToLocal(id);
            return segment->versions[localIdx];
        }

        /**
        * Clears all entities and resets the pool
        */
        void Clear() noexcept
        {
            m_segments.clear();
            m_segmentIndex.clear();
            m_globalFreeList.clear();
            m_nextId = 0;
            m_aliveCount = 0;
        }

        /**
        * Reserves capacity for entities
        * @param capacity Number of entities to reserve space for
        */
        void Reserve(std::size_t capacity)
        {
            // Calculate how many segments we need (ceiling division)
            size_t segmentsNeeded = (capacity + m_config.entitiesPerSegmentMask) >> m_config.entitiesPerSegmentShift;
            
            // Reserve space in containers
            m_segments.reserve(segmentsNeeded);
            m_segmentIndex.reserve(segmentsNeeded);
            
            // Reserve some space in global free list (assume ~10% cross-segment recycling)
            m_globalFreeList.reserve(capacity / 10);
        }

        /**
        * Returns the number of alive entities
        */
        ASTRA_NODISCARD std::size_t Size() const noexcept
        {
            return m_aliveCount;
        }

        /**
        * Returns the total capacity (highest ID + 1)
        */
        ASTRA_NODISCARD std::size_t Capacity() const noexcept
        {
            // Total capacity is the next ID to be allocated
            return m_nextId;
        }

        /**
        * Returns the number of recycled entities in the free list
        */
        ASTRA_NODISCARD std::size_t RecycledCount() const noexcept
        {
            size_t total = m_globalFreeList.size();
            for (const auto& segment : m_segments)
            {
                if (segment)
                {
                    total += segment->freeList.size();
                }
            }
            return total;
        }

        /**
        * Checks if the pool is empty
        */
        ASTRA_NODISCARD bool Empty() const noexcept
        {
            return m_aliveCount == 0;
        }

        /**
        * Shrinks internal storage to fit current usage
        */
        void ShrinkToFit()
        {
            // Compact segment vector
            m_segments.erase(
                std::remove(m_segments.begin(), m_segments.end(), nullptr),
                m_segments.end()
            );
            m_segments.shrink_to_fit();
            
            // Rebuild index after compaction
            RebuildSegmentIndex();
            
            // Shrink other containers
            m_globalFreeList.shrink_to_fit();
        }
        
        /**
        * Checks and potentially releases empty or underutilized segments
        */
        void MaybeReleaseSegments()
        {
            if (!m_config.autoRelease) ASTRA_UNLIKELY return;
            
            size_t emptyCount = 0;
            
            // Count and potentially release segments
            for (size_t i = 0; i < m_segments.size(); ++i)
            {
                auto& segment = m_segments[i];
                if (!segment) ASTRA_UNLIKELY continue;
                
                if (segment->aliveCount == 0) ASTRA_UNLIKELY
                {
                    ++emptyCount;
                    
                    // Keep some empty segments ready
                    if (emptyCount > m_config.maxEmptySegments) ASTRA_UNLIKELY
                    {
                        // Clear the segment's free list - these IDs are being lost
                        // This is acceptable as it's a tiny fraction of the 64K range
                        segment->freeList.clear();
                        
                        // Clear segment index entries
                        size_t segBase = static_cast<size_t>(segment->baseId >> m_config.entitiesPerSegmentShift);
                        if (segBase < m_segmentIndex.size()) ASTRA_LIKELY
                        {
                            m_segmentIndex[segBase] = Segment::INVALID_SEGMENT;
                        }
                        
                        // Release the segment
                        segment.reset();
                    }
                }
                else if (segment->Usage() < m_config.releaseThreshold) ASTRA_UNLIKELY
                {
                    // Consider compacting very sparse segments in the future
                    // For now, just track them
                }
            }
            
            // Clean up m_segments vector if many nullptrs
            size_t nullCount = std::count(m_segments.begin(), m_segments.end(), nullptr);
            if (nullCount > m_segments.size() / 2) ASTRA_UNLIKELY
            {
                m_segments.erase(
                    std::remove(m_segments.begin(), m_segments.end(), nullptr),
                    m_segments.end()
                );
                RebuildSegmentIndex();
            }
        }

        /**
        * Iterator for alive entities only
        */
        class Iterator
        {
        private:
            const EntityPool* m_pool;
            size_t m_segmentIdx;
            size_t m_localIdx;
            const Segment* m_currentSegment;

            void SkipToNextValid() noexcept
            {
                while (m_segmentIdx < m_pool->m_segments.size())
                {
                    // Skip null segments
                    while (m_segmentIdx < m_pool->m_segments.size() && 
                           !m_pool->m_segments[m_segmentIdx]) ASTRA_UNLIKELY
                    {
                        ++m_segmentIdx;
                        m_localIdx = 0;
                    }
                    
                    if (m_segmentIdx >= m_pool->m_segments.size()) ASTRA_UNLIKELY
                        break;
                        
                    m_currentSegment = m_pool->m_segments[m_segmentIdx].get();
                    
                    // Find next valid entity in current segment
                    while (m_localIdx < m_currentSegment->capacity) ASTRA_LIKELY
                    {
                        if (m_currentSegment->versions[m_localIdx] != NULL_VERSION) ASTRA_LIKELY
                        {
                            return; // Found valid entity
                        }
                        ++m_localIdx;
                    }
                    
                    // Move to next segment
                    ++m_segmentIdx;
                    m_localIdx = 0;
                }
                
                // End iterator
                m_currentSegment = nullptr;
            }

        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = Entity;
            using difference_type = std::ptrdiff_t;
            using pointer = const Entity*;
            using reference = Entity;

            Iterator(const EntityPool* pool, bool isEnd) noexcept 
                : m_pool(pool)
                , m_segmentIdx(isEnd ? pool->m_segments.size() : 0)
                , m_localIdx(0)
                , m_currentSegment(nullptr)
            {
                if (!isEnd) ASTRA_LIKELY
                {
                    SkipToNextValid();
                }
            }

            ASTRA_NODISCARD Entity operator*() const noexcept
            {
                assert(m_currentSegment && "Iterator out of range");
                IDType id = m_currentSegment->baseId + static_cast<IDType>(m_localIdx);
                VersionType version = m_currentSegment->versions[m_localIdx];
                assert(version != NULL_VERSION && "Iterator on invalid entity");
                return Entity(id, version);
            }

            Iterator& operator++() noexcept
            {
                ++m_localIdx;
                SkipToNextValid();
                return *this;
            }

            Iterator operator++(int) noexcept
            {
                Iterator tmp = *this;
                ++(*this);
                return tmp;
            }

            ASTRA_NODISCARD bool operator==(const Iterator& other) const noexcept
            {
                return m_pool == other.m_pool && 
                       m_segmentIdx == other.m_segmentIdx && 
                       m_localIdx == other.m_localIdx;
            }

            ASTRA_NODISCARD bool operator!=(const Iterator& other) const noexcept
            {
                return !(*this == other);
            }
        };

        /**
        * Returns iterator to first alive entity
        */
        ASTRA_NODISCARD Iterator begin() const noexcept
        {
            return Iterator(this, false);
        }

        /**
        * Returns iterator past last entity
        */
        ASTRA_NODISCARD Iterator end() const noexcept
        {
            return Iterator(this, true);
        }

        /**
        * Performs consistency checks (debug only)
        */
        void Validate() const noexcept
        {
#ifndef ASTRA_BUILD_DEBUG
            // Count alive entities across all segments
            std::size_t aliveCount = 0;
            std::size_t totalFreeListSize = m_globalFreeList.size();
            
            for (const auto& segment : m_segments)
            {
                if (!segment) ASTRA_UNLIKELY continue;
                
                std::size_t segmentAlive = 0;
                totalFreeListSize += segment->freeList.size();
                
                for (size_t i = 0; i < segment->capacity; ++i)
                {
                    if (segment->versions[i] != NULL_VERSION) ASTRA_LIKELY
                    {
                        ++segmentAlive;
                        ++aliveCount;
                    }
                }
                
                assert(segmentAlive == segment->aliveCount && "Segment alive count mismatch");
            }

            assert(aliveCount == m_aliveCount && "Total alive count mismatch");
            assert(m_nextId <= Entity::ID_MASK + 1 && "Next ID overflow");
            
            // Verify segment index integrity
            for (size_t i = 0; i < m_segmentIndex.size(); ++i)
            {
                if (m_segmentIndex[i] != Segment::INVALID_SEGMENT) ASTRA_LIKELY
                {
                    assert(m_segmentIndex[i] < m_segments.size() && "Invalid segment index");
                    assert(m_segments[m_segmentIndex[i]] != nullptr && "Segment index points to null");
                    
                    const auto& segment = m_segments[m_segmentIndex[i]];
                    size_t expectedSegIdx = static_cast<size_t>(segment->baseId >> m_config.entitiesPerSegmentShift);
                    assert(expectedSegIdx == i && "Segment index mismatch");
                }
            }
#endif
        }

        /**
        * Serializes the EntityPool state to a BinaryWriter
        * @param writer BinaryWriter to serialize to
        */
        void Serialize(BinaryWriter& writer) const
        {
            // Write configuration
            writer(m_config.entitiesPerSegment);
            writer(m_config.entitiesPerSegmentShift);
            writer(m_config.entitiesPerSegmentMask);
            writer(m_config.releaseThreshold);
            writer(m_config.autoRelease);
            writer(m_config.maxEmptySegments);
            
            // Write state
            writer(m_nextId);
            writer(m_aliveCount);
            
            // Count non-null segments
            uint32_t activeSegmentCount = 0;
            for (const auto& segment : m_segments)
            {
                if (segment) ++activeSegmentCount;
            }
            writer(activeSegmentCount);
            
            // Write each active segment
            for (const auto& segment : m_segments)
            {
                if (!segment) continue;
                
                writer(segment->baseId);
                writer(segment->capacity);
                writer(segment->aliveCount);
                
                // Write versions array
                writer.WriteBytes(segment->versions.get(), segment->capacity * sizeof(VersionType));
                
                // Write free list
                uint32_t freeListSize = static_cast<uint32_t>(segment->freeList.size());
                writer(freeListSize);
                for (const auto& entry : segment->freeList)
                {
                    writer(entry.id);
                    writer(entry.nextVersion);
                }
            }
            
            // Write global free list
            uint32_t globalFreeListSize = static_cast<uint32_t>(m_globalFreeList.size());
            writer(globalFreeListSize);
            for (const auto& entry : m_globalFreeList)
            {
                writer(entry.id);
                writer(entry.nextVersion);
            }
        }
        
        /**
        * Deserializes EntityPool state from a BinaryReader
        * @param reader BinaryReader to deserialize from
        * @return Deserialized EntityPool or error
        */
        static Result<std::unique_ptr<EntityPool>, SerializationError> Deserialize(BinaryReader& reader)
        {
            auto pool = std::make_unique<EntityPool>();
            
            // Read configuration
            reader(pool->m_config.entitiesPerSegment);
            reader(pool->m_config.entitiesPerSegmentShift);
            reader(pool->m_config.entitiesPerSegmentMask);
            reader(pool->m_config.releaseThreshold);
            reader(pool->m_config.autoRelease);
            reader(pool->m_config.maxEmptySegments);
            
            if (reader.HasError())
            {
                return Result<std::unique_ptr<EntityPool>, SerializationError>::Err(reader.GetError());
            }
            
            // Read state
            reader(pool->m_nextId);
            reader(pool->m_aliveCount);
            
            // Read segments
            uint32_t activeSegmentCount;
            reader(activeSegmentCount);
            
            if (reader.HasError())
            {
                return  Result<std::unique_ptr<EntityPool>, SerializationError>::Err(reader.GetError());
            }
            
            pool->m_segments.reserve(activeSegmentCount);
            
            // Calculate max segment index needed
            size_t maxSegmentIdx = 0;
            
            for (uint32_t i = 0; i < activeSegmentCount; ++i)
            {
                IDType baseId, capacity;
                size_t aliveCount;
                
                reader(baseId);
                reader(capacity);
                reader(aliveCount);
                
                if (reader.HasError())
                {
                    return Result<std::unique_ptr<EntityPool>, SerializationError>::Err(reader.GetError());
                }
                
                // Create segment
                auto segment = std::make_unique<Segment>(baseId, capacity);
                segment->aliveCount = aliveCount;
                
                // Read versions array
                reader.ReadBytes(segment->versions.get(), capacity * sizeof(VersionType));
                
                // Read free list
                uint32_t freeListSize;
                reader(freeListSize);
                segment->freeList.reserve(freeListSize);
                
                for (uint32_t j = 0; j < freeListSize; ++j)
                {
                    FreeListEntry entry;
                    reader(entry.id);
                    reader(entry.nextVersion);
                    segment->freeList.push_back(entry);
                }
                
                if (reader.HasError())
                {
                    return Result<std::unique_ptr<EntityPool>, SerializationError>::Err(reader.GetError());
                }
                
                // Update max segment index
                size_t segIdx = static_cast<size_t>(baseId >> pool->m_config.entitiesPerSegmentShift);
                maxSegmentIdx = std::max(maxSegmentIdx, segIdx);
                
                pool->m_segments.push_back(std::move(segment));
            }
            
            // Build segment index
            pool->m_segmentIndex.resize(maxSegmentIdx + 1, Segment::INVALID_SEGMENT);
            for (size_t i = 0; i < pool->m_segments.size(); ++i)
            {
                if (pool->m_segments[i])
                {
                    size_t segIdx = static_cast<size_t>(pool->m_segments[i]->baseId >> pool->m_config.entitiesPerSegmentShift);
                    if (segIdx < pool->m_segmentIndex.size())
                    {
                        pool->m_segmentIndex[segIdx] = i;
                    }
                }
            }
            
            // Read global free list
            uint32_t globalFreeListSize;
            reader(globalFreeListSize);
            pool->m_globalFreeList.reserve(globalFreeListSize);
            
            for (uint32_t i = 0; i < globalFreeListSize; ++i)
            {
                FreeListEntry entry;
                reader(entry.id);
                reader(entry.nextVersion);
                pool->m_globalFreeList.push_back(entry);
            }
            
            if (reader.HasError())
            {
                return Result<std::unique_ptr<EntityPool>, SerializationError>::Err(reader.GetError());
            }
            
            return Result<std::unique_ptr<EntityPool>, SerializationError>::Ok(std::move(pool));
        }
    };
}
