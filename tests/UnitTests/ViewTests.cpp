#include <catch2/catch_all.hpp>
#include <Astra/Registry/Registry.hpp>
#include <Astra/Registry/View.hpp>
#include <Astra/Entity/Entity.hpp>
#include <string>
#include <vector>
#include <set>
#include <algorithm>

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

struct Health 
{
    float current;
    float max;
    
    Health() = default;
    Health(float cur, float m) : current(cur), max(m) {}
};

struct Name 
{
    std::string value;
    
    Name() = default;
    explicit Name(std::string v) : value(std::move(v)) {}
};

// Components satisfy the Component concept automatically
// No explicit registration needed with C++20 concepts

TEST_CASE("View Single Component", "[View]")
{
    Registry registry;
    
    SECTION("Empty view")
    {
        auto view = registry.GetView<Position>();
        REQUIRE(view.empty());
        REQUIRE(view.size() == 0);
        
        int count = 0;
        for (auto& [entity, pos] : view)
        {
            (void)entity;
            (void)pos;
            count++;
        }
        REQUIRE(count == 0);
    }
    
    SECTION("View with single entity")
    {
        Entity e = registry.CreateEntity();
        registry.AddComponent<Position>(e, 1.0f, 2.0f, 3.0f);
        
        auto view = registry.GetView<Position>();
        REQUIRE(!view.empty());
        REQUIRE(view.size() == 1);
        
        int count = 0;
        for (auto& [entity, pos] : view)
        {
            REQUIRE(entity == e);
            REQUIRE(pos.x == 1.0f);
            REQUIRE(pos.y == 2.0f);
            REQUIRE(pos.z == 3.0f);
            count++;
        }
        REQUIRE(count == 1);
    }
    
    SECTION("View with multiple entities")
    {
        std::vector<Entity> entities;
        for (int i = 0; i < 10; ++i)
        {
            Entity e = registry.CreateEntity();
            entities.push_back(e);
            registry.AddComponent<Position>(e, float(i), float(i * 2), float(i * 3));
        }
        
        auto view = registry.GetView<Position>();
        REQUIRE(view.size() == 10);
        
        std::set<Entity, std::less<Entity>> foundEntities;
        for (auto& [entity, pos] : view)
        {
            foundEntities.insert(entity);
            
            // Find which entity this is
            auto it = std::find(entities.begin(), entities.end(), entity);
            REQUIRE(it != entities.end());
            int idx = static_cast<int>(std::distance(entities.begin(), it));
            
            REQUIRE(pos.x == float(idx));
            REQUIRE(pos.y == float(idx * 2));
            REQUIRE(pos.z == float(idx * 3));
        }
        
        REQUIRE(foundEntities.size() == 10);
    }
    
    SECTION("Modify components through view")
    {
        Entity e = registry.CreateEntity();
        registry.AddComponent<Position>(e, 1.0f, 2.0f, 3.0f);
        
        auto view = registry.GetView<Position>();
        for (auto& [entity, pos] : view)
        {
            pos.x = 10.0f;
            pos.y = 20.0f;
            pos.z = 30.0f;
        }
        
        // Verify changes
        auto* pos = registry.GetComponent<Position>(e);
        REQUIRE(pos->x == 10.0f);
        REQUIRE(pos->y == 20.0f);
        REQUIRE(pos->z == 30.0f);
    }
    
    SECTION("ForEach operation")
    {
        std::vector<Entity> entities;
        for (int i = 0; i < 5; ++i)
        {
            Entity e = registry.CreateEntity();
            entities.push_back(e);
            registry.AddComponent<Position>(e, float(i), 0.0f, 0.0f);
        }
        
        auto view = registry.GetView<Position>();
        
        int count = 0;
        view.ForEach([&count](Entity e, Position& pos)
        {
            REQUIRE(e.Valid());
            REQUIRE(pos.x >= 0.0f);
            REQUIRE(pos.x < 5.0f);
            count++;
        });
        
        REQUIRE(count == 5);
    }
    
    SECTION("ForEachGroup operation")
    {
        // Create enough entities to span multiple groups
        for (int i = 0; i < 50; ++i)
        {
            Entity e = registry.CreateEntity();
            registry.AddComponent<Position>(e, float(i), 0.0f, 0.0f);
        }
        
        auto view = registry.GetView<Position>();
        
        int totalCount = 0;
        view.ForEachGroup([&totalCount](Entity e, Position& pos)
        {
            REQUIRE(e.Valid());
            totalCount++;
        });
        
        REQUIRE(totalCount == 50);
    }
}

