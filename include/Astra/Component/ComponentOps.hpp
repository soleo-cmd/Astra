#pragma once

#include <cstddef>
#include <cstring>
#include <type_traits>
#include <utility>

#include "../Core/Base.hpp"
#include "Component.hpp"

namespace Astra::ComponentOps
{
    template<Component T, typename... Args>
    inline void Construct(void* ptr, Args&&... args)
    {
        new (ptr) T(std::forward<Args>(args)...);
    }

    static inline void MoveConstruct(const ComponentDescriptor& desc, void* dst, void* src)
    {
        if (desc.is_trivially_copyable) ASTRA_LIKELY
        {
            std::memcpy(dst, src, desc.size);
        }
        else
        {
            desc.moveConstruct(dst, src);
        }
    }

    static inline void DefaultConstruct(const ComponentDescriptor& desc, void* ptr)
    {
        if (desc.is_trivially_copyable && desc.is_nothrow_default_constructible) ASTRA_LIKELY
        {
#ifdef ASTRA_BUILD_DEBUG
            std::memset(ptr, 0, desc.size);
#endif
        // In release, skip initialization for true POD types
        }
        else
        {
            desc.defaultConstruct(ptr);
        }
    }

    static inline void BatchDefaultConstruct(const ComponentDescriptor& desc, void* ptr, size_t count)
    {
        if (desc.is_empty) ASTRA_UNLIKELY
        {
            return; // Nothing to do for empty types
        }

        if (desc.is_trivially_copyable && desc.is_nothrow_default_constructible) ASTRA_LIKELY
        {
            // For POD types, use memset for batch initialization
            // This is much faster than calling default constructor in a loop
            std::memset(ptr, 0, count * desc.size);
        }
        else
        {
            std::byte* p = static_cast<std::byte*>(ptr);
            for (size_t i = 0; i < count; ++i)
            {
                desc.defaultConstruct(p + i * desc.size);
            }
        }
    }

    static inline void Destruct(const ComponentDescriptor& desc, void* ptr)
    {
        desc.destruct(ptr);
    }
} // namespace Astra::ComponentOps