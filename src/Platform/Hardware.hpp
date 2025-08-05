#pragma once

#include <algorithm>
#include <cstddef>
#include <new>
#include <version>

#include "Platform.hpp"

namespace Astra
{
    // Cache line size constants for proper alignment
#ifdef __cpp_lib_hardware_interference_size
    inline constexpr std::size_t DESTRUCTIVE_INTERFERENCE = std::hardware_destructive_interference_size;
    inline constexpr std::size_t CONSTRUCTIVE_INTERFERENCE = std::hardware_constructive_interference_size;
#else
    // Platform-specific fallbacks when C++17 hardware_interference_size is not available
    #if defined(ASTRA_ARCH_X64) || defined(ASTRA_ARCH_X86)
        inline constexpr std::size_t DESTRUCTIVE_INTERFERENCE = 64;
        inline constexpr std::size_t CONSTRUCTIVE_INTERFERENCE = 64;
    #elif defined(ASTRA_ARCH_ARM64)
        inline constexpr std::size_t DESTRUCTIVE_INTERFERENCE = 128;
        inline constexpr std::size_t CONSTRUCTIVE_INTERFERENCE = 64;
    #else
        inline constexpr std::size_t DESTRUCTIVE_INTERFERENCE = 64;
        inline constexpr std::size_t CONSTRUCTIVE_INTERFERENCE = 64;
    #endif
#endif

    // Cache line size - typically used for alignment
    inline constexpr std::size_t CACHE_LINE_SIZE = std::max(DESTRUCTIVE_INTERFERENCE, CONSTRUCTIVE_INTERFERENCE);
    
    // SIMD alignment requirements
    inline constexpr std::size_t SIMD_ALIGNMENT = 16;
    
    // Default page size for memory allocation
    inline constexpr std::size_t DEFAULT_PAGE_SIZE = 4096;

} // namespace Astra