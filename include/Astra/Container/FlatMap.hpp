#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include "../Core/Config.hpp"
#include "../Core/Error.hpp"
#include "../Core/Result.hpp"
#include "../Core/Simd.hpp"
#include "../Entity/Entity.hpp"

namespace Astra
{    
    // Helper to select the best hash for a given key type
    template<typename Key>
    struct SelectHash
    {
        using type = std::hash<Key>;
    };
    
    // Specialize for Entity to use our optimized hash
    template<>
    struct SelectHash<Entity>
    {
        using type = EntityHash;
    };
    
    // FlatMap: A high-performance hash map with SwissTable-inspired design
    // - SIMD-accelerated metadata scanning
    // - Cache-friendly memory layout with prefetching
    // - Tombstone collapsing for better performance after deletions
    //
    // Thread Safety: This container is NOT thread-safe. Concurrent access
    // to non-const methods requires external synchronization.
    template<typename Key,
             typename Value,
             typename Hash = typename SelectHash<Key>::type,
             typename KeyEqual = std::equal_to<Key>,
             typename Allocator = std::allocator<std::pair<const Key, Value>>>
    class FlatMap
    {
    public:
        using KeyType = Key;
        using MappedType = Value;
        using ValueType = std::pair<const Key, Value>;
        using SizeType = std::size_t;
        using DifferenceType = std::ptrdiff_t;
        using Hasher = Hash;
        using KeyEqualType = KeyEqual;
        using AllocatorType = Allocator;
        using Reference = ValueType&;
        using ConstReference = const ValueType&;
        using Pointer = ValueType*;
        using ConstPointer = const ValueType*;

    private:
        static constexpr std::uint8_t EMPTY = 0b10000000;       // 0x80
        static constexpr std::uint8_t TOMBSTONE = 0b11111110;   // 0xFE

        static constexpr std::uint8_t H2_MASK = 0b01111111;     // 0x7F
        
        static constexpr SizeType GROUP_SIZE = 16;              // Group size for metadata
        static constexpr SizeType MIN_CAPACITY = 16;
        static constexpr float MAX_LOAD_FACTOR = 0.875f;
        
        // Helper to find first set bit (trailing zeros count)
        static inline int FindFirstSet(std::uint16_t mask) noexcept
        {
            if (mask == 0) return 16;
            return std::countr_zero(mask);  // C++20
        }
        
        // Extract H1 (57-bit position) and H2 (7-bit metadata) from hash
        static std::pair<std::size_t, std::uint8_t> SplitHash(std::size_t hash) noexcept
        {
            // H1: Use upper 57 bits for position
            std::size_t h1 = hash >> 7;
            // H2: Use lower 7 bits for metadata (ensuring it's never a control byte)
            std::uint8_t h2 = hash & H2_MASK;
            if (h2 == 0)
            {
                h2 = 1;  // Ensure valid H2
            }
            return {h1, h2};
        }
        
        struct Group
        {
            alignas(16) std::uint8_t metadata[GROUP_SIZE];
            
            Group() noexcept
            {
                std::memset(metadata, EMPTY, GROUP_SIZE);
            }
            
            std::uint16_t Match(std::uint8_t h2) const noexcept
            {
                ASTRA_ASSUME(h2 > 0 && h2 < 128);
                return Simd::Ops::MatchByteMask(metadata, h2);
            }
            
            std::uint16_t MatchEmpty() const noexcept
            {
                return Simd::Ops::MatchByteMask(metadata, EMPTY);
            }
            
            std::uint16_t MatchEmptyOrDeleted() const noexcept
            {
                return Simd::Ops::MatchEitherByteMask(metadata, EMPTY, TOMBSTONE);
            }
            
            std::uint16_t MatchOccupied() const noexcept
            {
                return ~Simd::Ops::MatchEitherByteMask(metadata, EMPTY, TOMBSTONE);
            }
            
            void Set(SizeType index, std::uint8_t value) noexcept
            {
                metadata[index] = value;
            }
            
            std::uint8_t Get(SizeType index) const noexcept
            {
                return metadata[index];
            }
            
            // Direct access to metadata for advanced SIMD operations
            [[nodiscard]] const std::uint8_t* Data() const noexcept
            {
                return metadata;
            }
            
            // Count occupied slots
            [[nodiscard]] int CountOccupied() const noexcept
            {
                return std::popcount(MatchOccupied());
            }
        };
        
        // Storage for key-value pairs
        struct Slot
        {
            alignas(ValueType) std::uint8_t storage[sizeof(ValueType)];
            
            ValueType* GetValue() noexcept
            {
                return std::launder(reinterpret_cast<ValueType*>(storage));
            }
            
            const ValueType* GetValue() const noexcept
            {
                return std::launder(reinterpret_cast<const ValueType*>(storage));
            }
        };
        
        using SlotAllocator = typename std::allocator_traits<Allocator>::template rebind_alloc<Slot>;
        using GroupAllocator = typename std::allocator_traits<Allocator>::template rebind_alloc<Group>;
        
