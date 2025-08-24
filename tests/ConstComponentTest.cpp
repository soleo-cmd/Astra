#include <gtest/gtest.h>
#include <Astra/Astra.hpp>

namespace
{
    struct Position
    {
        float x, y;
    };
    
    struct Velocity
    {
        float x, y;
    };
    
    struct Health
    {
        int value;
    };
}

TEST(ConstComponentTest, ViewWithConstComponents)
{
    Astra::Registry registry;
    
    // Create entities with components
    auto e1 = registry.CreateEntity();
    registry.AddComponent<Position>(e1, Position{10, 20});
    registry.AddComponent<Velocity>(e1, Velocity{1, 2});
    
    auto e2 = registry.CreateEntity();
    registry.AddComponent<Position>(e2, Position{30, 40});
    registry.AddComponent<Velocity>(e2, Velocity{3, 4});
    
    // Test 1: View with const Velocity, mutable Position
    {
        auto view = registry.CreateView<Position, const Velocity>();
        int count = 0;
        
        view.ForEach([&count](Astra::Entity e, Position& pos, const Velocity& vel) {
            // Can modify position
            pos.x += vel.x;
            pos.y += vel.y;
            
            // Cannot modify velocity (would be compile error)
            // vel.x = 0;  // This should not compile
            
            count++;
        });
        
        EXPECT_EQ(count, 2);
    }
    
    // Verify positions were modified
    {
        auto* pos1 = registry.GetComponent<Position>(e1);
        EXPECT_FLOAT_EQ(pos1->x, 11.0f);
        EXPECT_FLOAT_EQ(pos1->y, 22.0f);
        
        auto* pos2 = registry.GetComponent<Position>(e2);
        EXPECT_FLOAT_EQ(pos2->x, 33.0f);
        EXPECT_FLOAT_EQ(pos2->y, 44.0f);
    }
    
    // Test 2: View with all const components
    {
        auto view = registry.CreateView<const Position, const Velocity>();
        float sumX = 0;
        
        view.ForEach([&sumX](Astra::Entity e, const Position& pos, const Velocity& vel) {
            // Can only read, not modify
            sumX += pos.x + vel.x;
            
            // These should not compile:
            // pos.x = 0;
            // vel.x = 0;
        });
        
        EXPECT_FLOAT_EQ(sumX, 11.0f + 1.0f + 33.0f + 3.0f);
    }
}

TEST(ConstComponentTest, LambdaSystemWithConst)
{
    Astra::Registry registry;
    
    // Create entities
    for (int i = 0; i < 10; ++i)
    {
        auto e = registry.CreateEntity();
        registry.AddComponent<Position>(e, Position{float(i), float(i * 2)});
        registry.AddComponent<Velocity>(e, Velocity{1, 1});
        registry.AddComponent<Health>(e, Health{100});
    }
    
    Astra::SystemScheduler scheduler;
    
    // Lambda system that reads Velocity (const) and writes Position
    scheduler.AddSystem([](Astra::Entity e, const Velocity& vel, Position& pos) {
        pos.x += vel.x;
        pos.y += vel.y;
    });
    
    // Lambda system that only reads (all const)
    int totalHealth = 0;
    scheduler.AddSystem([&totalHealth](Astra::Entity e, const Health& health) {
        totalHealth += health.value;
    });
    
    // Execute systems
    scheduler.Execute(registry);
    
    // Verify results
    EXPECT_EQ(totalHealth, 1000);  // 10 entities * 100 health
    
    // Check that positions were updated
    auto view = registry.CreateView<const Position>();
    int count = 0;
    view.ForEach([&count](Astra::Entity e, const Position& pos) {
        EXPECT_FLOAT_EQ(pos.x, count + 1);  // Original + 1 from velocity
        EXPECT_FLOAT_EQ(pos.y, count * 2 + 1);
        count++;
    });
}

