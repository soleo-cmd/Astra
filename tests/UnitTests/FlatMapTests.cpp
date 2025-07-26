#include <catch2/catch_all.hpp>
#include <Astra/Container/FlatMap.hpp>
#include <Astra/Entity/Entity.hpp>
#include <string>
#include <vector>
#include <random>
#include <algorithm>
#include <set>

using namespace Astra;

TEST_CASE("FlatMap Construction", "[FlatMap]")
{
    SECTION("Default construction")
    {
        FlatMap<int, std::string> map;
        REQUIRE(map.Empty());
        REQUIRE(map.Size() == 0);
        REQUIRE(map.Capacity() == 0);
        REQUIRE(map.begin() == map.end());
    }
    
    SECTION("Construction with capacity")
    {
        FlatMap<int, std::string> map(100);
        REQUIRE(map.Empty());
        REQUIRE(map.Size() == 0);
        REQUIRE(map.Capacity() >= 100);
        // Capacity should be power of 2 and at least MIN_CAPACITY (16)
        REQUIRE(map.Capacity() >= 16);
        REQUIRE((map.Capacity() & (map.Capacity() - 1)) == 0); // Power of 2
    }
    
    SECTION("Custom hasher and comparator")
    {
        auto customHash = [](const int& key) { return std::hash<int>{}(key * 2); };
        auto customEqual = [](const int& a, const int& b) { return a == b; };
        
        FlatMap<int, std::string, decltype(customHash), decltype(customEqual)> map(
            50, customHash, customEqual);
        
        map.Insert({1, "one"});
        REQUIRE(map.Size() == 1);
        REQUIRE(map.Contains(1));
    }
}

TEST_CASE("FlatMap Insert Operations", "[FlatMap]")
{
    FlatMap<int, std::string> map;
    
    SECTION("Insert with pair")
    {
        auto [it, inserted] = map.Insert({42, "forty-two"});
        REQUIRE(inserted);
        REQUIRE(it != map.end());
        REQUIRE(it->first == 42);
        REQUIRE(it->second == "forty-two");
        REQUIRE(map.Size() == 1);
        
        // Insert duplicate
        auto [it2, inserted2] = map.Insert({42, "duplicate"});
        REQUIRE(!inserted2);
        REQUIRE(it2 == it);
        REQUIRE(it2->second == "forty-two"); // Value unchanged
    }
    
    SECTION("Insert with move")
    {
        std::string value = "moveable";
        auto [it, inserted] = map.Insert({1, std::move(value)});
        REQUIRE(inserted);
        REQUIRE(it->second == "moveable");
        REQUIRE((value.empty() || value == "moveable")); // Moved-from state
    }
    
    SECTION("Emplace operation")
    {
        auto [it, inserted] = map.Emplace(42, "forty-two");
        REQUIRE(inserted);
        REQUIRE(map.Size() == 1);
        REQUIRE(it->second == "forty-two");
        
        // Emplace with existing key
        auto [it2, inserted2] = map.Emplace(42, "duplicate");
        REQUIRE(!inserted2);
        REQUIRE(it2->second == "forty-two");
    }
    
    SECTION("Operator[] insertion")
    {
        map[42] = "forty-two";
        REQUIRE(map.Size() == 1);
        REQUIRE(map[42] == "forty-two");
        
        // Modify existing
        map[42] = "updated";
        REQUIRE(map[42] == "updated");
        REQUIRE(map.Size() == 1);
        
        // Access non-existent creates default
        std::string& ref = map[99];
        REQUIRE(ref.empty());
        REQUIRE(map.Size() == 2);
    }
}

TEST_CASE("FlatMap Find and Contains", "[FlatMap]")
{
    FlatMap<int, std::string> map;
    map.Insert({1, "one"});
    map.Insert({2, "two"});
    map.Insert({3, "three"});
    
    SECTION("Find existing keys")
    {
        auto it = map.Find(2);
        REQUIRE(it != map.end());
        REQUIRE(it->first == 2);
        REQUIRE(it->second == "two");
        
        // Const find
        const auto& constMap = map;
        auto constIt = constMap.Find(1);
        REQUIRE(constIt != constMap.end());
        REQUIRE(constIt->first == 1);
    }
    
    SECTION("Find non-existent keys")
    {
        auto it = map.Find(99);
        REQUIRE(it == map.end());
    }
    
    SECTION("Contains operation")
    {
        REQUIRE(map.Contains(1));
        REQUIRE(map.Contains(2));
        REQUIRE(map.Contains(3));
        REQUIRE(!map.Contains(4));
        REQUIRE(!map.Contains(0));
    }
    
    SECTION("TryGet operations")
    {
        auto* value = map.TryGet(2);
        REQUIRE(value != nullptr);
        REQUIRE(*value == "two");
        
        auto* missing = map.TryGet(99);
        REQUIRE(missing == nullptr);
        
        // Const version
        const auto& constMap = map;
        const auto* constValue = constMap.TryGet(1);
        REQUIRE(constValue != nullptr);
        REQUIRE(*constValue == "one");
    }
}

