#pragma once

#include <memory>
#include <version>

#include "Platform.hpp"

#define ASTRA_VERSION_MAJOR 0
#define ASTRA_VERSION_MINOR 1
#define ASTRA_VERSION_PATCH 0

#define ASTRA_VERSION ((ASTRA_VERSION_MAJOR << 16) | (ASTRA_VERSION_MINOR << 8) | ASTRA_VERSION_PATCH)

namespace Astra
{
    namespace config
    {
        inline constexpr std::size_t CACHE_LINE_SIZE = 64;
        
        inline constexpr std::size_t DEFAULT_PAGE_SIZE = 1024;
        
        inline constexpr std::size_t SPARSE_SET_PAGE_SIZE = 4096;
        
        inline constexpr std::size_t POOL_BLOCK_SIZE = 8192;
        
        inline constexpr std::size_t DENSE_MAP_INITIAL_CAPACITY = 64;
        
        inline constexpr float DENSE_MAP_MAX_LOAD_FACTOR = 0.875f;
        
        inline constexpr std::size_t PACKED_ARRAY_GROWTH_FACTOR = 2;
        
        inline constexpr std::size_t ENTITY_PAGE_SIZE = 4096;
        
        inline constexpr bool ENABLE_ASSERTS = 
#ifdef ASTRA_BUILD_DEBUG
            true;
#else
            false;
#endif
        
        inline constexpr bool ENABLE_ENTITY_CHECKS = 
#ifdef ASTRA_BUILD_DEBUG
            true;
#else
            false;
#endif
        
        inline constexpr bool ENABLE_TYPE_NAMES = 
#if defined(ASTRA_COMPILER_GCC) || defined(ASTRA_COMPILER_CLANG) || defined(ASTRA_COMPILER_MSVC)
            true;
#else
            false;
#endif
    }
    
    template<typename T>
    inline constexpr bool IsPowerOfTwo(T value) noexcept
    {
        return value && !(value & (value - 1));
    }
    
    template<typename T>
    inline constexpr T NextPowerOfTwo(T value) noexcept
    {
        if (value <= 1) return 1;
        
        --value;
        value |= value >> 1;
        value |= value >> 2;
        value |= value >> 4;
        value |= value >> 8;
        value |= value >> 16;
        
        if constexpr (sizeof(T) > 4)
        {
            value |= value >> 32;
        }
        
        return value + 1;
    }
    
    template<typename T>
    inline constexpr T AlignUp(T value, T alignment) noexcept
    {
        return (value + alignment - 1) & ~(alignment - 1);
    }
    
    template<typename T>
    inline constexpr T* AssumeAligned(T* ptr) noexcept
    {
#if defined(__cpp_lib_assume_aligned) && __cpp_lib_assume_aligned >= 201811L
        return std::assume_aligned<alignof(T)>(ptr);
#elif defined(ASTRA_COMPILER_GCC) || defined(ASTRA_COMPILER_CLANG)
        return static_cast<T*>(__builtin_assume_aligned(ptr, alignof(T)));
#else
        return ptr;
#endif
    }
}

#ifdef ASTRA_BUILD_DEBUG
    #include <cassert>
    #define ASTRA_ASSERT(condition, message) assert((condition) && (message))
#else
    #define ASTRA_ASSERT(condition, message) ((void)0)
#endif

// All platform-specific macros are now in Platform.hpp
// Just need to ensure backward compatibility if needed