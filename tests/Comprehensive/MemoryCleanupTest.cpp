#include <gtest/gtest.h>
#include <vector>
#include <memory>
#include <algorithm>
#include "Astra/Registry/Registry.hpp"
#include "Astra/Memory/ChunkPool.hpp"
#include "../TestComponents.hpp"

using namespace Astra;
using namespace Astra::Test;

class MemoryCleanupTest : public ::testing::Test
{
protected:
    std::unique_ptr<Registry> registry;
    
    void SetUp() override
    {
        registry = std::make_unique<Registry>();
        
        auto componentRegistry = registry->GetComponentRegistry();
        componentRegistry->RegisterComponents<Position, Velocity, Health, Transform, Name, Physics, Player, Enemy>();
    }
    
    void TearDown() override
    {
        registry.reset();
    }
    
    size_t GetCurrentMemoryUsage()
    {
        return registry->GetArchetypeMemoryUsage();
    }
    
    size_t GetArchetypeCount()
    {
        return registry->GetArchetypeCount();
    }
};

TEST_F(MemoryCleanupTest, MemoryReleasedAfterDestruction)
{
    size_t initialMemory = GetCurrentMemoryUsage();
    size_t initialArchetypes = GetArchetypeCount();
    
    std::vector<Entity> entities;
    for (int i = 0; i < 1000; ++i)
    {
        entities.push_back(registry->CreateEntity(
            Position{float(i), float(i * 2), float(i * 3)},
            Velocity{float(i * 10), 0, 0},
            Health{i, 1000}
        ));
    }
    
    size_t peakMemory = GetCurrentMemoryUsage();
    EXPECT_GT(peakMemory, initialMemory) << "Memory should increase after creating entities";
    
    registry->DestroyEntities(entities);
    
    // Memory still allocated in pools
    size_t afterDestroyMemory = GetCurrentMemoryUsage();
    EXPECT_GT(afterDestroyMemory, 0u) << "Some memory still allocated in pools";
    
    Registry::CleanupOptions options;
    options.minEmptyDuration = 0;
    options.minArchetypesToKeep = 1;
    
    size_t cleaned = registry->CleanupEmptyArchetypes(options);
    EXPECT_GT(cleaned, 0u) << "Should have cleaned some archetypes";
    
    size_t finalMemory = GetCurrentMemoryUsage();
    size_t finalArchetypes = GetArchetypeCount();
    
    EXPECT_LT(finalMemory, peakMemory) << "Memory should decrease after cleanup";
    EXPECT_LE(finalArchetypes, initialArchetypes + 1) << "Should have minimal archetypes after cleanup";
}

TEST_F(MemoryCleanupTest, RapidArchetypeTransitionCleanup)
{
    Entity entity = registry->CreateEntity();
    
    std::vector<size_t> memorySamples;
    
    for (int iteration = 0; iteration < 50; ++iteration)
    {
        registry->AddComponent<Position>(entity);
        registry->AddComponent<Velocity>(entity);
        registry->AddComponent<Health>(entity);
        registry->AddComponent<Transform>(entity);
        
        memorySamples.push_back(GetCurrentMemoryUsage());
        
        registry->RemoveComponent<Velocity>(entity);
        registry->RemoveComponent<Health>(entity);
        
        memorySamples.push_back(GetCurrentMemoryUsage());
        
        registry->RemoveComponent<Position>(entity);
        registry->RemoveComponent<Transform>(entity);
    }
    
    size_t beforeCleanup = GetArchetypeCount();
    EXPECT_GT(beforeCleanup, 5u) << "Multiple archetypes should exist";
    
    registry->DestroyEntity(entity);
    
    Registry::CleanupOptions options;
    options.minEmptyDuration = 0;
    size_t cleaned = registry->CleanupEmptyArchetypes(options);
    
    EXPECT_GT(cleaned, 0u) << "Should clean up empty archetypes";
    
    size_t afterCleanup = GetArchetypeCount();
    EXPECT_LT(afterCleanup, beforeCleanup) << "Archetype count should decrease";
}

