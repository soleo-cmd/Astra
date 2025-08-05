#pragma once

#include <cstdint>
#include <limits>
#include <type_traits>

namespace Astra
{
    using ComponentID = std::uint16_t;

    inline constexpr ComponentID INVALID_COMPONENT = std::numeric_limits<ComponentID>::max();

    // Allow users to override the maximum number of components
    // Usage: #define ASTRA_MAX_COMPONENTS 128 before including Astra
    #ifndef ASTRA_MAX_COMPONENTS
        #define ASTRA_MAX_COMPONENTS 64u
    #endif

    constexpr std::size_t MAX_COMPONENTS = ASTRA_MAX_COMPONENTS;

    template<typename T>
    concept Component = std::is_move_constructible_v<T>      && 
                        std::is_move_assignable_v<T>         &&
                        std::is_nothrow_move_assignable_v<T> &&
                        std::is_destructible_v<T>            &&
                        std::is_nothrow_destructible_v<T>;
    
    // Component descriptor - shared metadata for component types
    struct ComponentDescriptor
    {
        using ConstructFn = void(void*);
        using DestructFn = void(void*);
        using CopyConstructFn = void(void*, const void*);
        using MoveConstructFn = void(void*, void*);
        using MoveAssignFn = void(void*, void*);
        using CopyAssignFn = void(void*, const void*);

        ComponentID id;
        size_t size;
        size_t alignment;
        
        // Type traits for optimization
        bool is_trivially_copyable;
        bool is_nothrow_move_constructible;
        bool is_nothrow_default_constructible;
        bool is_empty;
        
        // Function pointers for operations
        ConstructFn* defaultConstruct;
        DestructFn* destruct;
        CopyConstructFn* copyConstruct;
        MoveConstructFn* moveConstruct;
        MoveAssignFn* moveAssign;
        CopyAssignFn* copyAssign;
    };
}