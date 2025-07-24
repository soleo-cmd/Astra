#pragma once

#include <cstdint>
#include <functional>

namespace Astra
{
    enum class ErrorCode : std::uint32_t
    {
        None = 0,
        
        InvalidArgument,
        OutOfBounds,
        NotFound,
        AlreadyExists,
        InvalidState,
        
        AllocationFailed,
        OutOfMemory,
        
        EntityNotFound,
        ComponentNotFound,
        InvalidEntity,
        InvalidComponent,
        
        CapacityExceeded,
        ContainerFull,
        ContainerEmpty,
        
        Unknown = 0xFFFFFFFF
    };
    
    struct Error
    {
        ErrorCode code;
        const char* message;
        
        constexpr Error(ErrorCode c = ErrorCode::None, const char* msg = nullptr) noexcept
            : code(c), message(msg ? msg : GetDefaultMessage(c))
        {}
        
        [[nodiscard]] constexpr bool operator==(const Error& other) const noexcept
        {
            return code == other.code;
        }
        
        [[nodiscard]] constexpr bool operator!=(const Error& other) const noexcept
        {
            return code != other.code;
        }
        
        [[nodiscard]] static constexpr const char* GetDefaultMessage(ErrorCode code) noexcept
        {
            switch (code)
            {
                case ErrorCode::None: return "No error";
                case ErrorCode::InvalidArgument: return "Invalid argument";
                case ErrorCode::OutOfBounds: return "Index out of bounds";
                case ErrorCode::NotFound: return "Item not found";
                case ErrorCode::AlreadyExists: return "Item already exists";
                case ErrorCode::InvalidState: return "Invalid state";
                case ErrorCode::AllocationFailed: return "Allocation failed";
                case ErrorCode::OutOfMemory: return "Out of memory";
                case ErrorCode::EntityNotFound: return "Entity not found";
                case ErrorCode::ComponentNotFound: return "Component not found";
                case ErrorCode::InvalidEntity: return "Invalid entity";
                case ErrorCode::InvalidComponent: return "Invalid component";
                case ErrorCode::CapacityExceeded: return "Capacity exceeded";
                case ErrorCode::ContainerFull: return "Container is full";
                case ErrorCode::ContainerEmpty: return "Container is empty";
                case ErrorCode::Unknown: return "Unknown error";
                default: return "Unspecified error";
            }
        }
    };
    
    inline constexpr Error MakeError(ErrorCode code, const char* message = nullptr) noexcept
    {
        return Error(code, message);
    }
}

namespace std
{
    template<>
    struct hash<Astra::Error>
    {
        std::size_t operator()(const Astra::Error& e) const noexcept
        {
            return std::hash<std::uint32_t>{}(static_cast<std::uint32_t>(e.code));
        }
    };
}