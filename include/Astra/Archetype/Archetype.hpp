#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "../Component/Component.hpp"
#include "../Container/Bitmap.hpp"
#include "../Container/FlatMap.hpp"
#include "../Container/SmallVector.hpp"
#include "../Core/Base.hpp"
#include "../Core/Memory.hpp"
#include "../Core/Result.hpp"
#include "../Core/TypeID.hpp"
#include "../Entity/Entity.hpp"
#include "../Entity/EntityRange.hpp"
#include "../Platform/Hardware.hpp"
#include "../Platform/Platform.hpp"
#include "../Platform/Simd.hpp"
#include "../Serialization/BinaryReader.hpp"
#include "../Serialization/BinaryWriter.hpp"
#include "ArchetypeChunkPool.hpp"

namespace Astra
{
    struct EntityLocation
    {
        uint32_t chunkIndex;
        uint32_t entityIndex;
        
        constexpr EntityLocation() noexcept :
            chunkIndex(std::numeric_limits<uint32_t>::max()),
            entityIndex(std::numeric_limits<uint32_t>::max())
        {}
        
        constexpr EntityLocation(uint32_t chunk, uint32_t entity) noexcept : chunkIndex(chunk), entityIndex(entity) {}
        
        ASTRA_NODISCARD constexpr static EntityLocation Create(size_t chunkIdx, size_t entityIdx) noexcept
        {
            return EntityLocation(static_cast<uint32_t>(chunkIdx), static_cast<uint32_t>(entityIdx));
        }
        
        ASTRA_NODISCARD constexpr size_t GetChunkIndex() const noexcept
        {
            return chunkIndex;
        }
        
        ASTRA_NODISCARD constexpr size_t GetEntityIndex() const noexcept
        {
            return entityIndex;
        }
        
        ASTRA_NODISCARD constexpr bool IsValid() const noexcept
        {
            return chunkIndex != std::numeric_limits<uint32_t>::max();
        }
        
        constexpr bool operator==(const EntityLocation& other) const noexcept 
        { 
            return chunkIndex == other.chunkIndex && entityIndex == other.entityIndex; 
        }
        constexpr bool operator!=(const EntityLocation& other) const noexcept 
        { 
            return !(*this == other); 
        }
        constexpr bool operator<(const EntityLocation& other) const noexcept 
        { 
            return chunkIndex < other.chunkIndex || (chunkIndex == other.chunkIndex && entityIndex < other.entityIndex); 
        }
        constexpr bool operator>(const EntityLocation& other) const noexcept 
        { 
            return other < *this; 
        }
        constexpr bool operator<=(const EntityLocation& other) const noexcept 
        { 
            return !(other < *this); 
        }
        constexpr bool operator>=(const EntityLocation& other) const noexcept 
        { 
            return !(*this < other); 
        }
    };
    
    class ArchetypeManager;
    class Registry;

    using ArchetypeChunk = ArchetypeChunkPool::Chunk;
    
    template<Component... Components>
    ASTRA_NODISCARD constexpr ComponentMask MakeComponentMask() noexcept
    {
        ComponentMask mask{};
        ((mask.Set(TypeID<Components>::Value())), ...);
        return mask;
    }
    
    class Archetype
    {
    public:
        explicit Archetype(ComponentMask mask) :
            m_mask(mask),
            m_componentCount(mask.Count()),
            m_entityCount(0),
            m_entitiesPerChunk(0),
            m_entitiesPerChunkShift(0),
            m_entitiesPerChunkMask(0),
            m_initialized(false)
        {}
        
        ~Archetype() = default;
        
        void Initialize(const std::vector<ComponentDescriptor>& componentDescriptors)
        {
            if (m_initialized) ASTRA_UNLIKELY
                return;

            m_componentDescriptors = componentDescriptors;

            // Calculate entities per chunk based on component sizes with cache line alignment
            size_t totalOffset = 0;
            size_t perEntitySize = 0;

            // First, calculate the fixed overhead for cache line alignment
            for (size_t i = 0; i < m_componentDescriptors.size(); ++i)
            {
                // Each component array starts at a cache line boundary
                totalOffset = (totalOffset + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1);
                totalOffset += CACHE_LINE_SIZE; // Account for at least one cache line per component

                // Track per-entity size
                perEntitySize += m_componentDescriptors[i].size;
            }

            // Calculate how many entities fit after accounting for alignment overhead
            // Get chunk size from the pool
            size_t chunkSize = m_chunkPool ? m_chunkPool->GetChunkSize() : ArchetypeChunkPool::DEFAULT_CHUNK_SIZE;
            size_t remainingSpace = chunkSize - totalOffset;
            size_t maxEntities = perEntitySize > 0 ? remainingSpace / perEntitySize : 256;

            // Round down to nearest power of 2 for optimal cache alignment and fast modulo
            // std::bit_floor gives us the largest power of 2 <= maxEntities
            m_entitiesPerChunk = maxEntities > 0 ? std::bit_floor(maxEntities) : 1;

            // Ensure at least one entity per chunk (already guaranteed by bit_floor logic above)
            m_entitiesPerChunk = std::max(size_t(1), m_entitiesPerChunk);

            // Pre-calculate mask for fast modulo operations (works because m_entitiesPerChunk is power of 2)
            m_entitiesPerChunkMask = m_entitiesPerChunk - 1;

            // Pre-calculate shift amount for fast division (log2 of power-of-2 value)
            // std::countr_zero counts trailing zeros, which equals log2 for powers of 2
            m_entitiesPerChunkShift = std::countr_zero(m_entitiesPerChunk);

            m_initialized = true;

            // Pre-allocate first chunk for any archetype (even empty ones need to store entities)
            auto chunk = m_chunkPool->CreateChunk(m_entitiesPerChunk, m_componentDescriptors);
            if (!chunk) ASTRA_UNLIKELY
            {
                // Mark as failed initialization - caller must check IsInitialized()
                m_initialized = false;
                return;
            }
            m_chunks.emplace_back(std::move(chunk));
        }

