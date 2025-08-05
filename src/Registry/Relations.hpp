#pragma once

#include "../Core/Base.hpp"
#include "../Entity/Entity.hpp"
#include "RelationshipGraph.hpp"
#include "Query.hpp"
#include <queue>
#include <type_traits>

namespace Astra
{
    // Forward declaration
    class Registry;
    
    /**
     * @brief Query object for filtered access to entity relationships
     * 
     * This template class provides access to entity relationships with optional
     * component filtering. Supports the same query modifiers as View:
     * - Basic components: Entity must have all specified components
     * - Not<T>: Entity must NOT have component T
     * - AnyOf<T...>: Entity must have at least one of T...
     * - OneOf<T...>: Entity must have exactly one of T...
     * 
     * @tparam QueryArgs Query arguments (components and modifiers)
     */
    template<typename... QueryArgs>
    class Relations
    {
        static_assert(ValidQuery<QueryArgs...>, "Relations template arguments must be valid components or query modifiers");
        
    private:
        // Extract query information
        using Classifier = QueryDetail::QueryClassifier<QueryArgs...>;
        using RequiredTuple = typename Classifier::RequiredComponents;
        using ExcludedTuple = typename Classifier::ExcludedComponents;
        using AnyGroups = typename Classifier::AnyGroups;
        using OneOfGroups = typename Classifier::OneOfGroups;
        
    public:
        using ChildrenView = const RelationshipGraph::ChildrenContainer&;
        using LinksView = const RelationshipGraph::LinksContainer&;
        
        /**
         * @brief Iterator for traversing descendants/ancestors with depth information
         */
        class HierarchyIterator
        {
        public:
            struct Entry
            {
                Entity entity;
                size_t depth;
            };
            
            using iterator_category = std::forward_iterator_tag;
            using value_type = Entry;
            using difference_type = ptrdiff_t;
            using pointer = const Entry*;
            using reference = const Entry&;
            
            HierarchyIterator() = default;
            
            HierarchyIterator(const Relations* parent, Entity root, bool descendants)
                : m_parent(parent)
                , m_registry(parent->m_registry)
                , m_graph(parent->m_graph)
                , m_descendants(descendants)
            {
                if (root.IsValid())
                {
                    // Start traversal from root's children/parent
                    if (descendants)
                    {
                        const auto& children = m_graph->GetChildren(root);
                        for (Entity child : children)
                        {
                            m_queue.push({child, 1});
                        }
                    }
                    else
                    {
                        Entity parent = m_graph->GetParent(root);
                        if (parent.IsValid())
                        {
                            m_queue.push({parent, 1});
                        }
                    }
                    
                    // Find first valid element
                    Advance();
                }
            }
            
            reference operator*() const { return m_current; }
            pointer operator->() const { return &m_current; }
            
            HierarchyIterator& operator++()
            {
                Advance();
                return *this;
            }
            
            HierarchyIterator operator++(int)
            {
                HierarchyIterator tmp = *this;
                ++(*this);
                return tmp;
            }
            
            friend bool operator==(const HierarchyIterator& a, const HierarchyIterator& b)
            {
                // Two iterators are equal if they're both at end (invalid current entity)
                // or if they point to the same entity
                if (!a.m_current.entity.IsValid() && !b.m_current.entity.IsValid())
                    return true;
                if (a.m_current.entity.IsValid() && b.m_current.entity.IsValid())
                    return a.m_current.entity == b.m_current.entity && a.m_current.depth == b.m_current.depth;
                return false;
            }
            
            friend bool operator!=(const HierarchyIterator& a, const HierarchyIterator& b)
            {
                return !(a == b);
            }
            
        private:
            void Advance()
            {
                // Find next valid element
                while (!m_queue.empty())
                {
                    Entry candidate = m_queue.front();
                    m_queue.pop();
                    
                    // Add children/parent of this candidate for future traversal
                    if (m_descendants)
                    {
                        const auto& children = m_graph->GetChildren(candidate.entity);
                        for (Entity child : children)
                        {
                            m_queue.push({child, candidate.depth + 1});
                        }
                    }
                    else
                    {
                        Entity parent = m_graph->GetParent(candidate.entity);
                        if (parent.IsValid())
                        {
                            m_queue.push({parent, candidate.depth + 1});
                        }
                    }
                    
                    // Only yield entities that pass the filter
                    if (PassesFilter(candidate.entity))
                    {
                        m_current = candidate;
                        return;
                    }
                }
                
                m_current = Entry{};  // End iterator state
            }
            
            bool PassesFilter(Entity entity) const
            {
                // Delegate to parent Relations class's PassesFilter
                return m_parent->PassesFilter(entity);
            }
            
