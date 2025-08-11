#pragma once

#include <array>
#include <memory>

#include "../Component/Component.hpp"
#include "../Container/FlatMap.hpp"
#include "../Core/Base.hpp"

namespace Astra
{
    // Forward declarations
    class Archetype;

    /**
     * @brief Optimized edge storage for archetype graph transitions
     * 
     * Uses a hybrid approach:
     * - Direct array indexing for low component IDs (0-255) for O(1) access
     * - Hash map for high component IDs (256+) for flexibility
     * 
     * This optimization is based on the observation that most frequently used
     * components have low IDs, as they're registered early in the application.
     * 
     * Performance characteristics:
     * - Fast path (ID < 256): O(1) lookup, no hash computation
     * - Slow path (ID >= 256): O(1) average, uses FlatMap
     * - Memory: 256 * sizeof(Edge) + FlatMap overhead
     */
    class ArchetypeEdgeStorage
    {
    public:
        // Configurable threshold for fast path
        // Components with ID < FAST_PATH_THRESHOLD use array indexing
        static constexpr ComponentID FAST_PATH_THRESHOLD = 256;
    
        struct Edge
        {
            Archetype* to = nullptr;
            ComponentID componentId = INVALID_COMPONENT;
        
            Edge() = default;
            Edge(Archetype* target, ComponentID id) noexcept : to(target), componentId(id) {}
        
            [[nodiscard]] bool IsValid() const noexcept
            {
                return to != nullptr && componentId != INVALID_COMPONENT;
            }
        
            void Reset() noexcept
            {
                to = nullptr;
                componentId = INVALID_COMPONENT;
            }
        
            bool operator==(const Edge& other) const noexcept
            {
                return to == other.to && componentId == other.componentId;
            }
        
            bool operator!=(const Edge& other) const noexcept
            {
                return !(*this == other);
            }
        };
    
        ArchetypeEdgeStorage() noexcept
        {
            // Initialize fast path array with invalid edges
            // This is important for correctness - uninitialized edges should be invalid
            for (auto& edge : m_fastPath)
            {
                edge.Reset();
            }
        }
    
        /**
         * @brief Add an edge for a component transition
         * @param componentId The component ID for this transition
         * @param targetArchetype The archetype to transition to
         */
        void AddEdge(ComponentID componentId, Archetype* targetArchetype)
        {
            ASTRA_ASSERT(targetArchetype != nullptr, "Target archetype cannot be null");
            ASTRA_ASSERT(componentId != INVALID_COMPONENT, "Invalid component ID");
        
            if (componentId < FAST_PATH_THRESHOLD)
            {
                // Fast path: direct array indexing
                m_fastPath[componentId] = Edge(targetArchetype, componentId);
                m_hasFastPathEdges = true;
            }
            else
            {
                // Slow path: hash map for high IDs
                m_slowPath.Insert({componentId, Edge(targetArchetype, componentId)});
                m_hasSlowPathEdges = true;
            }
        }
    
        /**
         * @brief Get an edge for a component transition
         * @param componentId The component ID to look up
         * @return Pointer to the edge if found, nullptr otherwise
         */
        [[nodiscard]] const Edge* GetEdge(ComponentID componentId) const noexcept
        {
            if (componentId < FAST_PATH_THRESHOLD)
            {
                // Fast path: direct array access - no branching needed
                const Edge& edge = m_fastPath[componentId];
                return edge.IsValid() ? &edge : nullptr;
            }
            else
            {
                // Slow path: hash map lookup
                auto it = m_slowPath.Find(componentId);
                return it != m_slowPath.end() ? &it->second : nullptr;
            }
        }
    
        /**
         * @brief Get or create an edge for a component transition
         * @param componentId The component ID
         * @param targetArchetype The archetype to transition to
         * @return Pointer to the edge (never null)
         */
        Edge* GetOrCreateEdge(ComponentID componentId, Archetype* targetArchetype)
        {
            ASTRA_ASSERT(targetArchetype != nullptr, "Target archetype cannot be null");
            ASTRA_ASSERT(componentId != INVALID_COMPONENT, "Invalid component ID");
        
            if (componentId < FAST_PATH_THRESHOLD)
            {
                // Fast path
                Edge& edge = m_fastPath[componentId];
                if (!edge.IsValid())
                {
                    edge = Edge(targetArchetype, componentId);
                    m_hasFastPathEdges = true;
                }
                return &edge;
            }
            else
            {
                // Slow path
                auto [it, inserted] = m_slowPath.Insert({componentId, Edge(targetArchetype, componentId)});
                if (inserted)
                {
                    m_hasSlowPathEdges = true;
                }
                return &it->second;
            }
        }
    
        /**
         * @brief Remove an edge for a component
         * @param componentId The component ID to remove
         * @return true if an edge was removed
         */
        bool RemoveEdge(ComponentID componentId)
        {
            if (componentId < FAST_PATH_THRESHOLD)
            {
                // Fast path
                Edge& edge = m_fastPath[componentId];
                if (edge.IsValid())
                {
                    edge.Reset();
                    UpdateFastPathFlag();
                    return true;
                }
            }
            else
            {
                // Slow path
                if (m_slowPath.Erase(componentId))
                {
                    m_hasSlowPathEdges = !m_slowPath.Empty();
                    return true;
                }
            }
            return false;
        }
    
