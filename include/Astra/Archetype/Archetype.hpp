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
#include "../Component/ComponentOps.hpp"
#include "../Container/Bitmap.hpp"
#include "../Container/SmallVector.hpp"
#include "../Core/Base.hpp"
#include "../Core/Result.hpp"
#include "../Core/TypeID.hpp"
#include "../Entity/Entity.hpp"
#include "../Memory/ChunkPool.hpp"
#include "../Memory/Memory.hpp"
#include "../Platform/Hardware.hpp"
#include "../Platform/Platform.hpp"
#include "../Platform/Simd.hpp"
#include "../Serialization/BinaryReader.hpp"
#include "../Serialization/BinaryWriter.hpp"
#include "ArchetypeEdgeStorage.hpp"

namespace Astra
{
    class ArchetypeStorage;

    using ComponentMask = Bitmap<MAX_COMPONENTS>;
    
    /**
     * PackedLocation - Simple struct for chunk/entity indices
     * 
     * Direct storage of chunk and entity indices for O(1) lookups.
     * Modern CPUs handle two 32-bit loads faster than bit operations.
     */
    struct PackedLocation
    {
        uint32_t chunkIndex;
        uint32_t entityIndex;
        
        constexpr PackedLocation() noexcept :
            chunkIndex(std::numeric_limits<uint32_t>::max()),
            entityIndex(std::numeric_limits<uint32_t>::max())
        {}
        
        constexpr PackedLocation(uint32_t chunk, uint32_t entity) noexcept : chunkIndex(chunk), entityIndex(entity) {}
        
        ASTRA_NODISCARD constexpr static PackedLocation Pack(size_t chunkIdx, size_t entityIdx, size_t) noexcept
        {
            return PackedLocation(static_cast<uint32_t>(chunkIdx), static_cast<uint32_t>(entityIdx));
        }
        
        ASTRA_NODISCARD constexpr size_t GetChunkIndex(size_t) const noexcept
        {
            return chunkIndex;
        }
        
        ASTRA_NODISCARD constexpr size_t GetEntityIndex(size_t) const noexcept
        {
            return entityIndex;
        }
        
        ASTRA_NODISCARD constexpr bool IsValid() const noexcept
        {
            return chunkIndex != std::numeric_limits<uint32_t>::max();
        }
        
        ASTRA_NODISCARD constexpr size_t Raw() const noexcept 
        { 
            return (static_cast<size_t>(chunkIndex) << 32) | entityIndex; 
        }
        
        constexpr bool operator==(const PackedLocation& other) const noexcept 
        { 
            return chunkIndex == other.chunkIndex && entityIndex == other.entityIndex; 
        }
        constexpr bool operator!=(const PackedLocation& other) const noexcept 
        { 
            return !(*this == other); 
        }
        constexpr bool operator<(const PackedLocation& other) const noexcept 
        { 
            return chunkIndex < other.chunkIndex || (chunkIndex == other.chunkIndex && entityIndex < other.entityIndex); 
        }
        constexpr bool operator>(const PackedLocation& other) const noexcept 
        { 
            return other < *this; 
        }
        constexpr bool operator<=(const PackedLocation& other) const noexcept 
        { 
            return !(other < *this); 
        }
        constexpr bool operator>=(const PackedLocation& other) const noexcept 
        { 
            return !(*this < other); 
        }
    };
    
    template<Component... Components>
    ASTRA_NODISCARD constexpr ComponentMask MakeComponentMask() noexcept
    {
        ComponentMask mask{};
        ((mask.Set(TypeID<Components>::Value())), ...);
        return mask;
    }
    
    class Archetype
    {
        class Chunk;
        struct ChunkDeleter;
    
    public:
        // Chunk utilization metrics for coalescing
        struct ChunkMetrics
        {
            float utilization = 0.0f;      // Current fill percentage
            size_t frameLastAccessed = 0;  // For hot/cold detection
            bool isHot = false;            // Frequently accessed chunk
        };
        
        explicit Archetype(ComponentMask mask) :
            m_mask(mask),
            m_componentCount(mask.Count()), // Cache component count
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
            size_t chunkSize = m_chunkPool ? m_chunkPool->GetChunkSize() : ChunkPool::DEFAULT_CHUNK_SIZE;
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
            auto chunk = Chunk::Create(m_entitiesPerChunk, m_componentDescriptors, AcquireChunkMemory(), m_chunkPool);
            if (!chunk) ASTRA_UNLIKELY
            {
                // Mark as failed initialization - caller must check IsInitialized()
                m_initialized = false;
                return;
            }
            m_chunks.emplace_back(std::move(chunk));
            m_chunkMetrics.resize(1);  // Initialize metrics for first chunk
        }
        
        /**
         * Add entity to archetype
         * @return Packed location or invalid location on failure
         */
        PackedLocation AddEntity(Entity entity)
        {
            // Find or create a chunk with space
            auto [chunkIdx, wasCreated] = FindOrCreateChunkWithSpace();
            if (chunkIdx == INVALID_CHUNK_INDEX) ASTRA_UNLIKELY
            {
                return PackedLocation();  // Allocation failed
            }

            size_t entityIdx = m_chunks[chunkIdx]->AddEntity(entity);
            ++m_entityCount;
            
            // If this chunk is now full, update first non-full index
            if (m_chunks[chunkIdx]->IsFull()) ASTRA_UNLIKELY
            {
                // Will be updated on next AddEntity call
                m_firstNonFullChunkIdx = chunkIdx + 1;
            }

            // Return packed location
            return PackedLocation::Pack(chunkIdx, entityIdx, m_entitiesPerChunkShift);
        }

