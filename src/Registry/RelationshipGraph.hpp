#pragma once

#include "../Core/Base.hpp"
#include "../Container/FlatMap.hpp"
#include "../Container/SmallVector.hpp"
#include "../Entity/Entity.hpp"

namespace Astra
{
    /**
     * @brief Graph-based storage for entity relationships (parent-child hierarchies and links)
     * 
     * This class manages relationships between entities separately from the component
     * system to avoid archetype fragmentation. Supports:
     * - Parent-child hierarchies (one parent per entity)
     * - Bidirectional links between entities
     * 
     * All relationships are automatically cleaned up when entities are destroyed.
     */
    class RelationshipGraph
    {
    public:
        using ChildrenContainer = SmallVector<Entity, 4>;
        using LinksContainer = SmallVector<Entity, 8>;

        // Parent-child relationships
        
        /**
         * @brief Set the parent of an entity
         * @param child The child entity
         * @param parent The parent entity
         * 
         * If the child already has a parent, it will be removed from the old parent's children.
         * This operation maintains consistency of both parent and children mappings.
         */
        void SetParent(Entity child, Entity parent)
        {
            ASTRA_ASSERT(child != parent, "Entity cannot be its own parent");
            ASTRA_ASSERT(child.IsValid() && parent.IsValid(), "Invalid entity in relationship");
            
            // Remove from old parent if exists
            RemoveParent(child);
            
            // Set new parent
            m_parents[child] = parent;
            m_children[parent].push_back(child);
        }
        
        /**
         * @brief Remove the parent of an entity
         * @param child The child entity
         */
        void RemoveParent(Entity child)
        {
            auto it = m_parents.Find(child);
            if (it != m_parents.end())
            {
                Entity parent = it->second;
                m_parents.Erase(it);
                
                // Remove from parent's children list
                auto& children = m_children[parent];
                children.erase(std::remove(children.begin(), children.end(), child), children.end());
                
                // Clean up empty children container
                if (children.empty())
                {
                    m_children.Erase(parent);
                }
            }
        }
        
        /**
         * @brief Get the parent of an entity
         * @param child The child entity
         * @return The parent entity, or Entity::Null() if no parent
         */
        Entity GetParent(Entity child) const
        {
            auto it = m_parents.Find(child);
            return (it != m_parents.end()) ? it->second : Entity{};
        }
        
        /**
         * @brief Check if an entity has a parent
         * @param child The child entity
         * @return True if the entity has a parent
         */
        bool HasParent(Entity child) const
        {
            return m_parents.Contains(child);
        }
        
        /**
         * @brief Get the children of an entity
         * @param parent The parent entity
         * @return View of the children (may be empty)
         */
        const ChildrenContainer& GetChildren(Entity parent) const
        {
            auto it = m_children.Find(parent);
            return (it != m_children.end()) ? it->second : s_emptyChildren;
        }
        
        /**
         * @brief Check if an entity has children
         * @param parent The parent entity
         * @return True if the entity has at least one child
         */
        bool HasChildren(Entity parent) const
        {
            auto it = m_children.Find(parent);
            return it != m_children.end() && !it->second.empty();
        }
        
        // Link relationships
        
        /**
         * @brief Add a bidirectional link between two entities
         * @param a First entity
         * @param b Second entity
         * 
         * Links are always bidirectional - both entities will see each other as linked.
         */
        void AddLink(Entity a, Entity b)
        {
            ASTRA_ASSERT(a != b, "Entity cannot link to itself");
            ASTRA_ASSERT(a.IsValid() && b.IsValid(), "Invalid entity in link");
            
            // Add bidirectional link
            auto& linksA = m_links[a];
            if (std::find(linksA.begin(), linksA.end(), b) == linksA.end())
            {
                linksA.push_back(b);
            }
            
            auto& linksB = m_links[b];
            if (std::find(linksB.begin(), linksB.end(), a) == linksB.end())
            {
                linksB.push_back(a);
            }
        }
        