TEST_CASE("FlatMap Erase Operations", "[FlatMap]")
{
    FlatMap<int, std::string> map;
    for (int i = 0; i < 10; ++i)
    {
        map.Insert({i, std::to_string(i)});
    }
    
    SECTION("Erase by key")
    {
        REQUIRE(map.Size() == 10);
        
        auto erased = map.Erase(5);
        REQUIRE(erased == 1);
        REQUIRE(map.Size() == 9);
        REQUIRE(!map.Contains(5));
        
        // Erase non-existent
        erased = map.Erase(5);
        REQUIRE(erased == 0);
        REQUIRE(map.Size() == 9);
    }
    
    SECTION("Erase by iterator")
    {
        auto it = map.Find(5);
        REQUIRE(it != map.end());
        
        auto next = map.Erase(it);
        REQUIRE(map.Size() == 9);
        REQUIRE(!map.Contains(5));
        // Next iterator should be valid or end()
        REQUIRE(((next == map.end()) || (next->first != 5)));
    }
    
    SECTION("Clear operation")
    {
        REQUIRE(!map.Empty());
        map.Clear();
        REQUIRE(map.Empty());
        REQUIRE(map.Size() == 0);
        
        // Can still use after clear
        map.Insert({100, "new"});
        REQUIRE(map.Size() == 1);
    }
}

TEST_CASE("FlatMap Iteration", "[FlatMap]")
{
    FlatMap<int, std::string> map;
    std::set<int> insertedKeys;
    
    for (int i = 0; i < 20; ++i)
    {
        map.Insert({i, std::to_string(i)});
        insertedKeys.insert(i);
    }
    
    SECTION("Iterator traversal")
    {
        std::set<int> foundKeys;
        for (auto it = map.begin(); it != map.end(); ++it)
        {
            foundKeys.insert(it->first);
            REQUIRE(it->second == std::to_string(it->first));
        }
        REQUIRE(foundKeys == insertedKeys);
    }
    
    SECTION("Range-based for loop")
    {
        std::set<int> foundKeys;
        for (const auto& [key, value] : map)
        {
            foundKeys.insert(key);
            REQUIRE(value == std::to_string(key));
        }
        REQUIRE(foundKeys == insertedKeys);
    }
    
    SECTION("Const iteration")
    {
        const auto& constMap = map;
        std::set<int> foundKeys;
        for (auto it = constMap.begin(); it != constMap.end(); ++it)
        {
            foundKeys.insert(it->first);
        }
        REQUIRE(foundKeys == insertedKeys);
    }
}