        Group* m_groups = nullptr;
        Slot* m_slots = nullptr;
        SizeType m_capacity = 0;
        SizeType m_size = 0;
        SizeType m_numGroups = 0;
        SizeType m_tombstoneCount = 0;
        Hasher m_hasher;
        KeyEqualType m_equal;
        AllocatorType m_alloc;  // Keep the original allocator for ValueType
        SlotAllocator m_slotAlloc;
        GroupAllocator m_groupAlloc;
        
    public:
        class iterator
        {
            friend class FlatMap;
            friend class const_iterator;
            
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = ValueType;
            using difference_type = DifferenceType;
            using pointer = Pointer;
            using reference = Reference;
            
            iterator() = default;
            
            iterator(FlatMap* map, SizeType index) noexcept
                : m_map(map), m_index(index)
            {
                SkipEmpty();
            }
            
            reference operator*() const noexcept
            {
                ASTRA_ASSERT(m_index < m_map->m_capacity, "Iterator out of bounds");
                ASTRA_ASSERT(m_map->m_slots != nullptr, "Dereferencing invalid iterator");

#ifdef ASTRA_BUILD_DEBUG
                SizeType groupIdx = m_index / GROUP_SIZE;
                SizeType slotIdx = m_index % GROUP_SIZE;
                std::uint8_t meta = m_map->m_groups[groupIdx].Get(slotIdx);
                ASTRA_ASSERT(meta != EMPTY && meta != TOMBSTONE, "Iterator points to invalid slot");
#endif

                return *m_map->m_slots[m_index].GetValue();
            }
            
            pointer operator->() const noexcept
            {
                ASTRA_ASSERT(m_index < m_map->m_capacity, "Iterator out of bounds");
                ASTRA_ASSERT(m_map->m_slots != nullptr, "Dereferencing invalid iterator");
                return m_map->m_slots[m_index].GetValue();
            }
            
            iterator& operator++() noexcept
            {
                ++m_index;
                SkipEmpty();
                return *this;
            }
            
            iterator operator++(int) noexcept
            {
                iterator tmp = *this;
                ++(*this);
                return tmp;
            }
            
            [[nodiscard]] bool operator==(const iterator& other) const noexcept = default;
            
        private:
            FlatMap* m_map = nullptr;
            SizeType m_index = 0;
            
            void SkipEmpty() noexcept
            {
                if (!m_map) return;

                while (m_index < m_map->m_capacity)  // Just check capacity
                {
                    SizeType groupIdx = m_index / GROUP_SIZE;
                    SizeType slotIdx = m_index % GROUP_SIZE;

                    // Only check groups that exist
                    if (groupIdx < m_map->m_numGroups)
                    {
                        std::uint8_t meta = m_map->m_groups[groupIdx].Get(slotIdx);
                        // Only skip EMPTY and TOMBSTONE - no SENTINEL check needed
                        if (meta != EMPTY && meta != TOMBSTONE)
                        {
                            return;
                        }
                    }
                    ++m_index;
                }
            }
        };
        
        // const_iterator implementation similar to iterator...
        class const_iterator
        {
            friend class FlatMap;
            
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = ValueType;
            using difference_type = DifferenceType;
            using pointer = ConstPointer;
            using reference = ConstReference;
            
            const_iterator() = default;
            
            const_iterator(const FlatMap* map, SizeType index) noexcept
                : m_map(map), m_index(index)
            {
                SkipEmpty();
            }
            
            // Conversion from iterator to const_iterator
            const_iterator(const iterator& it) noexcept
                : m_map(it.m_map), m_index(it.m_index)
            {
            }
            
            reference operator*() const noexcept
            {
                ASTRA_ASSERT(m_index < m_map->m_capacity, "Iterator out of bounds");
                ASTRA_ASSERT(m_map->m_slots != nullptr, "Dereferencing invalid iterator");

#ifdef ASTRA_BUILD_DEBUG
                SizeType groupIdx = m_index / GROUP_SIZE;
                SizeType slotIdx = m_index % GROUP_SIZE;
                std::uint8_t meta = m_map->m_groups[groupIdx].Get(slotIdx);
                ASTRA_ASSERT(meta != EMPTY && meta != TOMBSTONE, "Iterator points to invalid slot");
#endif

                return *m_map->m_slots[m_index].GetValue();
            }
            
            pointer operator->() const noexcept
            {
                ASTRA_ASSERT(m_index < m_map->m_capacity, "Iterator out of bounds");
                ASTRA_ASSERT(m_map->m_slots != nullptr, "Dereferencing invalid iterator");
                return m_map->m_slots[m_index].GetValue();
            }
            
            const_iterator& operator++() noexcept
            {
                ++m_index;
                SkipEmpty();
                return *this;
            }
            
            const_iterator operator++(int) noexcept
            {
                const_iterator tmp = *this;
                ++(*this);
                return tmp;
            }
            
            [[nodiscard]] bool operator==(const const_iterator& other) const noexcept = default;
            
        private:
            const FlatMap* m_map = nullptr;
            SizeType m_index = 0;
            
