#include <catch2/catch_all.hpp>
#include <Astra/Registry/Registry.hpp>
#include <Astra/Entity/Entity.hpp>
#include <string>
#include <vector>
#include <random>
#include <algorithm>
#include <unordered_set>

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

struct Health 
{
    float current;
    float max;
    
    Health() = default;
    Health(float cur, float m) : current(cur), max(m) {}
};

// Components satisfy the Component concept automatically
// No explicit registration needed with C++20 concepts

TEST_CASE("Registry Construction", "[Registry]")
{
    SECTION("Default construction")
    {
        Registry registry;
        REQUIRE(registry.EntityCount() == 0);
    }
    
    SECTION("Move construction")
    {
        Registry registry1;
        auto e = registry1.CreateEntity();
        registry1.AddComponent<Position>(e, 1.0f, 2.0f, 3.0f);
        
        Registry registry2(std::move(registry1));
        REQUIRE(registry2.Valid(e));
        REQUIRE(registry2.HasComponent<Position>(e));
    }
    
    SECTION("Move assignment")
    {
        Registry registry1;
        auto e = registry1.CreateEntity();
        registry1.AddComponent<Position>(e, 1.0f, 2.0f, 3.0f);
        
        Registry registry2;
        registry2 = std::move(registry1);
        REQUIRE(registry2.Valid(e));
        REQUIRE(registry2.HasComponent<Position>(e));
    }
}

TEST_CASE("Registry Entity Management", "[Registry]")
{
    Registry registry;
    
    SECTION("Create entity")
    {
        Entity e = registry.CreateEntity();
        REQUIRE(e.Valid());
        REQUIRE(e != Entity::Null());
        REQUIRE(registry.Valid(e));
        REQUIRE(registry.EntityCount() == 1);
    }
    
    SECTION("Create multiple entities")
    {
        std::vector<Entity> entities;
        for (int i = 0; i < 100; ++i)
        {
            Entity e = registry.CreateEntity();
            entities.push_back(e);
            REQUIRE(registry.Valid(e));
        }
        
        REQUIRE(registry.EntityCount() == 100);
        
        // All entities should be unique
        std::unordered_set<Entity, EntityHash> uniqueEntities(entities.begin(), entities.end());
        REQUIRE(uniqueEntities.size() == entities.size());
    }
    
    SECTION("Destroy entity")
    {
        Entity e = registry.CreateEntity();
        REQUIRE(registry.Valid(e));
        
        bool destroyed = registry.DestroyEntity(e);
        REQUIRE(destroyed);
        REQUIRE(!registry.Valid(e));
        REQUIRE(registry.EntityCount() == 0);
    }
    
    SECTION("Destroy invalid entity")
    {
        bool destroyed = registry.DestroyEntity(Entity::Null());
        REQUIRE(!destroyed);
        
        Entity fake(999, 1);
        destroyed = registry.DestroyEntity(fake);
        REQUIRE(!destroyed);
    }
    
    SECTION("Entity recycling")
    {
        Entity e1 = registry.CreateEntity();
        EntityID id = e1.Index();
        
        registry.DestroyEntity(e1);
        REQUIRE(!registry.Valid(e1));
        
        Entity e2 = registry.CreateEntity();
        REQUIRE(e2.Index() == id); // Same ID
        REQUIRE(e2.Version() != e1.Version()); // Different version
        REQUIRE(registry.Valid(e2));
        REQUIRE(!registry.Valid(e1)); // Old version still invalid
    }
}

