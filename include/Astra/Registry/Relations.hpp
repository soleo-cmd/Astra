#pragma once

#include <memory>
#include <queue>
#include <type_traits>
#include <unordered_set>

#include "../Archetype/ArchetypeStorage.hpp"
#include "../Core/Base.hpp"
#include "../Entity/Entity.hpp"
#include "../Entity/EntityPool.hpp"
#include "Query.hpp"
#include "RelationshipGraph.hpp"

namespace Astra
{
    // Forward declaration
    class Registry;
    
    /**
     * @brief Traversal order for hierarchy iteration
     */
    enum class TraversalOrder
    {
        BreadthFirst,  // Default - better for siblings with same components
        DepthFirst     // Alternative - may be better for deep hierarchies
    };
    
    /**
     * @brief Query object for filtered access to entity relationships
     * 
     * Supports component filtering with query modifiers:
     * - Basic components: Entity must have all specified components
     * - Not<T>: Entity must NOT have component T
     * - Any<T...>: Entity must have at least one of T...
     * - OneOf<T...>: Entity must have exactly one of T...
     * 
     * @tparam QueryArgs Query arguments (components and modifiers)
     */
    template<typename... QueryArgs>
    class Relations
    {
        static_assert(ValidQuery<QueryArgs...>, "Relations template arguments must be valid components or query modifiers");
        
    private:
        // Check if we have any filtering
        static constexpr bool HasFiltering = sizeof...(QueryArgs) > 0;
        
        // Extract query information only if filtering
        using Classifier = std::conditional_t<HasFiltering,
            Detail::QueryClassifier<QueryArgs...>,
            Detail::QueryClassifier<>>;
        using RequiredTuple = typename Classifier::RequiredComponents;
        using ExcludedTuple = typename Classifier::ExcludedComponents;
        using AnyGroups = typename Classifier::AnyGroups;
        using OneOfGroups = typename Classifier::OneOfGroups;
        
    public:
        using ChildrenView = const RelationshipGraph::ChildrenContainer&;
        using LinksView = const RelationshipGraph::LinksContainer&;
        
        /**
         * @brief Unified hierarchy iterator for both filtered and unfiltered access
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
                , m_graph(parent->m_graph)
                , m_descendants(descendants)
            {
                if (root.IsValid())
                {
                    // Mark root as visited to prevent cycles
                    m_visited.insert(root);
                    
                    // Start traversal from root's children/parent
                    if (descendants)
                    {
                        const auto& children = m_graph->GetChildren(root);
                        for (Entity child : children)
                        {
                            if (m_visited.find(child) == m_visited.end())
                            {
                                m_queue.push({child, 1});
                                m_visited.insert(child);
                            }
                        }
                    }
                    else
                    {
                        Entity parent = m_graph->GetParent(root);
                        if (parent.IsValid() && m_visited.find(parent) == m_visited.end())
                        {
                            m_queue.push({parent, 1});
                            m_visited.insert(parent);
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
                // Two iterators are equal if they're both at end or point to same entity
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
                // Process queue until we find a valid entity or exhaust the queue
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
                            if (m_visited.find(child) == m_visited.end())
                            {
                                m_queue.push({child, candidate.depth + 1});
                                m_visited.insert(child);
                            }
                        }
                    }
                    else
                    {
                        Entity parent = m_graph->GetParent(candidate.entity);
                        if (parent.IsValid() && m_visited.find(parent) == m_visited.end())
                        {
                            m_queue.push({parent, candidate.depth + 1});
                            m_visited.insert(parent);
                        }
                    }
                    
                    // Check if entity passes filter (always true if no filtering)
                    if (m_parent->PassesFilter(candidate.entity))
                    {
                        m_current = candidate;
                        return;
                    }
                }
                
                m_current = Entry{};  // End iterator state
            }
            
            const Relations* m_parent = nullptr;
            const RelationshipGraph* m_graph = nullptr;
            bool m_descendants = true;
            std::queue<Entry> m_queue;
            std::unordered_set<Entity> m_visited;
            Entry m_current{};
        };
        
        /**
         * @brief Range wrapper for hierarchy traversal
         */
        class HierarchyRange
        {
        public:
            HierarchyRange(const Relations* parent, Entity root, bool descendants)
                : m_parent(parent), m_root(root), m_descendants(descendants) {}
            
            HierarchyIterator begin() const { return HierarchyIterator(m_parent, m_root, m_descendants); }
            HierarchyIterator end() const { return HierarchyIterator(); }
            
        private:
            const Relations* m_parent;
            Entity m_root;
            bool m_descendants;
        };
        
