#pragma once

#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <span>
#include <tuple>
#include <vector>

#include "../Archetype/Archetype.hpp"
#include "../Archetype/ArchetypeStorage.hpp"
#include "../Component/Component.hpp"
#include "../Component/ComponentRegistry.hpp"
#include "../Core/Base.hpp"
#include "../Core/Result.hpp"
#include "../Core/Signal.hpp"
#include "../Core/TypeID.hpp"
#include "../Entity/Entity.hpp"
#include "../Entity/EntityPool.hpp"
#include "../Memory/ChunkPool.hpp"
#include "../Serialization/BinaryReader.hpp"
#include "../Serialization/BinaryWriter.hpp"
#include "../Serialization/SerializationError.hpp"
#include "Query.hpp"
#include "Relations.hpp"
#include "RelationshipGraph.hpp"
#include "View.hpp"

namespace Astra
{
    class Registry
    {
    public:
        struct Config
        {
            EntityPool::Config entityPoolConfig;
            ChunkPool::Config chunkPoolConfig;
        };
        
        explicit Registry(const Config& config = {}) :
            m_entityPool(std::make_shared<EntityPool>(config.entityPoolConfig)),
            m_archetypeStorage(std::make_shared<ArchetypeStorage>(config.chunkPoolConfig))
        {}
        
        Registry(const EntityPool::Config& entityConfig, const ChunkPool::Config& chunkConfig) :
            m_entityPool(std::make_shared<EntityPool>(entityConfig)),
            m_archetypeStorage(std::make_shared<ArchetypeStorage>(chunkConfig))
        {}
        
        Registry(std::shared_ptr<ComponentRegistry> componentRegistry, const Config& config = {}) :
            m_entityPool(std::make_shared<EntityPool>(config.entityPoolConfig)),
            m_archetypeStorage(std::make_shared<ArchetypeStorage>(componentRegistry, config.chunkPoolConfig))
        {}
        
        explicit Registry(const Registry& other, const Config& config = {}) :
            m_entityPool(std::make_shared<EntityPool>(config.entityPoolConfig)),
            m_archetypeStorage(std::make_shared<ArchetypeStorage>(other.GetComponentRegistry(), config.chunkPoolConfig))
        {}
        
        Entity CreateEntity()
        {
            Entity entity = m_entityPool->Create();
            m_archetypeStorage->AddEntity(entity);
            
            m_signalManager.Emit<Events::EntityCreated>(entity);
            
            return entity;
        }
        
        template<Component... Components>
        Entity CreateEntity(Components&&... components)
        {
            Entity entity = m_entityPool->Create();
            Archetype* archetype = m_archetypeStorage->GetOrCreateArchetype<Components...>();
            PackedLocation packedLocation = archetype->AddEntity(entity);
            
            if (!packedLocation.IsValid())
            {
                m_entityPool->Destroy(entity);
                return Entity::Invalid();
            }
            
            ((archetype->SetComponent<Components>(packedLocation, std::forward<Components>(components))), ...);
            
            m_archetypeStorage->SetEntityLocation(entity, archetype, packedLocation);
            
            m_signalManager.Emit<Events::EntityCreated>(entity);
            
            if (m_signalManager.IsSignalEnabled(Signal::ComponentAdded))
            {
                ((m_signalManager.Emit<Events::ComponentAdded>(entity, TypeID<Components>::Value(), archetype->GetComponent<Components>(packedLocation))), ...);
            }
            
            return entity;
        }
        
        template<Component... Components, typename Generator>
        void CreateEntities(size_t count, std::span<Entity> outEntities, Generator&& generator)
        {
            if (count == 0 || outEntities.size() < count)
                return;
            
            m_entityPool->CreateBatch(count, outEntities.begin());
            m_archetypeStorage->AddEntities<Components...>(outEntities.subspan(0, count), std::forward<Generator>(generator));
        }

        void DestroyEntity(Entity entity)
        {
            if (!m_entityPool->IsValid(entity))
                return;
            
            // Emit destroy signal before actually destroying
            m_signalManager.Emit<Events::EntityDestroyed>(entity);
            
            m_archetypeStorage->RemoveEntity(entity);
            m_relationshipGraph.OnEntityDestroyed(entity);
            m_entityPool->Destroy(entity);
        }
        
