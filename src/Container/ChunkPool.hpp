#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <utility>
#include <vector>

#include "../Core/Base.hpp"
#include "../Core/Memory.hpp"
#include "../Platform/Hardware.hpp"

namespace Astra
{
    class ChunkPool
    {
    public:
        static constexpr size_t CHUNK_SIZE = 16 * 1024;  // 16KB chunks
        
        // Configuration for pool behavior
        struct Config
        {
            size_t chunksPerBlock = 64; // Chunks allocated together (1MB blocks by default)
            size_t maxChunks = 4096;    // Maximum chunks (64MB by default)
            size_t initialBlocks = 0;   // Pre-allocate this many blocks
            bool useHugePages = true;   // Try to use huge pages for allocations
        };
        
        struct Stats
        {
            size_t totalChunks = 0;      // Total chunks allocated
            size_t freeChunks = 0;       // Currently available chunks
            size_t acquireCount = 0;     // Total acquires
            size_t releaseCount = 0;     // Total releases
            size_t blockAllocations = 0; // Number of block allocations
            size_t failedAcquires = 0;   // Acquires that failed (pool exhausted)
        };
        
        explicit ChunkPool(const Config& config = {}) :
            m_config(config),
            m_freeList(nullptr)
        {
            // Validate configuration
            if (m_config.chunksPerBlock == 0)
                m_config.chunksPerBlock = 64;
            
            if (m_config.maxChunks < m_config.chunksPerBlock)
                m_config.maxChunks = m_config.chunksPerBlock;
            
            // Pre-allocate initial blocks if requested
            for (size_t i = 0; i < m_config.initialBlocks; ++i)
            {
                AllocateBlock();
            }
        }
        
        ~ChunkPool()
        {
            // Free all allocated blocks
            for (const auto& block : m_blocks)
            {
                FreeMemory(block.memory, block.size, block.usedHugePages);
            }
        }
        
        // Disable copy
        ChunkPool(const ChunkPool&) = delete;
        ChunkPool& operator=(const ChunkPool&) = delete;
        
        // Enable move
        ChunkPool(ChunkPool&& other) noexcept :
            m_config(other.m_config),
            m_blocks(std::move(other.m_blocks)),
            m_freeList(other.m_freeList),
            m_totalChunks(other.m_totalChunks.load()),
            m_freeChunks(other.m_freeChunks.load()),
            m_acquireCount(other.m_acquireCount.load()),
            m_releaseCount(other.m_releaseCount.load()),
            m_blockAllocations(other.m_blockAllocations.load()),
            m_failedAcquires(other.m_failedAcquires.load())
        {
            other.m_freeList = nullptr;
        }
        
        ChunkPool& operator=(ChunkPool&& other) noexcept
        {
            if (this != &other)
            {
                m_config = other.m_config;
                m_blocks = std::move(other.m_blocks);
                m_freeList = other.m_freeList;
                // Copy atomic values
                m_totalChunks.store(other.m_totalChunks.load());
                m_freeChunks.store(other.m_freeChunks.load());
                m_acquireCount.store(other.m_acquireCount.load());
                m_releaseCount.store(other.m_releaseCount.load());
                m_blockAllocations.store(other.m_blockAllocations.load());
                m_failedAcquires.store(other.m_failedAcquires.load());
                other.m_freeList = nullptr;
            }
            return *this;
        }
        
        /**
         * Acquire a chunk from the pool
         * @return Pointer to 16KB aligned memory, or nullptr if pool exhausted
         */
        ASTRA_NODISCARD void* Acquire()
        {
            // Check free list first
            if (m_freeList) ASTRA_LIKELY
            {
                void* chunk = m_freeList;
                m_freeList = m_freeList->next;
                
                m_freeChunks.fetch_sub(1, std::memory_order_relaxed);
                m_acquireCount.fetch_add(1, std::memory_order_relaxed);
                
                // Clear the chunk for security/consistency
                std::memset(chunk, 0, CHUNK_SIZE);
                
                return chunk;
            }
            
            // Free list empty, try to allocate new block
            if (m_totalChunks < m_config.maxChunks) ASTRA_UNLIKELY
            {
                if (AllocateBlock())
                {
                    // Retry acquire after allocating block
                    return Acquire();
                }
            }
            
            // Pool exhausted
            m_failedAcquires.fetch_add(1, std::memory_order_relaxed);
            return nullptr;
        }
        