        /**
         * @brief Simplified filtered view for children/links
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
                    : m_parent(parent), m_it(it), m_end(end)
                {
                    // Skip to first passing entity
                    while (m_it != m_end && !m_parent->PassesFilter(*m_it))
                    {
                        ++m_it;
                    }
                }
                
                reference operator*() const { return *m_it; }
                pointer operator->() const { return &(*m_it); }
                
                Iterator& operator++()
                {
                    ++m_it;
                    while (m_it != m_end && !m_parent->PassesFilter(*m_it))
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
                const Relations* m_parent = nullptr;
                typename Container::const_iterator m_it;
                typename Container::const_iterator m_end;
            };
            
            FilteredView(const Relations* parent, const Container& container)
                : m_parent(parent), m_container(container) {}
            
            Iterator begin() const { return Iterator(m_parent, m_container.begin(), m_container.end()); }
            Iterator end() const { return Iterator(m_parent, m_container.end(), m_container.end()); }
            bool empty() const { return begin() == end(); }
            
        private:
            const Relations* m_parent;
            const Container& m_container;
        };
        
        // Constructor
        Relations(std::shared_ptr<ArchetypeStorage> storage, 
                 std::shared_ptr<EntityPool> entityPool,
                 Entity entity, 
                 const RelationshipGraph* graph)
            : m_storage(std::move(storage))
            , m_entityPool(std::move(entityPool))
            , m_entity(entity)
            , m_graph(graph)
        {
        }
        
        /**
         * @brief Get the parent of the entity (filtered by components if applicable)
         * @return Parent entity if it exists and passes filter, Entity::Null() otherwise
         */
        ASTRA_FORCEINLINE Entity GetParent() const
        {
            Entity parent = m_graph->GetParent(m_entity);
            if constexpr (!HasFiltering)
            {
                return parent;
            }
            else
            {
                return (parent.IsValid() && PassesFilter(parent)) ? parent : Entity::Invalid();
            }
        }
        
        /**
         * @brief Get children (filtered if applicable, direct view if not)
         */
        ASTRA_FORCEINLINE auto GetChildren() const
        {
            if constexpr (!HasFiltering)
            {
                return m_graph->GetChildren(m_entity);
            }
            else
            {
                return FilteredView<RelationshipGraph::ChildrenContainer>(this, m_graph->GetChildren(m_entity));
            }
        }
        
        /**
         * @brief Get linked entities (filtered if applicable, direct view if not)
         */
        ASTRA_FORCEINLINE auto GetLinks() const
        {
            if constexpr (!HasFiltering)
            {
                return m_graph->GetLinks(m_entity);
            }
            else
            {
                return FilteredView<RelationshipGraph::LinksContainer>(this, m_graph->GetLinks(m_entity));
            }
        }
        
        /**
         * @brief Get all descendants with depth info
         */
        HierarchyRange GetDescendants() const
        {
            return HierarchyRange(this, m_entity, true);
        }
        
        /**
         * @brief Get all ancestors with depth info
         */
        HierarchyRange GetAncestors() const
        {
            return HierarchyRange(this, m_entity, false);
        }
        
        /**
         * @brief Execute function for each child entity
         */
        template<typename Func>
        void ForEachChild(Func&& func)
        {
            const auto& children = m_graph->GetChildren(m_entity);
            for (Entity child : children)
            {
                if (PassesFilter(child))
                {
                    InvokeWithComponents(child, std::forward<Func>(func));
                }
            }
        }
        
        /**
         * @brief Execute function for each descendant entity
         */
        template<typename Func>
        void ForEachDescendant(Func&& func, TraversalOrder order = TraversalOrder::BreadthFirst)
        {
            std::queue<std::pair<Entity, size_t>> queue;
            std::unordered_set<Entity> visited;
            visited.insert(m_entity);
            
            // Initialize with children
            const auto& children = m_graph->GetChildren(m_entity);
            for (Entity child : children)
            {
                if (visited.insert(child).second && PassesFilter(child))
                {
                    if (order == TraversalOrder::BreadthFirst)
                    {
                        queue.push({child, 1});
                    }
                    else
                    {
                        // For DFS, process immediately
                        InvokeWithDepth(child, 1, std::forward<Func>(func));
                        ForEachDescendantDFS(child, 1, visited, std::forward<Func>(func));
                    }
                }
            }
            
            // BFS processing
            if (order == TraversalOrder::BreadthFirst)
            {
                while (!queue.empty())
                {
                    auto [entity, depth] = queue.front();
                    queue.pop();
                    
                    InvokeWithDepth(entity, depth, std::forward<Func>(func));
                    
                    const auto& entityChildren = m_graph->GetChildren(entity);
                    for (Entity child : entityChildren)
                    {
                        if (visited.insert(child).second && PassesFilter(child))
                        {
                            queue.push({child, depth + 1});
                        }
                    }
                }
            }
        }
        
        /**
         * @brief Execute function for each linked entity
         */
        template<typename Func>
        void ForEachLink(Func&& func)
        {
            const auto& links = m_graph->GetLinks(m_entity);
            for (Entity linked : links)
            {
                if (PassesFilter(linked))
                {
                    InvokeWithComponents(linked, std::forward<Func>(func));
                }
            }
        }
        
    private:
        // DFS helper
        template<typename Func>
        void ForEachDescendantDFS(Entity current, size_t depth, 
                                 std::unordered_set<Entity>& visited, Func&& func)
        {
            const auto& children = m_graph->GetChildren(current);
            for (Entity child : children)
            {
                if (visited.insert(child).second && PassesFilter(child))
                {
                    InvokeWithDepth(child, depth + 1, std::forward<Func>(func));
                    ForEachDescendantDFS(child, depth + 1, visited, std::forward<Func>(func));
                }
            }
        }
        
