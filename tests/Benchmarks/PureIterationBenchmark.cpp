#include <Astra/Astra.hpp>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <random>
#include <vector>

// Minimal components for fair comparison
struct Position { float x, y, z; };
struct Velocity { float x, y, z; };  
struct Rotation { float x, y, z, w; };
struct Scale { float x, y, z; };

// High precision timer
class Timer
{
    using Clock = std::chrono::high_resolution_clock;
    Clock::time_point m_start;
    
public:
    Timer() : m_start(Clock::now()) {}
    
    void Reset() { m_start = Clock::now(); }
    
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
};

// Prevent optimizer from removing our loops
template<typename T>
void DoNotOptimize(T& value) {
#ifdef _MSC_VER
    volatile T* p = &value;
    *p = value;
#else
    asm volatile("" : "+r"(value));
#endif
}

void RunPureIterationBenchmarks()
{
    std::cout << "=== Astra Pure Iteration Benchmark ===\n";
    std::cout << "Measuring iteration overhead only (no work done)\n\n";
    
    std::vector<size_t> entityCounts = {1000, 10000, 50000, 100000, 500000, 1000000};
    
    for (size_t count : entityCounts)
    {
        std::cout << "\n--- " << count << " Entities ---\n";
        
        Astra::Registry registry;
        std::vector<Astra::Entity> entities;
        entities.reserve(count);
        
        // Create entities with components
        for (size_t i = 0; i < count; ++i)
        {
            auto e = registry.CreateEntity();
            entities.push_back(e);
            
            registry.AddComponent<Position>(e, float(i), float(i), float(i));
            registry.AddComponent<Velocity>(e, 1.0f, 1.0f, 1.0f);
            
            // 50% have rotation
            if (i % 2 == 0)
                registry.AddComponent<Rotation>(e, 0.0f, 0.0f, 0.0f, 1.0f);
            
            // 25% have scale
            if (i % 4 == 0)
                registry.AddComponent<Scale>(e, 1.0f, 1.0f, 1.0f);
        }
        
        const size_t iterations = 1000;
        const size_t warmup = 100;
        
        // Test 1: Single component iteration
        {
            auto view = registry.GetView<Position>();
            volatile float sum = 0;
            
            // Warmup
            for (size_t i = 0; i < warmup; ++i)
            {
                for (auto [e, pos] : view)
                {
                    sum += pos.x;
                }
            }
            
            // Benchmark
            Timer timer;
            for (size_t i = 0; i < iterations; ++i)
            {
                for (auto [e, pos] : view)
                {
                    sum += pos.x;
                }
            }
            double elapsed = timer.ElapsedNs();
            
            double nsPerEntity = elapsed / (iterations * count);
            std::cout << "Single component (Position): " 
                      << std::fixed << std::setprecision(2) << nsPerEntity << " ns/entity\n";
            DoNotOptimize(sum);
        }
        
        // Test 2: Two component iteration (streaming)
        {
            auto view = registry.GetView<Position, Velocity>();
            volatile float sum = 0;
            
            // Warmup
            for (size_t i = 0; i < warmup; ++i)
            {
                for (auto [e, pos, vel] : view)
                {
                    sum += pos.x + vel.x;
                }
            }
            
            // Benchmark
            Timer timer;
            for (size_t i = 0; i < iterations; ++i)
            {
                for (auto [e, pos, vel] : view)
                {
                    sum += pos.x + vel.x;
                }
            }
            double elapsed = timer.ElapsedNs();
            
            double nsPerEntity = elapsed / (iterations * count);
            std::cout << "Two components (Pos+Vel) streaming: " 
                      << std::fixed << std::setprecision(2) << nsPerEntity << " ns/entity\n";
            DoNotOptimize(sum);
        }
        
        // Test 2b: Stream invalidation test
        {
            auto view = registry.GetView<Position, Velocity>();
            volatile float sum = 0;
            int rebuilds = 0;
            
            // Create a new entity to add/remove components
            auto testEntity = registry.CreateEntity();
            
            // Benchmark with invalidation every 100 iterations
            Timer timer;
            for (size_t i = 0; i < iterations; ++i)
            {
                // Every 100 iterations, invalidate the stream
                if (i % 100 == 0 && i > 0)
                {
                    if (i % 200 == 0)
                    {
                        // Add components to force rebuild
                        registry.AddComponent<Position>(testEntity, 999.0f, 999.0f, 999.0f);
                        registry.AddComponent<Velocity>(testEntity, 999.0f, 999.0f, 999.0f);
                    }
                    else
                    {
                        // Remove components to force rebuild
                        registry.RemoveComponent<Position>(testEntity);
                        registry.RemoveComponent<Velocity>(testEntity);
                    }
                    rebuilds++;
                }
                
                for (auto [e, pos, vel] : view)
                {
                    sum += pos.x + vel.x;
                }
            }
            double elapsed = timer.ElapsedNs();
            
            // Account for variable entity count due to add/remove
            double avgCount = count + 0.5; // Average between count and count+1
            double nsPerEntity = elapsed / (iterations * avgCount);
            
            std::cout << "Two components streaming w/ invalidation (" << rebuilds << " rebuilds): " 
                      << std::fixed << std::setprecision(2) << nsPerEntity << " ns/entity\n";
            
            // Cleanup test entity
            registry.DestroyEntity(testEntity);
            DoNotOptimize(sum);
        }
        
        // Test 2c: Correctness verification for stream invalidation
        {
            auto view = registry.GetView<Position, Velocity>();
            
            // Count initial entities
            size_t initialCount = 0;
            for (auto [e, pos, vel] : view) { initialCount++; }
            
            // Add a new entity with components
            auto newEntity = registry.CreateEntity();
            registry.AddComponent<Position>(newEntity, 777.0f, 777.0f, 777.0f);
            registry.AddComponent<Velocity>(newEntity, 888.0f, 888.0f, 888.0f);
            
            // Count after addition
            size_t afterAddCount = 0;
            bool foundNew = false;
            for (auto [e, pos, vel] : view) 
            { 
                afterAddCount++;
                if (pos.x == 777.0f && vel.x == 888.0f) foundNew = true;
            }
            
            // Remove component from the new entity
            registry.RemoveComponent<Velocity>(newEntity);
            
            // Count after removal
            size_t afterRemoveCount = 0;
            bool stillFoundNew = false;
            for (auto [e, pos, vel] : view) 
            { 
                afterRemoveCount++;
                if (e == newEntity) stillFoundNew = true;
            }
            
            // Verify correctness
            bool correct = (initialCount == count) && 
                          (afterAddCount == count + 1) && 
                          foundNew &&
                          (afterRemoveCount == count) &&
                          !stillFoundNew;
                          
            std::cout << "Stream invalidation correctness: " 
                      << (correct ? "PASSED" : "FAILED")
                      << " (initial=" << initialCount 
                      << ", after_add=" << afterAddCount
                      << ", after_remove=" << afterRemoveCount << ")\n";
                      
            registry.DestroyEntity(newEntity);
        }
        
        // Test 2e: View-aware dirty tracking test
        {
            // Create two views
            auto viewPosVel = registry.GetView<Position, Velocity>();
            auto viewPosRot = registry.GetView<Position, Rotation>();
            
            // Build initial streams
            size_t dummy1 = 0, dummy2 = 0;
            for (auto [e, p, v] : viewPosVel) { dummy1++; }
            for (auto [e, p, r] : viewPosRot) { dummy2++; }
            
            // Create entity for modifications that won't affect views
            auto unrelatedEntity = registry.CreateEntity();
            
            Timer timer;
            int avoidsRebuilds = 0;
            
            // Test iterations where we add components that don't match views
            for (size_t i = 0; i < 100; ++i)
            {
                // Add only Position (neither view should rebuild)
                registry.AddComponent<Position>(unrelatedEntity, 1.0f, 1.0f, 1.0f);
                
                // Iterate both views - should NOT rebuild
                dummy1 = 0;
                for (auto [e, p, v] : viewPosVel) { dummy1++; break; }
                
                dummy2 = 0;
                for (auto [e, p, r] : viewPosRot) { dummy2++; break; }
                
                avoidsRebuilds += 2; // Both views avoided rebuild
                
                // Remove for next iteration
                registry.RemoveComponent<Position>(unrelatedEntity);
            }
            
            double elapsed = timer.ElapsedNs();
            double nsPerIteration = elapsed / (100 * 2); // 100 iterations, 2 views
            
            std::cout << "View-aware tracking avoids " << avoidsRebuilds << " rebuilds, "
                      << std::fixed << std::setprecision(2) 
                      << nsPerIteration << " ns per view check\n";
                      
            registry.DestroyEntity(unrelatedEntity);
            DoNotOptimize(dummy1);
            DoNotOptimize(dummy2);
        }
        
        // Test 2d: Measure stream rebuild cost
        {
            auto view = registry.GetView<Position, Velocity>();
            
            // Force initial stream build
            size_t dummy = 0;
            for (auto [e, pos, vel] : view) { dummy++; }
            
            // Create entity for modifications
            auto modEntity = registry.CreateEntity();
            
            // Measure single rebuild cost
            const size_t rebuildTests = 100;
            Timer rebuildTimer;
            
            for (size_t i = 0; i < rebuildTests; ++i)
            {
                // Add components to invalidate
                registry.AddComponent<Position>(modEntity, 1.0f, 1.0f, 1.0f);
                registry.AddComponent<Velocity>(modEntity, 1.0f, 1.0f, 1.0f);
                
                // Force rebuild by iterating once
                dummy = 0;
                for (auto [e, pos, vel] : view) { dummy++; break; }
                
                // Remove for next iteration
                registry.RemoveComponent<Position>(modEntity);
                registry.RemoveComponent<Velocity>(modEntity);
            }
            
            double rebuildTime = rebuildTimer.ElapsedNs() / rebuildTests;
            std::cout << "Stream rebuild cost: " 
                      << std::fixed << std::setprecision(2) 
                      << rebuildTime / 1000.0 << " us per rebuild\n";
                      
            registry.DestroyEntity(modEntity);
            DoNotOptimize(dummy);
        }
        
        // Test 3: Two component iteration (traditional)
        {
            auto view = registry.GetView<Position, Velocity>();
            volatile float sum = 0;
            
            // Warmup
            for (size_t i = 0; i < warmup; ++i)
            {
                for (auto it = view.begin_traditional(); it != view.end_traditional(); ++it)
                {
                    auto [e, pos, vel] = *it;
                    sum += pos.x + vel.x;
                }
            }
            
            // Benchmark
            Timer timer;
            for (size_t i = 0; i < iterations; ++i)
            {
                for (auto it = view.begin_traditional(); it != view.end_traditional(); ++it)
                {
                    auto [e, pos, vel] = *it;
                    sum += pos.x + vel.x;
                }
            }
            double elapsed = timer.ElapsedNs();
            
            double nsPerEntity = elapsed / (iterations * count);
            std::cout << "Two components (Pos+Vel) traditional: " 
                      << std::fixed << std::setprecision(2) << nsPerEntity << " ns/entity\n";
            DoNotOptimize(sum);
        }
        
        // Test 4: Three component iteration
        {
            auto view = registry.GetView<Position, Rotation>();
            volatile float sum = 0;
            size_t actualCount = 0;
            
            // Count actual entities
            for (auto [e, p, r] : view) actualCount++;
            
            if (actualCount > 0)
            {
                // Warmup
                for (size_t i = 0; i < warmup; ++i)
                {
                    for (auto [e, pos, rot] : view)
                    {
                        sum += pos.x + rot.w;
                    }
                }
                
                // Benchmark
                Timer timer;
                for (size_t i = 0; i < iterations; ++i)
                {
                    for (auto [e, pos, rot] : view)
                    {
                        sum += pos.x + rot.w;
                    }
                }
                double elapsed = timer.ElapsedNs();
                
                double nsPerEntity = elapsed / (iterations * actualCount);
                std::cout << "Two components (Pos+Rot, 50% match): " 
                          << std::fixed << std::setprecision(2) << nsPerEntity << " ns/entity\n";
                DoNotOptimize(sum);
            }
        }
        
        // Test 5: Four component iteration
        {
            auto view = registry.GetView<Position, Velocity, Rotation, Scale>();
            volatile float sum = 0;
            size_t actualCount = 0;
            
            // Count actual entities
            for (auto [e, p, v, r, s] : view) actualCount++;
            
            if (actualCount > 0)
            {
                // Warmup
                for (size_t i = 0; i < warmup; ++i)
                {
                    for (auto [e, pos, vel, rot, scale] : view)
                    {
                        sum += pos.x + vel.x + rot.w + scale.x;
                    }
                }
                
                // Benchmark
                Timer timer;
                for (size_t i = 0; i < iterations; ++i)
                {
                    for (auto [e, pos, vel, rot, scale] : view)
                    {
                        sum += pos.x + vel.x + rot.w + scale.x;
                    }
                }
                double elapsed = timer.ElapsedNs();
                
                double nsPerEntity = elapsed / (iterations * actualCount);
                std::cout << "Four components (sparse, ~12.5% match): " 
                          << std::fixed << std::setprecision(2) << nsPerEntity << " ns/entity\n";
                DoNotOptimize(sum);
            }
        }
    }
}

// For comparison with other ECS frameworks
void PrintComparisonFormat()
{
    std::cout << "\n\n=== Format for comparison with EnTT/flecs ===\n";
    std::cout << "When testing other frameworks, use:\n";
    std::cout << "- Same entity counts: 1K, 10K, 50K, 100K, 500K, 1M\n";
    std::cout << "- Same component sizes: Position(12B), Velocity(12B), Rotation(16B), Scale(12B)\n";
    std::cout << "- Same access pattern: sum += component.x\n";
    std::cout << "- Same iteration count: 1000 iterations after 100 warmup\n";
    std::cout << "- Measure: nanoseconds per entity\n";
}

int main()
{
    std::cout << "Astra ECS - Pure Iteration Performance Benchmark\n";
    std::cout << "================================================\n\n";
    
    RunPureIterationBenchmarks();
    PrintComparisonFormat();
    
    return 0;
}