TEST_CASE("View Multiple Components", "[View]")
{
    Registry registry;
    
    SECTION("Empty multi-component view")
    {
        auto view = registry.GetView<Position, Velocity>();
        REQUIRE(view.empty());
        
        int count = 0;
        for (auto [entity, pos, vel] : view)
        {
            (void)entity;
            (void)pos;
            (void)vel;
            count++;
        }
        REQUIRE(count == 0);
    }
    
    SECTION("View with matching entities")
    {
        Entity e1 = registry.CreateEntity();
        Entity e2 = registry.CreateEntity();
        Entity e3 = registry.CreateEntity();
        
        // e1 has both components
        registry.AddComponent<Position>(e1, 1.0f, 1.0f, 1.0f);
        registry.AddComponent<Velocity>(e1, 2.0f, 2.0f, 2.0f);
        
        // e2 has only Position
        registry.AddComponent<Position>(e2, 3.0f, 3.0f, 3.0f);
        
        // e3 has only Velocity
        registry.AddComponent<Velocity>(e3, 4.0f, 4.0f, 4.0f);
        
        auto view = registry.GetView<Position, Velocity>();
        
        // Only e1 should be in the view
        int count = 0;
        for (auto [entity, pos, vel] : view)
        {
            REQUIRE(entity == e1);
            REQUIRE(pos.x == 1.0f);
            REQUIRE(vel.dx == 2.0f);
            count++;
        }
        REQUIRE(count == 1);
    }
    
    SECTION("Traditional iteration vs streaming")
    {
        // Create entities with both components
        std::vector<Entity> entities;
        for (int i = 0; i < 20; ++i)
        {
            Entity e = registry.CreateEntity();
            entities.push_back(e);
            registry.AddComponent<Position>(e, float(i), 0.0f, 0.0f);
            registry.AddComponent<Velocity>(e, 0.0f, float(i), 0.0f);
        }
        
        auto view = registry.GetView<Position, Velocity>();
        
        // Traditional iteration
        std::set<Entity, std::less<Entity>> traditionalEntities;
        for (auto it = view.begin_traditional(); it != view.end_traditional(); ++it)
        {
            auto [entity, pos, vel] = *it;
            traditionalEntities.insert(entity);
        }
        
        // Streaming iteration (default)
        std::set<Entity, std::less<Entity>> streamEntities;
        for (auto [entity, pos, vel] : view)
        {
            streamEntities.insert(entity);
        }
        
        // Both should find same entities
        REQUIRE(traditionalEntities.size() == 20);
        REQUIRE(streamEntities == traditionalEntities);
    }
    
    SECTION("Three component view")
    {
        Entity e1 = registry.CreateEntity();
        Entity e2 = registry.CreateEntity();
        
        // e1 has all three components
        registry.AddComponent<Position>(e1, 1.0f, 1.0f, 1.0f);
        registry.AddComponent<Velocity>(e1, 2.0f, 2.0f, 2.0f);
        registry.AddComponent<Health>(e1, 100.0f, 100.0f);
        
        // e2 has only two components
        registry.AddComponent<Position>(e2, 3.0f, 3.0f, 3.0f);
        registry.AddComponent<Velocity>(e2, 4.0f, 4.0f, 4.0f);
        
        auto view = registry.GetView<Position, Velocity, Health>();
        
        // Only e1 should match
        int count = 0;
        for (auto [entity, pos, vel, health] : view)
        {
            REQUIRE(entity == e1);
            REQUIRE(pos.x == 1.0f);
            REQUIRE(vel.dx == 2.0f);
            REQUIRE(health.current == 100.0f);
            count++;
        }
        REQUIRE(count == 1);
    }
    
    SECTION("ForEach with multiple components")
    {
        for (int i = 0; i < 10; ++i)
        {
            Entity e = registry.CreateEntity();
            registry.AddComponent<Position>(e, float(i), 0.0f, 0.0f);
            registry.AddComponent<Velocity>(e, 0.0f, float(i), 0.0f);
        }
        
        auto view = registry.GetView<Position, Velocity>();
        
        int count = 0;
        view.ForEach([&count](Entity e, Position& pos, Velocity& vel)
        {
            REQUIRE(e.Valid());
            REQUIRE(pos.x == vel.dy); // We set them to same value
            count++;
        });
        
        REQUIRE(count == 10);
    }
    
    SECTION("ForEachGroup with multiple components")
    {
        // Create enough entities for multiple groups
        for (int i = 0; i < 50; ++i)
        {
            Entity e = registry.CreateEntity();
            registry.AddComponent<Position>(e, float(i), 0.0f, 0.0f);
            registry.AddComponent<Velocity>(e, 0.0f, float(i), 0.0f);
        }
        
        auto view = registry.GetView<Position, Velocity>();
        
        int totalProcessed = 0;
        view.ForEachGroup([&totalProcessed](const Entity* entities, std::size_t count,
                                            Position** positions, Velocity** velocities)
        {
            REQUIRE(count > 0);
            REQUIRE(count <= 16); // Max batch size
            
            for (std::size_t i = 0; i < count; ++i)
            {
                REQUIRE(entities[i].Valid());
                REQUIRE(positions[i] != nullptr);
                REQUIRE(velocities[i] != nullptr);
                REQUIRE(positions[i]->x == velocities[i]->dy);
            }
            
            totalProcessed += static_cast<int>(count);
        });
        
        REQUIRE(totalProcessed == 50);
    }
}

