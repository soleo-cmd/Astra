#pragma once

#include <memory>
#include <string_view>
#include <type_traits>

#include "../Container/FlatMap.hpp"
#include "../Core/Result.hpp"
#include "../Core/TypeID.hpp"
#include "../Serialization/BinaryArchive.hpp"
#include "../Serialization/BinaryReader.hpp"
#include "../Serialization/BinaryWriter.hpp"
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
        
        // Serialization template functions
        template<typename T>
        static void Serialize(BinaryWriter& writer, void* ptr) {
            T* component = static_cast<T*>(ptr);
            writer(*component);
        }
        
        template<typename T>
        static void Deserialize(BinaryReader& reader, void* ptr) {
            T* component = static_cast<T*>(ptr);
            reader(*component);
        }
        
        template<typename T>
        static void SerializeVersioned(BinaryWriter& writer, void* ptr) {
            T* component = static_cast<T*>(ptr);
            writer.WriteVersionedComponent(*component);
        }
        
        template<typename T>
        static bool DeserializeVersioned(BinaryReader& reader, void* ptr) {
            T* component = static_cast<T*>(ptr);
            auto result = reader.ReadVersionedComponent(*component);
            return result.IsOk();
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
            
            // Component identification
            desc.hash = TypeID<T>::Hash();
            
            // Debug check for hash collisions
            #ifdef ASTRA_BUILD_DEBUG
            if (auto it = m_hashToId.Find(desc.hash); it != m_hashToId.end())
            {
                auto existing = GetComponent(it->second);
                if (existing)
                {
                    // We're comparing type names to detect collision
                    // If names are different but hash is same = collision!
                    auto currentName = TypeID<T>::Name();
                    std::string_view existingName = existing->name ? existing->name : "";
                    ASTRA_ASSERT(currentName == existingName,
                        "Hash collision detected! Components have same hash but different types");
                }
            }
            #endif
            
            // Store the type name - TypeID<T>::Name() returns a string_view
            // We need to store it in a persistent string
            auto nameView = TypeID<T>::Name();
            m_componentNames.emplace_back(nameView);
            desc.name = m_componentNames.back().c_str();
            
            // Versioning info from SerializationTraits
            desc.version = SerializationTraits<T>::Version;
            desc.minVersion = SerializationTraits<T>::MinVersion;
            
            // Type traits
            desc.is_trivially_copyable = std::is_trivially_copyable_v<T>;
            desc.is_copy_constructible = std::is_copy_constructible_v<T>;
            desc.is_nothrow_move_constructible = std::is_nothrow_move_constructible_v<T>;
            desc.is_nothrow_default_constructible = std::is_nothrow_default_constructible_v<T>;
            desc.is_empty = std::is_empty_v<T>;
            
            // Function pointers to template instantiations
            desc.defaultConstruct = &DefaultConstruct<T>;
            desc.destruct = &Destruct<T>;
            desc.moveConstruct = &MoveConstruct<T>;
            desc.moveAssign = &MoveAssign<T>;
            
            // Only set copy operations if type is copy constructible
            if constexpr (std::is_copy_constructible_v<T>)
            {
                desc.copyConstruct = &CopyConstruct<T>;
                desc.copyAssign = &CopyAssign<T>;
            }
            else
            {
                desc.copyConstruct = nullptr;
                desc.copyAssign = nullptr;
            }
            
            // Serialization function pointers
            desc.serialize = &Serialize<T>;
            desc.deserialize = &Deserialize<T>;
            desc.serializeVersioned = &SerializeVersioned<T>;
            desc.deserializeVersioned = &DeserializeVersioned<T>;
            
            // Add to both maps
            m_components[id] = desc;
            m_hashToId[desc.hash] = id;
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
         * Get component descriptor by ID
         */
        ASTRA_NODISCARD const ComponentDescriptor* GetComponent(ComponentID id) const
        {
            auto it = m_components.Find(id);
            return it != m_components.end() ? &it->second : nullptr;
        }
        
        /**
         * Get component descriptor by hash
         */
        ASTRA_NODISCARD const ComponentDescriptor* GetComponentByHash(uint64_t hash) const
        {
            auto hashIt = m_hashToId.Find(hash);
            if (hashIt == m_hashToId.end())
                return nullptr;
                
            return GetComponent(hashIt->second);
        }
        
        /**
         * Get component ID from hash
         */
        ASTRA_NODISCARD Result<ComponentID, std::string_view> GetComponentIdFromHash(uint64_t hash) const
        {
            auto it = m_hashToId.Find(hash);
            if (it == m_hashToId.end())
            {
                return Result<ComponentID, std::string_view>::Err("Unknown component hash");
            }
            return Result<ComponentID, std::string_view>::Ok(it->second);
        }
        
        /**
         * Get all registered components
         */
        ASTRA_NODISCARD const FlatMap<ComponentID, ComponentDescriptor>& GetAllComponents() const
        {
            return m_components;
        }
        
        /**
         * Get number of registered components
         */
        ASTRA_NODISCARD size_t GetComponentCount() const
        {
            return m_components.Size();
        }
        
        /**
         * Get all component descriptors for serialization
         */
        void GetAllDescriptors(std::vector<ComponentDescriptor>& descriptors) const
        {
            descriptors.clear();
            if (m_components.Empty())
            {
                return;
            }
            descriptors.reserve(m_components.Size());
            for (const auto& [id, desc] : m_components)
            {
                descriptors.push_back(desc);
            }
        }
        
    public:
        ComponentRegistry() = default;
        
    private:
        FlatMap<ComponentID, ComponentDescriptor> m_components;
        FlatMap<uint64_t, ComponentID> m_hashToId;  // Hash → ID mapping for deserialization
        std::vector<std::string> m_componentNames;  // Storage for component name strings
    };
}