        /**
         * Acquire multiple chunks from the pool in batch
         * @param count Number of chunks to acquire
         * @param outChunks Output vector to store acquired chunks
         * @return Number of chunks actually acquired
         */
        size_t AcquireBatch(size_t count, std::vector<void*>& outChunks)
        {
            if (count == 0) ASTRA_UNLIKELY
                return 0;
                
            size_t acquired = 0;
            outChunks.reserve(outChunks.size() + count);
            
            // First, grab as many as possible from free list
            while (acquired < count && m_freeList) ASTRA_LIKELY
            {
                void* chunk = m_freeList;
                m_freeList = m_freeList->next;
                
                // Clear the chunk
                std::memset(chunk, 0, CHUNK_SIZE);
                outChunks.push_back(chunk);
                ++acquired;
            }
            
            // Update stats for free list acquisitions
            if (acquired > 0) ASTRA_LIKELY
            {
                m_freeChunks.fetch_sub(acquired, std::memory_order_relaxed);
                m_acquireCount.fetch_add(acquired, std::memory_order_relaxed);
            }
            
            // If we need more, allocate new blocks
            while (acquired < count && m_totalChunks < m_config.maxChunks) ASTRA_UNLIKELY
            {
                size_t remaining = count - acquired;
                size_t blocksNeeded = (remaining + m_config.chunksPerBlock - 1) / m_config.chunksPerBlock;
                
                // Don't exceed max chunks
                size_t availableChunks = m_config.maxChunks - m_totalChunks;
                size_t maxBlocks = availableChunks / m_config.chunksPerBlock;
                blocksNeeded = std::min(blocksNeeded, maxBlocks);
                
                if (blocksNeeded == 0) ASTRA_UNLIKELY
                    break;
                
                // Allocate blocks and take chunks directly
                for (size_t i = 0; i < blocksNeeded; ++i)
                {
                    if (!AllocateBlockForBatch(outChunks, remaining)) ASTRA_UNLIKELY
                        break;
                    
                    size_t chunksFromBlock = std::min(remaining, m_config.chunksPerBlock);
                    acquired += chunksFromBlock;
                    remaining -= chunksFromBlock;
                    
                    if (remaining == 0) ASTRA_UNLIKELY
                        break;
                }
            }
            
            // Update failed acquires if we couldn't get all requested
            if (acquired < count) ASTRA_UNLIKELY
            {
                m_failedAcquires.fetch_add(count - acquired, std::memory_order_relaxed);
            }
            
            return acquired;
        }
        
        /**
         * Release a chunk back to the pool
         * @param chunk Pointer to chunk memory (must be from this pool)
         */
        void Release(void* chunk)
        {
            if (!chunk) ASTRA_UNLIKELY
                return;
            
            // Add to free list
            auto* node = reinterpret_cast<FreeNode*>(chunk);
            node->next = m_freeList;
            m_freeList = node;
            
            m_freeChunks.fetch_add(1, std::memory_order_relaxed);
            m_releaseCount.fetch_add(1, std::memory_order_relaxed);
        }
        
        /**
         * Get current pool statistics
         * Returns a snapshot of the current statistics
         * 
         * TODO: Add extended metrics and instrumentation
         * - Memory usage tracking (current vs peak)
         * - Allocation latency histograms
         * - Fragmentation metrics
         * - Cache hit/miss rates for free list
         * - Time-based metrics (allocations per second)
         */
        ASTRA_NODISCARD Stats GetStats() const
        {
            Stats snapshot;
            snapshot.totalChunks = m_totalChunks.load(std::memory_order_relaxed);
            snapshot.freeChunks = m_freeChunks.load(std::memory_order_relaxed);
            snapshot.acquireCount = m_acquireCount.load(std::memory_order_relaxed);
            snapshot.releaseCount = m_releaseCount.load(std::memory_order_relaxed);
            snapshot.blockAllocations = m_blockAllocations.load(std::memory_order_relaxed);
            snapshot.failedAcquires = m_failedAcquires.load(std::memory_order_relaxed);
            return snapshot;
        }
        
        /**
         * Check if a pointer belongs to this pool
         * Useful for determining if memory should be released to pool or freed directly
         */
        ASTRA_NODISCARD bool OwnsChunk(void* chunk) const
        {
            if (!chunk) ASTRA_UNLIKELY
                return false;
            
            for (const auto& block : m_blocks)
            {
                auto* blockStart = block.memory;
                auto* blockEnd = reinterpret_cast<char*>(blockStart) + block.size;
                
                if (chunk >= blockStart && chunk < blockEnd) ASTRA_UNLIKELY
                {
                    // Verify alignment
                    auto offset = reinterpret_cast<char*>(chunk) - reinterpret_cast<char*>(blockStart);
                    return offset % sizeof(PooledChunk) == 0;
                }
            }
            
            return false;
        }
        
    private:
        // Node in the free list
        struct FreeNode
        {
            FreeNode* next;
        };
        
        // Aligned chunk structure
        struct alignas(CACHE_LINE_SIZE) PooledChunk
        {
            std::byte memory[CHUNK_SIZE];
        };
        
        // Block allocation info
        struct BlockInfo
        {
            void* memory = nullptr;
            size_t size = 0;
            size_t chunkCount = 0;
            bool usedHugePages = false;
        };
        
