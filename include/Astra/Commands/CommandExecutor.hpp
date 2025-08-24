#pragma once

#include "CommandTypes.hpp"
#include "CommandStorage.hpp"
#include "../Registry/Registry.hpp"
#include "../Container/FlatMap.hpp"
#include <tuple>
#include <type_traits>

namespace Astra::Commands
{
    /**
     * Executes commands stored in CommandStorage on a Registry
     * @tparam CommandTypes... The command types this executor supports
     */
    template<typename... CommandTypes>
    class CommandExecutor
    {
    private:
        Registry* m_registry;
        
        // Map temporary entity IDs to real entity IDs for deferred entity creation
        FlatMap<Entity, Entity> m_entityRemapping;
        
    public:
        explicit CommandExecutor(Registry* registry)
            : m_registry(registry)
        {
            ASTRA_ASSERT(registry != nullptr, "Registry cannot be null");
        }
        
        /**
         * Execute all commands in the storage
         * @param storage The command storage containing commands to execute
         * @param clearAfterExecution Whether to clear the storage after execution (default: true)
         */
        void Execute(CommandStorage<CommandTypes...>& storage, bool clearAfterExecution = true)
        {
            const auto& executionOrder = storage.GetExecutionOrder();
            auto& commands = storage.GetCommands();
            
            // Process commands in execution order
            for (const auto& entry : executionOrder)
            {
                ExecuteCommandAtIndex(commands, entry.typeIndex, entry.commandIndex);
            }
            
            // Clear entity remapping after execution
            m_entityRemapping.Clear();
            
            // Clear storage by default for reuse
            if (clearAfterExecution)
            {
                storage.Clear();
            }
        }
        
        /**
         * Get the number of entities in the remapping table
         * Used for debugging and statistics
         */
        [[nodiscard]] size_t GetRemappingCount() const noexcept
        {
            return m_entityRemapping.Size();
        }
        
        /**
         * Clear the entity remapping table
         * Call this if you want to reset without executing
         */
        void ClearRemapping()
        {
            m_entityRemapping.Clear();
        }
        
        /**
         * Resolve a potentially temporary entity ID to a real entity ID
         * @param entity The entity to resolve (may be temporary or real)
         * @return The real entity ID, or Invalid if not found
         */
        Entity ResolveEntity(Entity entity) const
        {
            // Check if this is a temporary entity that needs remapping
            if (auto it = m_entityRemapping.Find(entity); it != m_entityRemapping.end())
            {
                return it->second;
            }
            
            // Otherwise assume it's a real entity
            return entity;
        }
        
    private:
        // Execute a specific command by type index and command index
        void ExecuteCommandAtIndex(auto& commands, uint16_t typeIndex, uint32_t commandIndex)
        {
            ExecuteCommandAtIndexImpl<0>(commands, typeIndex, commandIndex);
        }
        
        template<size_t I>
        void ExecuteCommandAtIndexImpl(auto& commands, uint16_t typeIndex, uint32_t commandIndex)
        {
            if constexpr (I < sizeof...(CommandTypes))
            {
                if (typeIndex == I)
                {
                    using CommandType = std::tuple_element_t<I, std::tuple<CommandTypes...>>;
                    auto& vec = std::get<I>(commands);
                    ExecuteCommand(vec[commandIndex]);
                }
                else
                {
                    ExecuteCommandAtIndexImpl<I + 1>(commands, typeIndex, commandIndex);
                }
            }
        }
        
        // ============= Entity Command Execution =============
        
        void ExecuteCommand(const CreateEntity& cmd)
        {
            Entity realEntity = m_registry->CreateEntity();
            if (cmd.tempEntity != Entity::Invalid())
            {
                m_entityRemapping[cmd.tempEntity] = realEntity;
            }
        }
        
        void ExecuteCommand(const DestroyEntity& cmd)
        {
            Entity entity = ResolveEntity(cmd.entity);
            if (entity != Entity::Invalid())
            {
                m_registry->DestroyEntity(entity);
            }
        }
        