TEST_CASE("View Const Correctness", "[View]")
{
    Registry registry;
    
    SECTION("Const view single component")
    {
        Entity e = registry.CreateEntity();
        registry.AddComponent<Position>(e, 1.0f, 2.0f, 3.0f);
        
        const Registry& constRegistry = registry;
        auto view = constRegistry.GetView<Position>();
        
        int count = 0;
        for (const auto& [entity, pos] : view)
        {
            REQUIRE(entity == e);
            REQUIRE(pos.x == 1.0f);
            // pos.x = 10.0f; // Should not compile
            count++;
        }
        REQUIRE(count == 1);
    }
    
    SECTION("Const view multiple components")
    {
        Entity e = registry.CreateEntity();
        registry.AddComponent<Position>(e, 1.0f, 2.0f, 3.0f);
        registry.AddComponent<Velocity>(e, 4.0f, 5.0f, 6.0f);
        
        const Registry& constRegistry = registry;
        auto view = constRegistry.GetView<Position, Velocity>();
        
        int count = 0;
        // Use traditional iteration for const views to avoid ComponentStream const issues
        for (auto it = view.begin_traditional(); it != view.end_traditional(); ++it)
        {
            auto [entity, pos, vel] = *it;
            REQUIRE(entity == e);
            REQUIRE(pos.x == 1.0f);
            REQUIRE(vel.dx == 4.0f);
            // pos.x = 10.0f; // Should not compile
            // vel.dx = 20.0f; // Should not compile
            count++;
        }
        REQUIRE(count == 1);
    }
}

