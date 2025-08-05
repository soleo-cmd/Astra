#pragma once

#include <cstdint>
#include <cstring>
#include <utility>
#include <new>
#include <type_traits>
#include <algorithm>

#include "Base.hpp"
#include "../Container/SmallVector.hpp"
#include "../Entity/Entity.hpp"
#include "../Component/Component.hpp"

namespace Astra
{
    // Signal types as bit flags
    enum class Signal : uint32_t
    {
        None             = 0,
        EntityCreated    = 1 << 0,
        EntityDestroyed  = 1 << 1,
        ComponentAdded   = 1 << 2,
        ComponentRemoved = 1 << 3,
        ComponentUpdated = 1 << 4,
        ParentChanged    = 1 << 5,
        LinkAdded        = 1 << 6,
        LinkRemoved      = 1 << 7,
        // Reserve space for future signals
        All = ~0u
    };
    
    // Enable bitwise operations for Signal enum
    inline Signal operator|(Signal a, Signal b) noexcept
    {
        return static_cast<Signal>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    
    inline Signal operator&(Signal a, Signal b) noexcept
    {
        return static_cast<Signal>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }
    
    inline Signal operator^(Signal a, Signal b) noexcept
    {
        return static_cast<Signal>(static_cast<uint32_t>(a) ^ static_cast<uint32_t>(b));
    }
    
    inline Signal operator~(Signal a) noexcept
    {
        return static_cast<Signal>(~static_cast<uint32_t>(a));
    }
    
    inline Signal& operator|=(Signal& a, Signal b) noexcept
    {
        return a = a | b;
    }
    
    inline Signal& operator&=(Signal& a, Signal b) noexcept
    {
        return a = a & b;
    }
    
    inline Signal& operator^=(Signal& a, Signal b) noexcept
    {
        return a = a ^ b;
    }
    
    inline bool HasSignal(Signal flags, Signal signal) noexcept
    {
        return (flags & signal) != Signal::None;
    }
    
    // Forward declarations
    template<typename Signature>
    class Delegate;
    
    template<typename Signature>
    class MulticastDelegate;
    
    // Delegate implementation for function signatures
    template<typename R, typename... Args>
    class Delegate<R(Args...)>
    {
    public:
        using result_type = R;
        
        // Size of small buffer optimization (adjust based on typical use cases)
        static constexpr size_t SmallBufferSize = 32;
        
        Delegate() noexcept : m_invoker(nullptr) {}
        
        Delegate(std::nullptr_t) noexcept : m_invoker(nullptr) {}
        
        // Constructor for function pointers
        template<typename Func>
        Delegate(Func* func) noexcept : m_invoker(nullptr)
        {
            if (func)
            {
                using FuncType = Func*;
                static_assert(sizeof(FuncType) <= SmallBufferSize, "Function pointer too large");
                
                new (m_storage) FuncType(func);
                m_invoker = &InvokeFunctionPointer<Func>;
            }
        }
        
        // Constructor for functors and lambdas
        template<typename Func, typename = std::enable_if_t<!std::is_same_v<std::decay_t<Func>, Delegate>>>
        Delegate(Func&& func) : m_invoker(nullptr)
        {
            using DecayedFunc = std::decay_t<Func>;
            
            if constexpr (sizeof(DecayedFunc) <= SmallBufferSize && 
                          std::is_nothrow_move_constructible_v<DecayedFunc>)
            {
                // Small buffer optimization
                new (m_storage) DecayedFunc(std::forward<Func>(func));
                m_invoker = &InvokeSmallFunctor<DecayedFunc>;
                m_deleter = &DeleteSmallFunctor<DecayedFunc>;
            }
            else
            {
                // Heap allocation for large functors
                auto* heapFunc = new DecayedFunc(std::forward<Func>(func));
                *reinterpret_cast<void**>(m_storage) = heapFunc;
                m_invoker = &InvokeLargeFunctor<DecayedFunc>;
                m_deleter = &DeleteLargeFunctor<DecayedFunc>;
            }
        }
        
        // Member function binding
        template<typename T, typename MemberFunc>
        static Delegate FromMember(T* instance, MemberFunc T::*memberFunc)
        {
            Delegate d;
            
            struct MemberBinding
            {
                T* instance;
                MemberFunc T::*memberFunc;
            };
            
            static_assert(sizeof(MemberBinding) <= SmallBufferSize, "Member binding too large");
            
            new (d.m_storage) MemberBinding{instance, memberFunc};
            d.m_invoker = &InvokeMemberFunction<T, MemberFunc>;
            d.m_deleter = &DeleteSmallFunctor<MemberBinding>;
            
            return d;
        }
        
        Delegate(const Delegate& other) : m_invoker(other.m_invoker), m_deleter(other.m_deleter)
        {
            if (m_invoker)
            {
                if (m_deleter == nullptr)
                {
                    // Simple copy (function pointer)
                    std::memcpy(m_storage, other.m_storage, SmallBufferSize);
                }
                else if (IsSmallStorage(m_deleter))
                {
                    // Copy construct in small buffer
                    CopySmallStorage(other);
                }
                else
                {
                    // Copy heap allocation
                    CopyLargeStorage(other);
                }
            }
        }
        
        Delegate(Delegate&& other) noexcept : m_invoker(other.m_invoker), m_deleter(other.m_deleter)
        {
            if (m_invoker)
            {
                std::memcpy(m_storage, other.m_storage, SmallBufferSize);
                other.m_invoker = nullptr;
                other.m_deleter = nullptr;
            }
        }
        
        ~Delegate()
        {
            Reset();
        }
        
        Delegate& operator=(const Delegate& other)
        {
            if (this != &other)
            {
                Reset();
                new (this) Delegate(other);
            }
            return *this;
        }
        
        Delegate& operator=(Delegate&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                m_invoker = other.m_invoker;
                m_deleter = other.m_deleter;
                std::memcpy(m_storage, other.m_storage, SmallBufferSize);
                other.m_invoker = nullptr;
                other.m_deleter = nullptr;
            }
            return *this;
        }
        
        Delegate& operator=(std::nullptr_t) noexcept
        {
            Reset();
            return *this;
        }
        
        R operator()(Args... args) const
        {
            ASTRA_ASSERT(m_invoker != nullptr, "Calling empty delegate");
            return m_invoker(m_storage, std::forward<Args>(args)...);
        }
        
        explicit operator bool() const noexcept
        {
            return m_invoker != nullptr;
        }
        
        void Reset() noexcept
        {
            if (m_deleter)
            {
                m_deleter(m_storage);
            }
            m_invoker = nullptr;
            m_deleter = nullptr;
        }
        
        bool operator==(const Delegate& other) const noexcept
        {
            if (m_invoker != other.m_invoker)
                return false;
                
            if (m_invoker == nullptr)
                return true;
                
            // For simplicity, we only compare function pointers
            if (m_deleter == nullptr && other.m_deleter == nullptr)
            {
                return std::memcmp(m_storage, other.m_storage, sizeof(void*)) == 0;
            }
            
            return false;
        }
        
        bool operator!=(const Delegate& other) const noexcept
        {
            return !(*this == other);
        }
        
    private:
        using InvokerType = R(*)(const void*, Args...);
        using DeleterType = void(*)(void*);
        
        alignas(std::max_align_t) mutable char m_storage[SmallBufferSize];
        InvokerType m_invoker;
        DeleterType m_deleter = nullptr;
        
        // Invoker implementations
        template<typename Func>
        static R InvokeFunctionPointer(const void* storage, Args... args)
        {
            auto func = *reinterpret_cast<Func* const*>(storage);
            return func(std::forward<Args>(args)...);
        }
        
        template<typename Func>
        static R InvokeSmallFunctor(const void* storage, Args... args)
        {
            const auto& func = *reinterpret_cast<const Func*>(storage);
            return func(std::forward<Args>(args)...);
        }
        
        template<typename Func>
        static R InvokeLargeFunctor(const void* storage, Args... args)
        {
            const auto& func = **reinterpret_cast<Func* const*>(storage);
            return func(std::forward<Args>(args)...);
        }
        
        template<typename T, typename MemberFunc>
        static R InvokeMemberFunction(const void* storage, Args... args)
        {
            struct MemberBinding
            {
                T* instance;
                MemberFunc T::*memberFunc;
            };
            
            const auto& binding = *reinterpret_cast<const MemberBinding*>(storage);
            return (binding.instance->*binding.memberFunc)(std::forward<Args>(args)...);
        }
        
        // Deleter implementations
        template<typename Func>
        static void DeleteSmallFunctor(void* storage)
        {
            reinterpret_cast<Func*>(storage)->~Func();
        }
        
        template<typename Func>
        static void DeleteLargeFunctor(void* storage)
        {
            delete *reinterpret_cast<Func**>(storage);
        }
        
        // Helper functions
        static bool IsSmallStorage(DeleterType deleter) noexcept
        {
            // Check if the deleter is one of the small storage deleters
            return deleter != nullptr && 
                   (void*)deleter != (void*)&DeleteLargeFunctor<void>;
        }
        
        void CopySmallStorage(const Delegate& other)
        {
            // This is a simplified version - in production, you'd need proper copy construction
            std::memcpy(m_storage, other.m_storage, SmallBufferSize);
        }
        
        void CopyLargeStorage(const Delegate& other)
        {
            // This is a simplified version - in production, you'd need proper copy construction
            void* otherPtr = *reinterpret_cast<void* const*>(other.m_storage);
            *reinterpret_cast<void**>(m_storage) = otherPtr; // Simplified - should deep copy
        }
    };
    
