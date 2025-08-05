#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

#include "../Component/Component.hpp"
#include "../Component/ComponentRegistry.hpp"
#include "../Container/Bitmap.hpp"
#include "../Container/ChunkPool.hpp"
#include "../Container/FlatMap.hpp"
#include "../Core/TypeID.hpp"
#include "../Entity/Entity.hpp"
#include "Archetype.hpp"

namespace Astra
{
    class ArchetypeStorage
    {
    public:
        struct EntityLocation
        {
            Archetype* archetype = nullptr;
            PackedLocation packedLocation;  // Encapsulates (chunkIdx << shift | entityIdx)
        };
        
        struct ArchetypeEdge
        {
            Archetype* to = nullptr;
            ComponentID componentId = 0;
        };
        
        struct MoveOp
        {
            enum Type { Move, Construct, Skip };
            Type type;
            size_t srcIdx;
            size_t dstIdx;
            bool isTrivial;
        };
        
        // Metrics for tracking archetype usage
        struct ArchetypeMetrics
        {
            size_t currentEntityCount = 0;
            size_t peakEntityCount = 0;
            size_t emptyDuration = 0;  // Number of cleanup calls while empty
            
            void UpdateForCleanup(size_t entityCount)
            {
                currentEntityCount = entityCount;
                if (entityCount > peakEntityCount)
                {
                    peakEntityCount = entityCount;
                }
                
                if (entityCount == 0)
                {
                    emptyDuration++;
                }
                else
                {
                    emptyDuration = 0;
                }
            }
            
            void UpdatePeak(size_t entityCount)
            {
                currentEntityCount = entityCount;
                if (entityCount > peakEntityCount)
                {
                    peakEntityCount = entityCount;
                }
            }
        };
        
        // TODO: Add defragmentation triggers based on utilization thresholds
        // - Periodic background defragmentation based on global memory pressure
        // - Prioritize defragmentation of archetypes with highest fragmentation cost
        // - Implement incremental defragmentation to avoid frame spikes
        // - Add API for manual defragmentation triggers
        // - Consider implementing a defragmentation budget (max entities moved per frame)
        
        // Default constructor creates new component registry
        explicit ArchetypeStorage(const ChunkPool::Config& poolConfig = {}) 
            : m_chunkPool(poolConfig)
            , m_componentRegistry(std::make_shared<ComponentRegistry>())
        {
            InitializeRootArchetype();
        }
        
        // Constructor for sharing component registry
        ArchetypeStorage(std::shared_ptr<ComponentRegistry> registry, const ChunkPool::Config& poolConfig = {})
            : m_chunkPool(poolConfig)
            , m_componentRegistry(std::move(registry))
        {
            InitializeRootArchetype();
        }
        
        // Get the component registry for sharing with other storage instances
        std::shared_ptr<ComponentRegistry> GetComponentRegistry() const
        {
            return m_componentRegistry;
        }
        
        /**
         * Create or get archetype for a specific component combination
         */
        template<Component... Components>
        ASTRA_NODISCARD Archetype* GetOrCreateArchetype()
        {
            // Register components before creating archetype
            (m_componentRegistry->RegisterComponent<Components>(), ...);
            ComponentMask mask = MakeComponentMask<Components...>();
            return GetOrCreateArchetype(mask);
        }
        
        /**
         * Add entity to storage (in root archetype)
         * Entity should be created by EntityPool
         */
        void AddEntity(Entity entity)
        {
            PackedLocation packedLocation = m_rootArchetype->AddEntity(entity);
            if (!packedLocation.IsValid()) ASTRA_UNLIKELY
            {
                // Allocation failed - entity won't be tracked
                return;
            }
            m_entityMap[entity] = EntityLocation{m_rootArchetype, packedLocation};
        }
        
