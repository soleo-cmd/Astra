#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>

#include "Config.hpp"

namespace Astra
{
    template<typename E>
    struct ErrorValue
    {
        E value;

        constexpr explicit ErrorValue(const E& e) : value(e) {}
        constexpr explicit ErrorValue(E&& e) : value(std::move(e)) {}

        template<typename... Args>
        constexpr explicit ErrorValue(std::in_place_t, Args&&... args) : value(std::forward<Args>(args)...) {}
    };

    template<typename E>
    constexpr ErrorValue<std::decay_t<E>> Err(E&& e)
    {
        return ErrorValue<std::decay_t<E>>(std::forward<E>(e));
    }

    struct OkTag {};
    constexpr OkTag OK{};

    namespace internal
    {
        template<typename T>
        struct AlignedStorage
        {
            alignas(T) std::uint8_t data[sizeof(T)];

            template<typename U>
            U* as() noexcept
            {
                return std::launder(reinterpret_cast<U*>(&data));
            }

            template<typename U>
            const U* as() const noexcept
            {
                return std::launder(reinterpret_cast<const U*>(&data));
            }
        };

        template<typename T, typename E>
        struct VariantStorage
        {
            static constexpr std::size_t size = std::max(sizeof(T), sizeof(E));
            static constexpr std::size_t alignment = std::max(alignof(T), alignof(E));

            alignas(alignment) std::uint8_t data[size];

            template<typename U>
            U* as() noexcept
            {
                return std::launder(reinterpret_cast<U*>(&data));
            }

            template<typename U>
            const U* as() const noexcept
            {
                return std::launder(reinterpret_cast<const U*>(&data));
            }
        };
    }

    template<typename T, typename E>
    class Result
    {
        static_assert(!std::is_reference_v<T>, "T cannot be a reference type");
        static_assert(!std::is_reference_v<E>, "E cannot be a reference type");

    public:
        using ValueType = T;
        using ErrorType = E;

        constexpr Result() noexcept(std::is_nothrow_default_constructible_v<T>) : m_hasValue(true)
        {
            ConstructValue();
        }

        constexpr Result(const T& value) noexcept(std::is_nothrow_copy_constructible_v<T>) : m_hasValue(true)
        {
            ConstructValue(value);
        }

        constexpr Result(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>) : m_hasValue(true)
        {
            ConstructValue(std::move(value));
        }

        constexpr Result(const ErrorValue<E>& err) noexcept(std::is_nothrow_copy_constructible_v<E>) : m_hasValue(false)
        {
            ConstructError(err.value);
        }

        constexpr Result(ErrorValue<E>&& err) noexcept(std::is_nothrow_move_constructible_v<E>) : m_hasValue(false)
        {
            ConstructError(std::move(err.value));
        }

        constexpr Result(const Result& other) noexcept(std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_copy_constructible_v<E>) 
            : m_hasValue(other.m_hasValue)
        {
            if (m_hasValue)
            {
                ConstructValue(other.Value());
            }
            else
            {
                ConstructError(other.Error());
            }
        }

        constexpr Result(Result&& other) noexcept(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_constructible_v<E>) 
            : m_hasValue(other.m_hasValue)
        {
            if (m_hasValue)
            {
                ConstructValue(std::move(other).Value());
            }
            else
            {
                ConstructError(std::move(other).Error());
            }
        }

        ~Result()
        {
            Destroy();
        }

        constexpr Result& operator=(const Result& other) noexcept(std::is_nothrow_copy_assignable_v<T> && std::is_nothrow_copy_assignable_v<E>)
        {
            if (this != &other)
            {
                if (m_hasValue && other.m_hasValue)
                {
                    Value() = other.Value();
                }
                else if (!m_hasValue && !other.m_hasValue)
                {
                    Error() = other.Error();
                }
                else
                {
                    Destroy();
                    m_hasValue = other.m_hasValue;
                    if (m_hasValue)
                    {
                        ConstructValue(other.Value());
                    }
                    else
                    {
                        ConstructError(other.Error());
                    }
                }
            }
            return *this;
        }

        constexpr Result& operator=(Result&& other) noexcept(std::is_nothrow_move_assignable_v<T> && std::is_nothrow_move_assignable_v<E>)
        {
            if (this != &other)
            {
                if (m_hasValue && other.m_hasValue)
                {
                    Value() = std::move(other).Value();
                }
                else if (!m_hasValue && !other.m_hasValue)
                {
                    Error() = std::move(other).Error();
                }
                else
                {
                    Destroy();
                    m_hasValue = other.m_hasValue;
                    if (m_hasValue)
                    {
                        ConstructValue(std::move(other).Value());
                    }
                    else
                    {
                        ConstructError(std::move(other).Error());
                    }
                }
            }
            return *this;
        }

