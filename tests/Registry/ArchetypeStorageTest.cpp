#include <algorithm>
#include <gtest/gtest.h>
#include <numeric>
#include <random>
#include <unordered_set>
#include <vector>
#include "../TestComponents.hpp"
#include "Astra/Component/ComponentRegistry.hpp"
#include "Astra/Archetype/ArchetypeStorage.hpp"

class ArchetypeStorageTest : public ::testing::Test
{
protected:
    std::unique_ptr<Astra::ArchetypeStorage> storage;
    std::vector<Astra::Entity> testEntities;
    
    void SetUp() override 
    {
        storage = std::make_unique<Astra::ArchetypeStorage>();
        
        // Register test components
        using namespace Astra::Test;
        auto registry = storage->GetComponentRegistry();
        registry->RegisterComponents<Position, Velocity, Health, Transform, Name, Physics, Player, Enemy>();
        
        // Create some test entities
        for (int i = 0; i < 100; ++i)
        {
            testEntities.emplace_back(i, 1);
        }
    }
    
    void TearDown() override 
    {
        storage.reset();
        testEntities.clear();
    }
};

// Test basic entity addition and removal
TEST_F(ArchetypeStorageTest, BasicEntityOperations)
{
    Astra::Entity entity(1, 1);
    
    // Add entity to storage
    storage->AddEntity(entity);
    
    // Entity should exist in root archetype (no components)
    EXPECT_EQ(storage->GetArchetypeCount(), 1u); // Root archetype
    
    // Remove entity
    storage->RemoveEntity(entity);
    
    // Storage should still have root archetype
    EXPECT_EQ(storage->GetArchetypeCount(), 1u);
}

// Test adding components to entities
TEST_F(ArchetypeStorageTest, AddComponentToEntity)
{
    using namespace Astra::Test;
    
    Astra::Entity entity(1, 1);
    storage->AddEntity(entity);
    
    // Add Position component
    Position* pos = storage->AddComponent<Position>(entity, 10.0f, 20.0f, 30.0f);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->x, 10.0f);
    EXPECT_EQ(pos->y, 20.0f);
    EXPECT_EQ(pos->z, 30.0f);
    
    // Should have created new archetype
    EXPECT_EQ(storage->GetArchetypeCount(), 2u); // Root + Position archetype
    
    // Get component back
    Position* retrieved = storage->GetComponent<Position>(entity);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->x, 10.0f);
    
    // Modify component
    retrieved->x = 100.0f;
    
    // Get again and verify modification
    Position* modified = storage->GetComponent<Position>(entity);
    EXPECT_EQ(modified->x, 100.0f);
}

// Test removing components from entities
TEST_F(ArchetypeStorageTest, RemoveComponentFromEntity)
{
    using namespace Astra::Test;
    
    Astra::Entity entity(1, 1);
    storage->AddEntity(entity);
    
    // Add components
    storage->AddComponent<Position>(entity, 1.0f, 2.0f, 3.0f);
    storage->AddComponent<Velocity>(entity, 10.0f, 0.0f, 0.0f);
    
    EXPECT_EQ(storage->GetArchetypeCount(), 3u); // Root, Position, Position+Velocity
    
    // Verify both components exist
    EXPECT_NE(storage->GetComponent<Position>(entity), nullptr);
    EXPECT_NE(storage->GetComponent<Velocity>(entity), nullptr);
    
    // Remove Velocity component
    bool removed = storage->RemoveComponent<Velocity>(entity);
    EXPECT_TRUE(removed);
    
    // Position should still exist, Velocity should not
    EXPECT_NE(storage->GetComponent<Position>(entity), nullptr);
    EXPECT_EQ(storage->GetComponent<Velocity>(entity), nullptr);
    
    // Try removing non-existent component
    bool removedAgain = storage->RemoveComponent<Velocity>(entity);
    EXPECT_FALSE(removedAgain);
}