            void SkipEmpty() noexcept
            {
                if (!m_map) return;
                
                while (m_index < m_map->m_capacity)
                {
                    SizeType groupIdx = m_index / GROUP_SIZE;
                    SizeType slotIdx = m_index % GROUP_SIZE;
                    
                    // Prefetch next group during iteration
                    if (slotIdx == 0 && groupIdx + 1 < m_map->m_numGroups)
                    {
                        Simd::Ops::PrefetchT0(&m_map->m_groups[groupIdx + 1]);
                    }
                    
                    if (groupIdx < m_map->m_numGroups)
                    {
                        std::uint8_t meta = m_map->m_groups[groupIdx].Get(slotIdx);
                        if (meta != EMPTY && meta != TOMBSTONE)
                        {
                            return;
                        }
                    }
                    ++m_index;
                }
            }
        };
        
        FlatMap() = default;
        
        explicit FlatMap(SizeType capacity,
                        const Hasher& hash = Hasher(),
                        const KeyEqualType& equal = KeyEqualType(),
                        const AllocatorType& alloc = AllocatorType())
            : m_hasher(hash)
            , m_equal(equal)
            , m_alloc(alloc)
            , m_slotAlloc(alloc)
            , m_groupAlloc(alloc)
        {
            if (capacity > 0)
            {
                Reserve(capacity);
            }
        }
        
        ~FlatMap() noexcept
        {
            Clear();
            DeallocateStorage();
        }
        
        FlatMap(const FlatMap& other) noexcept(std::is_nothrow_copy_constructible_v<Hasher> && std::is_nothrow_copy_constructible_v<KeyEqualType> && std::is_nothrow_copy_constructible_v<AllocatorType>)
            : m_hasher(other.m_hasher)
            , m_equal(other.m_equal)
            , m_alloc(other.m_alloc)
            , m_slotAlloc(other.m_slotAlloc)
            , m_groupAlloc(other.m_groupAlloc)
        {
            if (other.m_size == 0) return;

            const SizeType neededCapacity = [&]()
            {
                const SizeType minSize = static_cast<SizeType>(other.m_size / MAX_LOAD_FACTOR);
                if (minSize > std::numeric_limits<SizeType>::max() - GROUP_SIZE)
                {
                    return std::numeric_limits<SizeType>::max();
                }
                return minSize + GROUP_SIZE;
            }();
            Reserve(neededCapacity);

            // Copy elements group by group for cache efficiency
            for (SizeType groupIdx = 0; groupIdx < other.m_numGroups; ++groupIdx)
            {
                // Prefetch next group for better performance
                if (groupIdx + 1 < other.m_numGroups)
                {
                    Simd::Ops::PrefetchT0(&other.m_groups[groupIdx + 1]);
                    Simd::Ops::PrefetchT0(&other.m_slots[(groupIdx + 1) * GROUP_SIZE]);
                }

                // Process all occupied slots in this group at once
                std::uint16_t occupied = other.m_groups[groupIdx].MatchOccupied();

                while (occupied)
                {
                    SizeType slotIdx = FindFirstSet(occupied);
                    SizeType srcIdx = groupIdx * GROUP_SIZE + slotIdx;

                    std::uint8_t h2 = other.m_groups[groupIdx].Get(slotIdx);

                    if (srcIdx >= other.m_capacity)
                    {
                        occupied &= occupied - 1;
                        continue;
                    }

                    const auto& kv = *other.m_slots[srcIdx].GetValue();

                    // Use proper SwissTable group-based probing
                    auto [h1, _] = SplitHash(m_hasher(kv.first));
                    SizeType index = h1 & (m_capacity - 1);

                    // Find insertion position using group-based probing
                    while (true)
                    {
                        SizeType targetGroup = index / GROUP_SIZE;
                        SizeType startSlot = index % GROUP_SIZE;

                        // Get empty slots in this group
                        std::uint16_t emptySlots = m_groups[targetGroup].MatchEmpty();

                        // Phase 1: Check from start_slot to end
                        std::uint16_t phase1Empty = emptySlots >> startSlot;
                        if (phase1Empty)
                        {
                            SizeType offset = FindFirstSet(phase1Empty);
                            SizeType targetIdx = (targetGroup * GROUP_SIZE) + startSlot + offset;

                            std::allocator_traits<AllocatorType>::construct(
                                m_alloc,
                                m_slots[targetIdx].GetValue(),
                                kv
                            );
                            m_groups[targetGroup].Set(startSlot + offset, h2);
                            ++m_size;
                            break;
                        }

                        // Phase 2: Check from beginning to start_slot
                        if (startSlot > 0)
                        {
                            std::uint16_t phase2Mask = (1u << startSlot) - 1;
                            std::uint16_t phase2Empty = emptySlots & phase2Mask;

                            if (phase2Empty)
                            {
                                SizeType targetSlot = FindFirstSet(phase2Empty);
                                SizeType targetIdx = (targetGroup * GROUP_SIZE) + targetSlot;

                                std::allocator_traits<AllocatorType>::construct(
                                m_alloc,
                                m_slots[targetIdx].GetValue(),
                                kv
                            );
                                m_groups[targetGroup].Set(targetSlot, h2);
                                ++m_size;
                                break;
                            }
                        }

                        // Move to next group
                        targetGroup = (targetGroup + 1) % m_numGroups;
                        index = targetGroup * GROUP_SIZE;
                    }

                    // Clear this bit and continue
                    occupied &= occupied - 1;
                }
            }

            ASTRA_ASSERT(m_size == other.m_size, "Copy size mismatch");
        }
        