TEST_CASE("Registry Component Management", "[Registry]")
{
    Registry registry;
    
    SECTION("Add component")
    {
        Entity e = registry.CreateEntity();
        
        auto* pos = registry.AddComponent<Position>(e, 1.0f, 2.0f, 3.0f);
        REQUIRE(pos != nullptr);
        REQUIRE(pos->x == 1.0f);
        REQUIRE(pos->y == 2.0f);
        REQUIRE(pos->z == 3.0f);
        REQUIRE(registry.HasComponent<Position>(e));
    }
    
    SECTION("Add component to invalid entity")
    {
        auto* pos = registry.AddComponent<Position>(Entity::Null(), 1.0f, 2.0f, 3.0f);
        REQUIRE(pos == nullptr);
    }
    
    SECTION("Add duplicate component")
    {
        Entity e = registry.CreateEntity();
        
        auto* pos1 = registry.AddComponent<Position>(e, 1.0f, 2.0f, 3.0f);
        REQUIRE(pos1 != nullptr);
        
        auto* pos2 = registry.AddComponent<Position>(e, 4.0f, 5.0f, 6.0f);
        REQUIRE(pos2 == nullptr); // Should fail
        REQUIRE(pos1->x == 1.0f); // Original unchanged
    }
    
    SECTION("Set component (add or replace)")
    {
        Entity e = registry.CreateEntity();
        
        // First set (add)
        auto* pos1 = registry.SetComponent<Position>(e, 1.0f, 2.0f, 3.0f);
        REQUIRE(pos1 != nullptr);
        REQUIRE(pos1->x == 1.0f);
        
        // Second set (replace)
        auto* pos2 = registry.SetComponent<Position>(e, 4.0f, 5.0f, 6.0f);
        REQUIRE(pos2 != nullptr);
        REQUIRE(pos2->x == 4.0f);
        REQUIRE(registry.HasComponent<Position>(e));
    }
    
    SECTION("Get component")
    {
        Entity e = registry.CreateEntity();
        registry.AddComponent<Position>(e, 1.0f, 2.0f, 3.0f);
        
        auto* pos = registry.GetComponent<Position>(e);
        REQUIRE(pos != nullptr);
        REQUIRE(pos->x == 1.0f);
        
        // Modify through pointer
        pos->x = 10.0f;
        REQUIRE(registry.GetComponent<Position>(e)->x == 10.0f);
    }
    
    SECTION("Get component const")
    {
        Entity e = registry.CreateEntity();
        registry.AddComponent<Position>(e, 1.0f, 2.0f, 3.0f);
        
        const Registry& constRegistry = registry;
        const auto* pos = constRegistry.GetComponent<Position>(e);
        REQUIRE(pos != nullptr);
        REQUIRE(pos->x == 1.0f);
    }
    
    SECTION("Get non-existent component")
    {
        Entity e = registry.CreateEntity();
        
        auto* pos = registry.GetComponent<Position>(e);
        REQUIRE(pos == nullptr);
    }
    
    SECTION("Has component")
    {
        Entity e = registry.CreateEntity();
        
        REQUIRE(!registry.HasComponent<Position>(e));
        
        registry.AddComponent<Position>(e, 1.0f, 2.0f, 3.0f);
        REQUIRE(registry.HasComponent<Position>(e));
        REQUIRE(!registry.HasComponent<Velocity>(e));
    }
    
    SECTION("Remove component")
    {
        Entity e = registry.CreateEntity();
        registry.AddComponent<Position>(e, 1.0f, 2.0f, 3.0f);
        
        REQUIRE(registry.HasComponent<Position>(e));
        
        bool removed = registry.RemoveComponent<Position>(e);
        REQUIRE(removed);
        REQUIRE(!registry.HasComponent<Position>(e));
        
        // Remove again should fail
        removed = registry.RemoveComponent<Position>(e);
        REQUIRE(!removed);
    }
}