// Test archetype transitions
TEST_F(ArchetypeStorageTest, ArchetypeTransitions)
{
    using namespace Astra::Test;
    
    Astra::Entity entity(1, 1);
    storage->AddEntity(entity);
    
    // Track archetype count as we add components
    EXPECT_EQ(storage->GetArchetypeCount(), 1u); // Root
    
    // Add Position - creates Position archetype
    storage->AddComponent<Position>(entity);
    EXPECT_EQ(storage->GetArchetypeCount(), 2u);
    
    // Add Velocity - creates Position+Velocity archetype
    storage->AddComponent<Velocity>(entity);
    EXPECT_EQ(storage->GetArchetypeCount(), 3u);
    
    // Add Health - creates Position+Velocity+Health archetype
    storage->AddComponent<Health>(entity);
    EXPECT_EQ(storage->GetArchetypeCount(), 4u);
    
    // Remove Velocity - reuses Position+Health archetype or creates it
    storage->RemoveComponent<Velocity>(entity);
    EXPECT_EQ(storage->GetArchetypeCount(), 5u); // Added Position+Health
    
    // Add Velocity back - reuses Position+Velocity+Health archetype
    storage->AddComponent<Velocity>(entity);
    EXPECT_EQ(storage->GetArchetypeCount(), 5u); // No new archetype
}

// Test batch entity addition
TEST_F(ArchetypeStorageTest, BatchEntityAddition)
{
    using namespace Astra::Test;
    
    std::vector<Astra::Entity> entities;
    for (int i = 0; i < 50; ++i)
    {
        entities.emplace_back(i, 1);
    }
    
    // Add entities with components in batch
    storage->AddEntities<Position, Velocity>(
        entities,
        [](size_t i) {
            return std::make_tuple(
                Position{float(i), float(i * 2), float(i * 3)},
                Velocity{float(i * 10), 0.0f, 0.0f}
            );
        }
    );
    
    // Verify all entities have the components
    for (size_t i = 0; i < entities.size(); ++i)
    {
        Position* pos = storage->GetComponent<Position>(entities[i]);
        ASSERT_NE(pos, nullptr);
        EXPECT_EQ(pos->x, float(i));
        EXPECT_EQ(pos->y, float(i * 2));
        EXPECT_EQ(pos->z, float(i * 3));
        
        Velocity* vel = storage->GetComponent<Velocity>(entities[i]);
        ASSERT_NE(vel, nullptr);
        EXPECT_EQ(vel->dx, float(i * 10));
    }
}

// Test batch entity removal
TEST_F(ArchetypeStorageTest, BatchEntityRemoval)
{
    using namespace Astra::Test;
    
    // Add entities
    for (const auto& entity : testEntities)
    {
        storage->AddEntity(entity);
        storage->AddComponent<Position>(entity);
    }
    
    // Remove first 50 entities in batch
    std::vector<Astra::Entity> toRemove(testEntities.begin(), testEntities.begin() + 50);
    storage->RemoveEntities(toRemove);
    
    // Verify removed entities don't have components
    for (const auto& entity : toRemove)
    {
        EXPECT_EQ(storage->GetComponent<Position>(entity), nullptr);
    }
    
    // Verify remaining entities still have components
    for (size_t i = 50; i < testEntities.size(); ++i)
    {
        EXPECT_NE(storage->GetComponent<Position>(testEntities[i]), nullptr);
    }
}

// Test batch component addition
TEST_F(ArchetypeStorageTest, BatchComponentAddition)
{
    using namespace Astra::Test;
    
    // Add entities with Position
    for (const auto& entity : testEntities)
    {
        storage->AddEntity(entity);
        storage->AddComponent<Position>(entity);
    }
    
    // Add Velocity to all entities in batch
    storage->AddComponents<Velocity>(testEntities, 1.0f, 2.0f, 3.0f);
    
    // Verify all entities have both components
    for (const auto& entity : testEntities)
    {
        EXPECT_NE(storage->GetComponent<Position>(entity), nullptr);
        Velocity* vel = storage->GetComponent<Velocity>(entity);
        ASSERT_NE(vel, nullptr);
        EXPECT_EQ(vel->dx, 1.0f);
        EXPECT_EQ(vel->dy, 2.0f);
        EXPECT_EQ(vel->dz, 3.0f);
    }
}

