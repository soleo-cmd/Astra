# Astra ECS

A high-performance, archetype-based Entity Component System (ECS) library for modern C++20, featuring SIMD optimizations, relationship graphs, and cache-efficient iteration.

## Features

- **Archetype-based storage** - Entities with identical component sets grouped in contiguous 16KB chunks
- **SIMD acceleration** - Hardware-optimized operations (SSE2/SSE4.2/AVX2/NEON)
- **Relationship graphs** - Hierarchical parent-child and bidirectional entity links
- **Advanced queries** - Compile-time optimized queries with Optional, Not, Any, OneOf modifiers
- **Memory optimized** - Custom chunk allocator with huge page support (2MB pages)
- **Modern C++20** - Concepts, ranges, fold expressions, if constexpr

## Quick Start

```cpp
#include <Astra/Astra.hpp>

// Components must be trivially copyable
struct Position {
    float x, y, z;
};

struct Velocity {
    float dx, dy, dz;
};

int main() {
    // Create registry
    Astra::Registry registry;
    
    // Create entity with components
    auto entity = registry.CreateEntity<Position, Velocity>(
        Position{0, 0, 0},
        Velocity{1, 0, 0}
    );
    
    // Query and iterate
    auto view = registry.CreateView<Position, Velocity>();
    view.ForEach([](Astra::Entity e, Position& pos, Velocity& vel) {
        pos.x += vel.dx;
        pos.y += vel.dy;
        pos.z += vel.dz;
    });
    
    return 0;
}
```

## Building

### Requirements

- C++20 compatible compiler:
  - MSVC 2019+ (Windows)
  - GCC 9+ (Linux)
  - Clang 10+ (macOS/Linux)
- Premake5

### Build Instructions

#### Windows (Visual Studio)
```bash
# Generate Visual Studio 2022 solution
scripts/generate_vs2022.bat

# Open generated solution
Astra.sln
```

### Build Configurations

- **Debug** - Debug symbols, assertions enabled (`ASTRA_BUILD_DEBUG`)
- **Release** - Optimized with debug symbols (`ASTRA_BUILD_RELEASE`)
- **Dist** - Maximum optimization, no debug symbols (`ASTRA_BUILD_DIST`)

## Architecture Overview

### Archetype-Based Storage

Astra groups entities with identical component sets into "archetypes", storing components in Structure-of-Arrays format within 16KB memory chunks:

```
Archetype [Position, Velocity]:
  Chunk 0 (16KB):
    [Position][Position][Position]... (contiguous array)
    [Velocity][Velocity][Velocity]... (contiguous array)
    [Entity][Entity][Entity]...       (entity IDs)
  Chunk 1 (16KB):
    ... more entities ...
```

This design ensures:
- **Cache locality** - Components accessed together are stored together
- **SIMD-friendly** - Component arrays are naturally vectorizable
- **Memory efficiency** - Minimal fragmentation with chunk allocation
- **Fast iteration** - Linear memory access pattern

### Query System

Astra's query system uses compile-time validation and optimization:

```cpp
// Basic queries
auto movables = registry.CreateView<Position, Velocity>();

// Advanced query modifiers
auto enemies = registry.CreateView<Position, Enemy, Not<Dead>>();
auto renderables = registry.CreateView<Transform, Optional<Sprite>>();
auto targets = registry.CreateView<Position, Any<Player, Enemy, NPC>>();
auto weapons = registry.CreateView<Item, OneOf<Sword, Bow, Staff>>();
```

Query modifiers:
- `Optional<T>` - Component may or may not exist (nullptr if absent)
- `Not<T>` - Exclude entities with component T
- `Any<T...>` - At least one of the specified components
- `OneOf<T...>` - Exactly one of the specified components

### Relationship System

Separate from component storage to prevent archetype fragmentation:

```cpp
// Hierarchies
registry.SetParent(child, parent);
auto relations = registry.GetRelations(parent);
for (Astra::Entity child : relations.GetChildren()) {
    // Process children
}

// Filtered relationships
auto physicsChildren = registry.GetRelations<RigidBody>(parent);
physicsChildren.ForEachDescendant([](Entity e, size_t depth, RigidBody& rb) {
    // Only descendants with RigidBody
});

// Bidirectional links
registry.AddLink(entity1, entity2);
```

## Core Concepts

### Components

Components are simple data structures that hold entity data:

```cpp
// Components must be trivially copyable
struct Transform
{
    float x, y, z;
    float rotation;
    float scale;
};

struct Health
{
    int current;
    int max;
};

// Register component (optional, for runtime type info)
auto componentRegistry = registry.GetComponentRegistry();
componentRegistry->RegisterComponent<Transform>();
```

### Entities

Entities are lightweight IDs that reference component data:

```cpp
// Create entity with components
auto player = registry.CreateEntity<Transform, Health>(
    Transform{100, 0, 50, 0, 1},
    Health{100, 100}
);

// Add/remove components
registry.AddComponent<Velocity>(player, Velocity{0, 0, 0});
registry.RemoveComponent<Velocity>(player);

// Access components
if (auto* health = registry.GetComponent<Health>(player))
{
    health->current -= 10;
}

// Destroy entity
registry.DestroyEntity(player);
```