TEST_CASE("FlatMap Memory Management", "[FlatMap]")
{
    SECTION("Reserve operation")
    {
        FlatMap<int, int> map;
        map.Reserve(1000);
        
        REQUIRE(map.Capacity() >= 1000);
        REQUIRE(map.Empty());
        
        // Capacity should be power of 2
        REQUIRE((map.Capacity() & (map.Capacity() - 1)) == 0);
        
        // Insert without rehashing
        auto oldCapacity = map.Capacity();
        for (int i = 0; i < 500; ++i)
        {
            map.Insert({i, i});
        }
        REQUIRE(map.Capacity() == oldCapacity);
    }
    
    SECTION("Copy construction and assignment")
    {
        FlatMap<int, std::string> original;
        original.Insert({1, "one"});
        original.Insert({2, "two"});
        original.Insert({3, "three"});
        
        // Copy construction
        FlatMap<int, std::string> copy(original);
        REQUIRE(copy.Size() == original.Size());
        REQUIRE(copy.Contains(1));
        REQUIRE(*copy.TryGet(2) == "two");
        
        // Copy assignment
        FlatMap<int, std::string> assigned;
        assigned = original;
        REQUIRE(assigned.Size() == original.Size());
        REQUIRE(*assigned.TryGet(3) == "three");
        
        // Modify copy doesn't affect original
        copy[2] = "modified";
        REQUIRE(*original.TryGet(2) == "two");
        REQUIRE(*copy.TryGet(2) == "modified");
    }
    
    SECTION("Move construction and assignment")
    {
        FlatMap<int, std::string> original;
        original.Insert({1, "one"});
        original.Insert({2, "two"});
        
        auto originalSize = original.Size();
        
        // Move construction
        FlatMap<int, std::string> moved(std::move(original));
        REQUIRE(moved.Size() == originalSize);
        REQUIRE(moved.Contains(1));
        REQUIRE(original.Empty()); // Moved-from state
        
        // Move assignment
        FlatMap<int, std::string> assigned;
        assigned = std::move(moved);
        REQUIRE(assigned.Size() == originalSize);
        REQUIRE(assigned.Contains(2));
        REQUIRE(moved.Empty());
    }
    
    SECTION("Swap operation")
    {
        FlatMap<int, int> map1;
        FlatMap<int, int> map2;
        
        map1.Insert({1, 10});
        map1.Insert({2, 20});
        map2.Insert({3, 30});
        
        map1.Swap(map2);
        
        REQUIRE(map1.Size() == 1);
        REQUIRE(map1.Contains(3));
        REQUIRE(*map1.TryGet(3) == 30);
        
        REQUIRE(map2.Size() == 2);
        REQUIRE(map2.Contains(1));
        REQUIRE(map2.Contains(2));
    }
}

TEST_CASE("FlatMap with Entity Keys", "[FlatMap][Entity]")
{
    SECTION("Entity key with automatic hash selection")
    {
        // FlatMap should automatically use EntityHash for Entity keys
        FlatMap<Entity, int> entityMap;
        
        Entity e1(100, 1);
        Entity e2(200, 2);
        Entity e3(100, 2); // Same ID, different version
        
        entityMap.Insert({e1, 10});
        entityMap.Insert({e2, 20});
        entityMap.Insert({e3, 30});
        
        REQUIRE(entityMap.Size() == 3);
        REQUIRE(*entityMap.TryGet(e1) == 10);
        REQUIRE(*entityMap.TryGet(e2) == 20);
        REQUIRE(*entityMap.TryGet(e3) == 30);
    }
    
    SECTION("Null entity handling")
    {
        FlatMap<Entity, int> entityMap;
        
        Entity null = Entity::Null();
        entityMap.Insert({null, 42});
        
        REQUIRE(entityMap.Contains(null));
        REQUIRE(*entityMap.TryGet(null) == 42);
    }
}

TEST_CASE("FlatMap Batch Operations", "[FlatMap]")
{
    FlatMap<int, std::string> map;
    for (int i = 0; i < 100; ++i)
    {
        map.Insert({i, std::to_string(i)});
    }
    
    SECTION("FindBatch operation")
    {
        int keys[8] = {5, 15, 25, 35, 45, 55, 65, 75};
        FlatMap<int, std::string>::iterator results[8];
        
        map.FindBatch(keys, results);
        
        for (int i = 0; i < 8; ++i)
        {
            REQUIRE(results[i] != map.end());
            REQUIRE(results[i]->first == keys[i]);
            REQUIRE(results[i]->second == std::to_string(keys[i]));
        }
    }
    
    SECTION("GetBatch operation")
    {
        int keys[5] = {10, 20, 30, 40, 50};
        std::string* values[5];
        
        size_t found = map.GetBatch(keys, values);
        REQUIRE(found == 5);
        
        for (size_t i = 0; i < found; ++i)
        {
            REQUIRE(values[i] != nullptr);
            REQUIRE(*values[i] == std::to_string(keys[i]));
        }
    }
}