        template<Component... Components, typename Generator>
        void AddEntities(std::span<const Entity> entities, Generator&& generator)
        {
            size_t count = entities.size();
            if (count == 0) ASTRA_UNLIKELY
                return;

            // Get or create archetype once
            Archetype* archetype = GetOrCreateArchetype<Components...>();

            // Use optimized batch addition
            std::vector<PackedLocation> packedLocations = archetype->AddEntities(entities);
            
            // Apply components to all successfully added entities
            for (size_t i = 0; i < packedLocations.size(); ++i)
            {
                // Apply components
                std::apply([&](auto&&... components)
                {
                    ((archetype->SetComponent(packedLocations[i], std::forward<decltype(components)>(components))), ...);
                }, generator(i));
            }

            // Batch update entity map - reserve space first for efficiency
            m_entityMap.Reserve(m_entityMap.Size() + packedLocations.size());
            for (size_t i = 0; i < packedLocations.size(); ++i)
            {
                m_entityMap[entities[i]] = EntityLocation{archetype, packedLocations[i]};
            }
        }

        /**
         * Set entity location directly (for batch operations)
         * Used when creating entities with known archetype
         */
        void SetEntityLocation(Entity entity, Archetype* archetype, PackedLocation packedLocation)
        {
            m_entityMap[entity] = EntityLocation{archetype, packedLocation};
        }
        
        /**
         * Remove entity from storage
         * Entity destruction should be handled by EntityPool
         */
        void RemoveEntity(Entity entity)
        {
            auto it = m_entityMap.Find(entity);
            if (it == m_entityMap.end()) ASTRA_UNLIKELY return;
            
            EntityLocation& loc = it->second;
            
            // Remove from archetype, check if another entity was moved
            if (auto movedEntity = loc.archetype->RemoveEntity(loc.packedLocation)) ASTRA_LIKELY
            {
                // Update location of moved entity
                m_entityMap[*movedEntity].packedLocation = loc.packedLocation;
            }
            
            m_entityMap.Erase(it);
        }
        
        /**
         * Add component to entity (moves to new archetype)
         */
        template<Component T, typename... Args>
        T* AddComponent(Entity entity, Args&&... args)
        {
            // Ensure component is registered
            m_componentRegistry->RegisterComponent<T>();
            
            auto it = m_entityMap.Find(entity);
            if (it == m_entityMap.end()) ASTRA_UNLIKELY return nullptr;
            
            EntityLocation& oldLoc = it->second;
            ComponentID componentId = TypeID<T>::Value();
            
            // Check if entity already has component
            if (oldLoc.archetype->GetMask().Test(componentId)) ASTRA_UNLIKELY
                return nullptr;
                
            // Find or create edge to new archetype
            Archetype* newArchetype = GetArchetypeWithAdded(oldLoc.archetype, componentId);
            
            // Optimized move with in-place construction
            PackedLocation newPackedLocation = MoveEntity<T>(entity, oldLoc, newArchetype, 
                                         std::forward<Args>(args)...);
            if (!newPackedLocation.IsValid()) ASTRA_UNLIKELY
            {
                // Allocation failed
                return nullptr;
            }
            
            return newArchetype->GetComponent<T>(newPackedLocation);
        }
        
        /**
         * Remove component from entity
         */
        template<Component T>
        bool RemoveComponent(Entity entity)
        {
            // Ensure component is registered
            m_componentRegistry->RegisterComponent<T>();
            
            ComponentID componentId = TypeID<T>::Value();
            
            // Component validity is ensured at compile time through the type system
            
            auto it = m_entityMap.Find(entity);
            if (it == m_entityMap.end()) ASTRA_UNLIKELY return false;
            
            EntityLocation& oldLoc = it->second;
            
            // Check if entity has component
            if (!oldLoc.archetype->GetMask().Test(componentId)) ASTRA_UNLIKELY
                return false;
                
            // Find or create edge to new archetype
            Archetype* newArchetype = GetArchetypeWithRemoved(
                oldLoc.archetype, componentId);
            
            // Destroy component before moving
            // Check if the entity is still at the expected location
            size_t chunkIdx = oldLoc.packedLocation.GetChunkIndex(oldLoc.archetype->GetEntitiesPerChunkShift());
            size_t entityIdx = oldLoc.packedLocation.GetEntityIndex(oldLoc.archetype->GetEntitiesPerChunkMask());
            
            if (chunkIdx < oldLoc.archetype->GetChunks().size() && 
                entityIdx < oldLoc.archetype->GetChunks()[chunkIdx]->GetCount()) ASTRA_LIKELY
            {
                if (T* comp = oldLoc.archetype->GetComponent<T>(oldLoc.packedLocation)) ASTRA_LIKELY
                {
                    comp->~T();
                }
            }
            
            // Move entity to new archetype
            PackedLocation newPackedLocation = MoveEntity(entity, oldLoc, newArchetype);
            if (!newPackedLocation.IsValid()) ASTRA_UNLIKELY
            {
                // Allocation failed - component was destroyed but entity couldn't be moved
                // This is a critical error state
                return false;
            }
            
            return true;
        }
        
