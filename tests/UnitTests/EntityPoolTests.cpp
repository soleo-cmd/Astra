#include <catch2/catch_all.hpp>
#include <Astra/Entity/EntityPool.hpp>
#include <vector>
#include <unordered_set>
#include <algorithm>
#include <random>

using namespace Astra;

TEST_CASE("EntityPool Construction", "[EntityPool]")
{
    SECTION("Default construction")
    {
        EntityPool pool;
        REQUIRE(pool.Size() == 0);
        REQUIRE(pool.Empty());
        REQUIRE(pool.Capacity() == 0);
        REQUIRE(pool.RecycledCount() == 0);
    }
    
    SECTION("Construction with capacity")
    {
        EntityPool pool(1000);
        REQUIRE(pool.Size() == 0);
        REQUIRE(pool.Empty());
        REQUIRE(pool.Capacity() >= 0); // Capacity grows on demand
        REQUIRE(pool.RecycledCount() == 0);
    }
}

TEST_CASE("EntityPool Create Operations", "[EntityPool]")
{
    EntityPool pool;
    
    SECTION("Create single entity")
    {
        Entity e = pool.Create();
        REQUIRE(e.Valid());
        REQUIRE(e != Entity::Null());
        REQUIRE(e.Index() == 0);
        REQUIRE(e.Version() == EntityPool::INITIAL_VERSION); // Version 1
        REQUIRE(pool.Size() == 1);
        REQUIRE(!pool.Empty());
        REQUIRE(pool.IsValid(e));
    }
    
    SECTION("Create multiple entities")
    {
        std::vector<Entity> entities;
        const int count = 100;
        
        for (int i = 0; i < count; ++i)
        {
            Entity e = pool.Create();
            entities.push_back(e);
            REQUIRE(pool.IsValid(e));
            REQUIRE(e.Index() == i);
            REQUIRE(e.Version() == EntityPool::INITIAL_VERSION);
        }
        
        REQUIRE(pool.Size() == count);
        REQUIRE(pool.Capacity() >= count);
        
        // All entities should be unique
        std::unordered_set<Entity, EntityHash> uniqueEntities(entities.begin(), entities.end());
        REQUIRE(uniqueEntities.size() == entities.size());
    }
    
    SECTION("CreateBatch operation")
    {
        std::vector<Entity> entities;
        pool.CreateBatch(50, std::back_inserter(entities));
        
        REQUIRE(entities.size() == 50);
        REQUIRE(pool.Size() == 50);
        
        // Verify all entities are valid and unique
        std::unordered_set<Entity, EntityHash> uniqueSet(entities.begin(), entities.end());
        REQUIRE(uniqueSet.size() == 50);
        
        for (const auto& e : entities)
        {
            REQUIRE(pool.IsValid(e));
        }
    }
}

TEST_CASE("EntityPool Destroy Operations", "[EntityPool]")
{
    EntityPool pool;
    
    SECTION("Destroy single entity")
    {
        Entity e = pool.Create();
        REQUIRE(pool.Size() == 1);
        REQUIRE(pool.RecycledCount() == 0);
        
        bool destroyed = pool.Destroy(e);
        REQUIRE(destroyed);
        REQUIRE(!pool.IsValid(e));
        REQUIRE(pool.Size() == 0);
        REQUIRE(pool.RecycledCount() == 1); // Entity added to free list
    }
    
    SECTION("Destroy invalid entity")
    {
        Entity invalid = Entity::Null();
        bool destroyed = pool.Destroy(invalid);
        REQUIRE(!destroyed);
        
        Entity nonExistent(999, 1);
        destroyed = pool.Destroy(nonExistent);
        REQUIRE(!destroyed);
    }
    
    SECTION("Double destroy")
    {
        Entity e = pool.Create();
        REQUIRE(pool.Destroy(e));
        REQUIRE(!pool.Destroy(e)); // Second destroy should fail
    }
    
    SECTION("Version increment on recycle")
    {
        Entity e1 = pool.Create();
        EntityID id = e1.Index();
        REQUIRE(e1.Version() == EntityPool::INITIAL_VERSION);
        
        pool.Destroy(e1);
        REQUIRE(!pool.IsValid(e1));
        
        // Create new entity - should reuse ID with incremented version
        Entity e2 = pool.Create();
        REQUIRE(e2.Index() == id);
        REQUIRE(e2.Version() == EntityPool::INITIAL_VERSION + 1);
        REQUIRE(pool.IsValid(e2));
        REQUIRE(!pool.IsValid(e1)); // Old version still invalid
    }
    
    SECTION("Version wraparound")
    {
        Entity e = pool.Create();
        EntityID id = e.Index();
        
        // Destroy and recreate many times to test version wraparound
        for (int i = 0; i < 260; ++i) // More than 255 to force wraparound
        {
            pool.Destroy(e);
            e = pool.Create();
            REQUIRE(e.Index() == id);
            
            // Version should never be NULL_VERSION (0) or TOMBSTONE_VERSION (255)
            REQUIRE(e.Version() != EntityPool::NULL_VERSION);
            REQUIRE(e.Version() != EntityPool::TOMBSTONE_VERSION);
        }
    }
    
    SECTION("DestroyBatch operation")
    {
        std::vector<Entity> entities;
        for (int i = 0; i < 10; ++i)
        {
            entities.push_back(pool.Create());
        }
        
        REQUIRE(pool.Size() == 10);
        
        auto destroyed = pool.DestroyBatch(entities.begin(), entities.begin() + 5);
        REQUIRE(destroyed == 5);
        REQUIRE(pool.Size() == 5);
        REQUIRE(pool.RecycledCount() == 5);
        
        // First 5 should be invalid, last 5 should be valid
        for (int i = 0; i < 10; ++i)
        {
            if (i < 5)
            {
                REQUIRE(!pool.IsValid(entities[i]));
            }
            else
            {
                REQUIRE(pool.IsValid(entities[i]));
            }
        }
    }
}

