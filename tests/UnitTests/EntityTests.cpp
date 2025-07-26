#include <catch2/catch_all.hpp>
#include <Astra/Entity/Entity.hpp>
#include <unordered_map>
#include <unordered_set>
#include <set>

using namespace Astra;

TEST_CASE("Entity Construction and Properties", "[Entity]")
{
    SECTION("Default construction creates null entity")
    {
        Entity e;
        REQUIRE(!e.Valid());
        REQUIRE(e == Entity::Null());
        REQUIRE(e.Value() == EntityTraits32::NULL_VALUE);
        REQUIRE(e.Value() == std::numeric_limits<std::uint32_t>::max());
    }
    
    SECTION("Value construction")
    {
        Entity e(42);
        REQUIRE(e.Value() == 42);
        REQUIRE(e.Valid()); // 42 != NULL_VALUE, so it's valid
    }
    
    SECTION("Index and Version construction")
    {
        Entity e(100, 5);
        REQUIRE(e.Index() == 100);
        REQUIRE(e.Version() == 5);
        REQUIRE(e.Valid());
        
        // Test bit layout: version in upper 8 bits, index in lower 24 bits
        std::uint32_t expectedValue = (static_cast<std::uint32_t>(5) << 24) | 100;
        REQUIRE(e.Value() == expectedValue);
    }
    
    SECTION("Maximum index values")
    {
        // Max index is 24 bits = 0xFFFFFF
        Entity maxIndex(0xFFFFFF, 0);
        REQUIRE(maxIndex.Index() == 0xFFFFFF);
        REQUIRE(maxIndex.Version() == 0);
        
        // Index should be masked to 24 bits
        Entity overflowIndex(0x1FFFFFF, 0); // 25 bits
        REQUIRE(overflowIndex.Index() == 0xFFFFFF); // Masked to 24 bits
    }
    
    SECTION("Maximum version values")
    {
        // Max version is 8 bits = 0xFF
        Entity maxVersion(0, 0xFF);
        REQUIRE(maxVersion.Index() == 0);
        REQUIRE(maxVersion.Version() == 0xFF);
        
        // Version should be masked to 8 bits
        Entity overflowVersion(0, static_cast<std::uint8_t>(0x1FF)); // 9 bits
        REQUIRE(overflowVersion.Version() == 0xFF); // Masked to 8 bits
    }
    
    SECTION("Null entity constant")
    {
        REQUIRE(Entity::Null().Value() == EntityTraits32::NULL_VALUE);
        REQUIRE(!Entity::Null().Valid());
        REQUIRE(NULL_ENTITY == EntityTraits32::NULL_VALUE);
    }
}

TEST_CASE("Entity Version Management", "[Entity]")
{
    SECTION("NextVersion increments version")
    {
        Entity e(100, 5);
        Entity next = e.NextVersion();
        
        REQUIRE(next.Index() == 100);
        REQUIRE(next.Version() == 6);
        REQUIRE(next.Valid());
    }
    
    SECTION("NextVersion at maximum version returns invalid")
    {
        Entity e(100, 0xFF); // Max version
        Entity next = e.NextVersion();
        
        REQUIRE(!next.Valid());
        REQUIRE(next == Entity::Null());
    }
    
    SECTION("NextVersion preserves index")
    {
        Entity e(0xABCDEF, 42);
        Entity next = e.NextVersion();
        
        REQUIRE(next.Index() == 0xABCDEF);
        REQUIRE(next.Version() == 43);
    }
}

TEST_CASE("Entity Validity and Boolean Conversion", "[Entity]")
{
    SECTION("Valid entities")
    {
        Entity e1(100, 5);
        REQUIRE(e1.Valid());
        REQUIRE(static_cast<bool>(e1));
        
        Entity e2(0, 0); // Index 0, version 0 is still valid
        REQUIRE(e2.Valid());
        REQUIRE(static_cast<bool>(e2));
    }
    
    SECTION("Invalid entities")
    {
        Entity null = Entity::Null();
        REQUIRE(!null.Valid());
        REQUIRE(!static_cast<bool>(null));
        
        Entity maxValue(EntityTraits32::NULL_VALUE);
        REQUIRE(!maxValue.Valid());
        REQUIRE(!static_cast<bool>(maxValue));
    }
}

