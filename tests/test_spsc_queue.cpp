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
#include <iostream>
#include <thread>
#include <cassert>
#include <vector>

using namespace hft;

constexpr std::size_t TEST_CAPACITY = 1024;

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

    assert(queue.try_push(42));

    int value;
    assert(queue.try_pop(value));
    assert(value == 42);
}

TEST(test_empty_pop_fails) {
    SPSCQueue<int, TEST_CAPACITY> queue;

    int value;
    assert(!queue.try_pop(value));
    assert(queue.empty());
}

TEST(test_full_push_fails) {
    SPSCQueue<int, 4> queue; // Small capacity

    assert(queue.try_push(1));
    assert(queue.try_push(2));
    assert(queue.try_push(3));
    assert(queue.try_push(4));
    assert(!queue.try_push(5)); // Should fail - queue full

    int value;
    assert(queue.try_pop(value));
    assert(value == 1);

    assert(queue.try_push(5)); // Now should succeed
}

TEST(test_fifo_ordering) {
    SPSCQueue<int, TEST_CAPACITY> queue;

    for (int i = 0; i < 100; ++i) {
        assert(queue.try_push(i));
    }

    for (int i = 0; i < 100; ++i) {
        int value;
        assert(queue.try_pop(value));
        assert(value == i);
    }
}

TEST(test_wraparound) {
    SPSCQueue<int, 8> queue; // Small to force wraparound

    // Fill and drain multiple times to test wrap-around
    for (int round = 0; round < 10; ++round) {
        for (int i = 0; i < 8; ++i) {
            assert(queue.try_push(round * 100 + i));
        }
        for (int i = 0; i < 8; ++i) {
            int value;
            assert(queue.try_pop(value));
            assert(value == round * 100 + i);
        }
    }
}

TEST(test_size_tracking) {
    SPSCQueue<int, TEST_CAPACITY> queue;

    assert(queue.empty());
    assert(queue.size() == 0);

    queue.try_push(1);
    assert(!queue.empty());
    assert(queue.size() == 1);

    queue.try_push(2);
    queue.try_push(3);
    assert(queue.size() == 3);

    int value;
    queue.try_pop(value);
    assert(queue.size() == 2);
}

TEST(test_capacity) {
    SPSCQueue<int, 256> queue;
    assert(queue.capacity() == 256);

    SPSCQueue<int, 1024> queue2;
    assert(queue2.capacity() == 1024);
}

TEST(test_move_semantics) {
    SPSCQueue<std::string, TEST_CAPACITY> queue;

    std::string str = "Hello, HFT!";
    queue.try_push(std::move(str));
    assert(str.empty()); // Should be moved from

    std::string result;
    queue.try_pop(result);
    assert(result == "Hello, HFT!");
}

TEST(test_struct_elements) {
    struct Data {
        int id;
        double value;
        char tag;
    };

    SPSCQueue<Data, TEST_CAPACITY> queue;

    queue.try_push(Data{1, 3.14, 'A'});
    queue.try_push(Data{2, 2.71, 'B'});

    Data d;
    queue.try_pop(d);
    assert(d.id == 1);
    assert(d.value == 3.14);
    assert(d.tag == 'A');
}

TEST(test_concurrent_correctness) {
    constexpr std::size_t NUM_ITEMS = 100000;
    SPSCQueue<uint64_t, 4096> queue;

    std::atomic<bool> start{false};
    std::vector<uint64_t> received;
    received.reserve(NUM_ITEMS);

    // Consumer thread
    std::thread consumer([&]() {
        while (!start.load(std::memory_order_acquire)) {}

        uint64_t value;
        std::size_t count = 0;
        while (count < NUM_ITEMS) {
            if (queue.try_pop(value)) {
                received.push_back(value);
                ++count;
            }
        }
    });

    // Small delay to ensure consumer is ready
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    start.store(true, std::memory_order_release);

    // Producer (main thread)
    for (uint64_t i = 0; i < NUM_ITEMS; ++i) {
        while (!queue.try_push(i)) {
            // Spin if full
        }
    }

    consumer.join();

    // Verify all items received in order
    assert(received.size() == NUM_ITEMS);
    for (uint64_t i = 0; i < NUM_ITEMS; ++i) {
        assert(received[i] == i);
    }
}

TEST(test_interleaved_operations) {
    SPSCQueue<int, TEST_CAPACITY> queue;

    // Interleave push and pop operations
    for (int i = 0; i < 1000; ++i) {
        queue.try_push(i * 2);
        queue.try_push(i * 2 + 1);

        int v1, v2;
        assert(queue.try_pop(v1));
        assert(v1 == i * 2);
        assert(queue.try_pop(v2));
        assert(v2 == i * 2 + 1);
    }

    assert(queue.empty());
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