        /**
         * Get component for entity
         */
        template<Component T>
        ASTRA_NODISCARD T* GetComponent(Entity entity)
        {
            // Don't auto-register - component should already exist if retrieving
            ComponentID componentId = TypeID<T>::Value();
            
            // Component validity is ensured at compile time
            
            auto it = m_entityMap.Find(entity);
            if (it == m_entityMap.end()) ASTRA_UNLIKELY return nullptr;
            
            EntityLocation& loc = it->second;
            return loc.archetype->GetComponent<T>(loc.packedLocation);
        }
        
        /**
         * Query archetypes that match a component mask
         */
        ASTRA_NODISCARD auto QueryArchetypes(const ComponentMask& mask)
        {
            return m_archetypes |
                std::views::transform([](auto& entry) { return entry.archetype.get(); }) |
                std::views::filter([mask](Archetype* arch) { return arch->GetMask().HasAll(mask); });
        }
        
        /**
         * Get all archetypes for custom query logic
         */
        ASTRA_NODISCARD auto GetAllArchetypes()
        {
            return m_archetypes |
                std::views::transform([](auto& entry) { return entry.archetype.get(); });
        }
        
        /**
         * Get chunk pool statistics
         */
        ASTRA_NODISCARD ChunkPool::Stats GetPoolStats() const
        {
            return m_chunkPool.GetStats();
        }
        
        /**
         * Add component to multiple entities in batch (optimized)
         */
        template<Component T, typename... Args>
        void AddComponents(std::span<Entity> entities, Args&&... args)
        {
            m_componentRegistry->RegisterComponent<T>();
            ComponentID componentId = TypeID<T>::Value();
            
            // Group entities by current archetype
            FlatMap<Archetype*, std::vector<std::pair<Entity, PackedLocation>>> batches;
            
            for (Entity entity : entities)
            {
                auto it = m_entityMap.Find(entity);
                if (it == m_entityMap.end()) ASTRA_UNLIKELY continue;
                
                EntityLocation& loc = it->second;
                if (!loc.archetype->GetMask().Test(componentId)) ASTRA_LIKELY
                {
                    batches[loc.archetype].emplace_back(entity, loc.packedLocation);
                }
            }
            
            // Process each batch
            for (auto& [srcArchetype, entityBatch] : batches)
            {
                Archetype* dstArchetype = GetArchetypeWithAdded(srcArchetype, componentId);
                
                // Batch move all entities
                MoveEntities<T>(srcArchetype, dstArchetype, entityBatch, std::forward<Args>(args)...);
            }
        }
        
        /**
         * Remove multiple entities in batch (optimized)
         */
        void RemoveEntities(std::span<Entity> entities)
        {
            if (entities.empty()) ASTRA_UNLIKELY
                return;
                
            // Group entities by archetype for batch processing
            FlatMap<Archetype*, std::vector<std::pair<Entity, PackedLocation>>> batches;
            
            for (Entity entity : entities)
            {
                auto it = m_entityMap.Find(entity);
                if (it == m_entityMap.end()) ASTRA_UNLIKELY continue;
                
                EntityLocation& loc = it->second;
                batches[loc.archetype].emplace_back(entity, loc.packedLocation);
            }
            
            // Process each batch
            for (auto& [archetype, entityBatch] : batches)
            {
                // Extract packed locations
                std::vector<PackedLocation> packedLocations;
                packedLocations.reserve(entityBatch.size());
                for (const auto& [entity, packedLoc] : entityBatch)
                {
                    packedLocations.push_back(packedLoc);
                }
                
                // Batch remove from archetype
                auto movedEntities = archetype->RemoveEntities(packedLocations);
                
                // Update entity locations for moved entities
                for (const auto& [movedEntity, newPackedLocation] : movedEntities)
                {
                    auto movedIt = m_entityMap.Find(movedEntity);
                    if (movedIt != m_entityMap.end()) ASTRA_LIKELY
                    {
                        movedIt->second.packedLocation = newPackedLocation;
                    }
                }
                
                // Remove entities from map
                for (const auto& [entity, _] : entityBatch)
                {
                    m_entityMap.Erase(entity);
                }
            }
        }
        