TEST_CASE("Registry Multiple Components", "[Registry]")
{
    Registry registry;
    
    SECTION("Entity with multiple components")
    {
        Entity e = registry.CreateEntity();
        
        registry.AddComponent<Position>(e, 1.0f, 2.0f, 3.0f);
        registry.AddComponent<Velocity>(e, 4.0f, 5.0f, 6.0f);
        registry.AddComponent<Name>(e, "Player");
        
        REQUIRE(registry.HasComponent<Position>(e));
        REQUIRE(registry.HasComponent<Velocity>(e));
        REQUIRE(registry.HasComponent<Name>(e));
        REQUIRE(!registry.HasComponent<Health>(e));
        
        // Get all components
        auto* pos = registry.GetComponent<Position>(e);
        auto* vel = registry.GetComponent<Velocity>(e);
        auto* name = registry.GetComponent<Name>(e);
        
        REQUIRE(pos->x == 1.0f);
        REQUIRE(vel->dx == 4.0f);
        REQUIRE(name->value == "Player");
    }
    
    SECTION("Different entities with different components")
    {
        Entity e1 = registry.CreateEntity();
        Entity e2 = registry.CreateEntity();
        Entity e3 = registry.CreateEntity();
        
        registry.AddComponent<Position>(e1, 1.0f, 0.0f, 0.0f);
        registry.AddComponent<Velocity>(e1, 0.0f, 1.0f, 0.0f);
        
        registry.AddComponent<Position>(e2, 2.0f, 0.0f, 0.0f);
        registry.AddComponent<Name>(e2, "Enemy");
        
        registry.AddComponent<Health>(e3, 100.0f, 100.0f);
        
        // Verify component distribution
        REQUIRE(registry.HasComponent<Position>(e1));
        REQUIRE(registry.HasComponent<Velocity>(e1));
        REQUIRE(!registry.HasComponent<Name>(e1));
        
        REQUIRE(registry.HasComponent<Position>(e2));
        REQUIRE(!registry.HasComponent<Velocity>(e2));
        REQUIRE(registry.HasComponent<Name>(e2));
        
        REQUIRE(!registry.HasComponent<Position>(e3));
        REQUIRE(registry.HasComponent<Health>(e3));
    }
}

TEST_CASE("Registry Entity Destruction", "[Registry]")
{
    Registry registry;
    
    SECTION("Destroy entity removes all components")
    {
        Entity e = registry.CreateEntity();
        
        registry.AddComponent<Position>(e, 1.0f, 2.0f, 3.0f);
        registry.AddComponent<Velocity>(e, 4.0f, 5.0f, 6.0f);
        registry.AddComponent<Name>(e, "Test");
        
        REQUIRE(registry.HasComponent<Position>(e));
        REQUIRE(registry.HasComponent<Velocity>(e));
        REQUIRE(registry.HasComponent<Name>(e));
        
        registry.DestroyEntity(e);
        
        REQUIRE(!registry.Valid(e));
        REQUIRE(!registry.HasComponent<Position>(e));
        REQUIRE(!registry.HasComponent<Velocity>(e));
        REQUIRE(!registry.HasComponent<Name>(e));
    }
    
    SECTION("Components are actually removed from pools")
    {
        Entity e1 = registry.CreateEntity();
        Entity e2 = registry.CreateEntity();
        
        registry.AddComponent<Position>(e1, 1.0f, 0.0f, 0.0f);
        registry.AddComponent<Position>(e2, 2.0f, 0.0f, 0.0f);
        
        auto* pool = registry.GetPool<Position>();
        REQUIRE(pool != nullptr);
        REQUIRE(pool->Size() == 2);
        
        registry.DestroyEntity(e1);
        REQUIRE(pool->Size() == 1);
        REQUIRE(!pool->Contains(e1));
        REQUIRE(pool->Contains(e2));
    }
}

TEST_CASE("Registry Pool Access", "[Registry]")
{
    Registry registry;
    
    SECTION("Get pool for registered component")
    {
        Entity e = registry.CreateEntity();
        registry.AddComponent<Position>(e, 1.0f, 2.0f, 3.0f);
        
        auto* pool = registry.GetPool<Position>();
        REQUIRE(pool != nullptr);
        REQUIRE(pool->Size() == 1);
        REQUIRE(pool->Contains(e));
    }
    
    SECTION("Get pool for unregistered component")
    {
        auto* pool = registry.GetPool<Position>();
        REQUIRE(pool == nullptr);
    }
    
    SECTION("Pool is created lazily")
    {
        REQUIRE(registry.GetPool<Position>() == nullptr);
        
        Entity e = registry.CreateEntity();
        registry.AddComponent<Position>(e, 1.0f, 2.0f, 3.0f);
        
        auto* pool = registry.GetPool<Position>();
        REQUIRE(pool != nullptr);
        REQUIRE(pool->GetComponentID() == TypeID<Position>::Value());
    }
    
    SECTION("Const pool access")
    {
        Entity e = registry.CreateEntity();
        registry.AddComponent<Position>(e, 1.0f, 2.0f, 3.0f);
        
        const Registry& constRegistry = registry;
        const auto* pool = constRegistry.GetPool<Position>();
        REQUIRE(pool != nullptr);
        REQUIRE(pool->Size() == 1);
    }
}