        /**
         * Optimized batch add entities with pre-allocation
         * Returns vector of packed locations for added entities
         */
        std::vector<PackedLocation> AddEntities(std::span<const Entity> entities)
        {
            size_t count = entities.size();
            if (count == 0) ASTRA_UNLIKELY
                return {};
            
            std::vector<PackedLocation> packedLocations;
            packedLocations.reserve(count);
            
            // Calculate and allocate needed chunks upfront
            size_t remainingCapacity = CalculateRemainingCapacity();
            if (count > remainingCapacity) ASTRA_UNLIKELY
            {
                size_t additionalNeeded = count - remainingCapacity;
                size_t newChunksNeeded = (additionalNeeded + m_entitiesPerChunk - 1) >> m_entitiesPerChunkShift;
                
                // Try batch allocation from pool if available
                std::vector<void*> pooledMemory;
                size_t chunksAcquired = 0;
                
                if (m_chunkPool) ASTRA_LIKELY
                {
                    chunksAcquired = m_chunkPool->AcquireBatch(newChunksNeeded, pooledMemory);
                }
                
                // Create chunks with pooled memory
                for (size_t i = 0; i < chunksAcquired; ++i)
                {
                    auto chunk = Chunk::Create(m_entitiesPerChunk, m_componentDescriptors, pooledMemory[i], m_chunkPool);
                    if (!chunk) ASTRA_UNLIKELY
                    {
                        // This shouldn't happen with valid pooled memory
                        // Return memory to pool and stop
                        for (size_t j = i; j < chunksAcquired; ++j)
                        {
                            m_chunkPool->Release(pooledMemory[j]);
                        }
                        return packedLocations;
                    }
                    m_chunks.emplace_back(std::move(chunk));
                    m_chunkMetrics.emplace_back();
                }
                
                // Allocate remaining chunks without pool
                for (size_t i = chunksAcquired; i < newChunksNeeded; ++i)
                {
                    auto chunk = Chunk::Create(m_entitiesPerChunk, m_componentDescriptors, nullptr, m_chunkPool);
                    if (!chunk) ASTRA_UNLIKELY
                    {
                        // Allocation failed - return what we've added so far
                        return packedLocations;
                    }
                    m_chunks.emplace_back(std::move(chunk));
                    m_chunkMetrics.emplace_back();
                }
            }
            
            // Now batch fill chunks efficiently
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
                    
                    // Record packed locations
                    for (size_t i = 0; i < toAdd; ++i)
                    {
                        packedLocations.push_back(PackedLocation::Pack(chunkIdx, startIdx + i, m_entitiesPerChunkShift));
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
            return packedLocations;
        }
        
        /**
         * Remove entity from archetype
         * @param packedLocation The packed location (chunkIdx << shift | entityIdx)
         * @return The entity that was moved to fill the gap (if any)
         */
        std::optional<Entity> RemoveEntity(PackedLocation packedLocation)
        {
            size_t chunkIdx = packedLocation.GetChunkIndex(m_entitiesPerChunkShift);
            size_t entityIdx = packedLocation.GetEntityIndex(m_entitiesPerChunkMask);
            
            assert(chunkIdx < m_chunks.size());
            
            // Remove from chunk - chunk handles the swap-and-pop
            auto movedEntity = m_chunks[chunkIdx]->RemoveEntity(entityIdx);
            
            --m_entityCount;
            
            // Update first non-full chunk index if this chunk now has space
            if (chunkIdx < m_firstNonFullChunkIdx && !m_chunks[chunkIdx]->IsFull()) ASTRA_UNLIKELY
            {
                m_firstNonFullChunkIdx = chunkIdx;
            }
            
            // Consider removing empty chunks only if it's the last chunk
            // This avoids invalidating packed locations for entities in later chunks
            if (chunkIdx == m_chunks.size() - 1 && chunkIdx > 0 && m_chunks[chunkIdx]->IsEmpty()) ASTRA_UNLIKELY
            {
                m_chunks.pop_back();
                m_chunkMetrics.pop_back();
                
                // If we removed the last chunk, we might need to update first non-full index
                if (m_firstNonFullChunkIdx >= m_chunks.size()) ASTRA_UNLIKELY
                {
                    m_firstNonFullChunkIdx = m_chunks.size() > 0 ? m_chunks.size() - 1 : 0;
                }
            }
            
            return movedEntity;
        }

        /**
         * Remove multiple entities from archetype in batch
         * @param packedLocations Packed locations of entities to remove
         * @return Vector of entities that were moved to fill gaps with their new packed locations
         */
        std::vector<std::pair<Entity, PackedLocation>> RemoveEntities(std::span<const PackedLocation> packedLocations)
        {
            if (packedLocations.empty()) ASTRA_UNLIKELY
                return {};
                
            std::vector<std::pair<Entity, PackedLocation>> movedEntities;
            movedEntities.reserve(packedLocations.size());
            
            // Sort packed locations by chunk for better cache locality
            // Process in reverse order within each chunk to avoid invalidating indices
            std::vector<PackedLocation> sortedLocations(packedLocations.begin(), packedLocations.end());
            std::sort(sortedLocations.begin(), sortedLocations.end(), std::greater<PackedLocation>());
            
            size_t lowestModifiedChunk = std::numeric_limits<size_t>::max();
            
            // Process removals
            for (PackedLocation packedLocation : sortedLocations)
            {
                size_t chunkIdx = packedLocation.GetChunkIndex(m_entitiesPerChunkShift);
                size_t entityIdx = packedLocation.GetEntityIndex(m_entitiesPerChunkMask);
                
                if (chunkIdx >= m_chunks.size()) ASTRA_UNLIKELY
                    continue;
                    
                // Remove from chunk
                auto movedEntity = m_chunks[chunkIdx]->RemoveEntity(entityIdx);
                if (movedEntity) ASTRA_LIKELY
                {
                    // Calculate new packed location for the moved entity
                    PackedLocation newPackedLocation = PackedLocation::Pack(chunkIdx, entityIdx, m_entitiesPerChunkShift);
                    movedEntities.emplace_back(*movedEntity, newPackedLocation);
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
            
            // Remove empty chunks from the end
            while (!m_chunks.empty() && m_chunks.back()->IsEmpty() && m_chunks.size() > 1) ASTRA_UNLIKELY
            {
                m_chunks.pop_back();
                m_chunkMetrics.pop_back();
            }
            
            // Ensure first non-full index is valid
            if (m_firstNonFullChunkIdx >= m_chunks.size()) ASTRA_UNLIKELY
            {
                m_firstNonFullChunkIdx = m_chunks.size() > 0 ? m_chunks.size() - 1 : 0;
            }
            
            return movedEntities;
        }
        
        template<Component T>
        ASTRA_NODISCARD T* GetComponent(PackedLocation packedLocation)
        {
            ComponentID id = TypeID<T>::Value();
            if (!m_mask.Test(id)) ASTRA_UNLIKELY
                return nullptr;
                
            size_t chunkIdx = packedLocation.GetChunkIndex(m_entitiesPerChunkShift);
            size_t entityIdx = packedLocation.GetEntityIndex(m_entitiesPerChunkMask);
            
            assert(chunkIdx < m_chunks.size());
            assert(entityIdx < m_chunks[chunkIdx]->GetCount());
            
            return m_chunks[chunkIdx]->GetComponent<T>(entityIdx);
        }
        
        /**
         * Set component data
         */
        template<typename T>
        void SetComponent(PackedLocation packedLocation, T&& value)
        {
            using DecayedType = std::decay_t<T>;
            static_assert(Component<DecayedType>, "T must be a Component");
            
            DecayedType* ptr = GetComponent<DecayedType>(packedLocation);
            assert(ptr != nullptr);
            *ptr = std::forward<T>(value);
        }
        
        /**
        * Ensure capacity for additional entities in batch operations
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
                m_chunkMetrics.reserve(m_chunks.capacity());
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
        
        template<Component... Components, std::invocable<Entity, Components&...> Func>
        void ForEach(Func&& func)
        {
            // Early exit for empty archetype
            if (m_entityCount == 0 || m_chunks.empty()) ASTRA_UNLIKELY
                return;
            
            // Single optimized path using index_sequence
            for (auto& chunk : m_chunks)
            {
                const size_t count = chunk->GetCount();
                if (count == 0) ASTRA_UNLIKELY
                    continue;
                
                ForEachImpl<Components...>(chunk.get(), count, std::forward<Func>(func), 
                                          std::index_sequence_for<Components...>{});
            }
        }
        
    private:
        // Helper to expand component arrays with index_sequence
        template<typename... Components, typename Func, size_t... Is>
        ASTRA_FORCEINLINE void ForEachImpl(Chunk* chunk, size_t count, Func&& func, 
                                           std::index_sequence<Is...>)
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
        
    public:
        
        
        /**
         * Move entity data between archetypes
         * @param dstPackedLocation Packed destination location
         * @param srcArchetype Source archetype
         * @param srcPackedLocation Packed source location
         */
        void MoveEntityFrom(PackedLocation dstPackedLocation, Archetype& srcArchetype, PackedLocation srcPackedLocation)
        {
            // Get chunk positions
            size_t dstChunkIdx = dstPackedLocation.GetChunkIndex(m_entitiesPerChunkShift);
            size_t dstEntityIdx = dstPackedLocation.GetEntityIndex(m_entitiesPerChunkMask);
            size_t srcChunkIdx = srcPackedLocation.GetChunkIndex(srcArchetype.m_entitiesPerChunkShift);
            size_t srcEntityIdx = srcPackedLocation.GetEntityIndex(srcArchetype.m_entitiesPerChunkMask);
            
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
                if (!dstInfo.isValid) continue;
                
                void* dstPtr = static_cast<std::byte*>(dstInfo.base) + dstEntityIdx * dstInfo.stride;
                
                const auto& srcInfo = srcArrays[id];
                if (srcInfo.isValid) ASTRA_LIKELY
                {
                    // Component exists in both archetypes - move it
                    void* srcPtr = static_cast<std::byte*>(srcInfo.base) + srcEntityIdx * srcInfo.stride;
                    ComponentOps::MoveConstruct(dstInfo.descriptor, dstPtr, srcPtr);
                }
                else ASTRA_UNLIKELY
                {
                    // Component doesn't exist in source, default construct
                    ComponentOps::DefaultConstruct(dstInfo.descriptor, dstPtr);
                }
            }
        }
        
        // Getters
        ASTRA_NODISCARD size_t GetEntityCount() const noexcept { return m_entityCount; }
        ASTRA_NODISCARD const ComponentMask& GetMask() const noexcept { return m_mask; }
        ASTRA_NODISCARD size_t GetComponentCount() const noexcept { return m_componentCount; }
        /**
         * Get entity from packed location
         */
        ASTRA_NODISCARD Entity GetEntity(PackedLocation packedLocation) const 
        { 
            size_t chunkIdx = packedLocation.GetChunkIndex(m_entitiesPerChunkShift);
            size_t entityIdx = packedLocation.GetEntityIndex(m_entitiesPerChunkMask);
            assert(chunkIdx < m_chunks.size());
            return m_chunks[chunkIdx]->GetEntity(entityIdx);
        }
        ASTRA_NODISCARD bool HasComponent(ComponentID id) const { return m_mask.Test(id); }
        
        template<Component C>
        ASTRA_NODISCARD bool HasComponent() const { return m_mask.Test(TypeID<C>::Value()); }
        ASTRA_NODISCARD bool IsInitialized() const noexcept { return m_initialized; }
        ASTRA_NODISCARD const std::vector<ComponentDescriptor>& GetComponents() const { return m_componentDescriptors; }
        ASTRA_NODISCARD size_t GetEntitiesPerChunk() const noexcept { return m_entitiesPerChunk; }
        ASTRA_NODISCARD size_t GetEntitiesPerChunkShift() const noexcept { return m_entitiesPerChunkShift; }
        ASTRA_NODISCARD size_t GetEntitiesPerChunkMask() const noexcept { return m_entitiesPerChunkMask; }
        
        // Test/Debug accessors
        ASTRA_NODISCARD const std::vector<std::unique_ptr<Chunk, ChunkDeleter>>& GetChunks() const { return m_chunks; }
        ASTRA_NODISCARD const std::vector<ChunkMetrics>& GetChunkMetrics() const { return m_chunkMetrics; }
        
        /**
         * Get chunk and entity index from packed location
         */
        ASTRA_NODISCARD std::pair<Chunk*, size_t> GetChunkAndIndex(PackedLocation packedLocation)
        {
            size_t chunkIdx = packedLocation.GetChunkIndex(m_entitiesPerChunkShift);
            size_t entityIdx = packedLocation.GetEntityIndex(m_entitiesPerChunkMask);
            assert(chunkIdx < m_chunks.size());
            return {m_chunks[chunkIdx].get(), entityIdx};
        }
        
        /**
         * Update utilization metrics for all chunks
         */
        void UpdateChunkMetrics()
        {
            for (size_t i = 0; i < m_chunks.size(); ++i)
            {
                m_chunkMetrics[i].utilization = static_cast<float>(m_chunks[i]->GetCount()) / m_entitiesPerChunk;
            }
        }
        
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
        static std::unique_ptr<Archetype> Deserialize(BinaryReader& reader, 
                                                      const std::vector<ComponentDescriptor>& registryDescriptors,
                                                      ChunkPool* chunkPool = nullptr)
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
            archetype->m_chunkPool = chunkPool;
            archetype->Initialize(descriptors);
            
            if (!archetype->IsInitialized())
            {
                return nullptr;
            }
            
            // Clear the pre-allocated chunk
            archetype->m_chunks.clear();
            archetype->m_chunkMetrics.clear();
            archetype->m_entityCount = 0;
            
            // Read each chunk's data
            for (uint32_t chunkIdx = 0; chunkIdx < chunkCount; ++chunkIdx)
            {
                uint32_t chunkEntityCount;
                reader(chunkEntityCount);
                
                // Create new chunk
                void* chunkMemory = archetype->AcquireChunkMemory();
                auto chunk = Chunk::Create(entitiesPerChunk, descriptors, chunkMemory, chunkPool);
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
                archetype->m_chunkMetrics.push_back({});
            }
            
            archetype->m_entityCount = entityCount;
            archetype->UpdateChunkMetrics();
            
            return archetype;
        }
        
        /**
         * Check if coalescing is needed based on utilization
         * 
         * TODO: Implement adaptive coalescing thresholds
         * - Dynamic threshold based on archetype size and access patterns
         * - Consider entity lifetime and churn rate
         * - Factor in memory pressure from ChunkPool
         * - Machine learning based on historical coalescing effectiveness
         * - Different thresholds for different archetype types (hot vs cold)
         */
        ASTRA_NODISCARD bool NeedsCoalescing() const
        {
            if (m_chunks.size() <= 1) ASTRA_LIKELY return false;
            
            size_t sparseCount = 0;
            for (size_t i = 1; i < m_chunkMetrics.size(); ++i)  // Skip first chunk
            {
                if (m_chunkMetrics[i].utilization < COALESCE_UTILIZATION_THRESHOLD) ASTRA_UNLIKELY
                {
                    ++sparseCount;
                }
            }
            
            float sparseRatio = static_cast<float>(sparseCount) / (m_chunks.size() - 1);
            return sparseRatio > SPARSE_CHUNK_RATIO_THRESHOLD;
        }
        
        /**
         * Coalesce sparse chunks to improve memory utilization
         * Returns: pair of (chunks_freed, moved_entities_list)
         */
        std::pair<size_t, std::vector<std::pair<Entity, PackedLocation>>> CoalesceChunks()
        {
            std::vector<std::pair<Entity, PackedLocation>> allMovedEntities;
            if (m_chunks.size() <= 1) ASTRA_LIKELY return {0, allMovedEntities};
            
            UpdateChunkMetrics();
            
            // Find sparse chunks (excluding first chunk)
            std::vector<size_t> sparseIndices;
            for (size_t i = 1; i < m_chunks.size(); ++i)
            {
                if (m_chunkMetrics[i].utilization < COALESCE_UTILIZATION_THRESHOLD) ASTRA_UNLIKELY
                {
                    sparseIndices.push_back(i);
                }
            }
            
            if (sparseIndices.empty()) ASTRA_LIKELY return {0, allMovedEntities};
            
            // Sort by utilization (least utilized first)
            std::sort(sparseIndices.begin(), sparseIndices.end(),
                [this](size_t a, size_t b) {
                    return m_chunkMetrics[a].utilization < m_chunkMetrics[b].utilization;
                });
            
            size_t chunksFreed = 0;
            
            // Try to pack entities from sparse chunks into denser ones
            for (size_t sparseIdx : sparseIndices)
            {
                auto& sparseChunk = m_chunks[sparseIdx];
                size_t entitiesToMove = sparseChunk->GetCount();
                
                if (entitiesToMove == 0) ASTRA_UNLIKELY
                {
                    // Empty chunk - can be removed immediately
                    // Note: We'll handle removal after moving entities
                    continue;
                }
                
                // Find destination chunks with available space
                for (size_t destIdx = 0; destIdx < m_chunks.size(); ++destIdx)
                {
                    if (destIdx == sparseIdx) ASTRA_UNLIKELY continue;
                    
                    auto& destChunk = m_chunks[destIdx];
                    size_t available = m_entitiesPerChunk - destChunk->GetCount();
                    
                    if (available > 0) ASTRA_LIKELY
                    {
                        size_t toMove = std::min(available, entitiesToMove);
                        
                        // Move entities from sparse to destination chunk
                        // Note: Caller (ArchetypeStorage) must update entity locations
                        auto movedEntities = MoveEntitiesBetweenChunks(sparseIdx, destIdx, toMove);
                        allMovedEntities.insert(allMovedEntities.end(), movedEntities.begin(), movedEntities.end());
                        
                        entitiesToMove -= toMove;
                        if (entitiesToMove == 0) ASTRA_LIKELY break;
                    }
                }
            }
            
            // Remove empty chunks (in reverse order to maintain indices)
            for (auto it = m_chunks.rbegin(); it != m_chunks.rend(); )
            {
                size_t idx = std::distance(it + 1, m_chunks.rend());
                if (idx > 0 && (*it)->IsEmpty()) ASTRA_UNLIKELY  // Don't remove first chunk
                {
                    it = std::reverse_iterator(m_chunks.erase((it + 1).base()));
                    m_chunkMetrics.erase(m_chunkMetrics.begin() + idx);
                    ++chunksFreed;
                }
                else
                {
                    ++it;
                }
            }
            
            // No need to update entity array size anymore - entities are stored in chunks
            
            return {chunksFreed, allMovedEntities};
        }
        
    private:
        static constexpr size_t INVALID_CHUNK_INDEX = std::numeric_limits<size_t>::max();
        
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
            auto chunk = Chunk::Create(m_entitiesPerChunk, m_componentDescriptors, AcquireChunkMemory(), m_chunkPool);
            if (!chunk) ASTRA_UNLIKELY
            {
                return {INVALID_CHUNK_INDEX, false};  // Can't allocate new chunk
            }
            
            m_chunks.emplace_back(std::move(chunk));
            m_chunkMetrics.emplace_back();  // Add metrics for new chunk
            chunkIdx = m_chunks.size() - 1;  // Use the newly created chunk
            m_firstNonFullChunkIdx = chunkIdx;  // Update cache to new chunk
            
            return {chunkIdx, true};
        }
        