            const Relations* m_parent = nullptr;
            const Registry* m_registry = nullptr;
            const RelationshipGraph* m_graph = nullptr;
            bool m_descendants = true;
            std::queue<Entry> m_queue;
            Entry m_current{};
        };
        
        /**
         * @brief Range wrapper for hierarchy traversal
         */
        class HierarchyRange
        {
        public:
            HierarchyRange(const Relations* parent, Entity root, bool descendants)
                : m_parent(parent)
                , m_root(root)
                , m_descendants(descendants)
            {
            }
            
            HierarchyIterator begin() const
            {
                return HierarchyIterator(m_parent, m_root, m_descendants);
            }
            
            HierarchyIterator end() const
            {
                return HierarchyIterator();
            }
            
        private:
            const Relations* m_parent;
            Entity m_root;
            bool m_descendants;
        };
        
        /**
         * @brief Filtered view wrapper for children/links
         */
        template<typename Container>
        class FilteredView
        {
        public:
            class Iterator
            {
            public:
                using iterator_category = std::forward_iterator_tag;
                using value_type = Entity;
                using difference_type = ptrdiff_t;
                using pointer = const Entity*;
                using reference = const Entity&;
                
                Iterator() = default;
                
                Iterator(const Relations* parent, typename Container::const_iterator it, typename Container::const_iterator end)
                    : m_parent(parent)
                    , m_it(it)
                    , m_end(end)
                {
                    // Skip to first passing entity
                    while (m_it != m_end && !PassesFilter(*m_it))
                    {
                        ++m_it;
                    }
                }
                
                reference operator*() const { return *m_it; }
                pointer operator->() const { return &(*m_it); }
                
                Iterator& operator++()
                {
                    ++m_it;
                    while (m_it != m_end && !PassesFilter(*m_it))
                    {
                        ++m_it;
                    }
                    return *this;
                }
                
                Iterator operator++(int)
                {
                    Iterator tmp = *this;
                    ++(*this);
                    return tmp;
                }
                
                friend bool operator==(const Iterator& a, const Iterator& b)
                {
                    return a.m_it == b.m_it;
                }
                
                friend bool operator!=(const Iterator& a, const Iterator& b)
                {
                    return !(a == b);
                }
                
            private:
                bool PassesFilter(Entity entity) const
                {
                    // Delegate to parent Relations class's PassesFilter
                    return m_parent->PassesFilter(entity);
                }
                
                const Relations* m_parent = nullptr;
                typename Container::const_iterator m_it;
                typename Container::const_iterator m_end;
            };
            
            FilteredView(const Relations* parent, const Container& container)
                : m_parent(parent)
                , m_container(container)
            {
            }
            
            Iterator begin() const
            {
                return Iterator(m_parent, m_container.begin(), m_container.end());
            }
            
            Iterator end() const
            {
                return Iterator(m_parent, m_container.end(), m_container.end());
            }
            
            bool empty() const
            {
                return begin() == end();
            }
            
        private:
            const Relations* m_parent;
            const Container& m_container;
        };
        
        Relations(const Registry* registry, Entity entity, const RelationshipGraph* graph)
            : m_registry(registry)
            , m_entity(entity)
            , m_graph(graph)
        {
        }
        
        /**
         * @brief Get the parent of the entity (filtered by components)
         * @return Parent entity if it exists and passes filter, Entity::Null() otherwise
         */
        Entity GetParent() const
        {
            Entity parent = m_graph->GetParent(m_entity);
            if (parent.IsValid() && PassesFilter(parent))
            {
                return parent;
            }
            return Entity{};
        }
        
        /**
         * @brief Get filtered view of children
         * @return Filtered view of child entities
         */
        FilteredView<RelationshipGraph::ChildrenContainer> GetChildren() const
        {
            return FilteredView<RelationshipGraph::ChildrenContainer>(this, m_graph->GetChildren(m_entity));
        }
        
        /**
         * @brief Get filtered view of linked entities
         * @return Filtered view of linked entities
         */
        FilteredView<RelationshipGraph::LinksContainer> GetLinks() const
        {
            return FilteredView<RelationshipGraph::LinksContainer>(this, m_graph->GetLinks(m_entity));
        }
        
        /**
         * @brief Get all descendants (children, grandchildren, etc.) with depth info
         * @return Range for iterating descendants with depth
         */
        HierarchyRange GetDescendants() const
        {
            return HierarchyRange(this, m_entity, true);
        }
        
