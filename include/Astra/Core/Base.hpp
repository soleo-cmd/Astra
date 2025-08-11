#pragma once

#include "../Platform/Hardware.hpp"
#include "../Platform/Platform.hpp"

// Base macros and definitions that depend on platform/hardware detection
// This is the top-level header that provides all core macros

// Cross-platform struct packing macros
#ifdef _MSC_VER
    #define ASTRA_PACK_BEGIN __pragma(pack(push, 1))
    #define ASTRA_PACK_END __pragma(pack(pop))
#elif defined(__GNUC__) || defined(__clang__)
    #define ASTRA_PACK_BEGIN _Pragma("pack(push, 1)")
    #define ASTRA_PACK_END _Pragma("pack(pop)")
#else
    #error "Unsupported compiler for struct packing"
#endif

// Feature Detection Macros
#ifdef __has_builtin
    #define ASTRA_HAS_BUILTIN(x) __has_builtin(x)
#else
    #define ASTRA_HAS_BUILTIN(x) 0
#endif

#ifdef __has_feature
    #define ASTRA_HAS_FEATURE(x) __has_feature(x)
#else
    #define ASTRA_HAS_FEATURE(x) 0
#endif

#ifdef __has_attribute
    #define ASTRA_HAS_ATTRIBUTE(x) __has_attribute(x)
#else
    #define ASTRA_HAS_ATTRIBUTE(x) 0
#endif

#ifdef __has_cpp_attribute
    #define ASTRA_HAS_CPP_ATTRIBUTE(x) __has_cpp_attribute(x)
#else
    #define ASTRA_HAS_CPP_ATTRIBUTE(x) 0
#endif

#define ASTRA_NODISCARD [[nodiscard]]
#define ASTRA_MAYBE_UNUSED [[maybe_unused]]
#define ASTRA_FALLTHROUGH [[fallthrough]]
#define ASTRA_LIKELY [[likely]]
#define ASTRA_UNLIKELY [[unlikely]]

// Compiler-specific attributes
#if defined(ASTRA_COMPILER_MSVC)
    #define ASTRA_FORCEINLINE __forceinline
    #define ASTRA_NOINLINE __declspec(noinline)
    #define ASTRA_RESTRICT __restrict
    #define ASTRA_ASSUME(x) __assume(x)
    #define ASTRA_UNREACHABLE() __assume(0)
    #define ASTRA_DEPRECATED(msg) __declspec(deprecated(msg))
#elif defined(ASTRA_COMPILER_GCC) || defined(ASTRA_COMPILER_CLANG)
    #define ASTRA_FORCEINLINE inline __attribute__((always_inline))
    #define ASTRA_NOINLINE __attribute__((noinline))
    #define ASTRA_RESTRICT __restrict__
    #define ASTRA_DEPRECATED(msg) __attribute__((deprecated(msg)))
    
    #if ASTRA_HAS_BUILTIN(__builtin_assume)
        #define ASTRA_ASSUME(x) __builtin_assume(x)
    #else
        #define ASTRA_ASSUME(x) do { if (!(x)) __builtin_unreachable(); } while(0)
    #endif
    
    #if ASTRA_HAS_BUILTIN(__builtin_unreachable)
        #define ASTRA_UNREACHABLE() __builtin_unreachable()
    #else
        #define ASTRA_UNREACHABLE() ASTRA_ASSUME(false)
    #endif
#endif

// Cache line alignment for avoiding false sharing
#define ASTRA_CACHE_ALIGNED alignas(::Astra::CACHE_LINE_SIZE)

// SIMD alignment for vectorized operations (SSE/NEON)
#define ASTRA_SIMD_ALIGNED alignas(::Astra::SIMD_ALIGNMENT)

// Utility macros
#define ASTRA_STRINGIFY_IMPL(x) #x
#define ASTRA_STRINGIFY(x) ASTRA_STRINGIFY_IMPL(x)

#define ASTRA_CONCAT_IMPL(x, y) x##y
#define ASTRA_CONCAT(x, y) ASTRA_CONCAT_IMPL(x, y)

#define ASTRA_UNUSED(x) ((void)(x))

// Runtime assertion macro
// Note: Define BUILD_DEBUG in your build system (premake5) for debug builds
#ifdef ASTRA_BUILD_DEBUG
    #include <cassert>
    #define ASTRA_ASSERT(condition, message) assert((condition) && (message))
#else
    #define ASTRA_ASSERT(condition, message) ((void)0)
#endif