        void DestroyEntities(std::span<Entity> entities)
        {
            if (entities.empty())
                return;
            
            // Filter out invalid entities
            SmallVector<Entity, 16> validEntities;
            // Don't reserve more than reasonable amount to avoid allocation issues
            size_t reserveSize = std::min(entities.size(), size_t(10000));
            validEntities.reserve(reserveSize);
            
            for (Entity entity : entities)
            {
                if (m_entityPool->IsValid(entity))
                {
                    validEntities.push_back(entity);
                }
            }
            
            if (validEntities.empty())
                return;
                
            // Emit destroy signals before destroying
            if (m_signalManager.IsSignalEnabled(Signal::EntityDestroyed))
            {
                for (Entity entity : validEntities)
                {
                    m_signalManager.Emit<Events::EntityDestroyed>(entity);
                }
            }
                
            // Batch remove from storage
            m_archetypeStorage->RemoveEntities(validEntities);
            
            // Remove from relationships
            for (Entity entity : validEntities)
            {
                m_relationshipGraph.OnEntityDestroyed(entity);
            }
            
            // Destroy entities in pool
            for (Entity entity : validEntities)
            {
                m_entityPool->Destroy(entity);
            }
        }

        ASTRA_NODISCARD bool IsValid(Entity entity) const noexcept
        {
            return m_entityPool->IsValid(entity);
        }

        template<Component T, typename... Args>
        T* AddComponent(Entity entity, Args&&... args)
        {
            if (!m_entityPool->IsValid(entity))
                return nullptr;
                
            T* component = m_archetypeStorage->AddComponent<T>(entity, std::forward<Args>(args)...);
            
            // Emit component added signal
            if (component)
            {
                m_signalManager.Emit<Events::ComponentAdded>(entity, TypeID<T>::Value(), component);
            }
            
            return component;
        }

        template<Component T>
        bool RemoveComponent(Entity entity)
        {
            if (!m_entityPool->IsValid(entity))
                return false;
                
            // Get component pointer before removal for signal
            T* component = m_archetypeStorage->GetComponent<T>(entity);
            bool removed = m_archetypeStorage->RemoveComponent<T>(entity);
            
            // Emit component removed signal
            if (removed && component)
            {
                m_signalManager.Emit<Events::ComponentRemoved>(entity, TypeID<T>::Value(), component);
            }
            
            return removed;
        }

        template<Component T>
        ASTRA_NODISCARD T* GetComponent(Entity entity)
        {
            if (!m_entityPool->IsValid(entity))
                return nullptr;
            return m_archetypeStorage->GetComponent<T>(entity);
        }
        
        template<Component T>
        ASTRA_NODISCARD const T* GetComponent(Entity entity) const
        {
            if (!m_entityPool->IsValid(entity))
                return nullptr;
            return m_archetypeStorage->GetComponent<T>(entity);
        }
        
        template<Component T>
        ASTRA_NODISCARD bool HasComponent(Entity entity) const
        {
            if (!m_entityPool->IsValid(entity))
                return false;
            return m_archetypeStorage->HasComponent<T>(entity);
        }

        template<ValidQueryArg... QueryArgs>
        ASTRA_NODISCARD auto CreateView()
        {
            return View<QueryArgs...>(m_archetypeStorage);
        }
        
        void Clear()
        {
            // Emit destroy signals for all entities if enabled
            if (m_signalManager.IsSignalEnabled(Signal::EntityDestroyed))
            {
                // TODO: Consider if we want to emit signals during Clear()
                // For now, we skip signals as Clear() is typically used for bulk cleanup
            }
            
            // Keep the component registry when clearing
            auto componentRegistry = m_archetypeStorage->GetComponentRegistry();
            // Create new storage with the same component registry
            m_archetypeStorage = std::make_shared<ArchetypeStorage>(componentRegistry);
            // Clear relationships
            m_relationshipGraph.Clear();
            // Then clear entity pool
            m_entityPool->Clear();
            
            // Note: We don't clear signal handlers here as they may still be valid
            // for future entities. Users can manually clear handlers if needed.
        }

        ASTRA_NODISCARD std::size_t Size() const noexcept
        {
            return m_entityPool->Size();
        }

        ASTRA_NODISCARD bool IsEmpty() const noexcept
        {
            return Size() == 0;
        }
        