        /**
         * @brief Get all ancestors (parent, grandparent, etc.) with depth info
         * @return Range for iterating ancestors with depth
         */
        HierarchyRange GetAncestors() const
        {
            return HierarchyRange(this, m_entity, false);
        }
        
    private:
        // Helper to check if entity has all components in a tuple
        template<typename... Ts>
        bool HasAllComponents(Entity entity, std::tuple<Ts...>*) const
        {
            if constexpr (sizeof...(Ts) == 0)
                return true;
            else
                return ((m_registry->GetComponent<Ts>(entity) != nullptr) && ...);
        }
        
        // Helper to check if entity has any components in a tuple
        template<typename... Ts>
        bool HasAnyComponent(Entity entity, std::tuple<Ts...>*) const
        {
            if constexpr (sizeof...(Ts) == 0)
                return false;
            else
                return ((m_registry->GetComponent<Ts>(entity) != nullptr) || ...);
        }
        
        // Helper to count how many components entity has from a tuple
        template<typename... Ts>
        size_t CountComponents(Entity entity, std::tuple<Ts...>*) const
        {
            if constexpr (sizeof...(Ts) == 0)
                return 0;
            else
                return ((m_registry->GetComponent<Ts>(entity) != nullptr ? 1 : 0) + ...);
        }
        
        // Check a single AnyOf group
        template<size_t I>
        bool CheckAnyOfGroup(Entity entity) const
        {
            if constexpr (I < std::tuple_size_v<AnyGroups>)
            {
                using Group = std::tuple_element_t<I, AnyGroups>;
                return HasAnyComponent(entity, static_cast<Group*>(nullptr));
            }
            return true;
        }
        
        // Check all AnyOf groups
        template<size_t... Is>
        bool CheckAllAnyOfGroups(Entity entity, std::index_sequence<Is...>) const
        {
            return (CheckAnyOfGroup<Is>(entity) && ...);
        }
        
        // Check a single OneOf group
        template<size_t I>
        bool CheckOneOfGroup(Entity entity) const
        {
            if constexpr (I < std::tuple_size_v<OneOfGroups>)
            {
                using Group = std::tuple_element_t<I, OneOfGroups>;
                return CountComponents(entity, static_cast<Group*>(nullptr)) == 1;
            }
            return true;
        }
        
        // Check all OneOf groups
        template<size_t... Is>
        bool CheckAllOneOfGroups(Entity entity, std::index_sequence<Is...>) const
        {
            return (CheckOneOfGroup<Is>(entity) && ...);
        }
        
        bool PassesFilter(Entity entity) const
        {
            // No filtering if no query args
            if constexpr (sizeof...(QueryArgs) == 0)
            {
                return true;
            }
            else
            {
                // Check required components
                if (!HasAllComponents(entity, static_cast<RequiredTuple*>(nullptr)))
                    return false;
                
                // Check excluded components
                if (HasAnyComponent(entity, static_cast<ExcludedTuple*>(nullptr)))
                    return false;
                
                // Check AnyOf groups
                constexpr size_t anyGroupCount = std::tuple_size_v<AnyGroups>;
                if constexpr (anyGroupCount > 0)
                {
                    if (!CheckAllAnyOfGroups(entity, std::make_index_sequence<anyGroupCount>{}))
                        return false;
                }
                
                // Check OneOf groups
                constexpr size_t oneOfGroupCount = std::tuple_size_v<OneOfGroups>;
                if constexpr (oneOfGroupCount > 0)
                {
                    if (!CheckAllOneOfGroups(entity, std::make_index_sequence<oneOfGroupCount>{}))
                        return false;
                }
                
                return true;
            }
        }
        
        const Registry* m_registry;
        Entity m_entity;
        const RelationshipGraph* m_graph;
    };
    
    /**
     * @brief Specialization for unfiltered relationship access
     * 
     * This specialization provides direct access to relationships without
     * any component filtering, offering better performance when filtering
     * is not needed.
     */
    template<>
    class Relations<>
    {
    public:
        using ChildrenView = const RelationshipGraph::ChildrenContainer&;
        using LinksView = const RelationshipGraph::LinksContainer&;
        
        Relations(const Registry* registry, Entity entity, const RelationshipGraph* graph)
            : m_entity(entity)
            , m_graph(graph)
        {
            (void)registry; // Unused in unfiltered version
        }
        
        /**
         * @brief Get the parent of the entity (unfiltered)
         * @return Parent entity if it exists, Entity::Null() otherwise
         */
        Entity GetParent() const
        {
            return m_graph->GetParent(m_entity);
        }
        
        /**
         * @brief Get all children (unfiltered)
         * @return Reference to children container
         */
        ChildrenView GetChildren() const
        {
            return m_graph->GetChildren(m_entity);
        }
        