        template<Component T>
        ASTRA_NODISCARD T* GetComponent(EntityLocation location)
        {
            ComponentID id = TypeID<T>::Value();
            if (!m_mask.Test(id)) ASTRA_UNLIKELY
                return nullptr;
                
            size_t chunkIdx = location.GetChunkIndex();
            size_t entityIdx = location.GetEntityIndex();
            
            assert(chunkIdx < m_chunks.size());
            assert(entityIdx < m_chunks[chunkIdx]->GetCount());
            
            return m_chunks[chunkIdx]->GetComponent<T>(entityIdx);
        }

        /**
        * Set component data
        */
        template<typename T>
        void SetComponent(EntityLocation location, T&& value)
        {
            using DecayedType = std::decay_t<T>;
            static_assert(Component<DecayedType>, "T must be a Component");

            DecayedType* ptr = GetComponent<DecayedType>(location);
            assert(ptr != nullptr);
            *ptr = std::forward<T>(value);
        }

        /**
        * Batch set component values for multiple entities
        * @param locations Where to set the component
        * @param value Value to set
        */
        template<Component T>
        void BatchSetComponent(std::span<const EntityLocation> locations, const T& value)
        {
            if (locations.empty()) return;

            // Group by chunk for efficient processing
            FlatMap<size_t, std::vector<size_t>> chunkBatches;

            for (const auto& loc : locations)
            {
                size_t chunkIdx = loc.GetChunkIndex();
                size_t entityIdx = loc.GetEntityIndex();
                chunkBatches[chunkIdx].push_back(entityIdx);
            }

            // Batch construct component in each chunk
            for (auto& [chunkIdx, indices] : chunkBatches)
            {
                assert(chunkIdx < m_chunks.size());
                m_chunks[chunkIdx]->BatchConstructComponent<T>(indices, value);
            }
        }

        EntityLocation AddEntity(Entity entity)
        {
            // Find or create a chunk with space
            auto [chunkIdx, wasCreated] = FindOrCreateChunkWithSpace();
            if (chunkIdx == INVALID_CHUNK_INDEX) ASTRA_UNLIKELY
            {
                return EntityLocation();  // Allocation failed
            }

            size_t entityIdx = m_chunks[chunkIdx]->AddEntity(entity);
            ++m_entityCount;

            // If this chunk is now full, update first non-full index
            if (m_chunks[chunkIdx]->IsFull()) ASTRA_UNLIKELY
            {
                // Will be updated on next AddEntity call
                m_firstNonFullChunkIdx = chunkIdx + 1;
            }

            return EntityLocation::Create(chunkIdx, entityIdx);
        }

        std::vector<EntityLocation> AddEntities(std::span<const Entity> entities)
        {
            size_t count = entities.size();
            if (count == 0) ASTRA_UNLIKELY
                return {};

            std::vector<EntityLocation> locations;
            locations.reserve(count);

            // Calculate and allocate needed chunks upfront
            size_t remainingCapacity = CalculateRemainingCapacity();
            if (count > remainingCapacity) ASTRA_UNLIKELY
            {
                size_t additionalNeeded = count - remainingCapacity;
                size_t newChunksNeeded = (additionalNeeded + m_entitiesPerChunk - 1) >> m_entitiesPerChunkShift;

                // Create chunks directly from pool
                for (size_t i = 0; i < newChunksNeeded; ++i)
                {
                    auto chunk = m_chunkPool->CreateChunk(m_entitiesPerChunk, m_componentDescriptors);
                    if (!chunk) ASTRA_UNLIKELY
                    {
                        return locations;
                    }
                    m_chunks.emplace_back(std::move(chunk));
                }
            }

            // Now batch fill chunks
            size_t entityIdx = 0;
            size_t chunkIdx = m_firstNonFullChunkIdx;

            while (entityIdx < count && chunkIdx < m_chunks.size()) ASTRA_LIKELY
            {
                auto& chunk = m_chunks[chunkIdx];
                size_t available = m_entitiesPerChunk - chunk->GetCount();

                if (available > 0) ASTRA_LIKELY
                {
                    size_t toAdd = std::min(available, count - entityIdx);
                    size_t startIdx = chunk->GetCount();

                    // Batch add to chunk
                    chunk->BatchAddEntities(entities.subspan(entityIdx, toAdd));

                    for (size_t i = 0; i < toAdd; ++i)
                    {
                        locations.push_back(EntityLocation::Create(chunkIdx, startIdx + i));
                    }

                    entityIdx += toAdd;

                    // Update first non-full chunk if this one is now full
                    if (chunk->IsFull() && chunkIdx == m_firstNonFullChunkIdx) ASTRA_UNLIKELY
                    {
                        m_firstNonFullChunkIdx = chunkIdx + 1;
                    }
                }

                ++chunkIdx;
            }

            m_entityCount += entityIdx;
            return locations;
        }

        std::optional<Entity> RemoveEntity(EntityLocation location)
        {
            size_t chunkIdx = location.GetChunkIndex();
            size_t entityIdx = location.GetEntityIndex();

            assert(chunkIdx < m_chunks.size());

            // Remove from chunk - chunk handles the swap-and-pop
            auto movedEntity = m_chunks[chunkIdx]->RemoveEntity(entityIdx);

            --m_entityCount;

            // Update first non-full chunk index if this chunk now has space
            if (chunkIdx < m_firstNonFullChunkIdx && !m_chunks[chunkIdx]->IsFull()) ASTRA_UNLIKELY
            {
                m_firstNonFullChunkIdx = chunkIdx;
            }

            if (chunkIdx == m_chunks.size() - 1 && chunkIdx > 0 && m_chunks[chunkIdx]->IsEmpty()) ASTRA_UNLIKELY
            {
                m_chunks.pop_back();

                // If we removed the last chunk, we might need to update first non-full index
                if (m_firstNonFullChunkIdx >= m_chunks.size()) ASTRA_UNLIKELY
                {
                    m_firstNonFullChunkIdx = m_chunks.size() > 0 ? m_chunks.size() - 1 : 0;
                }
            }

            return movedEntity;
        }