TEST_CASE("Registry Clear", "[Registry]")
{
    Registry registry;
    
    SECTION("Clear all entities and components")
    {
        // Create entities with various components
        for (int i = 0; i < 10; ++i)
        {
            Entity e = registry.CreateEntity();
            registry.AddComponent<Position>(e, float(i), 0.0f, 0.0f);
            if (i % 2 == 0)
            {
                registry.AddComponent<Velocity>(e, 0.0f, float(i), 0.0f);
            }
            if (i % 3 == 0)
            {
                registry.AddComponent<Name>(e, std::to_string(i));
            }
        }
        
        REQUIRE(registry.EntityCount() == 10);
        REQUIRE(registry.GetPool<Position>()->Size() == 10);
        REQUIRE(registry.GetPool<Velocity>()->Size() == 5);
        REQUIRE(registry.GetPool<Name>()->Size() == 4);
        
        registry.Clear();
        
        REQUIRE(registry.EntityCount() == 0);
        REQUIRE(registry.GetPool<Position>()->Empty());
        REQUIRE(registry.GetPool<Velocity>()->Empty());
        REQUIRE(registry.GetPool<Name>()->Empty());
        
        // Can create new entities after clear
        Entity e = registry.CreateEntity();
        REQUIRE(registry.Valid(e));
    }
}

TEST_CASE("Registry Reserve", "[Registry]")
{
    Registry registry;
    
    SECTION("Reserve entities")
    {
        registry.ReserveEntities(1000);
        
        // Create many entities without reallocation
        std::vector<Entity> entities;
        for (int i = 0; i < 500; ++i)
        {
            entities.push_back(registry.CreateEntity());
        }
        
        REQUIRE(registry.EntityCount() == 500);
        for (const auto& e : entities)
        {
            REQUIRE(registry.Valid(e));
        }
    }
    
    SECTION("Reserve components")
    {
        registry.ReserveComponents<Position>(1000);
        registry.ReserveComponents<Velocity>(500);
        
        // Pool should be created
        auto* posPool = registry.GetPool<Position>();
        auto* velPool = registry.GetPool<Velocity>();
        
        REQUIRE(posPool != nullptr);
        REQUIRE(velPool != nullptr);
        
        // Add many components
        for (int i = 0; i < 500; ++i)
        {
            Entity e = registry.CreateEntity();
            registry.AddComponent<Position>(e, float(i), 0.0f, 0.0f);
            if (i < 250)
            {
                registry.AddComponent<Velocity>(e, 0.0f, float(i), 0.0f);
            }
        }
        
        REQUIRE(posPool->Size() == 500);
        REQUIRE(velPool->Size() == 250);
    }
}