### Views and Queries

Views provide efficient iteration over entities with specific components:

```cpp
// Basic view - entities with Position AND Velocity
auto view = registry.CreateView<Position, Velocity>();

// With query modifiers
auto enemies = registry.CreateView<Position, Enemy, Astra::Not<Dead>>();
auto targets = registry.CreateView<Position, Astra::Any<Player, Enemy>>();
auto renderables = registry.CreateView<Transform, Astra::Optional<Sprite>>();

// Iteration methods (Optimized)
view.ForEach([](Astra::Entity e, Position& pos, Velocity& vel) {
    pos.x += vel.dx;
});

// Or use range-based for loop (Slower)
for (auto [entity, pos, vel] : view)
{
    pos->x += vel->dx;
}
```

### Query Modifiers

- `Not<T>` - Exclude entities with component T
- `Optional<T>` - Include component T if present (can be nullptr)
- `AnyOf<T...>` - Require at least one of the specified components
- `OneOf<T...>` - Require exactly one of the specified components

### Relationships

Astra supports entity relationships for hierarchies and graphs:

```cpp
// Parent-child relationships
auto parent = registry.CreateEntity<Transform>();
auto child = registry.CreateEntity<Transform>();
registry.SetParent(child, parent);

// Query relationships
auto relations = registry.GetRelations(parent);
for (Astra::Entity child : relations.GetChildren()) {
    // Process children
}

// Filtered relationships
auto physicsChildren = registry.GetRelations<RigidBody>(parent);
physicsChildren.ForEachChild([](Entity e, RigidBody& rb)
{
    // Only children with RigidBody component
});

// Entity links (many-to-many)
registry.AddLink(entity1, entity2);
for (Astra::Entity linked : relations.GetLinks()) {
    // Process linked entities
}
```

### Batch Operations

Optimize entity creation and destruction:

```cpp
// Batch create entities
std::vector<Astra::Entity> enemies(1000);
registry.CreateEntities<Position, Velocity>(1000, enemies,
    [](size_t i) {
        return std::make_tuple(
            Position{i * 10.0f, 0, 0},
            Velocity{-1, 0, 0}
        );
    });

// Batch destroy
registry.DestroyEntities(enemies);
```

## Advanced Features

### Thread Safety

Enable thread-safe operations:

```cpp
Astra::Registry::Config config;
config.threadSafe = true;
config.initialArchetypeCapacity = 256;
Astra::Registry registry(config);
```

### Memory Configuration

Configure memory allocation:

```cpp
Astra::Registry::Config config;
config.useHugePages = true;  // Use 2MB pages if available
config.chunkSize = 16384;    // 16KB chunks (default)
config.initialChunkCount = 100;
Astra::Registry registry(config);
```

### SIMD Configuration

Astra automatically detects and uses available SIMD instructions:

- **x86/x64**: SSE2 (required), SSE4.2, AVX2
- **ARM**: NEON

## Examples

### Movement System

```cpp
void UpdateMovement(Astra::Registry& registry, float deltaTime) {
    auto view = registry.CreateView<Position, Velocity, Astra::Not<Frozen>>();
    
    view.ForEach([deltaTime](Astra::Entity e, Position& pos, Velocity& vel) {
        pos.x += vel.dx * deltaTime;
        pos.y += vel.dy * deltaTime;
        pos.z += vel.dz * deltaTime;
    });
}
```

### Hierarchy Transform

```cpp
void UpdateWorldTransforms(Astra::Registry& registry, Astra::Entity root) {
    auto relations = registry.GetRelations<Transform>(root);
    
    relations.ForEachDescendant(
        [](Astra::Entity e, size_t depth, Transform& local) {
            // Update world transform based on parent
            // Depth indicates hierarchy level
        },
        Astra::TraversalOrder::DepthFirst
    );
}
```

## Benchmarking

Run the included benchmarks!

