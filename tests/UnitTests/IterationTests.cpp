#include <catch2/catch_all.hpp>
#include <Astra/Astra.hpp>
#include <set>
#include <map>
#include <algorithm>
#include <random>

using namespace Astra;

// Test components
struct IterPos { float x, y, z; };
struct IterVel { float dx, dy, dz; };
struct IterHealth { int hp, maxHp; };
struct IterPhysics { float mass, friction; };
struct IterTag { int id; };

TEST_CASE("Single Component Iteration", "[Registry][View][Iteration]")
{
    Registry registry;
    
    SECTION("Basic iteration")
    {
        std::vector<Entity> entities;
        for (int i = 0; i < 100; ++i)
        {
            auto e = registry.CreateEntity();
            entities.push_back(e);
            registry.AddComponent<IterPos>(e, float(i), float(i * 2), float(i * 3));
        }
        
        auto view = registry.GetView<IterPos>();
        
        // Check count
        size_t count = 0;
        for (auto [e, pos] : view)
        {
            count++;
        }
        REQUIRE(count == 100);
        
        // Check values
        std::set<float> foundX;
        for (auto [e, pos] : view)
        {
            foundX.insert(pos.x);
            REQUIRE(pos.y == pos.x * 2);
            REQUIRE(pos.z == pos.x * 3);
        }
        REQUIRE(foundX.size() == 100);
    }
    
    SECTION("Empty view")
    {
        auto view = registry.GetView<IterPos>();
        
        size_t count = 0;
        for (auto [e, pos] : view)
        {
            count++;
        }
        REQUIRE(count == 0);
    }
    
    SECTION("Sparse components")
    {
        std::vector<Entity> withComponent;
        
        for (int i = 0; i < 1000; ++i)
        {
            auto e = registry.CreateEntity();
            if (i % 10 == 0)
            {
                registry.AddComponent<IterPos>(e, float(i), 0.0f, 0.0f);
                withComponent.push_back(e);
            }
        }
        
        auto view = registry.GetView<IterPos>();
        
        std::set<Entity> found;
        for (auto [e, pos] : view)
        {
            found.insert(e);
            REQUIRE(std::find(withComponent.begin(), withComponent.end(), e) != withComponent.end());
        }
        
        REQUIRE(found.size() == withComponent.size());
    }
}

TEST_CASE("Multi Component Iteration", "[Registry][View][Iteration]")
{
    Registry registry;
    
    SECTION("Two components - all entities match")
    {
        for (int i = 0; i < 50; ++i)
        {
            auto e = registry.CreateEntity();
            registry.AddComponent<IterPos>(e, float(i), 0.0f, 0.0f);
            registry.AddComponent<IterVel>(e, 0.0f, float(i), 0.0f);
        }
        
        auto view = registry.GetView<IterPos, IterVel>();
        
        size_t count = 0;
        for (auto [e, pos, vel] : view)
        {
            REQUIRE(pos.x == vel.dy);
            count++;
        }
        REQUIRE(count == 50);
    }
    
    SECTION("Two components - partial overlap")
    {
        std::vector<Entity> onlyPos, onlyVel, both;
        
        // Create entities with only Position
        for (int i = 0; i < 30; ++i)
        {
            auto e = registry.CreateEntity();
            registry.AddComponent<IterPos>(e, float(i), 0.0f, 0.0f);
            onlyPos.push_back(e);
        }
        
        // Create entities with only Velocity
        for (int i = 0; i < 20; ++i)
        {
            auto e = registry.CreateEntity();
            registry.AddComponent<IterVel>(e, 0.0f, float(i), 0.0f);
            onlyVel.push_back(e);
        }
        
        // Create entities with both
        for (int i = 0; i < 15; ++i)
        {
            auto e = registry.CreateEntity();
            registry.AddComponent<IterPos>(e, float(i + 100), 0.0f, 0.0f);
            registry.AddComponent<IterVel>(e, 0.0f, float(i + 100), 0.0f);
            both.push_back(e);
        }
        
        auto view = registry.GetView<IterPos, IterVel>();
        
        std::set<Entity> found;
        for (auto [e, pos, vel] : view)
        {
            found.insert(e);
            // Should only find entities with both components
            REQUIRE(std::find(both.begin(), both.end(), e) != both.end());
            REQUIRE(pos.x == vel.dy);
        }
        
        REQUIRE(found.size() == 15);
    }
    
    SECTION("Three components")
    {
        std::vector<Entity> matching;
        
        for (int i = 0; i < 100; ++i)
        {
            auto e = registry.CreateEntity();
            
            if (i % 2 == 0)
                registry.AddComponent<IterPos>(e, float(i), 0.0f, 0.0f);
            
            if (i % 3 == 0)
                registry.AddComponent<IterVel>(e, 0.0f, float(i), 0.0f);
            
            if (i % 5 == 0)
                registry.AddComponent<IterHealth>(e, i * 10, i * 10);
            
            // Only entities divisible by 30 have all three
            if (i % 2 == 0 && i % 3 == 0 && i % 5 == 0)
                matching.push_back(e);
        }
        
        auto view = registry.GetView<IterPos, IterVel, IterHealth>();
        
        std::set<Entity> found;
        for (auto [e, pos, vel, health] : view)
        {
            found.insert(e);
            REQUIRE(int(pos.x) == int(vel.dy));
            REQUIRE(health.hp == int(pos.x) * 10);
        }
        
        REQUIRE(found.size() == matching.size());
        for (auto e : matching)
        {
            REQUIRE(found.count(e) == 1);
        }
    }
    
    SECTION("Four components")
    {
        std::vector<Entity> all;
        std::vector<Entity> matching;
        
        for (int i = 0; i < 200; ++i)
        {
            auto e = registry.CreateEntity();
            all.push_back(e);
            
            bool hasAll = true;
            
            if (i % 2 == 0)
                registry.AddComponent<IterPos>(e, float(i), 0.0f, 0.0f);
            else
                hasAll = false;
            
            if (i % 3 == 0)
                registry.AddComponent<IterVel>(e, 0.0f, float(i), 0.0f);
            else
                hasAll = false;
            
            if (i % 5 == 0)
                registry.AddComponent<IterHealth>(e, i, i * 2);
            else
                hasAll = false;
            
            if (i % 7 == 0)
                registry.AddComponent<IterPhysics>(e, float(i), 0.3f);
            else
                hasAll = false;
            
            if (hasAll)
                matching.push_back(e);
        }
        
        auto view = registry.GetView<IterPos, IterVel, IterHealth, IterPhysics>();
        
        size_t count = 0;
        for (auto [e, pos, vel, health, physics] : view)
        {
            REQUIRE(int(pos.x) == int(vel.dy));
            REQUIRE(int(pos.x) == health.hp);
            REQUIRE(int(pos.x) == int(physics.mass));
            count++;
        }
        
        REQUIRE(count == matching.size());
    }
}

