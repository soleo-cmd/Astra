#pragma once

#include <concepts>
#include <cstdint>
#include <functional>
#include <limits>
#include <type_traits>

#include "../Platform/Simd.hpp"

namespace Astra
{
    template<typename T>
    concept EntityTraitsConcept = requires
    {
        typename T::Type;
        typename T::VersionType;
        
        { T::ID_BITS } -> std::convertible_to<std::size_t>;
        { T::VERSION_SHIFT } -> std::convertible_to<std::size_t>;
        { T::ID_MASK } -> std::convertible_to<typename T::Type>;
        { T::VERSION_MASK } -> std::convertible_to<typename T::Type>;
        { T::INVALID } -> std::convertible_to<typename T::Type>;
        
        requires std::unsigned_integral<typename T::Type>;
        requires std::unsigned_integral<typename T::VersionType>;
    };

    namespace Detail
    {
        template<EntityTraitsConcept Traits>
        class BasicEntity
        {
        public:
            using Type = typename Traits::Type;
            using VersionType = typename Traits::VersionType;

            static constexpr auto ID_MASK       = Traits::ID_MASK;
            static constexpr auto VERSION_MASK  = Traits::VERSION_MASK;
            static constexpr auto VERSION_SHIFT = Traits::VERSION_SHIFT;

            constexpr BasicEntity() noexcept : m_entity{Traits::INVALID} {}
            constexpr explicit BasicEntity(Type value) noexcept : m_entity{value} {}
            constexpr BasicEntity(Type id, VersionType version) noexcept: m_entity{(static_cast<Type>(version) << VERSION_SHIFT) | (id & ID_MASK)} {}

            ASTRA_NODISCARD constexpr explicit operator bool() const noexcept { return IsValid(); }

            ASTRA_NODISCARD constexpr operator Type() const noexcept { return m_entity; }

            ASTRA_NODISCARD constexpr bool operator==(const BasicEntity& other) const noexcept = default;
            ASTRA_NODISCARD constexpr bool operator==(Type value) const noexcept { return m_entity == value; }
            ASTRA_NODISCARD friend constexpr bool operator==(Type value, const BasicEntity& entity) noexcept { return entity.m_entity == value; }

            ASTRA_NODISCARD constexpr bool operator<(const BasicEntity& other) const noexcept { return m_entity < other.m_entity; }
            ASTRA_NODISCARD constexpr bool operator>(const BasicEntity& other) const noexcept { return m_entity > other.m_entity; }

            template<typename T>
            ASTRA_NODISCARD constexpr explicit operator T() const noexcept
                requires std::convertible_to<Type, T> && !std::same_as<T, bool>
            { 
                return static_cast<T>(m_entity);
            }

            ASTRA_NODISCARD constexpr BasicEntity NextVersion() const noexcept
            {
                const auto currentVersion = GetVersion();
                const auto currentID = GetID();

                if (currentVersion >= VERSION_MASK)
                {
                    return Invalid();
                }

                return BasicEntity(currentID, currentVersion + 1);
            }

            ASTRA_NODISCARD constexpr Type GetID() const noexcept { return m_entity & ID_MASK; }
            ASTRA_NODISCARD constexpr VersionType GetVersion() const noexcept { return static_cast<VersionType>(m_entity >> VERSION_SHIFT) & VERSION_MASK; }
            ASTRA_NODISCARD constexpr Type GetValue() const noexcept { return m_entity; }

            ASTRA_NODISCARD constexpr bool IsValid() const noexcept { return m_entity != Traits::INVALID; }
            ASTRA_NODISCARD constexpr bool IsInvalid() const noexcept { return m_entity == Traits::INVALID; }
            
            ASTRA_NODISCARD static constexpr BasicEntity Invalid() noexcept { return BasicEntity{Traits::INVALID}; }

        private:
            Type m_entity;
        };
    }

    template<std::size_t TotalBits, std::size_t VersionBits>
    struct EntityTraits
    {
        static_assert(TotalBits == 32 || TotalBits == 64, "Only 32 or 64 bit variants supported");
        static_assert(VersionBits < TotalBits, "Version bits must be less than total bits");

        using Type = std::conditional_t<TotalBits == 32, std::uint32_t, std::uint64_t>;
        using VersionType = std::conditional_t<VersionBits <= 8, std::uint8_t,
                                std::conditional_t<VersionBits <= 16, std::uint16_t, std::uint32_t>>;

        static constexpr std::size_t ID_BITS = TotalBits - VersionBits;
        static constexpr std::size_t VERSION_SHIFT = ID_BITS;
        static constexpr Type ID_MASK = (Type{1} << ID_BITS) - 1;
        static constexpr Type VERSION_MASK = (Type{1} << VersionBits) - 1;
        static constexpr Type INVALID = std::numeric_limits<Type>::max();
    };

    using EntityTraits32 = EntityTraits<32, 8>;   // 8-bit version, 24-bit ID
    using EntityTraits64 = EntityTraits<64, 32>;  // 32-bit version, 32-bit ID

    using Entity = Detail::BasicEntity<EntityTraits32>;

    struct EntityHash
    {
        std::size_t operator()(const Entity& entity) const noexcept
        {
            uint64_t hash = entity.GetValue();
            hash = Simd::Ops::HashCombine(hash, 0x9E3779B97F4A7C15ULL);
            if ((hash & 0x7F) == 0)
            {
                // Ensure valid H2
                hash |= 1;
            }
            return hash;
        }
    };
}

namespace std
{
    template<>
    struct hash<Astra::Entity>
    {
        ASTRA_NODISCARD std::size_t operator()(const Astra::Entity& entity) const noexcept
        {
            return Astra::EntityHash{}(entity);
        }
    };
}