        [[nodiscard]] constexpr bool HasValue() const noexcept { return m_hasValue; }
        [[nodiscard]] constexpr bool IsOk() const noexcept { return m_hasValue; }
        [[nodiscard]] constexpr bool IsErr() const noexcept { return !m_hasValue; }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return m_hasValue; }

        constexpr T& Value() &
        {
            ASTRA_ASSERT(m_hasValue, "Called Value() on Result containing error");
            return *ValPtr();
        }

        constexpr const T& Value() const&
        {
            ASTRA_ASSERT(m_hasValue, "Called Value() on Result containing error");
            return *ValPtr();
        }

        constexpr T&& Value() &&
        {
            ASTRA_ASSERT(m_hasValue, "Called Value() on Result containing error");
            return std::move(*ValPtr());
        }

        constexpr const T&& Value() const&&
        {
            ASTRA_ASSERT(m_hasValue, "Called Value() on Result containing error");
            return std::move(*ValPtr());
        }

        constexpr E& Error() &
        {
            ASTRA_ASSERT(!m_hasValue, "Called Error() on Result containing value");
            return *ErrPtr();
        }

        constexpr const E& Error() const&
        {
            ASTRA_ASSERT(!m_hasValue, "Called Error() on Result containing value");
            return *ErrPtr();
        }

        constexpr E&& Error() &&
        {
            ASTRA_ASSERT(!m_hasValue, "Called Error() on Result containing value");
            return std::move(*ErrPtr());
        }

        constexpr const E&& Error() const&&
        {
            ASTRA_ASSERT(!m_hasValue, "Called Error() on Result containing value");
            return std::move(*ErrPtr());
        }

        constexpr T& operator*() &
        {
            ASTRA_ASSERT(m_hasValue, "Called operator* on Result containing error");
            return Value();
        }

        constexpr const T& operator*() const&
        {
            ASTRA_ASSERT(m_hasValue, "Called operator* on Result containing error");
            return Value();
        }

        constexpr T&& operator*() &&
        {
            ASTRA_ASSERT(m_hasValue, "Called operator* on Result containing error");
            return std::move(*this).Value();
        }

        constexpr const T&& operator*() const&&
        {
            ASTRA_ASSERT(m_hasValue, "Called operator* on Result containing error");
            return std::move(*this).Value();
        }

        constexpr T* operator->() noexcept
        {
            ASTRA_ASSERT(m_hasValue, "Called operator-> on Result containing error");
            return ValPtr();
        }

        constexpr const T* operator->() const noexcept
        {
            ASTRA_ASSERT(m_hasValue, "Called operator-> on Result containing error");
            return ValPtr();
        }

        template<typename U>
        [[nodiscard]] constexpr T ValueOr(U&& defaultValue) const&
        {
            return m_hasValue ? Value() : static_cast<T>(std::forward<U>(defaultValue));
        }

        template<typename U>
        [[nodiscard]] constexpr T ValueOr(U&& defaultValue) &&
        {
            return m_hasValue ? std::move(*this).Value() : static_cast<T>(std::forward<U>(defaultValue));
        }