        FlatMap(FlatMap&& other) noexcept
            : m_groups(other.m_groups)
            , m_slots(other.m_slots)
            , m_capacity(other.m_capacity)
            , m_size(other.m_size)
            , m_tombstoneCount(other.m_tombstoneCount)
            , m_numGroups(other.m_numGroups)
            , m_hasher(std::move(other.m_hasher))
            , m_equal(std::move(other.m_equal))
            , m_alloc(std::move(other.m_alloc))
            , m_slotAlloc(std::move(other.m_slotAlloc))
            , m_groupAlloc(std::move(other.m_groupAlloc))
        {
            other.m_groups = nullptr;
            other.m_slots = nullptr;
            other.m_capacity = 0;
            other.m_size = 0;
            other.m_tombstoneCount = 0;
            other.m_numGroups = 0;
        }
        
        FlatMap& operator=(const FlatMap& other)
        {
            if (this != &other)
            {
                FlatMap tmp(other);
                Swap(tmp);
            }
            return *this;
        }
        
        FlatMap& operator=(FlatMap&& other) noexcept
        {
            if (this != &other)
            {
                Clear();
                DeallocateStorage();
                
                m_groups = other.m_groups;
                m_slots = other.m_slots;
                m_capacity = other.m_capacity;
                m_size = other.m_size;
                m_tombstoneCount = other.m_tombstoneCount;
                m_numGroups = other.m_numGroups;
                m_hasher = std::move(other.m_hasher);
                m_equal = std::move(other.m_equal);
                m_alloc = std::move(other.m_alloc);
                m_slotAlloc = std::move(other.m_slotAlloc);
                m_groupAlloc = std::move(other.m_groupAlloc);
                
                other.m_groups = nullptr;
                other.m_slots = nullptr;
                other.m_capacity = 0;
                other.m_size = 0;
                other.m_tombstoneCount = 0;
                other.m_numGroups = 0;
            }
            return *this;
        }
        
        [[nodiscard]] iterator begin() noexcept { return iterator(this, 0); }
        [[nodiscard]] const_iterator begin() const noexcept { return const_iterator(this, 0); }
        [[nodiscard]] iterator end() noexcept { return iterator(this, m_capacity); }
        [[nodiscard]] const_iterator end() const noexcept { return const_iterator(this, m_capacity); }
        
        [[nodiscard]] bool Empty() const noexcept { return m_size == 0; }
        [[nodiscard]] SizeType Size() const noexcept { return m_size; }
        [[nodiscard]] SizeType Capacity() const noexcept { return m_capacity; }
        
        void Clear() noexcept
        {
            if (!m_slots) return;
            
            for (SizeType i = 0; i < m_capacity; ++i)
            {
                SizeType groupIdx = i / GROUP_SIZE;
                SizeType slotIdx = i % GROUP_SIZE;
                
                std::uint8_t meta = m_groups[groupIdx].Get(slotIdx);
                if (meta != EMPTY && meta != TOMBSTONE)
                {
                    std::allocator_traits<AllocatorType>::destroy(m_alloc, m_slots[i].GetValue());
                    m_groups[groupIdx].Set(slotIdx, EMPTY);
                }
            }
            m_size = 0;
            m_tombstoneCount = 0;
        }
        
