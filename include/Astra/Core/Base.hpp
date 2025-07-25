#pragma once

#include "Platform.hpp"
#include "Hardware.hpp"

// Base macros and definitions that depend on platform/hardware detection
// This is the top-level header that provides all core macros

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

// Compiler-specific attributes
#if defined(ASTRA_COMPILER_MSVC)
    #define ASTRA_FORCEINLINE __forceinline
    #define ASTRA_NOINLINE __declspec(noinline)
    #define ASTRA_RESTRICT __restrict
    #define ASTRA_THREAD_LOCAL __declspec(thread)
    #define ASTRA_ALIGNED(x) __declspec(align(x))
    #define ASTRA_PACKED
    #define ASTRA_LIKELY(x) (x)
    #define ASTRA_UNLIKELY(x) (x)
    #define ASTRA_ASSUME(x) __assume(x)
    #define ASTRA_UNREACHABLE() __assume(0)
    #define ASTRA_PREFETCH(ptr, rw, locality) ((void)0)
    #define ASTRA_DEPRECATED(msg) __declspec(deprecated(msg))
    #define ASTRA_NODISCARD [[nodiscard]]
    #define ASTRA_MAYBE_UNUSED [[maybe_unused]]
    #define ASTRA_FALLTHROUGH [[fallthrough]]
#elif defined(ASTRA_COMPILER_GCC) || defined(ASTRA_COMPILER_CLANG)
    #define ASTRA_FORCEINLINE inline __attribute__((always_inline))
    #define ASTRA_NOINLINE __attribute__((noinline))
    #define ASTRA_RESTRICT __restrict__
    #define ASTRA_THREAD_LOCAL __thread
    #define ASTRA_ALIGNED(x) __attribute__((aligned(x)))
    #define ASTRA_PACKED __attribute__((packed))
    #define ASTRA_DEPRECATED(msg) __attribute__((deprecated(msg)))
    #define ASTRA_NODISCARD [[nodiscard]]
    #define ASTRA_MAYBE_UNUSED [[maybe_unused]]
    #define ASTRA_FALLTHROUGH [[fallthrough]]
    
    #if ASTRA_HAS_BUILTIN(__builtin_expect)
        #define ASTRA_LIKELY(x) __builtin_expect(!!(x), 1)
        #define ASTRA_UNLIKELY(x) __builtin_expect(!!(x), 0)
    #else
        #define ASTRA_LIKELY(x) (x)
        #define ASTRA_UNLIKELY(x) (x)
    #endif
    
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
    
    #if ASTRA_HAS_BUILTIN(__builtin_prefetch)
        #define ASTRA_PREFETCH(ptr, rw, locality) __builtin_prefetch(ptr, rw, locality)
    #else
        #define ASTRA_PREFETCH(ptr, rw, locality) ((void)0)
    #endif
#endif

// Export/Import Macros
#if defined(ASTRA_SHARED_LIB)
    #if defined(ASTRA_PLATFORM_WINDOWS)
        #if defined(ASTRA_EXPORTS)
            #define ASTRA_API __declspec(dllexport)
        #else
            #define ASTRA_API __declspec(dllimport)
        #endif
    #elif ASTRA_HAS_ATTRIBUTE(visibility)
        #define ASTRA_API __attribute__((visibility("default")))
    #else
        #define ASTRA_API
    #endif
#else
    #define ASTRA_API
#endif

// Alignment helper
#define ASTRA_ALIGNAS(x) alignas(x)

// Cache line alignment for avoiding false sharing
#define ASTRA_CACHE_ALIGNED alignas(::Astra::CACHE_LINE_SIZE)

// SIMD alignment for vectorized operations (SSE/NEON)
#define ASTRA_SIMD_ALIGNED alignas(::Astra::SIMD_ALIGNMENT)

// Platform-specific includes
#if defined(ASTRA_PLATFORM_WINDOWS)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
#endif

// Utility macros
#define ASTRA_STRINGIFY_IMPL(x) #x
#define ASTRA_STRINGIFY(x) ASTRA_STRINGIFY_IMPL(x)

#define ASTRA_CONCAT_IMPL(x, y) x##y
#define ASTRA_CONCAT(x, y) ASTRA_CONCAT_IMPL(x, y)

#define ASTRA_UNUSED(x) ((void)(x))

// Static assertion with message
#define ASTRA_STATIC_ASSERT(cond, msg) static_assert(cond, msg)

// Runtime assertion macro
// Note: Define ASTRA_BUILD_DEBUG in your build system (premake5) for debug builds
#ifdef ASTRA_BUILD_DEBUG
    #include <cassert>
    #define ASTRA_ASSERT(condition, message) assert((condition) && (message))
#else
    #define ASTRA_ASSERT(condition, message) ((void)0)
#endif