        // Custom deleter for chunks that returns memory to pool
        struct ChunkDeleter
        {
            ChunkPool* pool = nullptr;
            bool fromPool = false;
            void* memory = nullptr;
            size_t size = 0;
            bool usedHugePages = false;
            
            void operator()(class Chunk* chunk) const
            {
                if (chunk) ASTRA_LIKELY
                {
                    // Delete chunk first
                    delete chunk;
                    
                    // Then handle memory
                    if (fromPool && pool && memory) ASTRA_LIKELY
                    {
                        pool->Release(memory);
                    }
                    else if (!fromPool && memory) ASTRA_UNLIKELY
                    {
                        FreeMemory(memory, size, usedHugePages);
                    }
                }
            }
        };
        
        class Chunk
        {
        public:
            // Factory method that returns nullptr on allocation failure
            static std::unique_ptr<Chunk, ChunkDeleter> Create(size_t entitiesPerChunk, const std::vector<ComponentDescriptor>& componentDescriptors, void* poolMemory = nullptr, ChunkPool* pool = nullptr)
            {
                void* memory = poolMemory;
                bool fromPool = (poolMemory != nullptr);
                
                // Track allocation info
                size_t allocSize = pool ? pool->GetChunkSize() : ChunkPool::DEFAULT_CHUNK_SIZE;
                bool usedHugePages = false;
                
                // Fall back to direct allocation if no pool memory provided
                if (!memory) ASTRA_UNLIKELY
                {
                    AllocResult result = AllocateMemory(allocSize, 64);
                    memory = result.ptr;
                    allocSize = result.size;
                    usedHugePages = result.usedHugePages;
                    if (!memory) ASTRA_UNLIKELY
                    {
                        return nullptr;
                    }
                }
                
                // Create chunk with allocated memory and custom deleter
                ChunkDeleter deleter{pool, fromPool, memory, allocSize, usedHugePages};
                return std::unique_ptr<Chunk, ChunkDeleter>(new Chunk(entitiesPerChunk, componentDescriptors, memory, fromPool, allocSize), deleter);
            }
        
