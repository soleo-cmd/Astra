#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <ios>
#include <iostream>
#include <iterator>
#include <numeric>
#include <random>
#include <ratio>
#include <string>
#include <tuple>
#include <vector>

#include "Entity/Entity.hpp"
#include "Platform/Platform.hpp"
#include "Registry/Registry.hpp"

// Define which iteration approach to use:
// 0 = Structured bindings (cleaner syntax, ~3-4ns per entity)
// 1 = ForEach (maximum performance, ~1.2ns per entity)
#define USE_FOREACH_ITERATION 1

// Test components
struct Position
{
    float x, y, z;
    Position(float x = 0, float y = 0, float z = 0) : x(x), y(y), z(z) {}
};

struct Velocity
{
    float dx, dy, dz;
    Velocity(float dx = 0, float dy = 0, float dz = 0) : dx(dx), dy(dy), dz(dz) {}
};

struct Health
{
    float current, max;
    Health(float current = 100, float max = 100) : current(current), max(max) {}
};

struct Rotation
{
    float x, y, z, w;
    Rotation(float x = 0, float y = 0, float z = 0, float w = 1) : x(x), y(y), z(z), w(w) {}
};

class Timer
{
    using Clock = std::chrono::high_resolution_clock;
    Clock::time_point m_start;
    
public:
    Timer() : m_start(Clock::now()) {}
    
    double ElapsedMs() const
    {
        auto end = Clock::now();
        return std::chrono::duration<double, std::milli>(end - m_start).count();
    }
    
    double ElapsedNs() const
    {
        auto end = Clock::now();
        return std::chrono::duration<double, std::nano>(end - m_start).count();
    }
    
    void Reset() { m_start = Clock::now(); }
};

template<typename T>
void DoNotOptimize(T&& value)
{
#ifdef ASTRA_PLATFORM_WINDOWS
    // Force the value to be written to memory
    volatile T forcedCopy = value;
    (void)forcedCopy;
#else
    asm volatile("" : "+r"(value));
#endif
}

struct Stats
{
    double mean;
    double stddev;
    double min;
    double max;
    size_t samples;
};

Stats CalculateStats(const std::vector<double>& measurements)
{
    if (measurements.empty()) return {0, 0, 0, 0, 0};
    
    double sum = std::accumulate(measurements.begin(), measurements.end(), 0.0);
    double mean = sum / measurements.size();
    
    double variance = 0;
    for (double val : measurements) {
        double diff = val - mean;
        variance += diff * diff;
    }
    variance /= measurements.size();
    
    return {
        mean,
        std::sqrt(variance),
        *std::min_element(measurements.begin(), measurements.end()),
        *std::max_element(measurements.begin(), measurements.end()),
        measurements.size()
    };
}

void PrintStats(const std::string& name, const Stats& stats, const std::string& unit = "ms") {
    std::cout << std::setw(35) << std::left << name << ": "
              << std::fixed << std::setprecision(3) << std::setw(10) << std::right << stats.mean << " " << unit
              << " (stddev=" << std::setw(8) << stats.stddev 
              << ", min=" << std::setw(8) << stats.min
              << ", max=" << std::setw(8) << stats.max << ")\n";
}

