#pragma once

#include "CommandTypes.hpp"
#include "../Container/SmallVector.hpp"
#include <tuple>
#include <vector>
#include <cstdint>

namespace Astra
{
    // Forward declaration
    class Registry;
    
    namespace Commands
    {
        /**
         * SOA (Structure of Arrays) storage for commands
         * Each command type is stored in its own contiguous vector for cache efficiency
         * @tparam CommandTypes... The command types this storage supports
         */
        template<typename... CommandTypes>
        class CommandStorage
        {
        private:
            // One vector per command type (SOA layout)
            using StorageTuple = std::tuple<std::vector<CommandTypes>...>;
            StorageTuple m_commands;
            
            // Track execution order (which type index, which command index within that type)
            struct ExecutionEntry
            {
                uint16_t typeIndex;    // Index into the tuple (which command type)
                uint32_t commandIndex; // Index within that type's vector
            };
            SmallVector<ExecutionEntry, 256> m_executionOrder;
            
            // Helper to get the index of a type in the tuple at compile time
            template<typename T>
            static constexpr uint16_t GetTypeIndex()
            {
                return GetTypeIndexImpl<T, 0>();
            }
            
            template<typename T, size_t I>
            static constexpr uint16_t GetTypeIndexImpl()
            {
                if constexpr (I >= sizeof...(CommandTypes))
                {
                    static_assert(I < sizeof...(CommandTypes), "Type not found in command storage");
                    return uint16_t(-1);
                }
                else if constexpr (std::is_same_v<std::decay_t<T>, typename std::tuple_element_t<I, StorageTuple>::value_type>)
                {
                    return static_cast<uint16_t>(I);
                }
                else
                {
                    return GetTypeIndexImpl<T, I + 1>();
                }
            }
            
        public:
            CommandStorage() = default;
            
            /**
             * Add a command to storage
             * @tparam Cmd The command type (must be in CommandTypes...)
             * @param command The command to add
             */
            template<typename Cmd>
            void Add(Cmd&& command)
            {
                static_assert((std::is_same_v<std::decay_t<Cmd>, CommandTypes> || ...), 
                             "Command type not supported by this storage");
                
                // Get the vector for this command type
                auto& vec = std::get<std::vector<std::decay_t<Cmd>>>(m_commands);
                uint32_t index = static_cast<uint32_t>(vec.size());
                vec.push_back(std::forward<Cmd>(command));
                
                // Track execution order
                constexpr uint16_t typeIdx = GetTypeIndex<std::decay_t<Cmd>>();
                m_executionOrder.push_back({typeIdx, index});
            }
            
            /**
             * Batch add multiple commands of the same type
             * @tparam Cmd The command type
             * @param commands Span of commands to add
             */
            template<typename Cmd>
            void AddBatch(std::span<const Cmd> commands)
            {
                static_assert((std::is_same_v<Cmd, CommandTypes> || ...), 
                             "Command type not supported by this storage");
                
                auto& vec = std::get<std::vector<Cmd>>(m_commands);
                uint32_t startIndex = static_cast<uint32_t>(vec.size());
                vec.insert(vec.end(), commands.begin(), commands.end());
                
                // Track execution order for all added commands
                constexpr uint16_t typeIdx = GetTypeIndex<Cmd>();
                for (size_t i = 0; i < commands.size(); ++i)
                {
                    m_executionOrder.push_back({typeIdx, static_cast<uint32_t>(startIndex + i)});
                }
            }
            
            /**
             * Clear all stored commands
             */
            void Clear()
            {
                // Clear each vector in the tuple
                std::apply([](auto&... vecs) { (vecs.clear(), ...); }, m_commands);
                m_executionOrder.clear();
            }
            
            /**
             * Reserve space for expected number of commands
             * @param expectedCommands Expected total command count
             */
            void Reserve(size_t expectedCommands)
            {
                m_executionOrder.reserve(expectedCommands);
                // Could also reserve in individual vectors if we knew distribution
            }
            
            /**
             * Get total number of commands
             */
            [[nodiscard]] size_t GetTotalCommands() const noexcept
            {
                return m_executionOrder.size();
            }
            
            /**
             * Get count of specific command type
             * @tparam Cmd The command type to count
             */
            template<typename Cmd>
            [[nodiscard]] size_t GetCommandCount() const noexcept
            {
                return std::get<std::vector<Cmd>>(m_commands).size();
            }
            
            /**
             * Check if storage is empty
             */
            [[nodiscard]] bool IsEmpty() const noexcept
            {
                return m_executionOrder.empty();
            }
            
            /**
             * Get memory usage estimate in bytes
             */
            [[nodiscard]] size_t GetMemoryUsage() const noexcept
            {
                size_t total = m_executionOrder.size() * sizeof(ExecutionEntry);
                
                auto calculateSize = [&total](const auto& vec) {
                    using T = typename std::decay_t<decltype(vec)>::value_type;
                    total += vec.capacity() * sizeof(T);
                };
                
                std::apply([&](const auto&... vecs) { (calculateSize(vecs), ...); }, m_commands);
                
                return total;
            }
            
            /**
             * Merge another storage into this one
             * @param other Storage to merge (will be moved from)
             */
            void Merge(CommandStorage&& other)
            {
                MergeImpl<0>(std::move(other));
                
                // Append execution order
                m_executionOrder.reserve(m_executionOrder.size() + other.m_executionOrder.size());
                for (auto& entry : other.m_executionOrder)
                {
                    m_executionOrder.push_back(std::move(entry));
                }
                other.m_executionOrder.clear();
            }
            
        private:
            template<size_t I>
            void MergeImpl(CommandStorage&& other)
            {
                if constexpr (I < sizeof...(CommandTypes))
                {
                    auto& myVec = std::get<I>(m_commands);
                    auto& otherVec = std::get<I>(other.m_commands);
                    
                    // Update indices in other's execution order for this type
                    uint32_t oldSize = static_cast<uint32_t>(myVec.size());
                    for (auto& entry : other.m_executionOrder)
                    {
                        if (entry.typeIndex == I)
                        {
                            entry.commandIndex += oldSize;
                        }
                    }
                    
                    // Merge vectors
                    myVec.insert(
                        myVec.end(),
                        std::make_move_iterator(otherVec.begin()),
                        std::make_move_iterator(otherVec.end())
                    );
                    
                    MergeImpl<I + 1>(std::move(other));
                }
            }
            
            // Friend the executor to access internals
            template<typename...>
            friend class CommandExecutor;
            
            // Provide access to execution order and commands for the executor
            const auto& GetExecutionOrder() const { return m_executionOrder; }
            auto& GetCommands() { return m_commands; }
            const auto& GetCommands() const { return m_commands; }
        };
        
    } // namespace Commands
} // namespace Astra