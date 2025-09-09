#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>
#include "Astra/Component/Component.hpp"

namespace Astra::Test
{
    // 1. Basic position component (trivially copyable)
    struct Position
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        
        Position() = default;
        Position(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    };
    
    // 2. Velocity component (trivially copyable)
    struct Velocity
    {
        float dx = 0.0f;
        float dy = 0.0f;
        float dz = 0.0f;
        
        Velocity() = default;
        Velocity(float dx_, float dy_, float dz_) : dx(dx_), dy(dy_), dz(dz_) {}
    };
    
    // 3. Health component with logic
    struct Health
    {
        int current = 100;
        int max = 100;
        float regeneration = 0.0f;
        
        Health() = default;
        Health(int current_, int max_) : current(current_), max(max_) {}
        Health(int current_, int max_, float regen) : current(current_), max(max_), regeneration(regen) {}
        
        bool IsDead() const { return current <= 0; }
        float GetHealthPercent() const { return max > 0 ? float(current) / float(max) : 0.0f; }
    };
    
    // 4. Transform with matrix (larger trivially copyable)
    struct Transform
    {
        float matrix[16] =
        {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
        };
        
        Transform() = default;
    };
    
    // 5. Non-trivially copyable string component
    struct Name
    {
        std::string value;
        
        Name() = default;
        Name(const std::string& v) : value(v) {}
        Name(const char* v) : value(v) {}
        
        // Serialization support
        template<typename Archive>
        void Serialize(Archive& ar)
        {
            ar(value);
        }
    };
    
    // 6. Component with dynamic container
    struct Inventory
    {
        std::vector<int> items;
        int maxCapacity = 20;
        
        Inventory() = default;
        Inventory(int capacity) : maxCapacity(capacity) { items.reserve(capacity); }
        
        bool AddItem(int itemId)
        {
            if (static_cast<int>(items.size()) < maxCapacity)
            {
                items.push_back(itemId);
                return true;
            }
            return false;
        }
        
        // Serialization support
        template<typename Archive>
        void Serialize(Archive& ar)
        {
            ar(items)(maxCapacity);
        }
    };
    
    // 7. Move-only component with unique_ptr
    struct Resource
    {
        std::unique_ptr<int> data;
        
        Resource() : data(std::make_unique<int>(0)) {}
        
        // Serialization support for move-only type
        template<typename Archive>
        void Serialize(Archive& ar)
        {
            if (ar.IsLoading())
            {
                int value;
                ar(value);
                if (value >= 0)
                    data = std::make_unique<int>(value);
                else
                    data.reset();
            }
            else
            {
                int value = data ? *data : -1;
                ar(value);
            }
        }
        Resource(int val) : data(std::make_unique<int>(val)) {}
        Resource(Resource&&) = default;
        Resource& operator=(Resource&&) = default;
        ~Resource() = default;
        
        // Delete copy operations
        Resource(const Resource&) = delete;
        Resource& operator=(const Resource&) = delete;
    };
    
    // 8. Empty tag component - Player marker
    struct Player {};
    
    // 9. Empty tag component - Enemy marker  
    struct Enemy {};
    
    // 10. Empty tag component - Static object marker
    struct Static {};
    
    // 11. Component with special alignment
    struct alignas(32) RenderData
    {
        float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float uv[4] = {0.0f, 0.0f, 1.0f, 1.0f};
        int textureId = -1;
        int shaderId = 0;
        bool visible = true;
        
        RenderData() = default;
    };
    
    // 12. Physics component
    struct Physics
    {
        float mass = 1.0f;
        float friction = 0.5f;
        float restitution = 0.3f;
        bool isKinematic = false;
        bool useGravity = true;
        
        Physics() = default;
        Physics(float m, bool kinematic = false) : mass(m), isKinematic(kinematic) {}
    };
    
    // 13. Timer/cooldown component
    struct Timer
    {
        float elapsed = 0.0f;
        float duration = 1.0f;
        bool loop = false;
        bool paused = false;
        
        Timer() = default;
        Timer(float dur, bool l = false) : duration(dur), loop(l) {}
        
        bool IsExpired() const { return !paused && elapsed >= duration; }
        float GetProgress() const { return duration > 0 ? elapsed / duration : 1.0f; }
    };
    
    // 14. Relationship/hierarchy component
    struct Hierarchy
    {
        uint32_t parent = 0;
        uint32_t firstChild = 0;
        uint32_t nextSibling = 0;
        uint32_t childCount = 0;
        