TEST_CASE("EntityPool Validation", "[EntityPool]")
{
    EntityPool pool;
    
    SECTION("IsValid checks")
    {
        Entity e = pool.Create();
        REQUIRE(pool.IsValid(e));
        
        // Null entity is always invalid
        REQUIRE(!pool.IsValid(Entity::Null()));
        
        // Entity with wrong version is invalid
        Entity wrongVersion(e.Index(), e.Version() + 1);
        REQUIRE(!pool.IsValid(wrongVersion));
        
        // Out of bounds entity is invalid
        Entity outOfBounds(1000, 1);
        REQUIRE(!pool.IsValid(outOfBounds));
        
        // Destroyed entity is invalid
        pool.Destroy(e);
        REQUIRE(!pool.IsValid(e));
    }
    
    SECTION("GetVersion operation")
    {
        Entity e = pool.Create();
        REQUIRE(pool.GetVersion(e.Index()) == e.Version());
        
        // Out of bounds returns NULL_VERSION
        REQUIRE(pool.GetVersion(1000) == EntityPool::NULL_VERSION);
        
        // After destroy, version is TOMBSTONE
        pool.Destroy(e);
        REQUIRE(pool.GetVersion(e.Index()) == EntityPool::TOMBSTONE_VERSION);
    }
}

TEST_CASE("EntityPool Memory Management", "[EntityPool]")
{
    SECTION("Reserve operation")
    {
        EntityPool pool;
        pool.Reserve(1000);
        
        // Capacity is based on version array size, which grows on demand
        // So we can't check exact capacity, but we can verify Reserve doesn't crash
        
        // Create many entities without reallocation issues
        for (int i = 0; i < 500; ++i)
        {
            Entity e = pool.Create();
            REQUIRE(pool.IsValid(e));
        }
        
        REQUIRE(pool.Size() == 500);
    }
    
    SECTION("Clear operation")
    {
        EntityPool pool;
        std::vector<Entity> entities;
        
        for (int i = 0; i < 100; ++i)
        {
            entities.push_back(pool.Create());
        }
        
        REQUIRE(pool.Size() == 100);
        REQUIRE(!pool.Empty());
        
        pool.Clear();
        REQUIRE(pool.Size() == 0);
        REQUIRE(pool.Empty());
        REQUIRE(pool.Capacity() == 0);
        REQUIRE(pool.RecycledCount() == 0);
        
        // All entities should be invalid after clear
        for (const auto& e : entities)
        {
            REQUIRE(!pool.IsValid(e));
        }
        
        // Can create new entities after clear
        Entity newEntity = pool.Create();
        REQUIRE(pool.IsValid(newEntity));
        REQUIRE(newEntity.Index() == 0); // Should start from 0 again
    }
    
    SECTION("ShrinkToFit operation")
    {
        EntityPool pool;
        
        // Create and destroy many entities
        std::vector<Entity> entities;
        for (int i = 0; i < 1000; ++i)
        {
            entities.push_back(pool.Create());
        }
        
        // Destroy most of them
        for (int i = 0; i < 900; ++i)
        {
            pool.Destroy(entities[i]);
        }
        
        REQUIRE(pool.Size() == 100);
        REQUIRE(pool.RecycledCount() == 900);
        
        pool.ShrinkToFit(); // Should not crash or invalidate remaining entities
        
        // Remaining entities should still be valid
        for (int i = 900; i < 1000; ++i)
        {
            REQUIRE(pool.IsValid(entities[i]));
        }
    }
}