            ~Chunk()
            {
                // Destruct all active components using O(1) lookups
                for (size_t i = 0; i < m_count; ++i)
                {
                    for (ComponentID id = 0; id < MAX_COMPONENTS; ++id)
                    {
                        const auto& info = m_componentArrays[id];
                        if (!info.isValid) continue;
                        
                        void* ptr = static_cast<std::byte*>(info.base) + i * info.stride;
                        ComponentOps::Destruct(info.descriptor, ptr);
                    }
                }
                
                // Memory is either returned to pool by ChunkDeleter or freed here
                // If from pool, ChunkDeleter is responsible for returning it
                // Memory deallocation is handled by ChunkDeleter
            }
            
            // Disable copy
            Chunk(const Chunk&) = delete;
            Chunk& operator=(const Chunk&) = delete;
            
            // Enable move
            Chunk(Chunk&& other) noexcept
                : m_memory(std::exchange(other.m_memory, nullptr))
                , m_capacity(other.m_capacity)
                , m_count(other.m_count)
                , m_componentDescriptors(std::move(other.m_componentDescriptors))
                , m_componentOffsets(std::move(other.m_componentOffsets))
                , m_arrayBases(std::move(other.m_arrayBases))
            {
            }
            
            /**
             * Add entity to chunk (assumes space available)
             * @return Index within chunk
             */
            size_t AddEntity(Entity entity)
            {
                assert(m_count < m_capacity);
                size_t index = m_count++;
                
                // Store entity handle
                m_entities.push_back(entity);
                
                // Default construct components using O(1) lookups
                for (ComponentID id = 0; id < MAX_COMPONENTS; ++id)
                {
                    const auto& info = m_componentArrays[id];
                    if (!info.isValid) continue;
                    
                    if (info.descriptor.is_empty) ASTRA_UNLIKELY
                    {
                        // Empty types don't need initialization
                        continue;
                    }
                    
                    void* ptr = static_cast<std::byte*>(info.base) + index * info.stride;
                    ComponentOps::DefaultConstruct(info.descriptor, ptr);
                }
                
                return index;
            }
            