TEST_CASE("FlatMap Group Operations", "[FlatMap]")
{
    FlatMap<int, int> map;
    
    // Insert enough elements to span multiple groups (GROUP_SIZE = 16)
    for (int i = 0; i < 50; ++i)
    {
        map.Insert({i, i * 10});
    }
    
    SECTION("Group iteration")
    {
        std::set<int> foundKeys;
        
        for (auto groupIt = map.GroupBegin(); groupIt != map.GroupEnd(); ++groupIt)
        {
            auto view = *groupIt;
            while (view.HasNext())
            {
                auto& kv = view.NextValue();
                foundKeys.insert(kv.first);
                REQUIRE(kv.second == kv.first * 10);
            }
        }
        
        REQUIRE(foundKeys.size() == 50);
    }
    
    SECTION("ForEachGroup operation")
    {
        std::set<int> foundKeys;
        
        map.ForEachGroup([&foundKeys](auto& view)
        {
            view.ForEach([&foundKeys](const auto& kv)
            {
                foundKeys.insert(kv.first);
            });
        });
        
        REQUIRE(foundKeys.size() == 50);
    }
    
    SECTION("Group statistics")
    {
        auto stats = map.GetGroupStats();
        
        REQUIRE(stats.totalGroups > 0);
        REQUIRE(stats.averageOccupancy > 0.0f);
        REQUIRE(stats.averageOccupancy <= 1.0f);
        
        // Total groups should equal sum of empty, full, and partial
        REQUIRE(stats.totalGroups == 
                stats.emptyGroups + stats.fullGroups + stats.partialGroups);
    }
}

TEST_CASE("FlatMap Rehashing and Load Factor", "[FlatMap]")
{
    SECTION("Automatic rehashing on load")
    {
        FlatMap<int, int> map;
        
        // Insert enough to trigger rehashing
        // MAX_LOAD_FACTOR is 0.875, so with MIN_CAPACITY=16, 
        // it should rehash after 14 elements
        for (int i = 0; i < 20; ++i)
        {
            map.Insert({i, i});
        }
        
        REQUIRE(map.Size() == 20);
        REQUIRE(map.Capacity() > 16); // Should have rehashed
        
        // Verify all elements survived
        for (int i = 0; i < 20; ++i)
        {
            REQUIRE(map.Contains(i));
            REQUIRE(*map.TryGet(i) == i);
        }
    }
    
    SECTION("Tombstone cleanup on rehash")
    {
        FlatMap<int, int> map;
        
        // Insert and erase to create tombstones
        for (int i = 0; i < 100; ++i)
        {
            map.Insert({i, i});
        }
        
        // Erase half to create tombstones
        for (int i = 0; i < 100; i += 2)
        {
            map.Erase(i);
        }
        
        REQUIRE(map.Size() == 50);
        
        // Force rehash by inserting more
        auto oldCapacity = map.Capacity();
        for (int i = 100; i < 200; ++i)
        {
            map.Insert({i, i});
        }
        
        // Should have rehashed at some point
        REQUIRE(map.Capacity() >= oldCapacity);
        
        // Verify remaining elements
        for (int i = 1; i < 100; i += 2)
        {
            REQUIRE(map.Contains(i));
        }
        for (int i = 100; i < 200; ++i)
        {
            REQUIRE(map.Contains(i));
        }
    }
}

TEST_CASE("FlatMap Edge Cases", "[FlatMap]")
{
    SECTION("Empty map operations")
    {
        FlatMap<int, int> map;
        
        REQUIRE(!map.Contains(42));
        REQUIRE(map.Find(42) == map.end());
        REQUIRE(map.TryGet(42) == nullptr);
        REQUIRE(map.Erase(42) == 0);
        
        map.Clear(); // Should be safe on empty map
        REQUIRE(map.Empty());
    }
    
    SECTION("Single element map")
    {
        FlatMap<int, int> map;
        map.Insert({42, 100});
        
        REQUIRE(map.Size() == 1);
        REQUIRE(map.Contains(42));
        
        // Iteration should find exactly one element
        int count = 0;
        for (const auto& kv : map)
        {
            REQUIRE(kv.first == 42);
            REQUIRE(kv.second == 100);
            count++;
        }
        REQUIRE(count == 1);
    }
    
    SECTION("Hash collision handling")
    {
        // Create a custom hash that forces collisions
        struct BadHash
        {
            std::size_t operator()(int) const noexcept { return 42; }
        };
        
        FlatMap<int, int, BadHash> map;
        
        // All these will have the same hash
        for (int i = 0; i < 10; ++i)
        {
            map.Insert({i, i * 10});
        }
        
        REQUIRE(map.Size() == 10);
        
        // Verify all can be found despite collisions
        for (int i = 0; i < 10; ++i)
        {
            REQUIRE(map.Contains(i));
            REQUIRE(*map.TryGet(i) == i * 10);
        }
    }
}