#pragma once

#include <cstdint>
#include <limits>
#include <type_traits>

namespace Astra
{
    using ComponentID = std::uint16_t;
    
    inline constexpr ComponentID INVALID_COMPONENT = std::numeric_limits<ComponentID>::max();
    
    constexpr std::size_t MAX_COMPONENTS = 256u; // Increased for larger projects
    
    template<typename T>
    concept Component = std::is_move_constructible_v<T> && 
                        std::is_move_assignable_v<T>    &&
                        std::is_destructible_v<T>;
}
