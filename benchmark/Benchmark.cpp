#include <Astra/Astra.hpp>
#include <Astra/System/System.hpp>
#include <Astra/System/SystemScheduler.hpp>
#include <Astra/System/SystemExecutor.hpp>
#include <benchmark/benchmark.h>
#include <cstdint>
#include <queue>
#include <vector>
#include <thread>

struct Position
{
    std::uint64_t x;
    std::uint64_t y;
};

struct Velocity : Position {};

struct StablePosition : Position
{
};

template<int N>
struct Comp
{
    int x;
};


static void BM_CreateEntities(benchmark::State& state)
{
    const size_t count = state.range(0);
    
    for(auto _ : state)
    {
        Astra::Registry registry;
        for(size_t i = 0; i < count; ++i)
        {
            benchmark::DoNotOptimize(registry.CreateEntity());
        }
    }
    
    state.SetItemsProcessed(state.iterations() * count);
}

static void BM_CreateEntitiesBatch(benchmark::State& state)
{
    const size_t count = state.range(0);
    std::vector<Astra::Entity> entities(count);
    
    for(auto _ : state)
    {
        Astra::Registry registry;
        registry.CreateEntitiesWith(count, entities, [](size_t)
        { 
            return std::tuple<>(); 
        });
    }
    
    state.SetItemsProcessed(state.iterations() * count);
}

// Component manipulation benchmarks
static void BM_AddComponents(benchmark::State& state)
{
    const size_t count = state.range(0);
    
    for(auto _ : state)
    {
        state.PauseTiming();
        Astra::Registry registry;
        std::vector<Astra::Entity> entities;
        for(size_t i = 0; i < count; ++i)
        {
            entities.push_back(registry.CreateEntity());
        }
        state.ResumeTiming();
        
        for(auto entity : entities) {
            registry.AddComponent<Position>(entity);
            registry.AddComponent<Velocity>(entity);
        }
    }
    
    state.SetItemsProcessed(state.iterations() * count * 2);
}

static void BM_RemoveComponents(benchmark::State& state) {
    const size_t count = state.range(0);
    
    for(auto _ : state) {
        state.PauseTiming();
        Astra::Registry registry;
        std::vector<Astra::Entity> entities;
        for(size_t i = 0; i < count; i++) {
            auto e = registry.CreateEntity();
            registry.AddComponent<Position>(e);
            entities.push_back(e);
        }
        state.ResumeTiming();
        
        for(auto entity : entities) {
            registry.RemoveComponent<Position>(entity);
        }
    }
    
    state.SetItemsProcessed(state.iterations() * count);
}

// Batch component manipulation benchmarks
static void BM_AddComponentsBatch(benchmark::State& state) {
    const size_t count = state.range(0);
    
    for(auto _ : state) {
        state.PauseTiming();
        Astra::Registry registry;
        std::vector<Astra::Entity> entities;
        entities.reserve(count);
        for(size_t i = 0; i < count; ++i) {
            entities.push_back(registry.CreateEntity());
        }
        state.ResumeTiming();
        
        // Batch add Position component to all entities
        registry.AddComponents<Position>(entities, Position{42, 42});
        
        // Batch add Velocity component to all entities
        registry.AddComponents<Velocity>(entities, Velocity{10, 10});
    }
    
    state.SetItemsProcessed(state.iterations() * count * 2);
}

static void BM_RemoveComponentsBatch(benchmark::State& state) {
    const size_t count = state.range(0);
    
    for(auto _ : state) {
        state.PauseTiming();
        Astra::Registry registry;
        std::vector<Astra::Entity> entities;
        entities.reserve(count);
        for(size_t i = 0; i < count; ++i) {
            auto e = registry.CreateEntity();
            registry.AddComponent<Position>(e, Position{42, 42});
            entities.push_back(e);
        }
        state.ResumeTiming();
        
        // Batch remove Position component from all entities
        size_t removed = registry.RemoveComponents<Position>(entities);
        benchmark::DoNotOptimize(removed);
    }
    
    state.SetItemsProcessed(state.iterations() * count);
}

// Iteration benchmarks - most important!
static void BM_IterateSingleComponent(benchmark::State& state) {
    const size_t count = state.range(0);
    Astra::Registry registry;
    
    // Setup entities
    for(size_t i = 0; i < count; i++) {
        auto entity = registry.CreateEntity();
        registry.AddComponent<Position>(entity);
    }
    
    // Create view once before benchmarking
    auto view = registry.CreateView<Position>();
    
    for(auto _ : state) {
        view.ForEach([](Astra::Entity, Position& pos) {
            benchmark::DoNotOptimize(pos.x = 0);
        });
    }
    
    state.SetItemsProcessed(state.iterations() * count);
}