        template<typename... Args>
        constexpr void Emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>)
        {
            Destroy();
            ConstructValue(std::forward<Args>(args)...);
            m_hasValue = true;
        }

        template<typename... Args>
        constexpr void EmplaceError(Args&&... args) noexcept(std::is_nothrow_constructible_v<E, Args...>)
        {
            Destroy();
            ConstructError(std::forward<Args>(args)...);
            m_hasValue = false;
        }

    private:
        template<typename... Args>
        constexpr void ConstructValue(Args&&... args)
        {
            ::new(ValPtr()) T(std::forward<Args>(args)...);
        }

        template<typename... Args>
        constexpr void ConstructError(Args&&... args)
        {
            ::new(ErrPtr()) E(std::forward<Args>(args)...);
        }

        constexpr void Destroy()
        {
            if (m_hasValue)
            {
                if constexpr (!std::is_trivially_destructible_v<T>)
                {
                    ValPtr()->~T();
                }
            }
            else
            {
                if constexpr (!std::is_trivially_destructible_v<E>)
                {
                    ErrPtr()->~E();
                }
            }
        }

        T* ValPtr() noexcept
        {
            return m_storage.template as<T>();
        }

        const T* ValPtr() const noexcept
        {
            return m_storage.template as<T>();
        }

        E* ErrPtr() noexcept
        {
            return m_storage.template as<E>();
        }

        const E* ErrPtr() const noexcept
        {
            return m_storage.template as<E>();
        }

        internal::VariantStorage<T, E> m_storage;
        bool m_hasValue;
    };

    template<typename E>
    class Result<void, E>
    {
        static_assert(!std::is_reference_v<E>, "E cannot be a reference type");

    public:
        using ValueType = void;
        using ErrorType = E;

        constexpr Result() noexcept : m_hasValue(true) {}

        constexpr Result(OkTag) noexcept : m_hasValue(true) {}

        constexpr Result(const ErrorValue<E>& err) noexcept(std::is_nothrow_copy_constructible_v<E>) : m_hasValue(false)
        {
            ConstructError(err.value);
        }

        constexpr Result(ErrorValue<E>&& err) noexcept(std::is_nothrow_move_constructible_v<E>) : m_hasValue(false)
        {
            ConstructError(std::move(err.value));
        }

        constexpr Result(const Result& other) noexcept(std::is_nothrow_copy_constructible_v<E>) : m_hasValue(other.m_hasValue)
        {
            if (!m_hasValue)
                ConstructError(other.Error());
        }

        constexpr Result(Result&& other) noexcept(std::is_nothrow_move_constructible_v<E>) : m_hasValue(other.m_hasValue)
        {
            if (!m_hasValue)
                ConstructError(std::move(other).Error());
        }

        ~Result()
        {
            if (!m_hasValue)
                Destroy();
        }

        constexpr Result& operator=(const Result& other) noexcept(std::is_nothrow_copy_assignable_v<E>)
        {
            if (this != &other)
            {
                if (!m_hasValue && !other.m_hasValue)
                {
                    Error() = other.Error();
                }
                else if (m_hasValue && !other.m_hasValue)
                {
                    ConstructError(other.Error());
                    m_hasValue = false;
                }
                else if (!m_hasValue && other.m_hasValue)
                {
                    Destroy();
                    m_hasValue = true;
                }
            }
            return *this;
        }

        constexpr Result& operator=(Result&& other) noexcept(std::is_nothrow_move_assignable_v<E>)
        {
            if (this != &other)
            {
                if (!m_hasValue && !other.m_hasValue)
                {
                    Error() = std::move(other).Error();
                }
                else if (m_hasValue && !other.m_hasValue)
                {
                    ConstructError(std::move(other).Error());
                    m_hasValue = false;
                }
                else if (!m_hasValue && other.m_hasValue)
                {
                    Destroy();
                    m_hasValue = true;
                }
            }
            return *this;
        }

        [[nodiscard]] constexpr bool HasValue() const noexcept { return m_hasValue; }
        [[nodiscard]] constexpr bool IsOk() const noexcept { return m_hasValue; }
        [[nodiscard]] constexpr bool IsErr() const noexcept { return !m_hasValue; }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return m_hasValue; }

        constexpr void Value() const& { ASTRA_ASSERT(m_hasValue, "Called Value() on Result containing error"); }
        constexpr void Value() && { ASTRA_ASSERT(m_hasValue, "Called Value() on Result containing error"); }

        constexpr E& Error() & 
        { 
            ASTRA_ASSERT(!m_hasValue, "Called Error() on Result containing value"); 
            return *ErrPtr();
        }

        constexpr const E& Error() const& 
        { 
            ASTRA_ASSERT(!m_hasValue, "Called Error() on Result containing value"); 
            return *ErrPtr();
        }

        constexpr E&& Error() && 
        { 
            ASTRA_ASSERT(!m_hasValue, "Called Error() on Result containing value"); 
            return std::move(*ErrPtr());
        }

        constexpr const E&& Error() const&& 
        { 
            ASTRA_ASSERT(!m_hasValue, "Called Error() on Result containing value"); 
            return std::move(*ErrPtr());
        }

        constexpr void operator*() const noexcept
        {
            ASTRA_ASSERT(m_hasValue, "Called operator* on Result containing error");
        }

        template<typename... Args>
        constexpr void EmplaceError(Args&&... args) noexcept(std::is_nothrow_constructible_v<E, Args...>)
        {
            if (!m_hasValue)
                Destroy();
            ConstructError(std::forward<Args>(args)...);
            m_hasValue = false;
        }

    private:
        template<typename... Args>
        constexpr void ConstructError(Args&&... args)
        {
            ::new(ErrPtr()) E(std::forward<Args>(args)...);
        }

        constexpr void Destroy()
        {
            if constexpr (!std::is_trivially_destructible_v<E>)
            {
                ErrPtr()->~E();
            }
        }

        E* ErrPtr() noexcept { return m_storage.template as<E>(); }
        const E* ErrPtr() const noexcept { return m_storage.template as<E>(); }

        internal::AlignedStorage<E> m_storage;
        bool m_hasValue;
    };

    template<typename T, typename E>
    [[nodiscard]] constexpr bool operator==(const Result<T, E>& lhs, const Result<T, E>& rhs)
    {
        if (lhs.HasValue() != rhs.HasValue()) return false;
        if (lhs.HasValue())
        {
            if constexpr (!std::is_void_v<T>)
            {
                return lhs.Value() == rhs.Value();
            }
            else
            {
                return true;
            }
        }
        return lhs.Error() == rhs.Error();
    }

    template<typename T, typename E>
    [[nodiscard]] constexpr bool operator!=(const Result<T, E>& lhs, const Result<T, E>& rhs)
    {
        return !(lhs == rhs);
    }

    template<typename T, typename E, typename U>
    [[nodiscard]] constexpr bool operator==(const Result<T, E>& lhs, const U& rhs)
    {
        return lhs.HasValue() && lhs.Value() == rhs;
    }

    template<typename T, typename E, typename U>
    [[nodiscard]] constexpr bool operator==(const U& lhs, const Result<T, E>& rhs)
    {
        return rhs == lhs;
    }

    template<typename T, typename E, typename U>
    [[nodiscard]] constexpr bool operator!=(const Result<T, E>& lhs, const U& rhs)
    {
        return !(lhs == rhs);
    }

    template<typename T, typename E, typename U>
    [[nodiscard]] constexpr bool operator!=(const U& lhs, const Result<T, E>& rhs)
    {
        return !(lhs == rhs);
    }

    template<typename T, typename E>
    [[nodiscard]] constexpr bool operator==(const Result<T, E>& lhs, const ErrorValue<E>& rhs)
    {
        return !lhs.HasValue() && lhs.Error() == rhs.value;
    }

    template<typename T, typename E>
    [[nodiscard]] constexpr bool operator==(const ErrorValue<E>& lhs, const Result<T, E>& rhs)
    {
        return rhs == lhs;
    }

    template<typename T, typename E>
    [[nodiscard]] constexpr bool operator!=(const Result<T, E>& lhs, const ErrorValue<E>& rhs)
    {
        return !(lhs == rhs);
    }

    template<typename T, typename E>
    [[nodiscard]] constexpr bool operator!=(const ErrorValue<E>& lhs, const Result<T, E>& rhs)
    {
        return !(lhs == rhs);
    }
    
    // Forward declarations
    struct Error;
    
    // Helper function for creating successful void results
    inline Result<void, Error> Ok()
    {
        return Result<void, Error>();
    }
    
    // Helper function for creating successful results with values
    template<typename T>
    inline auto Ok(T&& value) -> Result<std::decay_t<T>, Error>
    {
        return Result<std::decay_t<T>, Error>(std::forward<T>(value));
    }
}

namespace std
{
    template<typename T, typename E>
    struct hash<Astra::Result<T, E>>
    {
        static constexpr size_t HASH_MIX = 0x9e3779b97f4a7c15ULL;

        size_t operator()(const Astra::Result<T, E>& r) const noexcept
        {
            if (r.HasValue())
            {
                if constexpr (!std::is_void_v<T>)
                {
                    return std::hash<T>{}(r.Value());
                }
                else
                {
                    return 0;
                }
            }
            else
            {
                return std::hash<E>{}(r.Error()) ^ HASH_MIX;
            }
        }
    };

    template<typename E>
    struct hash<Astra::Result<void, E>>
    {
        static constexpr size_t HASH_MIX = 0x9e3779b97f4a7c15ULL;

        size_t operator()(const Astra::Result<void, E>& r) const noexcept
        {
            if (r.HasValue())
            {
                return 0;
            }
            else
            {
                return std::hash<E>{}(r.Error()) ^ HASH_MIX;
            }
        }
    };
}