            void BatchAddEntities(std::span<const Entity> entities)
            {
                size_t count = entities.size();
                assert(m_count + count <= m_capacity);

                // Add entities
                m_entities.insert(m_entities.end(), entities.begin(), entities.end());

                // Batch default construct components using O(1) lookups
                for (ComponentID id = 0; id < MAX_COMPONENTS; ++id)
                {
                    const auto& info = m_componentArrays[id];
                    if (!info.isValid) continue;

                    std::byte* startPtr = static_cast<std::byte*>(info.base) + m_count * info.stride;
                    ComponentOps::BatchDefaultConstruct(info.descriptor, startPtr, count);
                }

                m_count += count;
            }

            /**
             * Remove entity from chunk (swap with last)
             * @return The entity that was moved to fill the gap (if any)
             */
            std::optional<Entity> RemoveEntity(size_t index)
            {
                assert(index < m_count);
                
                const size_t lastIndex = m_count - 1;
                std::optional<Entity> movedEntity;
                
                if (index != lastIndex) ASTRA_LIKELY
                {
                    // Move last entity to this position
                    m_entities[index] = m_entities[lastIndex];
                    movedEntity = m_entities[index];
                    
                    // Move components using O(1) lookups
                    for (ComponentID id = 0; id < MAX_COMPONENTS; ++id)
                    {
                        const auto& info = m_componentArrays[id];
                        if (!info.isValid) continue;
                        
                        void* dstPtr = static_cast<std::byte*>(info.base) + index * info.stride;
                        void* srcPtr = static_cast<std::byte*>(info.base) + lastIndex * info.stride;
                        
                        // Destruct destination, move from source
                        ComponentOps::Destruct(info.descriptor, dstPtr);
                        ComponentOps::MoveConstruct(info.descriptor, dstPtr, srcPtr);
                    }
                }
                else
                {
                    // Just destruct the last entity's components using O(1) lookups
                    for (ComponentID id = 0; id < MAX_COMPONENTS; ++id)
                    {
                        const auto& info = m_componentArrays[id];
                        if (!info.isValid) continue;
                        
                        void* ptr = static_cast<std::byte*>(info.base) + lastIndex * info.stride;
                        ComponentOps::Destruct(info.descriptor, ptr);
                    }
                }
                
                // Remove last entity
                m_entities.pop_back();
                --m_count;
                
                return movedEntity;
            }
            