TEST_CASE("Entity Comparison Operations", "[Entity]")
{
    Entity e1(100, 5);
    Entity e2(100, 5);
    Entity e3(100, 6);
    Entity e4(101, 5);
    
    SECTION("Equality")
    {
        REQUIRE(e1 == e2);
        REQUIRE(e1 != e3); // Different version
        REQUIRE(e1 != e4); // Different index
        REQUIRE(e3 != e4); // Different index and version
    }
    
    SECTION("Ordering based on raw value")
    {
        // Entities are ordered by their raw value
        // Since version is in upper bits, different versions of same index
        // will have very different values
        REQUIRE(e1 < e3); // Same index, version 5 < version 6
        REQUIRE(e1 < e4); // This depends on the actual bit layout
    }
    
    SECTION("Null entity comparisons")
    {
        Entity null1 = Entity::Null();
        Entity null2 = Entity::Null();
        Entity valid(100, 5);
        
        REQUIRE(null1 == null2);
        REQUIRE(null1 != valid);
        // Null has max value, so it's greater than any valid entity
        REQUIRE(valid < null1);
    }
}

TEST_CASE("Entity Hashing", "[Entity]")
{
    SECTION("EntityHash functionality")
    {
        EntityHash hasher;
        
        Entity e1(100, 5);
        Entity e2(100, 5);
        Entity e3(100, 6);
        
        // Same entities produce same hash
        REQUIRE(hasher(e1) == hasher(e2));
        
        // Different entities should produce different hashes (not guaranteed but likely)
        REQUIRE(hasher(e1) != hasher(e3));
        
        // Hash should never have lower 7 bits as zero (FlatMap requirement)
        std::size_t hash = hasher(e1);
        REQUIRE((hash & 0x7F) != 0);
    }
    
    SECTION("std::hash specialization")
    {
        std::hash<Entity> stdHasher;
        EntityHash customHasher;
        
        Entity e(100, 5);
        
        // std::hash should use EntityHash internally
        REQUIRE(stdHasher(e) == customHasher(e));
    }
    
    SECTION("Hash ensures valid H2 for FlatMap")
    {
        EntityHash hasher;
        
        // Test many entities to ensure hash always has valid H2
        for (std::uint32_t i = 0; i < 1000; ++i)
        {
            for (std::uint8_t v = 0; v < 10; ++v)
            {
                Entity e(i, v);
                std::size_t hash = hasher(e);
                REQUIRE((hash & 0x7F) != 0); // Lower 7 bits never all zero
            }
        }
    }
}

TEST_CASE("Entity in STL Containers", "[Entity]")
{
    SECTION("unordered_map with Entity key")
    {
        std::unordered_map<Entity, int> entityMap;
        
        Entity e1(100, 1);
        Entity e2(200, 2);
        Entity e3(100, 2); // Same index, different version
        
        entityMap[e1] = 10;
        entityMap[e2] = 20;
        entityMap[e3] = 30;
        
        REQUIRE(entityMap.size() == 3);
        REQUIRE(entityMap[e1] == 10);
        REQUIRE(entityMap[e2] == 20);
        REQUIRE(entityMap[e3] == 30);
    }
    
    SECTION("unordered_set with Entity")
    {
        std::unordered_set<Entity> entitySet;
        
        Entity e1(100, 1);
        Entity e2(100, 1); // Duplicate
        Entity e3(100, 2); // Same index, different version
        
        entitySet.insert(e1);
        entitySet.insert(e2);
        entitySet.insert(e3);
        
        REQUIRE(entitySet.size() == 2); // e1 and e2 are the same
        REQUIRE(entitySet.count(e1) == 1);
        REQUIRE(entitySet.count(e3) == 1);
    }
    
    SECTION("set with Entity (tests operator<)")
    {
        std::set<Entity> entitySet;
        
        Entity e1(100, 1);
        Entity e2(200, 1);
        Entity e3(100, 2);
        
        entitySet.insert(e1);
        entitySet.insert(e2);
        entitySet.insert(e3);
        
        REQUIRE(entitySet.size() == 3);
        
        // Check ordering
        auto it = entitySet.begin();
        // The exact order depends on how the bits are laid out
        // but all three should be present
        int count = 0;
        for (auto& e : entitySet)
        {
            REQUIRE(e.Valid());
            count++;
        }
        REQUIRE(count == 3);
    }
}

TEST_CASE("Entity Constants and Limits", "[Entity]")
{
    SECTION("MAX_ENTITIES constant")
    {
        REQUIRE(MAX_ENTITIES == EntityTraits32::ENTITY_MASK);
        REQUIRE(MAX_ENTITIES == 0xFFFFFF); // 16,777,215
    }
    
    SECTION("Bit layout verification")
    {
        // Verify the bit shifts and masks work correctly
        Entity e(0x123456, 0x78);
        
        std::uint32_t expectedValue = (0x78u << 24) | 0x123456;
        REQUIRE(e.Value() == expectedValue);
        
        // Extract and verify
        REQUIRE(e.Index() == 0x123456);
        REQUIRE(e.Version() == 0x78);
    }
}