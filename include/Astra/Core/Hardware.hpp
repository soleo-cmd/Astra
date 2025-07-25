#pragma once

#include <algorithm>
#include <cstddef>
#include <version>
#include "Platform.hpp"

namespace Astra
{
    // Hardware interference sizes - compile-time constants for cache optimization

#ifdef __cpp_lib_hardware_interference_size
    inline constexpr std::size_t DESTRUCTIVE_INTERFERENCE_SIZE = std::hardware_destructive_interference_size;
    inline constexpr std::size_t CONSTRUCTIVE_INTERFERENCE_SIZE = std::hardware_constructive_interference_size;
#else
    // Fallback cache line sizes:
    // - x86/x64: typically 64 bytes
    // - ARM64: some systems use 128-byte destructive padding
#if defined(ASTRA_ARCH_X64) || defined(ASTRA_ARCH_X86)
    inline constexpr std::size_t DESTRUCTIVE_INTERFERENCE_SIZE = 64;
    inline constexpr std::size_t CONSTRUCTIVE_INTERFERENCE_SIZE = 64;
#elif defined(ASTRA_ARCH_ARM64)
    inline constexpr std::size_t DESTRUCTIVE_INTERFERENCE_SIZE = 128;
    inline constexpr std::size_t CONSTRUCTIVE_INTERFERENCE_SIZE = 64;
#else
    inline constexpr std::size_t DESTRUCTIVE_INTERFERENCE_SIZE = 64;
    inline constexpr std::size_t CONSTRUCTIVE_INTERFERENCE_SIZE = 64;
#endif
#endif

    inline constexpr std::size_t CACHE_LINE_SIZE = std::max<std::size_t>(DESTRUCTIVE_INTERFERENCE_SIZE, CONSTRUCTIVE_INTERFERENCE_SIZE);

    // SIMD alignment size - 16 bytes for SSE/NEON (128-bit registers)
    inline constexpr std::size_t SIMD_ALIGNMENT = 16;

    // Default page size for memory allocation hints
    inline constexpr std::size_t DEFAULT_PAGE_SIZE = 4096;

    // Alignment helper for cache-friendly data structures
    template<typename T>
    struct alignas(CACHE_LINE_SIZE) CacheLineAligned
    {
        T value;
    };
}