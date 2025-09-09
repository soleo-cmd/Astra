#pragma once

#include "../Entity/Entity.hpp"
#include "../Component/Component.hpp"
#include "../Core/Base.hpp"
#include <vector>
#include <cstdint>

namespace Astra::Commands
{
    // ============= Entity Commands =============
    
    /**
     * Command to create a single entity
     */
    struct CreateEntity
    {
        Entity tempEntity;  // Temporary/placeholder entity ID
    };
    
    /**
     * Command to destroy a single entity
     */
    struct DestroyEntity
    {
        Entity entity;
    };
    
    /**
     * Command to create multiple entities in batch
     */
    struct CreateEntities
    {
        size_t count;
        Entity* outEntities;  // Pointer to output buffer
    };
    
    /**
     * Command to destroy multiple entities in batch
     */
    struct DestroyEntities
    {
        std::vector<Entity> entities;  // Owned copy of entities to destroy
    };
    
    // ============= Component Commands =============
    
    /**
     * Command to add a component to an entity
     * @tparam T The component type
     */
    template<typename T>
    struct AddComponent
    {
        Entity entity;
        T component;
        
        AddComponent(Entity e, T&& comp)
            : entity(e), component(std::forward<T>(comp)) {}
            
        AddComponent(Entity e, const T& comp)
            : entity(e), component(comp) {}
    };
    
    /**
     * Command to remove a component from an entity
     * @tparam T The component type
     */
    template<typename T>
    struct RemoveComponent
    {
        Entity entity;
        
        explicit RemoveComponent(Entity e)
            : entity(e) {}
    };
    
    /**
     * Command to set/update a component value
     * @tparam T The component type
     */
    template<typename T>
    struct SetComponent
    {
        Entity entity;
        T component;
        
        SetComponent(Entity e, T&& comp)
            : entity(e), component(std::forward<T>(comp)) {}
            
        SetComponent(Entity e, const T& comp)
            : entity(e), component(comp) {}
    };
    
    /**
     * Batch add components to multiple entities
     * @tparam T The component type
     */
    template<typename T>
    struct AddComponents
    {
        std::vector<Entity> entities;
        T component;  // Same component value for all entities
        
        AddComponents(std::span<Entity> ents, const T& comp)
            : entities(ents.begin(), ents.end()), component(comp) {}
    };
    
    /**
     * Batch remove components from multiple entities
     * @tparam T The component type
     */
    template<typename T>
    struct RemoveComponents
    {
        std::vector<Entity> entities;
        
        explicit RemoveComponents(std::span<Entity> ents)
            : entities(ents.begin(), ents.end()) {}
    };
    
    // ============= Relationship Commands =============
    
    /**
     * Command to set a parent-child relationship
     */
    struct SetParent
    {
        Entity child;
        Entity parent;
    };
    
    /**
     * Command to remove parent from a child
     */
    struct RemoveParent
    {
        Entity child;
    };
    
    /**
     * Command to add a bidirectional link between entities
     */
    struct AddLink
    {
        Entity a;
        Entity b;
    };
    
    /**
     * Command to remove a bidirectional link between entities
     */
    struct RemoveLink
    {
        Entity a;
        Entity b;
    };
    
    // ============= Command Traits =============
    
    /**
     * Trait to determine if a type is a command
     */
    template<typename T>
    struct IsCommand : std::false_type {};
    
    // Specialize for each command type
    template<> struct IsCommand<CreateEntity> : std::true_type {};
    template<> struct IsCommand<DestroyEntity> : std::true_type {};
    template<> struct IsCommand<CreateEntities> : std::true_type {};
    template<> struct IsCommand<DestroyEntities> : std::true_type {};
    template<typename T> struct IsCommand<AddComponent<T>> : std::true_type {};
    template<typename T> struct IsCommand<RemoveComponent<T>> : std::true_type {};
    template<typename T> struct IsCommand<SetComponent<T>> : std::true_type {};
    template<typename T> struct IsCommand<AddComponents<T>> : std::true_type {};
    template<typename T> struct IsCommand<RemoveComponents<T>> : std::true_type {};
    template<> struct IsCommand<SetParent> : std::true_type {};
    template<> struct IsCommand<RemoveParent> : std::true_type {};
    template<> struct IsCommand<AddLink> : std::true_type {};
    template<> struct IsCommand<RemoveLink> : std::true_type {};
    
    template<typename T>
    inline constexpr bool IsCommand_v = IsCommand<T>::value;
    
} // namespace Astra::Commands