#pragma once

#include <concepts>
#include <iostream>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <tuple>
#include <vector>

#include "../Component/Component.hpp"
#include "../Entity/Entity.hpp"
#include "Archetype.hpp"
#include "ArchetypeStorage.hpp"
#include "Query.hpp"

namespace Astra
{
    /**
     * View provides efficient iteration over entities with specific components.
     * 
     * Supports query modifiers:
     * - Optional<T>: Entity may or may not have component T
     * - Not<T>: Entity must NOT have component T
     * - Any<T...>: Entity must have at least one of T...
     * - OneOf<T...>: Entity must have exactly one of T...
     * 
     * Supports two iteration styles:
     * 1. ForEach - Maximum performance (~1-2ns/entity)
     * 2. Range-based for loops - Clean syntax (~3-4ns/entity) with structured bindings
     * 
     * For performance-critical code, prefer ForEach. For readability, use range-based for.
     * 
     * Example:
     *   // Basic query
     *   View<Position, Velocity> view1;
     *   
     *   // With optional component
     *   View<Position, Velocity, Optional<Health>> view2;
     *   
     *   // Exclude static entities
     *   View<Position, Not<Static>> view3;
     *   
     *   // Either velocity or acceleration
     *   View<Position, Any<Velocity, Acceleration>> view4;
     */
    template<typename... QueryArgs>
    class View
    {
        static_assert(ValidQuery<QueryArgs...>, "View template arguments must be valid components or query modifiers");
        
    private:
        // Extract component types for iteration
        using QueryClassifier = QueryDetail::QueryClassifier<QueryArgs...>;
        using RequiredTuple = typename QueryClassifier::RequiredComponents;
        using OptionalTuple = typename QueryClassifier::OptionalComponents;
        
        // Components to iterate over (required + optional)
        template<typename Required, typename Optional>
        struct CombineComponents;
        
        template<typename... Rs, typename... Os>
        struct CombineComponents<std::tuple<Rs...>, std::tuple<Os...>>
        {
            using type = std::tuple<Rs..., Os...>;
        };
        
        using IterationComponents = typename CombineComponents<RequiredTuple, OptionalTuple>::type;
        
        // Helper to apply tuple as template parameters
        template<typename Tuple>
        struct ApplyComponents;
        
        template<typename... Cs>
        struct ApplyComponents<std::tuple<Cs...>>
        {
            static constexpr size_t Count = sizeof...(Cs);
            using ComponentsTuple = std::tuple<Cs...>;
            
            template<template<typename...> class Template>
            using Apply = Template<Cs...>;
        };
        
        using ComponentsHelper = ApplyComponents<IterationComponents>;
        static constexpr size_t ComponentCount = ComponentsHelper::Count;
        
        // Forward declaration
        template<bool IsConst>
        class IteratorBase;
        
    public:
        using iterator = IteratorBase<false>;
        using const_iterator = IteratorBase<true>;
        using QueryBuilder = QueryBuilder<QueryArgs...>;

        explicit View(std::shared_ptr<ArchetypeStorage> storage) :
            m_storage(std::move(storage))
        {
            // Components are registered when first added to entities
        }
        
        /**
         * Execute function for each entity with the required components
         * Note: ForEach only passes required components to the callback, not optional ones
         */
        template<typename Func>
        void ForEach(Func&& func)
        {
            // Get archetypes matching our query
            auto archetypes = m_storage->GetAllArchetypes();
            
            for (Archetype* archetype : archetypes)
            {
                if (QueryBuilder::Matches(archetype->GetMask()))
                {
                    // Call ForEach with only required components
                    ForEachRequired(archetype, std::forward<Func>(func), RequiredTuple{});
                }
            }
        }
        
    private:
        template<typename Func, typename... Rs>
        void ForEachRequired(Archetype* archetype, Func&& func, std::tuple<Rs...>)
        {
            archetype->ForEach<Rs...>(std::forward<Func>(func));
        }
        
    public:
        
        /**
         * Count entities matching this view
         */
        ASTRA_NODISCARD size_t Count() const
        {
            size_t count = 0;
            auto archetypes = m_storage->GetAllArchetypes();
            
            for (Archetype* arch : archetypes)
            {
                if (QueryBuilder::Matches(arch->GetMask()))
                {
                    count += arch->GetEntityCount();
                }
            }
            return count;
        }
        