        std::vector<std::pair<Entity, EntityLocation>> RemoveEntities(std::span<const EntityLocation> locations, bool deferChunkCleanup = false)
        {
            if (locations.empty()) ASTRA_UNLIKELY
                return {};

            std::vector<std::pair<Entity, EntityLocation>> movedEntities;
            movedEntities.reserve(locations.size());

            std::vector<EntityLocation> sortedLocations(locations.begin(), locations.end());
            std::sort(sortedLocations.begin(), sortedLocations.end(), std::greater<EntityLocation>());

            size_t lowestModifiedChunk = std::numeric_limits<size_t>::max();

            // Process removals
            for (EntityLocation location : sortedLocations)
            {
                size_t chunkIdx = location.GetChunkIndex();
                size_t entityIdx = location.GetEntityIndex();

                if (chunkIdx >= m_chunks.size()) ASTRA_UNLIKELY
                    continue;

                // Validate entity index is within bounds
                if (entityIdx >= m_chunks[chunkIdx]->GetCount()) ASTRA_UNLIKELY
                {
                    // Entity index out of bounds - skip this removal
                    // This can happen if locations are stale or duplicated
                    continue;
                }

                    // Remove from chunk
                auto movedEntity = m_chunks[chunkIdx]->RemoveEntity(entityIdx);
                if (movedEntity) ASTRA_LIKELY
                {
                    EntityLocation newEntityLocation = EntityLocation::Create(chunkIdx, entityIdx);
                    movedEntities.emplace_back(*movedEntity, newEntityLocation);
                }

                --m_entityCount;
                lowestModifiedChunk = std::min(lowestModifiedChunk, chunkIdx);
            }

            // Update first non-full chunk index if needed
            if (lowestModifiedChunk < m_firstNonFullChunkIdx && lowestModifiedChunk < m_chunks.size()) ASTRA_UNLIKELY
            {
                if (!m_chunks[lowestModifiedChunk]->IsFull()) ASTRA_LIKELY
                {
                    m_firstNonFullChunkIdx = lowestModifiedChunk;
                }
            }

            // Remove empty chunks from the end (only if not deferred)
            if (!deferChunkCleanup) ASTRA_LIKELY
            {
                while (!m_chunks.empty() && m_chunks.back()->IsEmpty() && m_chunks.size() > 1) ASTRA_UNLIKELY
                {
                    m_chunks.pop_back();
                }
            }

            // Ensure first non-full index is valid
            if (m_firstNonFullChunkIdx >= m_chunks.size()) ASTRA_UNLIKELY
            {
                m_firstNonFullChunkIdx = m_chunks.size() > 0 ? m_chunks.size() - 1 : 0;
            }

            return movedEntities;
        }

        ASTRA_NODISCARD Entity GetEntity(EntityLocation location) const 
        { 
            size_t chunkIdx = location.GetChunkIndex();
            size_t entityIdx = location.GetEntityIndex();
            assert(chunkIdx < m_chunks.size());
            return m_chunks[chunkIdx]->GetEntity(entityIdx);
        }

        /**
        * Move entity data between archetypes
        * @param dstEntityLocation Packed destination location
        * @param srcArchetype Source archetype
        * @param srcEntityLocation Packed source location
        */
        void MoveEntityFrom(EntityLocation dstEntityLocation, Archetype& srcArchetype, EntityLocation srcEntityLocation)
        {
            // Get chunk positions
            size_t dstChunkIdx = dstEntityLocation.GetChunkIndex();
            size_t dstEntityIdx = dstEntityLocation.GetEntityIndex();
            size_t srcChunkIdx = srcEntityLocation.GetChunkIndex();
            size_t srcEntityIdx = srcEntityLocation.GetEntityIndex();

            assert(dstChunkIdx < m_chunks.size());
            assert(srcChunkIdx < srcArchetype.m_chunks.size());

            // Get source entity
            Entity srcEntity = srcArchetype.m_chunks[srcChunkIdx]->GetEntity(srcEntityIdx);

            // Add entity to destination chunk (it should already be added via AddEntity)
            // The entity handle is already stored in the chunk from AddEntity call

            // Move shared components using O(1) lookups
            auto& dstChunk = m_chunks[dstChunkIdx];
            auto& srcChunk = srcArchetype.m_chunks[srcChunkIdx];
            const auto& dstArrays = dstChunk->GetComponentArrays();
            const auto& srcArrays = srcChunk->GetComponentArrays();

            for (ComponentID id = 0; id < MAX_COMPONENTS; ++id)
            {
                const auto& dstInfo = dstArrays[id];
                if (!dstInfo.isValid)
                {
                    continue;
                }

                void* dstPtr = static_cast<std::byte*>(dstInfo.base) + dstEntityIdx * dstInfo.stride;

                const auto& srcInfo = srcArrays[id];
                if (srcInfo.isValid) ASTRA_LIKELY
                {
                    // Component exists in both archetypes - move it
                    void* srcPtr = static_cast<std::byte*>(srcInfo.base) + srcEntityIdx * srcInfo.stride;
                dstInfo.descriptor.MoveConstruct(dstPtr, srcPtr);
                }
                else ASTRA_UNLIKELY
                {
                    // Component doesn't exist in source, default construct
                    dstInfo.descriptor.DefaultConstruct(dstPtr);
                }
            }
        }