TEST_CASE("View Stream", "[View]")
{
    Registry registry;
    
    SECTION("Create and reuse stream")
    {
        // Create entities
        std::vector<Entity> entities;
        for (int i = 0; i < 20; ++i)
        {
            Entity e = registry.CreateEntity();
            entities.push_back(e);
            registry.AddComponent<Position>(e, float(i), 0.0f, 0.0f);
            registry.AddComponent<Velocity>(e, 0.0f, float(i), 0.0f);
        }
        
        auto view = registry.GetView<Position, Velocity>();
        auto stream = view.Stream();
        
        // First iteration
        int count1 = 0;
        stream.ForEach([&count1](Entity e, Position& pos, Velocity& vel)
        {
            REQUIRE(e.Valid());
            REQUIRE(pos.x == vel.dy);
            count1++;
        });
        REQUIRE(count1 == 20);
        
        // Second iteration on same stream
        int count2 = 0;
        stream.ForEach([&count2](Entity e, Position& pos, Velocity& vel)
        {
            REQUIRE(e.Valid());
            REQUIRE(pos.x == vel.dy);
            count2++;
        });
        REQUIRE(count2 == 20);
    }
    
    SECTION("Stream iteration")
    {
        for (int i = 0; i < 30; ++i)
        {
            Entity e = registry.CreateEntity();
            registry.AddComponent<Position>(e, float(i), 0.0f, 0.0f);
            registry.AddComponent<Velocity>(e, 0.0f, float(i), 0.0f);
        }
        
        auto view = registry.GetView<Position, Velocity>();
        auto stream = view.Stream();
        
        // Iterate using range-based for
        int count = 0;
        for (auto [entity, pos, vel] : stream)
        {
            REQUIRE(entity.Valid());
            REQUIRE(pos.x == vel.dy);
            count++;
        }
        REQUIRE(count == 30);
    }
}

TEST_CASE("View Edge Cases", "[View]")
{
    Registry registry;
    
    SECTION("View with null pool")
    {
        // Don't create any Position components
        auto view = registry.GetView<Position>();
        REQUIRE(view.empty());
        
        int count = 0;
        for (auto& [entity, pos] : view)
        {
            (void)entity;
            (void)pos;
            count++;
        }
        REQUIRE(count == 0);
    }
    
    SECTION("View after entity destruction")
    {
        Entity e1 = registry.CreateEntity();
        Entity e2 = registry.CreateEntity();
        
        registry.AddComponent<Position>(e1, 1.0f, 0.0f, 0.0f);
        registry.AddComponent<Position>(e2, 2.0f, 0.0f, 0.0f);
        
        auto view = registry.GetView<Position>();
        REQUIRE(view.size() == 2);
        
        // Destroy one entity
        registry.DestroyEntity(e1);
        
        // View should update
        int count = 0;
        for (auto& [entity, pos] : view)
        {
            REQUIRE(entity == e2);
            REQUIRE(pos.x == 2.0f);
            count++;
        }
        REQUIRE(count == 1);
    }
    
    SECTION("View with sparse components")
    {
        // Create many entities but only some have both components
        std::vector<Entity> matchingEntities;
        
        for (int i = 0; i < 100; ++i)
        {
            Entity e = registry.CreateEntity();
            
            if (i % 2 == 0)
            {
                registry.AddComponent<Position>(e, float(i), 0.0f, 0.0f);
            }
            
            if (i % 3 == 0)
            {
                registry.AddComponent<Velocity>(e, 0.0f, float(i), 0.0f);
            }
            
            // Only entities divisible by 6 have both
            if (i % 6 == 0)
            {
                matchingEntities.push_back(e);
            }
        }
        
        auto view = registry.GetView<Position, Velocity>();
        
        std::set<Entity, std::less<Entity>> foundEntities;
        for (auto [entity, pos, vel] : view)
        {
            foundEntities.insert(entity);
        }
        
        REQUIRE(foundEntities.size() == matchingEntities.size());
        
        // Verify all matching entities were found
        for (auto e : matchingEntities)
        {
            REQUIRE(foundEntities.count(e) == 1);
        }
    }
    
    SECTION("View with four components")
    {
        Entity e = registry.CreateEntity();
        registry.AddComponent<Position>(e, 1.0f, 2.0f, 3.0f);
        registry.AddComponent<Velocity>(e, 4.0f, 5.0f, 6.0f);
        registry.AddComponent<Health>(e, 100.0f, 100.0f);
        registry.AddComponent<Name>(e, "Player");
        
        auto view = registry.GetView<Position, Velocity, Health, Name>();
        
        int count = 0;
        for (auto [entity, pos, vel, health, name] : view)
        {
            REQUIRE(entity == e);
            REQUIRE(pos.x == 1.0f);
            REQUIRE(vel.dx == 4.0f);
            REQUIRE(health.current == 100.0f);
            REQUIRE(name.value == "Player");
            count++;
        }
        REQUIRE(count == 1);
    }
}