        ASTRA_NODISCARD const ArchetypeStorage& GetStorage() const { return *m_archetypeStorage; }
        ASTRA_NODISCARD ArchetypeStorage& GetStorage() { return *m_archetypeStorage; }

        std::shared_ptr<ComponentRegistry> GetComponentRegistry() const { return m_archetypeStorage->GetComponentRegistry(); }
        
        // ====================== Archetype Cleanup API ======================
        
        // Re-export types from ArchetypeStorage for convenience
        using CleanupOptions = ArchetypeStorage::CleanupOptions;
        using ArchetypeInfo = ArchetypeStorage::ArchetypeInfo;
        
        /**
         * Remove empty archetypes based on specified options
         * 
         * This is a lazy cleanup approach - you control when cleanup happens.
         * Call during loading screens, pause menus, or other convenient times.
         * 
         * @param options Configuration for cleanup behavior
         * @return Number of archetypes removed
         * 
         * Example usage:
         *   // Simple cleanup during loading
         *   registry.CleanupEmptyArchetypes();
         *   
         *   // Incremental cleanup during gameplay
         *   CleanupOptions options;
         *   options.maxArchetypesToRemove = 5;  // Limit per frame
         *   options.minEmptyDuration = 300;     // Empty for 5 seconds
         *   registry.CleanupEmptyArchetypes(options);
         */
        size_t CleanupEmptyArchetypes(const CleanupOptions& options = {})
        {
            // Update metrics before cleanup
            m_archetypeStorage->UpdateArchetypeMetrics();
            
            // Perform cleanup
            return m_archetypeStorage->CleanupEmptyArchetypes(options);
        }
        
        /**
         * Get statistics about all archetypes
         * Useful for debugging and monitoring fragmentation
         */
        ASTRA_NODISCARD std::vector<ArchetypeInfo> GetArchetypeStats() const
        {
            return m_archetypeStorage->GetArchetypeStats();
        }
        
        /**
         * Get current number of archetypes
         */
        ASTRA_NODISCARD size_t GetArchetypeCount() const
        {
            return m_archetypeStorage->GetArchetypeCount();
        }
        
        /**
         * Get approximate memory usage by all archetypes
         */
        ASTRA_NODISCARD size_t GetArchetypeMemoryUsage() const
        {
            return m_archetypeStorage->GetArchetypeMemoryUsage();
        }
        
        // ====================== Relationship API ======================

        template<typename... QueryArgs>
        ASTRA_NODISCARD Relations<QueryArgs...> GetRelations(Entity entity) const
        {
            return Relations<QueryArgs...>(m_archetypeStorage, m_entityPool, entity, &m_relationshipGraph);
        }
        
        /**
         * Set the parent of an entity
         * 
         * @param child The child entity
         * @param parent The parent entity
         */
        void SetParent(Entity child, Entity parent)
        {
            if (m_entityPool->IsValid(child) && m_entityPool->IsValid(parent))
            {
                m_relationshipGraph.SetParent(child, parent);
                
                // Emit parent changed signal
                m_signalManager.Emit<Events::ParentChanged>(child, parent);
            }
        }
        
        /**
         * Remove the parent of an entity
         * 
         * @param child The child entity
         */
        void RemoveParent(Entity child)
        {
            if (m_entityPool->IsValid(child))
            {
                // Get current parent before removal for signal
                Entity parent = m_relationshipGraph.GetParent(child);
                m_relationshipGraph.RemoveParent(child);
                
                // Emit parent changed signal (parent is now invalid)
                if (parent.IsValid())
                {
                    m_signalManager.Emit<Events::ParentChanged>(child, Entity::Invalid());
                }
            }
        }
        
        /**
         * Add a bidirectional link between two entities
         * 
         * @param a First entity
         * @param b Second entity
         */
        void AddLink(Entity a, Entity b)
        {
            if (m_entityPool->IsValid(a) && m_entityPool->IsValid(b))
            {
                m_relationshipGraph.AddLink(a, b);
                
                // Emit link added signal
                m_signalManager.Emit<Events::LinkAdded>(a, b);
            }
        }
        