static void BM_IterateTwoComponents(benchmark::State& state) {
    const size_t count = state.range(0);
    Astra::Registry registry;
    
    // Setup entities
    for(size_t i = 0; i < count; i++) {
        auto entity = registry.CreateEntity();
        registry.AddComponent<Position>(entity);
        registry.AddComponent<Velocity>(entity);
    }
    
    // Create view once before benchmarking
    auto view = registry.CreateView<Position, Velocity>();
    
    for(auto _ : state) {
        view.ForEach([](Astra::Entity, Position& pos, Velocity& vel) {
            benchmark::DoNotOptimize(pos.x = 0);
            benchmark::DoNotOptimize(vel.x = 0);
        });
    }
    
    state.SetItemsProcessed(state.iterations() * count);
}

static void BM_IterateTwoComponentsHalf(benchmark::State& state) {
    const size_t count = state.range(0);
    Astra::Registry registry;
    
    // Setup entities - only half have Position
    for(size_t i = 0; i < count; i++) {
        auto entity = registry.CreateEntity();
        registry.AddComponent<Velocity>(entity);
        
        if(i % 2) {
            registry.AddComponent<Position>(entity);
        }
    }
    
    // Create view once before benchmarking
    auto view = registry.CreateView<Position, Velocity>();
    
    size_t matched = 0;
    for(auto _ : state) {
        matched = 0;
        view.ForEach([&matched](Astra::Entity, Position& pos, Velocity& vel) {
            benchmark::DoNotOptimize(pos.x = 0);
            benchmark::DoNotOptimize(vel.x = 0);
            matched++;
        });
    }
    
    state.SetItemsProcessed(state.iterations() * matched);
}

static void BM_IterateTwoComponentsOne(benchmark::State& state) {
    const size_t count = state.range(0);
    Astra::Registry registry;
    
    // Setup entities - only one has Position
    for(size_t i = 0; i < count; i++) {
        auto entity = registry.CreateEntity();
        registry.AddComponent<Velocity>(entity);
        
        if(i == count / 2) {
            registry.AddComponent<Position>(entity);
        }
    }
    
    // Create view once before benchmarking
    auto view = registry.CreateView<Position, Velocity>();
    
    for(auto _ : state) {
        view.ForEach([](Astra::Entity, Position& pos, Velocity& vel) {
            benchmark::DoNotOptimize(pos.x = 0);
            benchmark::DoNotOptimize(vel.x = 0);
        });
    }
    
    state.SetItemsProcessed(state.iterations() * 1);
}

static void BM_IterateThreeComponents(benchmark::State& state) {
    const size_t count = state.range(0);
    Astra::Registry registry;
    
    // Setup entities
    for(size_t i = 0; i < count; i++) {
        auto entity = registry.CreateEntity();
        registry.AddComponent<Position>(entity);
        registry.AddComponent<Velocity>(entity);
        registry.AddComponent<Comp<0>>(entity);
    }
    
    // Create view once before benchmarking
    auto view = registry.CreateView<Position, Velocity, Comp<0>>();
    
    for(auto _ : state) {
        view.ForEach([](Astra::Entity, Position& pos, Velocity& vel, Comp<0>& c) {
            benchmark::DoNotOptimize(pos.x = 0);
            benchmark::DoNotOptimize(vel.x = 0);
            benchmark::DoNotOptimize(c.x = 0);
        });
    }
    
    state.SetItemsProcessed(state.iterations() * count);
}

static void BM_IterateFiveComponents(benchmark::State& state) {
    const size_t count = state.range(0);
    Astra::Registry registry;
    
    // Setup entities
    for(size_t i = 0; i < count; i++) {
        auto entity = registry.CreateEntity();
        registry.AddComponent<Position>(entity);
        registry.AddComponent<Velocity>(entity);
        registry.AddComponent<Comp<0>>(entity);
        registry.AddComponent<Comp<1>>(entity);
        registry.AddComponent<Comp<2>>(entity);
    }
    
    // Create view once before benchmarking
    auto view = registry.CreateView<Position, Velocity, Comp<0>, Comp<1>, Comp<2>>();
    
    for(auto _ : state) {
        view.ForEach([](Astra::Entity, Position& pos, Velocity& vel, 
                       Comp<0>& c0, Comp<1>& c1, Comp<2>& c2) {
            benchmark::DoNotOptimize(pos.x = 0);
            benchmark::DoNotOptimize(vel.x = 0);
            benchmark::DoNotOptimize(c0.x = 0);
            benchmark::DoNotOptimize(c1.x = 0);
            benchmark::DoNotOptimize(c2.x = 0);
        });
    }
    
    state.SetItemsProcessed(state.iterations() * count);
}

