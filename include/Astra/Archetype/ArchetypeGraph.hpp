#pragma once

#include "../Component/Component.hpp"
#include "../Container/FlatMap.hpp"
#include "../Core/Base.hpp"

namespace Astra
{
    class Archetype;
    
    /**
     * Manages the graph of archetypes and their transitions via component addition/removal.
     * This graph allows O(1) archetype lookups when adding or removing components from entities.
     * 
     * Uses FlatMap for high-performance lookups with SIMD acceleration.
     */
    class ArchetypeGraph
    {
    public:
        ArchetypeGraph() = default;
        ~ArchetypeGraph() = default;
        
        // Disable copy, allow move
        ArchetypeGraph(const ArchetypeGraph&) = delete;
        ArchetypeGraph& operator=(const ArchetypeGraph&) = delete;
        ArchetypeGraph(ArchetypeGraph&&) = default;
        ArchetypeGraph& operator=(ArchetypeGraph&&) = default;
        
        /**
         * Set or update an edge for adding a component.
         * @param from Source archetype
         * @param componentId Component being added
         * @param to Target archetype (with the component)
         */
        void SetAddEdge(Archetype* from, ComponentID componentId, Archetype* to)
        {
            auto& edges = m_addEdges[from];
            edges.Insert({componentId, to});
        }
        
        /**
         * Set or update an edge for removing a component.
         * @param from Source archetype
         * @param componentId Component being removed
         * @param to Target archetype (without the component)
         */
        void SetRemoveEdge(Archetype* from, ComponentID componentId, Archetype* to)
        {
            auto& edges = m_removeEdges[from];
            edges.Insert({componentId, to});
        }
        
        /**
         * Get the target archetype when adding a component.
         * @param from Source archetype
         * @param componentId Component to add
         * @return Target archetype or nullptr if edge doesn't exist
         */
        ASTRA_NODISCARD Archetype* GetAddEdge(Archetype* from, ComponentID componentId) const noexcept
        {
            auto it = m_addEdges.Find(from);
            if (it == m_addEdges.end())
                return nullptr;
                
            auto edgeIt = it->second.Find(componentId);
            return edgeIt != it->second.end() ? edgeIt->second : nullptr;
        }
        
        /**
         * Get the target archetype when removing a component.
         * @param from Source archetype
         * @param componentId Component to remove
         * @return Target archetype or nullptr if edge doesn't exist
         */
        ASTRA_NODISCARD Archetype* GetRemoveEdge(Archetype* from, ComponentID componentId) const noexcept
        {
            auto it = m_removeEdges.Find(from);
            if (it == m_removeEdges.end())
                return nullptr;
                
            auto edgeIt = it->second.Find(componentId);
            return edgeIt != it->second.end() ? edgeIt->second : nullptr;
        }
        
        /**
         * Remove all edges pointing to a specific archetype.
         * Used when an archetype is being destroyed.
         * @param target Archetype to remove edges to
         * @return Number of edges removed
         */
        size_t RemoveEdgesTo(Archetype* target)
        {
            size_t removed = 0;
            
            // Remove from add edges
            for (auto& [from, edges] : m_addEdges)
            {
                auto it = edges.begin();
                while (it != edges.end())
                {
                    if (it->second == target)
                    {
                        it = edges.Erase(it);
                        ++removed;
                    }
                    else
                    {
                        ++it;
                    }
                }
            }
            
            // Remove from remove edges
            for (auto& [from, edges] : m_removeEdges)
            {
                auto it = edges.begin();
                while (it != edges.end())
                {
                    if (it->second == target)
                    {
                        it = edges.Erase(it);
                        ++removed;
                    }
                    else
                    {
                        ++it;
                    }
                }
            }
            
            return removed;
        }
        
        /**
         * Remove all edges from a specific archetype.
         * Used when an archetype is being destroyed.
         * @param from Archetype to remove edges from
         */
        void RemoveEdgesFrom(Archetype* from)
        {
            m_addEdges.Erase(from);
            m_removeEdges.Erase(from);
        }
        
        /**
         * Clear all edges in the graph.
         */
        void Clear()
        {
            m_addEdges.Clear();
            m_removeEdges.Clear();
        }
        
        /**
         * Get the total number of edges in the graph.
         */
        ASTRA_NODISCARD size_t GetEdgeCount() const noexcept
        {
            size_t count = 0;
            for (const auto& [from, edges] : m_addEdges)
            {
                count += edges.Size();
            }
            for (const auto& [from, edges] : m_removeEdges)
            {
                count += edges.Size();
            }
            return count;
        }
        
    private:
        // Use FlatMap for high-performance lookups
        // Outer map: Archetype* -> edge map
        // Inner map: ComponentID -> target Archetype*
        FlatMap<Archetype*, FlatMap<ComponentID, Archetype*>> m_addEdges;
        FlatMap<Archetype*, FlatMap<ComponentID, Archetype*>> m_removeEdges;
    };
}