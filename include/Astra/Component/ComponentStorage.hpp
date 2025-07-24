#pragma once

#include <array>
#include <bitset>
#include <memory>
#include <type_traits>

#include "../Core/Config.hpp"
#include "../Core/TypeID.hpp"
#include "Component.hpp"
#include "ComponentPool.hpp"

namespace Astra
{
    /**
    * Storage container for all component pools in the ECS.
    * 
    * Features:
    * - Fixed-size array storage (no dynamic allocations after construction)
    * - Bitset tracking of registered components
    * - Type-safe access through templates
    * - Runtime access through IComponentPool interface
    * 
    * Thread Safety: This container is NOT thread-safe. All access must be
    * externally synchronized in multi-threaded environments.
    */
    template<typename BaseType = IComponentPool, std::size_t MaxCount = MAX_COMPONENTS>
    class ComponentStorage
    {
    public:
        /**
        * Get or create a component pool for type T.
        * @return Non-null pointer to the component pool
        */
        template<typename T, typename... Args>
        T* GetOrCreate(Args&&... args)
        {
            ValidateType<T>();

            const ComponentID id = TypeID<typename T::ComponentType>::Value();
            ASTRA_ASSERT(id < MaxCount, "ComponentID exceeds storage capacity");

            if (!m_registered[id])
            {
                m_storage[id] = std::make_unique<T>(std::forward<Args>(args)...);
                m_registered.set(id);
                ++m_count;
            }

            return static_cast<T*>(m_storage[id].get());
        }

        /**
        * Get existing component pool for type T.
        * @return Pointer to pool or nullptr if not registered
        */
        template<typename T>
        [[nodiscard]] T* Get() const noexcept
        {
            ValidateType<T>();

            const ComponentID id = TypeID<typename T::ComponentType>::Value();
            if (id >= MaxCount || !m_registered[id])
            {
                return nullptr;
            }

            return static_cast<T*>(m_storage[id].get());
        }

        /**
        * Check if a component type is registered.
        */
        template<typename T>
        [[nodiscard]] bool Has() const noexcept
        {
            const ComponentID id = TypeID<T>::Value();
            return id < MaxCount && m_registered[id];
        }

        /**
        * Remove a component pool.
        */
        template<typename T>
        bool Remove() noexcept
        {
            const ComponentID id = TypeID<T>::Value();
            if (id >= MaxCount || !m_registered[id])
            {
                return false;
            }

            m_storage[id].reset();
            m_registered.reset(id);
            --m_count;
            return true;
        }

        // Runtime access by ID
        [[nodiscard]] BaseType* GetByID(ComponentID id) const noexcept
        {
            return (id < MaxCount && m_registered[id]) 
                ? m_storage[id].get() 
                : nullptr;
        }

        [[nodiscard]] bool IsRegistered(ComponentID id) const noexcept
        {
            return id < MaxCount && m_registered[id];
        }

        [[nodiscard]] std::size_t Count() const noexcept { return m_count; }
        [[nodiscard]] bool Empty() const noexcept { return m_count == 0; }
        [[nodiscard]] const std::bitset<MaxCount>& RegisteredMask() const noexcept 
        { 
            return m_registered; 
        }

        void Clear() noexcept
        {
            m_storage.fill(nullptr);
            m_registered.reset();
            m_count = 0;
        }

        /**
        * Iterate over all registered component pools.
        */
        template<typename Func>
        void ForEach(Func&& func)
        {
            for (std::size_t id = 0; id < MaxCount; ++id)
            {
                if (m_registered[id])
                {
                    func(static_cast<ComponentID>(id), m_storage[id].get());
                }
            }
        }

        /**
        * Iterate over all registered component pools (const version).
        */
        template<typename Func>
        void ForEach(Func&& func) const
        {
            for (std::size_t id = 0; id < MaxCount; ++id)
            {
                if (m_registered[id])
                {
                    func(static_cast<ComponentID>(id), m_storage[id].get());
                }
            }
        }

        // Iterator support for range-based for loops
        class iterator
        {
        private:
            const ComponentStorage* m_storage;
            std::size_t m_index;

            void AdvanceToNext() noexcept
            {
                while (m_index < MaxCount && !m_storage->m_registered[m_index])
                {
                    ++m_index;
                }
            }

        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = std::pair<ComponentID, BaseType*>;
            using difference_type = std::ptrdiff_t;
            using pointer = value_type*;
            using reference = value_type;

            iterator(const ComponentStorage* storage, std::size_t index)
                : m_storage(storage), m_index(index)
            {
                AdvanceToNext();
            }

            reference operator*() const noexcept
            {
                return {static_cast<ComponentID>(m_index), m_storage->m_storage[m_index].get()};
            }

            iterator& operator++() noexcept
            {
                ++m_index;
                AdvanceToNext();
                return *this;
            }

            iterator operator++(int) noexcept
            {
                iterator tmp = *this;
                ++(*this);
                return tmp;
            }

            bool operator==(const iterator& other) const noexcept = default;
        };

        [[nodiscard]] iterator begin() const noexcept { return iterator(this, 0); }
        [[nodiscard]] iterator end() const noexcept { return iterator(this, MaxCount); }
        
    private:
        template<typename T>
        static void ValidateType()
        {
            static_assert(std::is_base_of_v<BaseType, T>, "Type must derive from BaseType");
        }
        
        // Member variables (declared last in private section)
        std::array<std::unique_ptr<BaseType>, MaxCount> m_storage{};
        std::bitset<MaxCount> m_registered{};
        std::size_t m_count = 0;
    };
}