        template<Component... Components, typename Func>
        requires std::invocable<Func, Entity, Components&...>
        ASTRA_FORCEINLINE void ForEach(Func&& func)
        {
            // Early exit for empty archetype
            if (m_entityCount == 0 || m_chunks.empty()) ASTRA_UNLIKELY
                return;
            
            const size_t numChunks = m_chunks.size();
            
            // Process chunks with prefetching
            for (size_t i = 0; i < numChunks; ++i)
            {
                auto& chunk = m_chunks[i];
                const size_t count = chunk->GetCount();
                if (count == 0) ASTRA_UNLIKELY
                {
                    continue;
                }
                
                // Prefetch next chunk's data while processing current chunk
                if (i + 1 < numChunks) ASTRA_LIKELY
                {
                    auto& nextChunk = m_chunks[i + 1];
                    if (nextChunk->GetCount() > 0)
                    {
                        // Prefetch the entity array and first component array of next chunk
                        Simd::Ops::PrefetchT0(&nextChunk->GetEntities()[0]);
                        if constexpr (sizeof...(Components) > 0)
                        {
                            using FirstComponent = std::tuple_element_t<0, std::tuple<Components...>>;
                            Simd::Ops::PrefetchT0(nextChunk->GetComponentArray<FirstComponent>());
                        }
                    }
                }
                
                ForEachImpl<Components...>(chunk.get(), count, std::forward<Func>(func), std::index_sequence_for<Components...>{});
            }
        }
        
        /**
         * Process a specific chunk for parallel execution
         * Called by View::ParallelForEach to process individual chunks on different threads
         * No prefetching since chunks are processed independently
         */
        template<Component... Components, typename Func>
        requires std::invocable<Func, Entity, Components&...>
        ASTRA_FORCEINLINE void ParallelForEachChunk(size_t chunkIndex, Func&& func)
        {
            if (chunkIndex >= m_chunks.size()) ASTRA_UNLIKELY
                return;
                
            auto& chunk = m_chunks[chunkIndex];
            const size_t count = chunk->GetCount();
            if (count == 0) ASTRA_UNLIKELY
                return;
                
            // Process this single chunk - no prefetching needed in parallel mode
            ForEachImpl<Components...>(chunk.get(), count, std::forward<Func>(func), std::index_sequence_for<Components...>{});
        }
        
        /**
         * Iterate over a specific range of entities within a chunk
         * Used for spatial queries, parallel work distribution, etc.
         */
        template<Component... Components, typename Func>
        requires std::invocable<Func, Entity, Components&...>
        ASTRA_FORCEINLINE void ForEachRange(const EntityRange& range, Func&& func)
        {
            if (!range.IsValid() || range.chunkIndex >= m_chunks.size()) ASTRA_UNLIKELY
                return;
                
            auto& chunk = m_chunks[range.chunkIndex];
            const size_t chunkCount = chunk->GetCount();
            
            // Determine actual iteration bounds
            const size_t startIdx = range.startIndex;
            const size_t endIdx = (range.count == 0) ? chunkCount : std::min(startIdx + range.count, chunkCount);
            
            if (startIdx >= endIdx) ASTRA_UNLIKELY
                return;
                
            // Get component arrays
            auto arrays = std::tuple{chunk->GetComponentArray<Components>()...};
            const auto& entities = chunk->GetEntities();
            
            // Iterate over the specified range
            std::apply([&](auto*... componentArrays) {
                for (size_t i = startIdx; i < endIdx; ++i)
                {
                    func(entities[i], componentArrays[i]...);
                }
            }, arrays);
        }
        
        ASTRA_NODISCARD bool IsInitialized() const noexcept { return m_initialized; }
        
        ASTRA_NODISCARD size_t GetEntityCount() const noexcept { return m_entityCount; }
        ASTRA_NODISCARD size_t GetChunkCount() const noexcept { return m_chunks.size(); }
        ASTRA_NODISCARD size_t GetComponentCount() const noexcept { return m_componentCount; }
        ASTRA_NODISCARD size_t GetChunkEntityCount(size_t chunkIndex) const noexcept { return (chunkIndex < m_chunks.size()) ? m_chunks[chunkIndex]->GetCount() : 0; }
        ASTRA_NODISCARD size_t GetEntitiesPerChunk() const noexcept { return m_entitiesPerChunk; }
        
        ASTRA_NODISCARD const ComponentMask& GetMask() const noexcept { return m_mask; }
        
        template<Component C>
        ASTRA_NODISCARD bool HasComponent() const { return m_mask.Test(TypeID<C>::Value()); }
        ASTRA_NODISCARD bool HasComponent(ComponentID id) const { return m_mask.Test(id); }
        ASTRA_NODISCARD const std::vector<ComponentDescriptor>& GetComponents() const { return m_componentDescriptors; }

        /**
        * Ensure capacity for additional entities 
        */
        void EnsureCapacity(size_t additionalCount)
        {
            size_t required = m_entityCount + additionalCount;
            size_t currentCapacity = m_chunks.size() * m_entitiesPerChunk;

            if (required > currentCapacity) ASTRA_UNLIKELY
            {
                // Ceiling division: ceil(a/b) = floor((a + b - 1) / b)
                // For power of 2: ceil(a/b) = (a + b - 1) >> log2(b)
                size_t neededChunks = (required - currentCapacity + m_entitiesPerChunk - 1) >> m_entitiesPerChunkShift;
                m_chunks.reserve(m_chunks.size() + neededChunks);
            }
        }

        /**
        * Calculate remaining capacity in existing chunks
        */
        ASTRA_NODISCARD size_t CalculateRemainingCapacity() const
        {
            if (m_chunks.empty()) ASTRA_UNLIKELY return 0;

            size_t remaining = 0;
            for (size_t i = m_firstNonFullChunkIdx; i < m_chunks.size(); ++i)
            {
                remaining += m_entitiesPerChunk - m_chunks[i]->GetCount();
            }
            return remaining;
        }

        /**
         * Calculate fragmentation level of this archetype
         * @return Fragmentation ratio (0.0 = no fragmentation, 1.0 = maximum fragmentation)
         */
        ASTRA_NODISCARD float GetFragmentationLevel() const noexcept
        {
            if (m_chunks.empty() || m_entityCount == 0)
                return 0.0f;
            
            // Calculate optimal chunk count (if perfectly packed)
            size_t optimalChunkCount = (m_entityCount + m_entitiesPerChunk - 1) / m_entitiesPerChunk;
            
            // Fragmentation = (actual chunks - optimal chunks) / actual chunks
            return static_cast<float>(m_chunks.size() - optimalChunkCount) / static_cast<float>(m_chunks.size());
        }
        
