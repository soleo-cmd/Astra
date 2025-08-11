#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <tuple>
#include <type_traits>
#include <utility>

#include "../Component/Component.hpp"
#include "../Container/SmallVector.hpp"
#include "../Entity/Entity.hpp"
#include "Base.hpp"

namespace Astra
{
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
    
    template<typename Signature>
    class Delegate;
    
    template<typename Signature>
    class MulticastDelegate;
    
    template<typename R, typename... Args>
    class Delegate<R(Args...)>
    {
    public:
        using ResultType = R;
        
        static constexpr size_t SmallBufferSize = 32;
        
        Delegate() noexcept : m_invoker(nullptr) {}
        
        Delegate(std::nullptr_t) noexcept : m_invoker(nullptr) {}
        
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
        
        template<typename Func, typename = std::enable_if_t<!std::is_same_v<std::decay_t<Func>, Delegate>>>
        Delegate(Func&& func) : m_invoker(nullptr)
        {
            using DecayedFunc = std::decay_t<Func>;
            
            if constexpr (sizeof(DecayedFunc) <= SmallBufferSize && std::is_nothrow_move_constructible_v<DecayedFunc>)
            {
                new (m_storage) DecayedFunc(std::forward<Func>(func));
                m_invoker = &InvokeSmallFunctor<DecayedFunc>;
                m_deleter = &DeleteSmallFunctor<DecayedFunc>;
            }
            else
            {
                auto* heapFunc = new DecayedFunc(std::forward<Func>(func));
                *reinterpret_cast<void**>(m_storage) = heapFunc;
                m_invoker = &InvokeLargeFunctor<DecayedFunc>;
                m_deleter = &DeleteLargeFunctor<DecayedFunc>;
            }
        }
        
        template<typename T, typename MemberFunc>
        static Delegate FromMember(T* instance, MemberFunc T::*memberFunc)
        {
            Delegate delegate;
            
            struct MemberBinding
            {
                T* instance;
                MemberFunc T::*memberFunc;
            };
            
            static_assert(sizeof(MemberBinding) <= SmallBufferSize, "Member binding too large");
            
            new (delegate.m_storage) MemberBinding{instance, memberFunc};
            delegate.m_invoker = &InvokeMemberFunction<T, MemberFunc>;
            delegate.m_deleter = &DeleteSmallFunctor<MemberBinding>;
            
            return delegate;
        }
        
