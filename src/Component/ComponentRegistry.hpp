#pragma once

#include <memory>
#include <type_traits>

#include "../Container/FlatMap.hpp"
#include "../Core/TypeID.hpp"
#include "Component.hpp"

namespace Astra
{
    /**
     * ComponentRegistry manages component metadata and provides
     * runtime component operations like construction and destruction.
     */
    class ComponentRegistry
    {
    private:
        // Template functions for component operations
        template<typename T>
        static void DefaultConstruct(void* ptr) {
            new (ptr) T{};
        }
        
        template<typename T>
        static void Destruct(void* ptr) {
            static_cast<T*>(ptr)->~T();
        }
        
        template<typename T>
        static void CopyConstruct(void* dst, const void* src) {
            new (dst) T(*static_cast<const T*>(src));
        }
        
        template<typename T>
        static void MoveConstruct(void* dst, void* src) {
            new (dst) T(std::move(*static_cast<T*>(src)));
        }
        
        template<typename T>
        static void MoveAssign(void* dst, void* src) {
            *static_cast<T*>(dst) = std::move(*static_cast<T*>(src));
        }
        
        template<typename T>
        static void CopyAssign(void* dst, const void* src) {
            *static_cast<T*>(dst) = *static_cast<const T*>(src);
        }
        
    public:
        
        /**
         * Register a component type
         */
        template<Component T>
        void RegisterComponent()
        {
            ComponentID id = TypeID<T>::Value();
            
            // Check if already registered
            if (m_components.Contains(id))
                return;
                
            ComponentDescriptor desc;
            desc.id = id;
            desc.size = sizeof(T);
            desc.alignment = alignof(T);
            
            // Type traits
            desc.is_trivially_copyable = std::is_trivially_copyable_v<T>;
            desc.is_nothrow_move_constructible = std::is_nothrow_move_constructible_v<T>;
            desc.is_nothrow_default_constructible = std::is_nothrow_default_constructible_v<T>;
            desc.is_empty = std::is_empty_v<T>;
            
            // Function pointers to template instantiations
            desc.defaultConstruct = &DefaultConstruct<T>;
            desc.destruct = &Destruct<T>;
            desc.copyConstruct = &CopyConstruct<T>;
            desc.moveConstruct = &MoveConstruct<T>;
            desc.moveAssign = &MoveAssign<T>;
            desc.copyAssign = &CopyAssign<T>;
            
            m_components[id] = std::move(desc);
        }

        template<Component... Components>
        void RegisterComponents()
        {
            constexpr size_t count = sizeof...(Components);
            if (count == 0) return;

            // Reserve space in map to avoid rehashing
            m_components.Reserve(m_components.Size() + count);

            // Register each component
            (RegisterComponent<Components>(), ...);
        }
        
        /**
         * Get component descriptor
         */
        ASTRA_NODISCARD const ComponentDescriptor* GetComponent(ComponentID id) const
        {
            auto it = m_components.Find(id);
            return it != m_components.end() ? &it->second : nullptr;
        }
        
        
    public:
        ComponentRegistry() = default;
        
    private:
        FlatMap<ComponentID, ComponentDescriptor> m_components;
    };
}