        // Test/Debug accessors
        ASTRA_NODISCARD const std::vector<std::unique_ptr<ArchetypeChunk, ArchetypeChunkPool::ChunkDeleter>>& GetChunks() const { return m_chunks; }
        
        /**
         * Serialize archetype to binary format
         * Maintains SOA layout for efficient storage
         */
        void Serialize(BinaryWriter& writer) const
        {
            // Write archetype metadata - serialize the bitmap's words
            for (size_t i = 0; i < ComponentMask::WORD_COUNT; ++i)
            {
                writer(m_mask.Data()[i]);
            }
            writer(m_entityCount);
            writer(m_entitiesPerChunk);
            writer(static_cast<uint32_t>(m_chunks.size()));
            
            // Write component descriptors
            writer(static_cast<uint32_t>(m_componentDescriptors.size()));
            for (const auto& desc : m_componentDescriptors)
            {
                writer(desc.hash);  // Write stable hash instead of runtime ID
                writer(desc.size);
                writer(desc.alignment);
                writer(desc.version);
            }
            
            // Write each chunk's data
            for (const auto& chunk : m_chunks)
            {
                if (!chunk) continue;
                
                // Write chunk metadata
                size_t chunkEntityCount = chunk->GetCount();
                writer(static_cast<uint32_t>(chunkEntityCount));
                
                // Write entities array
                const auto& entities = chunk->GetEntities();
                for (size_t i = 0; i < chunkEntityCount; ++i)
                {
                    writer(entities[i]);
                }
                
                // Write component arrays (SOA layout)
                for (const auto& desc : m_componentDescriptors)
                {
                    void* componentArray = chunk->GetComponentArrayById(desc.id);
                    if (!componentArray) continue;
                    
                    size_t arraySize = chunkEntityCount * desc.size;
                    
                    // Use component's serialization function if available
                    if (desc.serializeVersioned || desc.serialize)
                    {
                        // For custom serialization, we can't compress the whole array
                        // as each component is serialized individually
                        if (desc.serializeVersioned)
                        {
                            for (size_t i = 0; i < chunkEntityCount; ++i)
                            {
                                void* componentPtr = static_cast<char*>(componentArray) + (i * desc.size);
                                desc.serializeVersioned(writer, componentPtr);
                            }
                        }
                        else
                        {
                            for (size_t i = 0; i < chunkEntityCount; ++i)
                            {
                                void* componentPtr = static_cast<char*>(componentArray) + (i * desc.size);
                                desc.serialize(writer, componentPtr);
                            }
                        }
                    }
                    else if (desc.is_trivially_copyable)
                    {
                        // For POD types, compress the entire array if beneficial
                        // WriteCompressedBlock will automatically handle compression threshold
                        writer.WriteCompressedBlock(componentArray, arraySize);
                    }
                    else
                    {
                        // Should not happen - components should be serializable
                        ASTRA_ASSERT(false, "Component type is not serializable");
                    }
                }
            }
        }
        
        /**
         * Deserialize archetype from binary format
         * Factory method that creates a new archetype from serialized data
         */
        static std::unique_ptr<Archetype> Deserialize(BinaryReader& reader, const std::vector<ComponentDescriptor>& registryDescriptors, ArchetypeChunkPool* componentPool = nullptr)
        {
            // Read archetype metadata - deserialize the bitmap's words
            ComponentMask mask;
            for (size_t i = 0; i < ComponentMask::WORD_COUNT; ++i)
            {
                reader(mask.Data()[i]);
            }
            
            size_t entityCount;
            size_t entitiesPerChunk;
            uint32_t chunkCount;
            reader(entityCount);
            reader(entitiesPerChunk);
            reader(chunkCount);
            
            // Read component descriptors
            uint32_t descriptorCount;
            reader(descriptorCount);
            std::vector<ComponentDescriptor> descriptors;
            descriptors.reserve(descriptorCount);
            
            for (uint32_t i = 0; i < descriptorCount; ++i)
            {
                uint64_t hash;
                size_t size, alignment;
                uint32_t version;
                reader(hash)(size)(alignment)(version);
                
                // Find matching descriptor from registry by hash
                auto it = std::find_if(registryDescriptors.begin(), registryDescriptors.end(),
                    [hash](const auto& desc) { return desc.hash == hash; });
                
                if (it != registryDescriptors.end())
                {
                    descriptors.push_back(*it);
                }
                else
                {
                    // Component not registered - cannot deserialize
                    return nullptr;
                }
            }
            
            // Create new archetype
            auto archetype = std::make_unique<Archetype>(mask);
            archetype->m_chunkPool = componentPool;
            archetype->Initialize(descriptors);
            
            if (!archetype->IsInitialized())
            {
                return nullptr;
            }
            
            // Clear the pre-allocated chunk
            archetype->m_chunks.clear();
            archetype->m_entityCount = 0;
            
            // Read each chunk's data
            for (uint32_t chunkIdx = 0; chunkIdx < chunkCount; ++chunkIdx)
            {
                uint32_t chunkEntityCount;
                reader(chunkEntityCount);
                
                // Create new chunk
                auto chunk = componentPool ? componentPool->CreateChunk(entitiesPerChunk, descriptors) : nullptr;
                if (!chunk)
                {
                    // Out of memory - cannot continue
                    return nullptr;
                }
                
                // Read entities array
                for (uint32_t i = 0; i < chunkEntityCount; ++i)
                {
                    Entity entity;
                    reader(entity);
                    chunk->AddEntity(entity);
                }
                
                // Read component arrays (SOA layout)
                for (const auto& desc : descriptors)
                {
                    void* componentArray = chunk->GetComponentArrayById(desc.id);
                    if (!componentArray) continue;
                    
                    size_t arraySize = chunkEntityCount * desc.size;
                    
                    if (desc.deserializeVersioned || desc.deserialize)
                    {
                        // For custom deserialization, components are not compressed
                        // as they were serialized individually
                        if (desc.deserializeVersioned)
                        {
                            for (uint32_t i = 0; i < chunkEntityCount; ++i)
                            {
                                void* componentPtr = static_cast<char*>(componentArray) + (i * desc.size);
                                desc.deserializeVersioned(reader, componentPtr);
                            }
                        }
                        else
                        {
                            for (uint32_t i = 0; i < chunkEntityCount; ++i)
                            {
                                void* componentPtr = static_cast<char*>(componentArray) + (i * desc.size);
                                desc.deserialize(reader, componentPtr);
                            }
                        }
                    }
                    else if (desc.is_trivially_copyable)
                    {
                        // POD types may be compressed - use ReadCompressedBlock
                        auto result = reader.ReadCompressedBlock();
                        if (result.IsErr())
                        {
                            // Error reading compressed block
                            return nullptr;
                        }
                        
                        auto& data = *result.GetValue();
                        if (data.size() != arraySize)
                        {
                            // Size mismatch - data corruption
                            return nullptr;
                        }
                        
                        // Copy decompressed data to component array
                        std::memcpy(componentArray, data.data(), arraySize);
                    }
                }
                
                archetype->m_chunks.push_back(std::move(chunk));
            }
            
            archetype->m_entityCount = entityCount;
            
            return archetype;
        }
        