```bash
Run on (20 X 3610 MHz CPU s)
CPU Caches:
  L1 Data 48 KiB (x10)
  L1 Instruction 32 KiB (x10)
  L2 Unified 1280 KiB (x10)
  L3 Unified 25600 KiB (x1)
-----------------------------------------------------------------------------------------------
Benchmark                                     Time             CPU   Iterations UserCounters...
-----------------------------------------------------------------------------------------------
BM_CreateEntities/10000                 2360936 ns      2351589 ns          299 items_per_second=4.25244M/s
BM_CreateEntities/100000               21498662 ns     21484375 ns           32 items_per_second=4.65455M/s
BM_CreateEntities/1000000             268167600 ns    270833333 ns            3 items_per_second=3.69231M/s
BM_CreateEntitiesBatch/10000            1092974 ns      1098633 ns          640 items_per_second=9.10222M/s
BM_CreateEntitiesBatch/100000          12828623 ns     12276786 ns           56 items_per_second=8.14545M/s
BM_CreateEntitiesBatch/1000000        193051900 ns    191406250 ns            4 items_per_second=5.22449M/s
BM_AddComponents/10000                  2596141 ns      2259036 ns          249 items_per_second=8.85333M/s
BM_AddComponents/100000                28814623 ns     29829545 ns           22 items_per_second=6.70476M/s
BM_AddComponents/1000000              424144600 ns    429687500 ns            2 items_per_second=4.65455M/s
BM_RemoveComponents/10000               2002010 ns      1988002 ns          448 items_per_second=5.03018M/s
BM_RemoveComponents/100000             22746728 ns     18554688 ns           32 items_per_second=5.38947M/s
BM_RemoveComponents/1000000           339848600 ns    335937500 ns            2 items_per_second=2.97674M/s
BM_IterateSingleComponent/10000           10210 ns        10254 ns        64000 items_per_second=975.238M/s
BM_IterateSingleComponent/100000         109692 ns       109863 ns         6400 items_per_second=910.222M/s
BM_IterateSingleComponent/1000000       1397911 ns      1380522 ns          498 items_per_second=724.364M/s
BM_IterateTwoComponents/10000             21495 ns        21484 ns        32000 items_per_second=465.455M/s
BM_IterateTwoComponents/100000           230868 ns       230164 ns         2987 items_per_second=434.473M/s
BM_IterateTwoComponents/1000000         3482104 ns      3447770 ns          213 items_per_second=290.043M/s
BM_IterateTwoComponentsHalf/10000         11996 ns        11998 ns        56000 items_per_second=416.744M/s
BM_IterateTwoComponentsHalf/100000       127255 ns       125552 ns         4978 items_per_second=398.24M/s
BM_IterateTwoComponentsHalf/1000000     1535674 ns      1534598 ns          448 items_per_second=325.818M/s
BM_IterateTwoComponentsOne/10000           4.03 ns         4.01 ns    179200000 items_per_second=249.322M/s
BM_IterateTwoComponentsOne/100000          3.98 ns         3.90 ns    172307692 items_per_second=256.458M/s
BM_IterateTwoComponentsOne/1000000         3.98 ns         3.90 ns    172307692 items_per_second=256.458M/s
BM_IterateThreeComponents/10000           28744 ns        28878 ns        24889 items_per_second=346.282M/s
BM_IterateThreeComponents/100000         303046 ns       304813 ns         2358 items_per_second=328.07M/s
BM_IterateThreeComponents/1000000       5032185 ns      4800452 ns          166 items_per_second=208.314M/s
BM_IterateFiveComponents/10000            44993 ns        44922 ns        16000 items_per_second=222.609M/s
BM_IterateFiveComponents/100000          473957 ns       450017 ns         1493 items_per_second=222.214M/s
BM_IterateFiveComponents/1000000        5792779 ns      5859375 ns          112 items_per_second=170.667M/s
BM_GetComponent/10000                    223689 ns       219727 ns         3200 items_per_second=45.5111M/s
BM_GetComponent/100000                  3049325 ns      3012048 ns          249 items_per_second=33.2M/s
BM_GetComponent/1000000                80999571 ns     82589286 ns            7 items_per_second=12.1081M/s
BM_GetMultipleComponents/10000           361544 ns       360695 ns         2036 items_per_second=55.4485M/s
BM_GetMultipleComponents/100000         4726483 ns      4718960 ns          149 items_per_second=42.3822M/s
BM_GetMultipleComponents/1000000      156609500 ns    156250000 ns            5 items_per_second=12.8M/s
BM_HierarchyTraversal/1000               167075 ns       167411 ns         4480 items_per_second=8.1536M/s
BM_HierarchyTraversal/10000              164674 ns       163923 ns         4480 items_per_second=8.32708M/s
BM_HierarchyTraversal/100000           14780162 ns     13935811 ns           37 items_per_second=4.01749M/s
BM_HierarchyForEach/1000                 205527 ns       195312 ns         3200 items_per_second=6.9888M/s
BM_HierarchyForEach/10000                211764 ns       209961 ns         3200 items_per_second=6.50121M/s
BM_HierarchyForEach/100000             13131222 ns     13194444 ns           45 items_per_second=4.24323M/s
BM_FilteredHierarchyTraversal/1000       194031 ns       188354 ns         3733 items_per_second=3.62084M/s
BM_FilteredHierarchyTraversal/10000      187650 ns       188354 ns         3733 items_per_second=3.62084M/s
BM_FilteredHierarchyTraversal/100000   13380480 ns     13392857 ns           56 items_per_second=2.09014M/s
```

## Contributing

Contributions are welcome! Please ensure:

1. Code follows existing style conventions
2. All tests pass
3. Benchmarks show no performance regression
4. New features include tests

## License

Astra is available under the MIT License. See LICENSE file for details.

## Acknowledgments

Inspired by:
- [EnTT](https://github.com/skypjack/entt) - Modern C++ ECS
- [Flecs](https://github.com/SanderMertens/flecs) - Fast and lightweight ECS
- [DOTS](https://unity.com/dots) - Unity's Data-Oriented Technology Stack