        // Cleanup options for archetype removal
        struct CleanupOptions
        {
            size_t minEmptyDuration = 1;              // Remove if empty for at least this many calls
            size_t minArchetypesToKeep = 8;           // Keep at least this many archetypes
            size_t maxArchetypesToRemove = SIZE_MAX;  // Max to remove in one call
            size_t maxPeakEntityCount = SIZE_MAX;     // Only remove if peak count below this
        };
        
        // Information about an archetype
        struct ArchetypeInfo
        {
            Archetype* archetype;
            size_t currentEntityCount;
            size_t peakEntityCount;
            size_t emptyDuration;
            ComponentMask mask;
            size_t approximateMemoryUsage;
        };
        
        // Update metrics for all archetypes (call before cleanup)
        void UpdateArchetypeMetrics()
        {
            for (auto& entry : m_archetypes)
            {
                entry.metrics.UpdateForCleanup(entry.archetype->GetEntityCount());
            }
        }
        
        // Get archetype statistics
        ASTRA_NODISCARD std::vector<ArchetypeInfo> GetArchetypeStats() const
        {
            std::vector<ArchetypeInfo> stats;
            stats.reserve(m_archetypes.size());
            
            for (const auto& entry : m_archetypes)
            {
                ArchetypeInfo info;
                info.archetype = entry.archetype.get();
                info.currentEntityCount = entry.metrics.currentEntityCount;
                info.peakEntityCount = entry.metrics.peakEntityCount;
                info.emptyDuration = entry.metrics.emptyDuration;
                info.mask = entry.archetype->GetMask();
                
                // Calculate approximate memory usage
                size_t chunkCount = entry.archetype->GetChunks().size();
                info.approximateMemoryUsage = chunkCount * ChunkPool::CHUNK_SIZE + 
                                             sizeof(Archetype) + 
                                             sizeof(size_t) * MAX_COMPONENTS * 2;
                
                stats.push_back(info);
            }
            
            return stats;
        }
        
        // Get total archetype count
        ASTRA_NODISCARD size_t GetArchetypeCount() const
        {
            return m_archetypes.size();
        }
        
        // Get approximate memory usage
        ASTRA_NODISCARD size_t GetArchetypeMemoryUsage() const
        {
            size_t total = 0;
            for (const auto& entry : m_archetypes)
            {
                size_t chunkCount = entry.archetype->GetChunks().size();
                total += chunkCount * ChunkPool::CHUNK_SIZE;
                total += sizeof(Archetype) + sizeof(size_t) * MAX_COMPONENTS * 2;
            }
            return total;
        }
        