// Test archetype querying
TEST_F(ArchetypeStorageTest, ArchetypeQuerying)
{
    using namespace Astra::Test;
    
    // Create entities with different component combinations
    Astra::Entity e1(1, 1), e2(2, 1), e3(3, 1), e4(4, 1);
    
    storage->AddEntity(e1);
    storage->AddComponent<Position>(e1);
    
    storage->AddEntity(e2);
    storage->AddComponent<Position>(e2);
    storage->AddComponent<Velocity>(e2);
    
    storage->AddEntity(e3);
    storage->AddComponent<Position>(e3);
    storage->AddComponent<Velocity>(e3);
    storage->AddComponent<Health>(e3);
    
    storage->AddEntity(e4);
    storage->AddComponent<Health>(e4);
    
    // Query archetypes with Position
    auto positionMask = Astra::MakeComponentMask<Position>();
    auto positionArchetypes = storage->QueryArchetypes(positionMask);
    
    size_t posCount = 0;
    for (auto* archetype : positionArchetypes)
    {
        posCount++;
    }
    EXPECT_EQ(posCount, 3u); // 3 archetypes have Position
    
    // Query archetypes with Position and Velocity
    auto posVelMask = Astra::MakeComponentMask<Position, Velocity>();
    auto posVelArchetypes = storage->QueryArchetypes(posVelMask);
    
    size_t posVelCount = 0;
    for (auto* archetype : posVelArchetypes)
    {
        posVelCount++;
    }
    EXPECT_EQ(posVelCount, 2u); // 2 archetypes have both Position and Velocity
}

// Test archetype cleanup
TEST_F(ArchetypeStorageTest, ArchetypeCleanup)
{
    using namespace Astra::Test;
    
    // Create and remove entities to create empty archetypes
    for (int i = 0; i < 10; ++i)
    {
        Astra::Entity entity(i, 1);
        storage->AddEntity(entity);
        storage->AddComponent<Position>(entity);
        if (i % 2 == 0)
        {
            storage->AddComponent<Velocity>(entity);
        }
        if (i % 3 == 0)
        {
            storage->AddComponent<Health>(entity);
        }
    }
    
    size_t archetypeCount = storage->GetArchetypeCount();
    EXPECT_GT(archetypeCount, 1u);
    
    // Remove all entities
    for (int i = 0; i < 10; ++i)
    {
        storage->RemoveEntity(Astra::Entity(i, 1));
    }
    
    // Update metrics
    storage->UpdateArchetypeMetrics();
    
    // Cleanup empty archetypes
    Astra::ArchetypeStorage::CleanupOptions options;
    options.minEmptyDuration = 1;
    options.minArchetypesToKeep = 1; // Keep root
    
    size_t removed = storage->CleanupEmptyArchetypes(options);
    EXPECT_GT(removed, 0u);
    
    // Should have fewer archetypes now
    EXPECT_LT(storage->GetArchetypeCount(), archetypeCount);
}

// Test archetype statistics
TEST_F(ArchetypeStorageTest, ArchetypeStatistics)
{
    using namespace Astra::Test;
    
    // Create entities with various components
    for (int i = 0; i < 20; ++i)
    {
        Astra::Entity entity(i, 1);
        storage->AddEntity(entity);
        
        if (i < 10)
        {
            storage->AddComponent<Position>(entity);
        }
        if (i >= 5 && i < 15)
        {
            storage->AddComponent<Velocity>(entity);
        }
    }
    
    // Get archetype stats
    auto stats = storage->GetArchetypeStats();
    EXPECT_GT(stats.size(), 0u);
    
    // Verify stats contain valid information
    for (const auto& info : stats)
    {
        EXPECT_NE(info.archetype, nullptr);
        EXPECT_GE(info.currentEntityCount, 0u);
        EXPECT_GE(info.peakEntityCount, info.currentEntityCount);
        EXPECT_GT(info.approximateMemoryUsage, 0u);
    }
    
    // Get memory usage
    size_t memUsage = storage->GetArchetypeMemoryUsage();
    EXPECT_GT(memUsage, 0u);
    
    // Get pool stats
    auto poolStats = storage->GetPoolStats();
    EXPECT_GE(poolStats.totalChunks, 0u);
}