// Random access benchmarks
// Parallel iteration benchmarks

// Helper benchmark to show thread configuration
static void BM_ThreadInfo(benchmark::State& state) {
    for(auto _ : state) {
        benchmark::DoNotOptimize(std::thread::hardware_concurrency());
    }
    state.SetLabel("Hardware Threads: " + std::to_string(std::thread::hardware_concurrency()));
}

static void BM_ParallelIterateSingleComponent(benchmark::State& state) {
    const size_t count = state.range(0);
    Astra::Registry registry;
    
    // Setup entities
    for(size_t i = 0; i < count; i++) {
        auto entity = registry.CreateEntity();
        registry.AddComponent<Position>(entity);
    }
    
    // Create view once before benchmarking
    auto view = registry.CreateView<Position>();
    
    for(auto _ : state) {
        view.ParallelForEach([](Astra::Entity, Position& pos) {
            benchmark::DoNotOptimize(pos.x = 0);
        });
    }
    
    state.SetItemsProcessed(state.iterations() * count);
}

static void BM_ParallelIterateTwoComponents(benchmark::State& state) {
    const size_t count = state.range(0);
    Astra::Registry registry;
    
    // Setup entities
    for(size_t i = 0; i < count; i++) {
        auto entity = registry.CreateEntity();
        registry.AddComponent<Position>(entity);
        registry.AddComponent<Velocity>(entity);
    }
    
    // Create view once before benchmarking
    auto view = registry.CreateView<Position, Velocity>();
    
    for(auto _ : state) {
        view.ParallelForEach([](Astra::Entity, Position& pos, Velocity& vel) {
            benchmark::DoNotOptimize(pos.x = 0);
            benchmark::DoNotOptimize(vel.x = 0);
        });
    }
    
    state.SetItemsProcessed(state.iterations() * count);
}

static void BM_ParallelIterateTwoComponentsHalf(benchmark::State& state) {
    const size_t count = state.range(0);
    Astra::Registry registry;
    
    // Setup entities - only half have Position
    for(size_t i = 0; i < count; i++) {
        auto entity = registry.CreateEntity();
        registry.AddComponent<Velocity>(entity);
        
        if(i % 2) {
            registry.AddComponent<Position>(entity);
        }
    }
    
    // Create view once before benchmarking
    auto view = registry.CreateView<Position, Velocity>();
    
    size_t matched = count / 2;  // Approximately half match
    for(auto _ : state) {
        view.ParallelForEach([](Astra::Entity, Position& pos, Velocity& vel) {
            benchmark::DoNotOptimize(pos.x = 0);
            benchmark::DoNotOptimize(vel.x = 0);
        });
    }
    
    state.SetItemsProcessed(state.iterations() * matched);
}

static void BM_ParallelIterateTwoComponentsOne(benchmark::State& state) {
    const size_t count = state.range(0);
    Astra::Registry registry;
    
    // Setup entities - only one has Position
    for(size_t i = 0; i < count; i++) {
        auto entity = registry.CreateEntity();
        registry.AddComponent<Velocity>(entity);
        
        if(i == count / 2) {
            registry.AddComponent<Position>(entity);
        }
    }
    
    // Create view once before benchmarking
    auto view = registry.CreateView<Position, Velocity>();
    
    for(auto _ : state) {
        view.ParallelForEach([](Astra::Entity, Position& pos, Velocity& vel) {
            benchmark::DoNotOptimize(pos.x = 0);
            benchmark::DoNotOptimize(vel.x = 0);
        });
    }
    
    state.SetItemsProcessed(state.iterations() * 1);
}