        // Remove empty archetypes based on options
        size_t CleanupEmptyArchetypes(const CleanupOptions& options = {})
        {
            // Never remove root archetype
            if (m_archetypes.size() <= options.minArchetypesToKeep)
            {
                return 0;
            }
            
            // Identify candidates for removal
            std::vector<size_t> candidates;
            
            for (size_t i = 0; i < m_archetypes.size(); ++i)
            {
                const auto& entry = m_archetypes[i];
                
                // Skip root archetype
                if (entry.archetype.get() == m_rootArchetype)
                {
                    continue;
                }
                
                // Check if candidate for removal
                if (entry.metrics.currentEntityCount == 0 &&
                    entry.metrics.emptyDuration >= options.minEmptyDuration &&
                    entry.metrics.peakEntityCount <= options.maxPeakEntityCount)
                {
                    candidates.push_back(i);
                }
            }
            
            // Ensure we keep minimum archetypes
            size_t maxCanRemove = m_archetypes.size() - options.minArchetypesToKeep;
            if (candidates.size() > maxCanRemove)
            {
                candidates.resize(maxCanRemove);
            }
            
            // Limit removals per call
            if (candidates.size() > options.maxArchetypesToRemove)
            {
                // Sort by empty duration (longest empty first)
                std::partial_sort(
                    candidates.begin(),
                    candidates.begin() + options.maxArchetypesToRemove,
                    candidates.end(),
                    [this](size_t a, size_t b) {
                        return m_archetypes[a].metrics.emptyDuration > 
                               m_archetypes[b].metrics.emptyDuration;
                    }
                );
                candidates.resize(options.maxArchetypesToRemove);
            }
            
            // Remove archetypes in reverse order to maintain indices
            std::sort(candidates.rbegin(), candidates.rend());
            
            size_t removed = 0;
            for (size_t idx : candidates)
            {
                RemoveArchetypeAt(idx);
                removed++;
            }
            
            return removed;
        }
        
        
    private:        
        ASTRA_NODISCARD Archetype* GetOrCreateArchetype(const ComponentMask& mask)
        {
            // Check if archetype already exists
            auto it = m_archetypeMap.Find(mask);
            if (it != m_archetypeMap.end()) ASTRA_LIKELY
            {
                return it->second;
            }

            // Create new archetype
            auto archetype = std::make_unique<Archetype>(mask);
            Archetype* ptr = archetype.get();
            
            // Set the pool for the new archetype
            ptr->m_chunkPool = &m_chunkPool;

            // Initialize based on component mask
            std::vector<ComponentDescriptor> componentDescriptors;


            for (ComponentID id = 0; id < MAX_COMPONENTS; ++id)
            {
                if (mask.Test(id)) ASTRA_UNLIKELY
                {
                    if (const auto* desc = m_componentRegistry->GetComponent(id)) ASTRA_LIKELY
                    {
                        componentDescriptors.push_back
                        ({
                            .id = desc->id,
                            .size = desc->size,
                            .alignment = desc->alignment,
                            .is_trivially_copyable = desc->is_trivially_copyable,
                            .is_nothrow_move_constructible = desc->is_nothrow_move_constructible,
                            .is_empty = desc->is_empty,
                            .defaultConstruct = desc->defaultConstruct,
                            .destruct = desc->destruct,
                            .copyConstruct = desc->copyConstruct,
                            .moveConstruct = desc->moveConstruct,
                            .moveAssign = desc->moveAssign,
                            .copyAssign = desc->copyAssign
                        });
                    }
                }
            }

            if (!componentDescriptors.empty()) ASTRA_LIKELY
            {
                ptr->Initialize(componentDescriptors);
            }

            // Store archetype
            m_archetypeMap[mask] = ptr;
            
            ArchetypeEntry entry;
            entry.archetype = std::move(archetype);
            // Don't call Update(0) here - let it track naturally
            m_archetypes.push_back(std::move(entry));

            return ptr;
        }

        Archetype* GetArchetypeWithAdded(Archetype* from, ComponentID componentId)
        {
            // Check edge cache
            auto& edges = m_addEdges[from];
            auto it = edges.Find(componentId);
            if (it != edges.end()) ASTRA_LIKELY
            {
                return it->second.to;
            }
            
            // Create new mask with component added
            ComponentMask newMask = from->GetMask();
            newMask.Set(componentId);
            
            // Get or create archetype
            Archetype* to = GetOrCreateArchetype(newMask);
            
            // Cache edge
            edges[componentId] = ArchetypeEdge{to, componentId};
            
            return to;
        }
        