TEST_CASE("View Size Hint", "[View]")
{
    Registry registry;
    
    SECTION("Size hint accuracy")
    {
        // Create entities with different component combinations
        for (int i = 0; i < 20; ++i)
        {
            Entity e = registry.CreateEntity();
            registry.AddComponent<Position>(e, float(i), 0.0f, 0.0f);
        }
        
        for (int i = 0; i < 15; ++i)
        {
            Entity e = registry.CreateEntity();
            registry.AddComponent<Velocity>(e, 0.0f, float(i), 0.0f);
        }
        
        for (int i = 0; i < 10; ++i)
        {
            Entity e = registry.CreateEntity();
            registry.AddComponent<Position>(e, float(i + 20), 0.0f, 0.0f);
            registry.AddComponent<Velocity>(e, 0.0f, float(i + 15), 0.0f);
        }
        
        // Single component views
        auto posView = registry.GetView<Position>();
        REQUIRE(posView.size() == 30); // 20 + 10
        
        auto velView = registry.GetView<Velocity>();
        REQUIRE(velView.size() == 25); // 15 + 10
        
        // Multi-component view
        auto bothView = registry.GetView<Position, Velocity>();
        REQUIRE(bothView.SizeHint() <= 25); // Upper bound is smaller pool size
        
        // Count actual matches
        int actualCount = 0;
        for (auto [e, p, v] : bothView)
        {
            (void)e;
            (void)p;
            (void)v;
            actualCount++;
        }
        REQUIRE(actualCount == 10); // Only 10 entities have both
    }
}

TEST_CASE("View Performance Patterns", "[View]")
{
    Registry registry;
    
    SECTION("Group processing with prefetch")
    {
        // Create many entities for performance testing
        const int entityCount = 1000;
        for (int i = 0; i < entityCount; ++i)
        {
            Entity e = registry.CreateEntity();
            registry.AddComponent<Position>(e, float(i), float(i * 2), float(i * 3));
            registry.AddComponent<Velocity>(e, float(i * 4), float(i * 5), float(i * 6));
            registry.AddComponent<Health>(e, 100.0f - float(i % 100), 100.0f);
        }
        
        auto view = registry.GetView<Position, Velocity, Health>();
        
        // Process in groups
        float totalHealth = 0.0f;
        int processedCount = 0;
        
        view.ForEachGroup([&](const Entity* entities, std::size_t count,
                             Position** positions, Velocity** velocities, Health** healths)
        {
            for (std::size_t i = 0; i < count; ++i)
            {
                // Simulate work
                positions[i]->x += velocities[i]->dx * 0.016f;
                positions[i]->y += velocities[i]->dy * 0.016f;
                positions[i]->z += velocities[i]->dz * 0.016f;
                
                totalHealth += healths[i]->current;
                processedCount++;
            }
        });
        
        REQUIRE(processedCount == entityCount);
        REQUIRE(totalHealth > 0.0f);
    }
}