    // MulticastDelegate - manages multiple delegates
    template<typename R, typename... Args>
    class MulticastDelegate<R(Args...)>
    {
    public:
        using DelegateType = Delegate<R(Args...)>;
        using result_type = R;
        
        MulticastDelegate() = default;
        
        void Add(const DelegateType& delegate)
        {
            if (delegate)
            {
                m_delegates.push_back(delegate);
            }
        }
        
        void Add(DelegateType&& delegate)
        {
            if (delegate)
            {
                m_delegates.push_back(std::move(delegate));
            }
        }
        
        template<typename Func>
        void Add(Func&& func)
        {
            m_delegates.emplace_back(std::forward<Func>(func));
        }
        
        template<typename T, typename MemberFunc>
        void AddMember(T* instance, MemberFunc T::*memberFunc)
        {
            m_delegates.push_back(DelegateType::FromMember(instance, memberFunc));
        }
        
        bool Remove(const DelegateType& delegate)
        {
            auto it = std::find(m_delegates.begin(), m_delegates.end(), delegate);
            if (it != m_delegates.end())
            {
                m_delegates.erase(it);
                return true;
            }
            return false;
        }
        
        void Clear()
        {
            m_delegates.clear();
        }
        
        size_t Size() const noexcept
        {
            return m_delegates.size();
        }
        