        Archetype* GetArchetypeWithRemoved(Archetype* from, ComponentID componentId)
        {
            // Check edge cache
            auto& edges = m_removeEdges[from];
            auto it = edges.Find(componentId);
            if (it != edges.end()) ASTRA_LIKELY
            {
                return it->second.to;
            }
            
            // Create new mask with component removed
            ComponentMask newMask = from->GetMask();
            newMask.Reset(componentId);
            
            // Get or create archetype
            Archetype* to = GetOrCreateArchetype(newMask);
            
            // Cache edge
            edges[componentId] = ArchetypeEdge{to, componentId};
            
            return to;
        }
        
        PackedLocation MoveEntity(Entity entity, EntityLocation& oldLoc, Archetype* newArchetype)
        {
            // Reserve space in new archetype without constructing
            PackedLocation newPackedLocation = newArchetype->AddEntityNoConstruct(entity);
            if (!newPackedLocation.IsValid()) ASTRA_UNLIKELY
            {
                // Allocation failed - return invalid location
                return newPackedLocation;
            }
            
            // Move existing components efficiently
            if (oldLoc.archetype->IsInitialized() && newArchetype->IsInitialized()) ASTRA_LIKELY
            {
                // Move entity data from old archetype to new archetype
                newArchetype->MoveEntityFrom(newPackedLocation, *oldLoc.archetype, oldLoc.packedLocation);
            }
            
            // Remove from old archetype and handle moved entity
            if (auto movedEntity = oldLoc.archetype->RemoveEntity(oldLoc.packedLocation)) ASTRA_LIKELY
            {
                m_entityMap[*movedEntity].packedLocation = oldLoc.packedLocation;
            }
            
            // Update entity location
            oldLoc.archetype = newArchetype;
            oldLoc.packedLocation = newPackedLocation;
            
            return newPackedLocation;
        }
        
        // Optimized move that constructs new component in-place
        template<Component T, typename... Args>
        PackedLocation MoveEntity(Entity entity, EntityLocation& oldLoc, Archetype* newArchetype, Args&&... args)
        {
            // Reserve space in new archetype without constructing
            PackedLocation newPackedLocation = newArchetype->AddEntityNoConstruct(entity);
            if (!newPackedLocation.IsValid()) ASTRA_UNLIKELY
            {
                // Allocation failed - return invalid location
                return newPackedLocation;
            }
            
            // Move existing components and construct new one efficiently
            if (oldLoc.archetype->IsInitialized() && newArchetype->IsInitialized()) ASTRA_LIKELY
            {
                TransferComponentsAndAddNew<T>(
                    newPackedLocation, newArchetype, 
                    oldLoc.packedLocation, oldLoc.archetype,
                    std::forward<Args>(args)...
                );
            }
            
            // Remove from old archetype and handle moved entity
            if (auto movedEntity = oldLoc.archetype->RemoveEntity(oldLoc.packedLocation)) ASTRA_LIKELY
            {
                m_entityMap[*movedEntity].packedLocation = oldLoc.packedLocation;
            }
            
            // Update entity location
            oldLoc.archetype = newArchetype;
            oldLoc.packedLocation = newPackedLocation;
            
            return newPackedLocation;
        }
        
        // Transfer existing components and construct new component in-place
        template<Component T, typename... Args>
        void TransferComponentsAndAddNew(PackedLocation dstPackedLocation, Archetype* dstArchetype, PackedLocation srcPackedLocation, Archetype* srcArchetype, Args&&... args)
        {
            // Get component info for both archetypes
            auto& dstComponents = dstArchetype->GetComponents();
            auto& srcComponents = srcArchetype->GetComponents();
            
            // Build move operations list
            std::vector<MoveOp> moveOps;
            moveOps.reserve(dstComponents.size());
            
            // Create index map for source components - use array for O(1) access
            std::array<size_t, MAX_COMPONENTS> srcIndexMap;
            srcIndexMap.fill(std::numeric_limits<size_t>::max()); // Use max as "not found" marker
            for (size_t i = 0; i < srcComponents.size(); ++i)
            {
                srcIndexMap[srcComponents[i].id] = i;
            }
            
            ComponentID newComponentId = TypeID<T>::Value();
            
            // Plan move operations
            for (size_t dstIdx = 0; dstIdx < dstComponents.size(); ++dstIdx)
            {
                auto& dstComp = dstComponents[dstIdx];
                
                if (dstComp.id == newComponentId) ASTRA_UNLIKELY
                {
                    // This is our new component - will construct in-place
                    moveOps.push_back({MoveOp::Construct, 0, dstIdx, false});
                }
                else
                {
                    size_t srcIdx = srcIndexMap[dstComp.id];
                    if (srcIdx != std::numeric_limits<size_t>::max()) ASTRA_LIKELY
                    {
                        // Existing component - move it
                        bool isTrivial = dstComp.is_trivially_copyable;
                        moveOps.push_back({MoveOp::Move, srcIdx, dstIdx, isTrivial});
                    }
                    else
                    {
                        // This shouldn't happen in AddComponent
                        moveOps.push_back({MoveOp::Skip, 0, dstIdx, false});
                    }
                }
            }
            
            // Execute moves in optimal order
            ExecuteMoveOperations<T>(moveOps, dstPackedLocation, dstArchetype, srcPackedLocation, srcArchetype,
                                    std::forward<Args>(args)...);
        }
        
