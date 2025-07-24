#pragma once

#include <cstdint>
#include <cstring>
#include "Platform.hpp"

#if defined(ASTRA_ARCH_X64) || defined(ASTRA_ARCH_X86)
    #if defined(ASTRA_COMPILER_MSVC)
        #include <intrin.h>
        #include <nmmintrin.h>  // SSE4.2
    #else
        #include <x86intrin.h>
    #endif

    #ifdef __SSE2__
        #define ASTRA_HAS_SSE2 1
    #endif
    #ifdef __SSE4_2__
        #define ASTRA_HAS_SSE42 1
    #endif
#elif defined(ASTRA_ARCH_ARM64) || defined(ASTRA_ARCH_ARM32)
    #include <arm_neon.h>
    #define ASTRA_HAS_NEON 1
    #if defined(__ARM_FEATURE_CRC32)
        #define ASTRA_HAS_ARM_CRC32 1
    #endif
#endif

namespace Astra::Simd
{
    // Core operations needed for FlatMap
    namespace Ops
    {
        // Match 16 bytes against a repeated byte value
        ASTRA_FORCEINLINE uint16_t MatchByteMask(const void* data, uint8_t value) noexcept
        {
#if defined(ASTRA_HAS_SSE2)
            const __m128i group = _mm_load_si128(static_cast<const __m128i*>(data));
            const __m128i match = _mm_set1_epi8(static_cast<char>(value));
            const __m128i eq = _mm_cmpeq_epi8(group, match);
            return static_cast<uint16_t>(_mm_movemask_epi8(eq));

#elif defined(ASTRA_HAS_NEON)
            const uint8x16_t group = vld1q_u8(static_cast<const uint8_t*>(data));
            const uint8x16_t match = vdupq_n_u8(value);
            const uint8x16_t eq = vceqq_u8(group, match);

            // Efficient movemask emulation for ARM NEON
            const uint8x16_t bit_mask = {
                0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
                0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80
            };
            const uint8x16_t masked = vandq_u8(eq, bit_mask);
            const uint8x8_t low = vget_low_u8(masked);
            const uint8x8_t high = vget_high_u8(masked);

            // Horizontal add to create mask
            uint8_t low_mask = vaddv_u8(low);
            uint8_t high_mask = vaddv_u8(high);
            return (static_cast<uint16_t>(high_mask) << 8) | low_mask;

#else
            // Scalar fallback
            uint16_t mask = 0;
            const uint8_t* bytes = static_cast<const uint8_t*>(data);
            for (int i = 0; i < 16; ++i)
            {
                if (bytes[i] == value)
                {
                    mask |= (1u << i);
                }
            }
            return mask;
#endif
        }

        // Match 16 bytes for empty or deleted (two values)
        ASTRA_FORCEINLINE uint16_t MatchEitherByteMask(const void* data, uint8_t val1, uint8_t val2) noexcept
        {
#if defined(ASTRA_HAS_SSE2)
            const __m128i group = _mm_load_si128(static_cast<const __m128i*>(data));
            const __m128i match1 = _mm_set1_epi8(static_cast<char>(val1));
            const __m128i match2 = _mm_set1_epi8(static_cast<char>(val2));
            const __m128i eq1 = _mm_cmpeq_epi8(group, match1);
            const __m128i eq2 = _mm_cmpeq_epi8(group, match2);
            const __m128i combined = _mm_or_si128(eq1, eq2);
            return static_cast<uint16_t>(_mm_movemask_epi8(combined));

#elif defined(ASTRA_HAS_NEON)
            const uint8x16_t group = vld1q_u8(static_cast<const uint8_t*>(data));
            const uint8x16_t match1 = vdupq_n_u8(val1);
            const uint8x16_t match2 = vdupq_n_u8(val2);
            const uint8x16_t eq1 = vceqq_u8(group, match1);
            const uint8x16_t eq2 = vceqq_u8(group, match2);
            const uint8x16_t combined = vorrq_u8(eq1, eq2);

            // Use same movemask emulation as above
            const uint8x16_t bit_mask = {
                0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
                0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80
            };
            const uint8x16_t masked = vandq_u8(combined, bit_mask);
            const uint8x8_t low = vget_low_u8(masked);
            const uint8x8_t high = vget_high_u8(masked);

            uint8_t low_mask = vaddv_u8(low);
            uint8_t high_mask = vaddv_u8(high);
            return (static_cast<uint16_t>(high_mask) << 8) | low_mask;

#else
            // Scalar fallback
            uint16_t mask = 0;
            const uint8_t* bytes = static_cast<const uint8_t*>(data);
            for (int i = 0; i < 16; ++i)
            {
                if (bytes[i] == val1 || bytes[i] == val2)
                {
                    mask |= (1u << i);
                }
            }
            return mask;
#endif
        }

