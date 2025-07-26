//#include <Astra/Astra.hpp>
//#include <iostream>
//#include <chrono>
//#include <iomanip>
//#include <random>
//#include <vector>
//#include <algorithm>
//#include <numeric>
//#include <cmath>
//
//// Realistic game components
//struct Position 
//{ 
//    float x, y, z;
//};
//
//struct Velocity 
//{ 
//    float dx, dy, dz;
//};
//
//struct Rotation
//{
//    float pitch, yaw, roll;
//    float angularVelocity;
//};
//
//struct Renderable
//{
//    std::uint32_t meshId;
//    std::uint32_t materialId;
//    float lodBias;
//    bool visible;
//};
//
//struct Collider
//{
//    float radius;
//    std::uint32_t layer;
//    std::uint32_t mask;
//    bool isTrigger;
//};
//
//struct Health
//{
//    float current;
//    float max;
//    float regeneration;
//    float damageMultiplier;
//};
//
//struct AI
//{
//    enum State { Idle, Patrol, Chase, Attack, Flee };
//    State state;
//    float alertness;
//    float reactionTime;
//    Astra::Entity target;
//};
//
//struct Player
//{
//    std::uint32_t playerId;
//    std::uint32_t teamId;
//    float score;
//    float respawnTimer;
//};
//
//// High precision timer
//class PrecisionTimer
//{
//    using Clock = std::chrono::high_resolution_clock;
//    Clock::time_point m_start;
//    
//public:
//    PrecisionTimer() : m_start(Clock::now()) {}
//    
//    void Reset() { m_start = Clock::now(); }
//    
//    double ElapsedMs() const
//    {
//        auto end = Clock::now();
//        return std::chrono::duration<double, std::milli>(end - m_start).count();
//    }
//    
//    double ElapsedUs() const
//    {
//        auto end = Clock::now();
//        return std::chrono::duration<double, std::micro>(end - m_start).count();
//    }
//    
//    double ElapsedNs() const
//    {
//        auto end = Clock::now();
//        return std::chrono::duration<double, std::nano>(end - m_start).count();
//    }
//};
//
//// Statistics tracking
//struct FrameStats
//{
//    double frameTimeMs;
//    double updateTimeMs;
//    size_t entitiesProcessed;
//    
//    double GetEntitiesPerSecond() const 
//    { 
//        return (entitiesProcessed / frameTimeMs) * 1000.0; 
//    }
//};
//
//class BenchmarkStats
//{
//    std::vector<FrameStats> m_frames;
//    std::string m_name;
//    
//public:
//    explicit BenchmarkStats(const std::string& name) : m_name(name) 
//    {
//        m_frames.reserve(1000);
//    }
//    
//    void AddFrame(const FrameStats& frame)
//    {
//        m_frames.push_back(frame);
//    }
//    
//    void PrintSummary() const
//    {
//        if (m_frames.empty()) return;
//        
//        // Calculate statistics
//        double totalTime = 0;
//        double minFrame = std::numeric_limits<double>::max();
//        double maxFrame = 0;
//        size_t totalEntities = 0;
//        
//        for (const auto& frame : m_frames)
//        {
//            totalTime += frame.frameTimeMs;
//            minFrame = std::min(minFrame, frame.frameTimeMs);
//            maxFrame = std::max(maxFrame, frame.frameTimeMs);
//            totalEntities += frame.entitiesProcessed;
//        }
//        
//        double avgFrame = totalTime / m_frames.size();
//        
//        // Calculate std deviation
//        double variance = 0;
//        for (const auto& frame : m_frames)
//        {
//            double diff = frame.frameTimeMs - avgFrame;
//            variance += diff * diff;
//        }
//        double stdDev = std::sqrt(variance / m_frames.size());
//        
//        // Calculate percentiles
//        std::vector<double> frameTimes;
//        frameTimes.reserve(m_frames.size());
//        for (const auto& frame : m_frames)
//        {
//            frameTimes.push_back(frame.frameTimeMs);
//        }
//        std::sort(frameTimes.begin(), frameTimes.end());
//        
//        auto percentile = [&](double p) {
//            size_t idx = static_cast<size_t>(frameTimes.size() * p / 100.0);
//            return frameTimes[std::min(idx, frameTimes.size() - 1)];
//        };
//        
//        double avgEntitiesPerSec = (totalEntities / totalTime) * 1000.0;
//        
//        std::cout << "\n=== " << m_name << " ===\n";
//        std::cout << "Frames: " << m_frames.size() << "\n";
//        std::cout << "Total Time: " << std::fixed << std::setprecision(2) << totalTime << " ms\n";
//        std::cout << "Avg Frame: " << std::setprecision(3) << avgFrame << " ms (" 
//                  << std::setprecision(1) << (1000.0 / avgFrame) << " FPS)\n";
//        std::cout << "Min Frame: " << std::setprecision(3) << minFrame << " ms\n";
//        std::cout << "Max Frame: " << std::setprecision(3) << maxFrame << " ms\n";
//        std::cout << "Std Dev: " << std::setprecision(3) << stdDev << " ms\n";
//        std::cout << "50th %ile: " << std::setprecision(3) << percentile(50) << " ms\n";
//        std::cout << "90th %ile: " << std::setprecision(3) << percentile(90) << " ms\n";
//        std::cout << "95th %ile: " << std::setprecision(3) << percentile(95) << " ms\n";
//        std::cout << "99th %ile: " << std::setprecision(3) << percentile(99) << " ms\n";
//        std::cout << "Entities/sec: " << std::scientific << std::setprecision(2) 
//                  << avgEntitiesPerSec << "\n";
//    }
//};
//
//// System update functions
//void UpdateMovementSystem(Astra::Registry& registry, float deltaTime)
//{
//    auto view = registry.GetView<Position, Velocity>();
//    for (auto [entity, pos, vel] : view)
//    {
//        pos.x += vel.dx * deltaTime;
//        pos.y += vel.dy * deltaTime;
//        pos.z += vel.dz * deltaTime;
//        
//        // Simple boundary wrapping
//        if (pos.x > 1000.0f) pos.x -= 2000.0f;
//        if (pos.x < -1000.0f) pos.x += 2000.0f;
//        if (pos.z > 1000.0f) pos.z -= 2000.0f;
//        if (pos.z < -1000.0f) pos.z += 2000.0f;
//    }
//}
//
//void UpdatePhysicsSystem(Astra::Registry& registry, float deltaTime)
//{
//    auto view = registry.GetView<Position, Velocity, Collider>();
//    for (auto [entity, pos, vel, col] : view)
//    {
//        // Simple friction
//        vel.dx *= 0.99f;
//        vel.dy *= 0.99f;
//        vel.dz *= 0.99f;
//        
//        // Gravity for non-trigger colliders
//        if (!col.isTrigger)
//        {
//            vel.dy -= 9.81f * deltaTime;
//        }
//        
//        // Ground collision
//        if (pos.y < 0 && vel.dy < 0)
//        {
//            pos.y = 0;
//            vel.dy = -vel.dy * 0.5f; // Bounce with damping
//        }
//    }
//}
//
//void UpdateRotationSystem(Astra::Registry& registry, float deltaTime)
//{
//    auto view = registry.GetView<Rotation>();
//    for (auto [entity, rot] : view)
//    {
//        rot.yaw += rot.angularVelocity * deltaTime;
//        if (rot.yaw > 360.0f) rot.yaw -= 360.0f;
//        if (rot.yaw < 0.0f) rot.yaw += 360.0f;
//    }
//}
//
//void UpdateHealthSystem(Astra::Registry& registry, float deltaTime)
//{
//    auto view = registry.GetView<Health>();
//    for (auto [entity, health] : view)
//    {
//        if (health.current < health.max)
//        {
//            health.current = std::min(health.max, 
//                health.current + health.regeneration * deltaTime);
//        }
//    }
//}
//
//void UpdateAISystem(Astra::Registry& registry, float deltaTime)
//{
//    auto view = registry.GetView<Position, Velocity, AI>();
//    for (auto [entity, pos, vel, ai] : view)
//    {
//        ai.reactionTime -= deltaTime;
//        
//        if (ai.reactionTime <= 0)
//        {
//            ai.reactionTime = 0.5f + (rand() % 1000) / 1000.0f;
//            
//            // Simple state machine
//            switch (ai.state)
//            {
//            case AI::Idle:
//                if (ai.alertness > 0.3f)
//                {
//                    ai.state = AI::Patrol;
//                    vel.dx = (rand() % 200 - 100) / 100.0f * 5.0f;
//                    vel.dz = (rand() % 200 - 100) / 100.0f * 5.0f;
//                }
//                break;
//                
//            case AI::Patrol:
//                ai.alertness += deltaTime * 0.1f;
//                if (ai.alertness > 0.7f)
//                {
//                    ai.state = AI::Chase;
//                    vel.dx *= 2.0f;
//                    vel.dz *= 2.0f;
//                }
//                break;
//                
//            case AI::Chase:
//                if (ai.alertness < 0.5f)
//                {
//                    ai.state = AI::Patrol;
//                    vel.dx *= 0.5f;
//                    vel.dz *= 0.5f;
//                }
//                break;
//                
//            default:
//                break;
//            }
//        }
//        
//        ai.alertness = std::max(0.0f, ai.alertness - deltaTime * 0.05f);
//    }
//}
//
//void UpdateRenderingSystem(Astra::Registry& registry, float deltaTime)
//{
//    auto view = registry.GetView<Position, Renderable>();
//    for (auto [entity, pos, render] : view)
//    {
//        // Simple frustum culling simulation
//        float distSq = pos.x * pos.x + pos.z * pos.z;
//        render.visible = distSq < 500.0f * 500.0f;
//        
//        // LOD calculation
//        if (render.visible)
//        {
//            render.lodBias = std::min(3.0f, std::sqrt(distSq) / 100.0f);
//        }
//    }
//}
//
//// Benchmark scenarios
//void RunMovementBenchmark(size_t entityCount, size_t frameCount)
//{
//    std::cout << "\n--- Movement System Benchmark ---\n";
//    std::cout << "Entities: " << entityCount << ", Frames: " << frameCount << "\n";
//    
//    Astra::Registry registry;
//    BenchmarkStats stats("Movement System");
//    
//    // Create entities
//    std::cout << "Creating entities...\n";
//    {
//        PrecisionTimer timer;
//        std::mt19937 rng(12345);
//        std::uniform_real_distribution<float> posDist(-1000.0f, 1000.0f);
//        std::uniform_real_distribution<float> velDist(-10.0f, 10.0f);
//        
//        for (size_t i = 0; i < entityCount; ++i)
//        {
//            auto e = registry.CreateEntity();
//            registry.AddComponent<Position>(e, posDist(rng), posDist(rng) * 0.1f, posDist(rng));
//            registry.AddComponent<Velocity>(e, velDist(rng), velDist(rng) * 0.1f, velDist(rng));
//        }
//        
//        std::cout << "Entity creation took " << timer.ElapsedMs() << " ms\n";
//    }
//    
//    // Warmup
//    std::cout << "Warming up...\n";
//    for (size_t i = 0; i < 10; ++i)
//    {
//        UpdateMovementSystem(registry, 0.016f);
//    }
//    
//    // Benchmark
//    std::cout << "Running benchmark...\n";
//    for (size_t frame = 0; frame < frameCount; ++frame)
//    {
//        PrecisionTimer frameTimer;
//        
//        UpdateMovementSystem(registry, 0.016f);
//        
//        stats.AddFrame({
//            frameTimer.ElapsedMs(),
//            frameTimer.ElapsedMs(),
//            entityCount
//        });
//    }
//    
//    stats.PrintSummary();
//}
//
//void RunComplexSystemsBenchmark(size_t entityCount, size_t frameCount)
//{
//    std::cout << "\n--- Complex Multi-System Benchmark ---\n";
//    std::cout << "Entities: " << entityCount << ", Frames: " << frameCount << "\n";
//    
//    Astra::Registry registry;
//    BenchmarkStats stats("Complex Systems");
//    
//    // Create diverse entity population
//    std::cout << "Creating diverse entity population...\n";
//    {
//        PrecisionTimer timer;
//        std::mt19937 rng(54321);
//        std::uniform_real_distribution<float> posDist(-1000.0f, 1000.0f);
//        std::uniform_real_distribution<float> velDist(-10.0f, 10.0f);
//        std::uniform_int_distribution<int> chanceDist(0, 100);
//        
//        for (size_t i = 0; i < entityCount; ++i)
//        {
//            auto e = registry.CreateEntity();
//            
//            // All entities have position (100%)
//            registry.AddComponent<Position>(e, posDist(rng), std::abs(posDist(rng)) * 0.1f, posDist(rng));
//            
//            // 80% have velocity
//            if (chanceDist(rng) < 80)
//            {
//                registry.AddComponent<Velocity>(e, velDist(rng), velDist(rng) * 0.1f, velDist(rng));
//            }
//            
//            // 60% have rotation
//            if (chanceDist(rng) < 60)
//            {
//                registry.AddComponent<Rotation>(e, 0, 0, 0, velDist(rng) * 10.0f);
//            }
//            
//            // 50% are renderable
//            if (chanceDist(rng) < 50)
//            {
//                registry.AddComponent<Renderable>(e, i % 100, i % 10, 0.0f, true);
//            }
//            
//            // 40% have colliders
//            if (chanceDist(rng) < 40)
//            {
//                registry.AddComponent<Collider>(e, 1.0f + (i % 10) * 0.5f, 1 << (i % 8), 0xFF, false);
//            }
//            
//            // 30% have health
//            if (chanceDist(rng) < 30)
//            {
//                float maxHp = 100.0f + (i % 10) * 50.0f;
//                registry.AddComponent<Health>(e, maxHp * 0.8f, maxHp, 5.0f, 1.0f);
//            }
//            
//            // 10% have AI
//            if (chanceDist(rng) < 10)
//            {
//                registry.AddComponent<AI>(e, AI::Idle, 0.0f, 1.0f, Astra::Entity::Null());
//            }
//            
//            // 1% are players
//            if (chanceDist(rng) < 1)
//            {
//                registry.AddComponent<Player>(e, i % 64, i % 4, 0.0f, 0.0f);
//            }
//        }
//        
//        std::cout << "Entity creation took " << timer.ElapsedMs() << " ms\n";
//    }
//    
//    // Print component distribution
//    auto posView = registry.GetView<Position>();
//    auto velView = registry.GetView<Velocity>();
//    auto rotView = registry.GetView<Rotation>();
//    auto renView = registry.GetView<Renderable>();
//    auto colView = registry.GetView<Collider>();
//    auto hpView = registry.GetView<Health>();
//    auto aiView = registry.GetView<AI>();
//    auto plView = registry.GetView<Player>();
//    
//    size_t posCount = 0, velCount = 0, rotCount = 0, renCount = 0;
//    size_t colCount = 0, hpCount = 0, aiCount = 0, plCount = 0;
//    
//    for (auto [e, p] : posView) posCount++;
//    for (auto [e, v] : velView) velCount++;
//    for (auto [e, r] : rotView) rotCount++;
//    for (auto [e, r] : renView) renCount++;
//    for (auto [e, c] : colView) colCount++;
//    for (auto [e, h] : hpView) hpCount++;
//    for (auto [e, a] : aiView) aiCount++;
//    for (auto [e, p] : plView) plCount++;
//    
//    std::cout << "\nComponent Distribution:\n";
//    std::cout << "- Position:   " << posCount << " (" << (posCount * 100.0 / entityCount) << "%)\n";
//    std::cout << "- Velocity:   " << velCount << " (" << (velCount * 100.0 / entityCount) << "%)\n";
//    std::cout << "- Rotation:   " << rotCount << " (" << (rotCount * 100.0 / entityCount) << "%)\n";
//    std::cout << "- Renderable: " << renCount << " (" << (renCount * 100.0 / entityCount) << "%)\n";
//    std::cout << "- Collider:   " << colCount << " (" << (colCount * 100.0 / entityCount) << "%)\n";
//    std::cout << "- Health:     " << hpCount << " (" << (hpCount * 100.0 / entityCount) << "%)\n";
//    std::cout << "- AI:         " << aiCount << " (" << (aiCount * 100.0 / entityCount) << "%)\n";
//    std::cout << "- Player:     " << plCount << " (" << (plCount * 100.0 / entityCount) << "%)\n";
//    
//    // Warmup
//    std::cout << "\nWarming up...\n";
//    for (size_t i = 0; i < 10; ++i)
//    {
//        UpdateMovementSystem(registry, 0.016f);
//        UpdatePhysicsSystem(registry, 0.016f);
//        UpdateRotationSystem(registry, 0.016f);
//        UpdateHealthSystem(registry, 0.016f);
//        UpdateAISystem(registry, 0.016f);
//        UpdateRenderingSystem(registry, 0.016f);
//    }
//    
//    // Benchmark
//    std::cout << "Running benchmark...\n";
//    std::vector<double> systemTimes(6, 0.0);
//    
//    for (size_t frame = 0; frame < frameCount; ++frame)
//    {
//        PrecisionTimer frameTimer;
//        double frameStart = frameTimer.ElapsedMs();
//        
//        PrecisionTimer sysTimer;
//        
//        // Movement system
//        sysTimer.Reset();
//        UpdateMovementSystem(registry, 0.016f);
//        systemTimes[0] += sysTimer.ElapsedMs();
//        
//        // Physics system
//        sysTimer.Reset();
//        UpdatePhysicsSystem(registry, 0.016f);
//        systemTimes[1] += sysTimer.ElapsedMs();
//        
//        // Rotation system
//        sysTimer.Reset();
//        UpdateRotationSystem(registry, 0.016f);
//        systemTimes[2] += sysTimer.ElapsedMs();
//        
//        // Health system
//        sysTimer.Reset();
//        UpdateHealthSystem(registry, 0.016f);
//        systemTimes[3] += sysTimer.ElapsedMs();
//        
//        // AI system
//        sysTimer.Reset();
//        UpdateAISystem(registry, 0.016f);
//        systemTimes[4] += sysTimer.ElapsedMs();
//        
//        // Rendering system
//        sysTimer.Reset();
//        UpdateRenderingSystem(registry, 0.016f);
//        systemTimes[5] += sysTimer.ElapsedMs();
//        
//        double frameEnd = frameTimer.ElapsedMs();
//        
//        // Total entities processed (sum of all system iterations)
//        size_t totalProcessed = velCount + colCount + rotCount + hpCount + aiCount + renCount;
//        
//        stats.AddFrame({
//            frameEnd - frameStart,
//            frameEnd - frameStart,
//            totalProcessed
//        });
//        
//        // Show progress every 100 frames
//        if ((frame + 1) % 100 == 0)
//        {
//            std::cout << "Frame " << (frame + 1) << "/" << frameCount 
//                      << " (" << std::fixed << std::setprecision(1) 
//                      << ((frame + 1) * 100.0 / frameCount) << "%)\r" << std::flush;
//        }
//    }
//    
//    std::cout << "\n";
//    stats.PrintSummary();
//    
//    // System breakdown
//    std::cout << "\nSystem Time Breakdown:\n";
//    std::cout << "- Movement:  " << std::fixed << std::setprecision(2) 
//              << (systemTimes[0] / frameCount) << " ms/frame\n";
//    std::cout << "- Physics:   " << (systemTimes[1] / frameCount) << " ms/frame\n";
//    std::cout << "- Rotation:  " << (systemTimes[2] / frameCount) << " ms/frame\n";
//    std::cout << "- Health:    " << (systemTimes[3] / frameCount) << " ms/frame\n";
//    std::cout << "- AI:        " << (systemTimes[4] / frameCount) << " ms/frame\n";
//    std::cout << "- Rendering: " << (systemTimes[5] / frameCount) << " ms/frame\n";
//    std::cout << "- Total:     " << ((systemTimes[0] + systemTimes[1] + systemTimes[2] + 
//                                      systemTimes[3] + systemTimes[4] + systemTimes[5]) / frameCount) 
//              << " ms/frame\n";
//}
//
//void RunScalingBenchmark()
//{
//    std::cout << "\n--- Scaling Benchmark ---\n";
//    std::cout << "Testing performance scaling with entity count\n\n";
//    
//    std::vector<size_t> entityCounts = {10000, 50000, 100000, 500000, 1000000, 2000000};
//    std::vector<BenchmarkStats> results;
//    
//    for (size_t count : entityCounts)
//    {
//        Astra::Registry registry;
//        BenchmarkStats stats("Scale " + std::to_string(count));
//        
//        // Create entities
//        {
//            PrecisionTimer timer;
//            std::mt19937 rng(99999);
//            std::uniform_real_distribution<float> dist(-1000.0f, 1000.0f);
//            
//            for (size_t i = 0; i < count; ++i)
//            {
//                auto e = registry.CreateEntity();
//                registry.AddComponent<Position>(e, dist(rng), dist(rng), dist(rng));
//                registry.AddComponent<Velocity>(e, dist(rng) * 0.01f, dist(rng) * 0.01f, dist(rng) * 0.01f);
//            }
//            
//            std::cout << "Created " << count << " entities in " 
//                      << timer.ElapsedMs() << " ms\n";
//        }
//        
//        // Warmup
//        for (size_t i = 0; i < 5; ++i)
//        {
//            UpdateMovementSystem(registry, 0.016f);
//        }
//        
//        // Benchmark 100 frames
//        for (size_t frame = 0; frame < 100; ++frame)
//        {
//            PrecisionTimer frameTimer;
//            UpdateMovementSystem(registry, 0.016f);
//            
//            stats.AddFrame({
//                frameTimer.ElapsedMs(),
//                frameTimer.ElapsedMs(),
//                count
//            });
//        }
//        
//        stats.PrintSummary();
//        results.push_back(std::move(stats));
//    }
//}
//
//int main()
//{
//    std::cout << "Astra ECS Massive Scale Benchmark\n";
//    std::cout << "==================================\n";
//    std::cout << "Testing realistic game workloads with millions of entities\n";
//    
//    // Test different scenarios
//    
//    // 1. Pure movement benchmark (best case for ECS)
//    RunMovementBenchmark(1'000'000, 500);
//    
//    // 2. Complex multi-system benchmark (realistic game scenario)
//    RunComplexSystemsBenchmark(1'000'000, 300);
//    
//    // 3. Scaling benchmark (test how performance scales)
//    RunScalingBenchmark();
//    
//    std::cout << "\nBenchmark complete!\n";
//    return 0;
//}