static void BM_ParallelIterateThreeComponents(benchmark::State& state) {
    const size_t count = state.range(0);
    Astra::Registry registry;
    
    // Setup entities
    for(size_t i = 0; i < count; i++) {
        auto entity = registry.CreateEntity();
        registry.AddComponent<Position>(entity);
        registry.AddComponent<Velocity>(entity);
        registry.AddComponent<Comp<0>>(entity);
    }
    
    // Create view once before benchmarking
    auto view = registry.CreateView<Position, Velocity, Comp<0>>();
    
    for(auto _ : state) {
        view.ParallelForEach([](Astra::Entity, Position& pos, Velocity& vel, Comp<0>& c) {
            benchmark::DoNotOptimize(pos.x = 0);
            benchmark::DoNotOptimize(vel.x = 0);
            benchmark::DoNotOptimize(c.x = 0);
        });
    }
    
    state.SetItemsProcessed(state.iterations() * count);
}

static void BM_ParallelIterateFiveComponents(benchmark::State& state) {
    const size_t count = state.range(0);
    Astra::Registry registry;
    
    // Setup entities
    for(size_t i = 0; i < count; i++) {
        auto entity = registry.CreateEntity();
        registry.AddComponent<Position>(entity);
        registry.AddComponent<Velocity>(entity);
        registry.AddComponent<Comp<0>>(entity);
        registry.AddComponent<Comp<1>>(entity);
        registry.AddComponent<Comp<2>>(entity);
    }
    
    // Create view once before benchmarking
    auto view = registry.CreateView<Position, Velocity, Comp<0>, Comp<1>, Comp<2>>();
    
    for(auto _ : state) {
        view.ParallelForEach([](Astra::Entity, Position& pos, Velocity& vel, Comp<0>& c0, Comp<1>& c1, Comp<2>& c2) {
            benchmark::DoNotOptimize(pos.x = 0);
            benchmark::DoNotOptimize(vel.x = 0);
            benchmark::DoNotOptimize(c0.x = 0);
            benchmark::DoNotOptimize(c1.x = 0);
            benchmark::DoNotOptimize(c2.x = 0);
        });
    }
    
    state.SetItemsProcessed(state.iterations() * count);
}

static void BM_GetComponent(benchmark::State& state) {
    const size_t count = state.range(0);
    Astra::Registry registry;
    std::vector<Astra::Entity> entities;
    
    // Setup entities
    for(size_t i = 0; i < count; i++) {
        auto entity = registry.CreateEntity();
        registry.AddComponent<Position>(entity);
        entities.push_back(entity);
    }
    
    for(auto _ : state) {
        for(auto entity : entities) {
            auto* pos = registry.GetComponent<Position>(entity);
            benchmark::DoNotOptimize(pos->x = 0);
        }
    }
    
    state.SetItemsProcessed(state.iterations() * count);
}

static void BM_GetMultipleComponents(benchmark::State& state) {
    const size_t count = state.range(0);
    Astra::Registry registry;
    std::vector<Astra::Entity> entities;
    
    // Setup entities
    for(size_t i = 0; i < count; i++) {
        auto entity = registry.CreateEntity();
        registry.AddComponent<Position>(entity);
        registry.AddComponent<Velocity>(entity);
        entities.push_back(entity);
    }
    
    for(auto _ : state) {
        for(auto entity : entities) {
            auto* pos = registry.GetComponent<Position>(entity);
            auto* vel = registry.GetComponent<Velocity>(entity);
            benchmark::DoNotOptimize(pos->x = 0);
            benchmark::DoNotOptimize(vel->y = 0);
        }
    }
    
    state.SetItemsProcessed(state.iterations() * count * 2);
}

// Hierarchy/Relations benchmarks
static void BM_HierarchyTraversal(benchmark::State& state) {
    const size_t targetCount = state.range(0);
    
    // Calculate tree structure to get approximately targetCount entities
    // For a balanced tree: total = (branching^(depth+1) - 1) / (branching - 1)
    size_t depth = 4;
    size_t branching = 4;
    
    // Adjust branching factor based on target size
    if (targetCount <= 100) {
        depth = 3;
        branching = 3;
    } else if (targetCount <= 10000) {
        depth = 5;
        branching = 4;
    } else if (targetCount <= 100000) {
        depth = 6;
        branching = 6;
    } else {
        depth = 7;
        branching = 8;
    }
    
    Astra::Registry registry;
    std::vector<Astra::Entity> allEntities;
    
    // Build a tree hierarchy
    std::queue<std::pair<Astra::Entity, size_t>> toProcess;
    auto root = registry.CreateEntity();
    registry.AddComponent<Position>(root);
    allEntities.push_back(root);
    toProcess.push({root, 0});
    
    while (!toProcess.empty()) {
        auto [parent, currentDepth] = toProcess.front();
        toProcess.pop();
        
        if (currentDepth < depth) {
            for (size_t i = 0; i < branching; i++) {
                auto child = registry.CreateEntity();
                registry.AddComponent<Position>(child);
                registry.SetParent(child, parent);
                allEntities.push_back(child);
                toProcess.push({child, currentDepth + 1});
            }
        }
    }
    
    // Benchmark descendant traversal
    for(auto _ : state) {
        auto relations = registry.GetRelations<Position>(root);
        size_t count = 0;
        for (auto [entity, depth] : relations.GetDescendants()) {
            benchmark::DoNotOptimize(count++);
        }
    }
    
    state.SetItemsProcessed(state.iterations() * allEntities.size());
}