        /**
         * Check if coalescing would be beneficial
         */
        ASTRA_NODISCARD bool NeedsCoalescing() const
        {
            if (m_chunks.size() <= 1) ASTRA_LIKELY return false;
            
            // Simple heuristic: coalesce if we have sparse chunks
            for (size_t i = 1; i < m_chunks.size(); ++i)
            {
                float utilization = static_cast<float>(m_chunks[i]->GetCount()) / m_entitiesPerChunk;
                if (utilization < COALESCE_UTILIZATION_THRESHOLD)
                {
                    return true;
                }
            }
            return false;
        }

        /**
        * Coalesce sparse chunks to improve memory utilization
        * Returns: pair of (chunks_freed, moved_entities_list)
        */
        std::pair<size_t, std::vector<std::pair<Entity, EntityLocation>>> CoalesceChunks()
        {
            std::vector<std::pair<Entity, EntityLocation>> allMovedEntities;
            if (m_chunks.size() <= 1) ASTRA_LIKELY return {0, allMovedEntities};

            // Single pass: find and sort sparse chunks
            std::vector<std::pair<size_t, float>> sparseChunks;
            for (size_t i = 1; i < m_chunks.size(); ++i)  // Skip first chunk
            {
                float utilization = static_cast<float>(m_chunks[i]->GetCount()) / m_entitiesPerChunk;
                if (utilization < COALESCE_UTILIZATION_THRESHOLD)
                {
                    sparseChunks.emplace_back(i, utilization);
                }
            }

            if (sparseChunks.empty()) ASTRA_LIKELY return {0, allMovedEntities};

            // Sort by utilization (least utilized first)
            std::sort(sparseChunks.begin(), sparseChunks.end(),
                [](const auto& a, const auto& b) { return a.second < b.second; });

            // Try to pack entities from sparse chunks into denser ones
            for (const auto& [sparseIdx, _] : sparseChunks)
            {
                auto& sparseChunk = m_chunks[sparseIdx];
                size_t entitiesToMove = sparseChunk->GetCount();

                if (entitiesToMove == 0) continue;

                // Find destination chunks with available space
                for (size_t destIdx = 0; destIdx < m_chunks.size(); ++destIdx)
                {
                    if (destIdx == sparseIdx) continue;

                    auto& destChunk = m_chunks[destIdx];
                    size_t available = m_entitiesPerChunk - destChunk->GetCount();

                    if (available > 0)
                    {
                        size_t toMove = std::min(available, entitiesToMove);

                        // Move entities from sparse to destination chunk
                        auto movedEntities = MoveEntitiesBetweenChunks(sparseIdx, destIdx, toMove);
                        allMovedEntities.insert(allMovedEntities.end(), movedEntities.begin(), movedEntities.end());

                        entitiesToMove -= toMove;
                        if (entitiesToMove == 0) break;
                    }
                }
            }

            // Remove empty chunks (simple backwards iteration)
            size_t chunksFreed = 0;
            for (size_t i = m_chunks.size() - 1; i > 0; --i) // Skip first chunk
            {
                if (m_chunks[i]->IsEmpty())
                {
                    m_chunks.erase(m_chunks.begin() + i);
                    ++chunksFreed;
                }
            }

            return {chunksFreed, allMovedEntities};
        }
        
        void SetComponentPool(ArchetypeChunkPool* pool) { m_chunkPool = pool; }

    private:
        static constexpr size_t INVALID_CHUNK_INDEX = std::numeric_limits<size_t>::max();

