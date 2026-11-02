// File: tests/test_pool.cpp
// Purpose: Tests 4 and 5 on the deque-backed pool that real code uses: stale handles, reuse, retired
//          slots, and references that survive growth.
#include <string>

#include "check.hpp"
#include "jCppGUI/v001/pool.hpp"

int main() {
    using jgui::Handle;
    using jgui::Pool;
    jgui_test::Checker check;

    // Test 4: stale handle
    {
        Pool<std::string> pool;
        const Handle<std::string> old = pool.emplace("first").value();
        pool.destroy(old);
        check(pool.find(old) == nullptr, "lookup fails as soon as it is marked");
        pool.collect();
        check(pool.find(old) == nullptr, "lookup fails after it is freed");
        const Handle<std::string> next = pool.emplace("second").value();
        check(pool.slot_count() == 1, "the slot is reused");
        check(pool.find(old) == nullptr, "the old handle finds nothing in the reused slot");
        check(pool.find(next) != nullptr && *pool.find(next) == "second", "the new handle works");
        pool.destroy(old);
        pool.collect();
        check(pool.find(next) != nullptr, "destroying through a stale handle does nothing");
        Pool<std::string> empty;
        check(empty.find(next) == nullptr, "an out-of-range handle finds nothing");
    }

    // Test 5: generation exhaustion
    {
        Pool<int> pool{3};
        Handle<int> handle = pool.emplace(0).value();
        for (int i = 0; i < 3; ++i) {
            pool.destroy(handle);
            pool.collect();
            handle = pool.emplace(i).value();
        }
        check(pool.slot_count() == 1 && pool.retired_count() == 0, "reused up to the maximum");
        pool.destroy(handle);
        pool.collect();
        check(pool.retired_count() == 1, "the slot at the maximum generation is retired");
        const Handle<int> next = pool.emplace(1).value();
        check(pool.slot_count() == 2, "a retired slot is never reused");
        check(pool.find(handle) == nullptr && pool.find(next) != nullptr, "handles stay correct");
    }

    // References survive growth: the reason for the deque
    {
        Pool<int> pool;
        const Handle<int> first = pool.emplace(7).value();
        const int* address = pool.find(first);
        for (int i = 0; i < 10; ++i) static_cast<void>(pool.emplace(i).value());
        check(pool.find(first) == address, "an item does not move when the pool grows");
    }
    return check.finish();
}