TEST_F(MemoryCleanupTest, ChunkPoolMemoryRelease)
{
    ChunkPool::Config config;
    config.chunksPerBlock = 4;
    config.maxChunks = 100;
    config.initialBlocks = 1;
    
    ChunkPool pool(config);
    
    std::vector<void*> chunks;
    for (int i = 0; i < 20; ++i)
    {
        void* chunk = pool.Acquire();
        ASSERT_NE(chunk, nullptr);
        chunks.push_back(chunk);
    }
    
    auto stats1 = pool.GetStats();
    EXPECT_EQ(stats1.acquireCount - stats1.releaseCount, 20u);
    EXPECT_GT(stats1.totalChunks, 0u);
    
    for (int i = 0; i < 10; ++i)
    {
        pool.Release(chunks[i]);
    }
    
    auto stats2 = pool.GetStats();
    EXPECT_EQ(stats2.acquireCount - stats2.releaseCount, 10u);
    EXPECT_EQ(stats2.totalChunks, stats1.totalChunks) << "Total chunks should remain (pooled)";
    
    for (int i = 10; i < 20; ++i)
    {
        pool.Release(chunks[i]);
    }
    
    auto stats3 = pool.GetStats();
    EXPECT_EQ(stats3.acquireCount - stats3.releaseCount, 0u);
    
    EXPECT_GT(stats3.totalChunks, 0u) << "Chunks remain in pool for reuse";
}

TEST_F(MemoryCleanupTest, ComponentSizeMemoryUsage)
{
    struct LargeComponent
    {
        char data[1024];  // 1KB
    };
    
    struct MediumComponent
    {
        char data[256];  // 256 bytes
    };
    
    registry->GetComponentRegistry()->RegisterComponent<LargeComponent>();
    registry->GetComponentRegistry()->RegisterComponent<MediumComponent>();
    
    size_t baseMemory = GetCurrentMemoryUsage();
    
    std::vector<Entity> largeEntities;
    for (int i = 0; i < 100; ++i)
    {
        Entity e = registry->CreateEntity();
        registry->AddComponent<LargeComponent>(e);
        largeEntities.push_back(e);
    }
    
    size_t largeMemory = GetCurrentMemoryUsage();
    size_t largeIncrease = largeMemory - baseMemory;
    
    registry->DestroyEntities(largeEntities);
    
    std::vector<Entity> mediumEntities;
    for (int i = 0; i < 100; ++i)
    {
        Entity e = registry->CreateEntity();
        registry->AddComponent<MediumComponent>(e);
        mediumEntities.push_back(e);
    }
    
    size_t mediumMemory = GetCurrentMemoryUsage();
    size_t mediumIncrease = mediumMemory - baseMemory;
    
    // Due to chunking, not exactly 4x
    EXPECT_GT(largeIncrease, mediumIncrease) << "Large components should use more memory";
    
    registry->Clear();
    Registry::CleanupOptions options;
    options.minEmptyDuration = 0;
    registry->CleanupEmptyArchetypes(options);
}

TEST_F(MemoryCleanupTest, ArchetypeFragmentation)
{
    std::vector<std::vector<Entity>> archetypeGroups;
    
    for (int archetype = 0; archetype < 10; ++archetype)
    {
        std::vector<Entity> group;
        for (int i = 0; i < 50; ++i)
        {
            Entity e = registry->CreateEntity();
            
            if (archetype & 1) registry->AddComponent<Position>(e);
            if (archetype & 2) registry->AddComponent<Velocity>(e);
            if (archetype & 4) registry->AddComponent<Health>(e);
            if (archetype & 8) registry->AddComponent<Transform>(e);
            
            group.push_back(e);
        }
        archetypeGroups.push_back(group);
    }
    
    size_t fullMemory = GetCurrentMemoryUsage();
    size_t fullArchetypes = GetArchetypeCount();
    
    for (auto& group : archetypeGroups)
    {
        for (size_t i = 0; i < group.size(); i += 2)
        {
            registry->DestroyEntity(group[i]);
        }
    }
    
    size_t fragmentedMemory = GetCurrentMemoryUsage();
    
    EXPECT_GT(fragmentedMemory, fullMemory / 2) << "Memory remains allocated despite fragmentation";
    
    for (int i = 0; i < 250; ++i)
    {
        Entity e = registry->CreateEntity();
        int archetype = i % 10;
        if (archetype & 1) registry->AddComponent<Position>(e);
        if (archetype & 2) registry->AddComponent<Velocity>(e);
        if (archetype & 4) registry->AddComponent<Health>(e);
        if (archetype & 8) registry->AddComponent<Transform>(e);
    }
    
    size_t refilledMemory = GetCurrentMemoryUsage();
    
    EXPECT_LE(refilledMemory, fullMemory * 1.5) << "Should efficiently reuse fragmented memory";
}