        /**
         * Allocate a new block of chunks
         * @return true if successful, false if allocation failed or limit reached
         */
        bool AllocateBlock()
        {
            size_t remainingCapacity = m_config.maxChunks - m_totalChunks;
            if (remainingCapacity == 0) ASTRA_UNLIKELY
                return false;
            
            size_t chunksToAllocate = std::min(m_config.chunksPerBlock, remainingCapacity);
            size_t blockSize = chunksToAllocate * sizeof(PooledChunk);
            
            // Allocate block using huge pages if configured
            AllocFlags flags = AllocFlags::ZeroMemory;
            if (m_config.useHugePages)
            {
                flags = flags | AllocFlags::HugePages;
            }
            
            AllocResult result = AllocateMemory(blockSize, CACHE_LINE_SIZE, flags);
            if (!result.ptr) ASTRA_UNLIKELY
            {
                return false;
            }
            
            // Create block info
            BlockInfo blockInfo;
            blockInfo.memory = result.ptr;
            blockInfo.size = result.size;
            blockInfo.chunkCount = chunksToAllocate;
            blockInfo.usedHugePages = result.usedHugePages;
            
            // Add all chunks to free list
            auto* chunks = static_cast<PooledChunk*>(result.ptr);
            for (size_t i = 0; i < chunksToAllocate; ++i)
            {
                auto* node = reinterpret_cast<FreeNode*>(&chunks[i]);
                node->next = m_freeList;
                m_freeList = node;
            }
            
            // Update statistics
            m_totalChunks.fetch_add(chunksToAllocate, std::memory_order_relaxed);
            m_freeChunks.fetch_add(chunksToAllocate, std::memory_order_relaxed);
            m_blockAllocations.fetch_add(1, std::memory_order_relaxed);
            
            // Store block
            m_blocks.push_back(blockInfo);
            
            return true;
        }
        
        /**
         * Allocate a new block and directly add chunks to output vector
         * Used for batch allocation to avoid free list overhead
         * @param outChunks Output vector to append chunks to
         * @param maxChunks Maximum chunks to take from this block
         * @return true if successful, false if allocation failed
         */
        bool AllocateBlockForBatch(std::vector<void*>& outChunks, size_t maxChunks)
        {
            size_t remainingCapacity = m_config.maxChunks - m_totalChunks;
            if (remainingCapacity == 0) ASTRA_UNLIKELY
                return false;
            
            size_t chunksToAllocate = std::min(m_config.chunksPerBlock, remainingCapacity);
            size_t chunksToTake = std::min(chunksToAllocate, maxChunks);
            size_t blockSize = chunksToAllocate * sizeof(PooledChunk);
            
            // Allocate block using huge pages if configured
            AllocFlags flags = AllocFlags::ZeroMemory;
            if (m_config.useHugePages)
            {
                flags = flags | AllocFlags::HugePages;
            }
            
            AllocResult result = AllocateMemory(blockSize, CACHE_LINE_SIZE, flags);
            if (!result.ptr) ASTRA_UNLIKELY
            {
                return false;
            }
            
            // Create block info
            BlockInfo blockInfo;
            blockInfo.memory = result.ptr;
            blockInfo.size = result.size;
            blockInfo.chunkCount = chunksToAllocate;
            blockInfo.usedHugePages = result.usedHugePages;
            
            // Take chunks directly to output vector
            auto* chunks = static_cast<PooledChunk*>(result.ptr);
            for (size_t i = 0; i < chunksToTake; ++i)
            {
                void* chunk = &chunks[i];
                // Already zero initialized by AllocateMemory
                outChunks.push_back(chunk);
            }
            
            // Add remaining chunks to free list
            for (size_t i = chunksToTake; i < chunksToAllocate; ++i)
            {
                auto* node = reinterpret_cast<FreeNode*>(&chunks[i]);
                node->next = m_freeList;
                m_freeList = node;
            }
            
            // Update statistics
            m_totalChunks.fetch_add(chunksToAllocate, std::memory_order_relaxed);
            m_freeChunks.fetch_add(chunksToAllocate - chunksToTake, std::memory_order_relaxed);
            m_acquireCount.fetch_add(chunksToTake, std::memory_order_relaxed);
            m_blockAllocations.fetch_add(1, std::memory_order_relaxed);
            
            // Store block
            m_blocks.push_back(blockInfo);
            
            return true;
        }
        
        Config m_config;
        std::vector<BlockInfo> m_blocks;
        FreeNode* m_freeList;
        
        // Atomic statistics
        std::atomic<size_t> m_totalChunks{0};
        std::atomic<size_t> m_freeChunks{0};
        std::atomic<size_t> m_acquireCount{0};
        std::atomic<size_t> m_releaseCount{0};
        std::atomic<size_t> m_blockAllocations{0};
        std::atomic<size_t> m_failedAcquires{0};
    };
}