        template<Component T, typename... Args>
        void ExecuteMoveOperations(const std::vector<MoveOp>& ops, PackedLocation dstPackedLocation, Archetype* dstArchetype, PackedLocation srcPackedLocation, Archetype* srcArchetype, Args&&... args)
        {
            // Get chunks
            auto [dstChunk, dstEntityIdx] = dstArchetype->GetChunkAndIndex(dstPackedLocation);
            auto [srcChunk, srcEntityIdx] = srcArchetype->GetChunkAndIndex(srcPackedLocation);
            
            // Group operations by type for better performance
            for (const auto& op : ops)
            {
                switch (op.type)
                {
                case MoveOp::Move:
                    if (op.isTrivial) ASTRA_LIKELY
                    {
                        // Use memcpy for trivial types
                        void* dstPtr = dstChunk->GetComponentPointerCached(op.dstIdx, dstEntityIdx);
                        void* srcPtr = srcChunk->GetComponentPointerCached(op.srcIdx, srcEntityIdx);
                        std::memcpy(dstPtr, srcPtr, dstArchetype->GetComponents()[op.dstIdx].size);
                    }
                    else
                    {
                        // Use move constructor for non-trivial types
                        void* dstPtr = dstChunk->GetComponentPointerCached(op.dstIdx, dstEntityIdx);
                        void* srcPtr = srcChunk->GetComponentPointerCached(op.srcIdx, srcEntityIdx);
                        dstArchetype->GetComponents()[op.dstIdx].moveConstruct(dstPtr, srcPtr);
                    }
                    break;
                    
                case MoveOp::Construct:
                    // Construct new component in-place with perfect forwarding
                    {
                        void* dstPtr = dstChunk->GetComponentPointerCached(op.dstIdx, dstEntityIdx);
                        new (dstPtr) T(std::forward<Args>(args)...);
                    }
                    break;
                    
                case MoveOp::Skip:
                    // Should not happen in AddComponent
                    break;
                }
            }
        }
        
        // This method is no longer needed with packed locations
        // Entity locations are set individually when entities are added