static void BM_HierarchyForEach(benchmark::State& state) {
    const size_t targetCount = state.range(0);
    
    // Calculate tree structure to get approximately targetCount entities
    size_t depth = 4;
    size_t branching = 4;
    
    // Adjust branching factor based on target size
    if (targetCount <= 100) {
        depth = 3;
        branching = 3;
    } else if (targetCount <= 10000) {
        depth = 5;
        branching = 4;
    } else if (targetCount <= 100000) {
        depth = 6;
        branching = 6;
    } else {
        depth = 7;
        branching = 8;
    }
    
    Astra::Registry registry;
    std::vector<Astra::Entity> allEntities;
    
    // Build a tree hierarchy with Position component
    std::queue<std::pair<Astra::Entity, size_t>> toProcess;
    auto root = registry.CreateEntity();
    registry.AddComponent<Position>(root, Position{0, 0});
    allEntities.push_back(root);
    toProcess.push({root, 0});
    
    while (!toProcess.empty()) {
        auto [parent, currentDepth] = toProcess.front();
        toProcess.pop();
        
        if (currentDepth < depth) {
            for (size_t i = 0; i < branching; i++) {
                auto child = registry.CreateEntity();
                registry.AddComponent<Position>(child, Position{static_cast<std::uint64_t>(i), 0});
                registry.SetParent(child, parent);
                allEntities.push_back(child);
                toProcess.push({child, currentDepth + 1});
            }
        }
    }
    
    // Benchmark ForEachDescendant with component access
    for(auto _ : state) {
        auto relations = registry.GetRelations<Position>(root);
        relations.ForEachDescendant([](Astra::Entity e, size_t depth, Position& pos) {
            benchmark::DoNotOptimize(pos.x += 1);
        });
    }
    
    state.SetItemsProcessed(state.iterations() * allEntities.size());
}

static void BM_FilteredHierarchyTraversal(benchmark::State& state) {
    const size_t targetCount = state.range(0);
    
    // Calculate tree structure to get approximately targetCount entities
    size_t depth = 4;
    size_t branching = 4;
    
    // Adjust branching factor based on target size
    if (targetCount <= 100) {
        depth = 3;
        branching = 3;
    } else if (targetCount <= 10000) {
        depth = 5;
        branching = 4;
    } else if (targetCount <= 100000) {
        depth = 6;
        branching = 6;
    } else {
        depth = 7;
        branching = 8;
    }
    
    Astra::Registry registry;
    std::vector<Astra::Entity> allEntities;
    
    // Build a tree hierarchy where only half have Velocity
    std::queue<std::pair<Astra::Entity, size_t>> toProcess;
    auto root = registry.CreateEntity();
    registry.AddComponent<Position>(root);
    registry.AddComponent<Velocity>(root);  // Root has velocity
    allEntities.push_back(root);
    toProcess.push({root, 0});
    
    size_t entityCount = 0;
    while (!toProcess.empty()) {
        auto [parent, currentDepth] = toProcess.front();
        toProcess.pop();
        
        if (currentDepth < depth) {
            for (size_t i = 0; i < branching; i++) {
                auto child = registry.CreateEntity();
                registry.AddComponent<Position>(child);
                // Only add Velocity to every other entity
                if (entityCount++ % 2 == 0) {
                    registry.AddComponent<Velocity>(child);
                }
                registry.SetParent(child, parent);
                allEntities.push_back(child);
                toProcess.push({child, currentDepth + 1});
            }
        }
    }
    
    // Benchmark filtered descendant traversal (only entities with Velocity)
    for(auto _ : state) {
        auto relations = registry.GetRelations<Position, Velocity>(root);
        size_t count = 0;
        for (auto [entity, depth] : relations.GetDescendants()) {
            benchmark::DoNotOptimize(count++);
        }
    }
    
    state.SetItemsProcessed(state.iterations() * (allEntities.size() / 2));  // About half have Velocity
}

