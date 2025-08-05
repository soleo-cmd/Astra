#pragma once

#include <concepts>
#include <type_traits>

#include "../Core/Base.hpp"
#include "../Registry/Registry.hpp"

namespace Astra
{
    /**
     * @brief Concept that defines what constitutes a System in Astra ECS
     * 
     * A System is any callable object that can operate on a Registry.
     * This includes:
     * - Function objects (functors) with operator()
     * - Lambdas
     * - Function pointers
     * - std::function
     * 
     * The only requirement is that it must be invocable with a Registry& parameter.
     */
    template<typename T>
    concept System = requires(T system, Registry& registry)
    {
        { system(registry) } -> std::same_as<void>;
    };
}