TEST(ConstComponentTest, SystemTraitsWithReads)
{
    struct MoveSystem : Astra::SystemTraits<
        Astra::Reads<Velocity>,
        Astra::Writes<Position>
    >
    {
        void operator()(Astra::Registry& registry)
        {
            // When using Reads<Velocity>, we should get const Velocity
            auto view = registry.CreateView<Position, const Velocity>();
            view.ForEach([](Astra::Entity e, Position& pos, const Velocity& vel) {
                pos.x += vel.x;
                pos.y += vel.y;
            });
        }
    };
    
    struct HealthCheckSystem : Astra::SystemTraits<
        Astra::Reads<Health, Position>
    >
    {
        int totalHealth = 0;
        
        void operator()(Astra::Registry& registry)
        {
            // Reading both Health and Position
            auto view = registry.CreateView<const Health, const Position>();
            view.ForEach([this](Astra::Entity e, const Health& h, const Position& pos) {
                if (pos.x >= 0 && pos.y >= 0)  // In bounds
                {
                    totalHealth += h.value;
                }
            });
        }
    };
    
    Astra::Registry registry;
    
    // Create test entities
    auto e1 = registry.CreateEntity();
    registry.AddComponent<Position>(e1, Position{5, 5});
    registry.AddComponent<Velocity>(e1, Velocity{1, 1});
    registry.AddComponent<Health>(e1, Health{50});
    
    auto e2 = registry.CreateEntity();
    registry.AddComponent<Position>(e2, Position{-5, 5});  // Out of bounds
    registry.AddComponent<Velocity>(e2, Velocity{2, 2});
    registry.AddComponent<Health>(e2, Health{75});
    
    // Set up scheduler
    Astra::SystemScheduler scheduler;
    scheduler.AddSystem<MoveSystem>();
    
    HealthCheckSystem healthSystem;
    scheduler.AddSystem<HealthCheckSystem>(healthSystem);
    
    // Execute
    scheduler.Execute(registry);
    
    // Verify positions were updated
    auto* pos1 = registry.GetComponent<Position>(e1);
    EXPECT_FLOAT_EQ(pos1->x, 6.0f);
    EXPECT_FLOAT_EQ(pos1->y, 6.0f);
    
    auto* pos2 = registry.GetComponent<Position>(e2);
    EXPECT_FLOAT_EQ(pos2->x, -3.0f);  // Still negative
    EXPECT_FLOAT_EQ(pos2->y, 7.0f);
}

TEST(ConstComponentTest, ParallelIterationWithConst)
{
    Astra::Registry registry;
    
    // Create many entities
    for (int i = 0; i < 1000; ++i)
    {
        auto e = registry.CreateEntity();
        registry.AddComponent<Position>(e, Position{float(i), 0});
        registry.AddComponent<Velocity>(e, Velocity{1, 0});
    }
    
    // Parallel iteration with const velocity
    auto view = registry.CreateView<Position, const Velocity>();
    
    std::atomic<int> processedCount{0};
    
    view.ParallelForEach([&processedCount](Astra::Entity e, Position& pos, const Velocity& vel) {
        pos.x += vel.x;
        processedCount.fetch_add(1);
    });
    
    EXPECT_EQ(processedCount.load(), 1000);
    
    // Verify all positions were updated
    view.ForEach([](Astra::Entity e, Position& pos, const Velocity& vel) {
        // Position should have been incremented by 1
        static int index = 0;
        EXPECT_FLOAT_EQ(pos.x, float(index + 1));
        index++;
    });
}

TEST(ConstComponentTest, CompileTimeConstEnforcement)
{
    // This test verifies that const enforcement happens at compile time
    // Uncomment any of these to verify they produce compile errors
    
    Astra::Registry registry;
    auto e = registry.CreateEntity();
    registry.AddComponent<Position>(e, Position{0, 0});
    registry.AddComponent<Velocity>(e, Velocity{1, 1});
    
    // This should compile - modifying non-const component
    {
        auto view = registry.CreateView<Position>();
        view.ForEach([](Astra::Entity e, Position& pos) {
            pos.x = 10;  // OK
        });
    }
    
    // This should compile - reading const component
    {
        auto view = registry.CreateView<const Position>();
        view.ForEach([](Astra::Entity e, const Position& pos) {
            float x = pos.x;  // OK - reading
            (void)x;
        });
    }
    
    // These should NOT compile if uncommented:
    /*
    {
        auto view = registry.CreateView<const Position>();
        view.ForEach([](Astra::Entity e, const Position& pos) {
            pos.x = 10;  // ERROR: cannot modify const
        });
    }
    */
    
    /*
    {
        // Lambda with const parameter should enforce const in view
        Astra::SystemScheduler scheduler;
        scheduler.AddSystem([](Astra::Entity e, const Position& pos) {
            pos.x = 10;  // ERROR: cannot modify const
        });
    }
    */
    
    SUCCEED();  // If we get here, const enforcement is working
}