struct MoveSystem : Astra::SystemTraits<
    Astra::Reads<Velocity>,
    Astra::Writes<Position>
>
{
    void operator()(Astra::Registry& registry)
    {
        auto view = registry.CreateView<Position, Velocity>();
        view.ForEach([](Astra::Entity, Position& pos, Velocity& vel)
            {
                pos.x += vel.x;
                pos.y += vel.y;
            });
    }
};

struct BoundsCheckSystem : Astra::SystemTraits<
    Astra::Reads<Position>,
    Astra::Writes<Position>
>
{
    void operator()(Astra::Registry& registry)
    {
        auto view = registry.CreateView<Position>();
        view.ForEach([](Astra::Entity, Position& pos)
            {
                if (pos.x > 1000) pos.x = 0;
                if (pos.y > 1000) pos.y = 0;
            });
    }
};

struct SpecialProcessingSystem : Astra::SystemTraits<Astra::Reads<Position>, Astra::Writes<Comp<0>>>
{
    void operator()(Astra::Registry& registry)
    {
        auto view = registry.CreateView<Position, Comp<0>>();
        view.ForEach([](Astra::Entity, Position& pos, Comp<0>& c)
        {
            c.x = static_cast<int>(pos.x + pos.y);
        });
    }
};

static void BM_SystemScheduler_Sequential(benchmark::State& state)
{
    const size_t count = state.range(0);
    Astra::Registry registry;

    // Setup entities
    for(size_t i = 0; i < count; i++)
    {
        auto entity = registry.CreateEntity();
        registry.AddComponent<Position>(entity, Position{i, i});
        registry.AddComponent<Velocity>(entity, Velocity{1, 1});
        if (i % 3 == 0)
        {
            registry.AddComponent<Comp<0>>(entity);
        }
    }

    // Create system scheduler
    Astra::SystemScheduler scheduler;

    // Add systems - these will run sequentially without hints
    scheduler.AddSystem<MoveSystem>();
    scheduler.AddSystem<BoundsCheckSystem>();
    scheduler.AddSystem<SpecialProcessingSystem>();

    for(auto _ : state)
    {
        scheduler.Execute(registry);
    }

    state.SetItemsProcessed(state.iterations() * count * 3);  // 3 systems
}

static void BM_SystemScheduler_Lambda(benchmark::State& state)
{
    const size_t count = state.range(0);
    Astra::Registry registry;

    // Setup entities
    for(size_t i = 0; i < count; i++)
    {
        auto entity = registry.CreateEntity();
        registry.AddComponent<Position>(entity, Position{i, i});
        registry.AddComponent<Velocity>(entity, Velocity{1, 1});
    }

    // Create system scheduler with lambda systems
    Astra::SystemScheduler scheduler;

    // Add lambda systems - component access auto-deduced from const-ness
    scheduler.AddSystem([](Astra::Entity e, const Velocity& vel, Position& pos)
    {
        pos.x += vel.x;
        pos.y += vel.y;
    });  // Auto-detects: Reads<Velocity>, Writes<Position>

    scheduler.AddSystem([](Astra::Entity e, Position& pos)
    {
        if (pos.x > 1000) pos.x = 0;
        if (pos.y > 1000) pos.y = 0;
    });  // Auto-detects: Writes<Position>

    for(auto _ : state)
    {
        scheduler.Execute(registry);
    }

    state.SetItemsProcessed(state.iterations() * count * 2);  // 2 systems
}

// Additional systems for parallel execution benchmarks
struct PhysicsSystem : Astra::SystemTraits<Astra::Reads<Velocity>, Astra::Writes<Position>>
{
    void operator()(Astra::Registry& registry)
    {
        auto view = registry.CreateView<Position, Velocity>();
        view.ForEach([](Astra::Entity, Position& pos, Velocity& vel)
        {
            pos.x += vel.x;
            pos.y += vel.y;
        });
    }
};

struct Comp0ProcessingSystem : Astra::SystemTraits<
    Astra::Writes<Comp<0>>