            /**
             * Get component pointer for specific entity
             */
            template<Component T>
            T* GetComponent(size_t index)
            {
                assert(index < m_count);
                ComponentID id = TypeID<T>::Value();
                return static_cast<T*>(GetComponentPointer(id, index));
            }
            
            /**
             * Get base pointer to component array
             */
            template<Component T>
            T* GetComponentArray()
            {
                ComponentID id = TypeID<T>::Value();
                // Direct array access - still O(1)!
                return reinterpret_cast<T*>(m_componentArrays[id].base);
            }
            
            /**
             * Get component pointer using cached array base (optimized)
             */
            void* GetComponentPointerCached(size_t componentIndex, size_t entityIndex) const
            {
                assert(componentIndex < m_arrayBases.size());
                assert(entityIndex < m_count);
                return static_cast<std::byte*>(m_arrayBases[componentIndex]) + entityIndex * m_componentDescriptors[componentIndex].size;
            }
            
            /**
             * Get component array base by component index
             */
            template<typename T>
            T* GetComponentArrayByIndex(size_t componentIndex)
            {
                assert(componentIndex < m_arrayBases.size());
                return reinterpret_cast<T*>(m_arrayBases[componentIndex]);
            }
            
            /**
             * Get component array by ID (direct access)
             */
            void* GetComponentArrayById(ComponentID id) const
            {
                return m_componentArrays[id].base;
            }
            
