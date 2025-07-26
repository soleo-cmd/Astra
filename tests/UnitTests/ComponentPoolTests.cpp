#include <Astra/Component/ComponentPool.hpp>
#include <Astra/Entity/Entity.hpp>
#include <catch2/catch_all.hpp>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

using namespace Astra;

// Test components
struct Position 
{
    float x, y, z;
    
    Position() = default;
    Position(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    
    bool operator==(const Position& other) const 
    {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct Velocity 
{
    float dx, dy, dz;
    
    Velocity() = default;
    Velocity(float dx_, float dy_, float dz_) : dx(dx_), dy(dy_), dz(dz_) {}
};

struct Name 
{
    std::string value;
    
    Name() = default;
    explicit Name(std::string v) : value(std::move(v)) {}
};

// Components satisfy the Component concept automatically
// No explicit registration needed with C++20 concepts

TEST_CASE("ComponentPool Construction", "[ComponentPool]")
{
    SECTION("Default construction")
    {
        ComponentPool<Position> pool;
        REQUIRE(pool.Empty());
        REQUIRE(pool.Size() == 0);
        REQUIRE(pool.GetEntityCount() == 0);
        REQUIRE(pool.GetComponentID() == TypeID<Position>::Value());
        REQUIRE(pool.GetComponentSize() == sizeof(Position));
        REQUIRE(pool.GetComponentName() == TypeName_v<Position>);
    }
}

TEST_CASE("ComponentPool Add Operations", "[ComponentPool]")
{
    ComponentPool<Position> pool;
    Entity e1(100, 1);
    Entity e2(200, 2);
    
    SECTION("Add component")
    {
        auto* pos = pool.Add(e1, 1.0f, 2.0f, 3.0f);
        REQUIRE(pos != nullptr);
        REQUIRE(pos->x == 1.0f);
        REQUIRE(pos->y == 2.0f);
        REQUIRE(pos->z == 3.0f);
        REQUIRE(pool.Size() == 1);
        REQUIRE(pool.Contains(e1));
    }
    
    SECTION("Add duplicate component")
    {
        auto* pos1 = pool.Add(e1, 1.0f, 2.0f, 3.0f);
        REQUIRE(pos1 != nullptr);
        
        // Adding again should return nullptr
        auto* pos2 = pool.Add(e1, 4.0f, 5.0f, 6.0f);
        REQUIRE(pos2 == nullptr);
        REQUIRE(pool.Size() == 1);
        
        // Original component unchanged
        REQUIRE(pos1->x == 1.0f);
    }
    
    SECTION("Set component (replace)")
    {
        pool.Add(e1, 1.0f, 2.0f, 3.0f);
        
        auto* pos = pool.Set(e1, 4.0f, 5.0f, 6.0f);
        REQUIRE(pos != nullptr);
        REQUIRE(pos->x == 4.0f);
        REQUIRE(pos->y == 5.0f);
        REQUIRE(pos->z == 6.0f);
        REQUIRE(pool.Size() == 1);
    }
    
    SECTION("Set component (add new)")
    {
        auto* pos = pool.Set(e1, 1.0f, 2.0f, 3.0f);
        REQUIRE(pos != nullptr);
        REQUIRE(pool.Size() == 1);
        REQUIRE(pool.Contains(e1));
    }
}

TEST_CASE("ComponentPool Get Operations", "[ComponentPool]")
{
    ComponentPool<Position> pool;
    Entity e1(100, 1);
    Entity e2(200, 2);
    
    pool.Add(e1, 1.0f, 2.0f, 3.0f);
    
    SECTION("TryGet existing component")
    {
        auto* pos = pool.TryGet(e1);
        REQUIRE(pos != nullptr);
        REQUIRE(pos->x == 1.0f);
        REQUIRE(pos->y == 2.0f);
        REQUIRE(pos->z == 3.0f);
        
        // Const version
        const ComponentPool<Position>& constPool = pool;
        const auto* constPos = constPool.TryGet(e1);
        REQUIRE(constPos != nullptr);
        REQUIRE(constPos == pos);
    }
    
    SECTION("TryGet non-existent component")
    {
        auto* pos = pool.TryGet(e2);
        REQUIRE(pos == nullptr);
    }
    
    SECTION("Get existing component")
    {
        Position& pos = pool.Get(e1);
        REQUIRE(pos.x == 1.0f);
        
        // Modify through reference
        pos.x = 10.0f;
        REQUIRE(pool.Get(e1).x == 10.0f);
    }
    
    SECTION("Get const component")
    {
        const ComponentPool<Position>& constPool = pool;
        const Position& pos = constPool.Get(e1);
        REQUIRE(pos.x == 1.0f);
    }
}

TEST_CASE("ComponentPool Remove Operations", "[ComponentPool]")
{
    ComponentPool<Position> pool;
    Entity e1(100, 1);
    Entity e2(200, 2);
    
    SECTION("Remove existing component")
    {
        pool.Add(e1, 1.0f, 2.0f, 3.0f);
        pool.Add(e2, 4.0f, 5.0f, 6.0f);
        
        REQUIRE(pool.Size() == 2);
        
        bool removed = pool.Remove(e1);
        REQUIRE(removed);
        REQUIRE(!pool.Contains(e1));
        REQUIRE(pool.Size() == 1);
        REQUIRE(pool.Contains(e2));
    }
    
    SECTION("Remove non-existent component")
    {
        bool removed = pool.Remove(e1);
        REQUIRE(!removed);
        REQUIRE(pool.Size() == 0);
    }
    
    SECTION("Clear all components")
    {
        pool.Add(e1, 1.0f, 2.0f, 3.0f);
        pool.Add(e2, 4.0f, 5.0f, 6.0f);
        
        REQUIRE(pool.Size() == 2);
        
        pool.Clear();
        REQUIRE(pool.Empty());
        REQUIRE(pool.Size() == 0);
        REQUIRE(!pool.Contains(e1));
        REQUIRE(!pool.Contains(e2));
    }
}

TEST_CASE("ComponentPool Iteration", "[ComponentPool]")
{
    ComponentPool<Position> pool;
    std::vector<Entity> entities;
    
    // Add components
    for (int i = 0; i < 10; ++i)
    {
        Entity e(i, 1);
        entities.push_back(e);
        pool.Add(e, float(i), float(i * 2), float(i * 3));
    }
    
    SECTION("Range-based for loop")
    {
        int count = 0;
        for (const auto& [entity, pos] : pool)
        {
            REQUIRE(pool.Contains(entity));
            count++;
        }
        REQUIRE(count == 10);
    }
    
    SECTION("Iterator operations")
    {
        auto it = pool.begin();
        REQUIRE(it != pool.end());
        
        // Access pair
        Entity e = it->first;
        Position& pos = it->second;
        REQUIRE(pool.Contains(e));
        
        // Increment
        ++it;
        if (it != pool.end())
        {
            REQUIRE(it->first != e);
        }
    }
    
    SECTION("ForEachGroup operation")
    {
        int count = 0;
        pool.ForEachGroup([&count](Entity e, Position& pos)
        {
            count++;
        });
        REQUIRE(count == 10);
    }
}

TEST_CASE("ComponentPool Entity Cache", "[ComponentPool]")
{
    ComponentPool<Position> pool;
    
    SECTION("GetEntities updates cache")
    {
        Entity e1(100, 1);
        Entity e2(200, 2);
        Entity e3(300, 3);
        
        pool.Add(e1, 1.0f, 0.0f, 0.0f);
        pool.Add(e2, 2.0f, 0.0f, 0.0f);
        pool.Add(e3, 3.0f, 0.0f, 0.0f);
        
        const Entity* entities = pool.GetEntities();
        REQUIRE(entities != nullptr);
        REQUIRE(pool.GetEntityCount() == 3);
        
        // Verify all entities are in cache
        std::unordered_set<Entity, EntityHash> entitySet;
        for (size_t i = 0; i < pool.GetEntityCount(); ++i)
        {
            entitySet.insert(entities[i]);
        }
        
        REQUIRE(entitySet.count(e1) == 1);
        REQUIRE(entitySet.count(e2) == 1);
        REQUIRE(entitySet.count(e3) == 1);
    }
    
    SECTION("Cache invalidation on modification")
    {
        Entity e1(100, 1);
        pool.Add(e1, 1.0f, 0.0f, 0.0f);
        
        const Entity* entities1 = pool.GetEntities();
        REQUIRE(pool.GetEntityCount() == 1);
        
        // Add another entity
        Entity e2(200, 2);
        pool.Add(e2, 2.0f, 0.0f, 0.0f);
        
        // Cache should be updated
        const Entity* entities2 = pool.GetEntities();
        REQUIRE(pool.GetEntityCount() == 2);
    }
}

TEST_CASE("ComponentPool Batch Operations", "[ComponentPool]")
{
    ComponentPool<Position> pool;
    
    SECTION("GetBatch operation")
    {
        Entity entities[5] = {
            Entity(0, 1), Entity(1, 1), Entity(2, 1), Entity(3, 1), Entity(4, 1)
        };
        
        // Add some components
        pool.Add(entities[0], 0.0f, 0.0f, 0.0f);
        pool.Add(entities[2], 2.0f, 2.0f, 2.0f);
        pool.Add(entities[4], 4.0f, 4.0f, 4.0f);
        
        Position* components[5];
        size_t found = pool.GetBatch(entities, components);
        
        REQUIRE(found == 3);
        REQUIRE(components[0]->x == 0.0f);
        REQUIRE(components[1]->x == 2.0f);
        REQUIRE(components[2]->x == 4.0f);
    }
    
    SECTION("PrefetchBatch operation")
    {
        std::vector<Entity> entities;
        for (int i = 0; i < 100; ++i)
        {
            Entity e(i, 1);
            entities.push_back(e);
            pool.Add(e, float(i), 0.0f, 0.0f);
        }
        
        // Should not crash
        pool.PrefetchBatch(entities.data(), entities.size());
    }
}

TEST_CASE("ComponentPool Memory Management", "[ComponentPool]")
{
    ComponentPool<Position> pool;
    
    SECTION("Reserve operation")
    {
        pool.Reserve(1000);
        
        // Add many components without reallocation
        for (int i = 0; i < 500; ++i)
        {
            pool.Add(Entity(i, 1), float(i), 0.0f, 0.0f);
        }
        
        REQUIRE(pool.Size() == 500);
    }
    
    SECTION("ShrinkToFit operation")
    {
        // Add and remove many components
        for (int i = 0; i < 1000; ++i)
        {
            pool.Add(Entity(i, 1), float(i), 0.0f, 0.0f);
        }
        
        // Remove most
        for (int i = 0; i < 900; ++i)
        {
            pool.Remove(Entity(i, 1));
        }
        
        REQUIRE(pool.Size() == 100);
        
        pool.ShrinkToFit(); // Should not crash
        REQUIRE(pool.Size() == 100); // Size unchanged
    }
}

TEST_CASE("ComponentPool Type Information", "[ComponentPool]")
{
    SECTION("Component type traits")
    {
        ComponentPool<Position> posPool;
        ComponentPool<Velocity> velPool;
        ComponentPool<Name> namePool;
        
        // Component IDs should be unique
        REQUIRE(posPool.GetComponentID() != velPool.GetComponentID());
        REQUIRE(posPool.GetComponentID() != namePool.GetComponentID());
        REQUIRE(velPool.GetComponentID() != namePool.GetComponentID());
        
        // Component sizes
        REQUIRE(posPool.GetComponentSize() == sizeof(Position));
        REQUIRE(velPool.GetComponentSize() == sizeof(Velocity));
        REQUIRE(namePool.GetComponentSize() == sizeof(Name));
        
        // Component names (implementation specific, just verify not empty)
        REQUIRE(!posPool.GetComponentName().empty());
        REQUIRE(!velPool.GetComponentName().empty());
        REQUIRE(!namePool.GetComponentName().empty());
    }
}

TEST_CASE("ComponentPool with Complex Types", "[ComponentPool]")
{
    ComponentPool<Name> pool;
    
    SECTION("String component operations")
    {
        Entity e1(100, 1);
        Entity e2(200, 2);
        
        pool.Add(e1, "Entity One");
        pool.Add(e2, "Entity Two");
        
        REQUIRE(pool.Get(e1).value == "Entity One");
        REQUIRE(pool.Get(e2).value == "Entity Two");
        
        // Modify
        pool.Get(e1).value = "Modified";
        REQUIRE(pool.Get(e1).value == "Modified");
        
        // Set with move semantics
        std::string longName = "This is a very long entity name that should be moved";
        pool.Set(e1, std::move(longName));
        REQUIRE(pool.Get(e1).value == "This is a very long entity name that should be moved");
        REQUIRE((longName.empty() || longName == "This is a very long entity name that should be moved"));
    }
}

TEST_CASE("IComponentPool Polymorphism", "[ComponentPool]")
{
    SECTION("Polymorphic usage")
    {
        ComponentPool<Position> posPool;
        ComponentPool<Velocity> velPool;
        
        IComponentPool* pool1 = &posPool;
        IComponentPool* pool2 = &velPool;
        
        Entity e(100, 1);
        
        // Add through concrete type
        posPool.Add(e, 1.0f, 2.0f, 3.0f);
        velPool.Add(e, 4.0f, 5.0f, 6.0f);
        
        // Query through interface
        REQUIRE(pool1->Contains(e));
        REQUIRE(pool2->Contains(e));
        REQUIRE(pool1->Size() == 1);
        REQUIRE(pool2->Size() == 1);
        
        // Remove through interface
        REQUIRE(pool1->Remove(e));
        REQUIRE(!pool1->Contains(e));
        REQUIRE(pool1->Empty());
    }
}

TEST_CASE("ComponentPool Edge Cases", "[ComponentPool]")
{
    ComponentPool<Position> pool;
    
    SECTION("Operations on null entity")
    {
        Entity null = Entity::Null();
        
        // Add to null entity
        auto* pos = pool.Add(null, 1.0f, 2.0f, 3.0f);
        REQUIRE(pos != nullptr); // Null entity is still a valid key
        REQUIRE(pool.Contains(null));
        
        // Remove null entity
        REQUIRE(pool.Remove(null));
        REQUIRE(!pool.Contains(null));
    }
    
    SECTION("Empty pool operations")
    {
        // Note: GetEntities() may return nullptr when empty (std::vector::data() behavior)
        const Entity* entities = pool.GetEntities();
        REQUIRE(pool.GetEntityCount() == 0);
        // If we have entities, it should be valid; if count is 0, nullptr is acceptable
        REQUIRE(((pool.GetEntityCount() == 0) || (entities != nullptr)));
        
        auto it = pool.begin();
        REQUIRE(it == pool.end());
        
        // ForEachGroup on empty pool
        int count = 0;
        pool.ForEachGroup([&count](Entity, Position&) { count++; });
        REQUIRE(count == 0);
    }
}