>
{
    void operator()(Astra::Registry& registry)
    {
        auto view = registry.CreateView<Comp<0>>();
        view.ForEach([](Astra::Entity, Comp<0>& c)
        {
            c.x = c.x * 2 + 1;
        });
    }
};

struct Comp1ProcessingSystem : Astra::SystemTraits<
    Astra::Writes<Comp<1>>
>
{
    void operator()(Astra::Registry& registry)
    {
        auto view = registry.CreateView<Comp<1>>();
        view.ForEach([](Astra::Entity, Comp<1>& c)
        {
            c.x = c.x * 3 - 1;
        });
    }
};

struct Comp2ProcessingSystem : Astra::SystemTraits<Astra::Writes<Comp<2>>>
{
    void operator()(Astra::Registry& registry)
    {
        auto view = registry.CreateView<Comp<2>>();
        view.ForEach([](Astra::Entity, Comp<2>& c)
        {
            c.x = c.x * 4 + 2;
        });
    }
};

static void BM_SystemScheduler_Parallel(benchmark::State& state)
{
    const size_t count = state.range(0);
    Astra::Registry registry;

    // Setup entities
    for(size_t i = 0; i < count; i++)
    {
        auto entity = registry.CreateEntity();
        registry.AddComponent<Position>(entity, Position{i, i});
        registry.AddComponent<Velocity>(entity, Velocity{1, 1});
        if (i % 2 == 0)
        {
            registry.AddComponent<Comp<0>>(entity);
        }
        if (i % 3 == 0)
        {
            registry.AddComponent<Comp<1>>(entity);
        }
    }

    // Create system scheduler - component dependencies are automatically detected
    Astra::SystemScheduler scheduler;

    // Add independent systems that can run in parallel
    scheduler.AddSystem<PhysicsSystem>();      // Auto-detects: Reads<Velocity>, Writes<Position>
    scheduler.AddSystem<Comp0ProcessingSystem>(); // Auto-detects: Writes<Comp<0>>
    scheduler.AddSystem<Comp1ProcessingSystem>(); // Auto-detects: Writes<Comp<1>>

    // This system depends on Position from Physics
    scheduler.AddSystem<BoundsCheckSystem>();  // Auto-detects: Reads<Position>, Writes<Position>

    for(auto _ : state)
    {
        scheduler.Execute(registry);
    }

    state.SetItemsProcessed(state.iterations() * count * 4);  // 4 systems
}

// Systems for dependency chain benchmarks
template<int N>
struct ChainSystem : Astra::SystemTraits<Astra::Reads<Position>, Astra::Writes<Position>>
{
    void operator()(Astra::Registry& registry)
    {
        auto view = registry.CreateView<Position>();
        view.ForEach([](Astra::Entity, Position& pos)
        {
            benchmark::DoNotOptimize(pos.x += N);
        });
    }
};

static void BM_SystemScheduler_ManyIndependent(benchmark::State& state)
{
    Astra::Registry registry;

    // Create entities with various components
    for(size_t i = 0; i < 10000; i++)
    {
        auto entity = registry.CreateEntity();
        registry.AddComponent<Position>(entity);
        if (i % 2 == 0) registry.AddComponent<Velocity>(entity);
        if (i % 3 == 0) registry.AddComponent<Comp<0>>(entity);
        if (i % 4 == 0) registry.AddComponent<Comp<1>>(entity);
        if (i % 5 == 0) registry.AddComponent<Comp<2>>(entity);
    }

    // Create scheduler with fixed set of independent systems
    Astra::SystemScheduler scheduler;

    // Add multiple independent systems - dependencies auto-detected
    scheduler.AddSystem<MoveSystem>();          // Auto-detects: Reads<Velocity>, Writes<Position>
    scheduler.AddSystem<PhysicsSystem>();       // Auto-detects: Reads<Velocity>, Writes<Position>  
    scheduler.AddSystem<Comp0ProcessingSystem>(); // Auto-detects: Writes<Comp<0>>
    scheduler.AddSystem<Comp1ProcessingSystem>(); // Auto-detects: Writes<Comp<1>>
    scheduler.AddSystem<Comp2ProcessingSystem>(); // Auto-detects: Writes<Comp<2>>

    for(auto _ : state)
    {
        scheduler.Execute(registry);
    }

    state.SetItemsProcessed(state.iterations() * 5);  // 5 systems
}

