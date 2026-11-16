/**
 * @file test_spsc_queue.cpp
 * @brief Unit tests for SPSC Queue
 *
 * Tests:
 * - Basic push/pop operations
 * - Queue full/empty conditions
 * - FIFO ordering
 * - Concurrent producer-consumer correctness
 */

#include "spsc_queue.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace hft;

constexpr std::size_t TEST_CAPACITY = 1024;

// CHECK, not assert(): a Release build defines NDEBUG, which removes every
// assert() and the call inside it, so the tests would check nothing. A failed
// CHECK throws, and the runner below reports it.
#define CHECK(condition)                                                \
    do {                                                                \
        if (!(condition)) {                                             \
            throw std::runtime_error(std::string(__FILE__) + ":" +      \
                                     std::to_string(__LINE__) + ": " +  \
                                     #condition);                       \
        }                                                               \
    } while (0)

// Simple test macro
#define TEST(name) void name(); \
    struct name##_register { name##_register() { tests.push_back({#name, name}); } } name##_instance; \
    void name()

struct TestCase {
    const char* name;
    void (*func)();
};

std::vector<TestCase> tests;

// ============== Tests ==============

TEST(test_basic_push_pop) {
    SPSCQueue<int, TEST_CAPACITY> queue;

    CHECK(queue.try_push(42));

    int value;
    CHECK(queue.try_pop(value));
    CHECK(value == 42);
}

TEST(test_empty_pop_fails) {
    SPSCQueue<int, TEST_CAPACITY> queue;

    int value;
    CHECK(!queue.try_pop(value));
    CHECK(queue.empty());
}

TEST(test_full_push_fails) {
    SPSCQueue<int, 4> queue; // Small capacity

    CHECK(queue.try_push(1));
    CHECK(queue.try_push(2));
    CHECK(queue.try_push(3));
    CHECK(queue.try_push(4));
    CHECK(!queue.try_push(5)); // Should fail - queue full

    int value;
    CHECK(queue.try_pop(value));
    CHECK(value == 1);

    CHECK(queue.try_push(5)); // Now should succeed
}

TEST(test_fifo_ordering) {
    SPSCQueue<int, TEST_CAPACITY> queue;

    for (int i = 0; i < 100; ++i) {
        CHECK(queue.try_push(i));
    }

    for (int i = 0; i < 100; ++i) {
        int value;
        CHECK(queue.try_pop(value));
        CHECK(value == i);
    }
}

TEST(test_wraparound) {
    SPSCQueue<int, 8> queue; // Small to force wraparound

    // Fill and drain multiple times to test wrap-around
    for (int round = 0; round < 10; ++round) {
        for (int i = 0; i < 8; ++i) {
            CHECK(queue.try_push(round * 100 + i));
        }
        for (int i = 0; i < 8; ++i) {
            int value;
            CHECK(queue.try_pop(value));
            CHECK(value == round * 100 + i);
        }
    }
}

TEST(test_size_tracking) {
    SPSCQueue<int, TEST_CAPACITY> queue;

    CHECK(queue.empty());
    CHECK(queue.size() == 0);

    CHECK(queue.try_push(1));
    CHECK(!queue.empty());
    CHECK(queue.size() == 1);

    CHECK(queue.try_push(2));
    CHECK(queue.try_push(3));
    CHECK(queue.size() == 3);

    int value;
    CHECK(queue.try_pop(value));
    CHECK(queue.size() == 2);
}

TEST(test_capacity) {
    SPSCQueue<int, 256> queue;
    CHECK(queue.capacity() == 256);

    SPSCQueue<int, 1024> queue2;
    CHECK(queue2.capacity() == 1024);
}

TEST(test_move_semantics) {
    SPSCQueue<std::string, TEST_CAPACITY> queue;

    std::string str = "Hello, HFT!";
    CHECK(queue.try_push(std::move(str)));
    CHECK(str.empty()); // Should be moved from

    std::string result;
    CHECK(queue.try_pop(result));
    CHECK(result == "Hello, HFT!");
}

TEST(test_struct_elements) {
    struct Data {
        int id;
        double value;
        char tag;
    };

    SPSCQueue<Data, TEST_CAPACITY> queue;

    CHECK(queue.try_push(Data{1, 3.14, 'A'}));
    CHECK(queue.try_push(Data{2, 2.71, 'B'}));

    Data d;
    CHECK(queue.try_pop(d));
    CHECK(d.id == 1);
    CHECK(d.value == 3.14);
    CHECK(d.tag == 'A');
}

TEST(test_concurrent_correctness) {
    // One producer thread and one consumer thread. Every item must arrive once,
    // in order, and whole. The queue is small, so it is full or empty most of
    // the time and the ring wraps many times.
    struct Item {
        std::uint64_t sequence;
        std::uint64_t check;  // derived from sequence, to catch a half-written item
    };
    constexpr std::uint64_t NUM_ITEMS = 10'000'000;
    constexpr std::uint64_t CHECK_FACTOR = 0x9E3779B97F4A7C15ull;
    SPSCQueue<Item, 64> queue;

    std::uint64_t received = 0;
    std::uint64_t out_of_order = 0;
    std::uint64_t damaged = 0;

    std::thread consumer([&]() {
        Item item;
        while (received < NUM_ITEMS) {
            if (queue.try_pop(item)) {
                if (item.sequence != received) ++out_of_order;
                if (item.check != item.sequence * CHECK_FACTOR) ++damaged;
                ++received;
            }
        }
    });

    // Producer (main thread)
    for (std::uint64_t i = 0; i < NUM_ITEMS; ++i) {
        while (!queue.try_push(Item{i, i * CHECK_FACTOR})) {
            // Spin if full
        }
    }

    consumer.join();

    CHECK(received == NUM_ITEMS);
    CHECK(out_of_order == 0);
    CHECK(damaged == 0);
    CHECK(queue.empty());
}

TEST(test_interleaved_operations) {
    SPSCQueue<int, TEST_CAPACITY> queue;

    // Interleave push and pop operations
    for (int i = 0; i < 1000; ++i) {
        CHECK(queue.try_push(i * 2));
        CHECK(queue.try_push(i * 2 + 1));

        int v1, v2;
        CHECK(queue.try_pop(v1));
        CHECK(v1 == i * 2);
        CHECK(queue.try_pop(v2));
        CHECK(v2 == i * 2 + 1);
    }

    CHECK(queue.empty());
}

// ============== Test Runner ==============

int main() {
    std::cout << "========================================\n";
    std::cout << "  SPSC Queue Unit Tests\n";
    std::cout << "========================================\n\n";

    int passed = 0;
    int failed = 0;

    for (const auto& test : tests) {
        std::cout << "Running: " << test.name << "... ";
        try {
            test.func();
            std::cout << "✓ PASSED\n";
            ++passed;
        } catch (const std::exception& e) {
            std::cout << "✗ FAILED: " << e.what() << "\n";
            ++failed;
        } catch (...) {
            std::cout << "✗ FAILED: unknown exception\n";
            ++failed;
        }
    }

    std::cout << "\n========================================\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed\n";
    std::cout << "========================================\n";

    return failed > 0 ? 1 : 0;
}