        /**
         * Check if any entity matches
         */
        ASTRA_NODISCARD bool Empty() const
        {
            auto archetypes = m_storage->GetAllArchetypes();
            
            for (Archetype* arch : archetypes)
            {
                if (QueryBuilder::Matches(arch->GetMask()) && arch->GetEntityCount() > 0)
                {
                    return false;
                }
            }
            return true;
        }
        
        /**
         * Get first matching entity (if any)
         * Returns a tuple with Entity and pointers to components
         * Optional components may be nullptr
         */
        auto First()
        {
            return FirstImpl(std::make_index_sequence<ComponentCount>{});
        }
        
    private:
        template<size_t... Is>
        auto FirstImpl(std::index_sequence<Is...>)
        {
            using ReturnType = std::optional<std::tuple<Entity, std::tuple_element_t<Is, IterationComponents>*...>>;
            
            auto archetypes = m_storage->GetAllArchetypes();
            for (Archetype* arch : archetypes)
            {
                if (QueryBuilder::Matches(arch->GetMask()) && arch->GetEntityCount() > 0)
                {
                    Entity entity = arch->GetEntities()[0];
                    return ReturnType{std::make_tuple(entity, GetComponentPtr<Is>(arch, 0)...)};
                }
            }
            return ReturnType{std::nullopt};
        }
        
        template<size_t I>
        auto GetComponentPtr(Archetype* arch, size_t idx)
        {
            using Component = std::tuple_element_t<I, IterationComponents>;
            
            // Check if this component is optional
            constexpr bool isOptional = IsOptionalComponent<I>();
            
            if (arch->HasComponent<Component>())
            {
                return arch->GetComponent<Component>(idx);
            }
            else if constexpr (isOptional)
            {
                return static_cast<Component*>(nullptr);
            }
            else
            {
                // Should never happen if QueryBuilder works correctly
                ASTRA_ASSERT(false, "Required component missing from archetype");
                return static_cast<Component*>(nullptr);
            }
        }
        
        template<size_t I>
        static constexpr bool IsOptionalComponent()
        {
            // Check if component at index I is in the optional tuple
            if constexpr (I < std::tuple_size_v<RequiredTuple>)
            {
                return false; // It's a required component
            }
            else
            {
                return true; // It's an optional component
            }
        }
        
    public:
        
        // Iterator support for range-based for loops
        ASTRA_NODISCARD iterator begin() { return iterator(m_storage); }
        ASTRA_NODISCARD iterator end() { return iterator{}; }
        ASTRA_NODISCARD const_iterator begin() const { return const_iterator(m_storage); }
        ASTRA_NODISCARD const_iterator end() const { return const_iterator{}; }
        ASTRA_NODISCARD const_iterator cbegin() const { return const_iterator(m_storage); }
        ASTRA_NODISCARD const_iterator cend() const { return const_iterator{}; }
        
    private:
        std::shared_ptr<ArchetypeStorage> m_storage;
        
        /**
         * Iterator implementation for range-based for loops with structured bindings
         * @tparam IsConst Whether this is a const iterator
         */
        template<bool IsConst>
        class IteratorBase
        {
        public:
            // Conditional types based on IsConst
            template<typename T>
            using ComponentPtr = std::conditional_t<IsConst, const T*, T*>;
            using StoragePtr = std::conditional_t<IsConst, 
                std::shared_ptr<const ArchetypeStorage>, 
                std::shared_ptr<ArchetypeStorage>>;
            using ArchetypePtr = std::conditional_t<IsConst, const Archetype*, Archetype*>;
            
            // Build return type based on iteration components
            template<typename Component, size_t Index>
            using ComponentPtrType = ComponentPtr<Component>;
            
            template<typename... Cs>
            struct BuildEntityTuple
            {
                template<size_t... Is>
                static auto Build(std::index_sequence<Is...>)
                {
                    return std::tuple<Entity, ComponentPtrType<Cs, Is>...>{};
                }
                
                using type = decltype(Build(std::index_sequence_for<Cs...>{}));
            };
            
            using EntityTuple = typename ComponentsHelper::template Apply<BuildEntityTuple>::type;
            
