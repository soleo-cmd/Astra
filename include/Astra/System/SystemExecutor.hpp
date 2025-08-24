#pragma once

#include <future>
#include <vector>

#include "SystemMetadata.hpp"

namespace Astra
{
    /**
     * @brief Abstract interface for system execution strategies
     * 
     * This is the main integration point for custom job systems.
     * Users can implement this interface to integrate Astra's system
     * scheduler with their own threading/job system.
     * 
     * The executor receives a SystemExecutionContext which contains:
     * - Pre-analyzed parallel groups
     * - System execution functions
     * - System metadata (for debugging/profiling)
     * 
     * Example custom executor:
     * @code
     * class EnkiTSExecutor : public ISystemExecutor {
     * public:
     *     void Execute(const SystemExecutionContext& context) override {
     *         for (const auto& group : context.parallelGroups) {
     *             if (group.size() == 1) {
     *                 // Single system - run directly
     *                 context.systems[group[0]](*context.registry);
     *             } else {
     *                 // Multiple systems - submit to enkiTS job system
     *                 enki::TaskSet tasks(group.size(), [&](uint32_t i) {
     *                     context.systems[group[i]](*context.registry);
     *                 });
     *                 scheduler.AddTaskSetToPipe(&tasks);
     *                 scheduler.WaitforTask(&tasks);
     *             }
     *         }
     *     }
     * };
     * @endcode
     */
    class ISystemExecutor
    {
    public:
        virtual ~ISystemExecutor() = default;
        
        /**
         * Execute systems according to the provided execution context.
         * 
         * The parallel groups in the context represent:
         * - Groups that must execute sequentially (outer vector)
         * - Systems within each group that can run in parallel (inner vector)
         * 
         * @param context Contains parallel groups, system functions, and metadata
         */
        virtual void Execute(const SystemExecutionContext& context) = 0;
    };
    
    /**
     * @brief Default sequential executor
     * 
     * Executes all systems sequentially in a single thread.
     * This is the safest executor and is used by default when no
     * Read/Write hints are provided.
     */
    class SequentialExecutor : public ISystemExecutor
    {
    public:
        void Execute(const SystemExecutionContext& context) override
        {
            for (const auto& group : context.parallelGroups)
            {
                for (size_t systemIdx : group)
                {
                    context.systems[systemIdx](*context.registry);
                }
            }
        }
    };
    
    /**
     * @brief Default parallel executor using std::async
     * 
     * Uses std::async to execute systems in parallel when safe.
     * Systems within the same parallel group are executed concurrently,
     * while groups are executed sequentially.
     * 
     * This executor is suitable for:
     * - Simple parallelization needs
     * - Prototyping
     * - Applications without a dedicated job system
     * 
     * For production use with complex threading requirements,
     * consider implementing a custom executor.
     */
    class ParallelExecutor : public ISystemExecutor
    {
    public:
        void Execute(const SystemExecutionContext& context) override
        {
            for (const auto& group : context.parallelGroups)
            {
                if (group.size() == 1)
                {
                    // Single system in group - run directly to avoid overhead
                    context.systems[group[0]](*context.registry);
                }
                else
                {
                    // Multiple systems can run in parallel
                    std::vector<std::future<void>> futures;
                    futures.reserve(group.size());
                    
                    for (size_t systemIdx : group)
                    {
                        futures.push_back(std::async(std::launch::async,
                            [&context, systemIdx]() {
                                context.systems[systemIdx](*context.registry);
                            }));
                    }
                    
                    // Wait for all systems in this group to complete
                    for (auto& future : futures)
                    {
                        future.wait();
                    }
                }
            }
        }
    };
    
} // namespace Astra