        bool IsEmpty() const noexcept
        {
            return m_delegates.empty();
        }
        
        // Invoke all delegates (for void return types)
        template<typename U = R>
        std::enable_if_t<std::is_void_v<U>> Invoke(Args... args) const
        {
            for (const auto& delegate : m_delegates)
            {
                delegate(std::forward<Args>(args)...);
            }
        }
        
        // Invoke all delegates and collect results (for non-void return types)
        template<typename U = R>
        std::enable_if_t<!std::is_void_v<U>, SmallVector<R, 4>> Invoke(Args... args) const
        {
            SmallVector<R, 4> results;
            results.reserve(m_delegates.size());
            
            for (const auto& delegate : m_delegates)
            {
                results.push_back(delegate(std::forward<Args>(args)...));
            }
            
            return results;
        }
        
        // Operator() for convenience
        template<typename U = R>
        std::enable_if_t<std::is_void_v<U>> operator()(Args... args) const
        {
            Invoke(std::forward<Args>(args)...);
        }
        
        template<typename U = R>
        std::enable_if_t<!std::is_void_v<U>, SmallVector<R, 4>> operator()(Args... args) const
        {
            return Invoke(std::forward<Args>(args)...);
        }
        
    private:
        SmallVector<DelegateType, 4> m_delegates;
    };
    
    // Signal event data structures
    struct EntityEvent
    {
        Entity entity;
    };
    
    template<typename T>
    struct ComponentEvent
    {
        Entity entity;
        T* component;
    };
    
    struct ComponentEventBase
    {
        Entity entity;
        ComponentID componentId;
        void* component;
    };
    
    struct RelationshipEvent
    {
        Entity entity;
        Entity other;
    };
    
    // Type aliases for common signal signatures
    using EntitySignalHandler = MulticastDelegate<void(const EntityEvent&)>;
    using ComponentSignalHandler = MulticastDelegate<void(const ComponentEventBase&)>;
    using RelationshipSignalHandler = MulticastDelegate<void(const RelationshipEvent&)>;
    
    // SignalManager - Manages all signals with enable/disable functionality
    class SignalManager
    {
    public:
        SignalManager() : m_enabledSignals(Signal::None) {}
        
        // Enable/disable signals
        void EnableSignals(Signal signals) noexcept
        {
            m_enabledSignals |= signals;
        }
        
        void DisableSignals(Signal signals) noexcept
        {
            m_enabledSignals &= ~signals;
        }
        
        void SetEnabledSignals(Signal signals) noexcept
        {
            m_enabledSignals = signals;
        }
        