    private:
        template<typename K>
        SizeType FindImpl(const K& key, std::uint8_t& h2Out) const noexcept
        {
            if (Empty() || !m_groups) return m_capacity;

            auto [h1, h2] = SplitHash(m_hasher(key));
            h2Out = h2;
            SizeType index = h1 & (m_capacity - 1);
            SizeType probes = 0;

            while (probes++ < m_capacity)
            {
                SizeType groupIdx = index / GROUP_SIZE;
                SizeType startSlot = index % GROUP_SIZE;

                // IMPROVEMENT 1: Prefetch NEXT group's metadata early
                SizeType nextGroupIdx = (groupIdx + 1) % m_numGroups;
                Simd::Ops::PrefetchT1(&m_groups[nextGroupIdx]);

                // Get all matches in this group
                std::uint16_t matches = m_groups[groupIdx].Match(h2);

                // IMPROVEMENT 2: Prefetch slots if we have matches
                if (matches)
                {
                    // Prefetch the first potential match slot
                    SizeType firstMatchIdx = FindFirstSet(matches);
                    SizeType prefetchIdx = groupIdx * GROUP_SIZE + firstMatchIdx;
                    Simd::Ops::PrefetchT0(&m_slots[prefetchIdx]);

                    // IMPROVEMENT 3: If multiple matches, prefetch up to 2 more slots
                    std::uint16_t remainingMatches = matches & (matches - 1); // Clear first bit
                    if (remainingMatches)
                    {
                        SizeType secondMatchIdx = FindFirstSet(remainingMatches);
                        prefetchIdx = groupIdx * GROUP_SIZE + secondMatchIdx;
                        Simd::Ops::PrefetchT1(&m_slots[prefetchIdx]);
                    }
                }

                // Phase 1: Check from start_slot to end of group
                std::uint16_t phase1Matches = matches >> startSlot;

                while (phase1Matches)
                {
                    SizeType offset = FindFirstSet(phase1Matches);
                    SizeType slotIdx = startSlot + offset;
                    SizeType globalIdx = groupIdx * GROUP_SIZE + slotIdx;

                    // IMPROVEMENT 4: Prefetch next match while processing current
                    std::uint16_t nextMatches = phase1Matches & (phase1Matches - 1);
                    if (nextMatches)
                    {
                        SizeType nextOffset = FindFirstSet(nextMatches);
                        SizeType nextIdx = groupIdx * GROUP_SIZE + startSlot + nextOffset;
                        Simd::Ops::PrefetchT0(&m_slots[nextIdx]);
                    }

                    if (m_equal(m_slots[globalIdx].GetValue()->first, key))
                    {
                        return globalIdx;
                    }

                    phase1Matches = nextMatches; // Use already computed value
                }

                // Phase 2: Check from beginning of group to start_slot
                if (startSlot > 0)
                {
                    std::uint16_t phase2Mask = (1u << startSlot) - 1;
                    std::uint16_t phase2Matches = matches & phase2Mask;

                    while (phase2Matches)
                    {
                        SizeType slotIdx = FindFirstSet(phase2Matches);
                        SizeType globalIdx = groupIdx * GROUP_SIZE + slotIdx;

                        // Similar prefetch for phase 2
                        std::uint16_t nextMatches = phase2Matches & (phase2Matches - 1);
                        if (nextMatches)
                        {
                            SizeType nextIdx = groupIdx * GROUP_SIZE + FindFirstSet(nextMatches);
                            Simd::Ops::PrefetchT0(&m_slots[nextIdx]);
                        }

                        if (m_equal(m_slots[globalIdx].GetValue()->first, key))
                        {
                            return globalIdx;
                        }

                        phase2Matches = nextMatches;
                    }
                }

                // IMPROVEMENT 5: Combine empty checks to reduce SIMD operations
                std::uint16_t emptyMatches = m_groups[groupIdx].MatchEmpty();

                // Check both phases at once
                std::uint16_t relevantEmpty = (startSlot > 0) 
                    ? emptyMatches  // Need to check all slots
                    : (emptyMatches >> startSlot);  // Only check from start_slot

                if (relevantEmpty != 0)
                {
                    return m_capacity; // Empty slot found, key doesn't exist
                }

                // Move to the next group
                groupIdx = nextGroupIdx; // Reuse computed value
                index = groupIdx * GROUP_SIZE;

                // Prefetch already done at start of loop - removed duplicate
            }

            return m_capacity;
        }
        
    public:
        template<typename K>
        iterator Find(const K& key) noexcept
        {
            std::uint8_t h2;
            SizeType idx = FindImpl(key, h2);
            return idx < m_capacity ? iterator(this, idx) : end();
        }
        
        template<typename K>
        const_iterator Find(const K& key) const noexcept
        {
            std::uint8_t h2;
            SizeType idx = FindImpl(key, h2);
            return idx < m_capacity ? const_iterator(this, idx) : end();
        }
        
        template<typename K>
        [[nodiscard]] bool Contains(const K& key) const noexcept
        {
            return Find(key) != end();
        }
        