        // Invoke function with components (only if filtering)
        template<typename Func>
        void InvokeWithComponents(Entity entity, Func&& func)
        {
            if constexpr (!HasFiltering)
            {
                func(entity);
            }
            else
            {
                InvokeWithComponentsImpl(entity, std::forward<Func>(func), RequiredTuple{});
            }
        }
        
        template<typename Func, typename... Components>
        void InvokeWithComponentsImpl(Entity entity, Func&& func, std::tuple<Components...>)
        {
            if constexpr (sizeof...(Components) == 0)
            {
                func(entity);
            }
            else
            {
                func(entity, *m_storage->GetComponent<Components>(entity)...);
            }
        }
        
        // Invoke function with depth and components
        template<typename Func>
        void InvokeWithDepth(Entity entity, size_t depth, Func&& func)
        {
            if constexpr (!HasFiltering)
            {
                func(entity, depth);
            }
            else
            {
                InvokeWithDepthImpl(entity, depth, std::forward<Func>(func), RequiredTuple{});
            }
        }
        
        template<typename Func, typename... Components>
        void InvokeWithDepthImpl(Entity entity, size_t depth, Func&& func, std::tuple<Components...>)
        {
            if constexpr (sizeof...(Components) == 0)
            {
                func(entity, depth);
            }
            else
            {
                func(entity, depth, *m_storage->GetComponent<Components>(entity)...);
            }
        }
        
        /**
         * @brief Simplified filter check using if constexpr
         */
        ASTRA_FORCEINLINE bool PassesFilter(Entity entity) const
        {
            if constexpr (!HasFiltering)
            {
                return true;  // No filtering, always pass
            }
            else
            {
                // Check required components
                if constexpr (std::tuple_size_v<RequiredTuple> > 0)
                {
                    if (!HasAllRequired(entity, RequiredTuple{}))
                        return false;
                }
                
                // Check excluded components
                if constexpr (std::tuple_size_v<ExcludedTuple> > 0)
                {
                    if (HasAnyExcluded(entity, ExcludedTuple{}))
                        return false;
                }
                
                // Check Any groups
                if constexpr (std::tuple_size_v<AnyGroups> > 0)
                {
                    if (!CheckAnyGroups(entity))
                        return false;
                }
                
                // Check OneOf groups
                if constexpr (std::tuple_size_v<OneOfGroups> > 0)
                {
                    if (!CheckOneOfGroups(entity))
                        return false;
                }
                
                return true;
            }
        }
        
        // Simplified component checking helpers (only compiled if HasFiltering)
        template<typename... Ts>
        bool HasAllRequired(Entity entity, std::tuple<Ts...>) const
        {
            if constexpr (sizeof...(Ts) == 0)
                return true;
            else
                return ((m_storage->GetComponent<Ts>(entity) != nullptr) && ...);
        }
        
        template<typename... Ts>
        bool HasAnyExcluded(Entity entity, std::tuple<Ts...>) const
        {
            if constexpr (sizeof...(Ts) == 0)
                return false;
            else
                return ((m_storage->GetComponent<Ts>(entity) != nullptr) || ...);
        }
        
        bool CheckAnyGroups(Entity entity) const
        {
            return CheckAnyGroupsImpl(entity, std::make_index_sequence<std::tuple_size_v<AnyGroups>>{});
        }
        
        template<size_t... Is>
        bool CheckAnyGroupsImpl(Entity entity, std::index_sequence<Is...>) const
        {
            return (CheckSingleAnyGroup<Is>(entity) && ...);
        }
        
        template<size_t I>
        bool CheckSingleAnyGroup(Entity entity) const
        {
            using Group = std::tuple_element_t<I, AnyGroups>;
            return HasAnyInGroup(entity, Group{});
        }
        
        template<typename... Ts>
        bool HasAnyInGroup(Entity entity, std::tuple<Ts...>) const
        {
            return ((m_storage->GetComponent<Ts>(entity) != nullptr) || ...);
        }
        
        bool CheckOneOfGroups(Entity entity) const
        {
            return CheckOneOfGroupsImpl(entity, std::make_index_sequence<std::tuple_size_v<OneOfGroups>>{});
        }
        
        template<size_t... Is>
        bool CheckOneOfGroupsImpl(Entity entity, std::index_sequence<Is...>) const
        {
            return (CheckSingleOneOfGroup<Is>(entity) && ...);
        }
        
        template<size_t I>
        bool CheckSingleOneOfGroup(Entity entity) const
        {
            using Group = std::tuple_element_t<I, OneOfGroups>;
            return CountInGroup(entity, Group{}) == 1;
        }
        
        template<typename... Ts>
        size_t CountInGroup(Entity entity, std::tuple<Ts...>) const
        {
            return ((m_storage->GetComponent<Ts>(entity) != nullptr ? 1 : 0) + ...);
        }
        
        std::shared_ptr<ArchetypeStorage> m_storage;
        std::shared_ptr<EntityPool> m_entityPool;
        Entity m_entity;
        const RelationshipGraph* m_graph;
    };
}