TEST_CASE("Iteration Correctness", "[Registry][View][Iteration]")
{
    Registry registry;
    
    SECTION("Multiple iterations produce same results")
    {
        // Create complex entity distribution
        std::mt19937 rng(12345);
        std::uniform_int_distribution<int> dist(0, 4);
        
        for (int i = 0; i < 500; ++i)
        {
            auto e = registry.CreateEntity();
            
            if (dist(rng) > 0)
                registry.AddComponent<IterPos>(e, float(i), float(i * 2), float(i * 3));
            
            if (dist(rng) > 1)
                registry.AddComponent<IterVel>(e, float(i * 4), float(i * 5), float(i * 6));
            
            if (dist(rng) > 2)
                registry.AddComponent<IterHealth>(e, i * 10, i * 20);
        }
        
        auto view = registry.GetView<IterPos, IterVel>();
        
        // First iteration
        std::vector<Entity> firstRun;
        for (auto [e, pos, vel] : view)
        {
            firstRun.push_back(e);
        }
        
        // Subsequent iterations should produce same results
        for (int run = 0; run < 5; ++run)
        {
            std::vector<Entity> thisRun;
            for (auto [e, pos, vel] : view)
            {
                thisRun.push_back(e);
            }
            
            REQUIRE(thisRun.size() == firstRun.size());
            REQUIRE(thisRun == firstRun);
        }
    }
    
    SECTION("Modification invalidates iteration")
    {
        std::vector<Entity> entities;
        for (int i = 0; i < 100; ++i)
        {
            auto e = registry.CreateEntity();
            entities.push_back(e);
            registry.AddComponent<IterPos>(e, float(i), 0.0f, 0.0f);
            
            if (i < 50)
                registry.AddComponent<IterVel>(e, 0.0f, float(i), 0.0f);
        }
        
        // First count
        {
            auto view = registry.GetView<IterPos, IterVel>();
            size_t count1 = 0;
            for (auto [e, pos, vel] : view) count1++;
            REQUIRE(count1 == 50);
        }
        
        // Add velocity to more entities
        for (int i = 50; i < 75; ++i)
        {
            registry.AddComponent<IterVel>(entities[i], 0.0f, float(i), 0.0f);
        }
        
        // Count should increase - need new view after modifications
        {
            auto view = registry.GetView<IterPos, IterVel>();
            size_t count2 = 0;
            for (auto [e, pos, vel] : view) count2++;
            REQUIRE(count2 == 75);
        }
        
        // Remove some components
        for (int i = 0; i < 25; ++i)
        {
            registry.RemoveComponent<IterVel>(entities[i]);
        }
        
        // Count should decrease - need new view after modifications
        {
            auto view = registry.GetView<IterPos, IterVel>();
            size_t count3 = 0;
            for (auto [e, pos, vel] : view) count3++;
            REQUIRE(count3 == 50);
        }
    }
    
    SECTION("Component values can be modified during iteration")
    {
        std::map<Entity, float> originalValues;
        
        for (int i = 0; i < 100; ++i)
        {
            auto e = registry.CreateEntity();
            float originalX = float(i * 10); // Use distinct values
            registry.AddComponent<IterPos>(e, originalX, 0.0f, 0.0f);
            registry.AddComponent<IterVel>(e, 1.0f, 2.0f, 3.0f);
            originalValues[e] = originalX;
        }
        
        auto view = registry.GetView<IterPos, IterVel>();
        
        // Modify values during iteration
        for (auto [e, pos, vel] : view)
        {
            pos.x += vel.dx;
            pos.y += vel.dy;
            pos.z += vel.dz;
        }
        
        // Check modifications persisted
        for (auto [e, pos, vel] : view)
        {
            float expectedX = originalValues[e] + 1.0f;
            REQUIRE(pos.x == expectedX);
            REQUIRE(pos.y == 2.0f);
            REQUIRE(pos.z == 3.0f);
        }
    }
}