        template<typename K, typename... Args>
        std::pair<iterator, bool> Emplace(K&& key, Args&&... args)
        {
            ReserveForInsert();
            ASTRA_ASSERT(m_size + m_tombstoneCount < m_capacity, "Table should have space after reserve");

            auto [h1, h2] = SplitHash(m_hasher(key));
            SizeType index = h1 & (m_capacity - 1);
            SizeType probes = 0;

            while (probes++ < m_capacity)
            {
                SizeType groupIdx = index / GROUP_SIZE;
                SizeType startSlot = index % GROUP_SIZE;

                // First, check if key already exists
                std::uint16_t matches = m_groups[groupIdx].Match(h2);

                // Phase 1: Check existing keys from start_slot to end of group
                std::uint16_t phase1Matches = matches >> startSlot;

                while (phase1Matches)
                {
                    SizeType offset = FindFirstSet(phase1Matches);
                    SizeType slotIdx = startSlot + offset;
                    SizeType globalIdx = groupIdx * GROUP_SIZE + slotIdx;

                    if (m_equal(m_slots[globalIdx].GetValue()->first, key))
                    {
                        return {iterator(this, globalIdx), false}; // Key already exists
                    }

                    phase1Matches &= phase1Matches - 1;
                }

                // Phase 2: Check existing keys from beginning to start_slot
                if (startSlot > 0)
                {
                    std::uint16_t phase2Mask = (1u << startSlot) - 1;
                    std::uint16_t phase2Matches = matches & phase2Mask;

                    while (phase2Matches)
                    {
                        SizeType slotIdx = FindFirstSet(phase2Matches);
                        SizeType globalIdx = groupIdx * GROUP_SIZE + slotIdx;

                        if (m_equal(m_slots[globalIdx].GetValue()->first, key))
                        {
                            return {iterator(this, globalIdx), false}; // Key already exists
                        }

                        phase2Matches &= phase2Matches - 1;
                    }
                }

                // Now look for empty or deleted slots to insert
                std::uint16_t emptyOrDeleted = m_groups[groupIdx].MatchEmptyOrDeleted();

                // Phase 1: Check for empty/deleted slots from start_slot to end
                std::uint16_t phase1Empty = emptyOrDeleted >> startSlot;

                if (phase1Empty)
                {
                    SizeType offset = FindFirstSet(phase1Empty);
                    SizeType slotIdx = startSlot + offset;
                    SizeType globalIdx = groupIdx * GROUP_SIZE + slotIdx;

                    // Construct the key-value pair in place
                    std::allocator_traits<AllocatorType>::construct(
                        m_alloc,
                        m_slots[globalIdx].GetValue(),
                        std::piecewise_construct,
                        std::forward_as_tuple(std::forward<K>(key)),
                        std::forward_as_tuple(std::forward<Args>(args)...)
                    );

                    m_groups[groupIdx].Set(slotIdx, h2);
                    ++m_size;

                    return {iterator(this, globalIdx), true};
                }

                // Phase 2: Check for empty/deleted slots from beginning to start_slot
                if (startSlot > 0)
                {
                    std::uint16_t phase2Mask = (1u << startSlot) - 1;
                    std::uint16_t phase2Empty = emptyOrDeleted & phase2Mask;

                    if (phase2Empty)
                    {
                        SizeType slotIdx = FindFirstSet(phase2Empty);
                        SizeType globalIdx = groupIdx * GROUP_SIZE + slotIdx;

                        // Construct the key-value pair in place
                        std::allocator_traits<AllocatorType>::construct(
                            m_alloc,
                            m_slots[globalIdx].GetValue(),
                            std::piecewise_construct,
                            std::forward_as_tuple(std::forward<K>(key)),
                            std::forward_as_tuple(std::forward<Args>(args)...)
                        );

                        m_groups[groupIdx].Set(slotIdx, h2);
                        ++m_size;

                        return {iterator(this, globalIdx), true};
                    }
                }

                // No space in this group, move to next
                groupIdx = (groupIdx + 1) % m_numGroups;
                index = groupIdx * GROUP_SIZE;

                // Prefetch next group
                if (groupIdx < m_numGroups)
                {
                    Simd::Ops::PrefetchT0(&m_groups[groupIdx]);
                }
            }

            // Should never reach here if ReserveForInsert() works correctly
            ASTRA_ASSERT(false, "FlatMap::Emplace failed to find insertion slot");
            return {end(), false};
        }
        
        std::pair<iterator, bool> Insert(const ValueType& value)
        {
            return Emplace(value.first, value.second);
        }
        
        std::pair<iterator, bool> Insert(ValueType&& value)
        {
            return Emplace(std::move(value.first), std::move(value.second));
        }
        
        template<typename K>
        MappedType& operator[](K&& key)
        {
            return Emplace(std::forward<K>(key)).first->second;
        }
        
        template<typename K>
        SizeType Erase(const K& key)
        {
            auto it = Find(key);
            if (it == end()) return 0;
            
            Erase(it);
            return 1;
        }
        
        iterator Erase(iterator pos)
        {
            SizeType idx = pos.m_index;
            SizeType groupIdx = idx / GROUP_SIZE;
            SizeType slotIdx = idx % GROUP_SIZE;
            
            std::allocator_traits<AllocatorType>::destroy(m_alloc, m_slots[idx].GetValue());
            m_groups[groupIdx].Set(slotIdx, TOMBSTONE);
            --m_size;
            ++m_tombstoneCount;  // Track tombstones

            // CollapseTombstones(groupIdx, slot_idx);
            
            return ++pos;
        }
        
        void Reserve(SizeType count)
        {
            if (count > m_capacity)
            {
                SizeType newCapacity = NextPowerOfTwo(std::max(count, MIN_CAPACITY));
                Rehash(newCapacity);
            }
        }
        
        void Swap(FlatMap& other) noexcept
        {
            std::swap(m_groups, other.m_groups);
            std::swap(m_slots, other.m_slots);
            std::swap(m_capacity, other.m_capacity);
            std::swap(m_size, other.m_size);
            std::swap(m_tombstoneCount, other.m_tombstoneCount);
            std::swap(m_numGroups, other.m_numGroups);
            std::swap(m_hasher, other.m_hasher);
            std::swap(m_equal, other.m_equal);
            std::swap(m_alloc, other.m_alloc);
            std::swap(m_slotAlloc, other.m_slotAlloc);
            std::swap(m_groupAlloc, other.m_groupAlloc);
        }
        
        // TryGet methods for consistency with other containers
        template<typename K>
        [[nodiscard]] MappedType* TryGet(const K& key) noexcept
        {
            auto it = Find(key);
            return it != end() ? &it->second : nullptr;
        }
        
