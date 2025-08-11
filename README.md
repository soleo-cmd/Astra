# Astra ECS

A high-performance, archetype-based Entity Component System (ECS) library for C++20 with SIMD optimizations, relationship graphs, and zero-overhead iteration.

## Features

- **Archetype-based storage** - Entities with identical component sets stored contiguously for optimal cache performance
- **SIMD-accelerated operations** - Hardware-optimized component matching and iteration (SSE2/SSE4.2/AVX2/NEON)
- **Relationship system** - Efficient parent-child and entity linking with graph-based storage
- **Zero-overhead iteration** - ~1.2ns per entity with ForEach, compile-time query optimization
- **Memory optimized** - 16KB aligned chunks, huge page support, custom memory pools
- **Thread-safe operations** - Optional thread safety for concurrent access
- **Modern C++20** - Concepts, ranges, compile-time type safety

## Performance

Benchmark results on modern hardware:

| Operation | Performance | Entities |
|-----------|------------|----------|
| Single component iteration | ~1.2ns/entity | 1M |
| Multi-component iteration (5 components) | ~3.5ns/entity | 100K |
| Entity creation | ~15ns/entity | Batch |
| Component add/remove | ~50-100ns | Single |
| Hierarchy traversal | ~16-185ns/entity | 10K tree |

## Quick Start

```cpp
#include <Astra/Astra.hpp>

// Define components (must be trivially copyable)
struct Position {
    float x, y, z;
};

struct Velocity {
    float dx, dy, dz;
};

int main() {
    // Create registry
    Astra::Registry registry;
    
    // Create entities
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
- CMake 3.16+ or Premake5

### Build Instructions

#### Windows (Visual Studio)
```bash
# Generate Visual Studio 2022 solution
scripts/generate_vs2022.bat

# Open generated solution
build/Astra.sln
```

#### Linux/macOS
```bash
# Generate makefiles
scripts/generate_linux.sh  # or generate_macos.sh

# Build
cd build
make config=release -j8
```

### Build Configurations

- **Debug** - Debug symbols, assertions enabled
- **Release** - Optimized with debug symbols
- **Dist** - Maximum optimization, no debug symbols

## Core Concepts

### Components

Components are simple data structures that hold entity data:

```cpp
// Components must be trivially copyable
struct Transform {
    float x, y, z;
    float rotation;
    float scale;
};

struct Health {
    int current;
    int max;
};

// Register component (optional, for runtime type info)
registry.RegisterComponent<Transform>("Transform");
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
registry.AddComponent(player, Velocity{0, 0, 0});
registry.RemoveComponent<Velocity>(player);