        /**
         * Remove a link between two entities
         * 
         * @param a First entity
         * @param b Second entity
         */
        void RemoveLink(Entity a, Entity b)
        {
            if (m_entityPool->IsValid(a) && m_entityPool->IsValid(b))
            {
                m_relationshipGraph.RemoveLink(a, b);
                
                // Emit link removed signal
                m_signalManager.Emit<Events::LinkRemoved>(a, b);
            }
        }
        
        /**
         * Get direct access to the relationship graph (internal use)
         * 
         * @return Reference to the relationship graph
         */
        ASTRA_NODISCARD RelationshipGraph& GetRelationshipGraph() { return m_relationshipGraph; }
        ASTRA_NODISCARD const RelationshipGraph& GetRelationshipGraph() const { return m_relationshipGraph; }
        
        // ====================== Signal API ======================
        
        /**
         * Enable signals for specific events
         * 
         * @param signals Bitwise OR of Signal flags to enable
         * 
         * Example:
         *   registry.EnableSignals(Signal::EntityCreated | Signal::ComponentAdded);
         */
        void EnableSignals(Signal signals)
        {
            m_signalManager.EnableSignals(signals);
        }
        
        /**
         * Disable signals for specific events
         * 
         * @param signals Bitwise OR of Signal flags to disable
         */
        void DisableSignals(Signal signals)
        {
            m_signalManager.DisableSignals(signals);
        }
        
        /**
         * Set exact signal configuration
         * 
         * @param signals Exact set of signals to enable (all others will be disabled)
         */
        void SetEnabledSignals(Signal signals)
        {
            m_signalManager.SetEnabledSignals(signals);
        }
        
        /**
         * Get currently enabled signals
         * 
         * @return Bitwise OR of currently enabled Signal flags
         */
        ASTRA_NODISCARD Signal GetEnabledSignals() const
        {
            return m_signalManager.GetEnabledSignals();
        }
        
        /**
         * Get the signal manager for connecting handlers
         * 
         * Example:
         *   auto& signals = registry.GetSignalManager();
         *   signals.OnEntityCreated().Add([](const EntityEvent& e) { ... });
         * 
         * @return Reference to the signal manager
         */
        SignalManager& GetSignalManager()
        {
            return m_signalManager;
        }
        
        const SignalManager& GetSignalManager() const
        {
            return m_signalManager;
        }
        
        // ====================== Serialization API ======================
        
        /**
         * Configuration for saving registry
         */
        struct SaveConfig
        {
            CompressionMode compressionMode = CompressionMode::LZ4;
            Compression::CompressionLevel compressionLevel = Compression::CompressionLevel::Fast;
            size_t compressionThreshold = 1024; // Only compress blocks larger than this
        };
        
        /**
         * Save registry state to file with compression
         * @param path File path to save to
         * @param config Save configuration (compression settings)
         * @return Success or error code
         */
        Result<void, SerializationError> Save(const std::filesystem::path& path, 
                                             const SaveConfig& config = SaveConfig{}) const
        {
            BinaryWriter::Config writerConfig;
            writerConfig.compressionMode = config.compressionMode;
            writerConfig.compressionLevel = config.compressionLevel;
            writerConfig.compressionThreshold = config.compressionThreshold;
            
            BinaryWriter writer(path, writerConfig);
            if (writer.HasError())
            {
                return Result<void, SerializationError>::Err(SerializationError::IOError);
            }
            
            // Write header
            BinaryHeader header;
            header.entityCount = static_cast<uint32_t>(m_entityPool->Size());
            header.archetypeCount = static_cast<uint32_t>(m_archetypeStorage->GetArchetypeCount());
            
            writer.WriteHeader(header);
            
            // Serialize components
            m_entityPool->Serialize(writer);
            m_archetypeStorage->Serialize(writer);
            m_relationshipGraph.Serialize(writer);
            
            // Finalize with checksum
            writer.FinalizeHeader();
            
            return writer.HasError() ? 
                Result<void, SerializationError>::Err(writer.GetError()) : 
                Result<void, SerializationError>::Ok();
        }
        