static void BM_SystemScheduler_WithDependencies(benchmark::State& state)
{
    Astra::Registry registry;

    // Create entities
    for(size_t i = 0; i < 10000; i++)
    {
        auto entity = registry.CreateEntity();
        registry.AddComponent<Position>(entity, Position{i, i});
    }

    Astra::SystemScheduler scheduler;

    // Create a chain of dependent systems - all auto-detect Reads<Position>, Writes<Position>
    scheduler.AddSystem<ChainSystem<0>>();
    scheduler.AddSystem<ChainSystem<1>>();
    scheduler.AddSystem<ChainSystem<2>>();
    scheduler.AddSystem<ChainSystem<3>>();
    scheduler.AddSystem<ChainSystem<4>>();

    for(auto _ : state)
    {
        scheduler.Execute(registry);
    }

    state.SetItemsProcessed(state.iterations() * 5 * 10000);  // 5 systems, 10000 entities
}

// Custom executor implementation for benchmarking
struct BenchmarkExecutor : Astra::ISystemExecutor
{
    void Execute(const Astra::SystemExecutionContext& context) override
    {
        // Simple sequential execution
        for (const auto& group : context.parallelGroups)
        {
            for (size_t index : group)
            {
                context.systems[index](*context.registry);
            }
        }
    }
};

static void BM_SystemScheduler_CustomExecutor(benchmark::State& state)
{
    const size_t count = state.range(0);
    Astra::Registry registry;

    // Setup entities
    for(size_t i = 0; i < count; i++) {
        auto entity = registry.CreateEntity();
        registry.AddComponent<Position>(entity);
        registry.AddComponent<Velocity>(entity);
    }

    Astra::SystemScheduler scheduler;

    // Add systems
    scheduler.AddSystem<PhysicsSystem>();
    scheduler.AddSystem<BoundsCheckSystem>();

    // Custom executor instance
    BenchmarkExecutor customExecutor;

    for(auto _ : state) {
        scheduler.Execute(registry, &customExecutor);
    }

    state.SetItemsProcessed(state.iterations() * count * 2);
}

// Register benchmarks with common entity counts
BENCHMARK(BM_CreateEntities)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_CreateEntitiesBatch)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_AddComponents)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_RemoveComponents)->Arg(10000)->Arg(100000)->Arg(1000000);

// Batch component operations
BENCHMARK(BM_AddComponentsBatch)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_RemoveComponentsBatch)->Arg(10000)->Arg(100000)->Arg(1000000);

// Most important iteration benchmarks
BENCHMARK(BM_IterateSingleComponent)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_IterateTwoComponents)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_IterateTwoComponentsHalf)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_IterateTwoComponentsOne)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_IterateThreeComponents)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_IterateFiveComponents)->Arg(10000)->Arg(100000)->Arg(1000000);
// BENCHMARK(BM_IteratePathological);  // Temporarily disabled - causes crash

// Parallel iteration benchmarks
BENCHMARK(BM_ThreadInfo);
BENCHMARK(BM_ParallelIterateSingleComponent)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_ParallelIterateTwoComponents)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_ParallelIterateTwoComponentsHalf)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_ParallelIterateTwoComponentsOne)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_ParallelIterateThreeComponents)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_ParallelIterateFiveComponents)->Arg(10000)->Arg(100000)->Arg(1000000);

// Random access
BENCHMARK(BM_GetComponent)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_GetMultipleComponents)->Arg(10000)->Arg(100000)->Arg(1000000);

// Hierarchy/Relations benchmarks
BENCHMARK(BM_HierarchyTraversal)->Arg(1000)->Arg(10000)->Arg(100000);
BENCHMARK(BM_HierarchyForEach)->Arg(1000)->Arg(10000)->Arg(100000);
BENCHMARK(BM_FilteredHierarchyTraversal)->Arg(1000)->Arg(10000)->Arg(100000);

// System Scheduling benchmarks
BENCHMARK(BM_SystemScheduler_Sequential)->Arg(10000)->Arg(100000);
BENCHMARK(BM_SystemScheduler_Lambda)->Arg(10000)->Arg(100000);
BENCHMARK(BM_SystemScheduler_Parallel)->Arg(10000)->Arg(100000);
BENCHMARK(BM_SystemScheduler_ManyIndependent);
BENCHMARK(BM_SystemScheduler_WithDependencies);
BENCHMARK(BM_SystemScheduler_CustomExecutor)->Arg(10000)->Arg(100000);

BENCHMARK_MAIN();