            // Iterator traits
            using iterator_category = std::forward_iterator_tag;
            using value_type = EntityTuple;
            using difference_type = std::ptrdiff_t;
            using pointer = value_type*;
            using reference = value_type;
            
            IteratorBase() = default;
            
            IteratorBase(StoragePtr storage)
                : m_storage(std::move(storage))
            {
                // Get all archetypes and filter those matching our query
                auto allArchetypes = m_storage->GetAllArchetypes();
                for (auto* arch : allArchetypes)
                {
                    if (QueryBuilder::Matches(arch->GetMask()))
                    {
                        m_archetypes.push_back(arch);
                    }
                }
                
                if (!m_archetypes.empty())
                {
                    m_currentArchetype = 0;
                    m_currentChunk = 0;
                    m_currentEntityInChunk = 0;
                    
                    // Cache first valid chunk
                    const auto& chunks = m_archetypes[0]->GetChunks();
                    if (!chunks.empty())
                    {
                        CacheChunk(m_archetypes[0], 0);
                    }
                    
                    AdvanceToValid();
                }
            }
            
            // Enable conversion from non-const to const iterator
            template<bool OtherIsConst>
            requires (IsConst && !OtherIsConst)
            IteratorBase(const IteratorBase<OtherIsConst>& other)
                : m_storage(other.m_storage)
                , m_archetypes(other.m_archetypes)
                , m_currentArchetype(other.m_currentArchetype)
                , m_currentChunk(other.m_currentChunk)
                , m_currentEntityInChunk(other.m_currentEntityInChunk)
                , m_chunkCache(other.m_chunkCache)
            {}
            
            // Dereference returns a tuple for structured bindings
            ASTRA_NODISCARD ASTRA_FORCEINLINE EntityTuple operator*() const
            {
                return BuildTuple(std::make_index_sequence<ComponentCount>{});
            }
            
        private:
            // Build tuple with proper handling of optional components
            template<std::size_t... Is>
            ASTRA_NODISCARD ASTRA_FORCEINLINE EntityTuple BuildTuple(std::index_sequence<Is...>) const
            {
                return {m_chunkCache.entities[m_currentEntityInChunk], 
                       GetComponentPtrFromCache<Is>()...};
            }
            
            template<size_t I>
            ASTRA_NODISCARD ASTRA_FORCEINLINE auto GetComponentPtrFromCache() const
            {
                using Component = std::tuple_element_t<I, IterationComponents>;
                
                // Check if this component is optional
                if constexpr (View::IsOptionalComponent<I>())
                {
                    // For optional components, check if it exists in cache
                    if (std::get<I>(m_chunkCache.hasComponent))
                    {
                        auto& span = std::get<I>(m_chunkCache.componentArrays);
                        return &span[m_currentEntityInChunk];
                    }
                    else
                    {
                        return static_cast<ComponentPtr<Component>>(nullptr);
                    }
                }
                else
                {
                    // Required component - always present
                    auto& span = std::get<I>(m_chunkCache.componentArrays);
                    return &span[m_currentEntityInChunk];
                }
            }
            
        public:
            
            IteratorBase& operator++()
            {
                ++m_currentEntityInChunk;
                AdvanceToValid();
                return *this;
            }
            
            IteratorBase operator++(int)
            {
                IteratorBase tmp = *this;
                ++(*this);
                return tmp;
            }
            
            ASTRA_NODISCARD bool operator==(const IteratorBase& other) const
            {
                if (IsEnd() && other.IsEnd()) return true;
                if (IsEnd() != other.IsEnd()) return false;
                return m_currentArchetype == other.m_currentArchetype && 
                       m_currentChunk == other.m_currentChunk &&
                       m_currentEntityInChunk == other.m_currentEntityInChunk;
            }
            
            ASTRA_NODISCARD bool operator!=(const IteratorBase& other) const
            {
                return !(*this == other);
            }
            