        template<Component T, typename... Args>
        void MoveEntities(Archetype* srcArchetype, Archetype* dstArchetype, const std::vector<std::pair<Entity, PackedLocation>>& entities, Args&&... args)
        {
            // Sort entities by packed location for cache efficiency
            std::vector<std::pair<Entity, PackedLocation>> sortedEntities = entities;
            std::sort(sortedEntities.begin(), sortedEntities.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
            
            // Process entities
            for (auto [entity, srcPackedLocation] : sortedEntities)
            {
                PackedLocation dstPackedLocation = dstArchetype->AddEntityNoConstruct(entity);
                if (!dstPackedLocation.IsValid()) ASTRA_UNLIKELY
                {
                    // Allocation failed - stop processing remaining entities
                    // Already moved entities stay in new archetype
                    break;
                }
                
                // Move components efficiently
                TransferComponentsAndAddNew<T>(
                    dstPackedLocation, dstArchetype,
                    srcPackedLocation, srcArchetype,
                    args...); // Note: Can't perfect forward in batch
                
                // Update entity location
                m_entityMap[entity] = {dstArchetype, dstPackedLocation};
            }
            
            // Remove all entities from source archetype
            // Process in reverse to avoid invalidating indices
            for (auto it = sortedEntities.rbegin(); it != sortedEntities.rend(); ++it)
            {
                if (auto movedEntity = srcArchetype->RemoveEntity(it->second)) ASTRA_LIKELY
                {
                    m_entityMap[*movedEntity].packedLocation = it->second;
                }
            }
        }
        
    private:
        void InitializeRootArchetype()
        {
            // Create root archetype (no components)
            auto rootArchetype = std::make_unique<Archetype>(ComponentMask{});
            m_rootArchetype = rootArchetype.get();
            
            // Set the pool for the root archetype
            m_rootArchetype->m_chunkPool = &m_chunkPool;
            
            // Initialize root archetype with empty component list
            m_rootArchetype->Initialize({});
            
            ArchetypeEntry entry;
            entry.archetype = std::move(rootArchetype);
            // Metrics will be tracked when entities are added
            m_archetypes.push_back(std::move(entry));
        }
        
    private:
        // Archetype entry with metrics
        struct ArchetypeEntry
        {
            std::unique_ptr<Archetype> archetype;
            ArchetypeMetrics metrics;
        };
        
        ChunkPool m_chunkPool;  // Pool owned by storage
        std::shared_ptr<ComponentRegistry> m_componentRegistry;  // Shared component registry
        std::vector<ArchetypeEntry> m_archetypes;
        FlatMap<ComponentMask, Archetype*, BitmapHash<MAX_COMPONENTS>> m_archetypeMap;
        FlatMap<Entity, EntityLocation> m_entityMap;
        
        // Edge caches for fast archetype transitions
        FlatMap<Archetype*, FlatMap<ComponentID, ArchetypeEdge>> m_addEdges;
        FlatMap<Archetype*, FlatMap<ComponentID, ArchetypeEdge>> m_removeEdges;
        
        Archetype* m_rootArchetype = nullptr;
        
        // Helper to update metrics when entity count changes
        void UpdateArchetypeMetrics(Archetype* archetype)
        {
            for (auto& entry : m_archetypes)
            {
                if (entry.archetype.get() == archetype)
                {
                    entry.metrics.UpdatePeak(archetype->GetEntityCount());
                    break;
                }
            }
        }
        
        // Helper method to remove archetype at index
        void RemoveArchetypeAt(size_t index)
        {
            ASTRA_ASSERT(index < m_archetypes.size(), "Invalid archetype index");
            
            Archetype* archetype = m_archetypes[index].archetype.get();
            
            // Remove from archetype map
            m_archetypeMap.Erase(archetype->GetMask());
            
            // Remove all edges involving this archetype
            RemoveArchetypeEdges(archetype);
            
            // Remove the archetype entry (this will destroy the archetype)
            m_archetypes.erase(m_archetypes.begin() + index);
        }
        
        // Remove all edges to/from an archetype
        void RemoveArchetypeEdges(Archetype* archetype)
        {
            // Remove edges where this archetype is the source
            m_addEdges.Erase(archetype);
            m_removeEdges.Erase(archetype);
            
            // Remove edges where this archetype is the destination
            // We need to collect keys to remove first, then remove them
            for (auto& [fromArch, addEdges] : m_addEdges)
            {
                std::vector<ComponentID> toRemove;
                for (auto& [compId, edge] : addEdges)
                {
                    if (edge.to == archetype)
                    {
                        toRemove.push_back(compId);
                    }
                }
                for (ComponentID compId : toRemove)
                {
                    addEdges.Erase(compId);
                }
            }
            
            for (auto& [fromArch, removeEdges] : m_removeEdges)
            {
                std::vector<ComponentID> toRemove;
                for (auto& [compId, edge] : removeEdges)
                {
                    if (edge.to == archetype)
                    {
                        toRemove.push_back(compId);
                    }
                }
                for (ComponentID compId : toRemove)
                {
                    removeEdges.Erase(compId);
                }
            }
        }
    };
}