        ASTRA_NODISCARD Signal GetEnabledSignals() const noexcept
        {
            return m_enabledSignals;
        }
        
        ASTRA_NODISCARD bool IsSignalEnabled(Signal signal) const noexcept
        {
            return HasSignal(m_enabledSignals, signal);
        }
        
        // Entity signals
        EntitySignalHandler& OnEntityCreated() { return m_onEntityCreated; }
        const EntitySignalHandler& OnEntityCreated() const { return m_onEntityCreated; }
        
        EntitySignalHandler& OnEntityDestroyed() { return m_onEntityDestroyed; }
        const EntitySignalHandler& OnEntityDestroyed() const { return m_onEntityDestroyed; }
        
        // Component signals
        ComponentSignalHandler& OnComponentAdded() { return m_onComponentAdded; }
        const ComponentSignalHandler& OnComponentAdded() const { return m_onComponentAdded; }
        
        ComponentSignalHandler& OnComponentRemoved() { return m_onComponentRemoved; }
        const ComponentSignalHandler& OnComponentRemoved() const { return m_onComponentRemoved; }
        
        ComponentSignalHandler& OnComponentUpdated() { return m_onComponentUpdated; }
        const ComponentSignalHandler& OnComponentUpdated() const { return m_onComponentUpdated; }
        
        // Relationship signals
        RelationshipSignalHandler& OnParentChanged() { return m_onParentChanged; }
        const RelationshipSignalHandler& OnParentChanged() const { return m_onParentChanged; }
        
        RelationshipSignalHandler& OnLinkAdded() { return m_onLinkAdded; }
        const RelationshipSignalHandler& OnLinkAdded() const { return m_onLinkAdded; }
        
        RelationshipSignalHandler& OnLinkRemoved() { return m_onLinkRemoved; }
        const RelationshipSignalHandler& OnLinkRemoved() const { return m_onLinkRemoved; }
        
        // Emit signals (only if enabled)
        void EmitEntityCreated(const EntityEvent& event)
        {
            if (IsSignalEnabled(Signal::EntityCreated))
            {
                m_onEntityCreated(event);
            }
        }
        
        void EmitEntityDestroyed(const EntityEvent& event)
        {
            if (IsSignalEnabled(Signal::EntityDestroyed))
            {
                m_onEntityDestroyed(event);
            }
        }
        
        void EmitComponentAdded(const ComponentEventBase& event)
        {
            if (IsSignalEnabled(Signal::ComponentAdded))
            {
                m_onComponentAdded(event);
            }
        }
        
        void EmitComponentRemoved(const ComponentEventBase& event)
        {
            if (IsSignalEnabled(Signal::ComponentRemoved))
            {
                m_onComponentRemoved(event);
            }
        }
        
        void EmitComponentUpdated(const ComponentEventBase& event)
        {
            if (IsSignalEnabled(Signal::ComponentUpdated))
            {
                m_onComponentUpdated(event);
            }
        }
        
        void EmitParentChanged(const RelationshipEvent& event)
        {
            if (IsSignalEnabled(Signal::ParentChanged))
            {
                m_onParentChanged(event);
            }
        }
        
        void EmitLinkAdded(const RelationshipEvent& event)
        {
            if (IsSignalEnabled(Signal::LinkAdded))
            {
                m_onLinkAdded(event);
            }
        }
        
        void EmitLinkRemoved(const RelationshipEvent& event)
        {
            if (IsSignalEnabled(Signal::LinkRemoved))
            {
                m_onLinkRemoved(event);
            }
        }
        
        // Clear all handlers
        void ClearAllHandlers()
        {
            m_onEntityCreated.Clear();
            m_onEntityDestroyed.Clear();
            m_onComponentAdded.Clear();
            m_onComponentRemoved.Clear();
            m_onComponentUpdated.Clear();
            m_onParentChanged.Clear();
            m_onLinkAdded.Clear();
            m_onLinkRemoved.Clear();
        }
        
    private:
        Signal m_enabledSignals;
        
        // Entity signals
        EntitySignalHandler m_onEntityCreated;
        EntitySignalHandler m_onEntityDestroyed;
        
        // Component signals
        ComponentSignalHandler m_onComponentAdded;
        ComponentSignalHandler m_onComponentRemoved;
        ComponentSignalHandler m_onComponentUpdated;
        
        // Relationship signals
        RelationshipSignalHandler m_onParentChanged;
        RelationshipSignalHandler m_onLinkAdded;
        RelationshipSignalHandler m_onLinkRemoved;
    };
}