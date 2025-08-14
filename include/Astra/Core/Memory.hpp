#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

#include "../Core/Base.hpp"

#ifdef ASTRA_PLATFORM_WINDOWS
    #include <windows.h>
    #include <memoryapi.h>
#else
    #include <sys/mman.h>
    #include <unistd.h>
#endif

namespace Astra
{
    // Memory allocation flags
    enum class AllocFlags : uint32_t
    {
        None = 0,
        HugePages = 1 << 0,  // Use 2MB/1GB huge pages if available
        ZeroMem = 1 << 1,  // Zero-initialize allocated memory
    };
    
    // Combine allocation flags
    ASTRA_FORCEINLINE constexpr AllocFlags operator|(AllocFlags a, AllocFlags b) noexcept
    {
        return static_cast<AllocFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    
    ASTRA_FORCEINLINE constexpr bool operator&(AllocFlags a, AllocFlags b) noexcept
    {
        return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
    }
    
    // Memory allocation result
    struct AllocResult
    {
        void* ptr = nullptr;
        size_t size = 0;
        bool usedHugePages = false;
    };
    
    // Platform-specific huge page sizes
    #ifdef ASTRA_PLATFORM_WINDOWS
        constexpr size_t HUGE_PAGE_SIZE = 2 * 1024 * 1024;  // 2MB on Windows
    #else
        // Linux supports multiple huge page sizes, we'll try 2MB first
        constexpr size_t HUGE_PAGE_SIZE_2MB = 2 * 1024 * 1024;
        constexpr size_t HUGE_PAGE_SIZE_1GB = 1024 * 1024 * 1024;
        constexpr size_t HUGE_PAGE_SIZE = HUGE_PAGE_SIZE_2MB;
    #endif
    
    /**
     * Check if huge pages are available on the system
     */
    ASTRA_FORCEINLINE bool IsHugePagesAvailable() noexcept
    {
        static bool checked = false;
        static bool available = false;
        
        if (!checked)
        {
            #ifdef ASTRA_PLATFORM_WINDOWS
                // Check if we have the lock pages in memory privilege
                HANDLE token;
                if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
                {
                    LUID luid;
                    if (LookupPrivilegeValue(nullptr, SE_LOCK_MEMORY_NAME, &luid))
                    {
                        PRIVILEGE_SET privSet = {};
                        privSet.PrivilegeCount = 1;
                        privSet.Control = PRIVILEGE_SET_ALL_NECESSARY;
                        privSet.Privilege[0].Luid = luid;
                        privSet.Privilege[0].Attributes = SE_PRIVILEGE_ENABLED;
                        
                        BOOL result;
                        PrivilegeCheck(token, &privSet, &result);
                        available = (result == TRUE);
                    }
                    CloseHandle(token);
                }
            #else
                // On Linux, check if transparent huge pages are enabled
                FILE* f = fopen("/sys/kernel/mm/transparent_hugepage/enabled", "r");
                if (f)
                {
                    char buffer[256];
                    if (fgets(buffer, sizeof(buffer), f))
                    {
                        // Check if [always] or [madvise] is selected
                        available = (strstr(buffer, "[always]") != nullptr || 
                                   strstr(buffer, "[madvise]") != nullptr);
                    }
                    fclose(f);
                }
            #endif
            
            checked = true;
        }
        
        return available;
    }
    
    ASTRA_FORCEINLINE AllocResult AllocateMemory(size_t size, size_t alignment = 64, AllocFlags flags = AllocFlags::None) noexcept
    {
        AllocResult result;
        result.size = size;
        
        // Round size up to alignment
        size = (size + alignment - 1) & ~(alignment - 1);
        
        bool tryHugePages = (flags & AllocFlags::HugePages) && IsHugePagesAvailable();
        bool zeroMemory = (flags & AllocFlags::ZeroMem);
        
        #ifdef ASTRA_PLATFORM_WINDOWS
            if (tryHugePages && size >= HUGE_PAGE_SIZE)
            {
                // Round up to huge page size
                size_t hugePagesSize = (size + HUGE_PAGE_SIZE - 1) & ~(HUGE_PAGE_SIZE - 1);
                
                // Try to allocate with large pages
                void* ptr = VirtualAlloc(nullptr, hugePagesSize, MEM_COMMIT | MEM_RESERVE | MEM_LARGE_PAGES, PAGE_READWRITE);
                    
                if (ptr)
                {
                    result.ptr = ptr;
                    result.size = hugePagesSize;
                    result.usedHugePages = true;
                    return result;
                }
            }
            
            // Fall back to regular allocation
            void* ptr = VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (ptr)
            {
                result.ptr = ptr;
                result.size = size;
                
                if (zeroMemory)
                {
                    std::memset(ptr, 0, size);
                }
            }
        #else
            int prot = PROT_READ | PROT_WRITE;
            int flags_mmap = MAP_PRIVATE | MAP_ANONYMOUS;
            
            if (tryHugePages && size >= HUGE_PAGE_SIZE)
            {
                // Round up to huge page size
                size_t hugePagesSize = (size + HUGE_PAGE_SIZE - 1) & ~(HUGE_PAGE_SIZE - 1);
                
                // Try different huge page sizes
                #ifdef MAP_HUGE_2MB
                    void* ptr = mmap(nullptr, hugePagesSize, prot,
                        flags_mmap | MAP_HUGETLB | MAP_HUGE_2MB, -1, 0);
                    if (ptr != MAP_FAILED)
                    {
                        result.ptr = ptr;
                        result.size = hugePagesSize;
                        result.usedHugePages = true;
                        
                        // Advise kernel about huge pages if not already using them
                        #ifdef MADV_HUGEPAGE
                            madvise(ptr, hugePagesSize, MADV_HUGEPAGE);
                        #endif
                        
                        if (zeroMemory)
                        {
                            std::memset(ptr, 0, hugePagesSize);
                        }
                        return result;
                    }
                #endif
                
                // Try generic huge pages
                void* ptr = mmap(nullptr, hugePagesSize, prot,
                    flags_mmap | MAP_HUGETLB, -1, 0);
                if (ptr != MAP_FAILED)
                {
                    result.ptr = ptr;
                    result.size = hugePagesSize;
                    result.usedHugePages = true;
                    
                    if (zeroMemory)
                    {
                        std::memset(ptr, 0, hugePagesSize);
                    }
                    return result;
                }
            }
            
            // Fall back to regular allocation with alignment
            void* ptr = nullptr;
            if (alignment > sizeof(void*))
            {
                // Use aligned allocation
                if (posix_memalign(&ptr, alignment, size) == 0)
                {
                    result.ptr = ptr;
                    result.size = size;
                    
                    // Advise kernel about huge pages even for regular allocation
                    #ifdef MADV_HUGEPAGE
                        if (tryHugePages && size >= HUGE_PAGE_SIZE)
                        {
                            madvise(ptr, size, MADV_HUGEPAGE);
                        }
                    #endif
                    
                    if (zeroMemory)
                    {
                        std::memset(ptr, 0, size);
                    }
                }
            }
            else
            {
                // Regular allocation
                ptr = std::malloc(size);
                if (ptr)
                {
                    result.ptr = ptr;
                    result.size = size;
                    
                    if (zeroMemory)
                    {
                        std::memset(ptr, 0, size);
                    }
                }
            }
        #endif
        
        return result;
    }
    
    /**
     * Free memory allocated with AllocateMemory
     */
    ASTRA_FORCEINLINE void FreeMemory(void* ptr, size_t size, bool usedHugePages = false) noexcept
    {
        if (!ptr) return;
        
        #ifdef ASTRA_PLATFORM_WINDOWS
            (void)size;
            (void)usedHugePages;
            VirtualFree(ptr, 0, MEM_RELEASE);
        #else
            if (usedHugePages)
            {
                munmap(ptr, size);
            }
            else
            {
                std::free(ptr);
            }
        #endif
    }
    
    /**
     * Custom deleter for unique_ptr that tracks allocation info
     */
    struct MemoryDeleter
    {
        size_t size = 0;
        bool usedHugePages = false;
        
        void operator()(void* ptr) const noexcept
        {
            FreeMemory(ptr, size, usedHugePages);
        }
    };
    
    /**
     * Allocate aligned memory for components
     * Ensures proper cache line alignment to avoid false sharing
     */
    template<typename T>
    ASTRA_FORCEINLINE T* AllocateAligned(size_t count, size_t alignment = CACHE_LINE_SIZE) noexcept
    {
        size_t size = count * sizeof(T);
        
        // For large allocations, try huge pages
        AllocFlags flags = AllocFlags::None;
        if (size >= HUGE_PAGE_SIZE / 2)  // Use huge pages for allocations >= 1MB
        {
            flags = flags | AllocFlags::HugePages;
        }
        
        AllocResult result = AllocateMemory(size, alignment, flags);
        return static_cast<T*>(result.ptr);
    }
    
    /**
     * Free aligned memory
     */
    template<typename T>
    ASTRA_FORCEINLINE void FreeAligned(T* ptr, size_t count) noexcept
    {
        if (!ptr) return;
        
        size_t size = count * sizeof(T);
        bool maybeHugePages = (size >= HUGE_PAGE_SIZE / 2);
        FreeMemory(ptr, size, maybeHugePages);
    }
}