        Hierarchy() = default;
    };
    
    // 15. Complex component with multiple containers
    struct Metadata
    {
        std::string description;
        std::vector<std::string> tags;
        std::unique_ptr<std::array<float, 16>> customData;
        int priority = 0;
        bool active = true;
        
        Metadata() = default;
        Metadata(const std::string& desc) : description(desc) {}
        
        void AddTag(const std::string& tag)
        {
            tags.push_back(tag);
        }
        
        bool HasTag(const std::string& tag) const
        {
            return std::find(tags.begin(), tags.end(), tag) != tags.end();
        }
        
        // Move operations
        Metadata(Metadata&&) = default;
        Metadata& operator=(Metadata&&) = default;
        
        // Copy operations need custom implementation due to unique_ptr
        Metadata(const Metadata& other) 
            : description(other.description)
            , tags(other.tags)
            , priority(other.priority)
            , active(other.active)
        {
            if (other.customData)
            {
                customData = std::make_unique<std::array<float, 16>>(*other.customData);
            }
        }
        
        Metadata& operator=(const Metadata& other)
        {
            if (this != &other)
            {
                description = other.description;
                tags = other.tags;
                priority = other.priority;
                active = other.active;
                if (other.customData)
                {
                    customData = std::make_unique<std::array<float, 16>>(*other.customData);
                }
                else
                {
                    customData.reset();
                }
            }
            return *this;
        }
    };
    
    // 16. Damage/combat component
    struct Damage
    {
        enum class Type : uint8_t
        {
            Physical,
            Fire,
            Ice,
            Electric,
            Poison
        };
        
        float amount = 10.0f;
        Type type = Type::Physical;
        float criticalChance = 0.1f;
        float criticalMultiplier = 2.0f;
        bool canCrit = true;
        
        Damage() = default;
        Damage(float amt, Type t = Type::Physical) : amount(amt), type(t) {}
        
        float CalculateDamage(float randomValue = 0.5f) const
        {
            if (canCrit && randomValue < criticalChance)
            {
                return amount * criticalMultiplier;
            }
            return amount;
        }
    };
    
    // Helper type traits for testing
    template<typename T>
    struct ComponentTraits
    {
        static constexpr bool is_empty = std::is_empty_v<T>;
        static constexpr bool is_trivially_copyable = std::is_trivially_copyable_v<T>;
        static constexpr bool is_move_only = !std::is_copy_constructible_v<T> && std::is_move_constructible_v<T>;
        static constexpr size_t size = sizeof(T);
        static constexpr size_t alignment = alignof(T);
    };
    
    // Validate that all test components satisfy the Astra Component concept
    static_assert(Component<Position>, "Position must satisfy Component concept");
    static_assert(Component<Velocity>, "Velocity must satisfy Component concept");
    static_assert(Component<Health>, "Health must satisfy Component concept");
    static_assert(Component<Transform>, "Transform must satisfy Component concept");
    static_assert(Component<Name>, "Name must satisfy Component concept");
    static_assert(Component<Inventory>, "Inventory must satisfy Component concept");
    static_assert(Component<Resource>, "Resource must satisfy Component concept");
    static_assert(Component<Player>, "Player must satisfy Component concept");
    static_assert(Component<Enemy>, "Enemy must satisfy Component concept");
    static_assert(Component<Static>, "Static must satisfy Component concept");
    static_assert(Component<RenderData>, "RenderData must satisfy Component concept");
    static_assert(Component<Physics>, "Physics must satisfy Component concept");
    static_assert(Component<Timer>, "Timer must satisfy Component concept");
    static_assert(Component<Hierarchy>, "Hierarchy must satisfy Component concept");
    static_assert(Component<Metadata>, "Metadata must satisfy Component concept");
    static_assert(Component<Damage>, "Damage must satisfy Component concept");
    
    // Additional validation for specific properties we care about in tests
    static_assert(std::is_trivially_copyable_v<Position>, "Position should be trivially copyable");
    static_assert(!std::is_trivially_copyable_v<Name>, "Name should not be trivially copyable");
    static_assert(!std::is_copy_constructible_v<Resource>, "Resource should be move-only");
    static_assert(std::is_empty_v<Player> && std::is_empty_v<Enemy> && std::is_empty_v<Static>, "Tag components should be empty");
    static_assert(alignof(RenderData) == 32, "RenderData should be 32-byte aligned");
    
} // namespace Astra::Test