        /**
         * @brief Remove all edges pointing to a specific archetype
         * @param archetype The target archetype to remove
         * @return Number of edges removed
         */
        size_t RemoveEdgesTo(Archetype* archetype)
        {
            ASTRA_ASSERT(archetype != nullptr, "Archetype cannot be null");
        
            size_t removed = 0;
        
            // Check fast path
            for (auto& edge : m_fastPath)
            {
                if (edge.to == archetype)
                {
                    edge.Reset();
                    ++removed;
                }
            }
        
            if (removed > 0)
            {
                UpdateFastPathFlag();
            }
        
            // Check slow path
            auto it = m_slowPath.begin();
            while (it != m_slowPath.end())
            {
                if (it->second.to == archetype)
                {
                    it = m_slowPath.Erase(it);
                    ++removed;
                }
                else
                {
                    ++it;
                }
            }
        
            if (removed > 0)
            {
                m_hasSlowPathEdges = !m_slowPath.Empty();
            }
        
            return removed;
        }
    
        /**
         * @brief Clear all edges
         */
        void Clear()
        {
            // Reset fast path
            for (auto& edge : m_fastPath)
            {
                edge.Reset();
            }
        
            // Clear slow path
            m_slowPath.Clear();
        
            // Update flags
            m_hasFastPathEdges = false;
            m_hasSlowPathEdges = false;
        }
    
        /**
         * @brief Check if there are any edges
         */
        [[nodiscard]] bool Empty() const noexcept
        {
            return !m_hasFastPathEdges && !m_hasSlowPathEdges;
        }
    
        /**
         * @brief Get the number of edges
         */
        [[nodiscard]] size_t Size() const noexcept
        {
            size_t count = 0;
        
            // Count fast path edges
            if (m_hasFastPathEdges)
            {
                for (const auto& edge : m_fastPath)
                {
                    if (edge.IsValid())
                    {
                        ++count;
                    }
                }
            }
        
            // Add slow path edges
            return count + m_slowPath.Size();
        }
    
        /**
         * @brief Visit all edges with a callback
         * @param visitor Callback that receives (ComponentID, const Edge&)
         */
        template<typename Visitor>
        void ForEachEdge(Visitor&& visitor) const
        {
            // Visit fast path edges first (likely to be more frequently accessed)
            if (m_hasFastPathEdges)
            {
                for (ComponentID id = 0; id < FAST_PATH_THRESHOLD; ++id)
                {
                    const Edge& edge = m_fastPath[id];
                    if (edge.IsValid())
                    {
                        visitor(id, edge);
                    }
                }
            }
        
            // Visit slow path edges
            if (m_hasSlowPathEdges)
            {
                for (const auto& [id, edge] : m_slowPath)
                {
                    visitor(id, edge);
                }
            }
        }
    
        /**
         * @brief Get statistics about edge storage
         */
        struct Stats
        {
            size_t fastPathEdges = 0;
            size_t slowPathEdges = 0;
            size_t totalCapacity = FAST_PATH_THRESHOLD;
            double fastPathUtilization = 0.0;
            size_t memoryUsage = 0;
        };
    
        [[nodiscard]] Stats GetStats() const noexcept
        {
            Stats stats;
        
            // Count fast path edges
            if (m_hasFastPathEdges)
            {
                for (const auto& edge : m_fastPath)
                {
                    if (edge.IsValid())
                    {
                        ++stats.fastPathEdges;
                    }
                }
            }
        
            stats.slowPathEdges = m_slowPath.Size();
            stats.fastPathUtilization = static_cast<double>(stats.fastPathEdges) / FAST_PATH_THRESHOLD;
        
            // Calculate memory usage
            stats.memoryUsage = sizeof(m_fastPath) + sizeof(m_slowPath) + (stats.slowPathEdges * sizeof(Edge));
        
            return stats;
        }
    
        /**
         * @brief Check if a component ID would use the fast path
         */
        [[nodiscard]] static constexpr bool UsesFastPath(ComponentID id) noexcept
        {
            return id < FAST_PATH_THRESHOLD;
        }

    private:
        /**
        * @brief Update the fast path flag by checking if any edges are valid
        */
        void UpdateFastPathFlag()
        {
            m_hasFastPathEdges = false;
            for (const auto& edge : m_fastPath)
            {
                if (edge.IsValid()) {
                    m_hasFastPathEdges = true;
                    break;
                }
            }
        }

        // Fast path: Direct array indexing for low component IDs
        // Aligned to cache line for better performance
        alignas(CACHE_LINE_SIZE) std::array<Edge, FAST_PATH_THRESHOLD> m_fastPath;
    
        // Slow path: Hash map for high component IDs
        FlatMap<ComponentID, Edge> m_slowPath;
    
        // Flags to avoid checking empty containers
        // These are hot data, keep them together for cache locality
        bool m_hasFastPathEdges = false;
        bool m_hasSlowPathEdges = false;
    };
} // namespace Astra