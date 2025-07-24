#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <type_traits>

namespace Astra
{
    using EntityID = std::uint32_t;
    
    namespace internal
    {
        template<typename Traits>
        class BasicEntity
        {
        public:
            using EntityType = typename Traits::entity_type;
            using VersionType = typename Traits::version_type;
            
            static constexpr auto ENTITY_MASK = Traits::ENTITY_MASK;
            static constexpr auto VERSION_MASK = Traits::VERSION_MASK;
            static constexpr auto ENTITY_SHIFT = Traits::ENTITY_SHIFT;
            
        private:
            EntityType m_entity;
            
        public:
            constexpr BasicEntity() noexcept 
                : m_entity{Traits::NULL_VALUE} {}
                
            constexpr explicit BasicEntity(EntityType value) noexcept 
                : m_entity{value} {}
                
            constexpr BasicEntity(EntityType index, VersionType version) noexcept
                : m_entity{(static_cast<EntityType>(version) << ENTITY_SHIFT) | (index & ENTITY_MASK)} {}
            
            [[nodiscard]] constexpr EntityType Index() const noexcept
            {
                return m_entity & ENTITY_MASK;
            }
            
            [[nodiscard]] constexpr VersionType Version() const noexcept
            {
                return static_cast<VersionType>(m_entity >> ENTITY_SHIFT) & VERSION_MASK;
            }
            
            [[nodiscard]] constexpr BasicEntity NextVersion() const noexcept
            {
                const auto current_version = Version();
                const auto current_index = Index();
                
                // Check for version overflow
                if (current_version >= VERSION_MASK)
                {
                    // Return invalid entity on overflow
                    return BasicEntity(Traits::NULL_VALUE);
                }
                
                return BasicEntity(current_index, current_version + 1);
            }
            
            [[nodiscard]] constexpr EntityType Value() const noexcept
            {
                return m_entity;
            }
            
            [[nodiscard]] constexpr bool Valid() const noexcept
            {
                return m_entity != Traits::NULL_VALUE;
            }
            
            [[nodiscard]] constexpr explicit operator bool() const noexcept
            {
                return Valid();
            }
            
            [[nodiscard]] constexpr bool operator==(const BasicEntity& other) const noexcept = default;
            [[nodiscard]] constexpr bool operator!=(const BasicEntity& other) const noexcept = default;
            
            [[nodiscard]] constexpr bool operator<(const BasicEntity& other) const noexcept
            {
                return m_entity < other.m_entity;
            }
            
            // Static factory for null entity
            [[nodiscard]] static constexpr BasicEntity Null() noexcept
            {
                return BasicEntity();
            }
        };
    }
    
    struct EntityTraits32
    {
        using entity_type = std::uint32_t;
        using version_type = std::uint8_t;
        
        static constexpr std::size_t ENTITY_SHIFT = 24u;
        static constexpr entity_type ENTITY_MASK = 0xFFFFFFu;  // 24 bits for entity ID (16,777,215 max)
        static constexpr entity_type VERSION_MASK = 0xFFu;     // 8 bits for version (255 max)
        static constexpr entity_type NULL_VALUE = std::numeric_limits<entity_type>::max();
    };
    
    struct EntityTraits64
    {
        using entity_type = std::uint64_t;
        using version_type = std::uint32_t;
        
        static constexpr std::size_t ENTITY_SHIFT = 32u;
        static constexpr entity_type ENTITY_MASK = 0xFFFFFFFFu;
        static constexpr entity_type VERSION_MASK = 0xFFFFFFFFu;
        static constexpr entity_type NULL_VALUE = std::numeric_limits<entity_type>::max();
    };
    
    using Entity = internal::BasicEntity<EntityTraits32>;
    using Entity64 = internal::BasicEntity<EntityTraits64>;
    
    inline constexpr EntityID NULL_ENTITY = std::numeric_limits<EntityID>::max();
    
    constexpr std::size_t MAX_ENTITIES = EntityTraits32::ENTITY_MASK;  // 16,777,215 entities max
}

namespace std
{
    template<>
    struct hash<Astra::Entity>
    {
        [[nodiscard]] std::size_t operator()(const Astra::Entity& entity) const noexcept
        {
            return std::hash<Astra::EntityID>{}(entity.Value());
        }
    };
    
    template<>
    struct hash<Astra::Entity64>
    {
        [[nodiscard]] std::size_t operator()(const Astra::Entity64& entity) const noexcept
        {
            return std::hash<std::uint64_t>{}(entity.Value());
        }
    };
}