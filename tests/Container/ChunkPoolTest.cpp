#include <gtest/gtest.h>
#include "Astra/Memory/ChunkPool.hpp"
#include <vector>
#include <set>
#include <thread>
#include <cstring>


class ChunkPoolTest : public ::testing::Test
{
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Test basic acquire and release
TEST_F(ChunkPoolTest, BasicAcquireRelease)
{
    Astra::ChunkPool pool;
    
    // Acquire a chunk
    void* chunk1 = pool.Acquire();
    ASSERT_NE(chunk1, nullptr);
    
    // Verify stats
    auto stats = pool.GetStats();
    EXPECT_EQ(stats.acquireCount, 1u);
    EXPECT_EQ(stats.releaseCount, 0u);
    EXPECT_GT(stats.totalChunks, 0u);
    
    // Write some data to verify the chunk is valid memory
    std::memset(chunk1, 0xAB, pool.GetChunkSize());
    
    // Release the chunk
    pool.Release(chunk1);
    
    // Verify release stats
    stats = pool.GetStats();
    EXPECT_EQ(stats.releaseCount, 1u);
    
    // Acquire again - may get the same chunk back (recycled)
    void* chunk2 = pool.Acquire();
    ASSERT_NE(chunk2, nullptr);
    
    // With our optimization, recycled chunks are NOT zeroed by default
    // This is fine because components will be explicitly initialized when used
    // The chunk memory content is undefined until components are constructed
    
    pool.Release(chunk2);
}

// Test batch acquisition
TEST_F(ChunkPoolTest, BatchAcquire)
{
    Astra::ChunkPool pool;
    std::vector<void*> chunks;
    
    // Acquire 10 chunks in batch
    size_t requested = 10;
    size_t acquired = pool.AcquireBatch(requested, chunks);
    
    EXPECT_EQ(acquired, requested);
    EXPECT_EQ(chunks.size(), requested);
    
    // All chunks should be unique and valid
    std::set<void*> uniqueChunks(chunks.begin(), chunks.end());
    EXPECT_EQ(uniqueChunks.size(), chunks.size());
    
    for (void* chunk : chunks)
    {
        EXPECT_NE(chunk, nullptr);
    }
    
    // Release all chunks
    for (void* chunk : chunks)
    {
        pool.Release(chunk);
    }
    
    auto stats = pool.GetStats();
    EXPECT_EQ(stats.acquireCount, requested);
    EXPECT_EQ(stats.releaseCount, requested);
}

// Test pool recycling
TEST_F(ChunkPoolTest, PoolRecycling)
{
    Astra::ChunkPool pool;
    
    // Acquire and release multiple times
    std::vector<void*> firstBatch;
    for (int i = 0; i < 5; ++i)
    {
        void* chunk = pool.Acquire();
        ASSERT_NE(chunk, nullptr);
        firstBatch.push_back(chunk);
    }
    
    // Release all
    for (void* chunk : firstBatch)
    {
        pool.Release(chunk);
    }
    
    // Acquire again - should reuse from free list
    std::vector<void*> secondBatch;
    for (int i = 0; i < 5; ++i)
    {
        void* chunk = pool.Acquire();
        ASSERT_NE(chunk, nullptr);
        secondBatch.push_back(chunk);
    }
    
    // The chunks should come from the free list (same pointers)
    // Note: Order might be different due to LIFO free list
    std::set<void*> firstSet(firstBatch.begin(), firstBatch.end());
    std::set<void*> secondSet(secondBatch.begin(), secondBatch.end());
    EXPECT_EQ(firstSet, secondSet);
    
    // Cleanup
    for (void* chunk : secondBatch)
    {
        pool.Release(chunk);
    }
}

// Test pool configuration
TEST_F(ChunkPoolTest, PoolConfiguration)
{
    Astra::ChunkPool::Config config;
    config.chunksPerBlock = 4;
    config.maxChunks = 16;
    config.initialBlocks = 2;
    config.useHugePages = false; // Disable for testing
    
    Astra::ChunkPool pool(config);
    
    auto stats = pool.GetStats();
    // Should have pre-allocated 2 blocks * 4 chunks = 8 chunks
    EXPECT_GE(stats.totalChunks, 8u);
    EXPECT_EQ(stats.blockAllocations, 2u);
    
    // Try to acquire more than max
    std::vector<void*> chunks;
    size_t acquired = pool.AcquireBatch(20, chunks);
    
    // Should only get up to maxChunks (16)
    EXPECT_LE(acquired, 16u);
    
    // Release all
    for (void* chunk : chunks)
    {
        pool.Release(chunk);
    }
}

// Test OwnsChunk
TEST_F(ChunkPoolTest, OwnsChunk)
{
    Astra::ChunkPool pool;
    
    void* poolChunk = pool.Acquire();
    ASSERT_NE(poolChunk, nullptr);
    
    // Pool should own this chunk
    EXPECT_TRUE(pool.OwnsChunk(poolChunk));
    
    // Random pointer should not be owned
    int stackVar = 42;
    EXPECT_FALSE(pool.OwnsChunk(&stackVar));
    
    // Null should not be owned
    EXPECT_FALSE(pool.OwnsChunk(nullptr));
    
    // Heap allocated memory should not be owned
    void* heapMem = malloc(1024);
    EXPECT_FALSE(pool.OwnsChunk(heapMem));
    free(heapMem);
    
    pool.Release(poolChunk);
}

// Test releasing null pointer
TEST_F(ChunkPoolTest, ReleaseNull)
{
    Astra::ChunkPool pool;
    
    // Should handle null gracefully
    pool.Release(nullptr);
    
    auto stats = pool.GetStats();
    EXPECT_EQ(stats.releaseCount, 0u);
}

// Test move semantics
TEST_F(ChunkPoolTest, MoveSemantics)
{
    Astra::ChunkPool pool1;
    
    // Acquire some chunks
    void* chunk1 = pool1.Acquire();
    void* chunk2 = pool1.Acquire();
    ASSERT_NE(chunk1, nullptr);
    ASSERT_NE(chunk2, nullptr);
    
    auto stats1 = pool1.GetStats();
    size_t acquireCount1 = stats1.acquireCount;
    
    // Move construct
    Astra::ChunkPool pool2(std::move(pool1));
    
    // pool2 should now own the chunks
    EXPECT_TRUE(pool2.OwnsChunk(chunk1));
    EXPECT_TRUE(pool2.OwnsChunk(chunk2));
    
    auto stats2 = pool2.GetStats();
    EXPECT_EQ(stats2.acquireCount, acquireCount1);
    
    // Can still use pool2
    pool2.Release(chunk1);
    pool2.Release(chunk2);
    
    // Move assign
    Astra::ChunkPool pool3;
    pool3 = std::move(pool2);
    
    // pool3 should work
    void* chunk3 = pool3.Acquire();
    ASSERT_NE(chunk3, nullptr);
    pool3.Release(chunk3);
}

// Test statistics
TEST_F(ChunkPoolTest, Statistics)
{
    Astra::ChunkPool pool;
    
    // Initial stats
    auto stats = pool.GetStats();
    EXPECT_EQ(stats.acquireCount, 0u);
    EXPECT_EQ(stats.releaseCount, 0u);
    EXPECT_EQ(stats.failedAcquires, 0u);
    
    // Acquire and release
    std::vector<void*> chunks;
    pool.AcquireBatch(5, chunks);
    
    stats = pool.GetStats();
    EXPECT_EQ(stats.acquireCount, 5u);
    EXPECT_GE(stats.totalChunks, 5u);
    EXPECT_LE(stats.freeChunks, stats.totalChunks - 5u);
    
    for (void* chunk : chunks)
    {
        pool.Release(chunk);
    }
    
    stats = pool.GetStats();
    EXPECT_EQ(stats.releaseCount, 5u);
    EXPECT_EQ(stats.freeChunks, stats.totalChunks);
}

// Test exhaustion handling
TEST_F(ChunkPoolTest, PoolExhaustion)
{
    Astra::ChunkPool::Config config;
    config.chunksPerBlock = 2;
    config.maxChunks = 4;
    config.initialBlocks = 0;
    config.useHugePages = false;
    
    Astra::ChunkPool pool(config);
    
    std::vector<void*> chunks;
    
    // Acquire all available chunks
    for (int i = 0; i < 4; ++i)
    {
        void* chunk = pool.Acquire();
        if (chunk)
        {
            chunks.push_back(chunk);
        }
    }
    
    EXPECT_EQ(chunks.size(), 4u);
    
    // Try to acquire more - should fail
    void* extraChunk = pool.Acquire();
    EXPECT_EQ(extraChunk, nullptr);
    
    auto stats = pool.GetStats();
    EXPECT_GT(stats.failedAcquires, 0u);
    
    // Release one and try again
    pool.Release(chunks[0]);
    chunks[0] = pool.Acquire();
    EXPECT_NE(chunks[0], nullptr);
    
    // Cleanup
    for (void* chunk : chunks)
    {
        pool.Release(chunk);
    }
}

// Test batch allocation with partial success
TEST_F(ChunkPoolTest, PartialBatchAcquire)
{
    Astra::ChunkPool::Config config;
    config.chunksPerBlock = 2;
    config.maxChunks = 6;  // Changed to 6 to allow exactly 3 blocks
    config.initialBlocks = 0;
    config.useHugePages = false;
    
    Astra::ChunkPool pool(config);
    
    std::vector<void*> chunks;
    
    // Request more than available
    size_t requested = 10;
    size_t acquired = pool.AcquireBatch(requested, chunks);
    
    // Should get only what's available (6)
    EXPECT_EQ(acquired, 6u);
    EXPECT_EQ(chunks.size(), 6u);
    
    auto stats = pool.GetStats();
    EXPECT_EQ(stats.failedAcquires, requested - acquired);
    
    // Release all
    for (void* chunk : chunks)
    {
        pool.Release(chunk);
    }
}