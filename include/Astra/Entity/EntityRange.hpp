#pragma once

#include <cstddef>
#include <cstdint>
#include "../Core/Base.hpp"

namespace Astra
{
    /**
     * Internal structure representing a contiguous range of entities within a chunk.
     * Used by View and Archetype for efficient iteration over entity subsets.
     * Not exposed to end users.
     */
    struct EntityRange
    {
        size_t chunkIndex = SIZE_MAX;  // Which chunk in the archetype
        size_t startIndex = 0;         // Starting entity index within the chunk
        size_t count = 0;               // Number of entities (0 means "rest of chunk")
        
        /**
         * Check if this range is valid
         */
        ASTRA_NODISCARD bool IsValid() const noexcept
        {
            return chunkIndex != SIZE_MAX;
        }
        
        /**
         * Check if this range is adjacent to another (can be merged)
         */
        ASTRA_NODISCARD bool IsAdjacentTo(const EntityRange& other) const noexcept
        {
            return chunkIndex == other.chunkIndex && 
                   (startIndex + count == other.startIndex);
        }
        
        /**
         * Merge with an adjacent range
         */
        void MergeWith(const EntityRange& other)
        {
            ASTRA_ASSERT(IsAdjacentTo(other), "Cannot merge non-adjacent ranges");
            count = (other.count == 0) ? 0 : (count + other.count);
        }
        
        /**
         * Comparison operator for sorting ranges
         */
        ASTRA_NODISCARD bool operator<(const EntityRange& other) const noexcept
        {
            if (chunkIndex != other.chunkIndex)
                return chunkIndex < other.chunkIndex;
            return startIndex < other.startIndex;
        }
        
        ASTRA_NODISCARD bool operator==(const EntityRange& other) const noexcept
        {
            return chunkIndex == other.chunkIndex && 
                   startIndex == other.startIndex && 
                   count == other.count;
        }
    };
    
} // namespace Astra