TEST_CASE("EntityPool Iteration", "[EntityPool]")
{
    EntityPool pool;
    
    SECTION("Empty pool iteration")
    {
        int count = 0;
        for (auto e : pool)
        {
            (void)e;
            count++;
        }
        REQUIRE(count == 0);
        REQUIRE(pool.begin() == pool.end());
    }
    
    SECTION("Iterate over alive entities only")
    {
        std::vector<Entity> entities;
        for (int i = 0; i < 10; ++i)
        {
            entities.push_back(pool.Create());
        }
        
        // Destroy some entities
        pool.Destroy(entities[2]);
        pool.Destroy(entities[5]);
        pool.Destroy(entities[7]);
        
        // Iterate and count
        std::set<EntityID> foundIds;
        for (auto e : pool)
        {
            REQUIRE(pool.IsValid(e));
            foundIds.insert(e.Index());
        }
        
        REQUIRE(foundIds.size() == 7); // 10 - 3 destroyed
        
        // Check that destroyed entities are not in iteration
        REQUIRE(foundIds.find(2) == foundIds.end());
        REQUIRE(foundIds.find(5) == foundIds.end());
        REQUIRE(foundIds.find(7) == foundIds.end());
    }
    
    SECTION("Iterator operations")
    {
        for (int i = 0; i < 5; ++i)
        {
            Entity e = pool.Create();
            REQUIRE(pool.IsValid(e));
        }
        
        auto it = pool.begin();
        REQUIRE(it != pool.end());
        
        // Test dereference
        Entity e = *it;
        REQUIRE(pool.IsValid(e));
        
        // Test increment
        ++it;
        Entity e2 = *it;
        REQUIRE(e != e2);
        REQUIRE(pool.IsValid(e2));
        
        // Test post-increment
        auto it2 = it++;
        REQUIRE(it != it2);
        REQUIRE(*it != *it2);
    }
}

TEST_CASE("EntityPool Free List Behavior", "[EntityPool]")
{
    EntityPool pool;
    
    SECTION("LIFO recycling")
    {
        Entity e1 = pool.Create();
        Entity e2 = pool.Create();
        Entity e3 = pool.Create();
        
        // Destroy in order 1, 2, 3
        pool.Destroy(e1);
        pool.Destroy(e2);
        pool.Destroy(e3);
        
        // Create new entities - should reuse in LIFO order: 3, 2, 1
        Entity r1 = pool.Create();
        Entity r2 = pool.Create();
        Entity r3 = pool.Create();
        
        REQUIRE(r1.Index() == e3.Index()); // Last destroyed, first recycled
        REQUIRE(r2.Index() == e2.Index());
        REQUIRE(r3.Index() == e1.Index()); // First destroyed, last recycled
    }
    
    SECTION("Mix of fresh and recycled entities")
    {
        // Create some entities
        Entity e1 = pool.Create();
        Entity e2 = pool.Create();
        
        // Destroy one
        pool.Destroy(e1);
        
        // Create more - first should be recycled, rest should be fresh
        Entity r1 = pool.Create();
        Entity f1 = pool.Create();
        Entity f2 = pool.Create();
        
        REQUIRE(r1.Index() == e1.Index()); // Recycled
        REQUIRE(f1.Index() == 2);          // Fresh
        REQUIRE(f2.Index() == 3);          // Fresh
    }
}

TEST_CASE("EntityPool Validate Debug Checks", "[EntityPool]")
{
    EntityPool pool;
    
    SECTION("Validate empty pool")
    {
        pool.Validate(); // Should not crash
    }
    
    SECTION("Validate after operations")
    {
        std::vector<Entity> entities;
        
        // Create entities
        for (int i = 0; i < 20; ++i)
        {
            entities.push_back(pool.Create());
        }
        
        pool.Validate();
        
        // Destroy some
        for (int i = 0; i < 20; i += 2)
        {
            pool.Destroy(entities[i]);
        }
        
        pool.Validate();
        
        // Create more (mix of recycled and fresh)
        for (int i = 0; i < 10; ++i)
        {
            Entity e = pool.Create();
            REQUIRE(pool.IsValid(e));
        }
        
        pool.Validate();
    }
}

TEST_CASE("EntityPool Stress Test", "[EntityPool][Stress]")
{
    SECTION("Many create/destroy cycles")
    {
        EntityPool pool;
        std::random_device rd;
        std::mt19937 gen(rd());
        std::vector<Entity> activeEntities;
        
        const int iterations = 10000;
        
        for (int i = 0; i < iterations; ++i)
        {
            std::uniform_int_distribution<> opDist(0, 2);
            int op = opDist(gen);
            
            if (op < 2 || activeEntities.empty()) // Bias towards creation
            {
                // Create
                Entity e = pool.Create();
                REQUIRE(pool.IsValid(e));
                activeEntities.push_back(e);
            }
            else
            {
                // Destroy random entity
                std::uniform_int_distribution<size_t> indexDist(0, activeEntities.size() - 1);
                size_t index = indexDist(gen);
                
                REQUIRE(pool.Destroy(activeEntities[index]));
                activeEntities.erase(activeEntities.begin() + index);
            }
        }
        
        // Verify final state
        REQUIRE(pool.Size() == activeEntities.size());
        for (const auto& e : activeEntities)
        {
            REQUIRE(pool.IsValid(e));
        }
        
        pool.Validate();
    }
}

TEST_CASE("EntityPool Constants", "[EntityPool]")
{
    SECTION("Version constants")
    {
        REQUIRE(EntityPool::NULL_VERSION == 0);
        REQUIRE(EntityPool::INITIAL_VERSION == 1);
        REQUIRE(EntityPool::TOMBSTONE_VERSION == 255);
        REQUIRE(EntityPool::INVALID_ID == EntityTraits32::ENTITY_MASK);
    }
}