        // Hardware CRC32 or high-quality fallback
        ASTRA_FORCEINLINE uint64_t HashCombine(uint64_t seed, uint64_t value) noexcept
        {
#if defined(ASTRA_HAS_SSE42)
            return _mm_crc32_u64(seed, value);

#elif defined(ASTRA_HAS_ARM_CRC32)
            return __crc32cd(static_cast<uint32_t>(seed), value);

#else
            // MurmurHash3 finalizer - high quality mixing
            uint64_t h = seed ^ value;
            h ^= h >> 33;
            h *= 0xff51afd7ed558ccdULL;
            h ^= h >> 33;
            h *= 0xc4ceb9fe1a85ec53ULL;
            h ^= h >> 33;
            return h;
#endif
        }

        // Count trailing zeros (for finding first set bit)
        ASTRA_FORCEINLINE int CountTrailingZeros(uint16_t mask) noexcept
        {
            if (!mask) return 16;
#if defined(ASTRA_COMPILER_MSVC)
            unsigned long idx;
            _BitScanForward(&idx, mask);
            return static_cast<int>(idx);
#elif ASTRA_HAS_BUILTIN(__builtin_ctz)
            return __builtin_ctz(static_cast<unsigned>(mask));
#else
            int count = 0;
            while ((mask & 1) == 0)
            {
                mask >>= 1;
                ++count;
            }
            return count;
#endif
        }

        // Prefetch hint levels
        enum class PrefetchHint
        {
            T0 = 0,  // Prefetch to all cache levels (use for data needed immediately)
            T1 = 1,  // Prefetch to L2 and higher (use for data needed soon)
            T2 = 2,  // Prefetch to L3 and higher (use for data needed later)
            NTA = 3  // Non-temporal (use for streaming data that won't be reused)
        };

        // Prefetch for read with locality hint
        ASTRA_FORCEINLINE void PrefetchRead(const void* ptr, PrefetchHint hint = PrefetchHint::T0) noexcept
        {
#if defined(ASTRA_ARCH_X64) || defined(ASTRA_ARCH_X86)
    #if defined(ASTRA_COMPILER_MSVC)
            switch (hint)
            {
                case PrefetchHint::T0:  _mm_prefetch(static_cast<const char*>(ptr), _MM_HINT_T0); break;
                case PrefetchHint::T1:  _mm_prefetch(static_cast<const char*>(ptr), _MM_HINT_T1); break;
                case PrefetchHint::T2:  _mm_prefetch(static_cast<const char*>(ptr), _MM_HINT_T2); break;
                case PrefetchHint::NTA: _mm_prefetch(static_cast<const char*>(ptr), _MM_HINT_NTA); break;
            }
    #else
            // GCC/Clang x86 intrinsics
            switch (hint)
            {
                case PrefetchHint::T0:  __builtin_prefetch(ptr, 0, 3); break;  // locality 3 = keep in all levels
                case PrefetchHint::T1:  __builtin_prefetch(ptr, 0, 2); break;  // locality 2 = L2 and up
                case PrefetchHint::T2:  __builtin_prefetch(ptr, 0, 1); break;  // locality 1 = L3 and up
                case PrefetchHint::NTA: __builtin_prefetch(ptr, 0, 0); break;  // locality 0 = no temporal locality
            }
    #endif
#elif ASTRA_HAS_BUILTIN(__builtin_prefetch)
            // Generic builtin prefetch (ARM, etc.)
            int locality = 3 - static_cast<int>(hint);  // Convert hint to locality
            __builtin_prefetch(ptr, 0, locality);
#elif defined(ASTRA_HAS_NEON) && defined(__ARM_FEATURE_UNALIGNED)
            // ARM doesn't have as fine-grained control as x86
            __pld(ptr);  // Same instruction for all hints on most ARM chips
#else
            // No prefetch available
            (void)ptr;
            (void)hint;
#endif
        }

        ASTRA_FORCEINLINE void PrefetchT0(const void* ptr) noexcept
        {
            PrefetchRead(ptr, PrefetchHint::T0);
        }

        ASTRA_FORCEINLINE void PrefetchT1(const void* ptr) noexcept
        {
            PrefetchRead(ptr, PrefetchHint::T1);
        }

        ASTRA_FORCEINLINE void PrefetchT2(const void* ptr) noexcept
        {
            PrefetchRead(ptr, PrefetchHint::T2);
        }

        ASTRA_FORCEINLINE void PrefetchNTA(const void* ptr) noexcept
        {
            PrefetchRead(ptr, PrefetchHint::NTA);
        }
    }
}