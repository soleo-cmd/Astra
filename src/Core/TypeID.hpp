#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "../Component/Component.hpp"
#include "Base.hpp"

namespace Astra
{
    namespace Detail
    {
        template<typename T>
        struct TypeName
        {
            ASTRA_NODISCARD static constexpr std::string_view Value() noexcept
            {
#if defined(ASTRA_COMPILER_CLANG)
                constexpr auto prefix = std::string_view{"[T = "};
                constexpr auto suffix = std::string_view{"]"};
                constexpr auto function = std::string_view{__PRETTY_FUNCTION__};
#elif defined(ASTRA_COMPILER_GCC)
                constexpr auto prefix = std::string_view{"with T = "};
                constexpr auto suffix = std::string_view{"]"};
                constexpr auto function = std::string_view{__PRETTY_FUNCTION__};
#elif defined(ASTRA_COMPILER_MSVC)
                constexpr auto prefix = std::string_view{"TypeName<"};
                constexpr auto suffix = std::string_view{">::Value"};
                constexpr auto function = std::string_view{__FUNCSIG__};
#else
                return "unknown";
#endif

#if defined(ASTRA_COMPILER_CLANG) || defined(ASTRA_COMPILER_GCC) || defined(ASTRA_COMPILER_MSVC)
                constexpr auto start = function.find(prefix) + prefix.size();
                constexpr auto end = function.rfind(suffix);
                constexpr auto size = end - start;

                return function.substr(start, size);
#endif
            }
        };

        template<typename T>
        struct TypeHash
        {
            ASTRA_NODISCARD static constexpr std::uint64_t Value() noexcept
            {
                constexpr auto name = TypeName<T>::Value();
                std::uint64_t hash = 14695981039346656037ULL;

                for (char c : name)
                {
                    hash ^= static_cast<std::uint64_t>(c);
                    hash *= 1099511628211ull;
                }

                return hash;
            }
        };

        class TypeIDGenerator
        {
        public:
            ASTRA_NODISCARD static ComponentID Next() noexcept
            {
                // We only need atomicity, not ordering. Each type's ID is
                // generated once during static initialization and cached.
                // The C++11 magic statics guarantee thread-safe initialization
                // of the static variable in TypeIDStorage::Value().
                return s_nextId.fetch_add(1, std::memory_order_relaxed);
            }

        private:
            inline static std::atomic<ComponentID> s_nextId{0};
        };

        template<typename T>
        class TypeIDStorage
        {
        public:
            ASTRA_NODISCARD static ComponentID Value() noexcept
            {
                static const ComponentID s_id = TypeIDGenerator::Next();
                return s_id;
            }
        };
    }

    template<typename T>
    struct TypeID
    {
        using Type = std::remove_cv_t<std::remove_reference_t<T>>;

        ASTRA_NODISCARD static ComponentID Value() noexcept
        {
            return Detail::TypeIDStorage<Type>::Value();
        }

        ASTRA_NODISCARD static constexpr std::string_view Name() noexcept
        {
#if defined(ASTRA_COMPILER_GCC) || defined(ASTRA_COMPILER_CLANG) || defined(ASTRA_COMPILER_MSVC)
            return Detail::TypeName<Type>::Value();
#else
            return "unknown";
#endif
        }

        ASTRA_NODISCARD static constexpr std::uint64_t Hash() noexcept
        {
            return Detail::TypeHash<Type>::Value();
        }
    };

    template<typename T>
    inline const ComponentID TypeID_v = TypeID<T>::Value();

    template<typename T>
    inline constexpr std::string_view TypeName_v = TypeID<T>::Name();

    template<typename T>
    inline constexpr std::uint64_t TypeHash_v = TypeID<T>::Hash();

    template<typename T, typename U>
    ASTRA_NODISCARD constexpr bool IsSameType() noexcept
    {
        return std::is_same_v<typename TypeID<T>::Type, typename TypeID<U>::Type>;
    }
}