        /**
        * Batch add entities without constructing components
        * Used when components will be move-constructed from another source
        * @return Locations where entities were added
        */
        std::vector<EntityLocation> AddEntitiesNoConstruct(std::span<const Entity> entities)
        {
            size_t count = entities.size();
            if (count == 0) ASTRA_UNLIKELY
                return {};

            std::vector<EntityLocation> locations;
            locations.reserve(count);

            // Calculate and allocate needed chunks upfront
            size_t remainingCapacity = CalculateRemainingCapacity();
            if (count > remainingCapacity) ASTRA_UNLIKELY
            {
                size_t additionalNeeded = count - remainingCapacity;
            size_t newChunksNeeded = (additionalNeeded + m_entitiesPerChunk - 1) >> m_entitiesPerChunkShift;

            // Create chunks directly from pool
            for (size_t i = 0; i < newChunksNeeded; ++i)
            {
                auto chunk = m_chunkPool->CreateChunk(m_entitiesPerChunk, m_componentDescriptors);
                if (!chunk) ASTRA_UNLIKELY
                {
                    return locations;
                }
                m_chunks.emplace_back(std::move(chunk));
            }
            }

                // Now batch fill chunks WITHOUT constructing components
            size_t entityIdx = 0;
            size_t chunkIdx = m_firstNonFullChunkIdx;

            while (entityIdx < count && chunkIdx < m_chunks.size()) ASTRA_LIKELY
            {
                auto& chunk = m_chunks[chunkIdx];
                size_t available = m_entitiesPerChunk - chunk->GetCount();

                if (available > 0) ASTRA_LIKELY
                {
                    size_t toAdd = std::min(available, count - entityIdx);
                size_t startIdx = chunk->GetCount();

                // Add entities without constructing components
                for (size_t i = 0; i < toAdd; ++i)
                {
                    chunk->GetEntities().push_back(entities[entityIdx + i]);
                    locations.push_back(EntityLocation::Create(chunkIdx, startIdx + i));
                }
                chunk->SetCount(chunk->GetCount() + toAdd);

                entityIdx += toAdd;

                // Update first non-full chunk if this one is now full
                if (chunk->IsFull() && chunkIdx == m_firstNonFullChunkIdx) ASTRA_UNLIKELY
                {
                    m_firstNonFullChunkIdx = chunkIdx + 1;
                }
            }

            ++chunkIdx;
            }

            m_entityCount += entityIdx;
            return locations;
        }

        /**
        * Batch move entities from another archetype
        * Pre-allocates chunks and moves components in batch
        * @return Locations of moved entities in this archetype
        */
        std::vector<EntityLocation> BatchMoveEntitiesFrom(std::span<const Entity> entities, Archetype& srcArchetype, std::span<const EntityLocation> srcLocations)
        {
            assert(entities.size() == srcLocations.size());
            size_t count = entities.size();
            if (count == 0) return {};

            // Use batch allocation without construction for maximum performance
            std::vector<EntityLocation> dstLocations = AddEntitiesNoConstruct(entities);

            // Check if allocation was successful for all entities
            if (dstLocations.size() != entities.size())
            {
                // Partial allocation - only process what we got
                // This shouldn't happen in normal operation but handle it gracefully
                return dstLocations;
            }

            // Group by chunks for efficient batch processing
            struct ChunkBatch {
                size_t srcChunkIdx;
                size_t dstChunkIdx;
                SmallVector<size_t, 32> srcIndices;  // Use SmallVector to avoid allocations
                SmallVector<size_t, 32> dstIndices;
            };

            FlatMap<uint64_t, ChunkBatch> batches;
            batches.Reserve(16);  // Pre-allocate for typical case

            for (size_t i = 0; i < dstLocations.size(); ++i)
            {
                // Check if source location is valid
                if (!srcLocations[i].IsValid())
                {
                    // Skip invalid source locations
                    continue;
                }

                size_t srcChunkIdx = srcLocations[i].GetChunkIndex();
                size_t srcEntityIdx = srcLocations[i].GetEntityIndex();
                size_t dstChunkIdx = dstLocations[i].GetChunkIndex();
                size_t dstEntityIdx = dstLocations[i].GetEntityIndex();

                // Validate source and destination locations
                assert(srcChunkIdx < srcArchetype.m_chunks.size() && 
                    "Source chunk index out of bounds");
                assert(dstChunkIdx < m_chunks.size() && 
                    "Destination chunk index out of bounds");

                // Create unique key for src-dst chunk pair
                uint64_t key = (uint64_t(srcChunkIdx) << 32) | dstChunkIdx;

                auto& batch = batches[key];
                batch.srcChunkIdx = srcChunkIdx;
                batch.dstChunkIdx = dstChunkIdx;
                batch.srcIndices.push_back(srcEntityIdx);
                batch.dstIndices.push_back(dstEntityIdx);
            }

            // Calculate components to move (only shared components)
            // This is the intersection of source and destination masks
            ComponentMask componentsToMove = srcArchetype.GetMask() & GetMask();

            // If there are no components to move (e.g., moving from root archetype),
            // we're done - entities are already allocated
            if (componentsToMove.None())
            {
                return dstLocations;
            }

            // Batch move components for each chunk pair
            for (auto& [key, batch] : batches)
            {
                // Validate chunk indices
                if (batch.srcChunkIdx >= srcArchetype.m_chunks.size() ||
                    batch.dstChunkIdx >= m_chunks.size())
                {
                    assert(false && "Invalid chunk index in batch move");
                    continue;
                }

                auto& srcChunk = srcArchetype.m_chunks[batch.srcChunkIdx];
                auto& dstChunk = m_chunks[batch.dstChunkIdx];

                dstChunk->BatchMoveComponentsFrom(
                    batch.dstIndices,
                    *srcChunk,
                    batch.srcIndices,
                    componentsToMove
                );
            }

            return dstLocations;
        }

        ASTRA_NODISCARD std::pair<ArchetypeChunk*, size_t> GetChunkAndIndex(EntityLocation location)
        {
            size_t chunkIdx = location.GetChunkIndex();
            size_t entityIdx = location.GetEntityIndex();
            assert(chunkIdx < m_chunks.size());
            return {m_chunks[chunkIdx].get(), entityIdx};
        }

        // Helper to expand component arrays with index_sequence
        template<typename... Components, typename Func, size_t... Is>
        ASTRA_FORCEINLINE void ForEachImpl(ArchetypeChunk* chunk, size_t count, Func&& func, std::index_sequence<Is...>)
        {
            // Get all component arrays at once
            auto arrays = std::tuple{chunk->GetComponentArray<Components>()...};
            const auto& entities = chunk->GetEntities();

            // Single tight loop - compiler can optimize this as well as manual unrolling
            for (size_t i = 0; i < count; ++i)
            {
                func(entities[i], std::get<Is>(arrays)[i]...);
            }
        }
        
