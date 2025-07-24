# Astra ECS

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![C++](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)](https://github.com/T3mps/Astra)

A modern Entity Component System reimagined for contemporary CPU architectures and cache hierarchies.

## 📋 Table of Contents

- [Overview](#-overview)
- [Features](#-features)
- [Performance](#-performance)
- [Quick Start](#-quick-start)
- [Architecture](#-architecture)
- [Benchmarks](#-benchmarks)
- [Building](#-building)
- [Examples](#-examples)
- [Roadmap](#-roadmap)
- [License](#-license)

## 🌟 Overview

Astra is a high-performance Entity Component System that breaks from 15+ years of established ECS patterns. Instead of traditional sparse-set implementations with multiple indirections, Astra uses a SwissTable-inspired `FlatMap` for direct entity-to-component mapping.

```cpp
// Traditional ECS: Entity → Sparse Array → Dense Array → Component
auto* component = sparse[entity] != INVALID ? &dense[sparse[entity]] : nullptr;

// Astra: Entity → Component (direct mapping)
auto* component = pool.TryGet(entity);
```

## ✨ Features

- **🚀 Direct Access Architecture** - Single hash lookup instead of multiple array indirections
- **⚡ SIMD Acceleration** - Platform-agnostic SIMD operations for x86 (SSE/AVX) and ARM (NEON)
- **📦 Cache-Optimized** - 16-element groups aligned to cache lines for optimal memory access
- **🔧 Simple API** - Intuitive, modern C++20 interface
- **🎯 Batch Operations** - Efficient multi-component queries with intelligent prefetching
- **📏 Minimal Footprint** - ~2,500 lines of code vs typical 10,000+ line implementations

## 📊 Performance

<table>
<tr>
<th>Operation</th>
<th>Time</th>
<th>Throughput</th>
</tr>
<tr>
<td>Entity Creation</td>
<td>3.08 ns</td>
<td>325 million/sec</td>
</tr>
<tr>
<td>Component Iteration</td>
<td>0.50 ns</td>
<td>2.0 billion/sec</td>
</tr>
<tr>
<td>Component Addition</td>
<td>29.1 ns</td>
<td>34 million/sec</td>
</tr>
<tr>
<td>Random Access</td>
<td>10.5 ns</td>
<td>95 million/sec</td>
</tr>
<tr>
<td>Multi-Component Query (3)</td>
<td>5.9 ns/entity</td>
<td>169 million/sec</td>
</tr>
<tr>
<td>Memory Usage</td>
<td colspan="2">~9 bytes per entity-component pair</td>
</tr>
</table>

*Benchmarked on Intel i7-12700K, Windows 10, MSVC 2022, Release mode*

### Real-World Performance (100K entities)

- **Single Component View**: 0.20 ms (500M entities/sec)
- **Two Component Query**: 1.10 ms (91M entities/sec)
- **Three Component Query**: 0.95 ms (105M entities/sec)
- **Entity Destruction**: 0.22 ms (455K entities/sec)

## 🚀 Quick Start

```cpp
#include <Astra/Astra.hpp>

// Define components
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
    auto entity = registry.CreateEntity();
    
    // Add components
    registry.AddComponent<Position>(entity, 0.0f, 0.0f, 0.0f);
    registry.AddComponent<Velocity>(entity, 1.0f, 0.0f, 0.0f);
    
    // Query entities with both Position and Velocity
    auto view = registry.GetView<Position, Velocity>();
    
    for (auto [entity, pos, vel] : view) {
        pos.x += vel.dx;
        pos.y += vel.dy;
        pos.z += vel.dz;
    }
    
    return 0;
}
```

## 🏗️ Architecture

### Core Innovation

Astra replaces the traditional sparse-set architecture with a FlatMap-based design:

```
Traditional ECS:
┌────────┐     ┌──────────────┐     ┌──────────────┐     ┌───────────┐
│ Entity │ --> │ Sparse Array │ --> │ Dense Array  │ --> │ Component │
└────────┘     └──────────────┘     └──────────────┘     └───────────┘
                    (lookup)            (validation)         (data)

Astra:
┌────────┐     ┌─────────────────────────┐     ┌───────────┐
│ Entity │ --> │ FlatMap<Entity, Comp>   │ --> │ Component │
└────────┘     └─────────────────────────┘     └───────────┘
                    (direct hash lookup)           (data)
```

### Key Components

- **`FlatMap`** - SwissTable-inspired hash map with SIMD-accelerated lookups
- **`ComponentPool<T>`** - Type-safe wrapper around FlatMap for component storage
- **`Registry`** - Central coordinator for entities and components
- **`View`** - Efficient iteration over entities with specific component combinations
- **`EntityPool`** - Manages entity lifecycle with integrated versioning

### Why FlatMap?

Modern CPUs have evolved significantly since sparse-sets became the ECS standard:
- **Large L3 caches** (32MB+) make hash tables more viable
- **Sophisticated prefetchers** reduce random access penalties  
- **Powerful SIMD units** enable efficient parallel searches
- **Better branch predictors** handle hash collision resolution efficiently

## 📈 Benchmarks

### Scalability Analysis

Astra maintains consistent performance across different entity counts:

| Entity Count | Insert (ns) | Lookup (ns) | Iterate (ns) | Memory/Entity |
|--------------|-------------|-------------|--------------|---------------|
| 1K           | 25.3        | 10.5        | 2.5          | ~9 bytes      |
| 10K          | 16.8        | 10.4        | 2.1          | ~9 bytes      |
| 100K         | 13.8        | 10.9        | 1.7          | ~9 bytes      |
| 1M           | 31.6        | 48.2        | 2.3          | ~9 bytes      |
| 16M          | 68.9        | 60.1        | 2.3          | ~9 bytes      |

Key insights:
- **Iteration performance is constant** regardless of entity count (cache-friendly design)
- **Lookup scales logarithmically** with excellent cache behavior up to 1M entities
- **Memory overhead remains constant** at ~9 bytes per entity-component pair

### Complex Game Simulation

Simulating a game with 35,000 entities (10K static, 20K moving, 5K AI):

- **Physics System**: 0.58 ms/frame
- **AI System**: 0.16 ms/frame
- **Damage System**: 0.11 ms/frame
- **Total ECS Time**: 0.85 ms/frame (>1,000 FPS for ECS alone)

### Extreme Scale Test (16 Million Entities)

Astra successfully handles the theoretical maximum of 2^24 entities:

- **Total Memory**: 288 MB (32 MB metadata + 256 MB storage)
- **Sequential Insert**: 1.11 seconds total (69 ns/entity)
- **Random Access**: 60 ns/lookup
- **Full Iteration**: 36 ms (2.3 ns/entity)
- **Batch Deletion (1.6M)**: 140 ms

## 🔨 Building

### Requirements
- C++20 compatible compiler
- Premake5

### Build Instructions

```bash
# Clone the repository
git clone https://github.com/T3mps/Astra.git
cd Astra

# Generate project files with Premake5
# Windows (Visual Studio 2022)
premake5 vs2022

# Linux (GNU Make)
premake5 gmake2

# macOS (Xcode)
premake5 xcode4
```

## 📚 Examples

<details>
<summary>Basic Usage</summary>

```cpp
// Create and destroy entities
auto e1 = registry.CreateEntity();
auto e2 = registry.CreateEntity();
registry.DestroyEntity(e1);

// Add/remove components
registry.AddComponent<Health>(e2, 100);
registry.RemoveComponent<Health>(e2);

// Check components
if (registry.HasComponent<Health>(e2)) {
    auto* health = registry.GetComponent<Health>(e2);
    health->value -= 10;
}
```

</details>

<details>
<summary>Advanced Queries</summary>

```cpp
// Multi-component view
auto view = registry.GetView<Position, Velocity, Sprite>();

// Traditional iteration
for (auto [entity, pos, vel, sprite] : view) {
    pos.x += vel.dx;
    pos.y += vel.dy;
}

// Group-based iteration for better cache usage
view.ForEachGroup([](const Entity* entities, size_t count,
                     Position** positions, Velocity** velocities, Sprite** sprites) {
    // Process 16 entities at a time (SIMD-friendly)
    for (size_t i = 0; i < count; ++i) {
        positions[i]->x += velocities[i]->dx;
        positions[i]->y += velocities[i]->dy;
    }
});
```

</details>

## 🗺️ Roadmap

- [x] Core ECS functionality
- [x] SIMD-accelerated operations
- [x] Multi-component views
- [x] Batch operations
- [x] Group-based iteration
- [ ] Thread-safe operations
- [ ] Entity relationships/hierarchies
- [ ] Component System Integration

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---

<p align="center">
Made with ❤️ for the game development community<br/>
</p>