TEST_CASE("Registry Complex Scenarios", "[Registry]")
{
    Registry registry;
    
    SECTION("Entity lifecycle with component changes")
    {
        Entity e = registry.CreateEntity();
        
        // Add components progressively
        registry.AddComponent<Position>(e, 0.0f, 0.0f, 0.0f);
        REQUIRE(registry.HasComponent<Position>(e));
        
        registry.AddComponent<Velocity>(e, 1.0f, 1.0f, 1.0f);
        REQUIRE(registry.HasComponent<Velocity>(e));
        
        // Modify components
        registry.GetComponent<Position>(e)->x = 10.0f;
        registry.GetComponent<Velocity>(e)->dx = 5.0f;
        
        // Remove a component
        registry.RemoveComponent<Velocity>(e);
        REQUIRE(!registry.HasComponent<Velocity>(e));
        REQUIRE(registry.HasComponent<Position>(e)); // Other component unaffected
        
        // Add different component
        registry.AddComponent<Name>(e, "Player");
        REQUIRE(registry.HasComponent<Name>(e));
        
        // Destroy entity
        registry.DestroyEntity(e);
        REQUIRE(!registry.Valid(e));
    }
    
    SECTION("Mass entity operations")
    {
        const int entityCount = 1000;
        std::vector<Entity> entities;
        
        // Create entities
        for (int i = 0; i < entityCount; ++i)
        {
            entities.push_back(registry.CreateEntity());
        }
        
        // Add components to half
        for (int i = 0; i < entityCount / 2; ++i)
        {
            registry.AddComponent<Position>(entities[i], float(i), 0.0f, 0.0f);
            registry.AddComponent<Health>(entities[i], 100.0f, 100.0f);
        }
        
        // Add different components to other half
        for (int i = entityCount / 2; i < entityCount; ++i)
        {
            registry.AddComponent<Velocity>(entities[i], 0.0f, float(i), 0.0f);
            registry.AddComponent<Name>(entities[i], std::to_string(i));
        }
        
        // Verify counts
        REQUIRE(registry.GetPool<Position>()->Size() == entityCount / 2);
        REQUIRE(registry.GetPool<Health>()->Size() == entityCount / 2);
        REQUIRE(registry.GetPool<Velocity>()->Size() == entityCount / 2);
        REQUIRE(registry.GetPool<Name>()->Size() == entityCount / 2);
        
        // Destroy every other entity
        for (int i = 0; i < entityCount; i += 2)
        {
            registry.DestroyEntity(entities[i]);
        }
        
        REQUIRE(registry.EntityCount() == entityCount / 2);
    }
}

TEST_CASE("Registry String Components", "[Registry]")
{
    Registry registry;
    
    SECTION("Complex component types")
    {
        Entity e = registry.CreateEntity();
        
        // Add string component
        registry.AddComponent<Name>(e, "Long Entity Name That Should Be Moved");
        
        auto* name = registry.GetComponent<Name>(e);
        REQUIRE(name != nullptr);
        REQUIRE(name->value == "Long Entity Name That Should Be Moved");
        
        // Modify string
        name->value = "Modified";
        REQUIRE(registry.GetComponent<Name>(e)->value == "Modified");
        
        // Replace with SetComponent
        registry.SetComponent<Name>(e, "Replaced");
        REQUIRE(registry.GetComponent<Name>(e)->value == "Replaced");
    }
}

TEST_CASE("Registry Edge Cases", "[Registry]")
{
    Registry registry;
    
    SECTION("Operations on null entity")
    {
        Entity null = Entity::Null();
        
        REQUIRE(!registry.Valid(null));
        REQUIRE(!registry.DestroyEntity(null));
        REQUIRE(registry.AddComponent<Position>(null, 1.0f, 2.0f, 3.0f) == nullptr);
        REQUIRE(registry.SetComponent<Position>(null, 1.0f, 2.0f, 3.0f) == nullptr);
        REQUIRE(registry.GetComponent<Position>(null) == nullptr);
        REQUIRE(!registry.HasComponent<Position>(null));
        REQUIRE(!registry.RemoveComponent<Position>(null));
    }
    
    SECTION("Operations after entity destruction")
    {
        Entity e = registry.CreateEntity();
        registry.AddComponent<Position>(e, 1.0f, 2.0f, 3.0f);
        
        registry.DestroyEntity(e);
        
        // All operations should fail gracefully
        REQUIRE(registry.AddComponent<Velocity>(e, 1.0f, 1.0f, 1.0f) == nullptr);
        REQUIRE(registry.GetComponent<Position>(e) == nullptr);
        REQUIRE(!registry.HasComponent<Position>(e));
        REQUIRE(!registry.RemoveComponent<Position>(e));
    }
    
    SECTION("Empty registry operations")
    {
        REQUIRE(registry.EntityCount() == 0);
        REQUIRE(registry.GetPool<Position>() == nullptr);
        
        registry.Clear(); // Should be safe on empty registry
        REQUIRE(registry.EntityCount() == 0);
    }
}