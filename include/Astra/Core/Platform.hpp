#pragma once

// Platform Detection
#if defined(_WIN32) || defined(_WIN64)
    #define ASTRA_PLATFORM_WINDOWS 1
    #if defined(_WIN64)
        #define ASTRA_PLATFORM_WIN64 1
    #else
        #define ASTRA_PLATFORM_WIN32 1
    #endif
#elif defined(__APPLE__) && defined(__MACH__)
    #include <TargetConditionals.h>
    #if TARGET_OS_MAC == 1
        #define ASTRA_PLATFORM_MACOS 1
    #elif TARGET_OS_IPHONE == 1
        #define ASTRA_PLATFORM_IOS 1
    #endif
    #define ASTRA_PLATFORM_APPLE 1
#elif defined(__linux__)
    #define ASTRA_PLATFORM_LINUX 1
    #if defined(__ANDROID__)
        #define ASTRA_PLATFORM_ANDROID 1
    #endif
#elif defined(__unix__)
    #define ASTRA_PLATFORM_UNIX 1
#elif defined(__FreeBSD__)
    #define ASTRA_PLATFORM_FREEBSD 1
#elif defined(__NetBSD__)
    #define ASTRA_PLATFORM_NETBSD 1
#elif defined(__OpenBSD__)
    #define ASTRA_PLATFORM_OPENBSD 1
#else
    #error "Unknown platform"
#endif

// Compiler Detection
#if defined(_MSC_VER)
    #define ASTRA_COMPILER_MSVC 1
    #define ASTRA_COMPILER_VERSION _MSC_VER
    #if _MSC_VER >= 1930
        #define ASTRA_COMPILER_MSVC_2022 1
    #elif _MSC_VER >= 1920
        #define ASTRA_COMPILER_MSVC_2019 1
    #elif _MSC_VER >= 1910
        #define ASTRA_COMPILER_MSVC_2017 1
    #endif
#elif defined(__clang__)
    #define ASTRA_COMPILER_CLANG 1
    #define ASTRA_COMPILER_VERSION (__clang_major__ * 10000 + __clang_minor__ * 100 + __clang_patchlevel__)
    #if defined(__apple_build_version__)
        #define ASTRA_COMPILER_APPLE_CLANG 1
    #endif
#elif defined(__GNUC__) || defined(__GNUG__)
    #define ASTRA_COMPILER_GCC 1
    #define ASTRA_COMPILER_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#elif defined(__INTEL_COMPILER)
    #define ASTRA_COMPILER_INTEL 1
    #define ASTRA_COMPILER_VERSION __INTEL_COMPILER
#else
    #error "Unknown compiler"
#endif

// Architecture Detection
#if defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
    #define ASTRA_ARCH_X64 1
    #define ASTRA_ARCH_NAME "x86_64"
    #define ASTRA_POINTER_SIZE 8
#elif defined(__i386__) || defined(_M_IX86)
    #define ASTRA_ARCH_X86 1
    #define ASTRA_ARCH_NAME "x86"
    #define ASTRA_POINTER_SIZE 4
#elif defined(__arm__) || defined(_M_ARM)
    #define ASTRA_ARCH_ARM32 1
    #define ASTRA_ARCH_NAME "arm32"
    #define ASTRA_POINTER_SIZE 4
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define ASTRA_ARCH_ARM64 1
    #define ASTRA_ARCH_NAME "arm64"
    #define ASTRA_POINTER_SIZE 8
#elif defined(__wasm__)
    #define ASTRA_ARCH_WASM 1
    #define ASTRA_ARCH_NAME "wasm"
    #if defined(__wasm64__)
        #define ASTRA_ARCH_WASM64 1
        #define ASTRA_POINTER_SIZE 8
    #else
        #define ASTRA_ARCH_WASM32 1
        #define ASTRA_POINTER_SIZE 4
    #endif
#else
    #error "Unknown architecture"
#endif

// Endianness Detection
#if defined(__BYTE_ORDER__)
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        #define ASTRA_LITTLE_ENDIAN 1
    #elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        #define ASTRA_BIG_ENDIAN 1
    #endif
#elif defined(_MSC_VER) || defined(__i386__) || defined(__x86_64__) || defined(__aarch64__)
    #define ASTRA_LITTLE_ENDIAN 1
#else
    #error "Unknown endianness"
#endif

// Debug/Release Detection
#if defined(DEBUG) || defined(_DEBUG) || defined(ASTRA_DEBUG)
    #define ASTRA_BUILD_DEBUG 1
    #define ASTRA_BUILD_TYPE "Debug"
#elif defined(NDEBUG) || defined(ASTRA_RELEASE)
    #define ASTRA_BUILD_RELEASE 1
    #define ASTRA_BUILD_TYPE "Release"
#else
    #define ASTRA_BUILD_UNKNOWN 1
    #define ASTRA_BUILD_TYPE "Unknown"
#endif

// Standard Library Detection
#if defined(_LIBCPP_VERSION)
    #define ASTRA_STDLIB_LIBCXX 1
#elif defined(__GLIBCXX__)
    #define ASTRA_STDLIB_LIBSTDCXX 1
#elif defined(_MSC_VER)
    #define ASTRA_STDLIB_MSVC 1
#endif

// C++ Standard Detection
#if __cplusplus >= 202302L
    #define ASTRA_CPP23 1
    #define ASTRA_CPP_VERSION 23
#elif __cplusplus >= 202002L
    #define ASTRA_CPP20 1
    #define ASTRA_CPP_VERSION 20
#elif __cplusplus >= 201703L
    #define ASTRA_CPP17 1
    #define ASTRA_CPP_VERSION 17
#elif __cplusplus >= 201402L
    #define ASTRA_CPP14 1
    #define ASTRA_CPP_VERSION 14
#elif __cplusplus >= 201103L
    #define ASTRA_CPP11 1
    #define ASTRA_CPP_VERSION 11
#else
    #error "Astra requires C++20 or later"
#endif

#if !defined(ASTRA_CPP20)
    #error "Astra requires C++20 or later"
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

// Cache line size (can be overridden)
#ifndef ASTRA_CACHE_LINE_SIZE
    #if defined(ASTRA_ARCH_X64) || defined(ASTRA_ARCH_X86)
        #define ASTRA_CACHE_LINE_SIZE 64
    #elif defined(ASTRA_ARCH_ARM64)
        #define ASTRA_CACHE_LINE_SIZE 64
    #elif defined(ASTRA_ARCH_ARM32)
        #define ASTRA_CACHE_LINE_SIZE 32
    #else
        #define ASTRA_CACHE_LINE_SIZE 64
    #endif
#endif

// Alignment helper
#define ASTRA_ALIGNAS(x) alignas(x)
#define ASTRA_CACHE_ALIGNED ASTRA_ALIGNAS(ASTRA_CACHE_LINE_SIZE)

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