        template<typename K>
        [[nodiscard]] const MappedType* TryGet(const K& key) const noexcept
        {
            auto it = Find(key);
            return it != end() ? &it->second : nullptr;
        }
        
        // Batch operations for ECS workloads
        template<typename K, size_t N>
        void FindBatch(const K (&keys)[N], iterator (&results)[N]) noexcept
        {
            // Prefetch all hash computations
            std::uint8_t h2Values[N];
            SizeType indices[N];
            
            for (size_t i = 0; i < N; ++i)
            {
                auto [h1, h2] = SplitHash(m_hasher(keys[i]));
                h2Values[i] = h2;
                indices[i] = h1 & (m_capacity - 1);
                
                // Prefetch the group we'll need
                SizeType groupIdx = indices[i] / GROUP_SIZE;
                if (groupIdx < m_numGroups)
                {
                    Simd::Ops::PrefetchT0(&m_groups[groupIdx]);
                }
            }
            
            // Now perform lookups with data in cache
            for (size_t i = 0; i < N; ++i)
            {
                SizeType idx = FindImpl(keys[i], h2Values[i]);
                results[i] = idx < m_capacity ? iterator(this, idx) : end();
            }
        }
        
        // Group-based iteration for SIMD processing
        template<bool IsConst>
        struct GroupViewBase
        {
            using SlotPtr = std::conditional_t<IsConst, const Slot*, Slot*>;
            using ValueRef = std::conditional_t<IsConst, const ValueType&, ValueType&>;
            using ValuePtr = std::conditional_t<IsConst, const ValueType*, ValueType*>;
            
            const Group* group;
            SlotPtr slots;
            SizeType groupIndex;
            std::uint16_t occupiedMask;
            
            [[nodiscard]] bool HasNext() const noexcept
            {
                return occupiedMask != 0;
            }
            
            [[nodiscard]] SizeType NextIndex() noexcept
            {
                ASTRA_ASSERT(HasNext(), "No more elements in group");
                int slotIdx = Simd::Ops::CountTrailingZeros(occupiedMask);
                occupiedMask &= (occupiedMask - 1); // Clear lowest bit
                return groupIndex * GROUP_SIZE + slotIdx;
            }
            
            [[nodiscard]] ValueRef NextValue() noexcept
            {
                SizeType idx = NextIndex();
                return *slots[idx % GROUP_SIZE].GetValue();
            }
            
            // Process all occupied slots with a function
            template<typename Func>
            void ForEach(Func&& func)
            {
                while (HasNext())
                {
                    func(NextValue());
                }
            }
            
            // Get pointers to all occupied values for vectorized processing
            SizeType GetValuePointers(ValuePtr* ptrs, SizeType max_count) const noexcept
            {
                SizeType count = 0;
                std::uint16_t mask = occupiedMask;
                while (mask != 0 && count < max_count)
                {
                    int slotIdx = Simd::Ops::CountTrailingZeros(mask);
                    ptrs[count++] = slots[slotIdx].GetValue();
                    mask &= (mask - 1);
                }
                return count;
            }
        };
        
        using GroupView = GroupViewBase<false>;
        using ConstGroupView = GroupViewBase<true>;
        
        // Iterator over groups for SIMD-friendly processing
        class GroupIterator
        {
        public:
            GroupIterator(FlatMap* map, SizeType groupIdx) noexcept
                : m_map(map), m_groupIdx(groupIdx) {}
            
            [[nodiscard]] GroupView operator*() const noexcept
            {
                ASTRA_ASSERT(m_groupIdx < m_map->m_numGroups, "Group index out of bounds");
                return GroupView{
                    &m_map->m_groups[m_groupIdx],
                    &m_map->m_slots[m_groupIdx * GROUP_SIZE],
                    m_groupIdx,
                    m_map->m_groups[m_groupIdx].MatchOccupied()
                };
            }
            
            GroupIterator& operator++() noexcept
            {
                ++m_groupIdx;
                return *this;
            }
            
            GroupIterator operator++(int) noexcept
            {
                GroupIterator tmp = *this;
                ++m_groupIdx;
                return tmp;
            }
            
            [[nodiscard]] bool operator==(const GroupIterator& other) const noexcept = default;
            [[nodiscard]] bool operator!=(const GroupIterator& other) const noexcept = default;
            
        private:
            FlatMap* m_map;
            SizeType m_groupIdx;
        };
        
        [[nodiscard]] GroupIterator GroupBegin() noexcept
        {
            return GroupIterator(this, 0);
        }
        
        [[nodiscard]] GroupIterator GroupEnd() noexcept
        {
            return GroupIterator(this, m_numGroups);
        }
        