        /**
         * @brief Remove a link between two entities
         * @param a First entity
         * @param b Second entity
         */
        void RemoveLink(Entity a, Entity b)
        {
            auto RemoveFromLinks = [this](Entity from, Entity to)
            {
                auto it = m_links.Find(from);
                if (it != m_links.end())
                {
                    auto& links = it->second;
                    links.erase(std::remove(links.begin(), links.end(), to), links.end());
                    
                    if (links.empty())
                    {
                        m_links.Erase(it);
                    }
                }
            };
            
            RemoveFromLinks(a, b);
            RemoveFromLinks(b, a);
        }
        
        /**
         * @brief Get all entities linked to the given entity
         * @param entity The entity to query
         * @return View of linked entities (may be empty)
         */
        const LinksContainer& GetLinks(Entity entity) const
        {
            auto it = m_links.Find(entity);
            return (it != m_links.end()) ? it->second : s_emptyLinks;
        }
        
        /**
         * @brief Check if two entities are linked
         * @param a First entity
         * @param b Second entity
         * @return True if the entities are linked
         */
        bool AreLinked(Entity a, Entity b) const
        {
            auto it = m_links.Find(a);
            if (it != m_links.end())
            {
                const auto& links = it->second;
                return std::find(links.begin(), links.end(), b) != links.end();
            }
            return false;
        }
        
        /**
         * @brief Check if an entity has any links
         * @param entity The entity to check
         * @return True if the entity has at least one link
         */
        bool HasLinks(Entity entity) const
        {
            auto it = m_links.Find(entity);
            return it != m_links.end() && !it->second.empty();
        }
        
        // Entity cleanup
        
        /**
         * @brief Remove all relationships for a destroyed entity
         * @param entity The entity being destroyed
         * 
         * This should be called by the Registry when an entity is destroyed.
         * It removes the entity from all relationships:
         * - Removes it as a child from its parent
         * - Removes all its children (orphaning them)
         * - Removes all links to/from this entity
         */
        void OnEntityDestroyed(Entity entity)
        {
            // Remove as child from parent
            RemoveParent(entity);
            
            // Remove all children (they become orphaned)
            auto childrenIt = m_children.Find(entity);
            if (childrenIt != m_children.end())
            {
                // Clear parent references for all children
                for (Entity child : childrenIt->second)
                {
                    m_parents.Erase(child);
                }
                m_children.Erase(childrenIt);
            }
            
            // Remove all links
            auto linksIt = m_links.Find(entity);
            if (linksIt != m_links.end())
            {
                // Remove this entity from all linked entities
                for (Entity linked : linksIt->second)
                {
                    auto otherIt = m_links.Find(linked);
                    if (otherIt != m_links.end())
                    {
                        auto& otherLinks = otherIt->second;
                        otherLinks.erase(std::remove(otherLinks.begin(), otherLinks.end(), entity), otherLinks.end());
                        
                        if (otherLinks.empty())
                        {
                            m_links.Erase(otherIt);
                        }
                    }
                }
                m_links.Erase(linksIt);
            }
        }
        
        // Statistics
        
        /**
         * @brief Get the total number of parent-child relationships
         * @return Number of entities that have a parent
         */
        size_t GetParentChildCount() const { return m_parents.Size(); }
        
        /**
         * @brief Get the total number of entities with children
         * @return Number of entities that have at least one child
         */
        size_t GetParentCount() const { return m_children.Size(); }
        
        /**
         * @brief Get the total number of entities with links
         * @return Number of entities that have at least one link
         */
        size_t GetLinkedEntityCount() const { return m_links.Size(); }
        
        /**
         * @brief Clear all relationships
         */
        void Clear()
        {
            m_parents.Clear();
            m_children.Clear();
            m_links.Clear();
        }

    private:
        // Parent-child relationships
        FlatMap<Entity, Entity> m_parents;                    // child -> parent
        FlatMap<Entity, ChildrenContainer> m_children;       // parent -> children
        
        // Link relationships
        FlatMap<Entity, LinksContainer> m_links;             // entity -> linked entities
        
        // Empty containers for const references
        static inline const ChildrenContainer s_emptyChildren{};
        static inline const LinksContainer s_emptyLinks{};
    };
}