TEST_CASE("Streaming vs Traditional Iteration", "[Registry][View][Iteration]")
{
    Registry registry;
    
    SECTION("Both produce same results")
    {
        // Create entities with random component combinations
        std::mt19937 rng(54321);
        std::uniform_int_distribution<int> dist(0, 100);
        
        for (int i = 0; i < 1000; ++i)
        {
            auto e = registry.CreateEntity();
            
            if (dist(rng) < 80)
                registry.AddComponent<IterPos>(e, float(i), float(i * 2), 0.0f);
            
            if (dist(rng) < 60)
                registry.AddComponent<IterVel>(e, float(i * 3), float(i * 4), 0.0f);
            
            if (dist(rng) < 40)
                registry.AddComponent<IterHealth>(e, i, i * 10);
        }
        
        auto view = registry.GetView<IterPos, IterVel, IterHealth>();
        
        // Collect results from streaming iteration
        std::vector<std::tuple<Entity, float, float, int>> streamResults;
        for (auto [e, pos, vel, health] : view)
        {
            streamResults.push_back({e, pos.x, vel.dx, health.hp});
        }
        
        // Collect results from traditional iteration
        std::vector<std::tuple<Entity, float, float, int>> tradResults;
        for (auto it = view.begin_traditional(); it != view.end_traditional(); ++it)
        {
            auto [e, pos, vel, health] = *it;
            tradResults.push_back({e, pos.x, vel.dx, health.hp});
        }
        
        // Should have same count
        REQUIRE(streamResults.size() == tradResults.size());
        
        // Sort both to compare (order might differ)
        std::sort(streamResults.begin(), streamResults.end());
        std::sort(tradResults.begin(), tradResults.end());
        
        // Should have exact same results
        REQUIRE(streamResults == tradResults);
    }
}

TEST_CASE("Edge Cases", "[Registry][View][Iteration]")
{
    Registry registry;
    
    SECTION("Single entity with all components")
    {
        auto e = registry.CreateEntity();
        registry.AddComponent<IterPos>(e, 1.0f, 2.0f, 3.0f);
        registry.AddComponent<IterVel>(e, 4.0f, 5.0f, 6.0f);
        registry.AddComponent<IterHealth>(e, 100, 100);
        registry.AddComponent<IterPhysics>(e, 10.0f, 0.5f);
        registry.AddComponent<IterTag>(e, 42);
        
        auto view = registry.GetView<IterPos, IterVel, IterHealth, IterPhysics, IterTag>();
        
        size_t count = 0;
        for (auto [entity, pos, vel, health, physics, tag] : view)
        {
            REQUIRE(entity == e);
            REQUIRE(pos.x == 1.0f);
            REQUIRE(vel.dx == 4.0f);
            REQUIRE(health.hp == 100);
            REQUIRE(physics.mass == 10.0f);
            REQUIRE(tag.id == 42);
            count++;
        }
        REQUIRE(count == 1);
    }
    
    SECTION("No entities match")
    {
        // Create entities but none have all required components
        for (int i = 0; i < 100; ++i)
        {
            auto e = registry.CreateEntity();
            if (i % 2 == 0)
                registry.AddComponent<IterPos>(e, float(i), 0.0f, 0.0f);
            else
                registry.AddComponent<IterVel>(e, 0.0f, float(i), 0.0f);
        }
        
        auto view = registry.GetView<IterPos, IterVel>();
        
        size_t count = 0;
        for (auto [e, pos, vel] : view)
        {
            count++;
        }
        REQUIRE(count == 0);
    }
    
    SECTION("Destroyed entities are excluded")
    {
        std::vector<Entity> entities;
        for (int i = 0; i < 20; ++i)
        {
            auto e = registry.CreateEntity();
            entities.push_back(e);
            registry.AddComponent<IterPos>(e, float(i), 0.0f, 0.0f);
            registry.AddComponent<IterVel>(e, 0.0f, float(i), 0.0f);
        }
        
        // Destroy some entities
        for (int i = 0; i < 10; ++i)
        {
            registry.DestroyEntity(entities[i]);
        }
        
        auto view = registry.GetView<IterPos, IterVel>();
        
        std::set<Entity> found;
        for (auto [e, pos, vel] : view)
        {
            found.insert(e);
            // Should not find destroyed entities
            REQUIRE(std::find(entities.begin(), entities.begin() + 10, e) == entities.begin() + 10);
        }
        
        REQUIRE(found.size() == 10);
    }
}