void RunBenchmark() {
    std::cout << "=== ARCHETYPE ECS BENCHMARK ===\n";
    std::cout << "Testing multi-component iteration performance\n\n";
    
    const std::vector<size_t> entityCounts = {1000, 10000, 50000, 100000, 500000, 1000000};
    const size_t warmupIterations = 100;
    const size_t benchmarkIterations = 1000;
    
    for (size_t entityCount : entityCounts) {
        std::cout << "\n--- " << entityCount << " Entities ---\n";
        
        // Setup
        Astra::Registry registry;
        std::vector<Astra::Entity> entities;
        entities.reserve(entityCount);
        
        // Create entities
        std::mt19937 rng(42); // Fixed seed for reproducibility
        std::uniform_real_distribution<float> dist(-100.0f, 100.0f);
        
        Timer setupTimer;
        
        // Create entities using optimized batch approach
        // Group entities by their component combination to avoid archetype transitions
        
        // Calculate entity distribution
        size_t posOnly = entityCount * 2 / 10;           // 20% have only Position
        size_t posVel = entityCount * 2 / 10;            // 20% have Position+Velocity  
        size_t posVelHealth = entityCount * 2 / 10;      // 20% have Position+Velocity+Health
        size_t posVelHealthRot = entityCount * 4 / 10;   // 40% have all components
        
        // Adjust for rounding
        size_t total = posOnly + posVel + posVelHealth + posVelHealthRot;
        if (total < entityCount) {
            posVelHealthRot += entityCount - total;
        }
        
        size_t idx = 0;
        
        // Create entities with Position only
        {
            std::vector<Astra::Entity> batch(posOnly);
            registry.CreateEntities<Position>
            (
                posOnly, batch,
                [&](size_t i)
                {
                    return std::make_tuple(Position{dist(rng), dist(rng), dist(rng)});
                }
            );
            entities.insert(entities.end(), batch.begin(), batch.end());
        }
        
        // Create entities with Position + Velocity
        {
            std::vector<Astra::Entity> batch(posVel);
            registry.CreateEntities<Position, Velocity>(
                posVel, batch,
                [&](size_t i) {
                    return std::make_tuple(
                        Position{dist(rng), dist(rng), dist(rng)},
                        Velocity{dist(rng), dist(rng), dist(rng)}
                    );
                }
            );
            entities.insert(entities.end(), batch.begin(), batch.end());
        }
        
        // Create entities with Position + Velocity + Health
        {
            std::vector<Astra::Entity> batch(posVelHealth);
            registry.CreateEntities<Position, Velocity, Health>(
                posVelHealth, batch,
                [&](size_t i) {
                    return std::make_tuple(
                        Position{dist(rng), dist(rng), dist(rng)},
                        Velocity{dist(rng), dist(rng), dist(rng)},
                        Health{100.0f, 100.0f}
                    );
                }
            );
            entities.insert(entities.end(), batch.begin(), batch.end());
        }
        
        // Create entities with all components
        {
            std::vector<Astra::Entity> batch(posVelHealthRot);
            registry.CreateEntities<Position, Velocity, Health, Rotation>(
                posVelHealthRot, batch,
                [&](size_t i) {
                    return std::make_tuple(
                        Position{dist(rng), dist(rng), dist(rng)},
                        Velocity{dist(rng), dist(rng), dist(rng)},
                        Health{100.0f, 100.0f},
                        Rotation{0.0f, 0.0f, 0.0f, 1.0f}
                    );
                }
            );
            entities.insert(entities.end(), batch.begin(), batch.end());
        }
        
        std::cout << "Setup time: " << setupTimer.ElapsedMs() << " ms\n";
        
        // Create views
        auto viewPos = registry.CreateView<Position>();
        auto viewPosVel = registry.CreateView<Position, Velocity>();
        auto viewPosVelHealth = registry.CreateView<Position, Velocity, Health>();
        auto viewAll = registry.CreateView<Position, Velocity, Health, Rotation>();
        
        std::cout << "Entities with Pos: " << viewPos.Count() << "\n";
        std::cout << "Entities with Pos+Vel: " << viewPosVel.Count() << "\n";
        std::cout << "Entities with Pos+Vel+Health: " << viewPosVelHealth.Count() << "\n";
        std::cout << "Entities with all 4 components: " << viewAll.Count() << "\n\n";
        
        {
            std::vector<double> measurements;
            measurements.reserve(benchmarkIterations);

            // Warmup
            for (size_t i = 0; i < warmupIterations; ++i) {
                float sum = 0;
#if USE_FOREACH_ITERATION
                // ForEach approach - maximum performance
                viewPos.ForEach([&sum](Astra::Entity e, Position& pos) {
                    sum += pos.x + pos.y + pos.z;
                });
#else
                // Structured binding approach - cleaner syntax
                for (auto [e, pos] : viewPos) {
                    sum += pos->x + pos->y + pos->z;
                }
#endif
                DoNotOptimize(sum);
            }

            // Benchmark
            volatile float totalSum = 0;
            for (size_t i = 0; i < benchmarkIterations; ++i) {
                float sum = 0;
                Timer timer;

#if USE_FOREACH_ITERATION
                // ForEach approach - maximum performance
                viewPos.ForEach([&sum](Astra::Entity e, Position& pos) {
                    sum += pos.x + pos.y + pos.z;
                });
#else
                // Structured binding approach - cleaner syntax
                for (auto [e, pos] : viewPos) {
                    sum += pos->x + pos->y + pos->z;
                }
#endif

                auto elapsed = timer.ElapsedMs();
                totalSum += sum;
                measurements.push_back(elapsed);
            }
            DoNotOptimize(totalSum);

            auto stats = CalculateStats(measurements);
            PrintStats("Position iteration", stats);

            // Also show per-entity time
            Stats perEntity = stats;
            perEntity.mean = (stats.mean * 1e6) / viewPos.Count(); // Convert to nanoseconds per entity
            perEntity.stddev = (stats.stddev * 1e6) / viewPos.Count();
            perEntity.min = (stats.min * 1e6) / viewPos.Count();
            perEntity.max = (stats.max * 1e6) / viewPos.Count();
            PrintStats("  Per entity", perEntity, "ns");
        }

        // Benchmark 1: Position + Velocity iteration
        {
            std::vector<double> measurements;
            measurements.reserve(benchmarkIterations);
            
            // Warmup
            for (size_t i = 0; i < warmupIterations; ++i) {
                float sum = 0;
#if USE_FOREACH_ITERATION
                // ForEach approach - maximum performance
                viewPosVel.ForEach([&sum](Astra::Entity e, Position& pos, Velocity& vel) {
                    sum += pos.x * vel.dx + pos.y * vel.dy + pos.z * vel.dz;
                });
#else
                // Structured binding approach - cleaner syntax
                for (auto [e, pos, vel] : viewPosVel) {
                    sum += pos->x * vel->dx + pos->y * vel->dy + pos->z * vel->dz;
                }
#endif
                DoNotOptimize(sum);
            }
            
            // Benchmark
            volatile float totalSum = 0;
            for (size_t i = 0; i < benchmarkIterations; ++i) {
                float sum = 0;
                Timer timer;
                
#if USE_FOREACH_ITERATION
                // ForEach approach - maximum performance
                viewPosVel.ForEach([&sum](Astra::Entity e, Position& pos, Velocity& vel) {
                    sum += pos.x * vel.dx + pos.y * vel.dy + pos.z * vel.dz;
                });
#else
                // Structured binding approach - cleaner syntax
                for (auto [e, pos, vel] : viewPosVel) {
                    sum += pos->x * vel->dx + pos->y * vel->dy + pos->z * vel->dz;
                }
#endif
                
                auto elapsed = timer.ElapsedMs();
                totalSum += sum;
                measurements.push_back(elapsed);
            }
            DoNotOptimize(totalSum);
            
            auto stats = CalculateStats(measurements);
            PrintStats("Position + Velocity iteration", stats);
            
            // Also show per-entity time
            Stats perEntity = stats;
            perEntity.mean = (stats.mean * 1e6) / viewPosVel.Count(); // Convert to nanoseconds per entity
            perEntity.stddev = (stats.stddev * 1e6) / viewPosVel.Count();
            perEntity.min = (stats.min * 1e6) / viewPosVel.Count();
            perEntity.max = (stats.max * 1e6) / viewPosVel.Count();
            PrintStats("  Per entity", perEntity, "ns");
        }
        
        // Benchmark 2: Position + Velocity + Health iteration
        {
            std::vector<double> measurements;
            measurements.reserve(benchmarkIterations);
            
            // Warmup
            for (size_t i = 0; i < warmupIterations; ++i) {
                float sum = 0;
#if USE_FOREACH_ITERATION
                // ForEach approach - maximum performance
                viewPosVelHealth.ForEach([&sum](Astra::Entity e, Position& pos, Velocity& vel, Health& health) {
                    sum += (pos.x + vel.dx) * health.current;
                });
#else
                // Structured binding approach - cleaner syntax
                for (auto [e, pos, vel, health] : viewPosVelHealth) {
                    sum += (pos->x + vel->dx) * health->current;
                }
#endif
                DoNotOptimize(sum);
            }
            
            // Benchmark
            volatile float totalSum = 0;
            for (size_t i = 0; i < benchmarkIterations; ++i) {
                float sum = 0;
                Timer timer;
                
#if USE_FOREACH_ITERATION
                // ForEach approach - maximum performance
                viewPosVelHealth.ForEach([&sum](Astra::Entity e, Position& pos, Velocity& vel, Health& health) {
                    sum += (pos.x + vel.dx) * health.current;
                });
#else
                // Structured binding approach - cleaner syntax
                for (auto [e, pos, vel, health] : viewPosVelHealth) {
                    sum += (pos->x + vel->dx) * health->current;
                }
#endif
                
                auto elapsed = timer.ElapsedMs();
                totalSum += sum;
                measurements.push_back(elapsed);
            }
            DoNotOptimize(totalSum);
            
            auto stats = CalculateStats(measurements);
            PrintStats("Pos + Vel + Health iteration", stats);
            
            Stats perEntity = stats;
            perEntity.mean = (stats.mean * 1e6) / viewPosVelHealth.Count();
            perEntity.stddev = (stats.stddev * 1e6) / viewPosVelHealth.Count();
            perEntity.min = (stats.min * 1e6) / viewPosVelHealth.Count();
            perEntity.max = (stats.max * 1e6) / viewPosVelHealth.Count();
            PrintStats("  Per entity", perEntity, "ns");
        }
        
        // Benchmark 3: All 4 components iteration
        {
            std::vector<double> measurements;
            measurements.reserve(benchmarkIterations);
            
            
            // Warmup
            for (size_t i = 0; i < warmupIterations; ++i) {
                float sum = 0;
#if USE_FOREACH_ITERATION
                // ForEach approach - maximum performance
                viewAll.ForEach([&sum](Astra::Entity e, Position& pos, Velocity& vel, Health& health, Rotation& rot) {
                    sum += pos.x * vel.dx * health.current * rot.w;
                });
#else
                // Structured binding approach - cleaner syntax
                for (auto [e, pos, vel, health, rot] : viewAll) {
                    sum += pos->x * vel->dx * health->current * rot->w;
                }
#endif
                DoNotOptimize(sum);
            }
            
            // Benchmark
            volatile float totalSum = 0;  // Use volatile to prevent optimization
            for (size_t i = 0; i < benchmarkIterations; ++i) {
                float sum = 0;
                Timer timer;
                
#if USE_FOREACH_ITERATION
                // ForEach approach - maximum performance
                viewAll.ForEach([&sum](Astra::Entity e, Position& pos, Velocity& vel, Health& health, Rotation& rot) {
                    sum += pos.x * vel.dx * health.current * rot.w;
                });
#else
                // Structured binding approach - cleaner syntax
                for (auto [e, pos, vel, health, rot] : viewAll) {
                    sum += pos->x * vel->dx * health->current * rot->w;
                }
#endif
                
                auto elapsed = timer.ElapsedMs();
                totalSum += sum;  // Force the sum to be used
                measurements.push_back(elapsed);
            }
            DoNotOptimize(totalSum);  // Ensure total sum is not optimized away
            
            auto stats = CalculateStats(measurements);
            PrintStats("All 4 components iteration", stats);
            
            Stats perEntity = stats;
            perEntity.mean = (stats.mean * 1e6) / viewAll.Count();
            perEntity.stddev = (stats.stddev * 1e6) / viewAll.Count();
            perEntity.min = (stats.min * 1e6) / viewAll.Count();
            perEntity.max = (stats.max * 1e6) / viewAll.Count();
            PrintStats("  Per entity", perEntity, "ns");
        }
        
        // Benchmark 4: Random access comparison
        {
            std::vector<double> measurements;
            measurements.reserve(benchmarkIterations);
            
            // Create random access pattern
            std::vector<Astra::Entity> randomEntities;
            std::sample(entities.begin(), entities.end(), 
                       std::back_inserter(randomEntities), 
                       std::min(size_t(1000), entityCount), rng);
            
            // Warmup
            for (size_t i = 0; i < warmupIterations; ++i) {
                float sum = 0;
                for (Astra::Entity e : randomEntities) {
                    if (auto* pos = registry.GetComponent<Position>(e)) {
                        if (auto* vel = registry.GetComponent<Velocity>(e)) {
                            sum += pos->x * vel->dx;
                        }
                    }
                }
                DoNotOptimize(sum);
            }
            
            // Benchmark
            for (size_t i = 0; i < benchmarkIterations; ++i) {
                float sum = 0;
                Timer timer;
                
                for (Astra::Entity e : randomEntities) {
                    if (auto* pos = registry.GetComponent<Position>(e)) {
                        if (auto* vel = registry.GetComponent<Velocity>(e)) {
                            sum += pos->x * vel->dx;
                        }
                    }
                }
                
                measurements.push_back(timer.ElapsedMs());
                DoNotOptimize(sum);
            }
            
            auto stats = CalculateStats(measurements);
            std::cout << "\nRandom access (1000 entities):\n";
            PrintStats("  Time", stats);
        }
    }
    
    // Relationship traversal benchmarks
    std::cout << "\n=== RELATIONSHIP SYSTEM BENCHMARKS ===\n";
    {
        Astra::Registry registry;
        const size_t numRuns = benchmarkIterations;
        
        // Create a hierarchy for testing
        std::vector<Astra::Entity> parents;
        std::vector<Astra::Entity> allEntities;
        
        // Create 100 parent entities with Position
        for (int i = 0; i < 100; ++i)
        {
            auto parent = registry.CreateEntity<Position>(Position(i, 0, 0));
            parents.push_back(parent);
            allEntities.push_back(parent);
            
            // Each parent has 10 children
            for (int j = 0; j < 10; ++j)
            {
                auto child = registry.CreateEntity<Position, Velocity>(
                    Position(i, j, 0), Velocity(1, 1, 1)
                );
                registry.SetParent(child, parent);
                allEntities.push_back(child);
                
                // Each child has 2 grandchildren
                for (int k = 0; k < 2; ++k)
                {
                    auto grandchild = registry.CreateEntity<Position, Velocity, Health>(
                        Position(i, j, k), Velocity(2, 2, 2), Health(100, 100)
                    );
                    registry.SetParent(grandchild, child);
                    allEntities.push_back(grandchild);
                }
            }
        }
        
        // Benchmark: Iterate immediate children (unfiltered)
        {
            std::vector<double> measurements;
            measurements.reserve(numRuns);
            
            for (size_t run = 0; run < numRuns; ++run)
            {
                Timer timer;
                int count = 0;
                
                for (auto parent : parents)
                {
                    auto relations = registry.GetRelations<>(parent);
                    for (auto child : relations.GetChildren())
                    {
                        count++;
                        DoNotOptimize(child);
                    }
                }
                
                measurements.push_back(timer.ElapsedMs());
                DoNotOptimize(count);
            }
            
            auto stats = CalculateStats(measurements);
            std::cout << "\nIterate immediate children (unfiltered):\n";
            PrintStats("  Time", stats);
        }
        
        // Benchmark: Iterate immediate children (filtered by Velocity)
        {
            std::vector<double> measurements;
            measurements.reserve(numRuns);
            
            for (size_t run = 0; run < numRuns; ++run)
            {
                Timer timer;
                int count = 0;
                
                for (auto parent : parents)
                {
                    auto relations = registry.GetRelations<Velocity>(parent);
                    for (auto child : relations.GetChildren())
                    {
                        count++;
                        DoNotOptimize(child);
                    }
                }
                
                measurements.push_back(timer.ElapsedMs());
                DoNotOptimize(count);
            }
            
            auto stats = CalculateStats(measurements);
            std::cout << "\nIterate immediate children (filtered by Velocity):\n";
            PrintStats("  Time", stats);
        }
        
        // Benchmark: Traverse all descendants
        {
            std::vector<double> measurements;
            measurements.reserve(numRuns);
            
            for (size_t run = 0; run < numRuns; ++run)
            {
                Timer timer;
                int count = 0;
                
                for (auto parent : parents)
                {
                    auto relations = registry.GetRelations<>(parent);
                    for (auto [entity, depth] : relations.GetDescendants())
                    {
                        count++;
                        DoNotOptimize(entity);
                        DoNotOptimize(depth);
                    }
                }
                
                measurements.push_back(timer.ElapsedMs());
                DoNotOptimize(count);
            }
            
            auto stats = CalculateStats(measurements);
            std::cout << "\nTraverse all descendants (BFS):\n";
            PrintStats("  Time", stats);
        }
        
        // Create some links for link traversal benchmark
        for (size_t i = 0; i < allEntities.size(); i += 10)
        {
            for (size_t j = 1; j < 5 && (i + j) < allEntities.size(); ++j)
            {
                registry.AddLink(allEntities[i], allEntities[i + j]);
            }
        }
        
        // Benchmark: Traverse links
        {
            std::vector<double> measurements;
            measurements.reserve(numRuns);
            
            for (size_t run = 0; run < numRuns; ++run)
            {
                Timer timer;
                int count = 0;
                
                for (size_t i = 0; i < allEntities.size(); i += 10)
                {
                    auto relations = registry.GetRelations<>(allEntities[i]);
                    for (auto linked : relations.GetLinks())
                    {
                        count++;
                        DoNotOptimize(linked);
                    }
                }
                
                measurements.push_back(timer.ElapsedMs());
                DoNotOptimize(count);
            }
            
            auto stats = CalculateStats(measurements);
            std::cout << "\nTraverse links:\n";
            PrintStats("  Time", stats);
        }
    }
    
    std::cout << "\n=== BENCHMARK COMPLETE ===\n";
}

int main() {
    RunBenchmark();
    return 0;
}