        void ExecuteCommand(const CreateEntities& cmd)
        {
            // Create entities directly into the output buffer
            m_registry->CreateEntities(cmd.count, std::span<Entity>(cmd.outEntities, cmd.count));
        }
        
        void ExecuteCommand(const DestroyEntities& cmd)
        {
            // Resolve entity IDs and destroy
            SmallVector<Entity, 256> resolvedEntities;
            resolvedEntities.reserve(cmd.entities.size());
            
            for (Entity e : cmd.entities)
            {
                Entity resolved = ResolveEntity(e);
                if (resolved != Entity::Invalid())
                {
                    resolvedEntities.push_back(resolved);
                }
            }
            
            m_registry->DestroyEntities(resolvedEntities);
        }
        
        // ============= Component Command Execution =============
        
        template<typename T>
        void ExecuteCommand(const AddComponent<T>& cmd)
        {
            Entity entity = ResolveEntity(cmd.entity);
            if (entity != Entity::Invalid())
            {
                m_registry->AddComponent<T>(entity, cmd.component);
            }
        }
        
        template<typename T>
        void ExecuteCommand(const RemoveComponent<T>& cmd)
        {
            Entity entity = ResolveEntity(cmd.entity);
            if (entity != Entity::Invalid())
            {
                m_registry->RemoveComponent<T>(entity);
            }
        }
        
        template<typename T>
        void ExecuteCommand(const SetComponent<T>& cmd)
        {
            Entity entity = ResolveEntity(cmd.entity);
            if (entity != Entity::Invalid())
            {
                if (T* component = m_registry->GetComponent<T>(entity))
                {
                    *component = cmd.component;
                }
                else
                {
                    // If component doesn't exist, add it
                    m_registry->AddComponent<T>(entity, cmd.component);
                }
            }
        }
        
        template<typename T>
        void ExecuteCommand(const AddComponents<T>& cmd)
        {
            // Resolve entity IDs
            SmallVector<Entity, 256> resolvedEntities;
            resolvedEntities.reserve(cmd.entities.size());
            
            for (Entity e : cmd.entities)
            {
                Entity resolved = ResolveEntity(e);
                if (resolved != Entity::Invalid())
                {
                    resolvedEntities.push_back(resolved);
                }
            }
            
            m_registry->AddComponents<T>(resolvedEntities, cmd.component);
        }
        
        template<typename T>
        void ExecuteCommand(const RemoveComponents<T>& cmd)
        {
            // Resolve entity IDs
            SmallVector<Entity, 256> resolvedEntities;
            resolvedEntities.reserve(cmd.entities.size());
            
            for (Entity e : cmd.entities)
            {
                Entity resolved = ResolveEntity(e);
                if (resolved != Entity::Invalid())
                {
                    resolvedEntities.push_back(resolved);
                }
            }
            
            m_registry->RemoveComponents<T>(resolvedEntities);
        }
        
        // ============= Relationship Command Execution =============
        
        void ExecuteCommand(const SetParent& cmd)
        {
            Entity child = ResolveEntity(cmd.child);
            Entity parent = ResolveEntity(cmd.parent);
            
            if (child != Entity::Invalid() && parent != Entity::Invalid())
            {
                m_registry->SetParent(child, parent);
            }
        }
        
        void ExecuteCommand(const RemoveParent& cmd)
        {
            Entity child = ResolveEntity(cmd.child);
            
            if (child != Entity::Invalid())
            {
                m_registry->RemoveParent(child);
            }
        }
        
        void ExecuteCommand(const AddLink& cmd)
        {
            Entity a = ResolveEntity(cmd.a);
            Entity b = ResolveEntity(cmd.b);
            
            if (a != Entity::Invalid() && b != Entity::Invalid())
            {
                m_registry->AddLink(a, b);
            }
        }
        
        void ExecuteCommand(const RemoveLink& cmd)
        {
            Entity a = ResolveEntity(cmd.a);
            Entity b = ResolveEntity(cmd.b);
            
            if (a != Entity::Invalid() && b != Entity::Invalid())
            {
                m_registry->RemoveLink(a, b);
            }
        }
        
    };
    
} // namespace Astra::Commands