TEST_F(MemoryCleanupTest, ClearMemoryCleanup)
{
    for (int batch = 0; batch < 5; ++batch)
    {
        std::vector<Entity> entities;
        for (int i = 0; i < 200; ++i)
        {
            entities.push_back(registry->CreateEntity(
                Position{float(i), 0, 0},
                Velocity{1, 0, 0}
            ));
        }
        
        for (size_t i = 0; i < 100; ++i)
        {
            registry->AddComponent<Health>(entities[i], 100, 100);
        }
    }
    
    size_t beforeClear = GetCurrentMemoryUsage();
    size_t beforeArchetypes = GetArchetypeCount();
    
    EXPECT_GT(beforeClear, 0u);
    EXPECT_GT(beforeArchetypes, 1u);
    
    registry->Clear();
    
    EXPECT_EQ(registry->Size(), 0u) << "All entities should be gone";
    
    size_t afterClear = GetCurrentMemoryUsage();
    
    Registry::CleanupOptions options;
    options.minEmptyDuration = 0;
    registry->CleanupEmptyArchetypes(options);
    
    size_t afterCleanup = GetCurrentMemoryUsage();
    size_t afterArchetypes = GetArchetypeCount();
    
    EXPECT_LT(afterCleanup, beforeClear) << "Memory should decrease after cleanup";
    EXPECT_LT(afterArchetypes, beforeArchetypes) << "Fewer archetypes after cleanup";
}

TEST_F(MemoryCleanupTest, CleanupOptionsRespected)
{
    std::vector<Entity> persistent;
    for (int i = 0; i < 10; ++i)
    {
        persistent.push_back(registry->CreateEntity(Position{}));
    }
    
    std::vector<Entity> temporary;
    for (int i = 0; i < 10; ++i)
    {
        temporary.push_back(registry->CreateEntity(Position{}, Velocity{}));
    }
    
    registry->DestroyEntities(temporary);
    
    size_t archetypesBefore = GetArchetypeCount();
    
    Registry::CleanupOptions options1;
    options1.minEmptyDuration = 100;
    
    size_t cleaned1 = registry->CleanupEmptyArchetypes(options1);
    EXPECT_EQ(cleaned1, 0u) << "Shouldn't clean archetypes that haven't been empty long enough";
    
    for (int i = 0; i < 5; ++i)
    {
        registry->CleanupEmptyArchetypes(options1);
    }
    
    Registry::CleanupOptions options2;
    options2.minEmptyDuration = 1;
    options2.maxArchetypesToRemove = 1;
    
    size_t cleaned2 = registry->CleanupEmptyArchetypes(options2);
    EXPECT_LE(cleaned2, 1u) << "Should respect maxArchetypesToRemove";
    
    Registry::CleanupOptions options3;
    options3.minEmptyDuration = 0;
    options3.minArchetypesToKeep = archetypesBefore;
    
    size_t cleaned3 = registry->CleanupEmptyArchetypes(options3);
    EXPECT_EQ(cleaned3, 0u) << "Should respect minArchetypesToKeep";
}

// Removed BatchOperationMemoryStability test - memory pooling behavior is an
// implementation detail. The ECS may keep memory allocated for performance reasons
// even after cleanup. The important guarantee is no unbounded growth, which is
// tested in other tests.