        /**
         * Find or create a chunk with available space
         * @return Pair of (chunk index, whether a new chunk was created)
         */
        std::pair<size_t, bool> FindOrCreateChunkWithSpace()
        {
            // Use first non-full chunk for O(1) lookup
            size_t chunkIdx = m_firstNonFullChunkIdx;
            
            // Validate that our cached index is still valid
            if (chunkIdx < m_chunks.size() && !m_chunks[chunkIdx]->IsFull()) ASTRA_LIKELY
            {
                return {chunkIdx, false};  // Use the cached chunk
            }
            
            // Find a chunk with space starting from the cached index
            for (chunkIdx = m_firstNonFullChunkIdx; chunkIdx < m_chunks.size(); ++chunkIdx)
            {
                if (!m_chunks[chunkIdx]->IsFull()) ASTRA_LIKELY
                {
                    m_firstNonFullChunkIdx = chunkIdx;  // Update cache
                    return {chunkIdx, false};
                }
            }
            
            // Need new chunk
            auto chunk = m_chunkPool->CreateChunk(m_entitiesPerChunk, m_componentDescriptors);
            if (!chunk) ASTRA_UNLIKELY
            {
                return {INVALID_CHUNK_INDEX, false};  // Can't allocate new chunk
            }
            
            m_chunks.emplace_back(std::move(chunk));
            chunkIdx = m_chunks.size() - 1;  // Use the newly created chunk
            m_firstNonFullChunkIdx = chunkIdx;  // Update cache to new chunk
            
            return {chunkIdx, true};
        }

        EntityLocation AddEntityNoConstruct(Entity entity)
        {
            // Find or create a chunk with space
            auto [chunkIdx, wasCreated] = FindOrCreateChunkWithSpace();
            if (chunkIdx == INVALID_CHUNK_INDEX) ASTRA_UNLIKELY
            {
                return EntityLocation();  // Allocation failed
            }

            // Directly add entity without component construction
            auto* chunk = m_chunks[chunkIdx].get();
            
            // Inline AddEntityNoConstruct for better performance
            assert(chunk->GetCount() < chunk->GetCapacity());
            chunk->GetEntities().resize(chunk->GetCount() + 1);  // Ensure vector has space
            size_t entityIdx = chunk->GetCount();
            chunk->SetCount(chunk->GetCount() + 1);
            
            // Store the entity handle directly in the chunk's entity array
            chunk->GetEntities()[entityIdx] = entity;
            
            ++m_entityCount;
            
            // If this chunk is now full, update first non-full index
            if (chunk->IsFull()) ASTRA_UNLIKELY
            {
                m_firstNonFullChunkIdx = chunkIdx + 1;
            }

            return EntityLocation::Create(chunkIdx, entityIdx);
        }
        
        std::vector<std::pair<Entity, EntityLocation>> MoveEntitiesBetweenChunks(size_t srcChunkIdx, size_t destChunkIdx, size_t count)
        {
            std::vector<std::pair<Entity, EntityLocation>> movedEntities;
            movedEntities.reserve(count);
            
            auto& srcChunk = m_chunks[srcChunkIdx];
            auto& destChunk = m_chunks[destChunkIdx];
            
            // Get the last 'count' entities from source chunk
            size_t srcCount = srcChunk->GetCount();
            size_t destCount = destChunk->GetCount();
            
            for (size_t i = 0; i < count; ++i)
            {
                size_t srcEntityIdx = srcCount - i - 1;
                size_t destEntityIdx = destCount + i;
                
                // Get entity from source chunk
                Entity entity = srcChunk->GetEntity(srcEntityIdx);
                
                // Add entity to destination chunk's entity vector
                destChunk->GetEntities().push_back(entity);
                
                EntityLocation destEntityLocation = EntityLocation::Create(destChunkIdx, destEntityIdx);
                movedEntities.emplace_back(entity, destEntityLocation);
                
                // Move components using O(1) lookups
                const auto& srcArrays = srcChunk->GetComponentArrays();
                const auto& destArrays = destChunk->GetComponentArrays();
                
                for (ComponentID id = 0; id < MAX_COMPONENTS; ++id)
                {
                    const auto& srcInfo = srcArrays[id];
                    if (!srcInfo.isValid) continue;
                    
                    void* srcPtr = static_cast<std::byte*>(srcInfo.base) + srcEntityIdx * srcInfo.stride;
                    void* destPtr = static_cast<std::byte*>(destArrays[id].base) + destEntityIdx * srcInfo.stride;
                    
                    // Use move constructor to transfer component data
                    srcInfo.descriptor.MoveConstruct(destPtr, srcPtr);
                    srcInfo.descriptor.Destruct(srcPtr);
                }
                
                // Remove entity from source chunk's entity vector
                srcChunk->GetEntities().pop_back();
            }
            
            // Update chunk counts
            srcChunk->SetCount(srcCount - count);
            destChunk->SetCount(destCount + count);
            
            return movedEntities;
        }

        ASTRA_NODISCARD size_t GetEntitiesPerChunkShift() const noexcept { return m_entitiesPerChunkShift; }
        ASTRA_NODISCARD size_t GetEntitiesPerChunkMask() const noexcept { return m_entitiesPerChunkMask; }

        ComponentMask m_mask;
        size_t m_componentCount;  // Cached component count for fast access
        std::vector<ComponentDescriptor> m_componentDescriptors;
        std::vector<std::unique_ptr<ArchetypeChunk, ArchetypeChunkPool::ChunkDeleter>> m_chunks;
        size_t m_entityCount;
        size_t m_entitiesPerChunk;
        size_t m_entitiesPerChunkShift;     // For fast division via bit shift (log2(m_entitiesPerChunk))
        size_t m_entitiesPerChunkMask;      // For fast modulo operations (m_entitiesPerChunk - 1)
        size_t m_firstNonFullChunkIdx = 0;  // Track first chunk with available space for O(1) lookup
        bool m_initialized;
        
        static constexpr float COALESCE_UTILIZATION_THRESHOLD = 0.5f;
        static constexpr float SPARSE_CHUNK_RATIO_THRESHOLD = 0.25f;

        ArchetypeChunkPool* m_chunkPool = nullptr;
        friend class ArchetypeManager;
        friend class Registry;
        template<typename...> friend class View;
    };
}