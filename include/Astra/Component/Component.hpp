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
    
    // Forward declarations for serialization
    class BinaryWriter;
    class BinaryReader;
    
    // Component descriptor - shared metadata for component types
    struct ComponentDescriptor
    {
        using ConstructFn = void(void*);
        using DestructFn = void(void*);
        using CopyConstructFn = void(void*, const void*);
        using MoveConstructFn = void(void*, void*);
        using MoveAssignFn = void(void*, void*);
        using CopyAssignFn = void(void*, const void*);
        
        // Serialization function types
        using SerializeFn = void(BinaryWriter&, void*);              // Non-const for unified Serialize method
        using DeserializeFn = void(BinaryReader&, void*);
        using SerializeVersionedFn = void(BinaryWriter&, void*);     // Non-const for unified Serialize method
        using DeserializeVersionedFn = bool(BinaryReader&, void*);  // Returns false on error

        ComponentID id;
        size_t size;
        size_t alignment;
        
        // Component identification
        uint64_t hash;           // XXHash64 of component type name
        const char* name;        // Component type name (for debugging)
        
        // Versioning info
        uint32_t version;        // Current component version
        uint32_t minVersion;     // Minimum supported version for migration
        
        // Type traits for optimization
        bool is_trivially_copyable;
        bool is_copy_constructible;
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
        
        // Serialization function pointers
        SerializeFn* serialize;                    // Basic serialization
        DeserializeFn* deserialize;                // Basic deserialization
        SerializeVersionedFn* serializeVersioned;  // Versioned serialization
        DeserializeVersionedFn* deserializeVersioned; // Versioned deserialization with migration
    };
}