        Delegate(const Delegate& other) : m_invoker(other.m_invoker), m_deleter(other.m_deleter)
        {
            if (m_invoker)
            {
                if (m_deleter == nullptr)
                {
                    std::memcpy(m_storage, other.m_storage, SmallBufferSize);
                }
                else if (IsSmallStorage(m_deleter))
                {
                    CopySmallStorage(other);
                }
                else
                {
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
        
        alignas(std::max_align_t) mutable std::byte m_storage[SmallBufferSize];
        InvokerType m_invoker;
        DeleterType m_deleter = nullptr;
        
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
        
        static bool IsSmallStorage(DeleterType deleter) noexcept
        {
            return deleter != nullptr && (void*)deleter != (void*)&DeleteLargeFunctor<void>;
        }
        
        void CopySmallStorage(const Delegate& other)
        {
            // For small functors, simple memcpy is safe as they're trivially copyable
            std::memcpy(m_storage, other.m_storage, SmallBufferSize);
        }
        
        void CopyLargeStorage(const Delegate& other)
        {
            // WARNING: This performs a shallow copy of the heap pointer.
            // Large functors (>32 bytes) are not safely copyable.
            // If you need to copy delegates with large functors, consider:
            // 1. Reducing functor size to fit in small buffer
            // 2. Using std::shared_ptr to wrap the functor
            // 3. Using std::function which handles this correctly
            // TODO: Consider making this a compile-time error or using reference counting
            void* otherPtr = *reinterpret_cast<void* const*>(other.m_storage);
            *reinterpret_cast<void**>(m_storage) = otherPtr;
        }
    };
    
    template<typename R, typename... Args>
    class MulticastDelegate<R(Args...)>
    {
    public:
        using DelegateType = Delegate<R(Args...)>;
        using result_type = R;
        using HandlerID = std::size_t;
        
        MulticastDelegate() = default;
        
        struct Handler
        {
            HandlerID id;
            DelegateType delegate;
        };
        
        HandlerID Register(const DelegateType& delegate)
        {
            if (delegate)
            {
                HandlerID id = m_nextID++;
                m_handlers.push_back({id, delegate});
                return id;
            }
            return 0;
        }
        
        HandlerID Register(DelegateType&& delegate)
        {
            if (delegate)
            {
                HandlerID id = m_nextID++;
                m_handlers.push_back({id, std::move(delegate)});
                return id;
            }
            return 0;
        }
        
        template<typename Func>
        HandlerID Register(Func&& func)
        {
            HandlerID id = m_nextID++;
            m_handlers.push_back({id, DelegateType(std::forward<Func>(func))});
            return id;
        }
        
        template<typename T, typename MemberFunc>
        HandlerID RegisterMember(T* instance, MemberFunc T::*memberFunc)
        {
            HandlerID id = m_nextID++;
            m_handlers.push_back({id, DelegateType::FromMember(instance, memberFunc)});
            return id;
        }
        
        bool Unregister(HandlerID id)
        {
            auto it = std::find_if(m_handlers.begin(), m_handlers.end(),
                                  [id](const Handler& h) { return h.id == id; });
            if (it != m_handlers.end())
            {
                m_handlers.erase(it);
                return true;
            }
            return false;
        }
        
        void Clear()
        {
            m_handlers.clear();
        }
        
        size_t Size() const noexcept
        {
            return m_handlers.size();
        }
        
        bool IsEmpty() const noexcept
        {
            return m_handlers.empty();
        }
        
        template<typename U = R>
        std::enable_if_t<std::is_void_v<U>> Invoke(Args... args) const
        {
            for (const auto& handler : m_handlers)
            {
                handler.delegate(std::forward<Args>(args)...);
            }
        }
        
        template<typename U = R>
        std::enable_if_t<!std::is_void_v<U>, SmallVector<R, 4>> Invoke(Args... args) const
        {
            SmallVector<R, 4> results;
            results.reserve(m_handlers.size());
            
            for (const auto& handler : m_handlers)
            {
                results.push_back(handler.delegate(std::forward<Args>(args)...));
            }
            
            return results;
        }
        
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
        std::vector<Handler> m_handlers;
        HandlerID m_nextID = 1;
    };
    
    template<typename T>
    concept Event = requires
    {
        { T::flag } -> std::convertible_to<Signal>;
    } && std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>;
    
    namespace Events
    {
        struct EntityCreated
        {
            static constexpr Signal flag = Signal::EntityCreated;
            Entity entity;
        };
        
        struct EntityDestroyed
        {
            static constexpr Signal flag = Signal::EntityDestroyed;
            Entity entity;
        };
        
        struct ComponentAdded
        {
            static constexpr Signal flag = Signal::ComponentAdded;
            Entity entity;
            ComponentID componentId;
            void* component;
        };
        
        struct ComponentRemoved
        {
            static constexpr Signal flag = Signal::ComponentRemoved;
            Entity entity;
            ComponentID componentId;
            void* component;
        };
        
        struct ComponentUpdated
        {
            static constexpr Signal flag = Signal::ComponentUpdated;
            Entity entity;
            ComponentID componentId;
            void* component;
        };
        
        struct ParentChanged
        {
            static constexpr Signal flag = Signal::ParentChanged;
            Entity child;
            Entity parent;
        };
        
        struct LinkAdded
        {
            static constexpr Signal flag = Signal::LinkAdded;
            Entity first;
            Entity second;
        };
        
        struct LinkRemoved
        {
            static constexpr Signal flag = Signal::LinkRemoved;
            Entity first;
            Entity second;
        };
    }
    
    namespace Detail
    {
        template<typename Event, typename... Events>
        struct EventIndexImpl;
        
        template<typename Event>
        struct EventIndexImpl<Event>
        {
            static constexpr size_t value = size_t(-1);
        };
        
        template<typename Event, typename... Rest>
        struct EventIndexImpl<Event, Event, Rest...>
        {
            static constexpr size_t value = 0;
        };
        
        template<typename Event, typename First, typename... Rest>
        struct EventIndexImpl<Event, First, Rest...>
        {
            static constexpr size_t value = 1 + EventIndexImpl<Event, Rest...>::value;
        };
        
        template<typename Event, typename... Events>
        inline constexpr size_t EventIndex = EventIndexImpl<Event, Events...>::value;
    }
    
    class SignalManager
    {
    private:
        using AllEventTypes = std::tuple<
            Events::EntityCreated,
            Events::EntityDestroyed,
            Events::ComponentAdded,
            Events::ComponentRemoved,
            Events::ComponentUpdated,
            Events::ParentChanged,
            Events::LinkAdded,
            Events::LinkRemoved
        >;
        
        // Handler tuple
        std::tuple<
            MulticastDelegate<void(const Events::EntityCreated&)>,
            MulticastDelegate<void(const Events::EntityDestroyed&)>,
            MulticastDelegate<void(const Events::ComponentAdded&)>,
            MulticastDelegate<void(const Events::ComponentRemoved&)>,
            MulticastDelegate<void(const Events::ComponentUpdated&)>,
            MulticastDelegate<void(const Events::ParentChanged&)>,
            MulticastDelegate<void(const Events::LinkAdded&)>,
            MulticastDelegate<void(const Events::LinkRemoved&)>
        > m_handlers;
        
        Signal m_enabledSignals;
        
        template<Event E>
        static constexpr size_t IndexOf() noexcept
        {
            return Detail::EventIndex<E,
                Events::EntityCreated,
                Events::EntityDestroyed,
                Events::ComponentAdded,
                Events::ComponentRemoved,
                Events::ComponentUpdated,
                Events::ParentChanged,
                Events::LinkAdded,
                Events::LinkRemoved
            >;
        }
        
    public:
        SignalManager() noexcept : m_enabledSignals(Signal::None) {}
        
        template<Event E, typename... Args>
        void Emit(Args&&... args)
        {
            if (IsEnabled<E>())
            {
                E event{std::forward<Args>(args)...};
                auto& handler = std::get<IndexOf<E>()>(m_handlers);
                handler(event);
            }
        }
        
        template<Event E>
        auto& On() noexcept
        {
            return std::get<IndexOf<E>()>(m_handlers);
        }
        
        template<Event E>
        const auto& On() const noexcept
        {
            return std::get<IndexOf<E>()>(m_handlers);
        }
        
        template<Event E>
        void Enable() noexcept
        {
            m_enabledSignals |= E::flag;
        }
        
        template<Event E>
        void Disable() noexcept
        {
            m_enabledSignals &= ~E::flag;
        }
        
        template<Event E>
        bool IsEnabled() const noexcept
        {
            return HasSignal(m_enabledSignals, E::flag);
        }
        
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
        
        void ClearAllHandlers()
        {
            std::apply([](auto&... handler)
            {
                ((handler.Clear()), ...);
            }, m_handlers);
        }
    };
}