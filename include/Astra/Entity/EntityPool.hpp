#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

#include "Entity.hpp"
#include "../Core/Config.hpp"
#include "../Core/Error.hpp"

namespace Astra
{
    /**
     * EntityPool manages the lifecycle of entities in the ECS.
     * 
     * Features:
     * - O(1) entity creation and destruction
     * - Automatic entity recycling with version incrementation
     * - Cache-friendly storage with no holes
     * - Follows EnTT patterns for maximum performance
     * 
     * The pool uses a free list (LIFO) for recycling destroyed entities.
     * Entity versions are stored in a dense array indexed by entity ID.
     * Version 0 is reserved for null/invalid entities.
     */
    class EntityPool
    {
    public:
        using SizeType = std::size_t;
        using VersionType = std::uint8_t;
        
        static constexpr VersionType NULL_VERSION = 0;
        static constexpr VersionType INITIAL_VERSION = 1;
        static constexpr VersionType TOMBSTONE_VERSION = std::numeric_limits<VersionType>::max(); // 255
        static constexpr EntityID INVALID_ID = EntityTraits32::ENTITY_MASK;
        
    private:
        // Free list entry: stores entity ID and its next version
        struct FreeListEntry
        {
            EntityID id;
            VersionType nextVersion;
        };
        
        // Dense array of versions indexed by entity ID
        std::vector<VersionType> m_versions;
        
        // Stack of recycled entities (LIFO for cache locality)
        std::vector<FreeListEntry> m_freeList;
        
        // Next fresh entity ID to allocate
        EntityID m_nextId = 0;
        
        // Number of alive entities
        SizeType m_aliveCount = 0;
        
    public:
        /**
         * Default constructor
         */
        EntityPool() = default;
        
        /**
         * Constructor with capacity reservation
         * @param capacity Initial capacity to reserve
         */
        explicit EntityPool(SizeType capacity)
        {
            Reserve(capacity);
        }
        
        /**
         * Creates a new entity or recycles a destroyed one
         * @return Valid entity with unique version
         */
        [[nodiscard]] Entity Create() noexcept
        {
            EntityID id;
            VersionType version;
            
            if (!m_freeList.empty())
            {
                // Recycle from free list (LIFO)
                const auto entry = m_freeList.back();
                m_freeList.pop_back();
                
                id = entry.id;
                version = entry.nextVersion;
                
                ASTRA_ASSERT(id < m_versions.size(), "Invalid ID in free list");
                ASTRA_ASSERT(version != NULL_VERSION, "Attempting to use null version");
                
                // Update version in dense array
                m_versions[id] = version;
            }
            else
            {
                // Allocate fresh entity
                id = m_nextId++;
                version = INITIAL_VERSION;
                
                ASTRA_ASSERT(id <= EntityTraits32::ENTITY_MASK, "Entity ID overflow");
                
                // Grow version array if needed
                if (id >= m_versions.size())
                {
                    m_versions.resize(id + 1, NULL_VERSION);
                }
                
                m_versions[id] = version;
            }
            
            ++m_aliveCount;
            return Entity(id, version);
        }
        
        /**
         * Creates multiple entities in batch for better performance
         * @param count Number of entities to create
         * @param out Output iterator for created entities
         */
        template<typename OutputIt>
        void CreateBatch(SizeType count, OutputIt out) noexcept
        {
            // Reserve space if needed
            const SizeType requiredCapacity = m_aliveCount + count;
            if (requiredCapacity > Capacity())
            {
                Reserve(requiredCapacity);
            }
            
            for (SizeType i = 0; i < count; ++i)
            {
                *out++ = Create();
            }
        }
        
        /**
         * Destroys an entity, making it available for recycling
         * @param entity Entity to destroy
         * @return true if entity was valid and destroyed, false otherwise
         */
        bool Destroy(Entity entity) noexcept
        {
            if (!IsValid(entity))
            {
                return false;
            }
            
            const EntityID id = entity.Index();
            const VersionType currentVersion = entity.Version();
            
            ASTRA_ASSERT(id < m_versions.size(), "Entity ID out of range");
            ASTRA_ASSERT(m_versions[id] == currentVersion, "Version mismatch in destroy");
            
            // Calculate next version with wraparound
            VersionType nextVersion = currentVersion + 1;
            if (nextVersion == NULL_VERSION)
            {
                nextVersion = INITIAL_VERSION; // Skip null version
            }
            // Also skip tombstone version when wrapping
            if (nextVersion == TOMBSTONE_VERSION)
            {
                nextVersion = INITIAL_VERSION;
            }
            
            // Mark as destroyed with tombstone marker
            m_versions[id] = TOMBSTONE_VERSION;
            
            // Add to free list for recycling with the next version
            m_freeList.push_back({id, nextVersion});
            
            --m_aliveCount;
            return true;
        }
        
        /**
         * Destroys multiple entities in batch
         * @param first Iterator to first entity
         * @param last Iterator past last entity
         * @return Number of entities successfully destroyed
         */
        template<typename InputIt>
        SizeType DestroyBatch(InputIt first, InputIt last) noexcept
        {
            SizeType destroyed = 0;
            
            // Reserve space in free list
            const SizeType count = std::distance(first, last);
            m_freeList.reserve(m_freeList.size() + count);
            
            for (auto it = first; it != last; ++it)
            {
                if (Destroy(*it))
                {
                    ++destroyed;
                }
            }
            
            return destroyed;
        }
        