        // Apply function to all elements in groups of 16
        template<typename Func>
        void ForEachGroup(Func&& func)
        {
            for (SizeType groupIdx = 0; groupIdx < m_numGroups; ++groupIdx)
            {
                std::uint16_t occupied = m_groups[groupIdx].MatchOccupied();
                if (occupied == 0) continue;
                
                // Prefetch next group
                if (groupIdx + 1 < m_numGroups)
                {
                    Simd::Ops::PrefetchT0(&m_groups[groupIdx + 1]);
                }
                
                GroupView view{
                    &m_groups[groupIdx],
                    &m_slots[groupIdx * GROUP_SIZE],
                    groupIdx,
                    occupied
                };
                
                func(view);
            }
        }
        
        // Get statistics about group occupancy for optimization
        struct GroupStats
        {
            SizeType totalGroups;
            SizeType emptyGroups;
            SizeType fullGroups;
            SizeType partialGroups;
            float averageOccupancy;
        };
        
        [[nodiscard]] GroupStats GetGroupStats() const noexcept
        {
            GroupStats stats{m_numGroups, 0, 0, 0, 0.0f};
            SizeType totalOccupied = 0;
            
            for (SizeType i = 0; i < m_numGroups; ++i)
            {
                std::uint16_t occupied = m_groups[i].MatchOccupied();
                int count = std::popcount(occupied);
                
                if (count == 0)
                    ++stats.emptyGroups;
                else if (count == GROUP_SIZE)
                    ++stats.fullGroups;
                else
                    ++stats.partialGroups;
                    
                totalOccupied += count;
            }
            
            stats.averageOccupancy = m_numGroups > 0 ? 
                static_cast<float>(totalOccupied) / (m_numGroups * GROUP_SIZE) : 0.0f;
                
            return stats;
        }
        
    private:
        void AllocateStorage(SizeType capacity)
        {
            m_capacity = capacity;
            m_numGroups = (capacity + GROUP_SIZE - 1) / GROUP_SIZE;
            
            m_groups = m_groupAlloc.allocate(m_numGroups);
            m_slots = m_slotAlloc.allocate(capacity);
            
            // Initialize groups
            for (SizeType i = 0; i < m_numGroups; ++i)
            {
                std::allocator_traits<GroupAllocator>::construct(m_groupAlloc, &m_groups[i]);
            }
        }
        
        void DeallocateStorage() noexcept
        {
            if (m_groups)
            {
                // Destroy all Group objects before deallocating
                for (SizeType i = 0; i < m_numGroups; ++i)
                {
                    std::allocator_traits<GroupAllocator>::destroy(m_groupAlloc, &m_groups[i]);
                }
                m_groupAlloc.deallocate(m_groups, m_numGroups);
                m_groups = nullptr;
            }
            if (m_slots)
            {
                m_slotAlloc.deallocate(m_slots, m_capacity);
                m_slots = nullptr;
            }
            m_capacity = 0;
            m_numGroups = 0;
        }
        
        void ReserveForInsert()
        {
            if (m_capacity == 0)
            {
                Rehash(MIN_CAPACITY);
            }
            else if (m_size + m_tombstoneCount + 1 > m_capacity * MAX_LOAD_FACTOR)
            {
                Rehash(m_capacity * 2);
            }
            else if (m_tombstoneCount > m_capacity * 0.25f)
            {
                // Trigger rehash when tombstones exceed 25%
                // Same size, clean tombstones
                Rehash(m_capacity);
            }
        }
        
        void Rehash(SizeType newCapacity)
        {
            auto oldGroups = m_groups;
            auto oldSlots = m_slots;
            auto oldCapacity = m_capacity;
            auto oldNumGroups = m_numGroups;

            m_groups = nullptr;
            m_slots = nullptr;
            m_size = 0;
            m_tombstoneCount = 0;

            AllocateStorage(newCapacity);

            for (SizeType i = 0; i < oldCapacity; ++i)
            {
                SizeType groupIdx = i / GROUP_SIZE;
                SizeType slotIdx = i % GROUP_SIZE;

                std::uint8_t meta = oldGroups[groupIdx].Get(slotIdx);
                if (meta != EMPTY && meta != TOMBSTONE)
                {
                    // Move the entire pair at once
                    ValueType* oldPair = oldSlots[i].GetValue();

                    // Use placement new to move-construct directly
                    // This avoids the const issue entirely
                    Emplace(std::move(oldPair->first), std::move(oldPair->second));

                    // Now destroy the old pair
                    std::allocator_traits<AllocatorType>::destroy(m_alloc, oldPair);
                }
            }

            // Destroy old groups before deallocating
            for (SizeType i = 0; i < oldNumGroups; ++i)
            {
                std::allocator_traits<GroupAllocator>::destroy(m_groupAlloc, &oldGroups[i]);
            }
            m_groupAlloc.deallocate(oldGroups, oldNumGroups);
            m_slotAlloc.deallocate(oldSlots, oldCapacity);
        }
        
        static constexpr SizeType NextPowerOfTwo(SizeType value) noexcept
        {
            static_assert(std::is_unsigned_v<SizeType>, "SizeType must be an unsigned integer type");
            if (value <= 1)
            {
                return 1;
            }
            if (value > (std::numeric_limits<SizeType>::max() >> 1) + 1)
            {
                return std::numeric_limits<SizeType>::max();
            }
            return std::bit_ceil(value);
        }
    };
}
