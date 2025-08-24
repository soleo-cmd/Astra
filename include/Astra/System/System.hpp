#pragma once

#include <concepts>
#include <type_traits>
#include <tuple>

#include "../Core/Base.hpp"
#include "../Component/Component.hpp"
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
    
    /**
     * @brief Helper template to define component access patterns for systems
     * 
     * Systems can optionally inherit from this to declare their component dependencies.
     * This allows the scheduler to automatically determine parallelization opportunities.
     * 
     * Example:
     * @code
     * struct PhysicsSystem : SystemTraits<
     *     Reads<Velocity>,
     *     Writes<Position>
     * > {
     *     void operator()(Registry& registry) {
     *         // Implementation
     *     }
     * };
     * @endcode
     */
    template<typename... Components>
    struct Reads { using type = std::tuple<Components...>; };
    
    template<typename... Components>
    struct Writes { using type = std::tuple<Components...>; };
    
    template<typename... Traits>
    struct SystemTraits {};
    
    // Specialization for systems with reads and writes
    template<typename... ReadComponents, typename... WriteComponents>
    struct SystemTraits<Reads<ReadComponents...>, Writes<WriteComponents...>>
    {
        using ReadsComponents = std::tuple<ReadComponents...>;
        using WritesComponents = std::tuple<WriteComponents...>;
        static constexpr bool has_traits = true;
    };
    
    // Specialization for systems with only reads
    template<typename... ReadComponents>
    struct SystemTraits<Reads<ReadComponents...>>
    {
        using ReadsComponents = std::tuple<ReadComponents...>;
        using WritesComponents = std::tuple<>;
        static constexpr bool has_traits = true;
    };
    
    // Specialization for systems with only writes
    template<typename... WriteComponents>
    struct SystemTraits<Writes<WriteComponents...>>
    {
        using ReadsComponents = std::tuple<>;
        using WritesComponents = std::tuple<WriteComponents...>;
        static constexpr bool has_traits = true;
    };
    
    // Type trait to detect if a system has component traits
    template<typename T>
    struct HasSystemTraits : std::false_type {};
    
    template<typename T>
    requires requires { typename T::ReadsComponents; typename T::WritesComponents; T::has_traits; }
    struct HasSystemTraits<T> : std::true_type {};
    
    template<typename T>
    inline constexpr bool HasSystemTraits_v = HasSystemTraits<T>::value;
    
    /**
     * @brief Wrapper for lambda-based systems with automatic trait deduction
     * 
     * This wrapper takes a lambda that operates on Entity + Components and:
     * 1. Automatically creates the appropriate View
     * 2. Deduces read/write access from parameter const-ness
     * 3. Implements the System interface
     * 
     * @tparam Lambda The lambda function type
     * @tparam Args The lambda parameter types (Entity + Components)
     */
    template<typename Lambda, typename... Args>
    class LambdaSystemWrapper
    {
    private:
        Lambda m_lambda;
        
        // Helper to check if a type is const
        template<typename T>
        static constexpr bool IsReadOnly = std::is_const_v<std::remove_reference_t<T>>;
        
        // Helper to remove const/ref qualifiers
        template<typename T>
        using BaseType = std::remove_const_t<std::remove_reference_t<T>>;
        
        // Skip the first parameter (Entity) when processing components
        template<typename First, typename... Rest>
        struct SkipFirst
        {
            using Components = std::tuple<Rest...>;
        };
        
        using ComponentArgs = typename SkipFirst<Args...>::Components;
        
        // Extract read components (const parameters)
        template<typename Tuple, size_t... Is>
        static auto ExtractReads(std::index_sequence<Is...>)
        {
            return std::tuple_cat(
                std::conditional_t<
                    IsReadOnly<std::tuple_element_t<Is, Tuple>>,
                    std::tuple<BaseType<std::tuple_element_t<Is, Tuple>>>,
                    std::tuple<>
                >{}...
            );
        }
        
        // Extract write components (non-const parameters)
        template<typename Tuple, size_t... Is>
        static auto ExtractWrites(std::index_sequence<Is...>)
        {
            return std::tuple_cat(
                std::conditional_t<
                    !IsReadOnly<std::tuple_element_t<Is, Tuple>>,
                    std::tuple<BaseType<std::tuple_element_t<Is, Tuple>>>,
                    std::tuple<>
                >{}...
            );
        }
        
        static constexpr auto MakeIndexSeq()
        {
            return std::make_index_sequence<std::tuple_size_v<ComponentArgs>>{};
        }
        
    public:
        // Expose deduced traits
        using ReadsComponents = decltype(ExtractReads<ComponentArgs>(MakeIndexSeq()));
        using WritesComponents = decltype(ExtractWrites<ComponentArgs>(MakeIndexSeq()));
        static constexpr bool has_traits = true;
        
        explicit LambdaSystemWrapper(Lambda lambda) : m_lambda(std::move(lambda)) {}
        
        // Implement the System interface
        void operator()(Registry& registry)
        {
            // Extract component types without const/ref
            ExtractAndExecute<Args...>(registry);
        }
        
    private:
        template<typename First, typename... Components>
        void ExtractAndExecute(Registry& registry)
        {
            static_assert(std::is_same_v<BaseType<First>, Entity>, 
                         "First parameter must be Entity");
            
            // Create view preserving const-ness of components
            // Convert const T& to const T, and T& to T
            auto view = registry.CreateView<std::conditional_t<
                IsReadOnly<Components>,
                const BaseType<Components>,
                BaseType<Components>
            >...>();
            view.ForEach(m_lambda);
        }
    };
    
    /**
     * @brief Helper to check if a type is a lambda-like callable
     * 
     * Detects if a type has operator() but is not invocable with Registry&
     */
    template<typename T>
    concept LambdaLike = requires {
        &T::operator();  // Has operator()
    } && !std::invocable<T, Registry&>;  // But not a traditional system
}