        /**
         * @brief Get all linked entities (unfiltered)
         * @return Reference to links container
         */
        LinksView GetLinks() const
        {
            return m_graph->GetLinks(m_entity);
        }
        
        /**
         * @brief Simple hierarchy iterator for unfiltered access
         */
        class SimpleHierarchyIterator
        {
        public:
            struct Entry
            {
                Entity entity;
                size_t depth;
            };
            
            using iterator_category = std::forward_iterator_tag;
            using value_type = Entry;
            using difference_type = ptrdiff_t;
            using pointer = const Entry*;
            using reference = const Entry&;
            
            SimpleHierarchyIterator() = default;
            
            SimpleHierarchyIterator(Entity root, const RelationshipGraph* graph, bool descendants)
                : m_graph(graph)
                , m_descendants(descendants)
            {
                if (root.IsValid())
                {
                    if (descendants)
                    {
                        const auto& children = m_graph->GetChildren(root);
                        for (Entity child : children)
                        {
                            m_queue.push({child, 1});
                        }
                    }
                    else
                    {
                        Entity parent = m_graph->GetParent(root);
                        if (parent.IsValid())
                        {
                            m_queue.push({parent, 1});
                        }
                    }
                    
                    // Process first element if queue is not empty
                    if (!m_queue.empty())
                    {
                        m_current = m_queue.front();
                        m_queue.pop();
                    }
                }
            }
            
            reference operator*() const { return m_current; }
            pointer operator->() const { return &m_current; }
            
            SimpleHierarchyIterator& operator++()
            {
                Advance();
                return *this;
            }
            
            SimpleHierarchyIterator operator++(int)
            {
                SimpleHierarchyIterator tmp = *this;
                ++(*this);
                return tmp;
            }
            
            friend bool operator==(const SimpleHierarchyIterator& a, const SimpleHierarchyIterator& b)
            {
                // Two iterators are equal if they're both at end (invalid current entity)
                // or if they point to the same entity
                if (!a.m_current.entity.IsValid() && !b.m_current.entity.IsValid())
                    return true;
                if (a.m_current.entity.IsValid() && b.m_current.entity.IsValid())
                    return a.m_current.entity == b.m_current.entity && a.m_current.depth == b.m_current.depth;
                return false;
            }
            
            friend bool operator!=(const SimpleHierarchyIterator& a, const SimpleHierarchyIterator& b)
            {
                return !(a == b);
            }
            
        private:
            void Advance()
            {
                // First, add children/parent of current entity to queue
                if (m_current.entity.IsValid())
                {
                    if (m_descendants)
                    {
                        const auto& children = m_graph->GetChildren(m_current.entity);
                        for (Entity child : children)
                        {
                            m_queue.push({child, m_current.depth + 1});
                        }
                    }
                    else
                    {
                        Entity parent = m_graph->GetParent(m_current.entity);
                        if (parent.IsValid())
                        {
                            m_queue.push({parent, m_current.depth + 1});
                        }
                    }
                }
                
                // Now get the next element from queue
                if (!m_queue.empty())
                {
                    m_current = m_queue.front();
                    m_queue.pop();
                }
                else
                {
                    m_current = Entry{};  // End iterator state
                }
            }
            
            const RelationshipGraph* m_graph = nullptr;
            bool m_descendants = true;
            std::queue<Entry> m_queue;
            Entry m_current{};
        };
        
        class SimpleHierarchyRange
        {
        public:
            SimpleHierarchyRange(Entity root, const RelationshipGraph* graph, bool descendants)
                : m_root(root)
                , m_graph(graph)
                , m_descendants(descendants)
            {
            }
            
            SimpleHierarchyIterator begin() const
            {
                return SimpleHierarchyIterator(m_root, m_graph, m_descendants);
            }
            
            SimpleHierarchyIterator end() const
            {
                return SimpleHierarchyIterator();
            }
            
        private:
            Entity m_root;
            const RelationshipGraph* m_graph;
            bool m_descendants;
        };
        
        /**
         * @brief Get all descendants (unfiltered)
         * @return Range for iterating descendants with depth
         */
        SimpleHierarchyRange GetDescendants() const
        {
            return SimpleHierarchyRange(m_entity, m_graph, true);
        }
        
        /**
         * @brief Get all ancestors (unfiltered)
         * @return Range for iterating ancestors with depth
         */
        SimpleHierarchyRange GetAncestors() const
        {
            return SimpleHierarchyRange(m_entity, m_graph, false);
        }
        
    private:
        Entity m_entity;
        const RelationshipGraph* m_graph;
    };
}