// Test component registry sharing
TEST_F(ArchetypeStorageTest, ComponentRegistrySharing)
{
    using namespace Astra::Test;
    
    // Get registry from first storage
    auto sharedRegistry = storage->GetComponentRegistry();
    
    // Create second storage with shared registry
    Astra::ArchetypeStorage storage2(sharedRegistry);
    
    // Components registered in first storage should work in second
    Astra::Entity entity(100, 1);
    storage2.AddEntity(entity);
    
    // Should be able to add component without registering again
    Position* pos = storage2.AddComponent<Position>(entity, 1.0f, 2.0f, 3.0f);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->x, 1.0f);
}

// Test edge caching for archetype transitions
TEST_F(ArchetypeStorageTest, EdgeCaching)
{
    using namespace Astra::Test;
    
    // Create multiple entities that will follow same transition path
    for (int i = 0; i < 10; ++i)
    {
        Astra::Entity entity(i, 1);
        storage->AddEntity(entity);
        
        // Same sequence of component additions
        storage->AddComponent<Position>(entity);
        storage->AddComponent<Velocity>(entity);
        storage->AddComponent<Health>(entity);
        
        // Same sequence of component removals
        storage->RemoveComponent<Velocity>(entity);
        storage->AddComponent<Velocity>(entity);
    }
    
    // Should reuse archetypes due to edge caching
    // We expect: Root, Position, Position+Velocity, Position+Velocity+Health, Position+Health
    EXPECT_EQ(storage->GetArchetypeCount(), 5u);
}

// Test move-only component handling
TEST_F(ArchetypeStorageTest, MoveOnlyComponents)
{
    using namespace Astra::Test;
    
    Astra::Entity entity(1, 1);
    storage->AddEntity(entity);
    
    // Add move-only component
    auto* resource = storage->AddComponent<Resource>(entity, 42);
    ASSERT_NE(resource, nullptr);
    EXPECT_EQ(*resource->data, 42);
    
    // Should be able to retrieve it
    auto* retrieved = storage->GetComponent<Resource>(entity);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(*retrieved->data, 42);
    
    // Add another component to trigger archetype transition
    storage->AddComponent<Position>(entity);
    
    // Resource should still be accessible after move
    auto* afterMove = storage->GetComponent<Resource>(entity);
    ASSERT_NE(afterMove, nullptr);
    EXPECT_EQ(*afterMove->data, 42);
}

// Test entity location tracking
TEST_F(ArchetypeStorageTest, EntityLocationTracking)
{
    using namespace Astra::Test;
    
    // Create archetype directly
    auto* archetype = storage->GetOrCreateArchetype<Position, Velocity>();
    ASSERT_NE(archetype, nullptr);
    
    // Add entity to archetype and set location
    Astra::Entity entity(1, 1);
    auto packedLoc = archetype->AddEntity(entity);
    storage->SetEntityLocation(entity, archetype, packedLoc);
    
    // Should be able to get components
    archetype->SetComponent(packedLoc, Position{1.0f, 2.0f, 3.0f});
    
    Position* pos = storage->GetComponent<Position>(entity);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->x, 1.0f);
}

// Test stress with many archetypes
TEST_F(ArchetypeStorageTest, StressManyArchetypes)
{
    using namespace Astra::Test;
    
    // Create entities with many different component combinations
    // This will create 2^3 = 8 different archetypes (including root)
    for (int i = 0; i < 100; ++i)
    {
        Astra::Entity entity(i, 1);
        storage->AddEntity(entity);
        
        if (i & 1) storage->AddComponent<Position>(entity);
        if (i & 2) storage->AddComponent<Velocity>(entity);
        if (i & 4) storage->AddComponent<Health>(entity);
    }
    
    // Should have created multiple archetypes
    EXPECT_GE(storage->GetArchetypeCount(), 8u);
    
    // All entities should still be accessible
    for (int i = 0; i < 100; ++i)
    {
        Astra::Entity entity(i, 1);
        
        if (i & 1)
        {
            EXPECT_NE(storage->GetComponent<Position>(entity), nullptr);
        }
        else
        {
            EXPECT_EQ(storage->GetComponent<Position>(entity), nullptr);
        }
        
        if (i & 2)
        {
            EXPECT_NE(storage->GetComponent<Velocity>(entity), nullptr);
        }
        else
        {
            EXPECT_EQ(storage->GetComponent<Velocity>(entity), nullptr);
        }
    }
}