// Access components
if (auto* health = registry.GetComponent<Health>(player)) {
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
auto enemies = registry.CreateView<Position, Enemy, Not<Dead>>();
auto targets = registry.CreateView<Position, AnyOf<Player, Enemy>>();
auto renderables = registry.CreateView<Transform, Optional<Sprite>>();

// Iteration methods
view.ForEach([](Entity e, Position& pos, Velocity& vel) {
    // Process components
});

// Or use range-based for loop
for (auto [entity, pos, vel] : view) {
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
for (Entity child : relations.GetChildren()) {
    // Process children
}

// Filtered relationships
auto physicsChildren = registry.GetRelations<RigidBody>(parent);
physicsChildren.ForEachChild([](Entity e, RigidBody& rb) {
    // Only children with RigidBody component
});

// Entity links (many-to-many)
registry.AddLink(entity1, entity2);
for (Entity linked : relations.GetLinks()) {
    // Process linked entities
}
```

### Batch Operations

Optimize entity creation and destruction:

```cpp
// Batch create entities
std::vector<Entity> enemies(1000);
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
Registry::Config config;
config.threadSafe = true;
config.initialArchetypeCapacity = 256;
Registry registry(config);
```

### Memory Configuration

Configure memory allocation:

```cpp
Registry::Config config;
config.useHugePages = true;  // Use 2MB pages if available
config.chunkSize = 16384;    // 16KB chunks (default)
config.initialChunkCount = 100;
```

### Custom Allocators

Provide custom memory allocation:

```cpp
struct CustomAllocator {
    void* allocate(size_t size, size_t alignment) {
        // Custom allocation logic
    }
    void deallocate(void* ptr, size_t size) {
        // Custom deallocation logic
    }
};

Registry::Config config;
config.allocator = std::make_shared<CustomAllocator>();
```

### SIMD Configuration

Astra automatically detects and uses available SIMD instructions:

- **x86/x64**: SSE2 (required), SSE4.2, AVX2
- **ARM**: NEON

Force specific SIMD level:

```cpp
// In build configuration or before including Astra
#define ASTRA_FORCE_SSE2  // Use only SSE2
#define ASTRA_FORCE_AVX2  // Require AVX2
```

## Examples

### Movement System

```cpp
void UpdateMovement(Registry& registry, float deltaTime) {
    auto view = registry.CreateView<Position, Velocity, Not<Frozen>>();
    
    view.ForEach([deltaTime](Entity e, Position& pos, Velocity& vel) {
        pos.x += vel.dx * deltaTime;
        pos.y += vel.dy * deltaTime;
        pos.z += vel.dz * deltaTime;
    });
}
```

### Collision Detection

```cpp
void CheckCollisions(Registry& registry) {
    auto view = registry.CreateView<Position, Collider>();
    
    // Spatial partitioning would optimize this
    view.ForEach([&](Entity e1, Position& pos1, Collider& col1) {
        view.ForEach([&](Entity e2, Position& pos2, Collider& col2) {
            if (e1 >= e2) return;  // Skip self and duplicates
            
            float dist = distance(pos1, pos2);
            if (dist < col1.radius + col2.radius) {
                // Handle collision
            }
        });
    });
}
```

### Hierarchy Transform

```cpp
void UpdateWorldTransforms(Registry& registry, Entity root) {
    auto relations = registry.GetRelations<Transform>(root);
    
    relations.ForEachDescendant([](Entity e, size_t depth, Transform& local) {
        // Update world transform based on parent
        // Depth indicates hierarchy level
    }, TraversalOrder::DepthFirst);
}
```

## Benchmarking

Run the included benchmarks:

```bash
# Build in release mode
cd build
make config=release

# Run benchmarks
./bin/Release-linux-x64/AstraTest/AstraTest
```

## API Reference

### Registry

| Method | Description |
|--------|-------------|
| `CreateEntity<Ts...>(args...)` | Create entity with components |
| `DestroyEntity(entity)` | Destroy entity and its components |
| `AddComponent<T>(entity, args...)` | Add component to entity |
| `RemoveComponent<T>(entity)` | Remove component from entity |
| `GetComponent<T>(entity)` | Get component pointer (or nullptr) |
| `HasComponent<T>(entity)` | Check if entity has component |
| `CreateView<QueryArgs...>()` | Create filtered view of entities |
| `SetParent(child, parent)` | Set entity parent relationship |
| `GetRelations<Filters...>(entity)` | Get filtered entity relationships |

### View

| Method | Description |
|--------|-------------|
| `ForEach(func)` | Execute function for each matching entity |
| `Size()` | Count matching entities |
| `Empty()` | Check if view has no entities |
| `begin()/end()` | STL-compatible iterators |

### Relations

| Method | Description |
|--------|-------------|
| `GetParent()` | Get parent entity |
| `GetChildren()` | Get child entities |
| `GetLinks()` | Get linked entities |
| `GetDescendants()` | Iterate full hierarchy |
| `ForEachChild(func)` | Execute for each child |
| `ForEachDescendant(func)` | Execute for each descendant |

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

Special thanks to the C++ and game development communities for their valuable feedback and contributions.