            ASTRA_NODISCARD bool IsFull() const noexcept { return m_count >= m_capacity; }
            ASTRA_NODISCARD bool IsEmpty() const noexcept { return m_count == 0; }
            ASTRA_NODISCARD size_t GetCount() const noexcept { return m_count; }
            ASTRA_NODISCARD size_t GetCapacity() const noexcept { return m_capacity; }
            ASTRA_NODISCARD Entity GetEntity(size_t index) const { assert(index < m_count); return m_entities[index]; }
            ASTRA_NODISCARD const std::vector<Entity>& GetEntities() const { return m_entities; }
            ASTRA_NODISCARD std::vector<Entity>& GetEntities() { return m_entities; }
            ASTRA_NODISCARD const auto& GetComponentArrays() const { return m_componentArrays; }
            
            // Used for chunk coalescing
            void SetCount(size_t count) noexcept { m_count = count; }
            
            void* GetComponentPointer(ComponentID id, size_t index) const
            {
                // O(1) lookup with pre-cached stride!
                const auto& info = m_componentArrays[id];
                if (!info.base) ASTRA_UNLIKELY return nullptr;
                
                return static_cast<std::byte*>(info.base) + index * info.stride;
            }
            
        private:
            // Private constructor - only callable from factory
            Chunk(size_t entitiesPerChunk, const std::vector<ComponentDescriptor>& componentDescriptors, void* memory, bool fromPool, size_t chunkSize)
                : m_memory(memory)
                , m_capacity(entitiesPerChunk)
                , m_count(0)
                , m_componentDescriptors(componentDescriptors)
                , m_fromPool(fromPool)
                , m_chunkSize(chunkSize)
            {
                // Reserve space for entities
                m_entities.reserve(m_capacity);
                
                // Calculate layout for SoA within chunk
                CalculateLayout();

                // Clear memory
                std::memset(m_memory, 0, m_chunkSize);

                // Pre-compute array base pointers and strides
                m_arrayBases.resize(m_componentDescriptors.size());
                for (size_t i = 0; i < m_componentDescriptors.size(); ++i)
                {
                    void* arrayBase = static_cast<std::byte*>(m_memory) + m_componentOffsets[m_componentDescriptors[i].id];
                    m_arrayBases[i] = arrayBase;
                    // Store full descriptor info for O(1) access by ComponentID
                    m_componentArrays[m_componentDescriptors[i].id] = {
                        arrayBase, 
                        m_componentDescriptors[i].size,
                        m_componentDescriptors[i],
                        true
                    };
                }
            }

            void CalculateLayout()
            {
                size_t offset = 0;
                
                for (const auto& comp : m_componentDescriptors)
                {
                    // Align offset to cache line boundary for better cache performance
                    // This prevents false sharing between component arrays
                    offset = (offset + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1);
                    
                    // Store offset for this component's array - direct array access
                    m_componentOffsets[comp.id] = offset;
                    
                    // Advance by total size of component array
                    offset += comp.size * m_capacity;
                }
                
                // Ensure we don't exceed chunk size
                assert(offset <= m_chunkSize);
            }

            // Optimized component array info for O(1) lookups
            struct ComponentArrayInfo {
                void* base{nullptr};
                size_t stride{0};  // Component size for pointer arithmetic
                ComponentDescriptor descriptor{};  // Full descriptor for O(1) component operations
                bool isValid{false};  // Whether this component exists in the archetype
            };

            void* m_memory;
            size_t m_capacity;  // Max entities per chunk
            size_t m_count;     // Current entity count
            std::vector<Entity> m_entities;  // Entity handles stored in this chunk
            std::vector<ComponentDescriptor> m_componentDescriptors;
            std::array<size_t, MAX_COMPONENTS> m_componentOffsets{};  // Direct array access by ComponentID
            std::array<ComponentArrayInfo, MAX_COMPONENTS> m_componentArrays{};   // Direct pointers + stride indexed by ComponentID
            std::vector<void*> m_arrayBases;  // Cached base pointers indexed by component index
            bool m_fromPool;  // Whether memory came from chunk pool
            size_t m_chunkSize;  // Size of this chunk
        };