        /**
         * Save registry state to memory buffer with compression
         * @param config Save configuration (compression settings)
         * @return Buffer containing serialized data or error
         */
        Result<std::vector<std::byte>, SerializationError> Save(const SaveConfig& config = SaveConfig{}) const
        {
            std::vector<std::byte> buffer;
            
            BinaryWriter::Config writerConfig;
            writerConfig.compressionMode = config.compressionMode;
            writerConfig.compressionLevel = config.compressionLevel;
            writerConfig.compressionThreshold = config.compressionThreshold;
            
            BinaryWriter writer(buffer, writerConfig);
            
            // Write header
            BinaryHeader header;
            header.entityCount = static_cast<uint32_t>(m_entityPool->Size());
            header.archetypeCount = static_cast<uint32_t>(m_archetypeStorage->GetArchetypeCount());
            
            writer.WriteHeader(header);
            
            // Serialize components
            m_entityPool->Serialize(writer);
            m_archetypeStorage->Serialize(writer);
            m_relationshipGraph.Serialize(writer);
            
            // Finalize with checksum
            writer.FinalizeHeader();
            
            return writer.HasError() ? 
                Result<std::vector<std::byte>, SerializationError>::Err(writer.GetError()) : 
                Result<std::vector<std::byte>, SerializationError>::Ok(std::move(buffer));
        }
        
        /**
         * Load registry state from file
         * @param path File path to load from
         * @param componentRegistry Pre-configured component registry with all components registered
         * @return New registry instance or error
         */
        static Result<std::unique_ptr<Registry>, SerializationError> Load(
            const std::filesystem::path& path,
            std::shared_ptr<ComponentRegistry> componentRegistry)
        {
            BinaryReader reader(path);
            if (reader.HasError())
            {
                return Result<std::unique_ptr<Registry>, SerializationError>::Err(SerializationError::IOError);
            }
            
            return LoadInternal(reader, std::move(componentRegistry));
        }
        
        /**
         * Load registry state from memory buffer
         * @param data Buffer containing serialized data
         * @param componentRegistry Pre-configured component registry with all components registered
         * @return New registry instance or error
         */
        static Result<std::unique_ptr<Registry>, SerializationError> Load(
            std::span<const std::byte> data,
            std::shared_ptr<ComponentRegistry> componentRegistry)
        {
            BinaryReader reader(data);
            return LoadInternal(reader, std::move(componentRegistry));
        }
        
    private:
        /**
         * Internal helper to load registry from reader
         */
        static Result<std::unique_ptr<Registry>, SerializationError> LoadInternal(
            BinaryReader& reader,
            std::shared_ptr<ComponentRegistry> componentRegistry)
        {
            // Read and validate header
            auto headerResult = reader.ReadHeader();
            if (headerResult.IsErr())
            {
                return Result<std::unique_ptr<Registry>, SerializationError>::Err(*headerResult.GetError());
            }
            
            // Deserialize EntityPool
            auto poolResult = EntityPool::Deserialize(reader);
            if (poolResult.IsErr())
            {
                return Result<std::unique_ptr<Registry>, SerializationError>::Err(*poolResult.GetError());
            }
            
            // Create new registry instance with the provided component registry
            auto registry = std::make_unique<Registry>(componentRegistry);
            
            // Set the entity pool (convert unique_ptr to shared_ptr)
            registry->m_entityPool = std::move(*poolResult.GetValue());
            
            // Create new ArchetypeStorage with the component registry and deserialize into it
            registry->m_archetypeStorage = std::make_shared<ArchetypeStorage>(componentRegistry);
            if (!registry->m_archetypeStorage->Deserialize(reader))
            {
                return Result<std::unique_ptr<Registry>, SerializationError>::Err(SerializationError::CorruptedData);
            }
            
            // Deserialize RelationshipGraph
            auto graphResult = RelationshipGraph::Deserialize(reader);
            if (graphResult.IsErr())
            {
                return Result<std::unique_ptr<Registry>, SerializationError>::Err(*graphResult.GetError());
            }
            registry->m_relationshipGraph = std::move(*graphResult.GetValue());
            
            // Verify checksum
            auto checksumResult = reader.VerifyChecksum();
            if (checksumResult.IsErr())
            {
                return Result<std::unique_ptr<Registry>, SerializationError>::Err(*checksumResult.GetError());
            }
            
            return Result<std::unique_ptr<Registry>, SerializationError>::Ok(std::move(registry));
        }
        
    private:
        std::shared_ptr<EntityPool> m_entityPool;
        std::shared_ptr<ArchetypeStorage> m_archetypeStorage;
        RelationshipGraph m_relationshipGraph;
        SignalManager m_signalManager;
    };
}