// Test component data preservation during transitions
TEST_F(ArchetypeStorageTest, ComponentDataPreservation)
{
    using namespace Astra::Test;
    
    Astra::Entity entity(1, 1);
    storage->AddEntity(entity);
    
    // Add Position with specific values
    storage->AddComponent<Position>(entity, 10.0f, 20.0f, 30.0f);
    
    // Add Health with specific values
    storage->AddComponent<Health>(entity, 75, 100);
    
    // Add Velocity (triggers archetype transition)
    storage->AddComponent<Velocity>(entity, 5.0f, 10.0f, 15.0f);
    
    // Verify all components retained their values after transitions
    Position* pos = storage->GetComponent<Position>(entity);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->x, 10.0f);
    EXPECT_EQ(pos->y, 20.0f);
    EXPECT_EQ(pos->z, 30.0f);
    
    Health* health = storage->GetComponent<Health>(entity);
    ASSERT_NE(health, nullptr);
    EXPECT_EQ(health->current, 75);
    EXPECT_EQ(health->max, 100);
    
    Velocity* vel = storage->GetComponent<Velocity>(entity);
    ASSERT_NE(vel, nullptr);
    EXPECT_EQ(vel->dx, 5.0f);
    EXPECT_EQ(vel->dy, 10.0f);
    EXPECT_EQ(vel->dz, 15.0f);
    
    // Remove Velocity and verify other components still intact
    storage->RemoveComponent<Velocity>(entity);
    
    pos = storage->GetComponent<Position>(entity);
    ASSERT_NE(pos, nullptr);
    EXPECT_EQ(pos->x, 10.0f);
    
    health = storage->GetComponent<Health>(entity);
    ASSERT_NE(health, nullptr);
    EXPECT_EQ(health->current, 75);
}

// Test invalid entity operations
TEST_F(ArchetypeStorageTest, InvalidEntityOperations)
{
    using namespace Astra::Test;
    
    Astra::Entity invalidEntity(9999, 1);
    
    // Try to get component from non-existent entity
    EXPECT_EQ(storage->GetComponent<Position>(invalidEntity), nullptr);
    
    // Try to add component to non-existent entity
    EXPECT_EQ(storage->AddComponent<Position>(invalidEntity), nullptr);
    
    // Try to remove component from non-existent entity
    EXPECT_FALSE(storage->RemoveComponent<Position>(invalidEntity));
    
    // Try to remove non-existent entity
    storage->RemoveEntity(invalidEntity); // Should not crash
}

// Test duplicate component addition
TEST_F(ArchetypeStorageTest, DuplicateComponentAddition)
{
    using namespace Astra::Test;
    
    Astra::Entity entity(1, 1);
    storage->AddEntity(entity);
    
    // Add Position component
    Position* pos1 = storage->AddComponent<Position>(entity, 1.0f, 2.0f, 3.0f);
    ASSERT_NE(pos1, nullptr);
    
    // Try to add Position again - should return nullptr
    Position* pos2 = storage->AddComponent<Position>(entity, 4.0f, 5.0f, 6.0f);
    EXPECT_EQ(pos2, nullptr);
    
    // Original component should be unchanged
    Position* original = storage->GetComponent<Position>(entity);
    ASSERT_NE(original, nullptr);
    EXPECT_EQ(original->x, 1.0f);
    EXPECT_EQ(original->y, 2.0f);
    EXPECT_EQ(original->z, 3.0f);
}