        void* AcquireChunkMemory()
        {
            if (m_chunkPool) ASTRA_LIKELY
            {
                return m_chunkPool->Acquire();
            }
            return nullptr;
        }

        void ReleaseChunkMemory(void* memory)
        {
            if (m_chunkPool && memory) ASTRA_LIKELY
            {
                m_chunkPool->Release(memory);
            }
        }

        PackedLocation AddEntityNoConstruct(Entity entity)
        {
            // Find or create a chunk with space
            auto [chunkIdx, wasCreated] = FindOrCreateChunkWithSpace();
            if (chunkIdx == INVALID_CHUNK_INDEX) ASTRA_UNLIKELY
            {
                return PackedLocation();  // Allocation failed
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

            // Return packed location
            return PackedLocation::Pack(chunkIdx, entityIdx, m_entitiesPerChunkShift);
        }
        
        /**
         * Move entities between chunks for coalescing
         * Returns pairs of (entity, new_packed_location) for entities that were moved
         */
        std::vector<std::pair<Entity, PackedLocation>> MoveEntitiesBetweenChunks(size_t srcChunkIdx, size_t destChunkIdx, size_t count)
        {
            std::vector<std::pair<Entity, PackedLocation>> movedEntities;
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
                
                // Create packed location for moved entity
                PackedLocation destPackedLocation = PackedLocation::Pack(destChunkIdx, destEntityIdx, m_entitiesPerChunkShift);
                movedEntities.emplace_back(entity, destPackedLocation);
                
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
                    ComponentOps::MoveConstruct(srcInfo.descriptor, destPtr, srcPtr);
                    ComponentOps::Destruct(srcInfo.descriptor, srcPtr);
                }
                
                // Remove entity from source chunk's entity vector
                srcChunk->GetEntities().pop_back();
            }
            
            // Update chunk counts
            srcChunk->SetCount(srcCount - count);
            destChunk->SetCount(destCount + count);
            
            return movedEntities;
        }

        // ====================== Edge Storage Methods (NEW) ======================
        
        /**
         * Get or create an edge for adding a component
         * @param componentId The component ID to add
         * @param targetArchetype The archetype to transition to
         * @return Pointer to the edge (never null)
         */
        ArchetypeEdgeStorage::Edge* GetOrCreateAddEdge(ComponentID componentId, Archetype* targetArchetype)
        {
            return m_addEdges.GetOrCreateEdge(componentId, targetArchetype);
        }
        
        /**
         * Get an edge for adding a component
         * @param componentId The component ID to look up
         * @return Pointer to the edge if found, nullptr otherwise
         */
        [[nodiscard]] const ArchetypeEdgeStorage::Edge* GetAddEdge(ComponentID componentId) const noexcept
        {
            return m_addEdges.GetEdge(componentId);
        }
        
        /**
         * Get or create an edge for removing a component
         * @param componentId The component ID to remove
         * @param targetArchetype The archetype to transition to
         * @return Pointer to the edge (never null)
         */
        ArchetypeEdgeStorage::Edge* GetOrCreateRemoveEdge(ComponentID componentId, Archetype* targetArchetype)
        {
            return m_removeEdges.GetOrCreateEdge(componentId, targetArchetype);
        }
        
        /**
         * Get an edge for removing a component
         * @param componentId The component ID to look up
         * @return Pointer to the edge if found, nullptr otherwise
         */
        [[nodiscard]] const ArchetypeEdgeStorage::Edge* GetRemoveEdge(ComponentID componentId) const noexcept
        {
            return m_removeEdges.GetEdge(componentId);
        }
        
        /**
         * Remove all edges pointing to a specific archetype
         * Used when an archetype is being destroyed
         * @param archetype The target archetype to remove
         * @return Total number of edges removed
         */
        size_t RemoveEdgesTo(Archetype* archetype)
        {
            size_t removed = m_addEdges.RemoveEdgesTo(archetype);
            removed += m_removeEdges.RemoveEdgesTo(archetype);
            return removed;
        }
        
    private:
        ComponentMask m_mask;
        size_t m_componentCount;  // Cached component count for fast access
        std::vector<ComponentDescriptor> m_componentDescriptors;
        std::vector<std::unique_ptr<Chunk, ChunkDeleter>> m_chunks;
        std::vector<ChunkMetrics> m_chunkMetrics;  // Utilization tracking
        size_t m_entityCount;
        size_t m_entitiesPerChunk;
        size_t m_entitiesPerChunkShift;  // For fast division via bit shift (log2(m_entitiesPerChunk))
        size_t m_entitiesPerChunkMask;   // For fast modulo operations (m_entitiesPerChunk - 1)
        size_t m_firstNonFullChunkIdx = 0;  // Track first chunk with available space for O(1) lookup
        bool m_initialized;
        
        static constexpr float COALESCE_UTILIZATION_THRESHOLD = 0.5f;
        static constexpr float SPARSE_CHUNK_RATIO_THRESHOLD = 0.25f;

        class ChunkPool* m_chunkPool = nullptr;
        
        ArchetypeEdgeStorage m_addEdges;
        ArchetypeEdgeStorage m_removeEdges;
        friend class ArchetypeStorage;
        template<typename...> friend class View;
        
    };
}