        /**
         * Checks if an entity is valid (alive)
         * @param entity Entity to check
         * @return true if entity exists and version matches
         */
        [[nodiscard]] bool IsValid(Entity entity) const noexcept
        {
            const EntityID id = entity.Index();
            const VersionType version = entity.Version();
            
            // Check bounds and version match, excluding null and tombstone
            return id < m_versions.size() && 
                   m_versions[id] == version && 
                   version != NULL_VERSION &&
                   version != TOMBSTONE_VERSION;
        }
        
        /**
         * Gets the current version of an entity ID
         * @param id Entity ID to query
         * @return Current version or NULL_VERSION if invalid
         */
        [[nodiscard]] VersionType GetVersion(EntityID id) const noexcept
        {
            return id < m_versions.size() ? m_versions[id] : NULL_VERSION;
        }
        
        /**
         * Clears all entities and resets the pool
         */
        void Clear() noexcept
        {
            m_versions.clear();
            m_freeList.clear();
            m_nextId = 0;
            m_aliveCount = 0;
        }
        
        /**
         * Reserves capacity for entities
         * @param capacity Number of entities to reserve space for
         */
        void Reserve(SizeType capacity)
        {
            m_versions.reserve(capacity);
            // Reserve some space in free list (assume ~25% recycling)
            m_freeList.reserve(capacity / 4);
        }
        
        /**
         * Returns the number of alive entities
         */
        [[nodiscard]] SizeType Size() const noexcept
        {
            return m_aliveCount;
        }
        
        /**
         * Returns the total capacity (highest ID + 1)
         */
        [[nodiscard]] SizeType Capacity() const noexcept
        {
            return m_versions.size();
        }
        
        /**
         * Returns the number of recycled entities in the free list
         */
        [[nodiscard]] SizeType RecycledCount() const noexcept
        {
            return m_freeList.size();
        }
        
        /**
         * Checks if the pool is empty
         */
        [[nodiscard]] bool Empty() const noexcept
        {
            return m_aliveCount == 0;
        }
        
        /**
         * Shrinks internal storage to fit current usage
         */
        void ShrinkToFit()
        {
            m_versions.shrink_to_fit();
            m_freeList.shrink_to_fit();
        }
        
        /**
         * Iterator for alive entities only
         */
        class Iterator
        {
        private:
            const EntityPool* m_pool;
            EntityID m_current;
            
            void SkipInvalid() noexcept
            {
                while (m_current < m_pool->m_versions.size() &&
                       (m_pool->m_versions[m_current] == NULL_VERSION ||
                        m_pool->m_versions[m_current] == TOMBSTONE_VERSION))
                {
                    ++m_current;
                }
            }
            
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = Entity;
            using difference_type = std::ptrdiff_t;
            using pointer = const Entity*;
            using reference = Entity;
            
            Iterator(const EntityPool* pool, EntityID start) noexcept : m_pool(pool), m_current(start)
            {
                SkipInvalid();
            }
            
            [[nodiscard]] Entity operator*() const noexcept
            {
                ASTRA_ASSERT(m_current < m_pool->m_versions.size(), "Iterator out of range");
                const VersionType version = m_pool->m_versions[m_current];
                ASTRA_ASSERT(version != NULL_VERSION && version != TOMBSTONE_VERSION, "Iterator on invalid entity");
                return Entity(m_current, version);
            }
            
            Iterator& operator++() noexcept
            {
                ++m_current;
                SkipInvalid();
                return *this;
            }
            
            Iterator operator++(int) noexcept
            {
                Iterator tmp = *this;
                ++(*this);
                return tmp;
            }
            
            [[nodiscard]] bool operator==(const Iterator& other) const noexcept
            {
                return m_pool == other.m_pool && m_current == other.m_current;
            }
            
            [[nodiscard]] bool operator!=(const Iterator& other) const noexcept
            {
                return !(*this == other);
            }
        };
        
        /**
         * Returns iterator to first alive entity
         */
        [[nodiscard]] Iterator begin() const noexcept
        {
            return Iterator(this, 0);
        }
        
        /**
         * Returns iterator past last entity
         */
        [[nodiscard]] Iterator end() const noexcept
        {
            return Iterator(this, static_cast<EntityID>(m_versions.size()));
        }
        
        /**
         * Performs consistency checks (debug only)
         */
        void Validate() const noexcept
        {
#ifdef ASTRA_BUILD_DEBUG
            // Count alive entities
            SizeType aliveCount = 0;
            for (EntityID id = 0; id < m_versions.size(); ++id)
            {
                if (m_versions[id] != NULL_VERSION && m_versions[id] != TOMBSTONE_VERSION)
                {
                    ++aliveCount;
                }
                else if (m_versions[id] == TOMBSTONE_VERSION)
                {
                    // Should be in free list
                    bool found = false;
                    for (const auto& entry : m_freeList)
                    {
                        if (entry.id == id)
                        {
                            found = true;
                            break;
                        }
                    }
                    ASTRA_ASSERT(found, "Tombstone entity not in free list");
                }
            }
            
            ASTRA_ASSERT(aliveCount == m_aliveCount, "Alive count mismatch");
            ASTRA_ASSERT(m_nextId <= EntityTraits32::ENTITY_MASK + 1, "Next ID overflow");
#endif
        }
    };
}