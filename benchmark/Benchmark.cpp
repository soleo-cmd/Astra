#include <benchmark/benchmark.h>
#include <vector>
#include <cstdint>
#include <queue>
#include <Astra/Astra.hpp>

// Components matching EnTT's benchmark
struct Position {
    std::uint64_t x;
    std::uint64_t y;
};

struct Velocity : Position {};

struct StablePosition : Position {
    // In Astra, we don't have in_place_delete concept
};

template<int N>
struct Comp {
    int x;
};


// Entity creation benchmarks
static void BM_CreateEntities(benchmark::State& state) {
    const size_t count = state.range(0);
    
    for(auto _ : state) {
        Astra::Registry registry;
        for(size_t i = 0; i < count; i++) {
            benchmark::DoNotOptimize(registry.CreateEntity());
        }
    }
    
    state.SetItemsProcessed(state.iterations() * count);
}

static void BM_CreateEntitiesBatch(benchmark::State& state) {
    const size_t count = state.range(0);
    std::vector<Astra::Entity> entities(count);
    
    for(auto _ : state) {
        Astra::Registry registry;
        registry.CreateEntities(count, entities, [](size_t) { 
            return std::tuple<>(); 
        });
    }
    
    state.SetItemsProcessed(state.iterations() * count);
}

// Component manipulation benchmarks
static void BM_AddComponents(benchmark::State& state) {
    const size_t count = state.range(0);
    
    for(auto _ : state) {
        state.PauseTiming();
        Astra::Registry registry;
        std::vector<Astra::Entity> entities;
        for(size_t i = 0; i < count; i++) {
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

// Pathological case - heavily fragmented registry
static void BM_IteratePathological(benchmark::State& state) {
    Astra::Registry registry;
    
    // Create 500,000 entities with all components
    for(std::uint64_t i = 0; i < 500000L; i++) {
        auto entity = registry.CreateEntity();
        registry.AddComponent<Position>(entity);
        registry.AddComponent<Velocity>(entity);
        registry.AddComponent<Comp<0>>(entity);
    }
    
    // Fragment the registry by removing components and entities
    for(int iteration = 0; iteration < 10; ++iteration) {
        int curr = 0;
        
        // Get all entities - need to collect them first to avoid iterator invalidation
        auto allView = registry.CreateView<>();
        std::vector<Astra::Entity> entities;
        entities.reserve(600000); // Reserve space to avoid reallocation
        allView.ForEach([&entities](Astra::Entity e) {
            entities.push_back(e);
        });
        
        // Fragment by removing components
        for(auto entity : entities) {
            // Check validity first - entity might have been destroyed in previous iteration
            if(!registry.IsValid(entity)) {
                continue;
            }
            
            // Decide if we're destroying this entity
            bool shouldDestroy = !(++curr % 17);
            
            if(shouldDestroy) {
                registry.DestroyEntity(entity);
                continue; // Skip all component operations on destroyed entity
            }
            
            // Only modify components if entity won't be destroyed
            if(!(++curr % 7) && registry.HasComponent<Position>(entity)) {
                registry.RemoveComponent<Position>(entity);
            }
            
            if(!(++curr % 11) && registry.HasComponent<Velocity>(entity)) {
                registry.RemoveComponent<Velocity>(entity);
            }
            
            if(!(++curr % 13) && registry.HasComponent<Comp<0>>(entity)) {
                registry.RemoveComponent<Comp<0>>(entity);
            }
        }
        
        // Add 50,000 new entities
        for(std::uint64_t j = 0; j < 50000L; j++) {
            auto entity = registry.CreateEntity();
            registry.AddComponent<Position>(entity);
            registry.AddComponent<Velocity>(entity);
            registry.AddComponent<Comp<0>>(entity);
        }
    }
    
    // Now benchmark the iteration after fragmentation
    auto view = registry.CreateView<Position, Velocity, Comp<0>>();
    
    size_t matched = 0;
    for(auto _ : state) {
        matched = 0;
        view.ForEach([&matched](Astra::Entity, Position& pos, Velocity& vel, Comp<0>& c) {
            benchmark::DoNotOptimize(pos.x = 0);
            benchmark::DoNotOptimize(vel.x = 0);
            benchmark::DoNotOptimize(c.x = 0);
            matched++;
        });
    }
    
    state.SetItemsProcessed(state.iterations() * matched);
}

// Random access benchmarks
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

// Register benchmarks with common entity counts
BENCHMARK(BM_CreateEntities)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_CreateEntitiesBatch)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_AddComponents)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_RemoveComponents)->Arg(10000)->Arg(100000)->Arg(1000000);

// Most important iteration benchmarks
BENCHMARK(BM_IterateSingleComponent)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_IterateTwoComponents)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_IterateTwoComponentsHalf)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_IterateTwoComponentsOne)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_IterateThreeComponents)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_IterateFiveComponents)->Arg(10000)->Arg(100000)->Arg(1000000);
// BENCHMARK(BM_IteratePathological);  // Temporarily disabled - causes crash

// Random access
BENCHMARK(BM_GetComponent)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(BM_GetMultipleComponents)->Arg(10000)->Arg(100000)->Arg(1000000);

// Hierarchy/Relations benchmarks
BENCHMARK(BM_HierarchyTraversal)->Arg(1000)->Arg(10000)->Arg(100000);
BENCHMARK(BM_HierarchyForEach)->Arg(1000)->Arg(10000)->Arg(100000);
BENCHMARK(BM_FilteredHierarchyTraversal)->Arg(1000)->Arg(10000)->Arg(100000);

BENCHMARK_MAIN();