        private:
            void AdvanceToValid()
            {
                while (m_currentArchetype < m_archetypes.size())
                {
                    ArchetypePtr arch = m_archetypes[m_currentArchetype];
                    const auto& chunks = arch->GetChunks();
                    
                    // Check if we need to move to next chunk
                    while (m_currentChunk < chunks.size())
                    {
                        // Check if we're still within current chunk
                        if (m_currentEntityInChunk < m_chunkCache.count)
                        {
                            return; // Valid position
                        }
                        
                        // Move to next chunk
                        ++m_currentChunk;
                        m_currentEntityInChunk = 0;
                        
                        // Cache next chunk if available
                        if (m_currentChunk < chunks.size())
                        {
                            CacheChunk(arch, m_currentChunk);
                        }
                    }
                    
                    // Move to next archetype
                    ++m_currentArchetype;
                    m_currentChunk = 0;
                    m_currentEntityInChunk = 0;
                    
                    // Cache first chunk of new archetype if available
                    if (m_currentArchetype < m_archetypes.size())
                    {
                        auto newArch = m_archetypes[m_currentArchetype];
                        const auto& newChunks = newArch->GetChunks();
                        if (!newChunks.empty())
                        {
                            CacheChunk(newArch, 0);
                        }
                    }
                }
            }
            
            void CacheChunk(ArchetypePtr arch, size_t chunkIdx)
            {
                // Direct chunk access without creating iterator
                const auto& chunks = arch->GetChunks();
                const auto& chunk = chunks[chunkIdx];
                
                m_chunkCache.count = chunk->GetCount();
                if (m_chunkCache.count == 0) return;
                
                // Cache entity array pointer
                const auto& entities = chunk->GetEntities();
                m_chunkCache.entities = entities.data();
                
                // Cache component arrays and presence flags
                CacheComponentsImpl(arch, chunk, std::make_index_sequence<ComponentCount>{});
            }
            
            template<size_t... Is>
            void CacheComponentsImpl(ArchetypePtr arch, const auto& chunk, std::index_sequence<Is...>)
            {
                m_chunkCache.componentArrays = std::make_tuple(
                    GetComponentArrayForCache<Is>(arch, chunk)...
                );
                m_chunkCache.hasComponent = std::make_tuple(
                    arch->HasComponent<std::tuple_element_t<Is, IterationComponents>>()...
                );
            }
            
            template<size_t I>
            std::span<std::tuple_element_t<I, IterationComponents>> GetComponentArrayForCache(ArchetypePtr arch, const auto& chunk)
            {
                using Component = std::tuple_element_t<I, IterationComponents>;
                
                if (arch->HasComponent<Component>())
                {
                    Component* ptr = chunk->template GetComponentArray<Component>();
                    size_t count = chunk->GetCount();
                    return std::span<Component>(ptr, count);
                }
                else
                {
                    // Return empty span for components not in this archetype
                    return std::span<Component>{};
                }
            }
            
            ASTRA_NODISCARD bool IsEnd() const
            {
                return m_archetypes.empty() || m_currentArchetype >= m_archetypes.size();
            }
            
            // Friend the other instantiation for conversion constructor
            template<bool OtherIsConst>
            friend class IteratorBase;
            
            // Build tuple type for component arrays based on iteration components
            template<typename... Cs>
            struct BuildComponentArrays
            {
                using type = std::tuple<std::span<Cs>...>;
            };
            
            using ComponentArraysTuple = typename ComponentsHelper::template Apply<BuildComponentArrays>::type;
            
            // Build tuple type for has-component flags
            template<size_t N>
            struct BoolTupleHelper
            {
                template<size_t>
                using BoolType = bool;
                
                template<size_t... Is>
                static std::tuple<BoolType<Is>...> MakeTuple(std::index_sequence<Is...>);
                
                using type = decltype(MakeTuple(std::make_index_sequence<N>{}));
            };
            
            template<size_t N>
            using BoolTuple = typename BoolTupleHelper<N>::type;
            
            // Cache component arrays per chunk (like ForEach does)
            struct ChunkCache
            {
                const Entity* entities = nullptr;
                size_t count = 0;
                ComponentArraysTuple componentArrays;
                BoolTuple<ComponentCount> hasComponent;
            };
            
            
            StoragePtr m_storage;
            std::vector<ArchetypePtr> m_archetypes;
            size_t m_currentArchetype = std::numeric_limits<size_t>::max();
            size_t m_currentChunk = 0;
            size_t m_currentEntityInChunk = 0;
            ChunkCache m_chunkCache;
        };
    };
}