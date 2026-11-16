/**
 * @file benchmark.cpp
 * @brief Performance benchmarks for SPSC Queue
 *
 * Measures:
 * - Throughput: items per second from a producer thread to a consumer thread
 * - Round trip: one push and one pop on the same thread, batch-timed
 *
 * On Apple Silicon the clock ticks every 41.7 ns, longer than a push and a
 * pop together, so nothing here is timed one operation at a time.
 */

#include "spsc_queue.hpp"
#include <chrono>
#include <thread>
#include <iostream>
#include <iomanip>

using namespace hft;
using Clock = std::chrono::high_resolution_clock;

// Simple struct to simulate market data
struct MarketData {
    uint64_t timestamp;
    uint64_t sequence;
    double price;
    uint32_t quantity;
    char symbol[8];
};

constexpr std::size_t QUEUE_CAPACITY = 65536; // 64K - power of 2
constexpr std::size_t NUM_OPERATIONS = 10'000'000;
constexpr std::size_t ROUNDTRIP_PAIRS = 10'000'000;

// Tells the compiler that value is read here and that any memory may change,
// so the push or pop being timed cannot be deleted or merged with the next.
template <typename T>
inline void keep(T& value) {
    asm volatile("" : : "r"(&value) : "memory");
}

/**
 * @brief Measure throughput with producer-consumer threads
 */
template <typename T>
double benchmark_throughput(std::size_t num_ops) {
    SPSCQueue<T, QUEUE_CAPACITY> queue;
    std::atomic<bool> start{false};
    std::atomic<bool> done{false};

    // Consumer thread
    std::thread consumer([&]() {
        while (!start.load(std::memory_order_acquire)) {
            // Wait for start signal
        }

        T value;
        std::size_t count = 0;
        while (count < num_ops) {
            if (queue.try_pop(value)) {
                ++count;
            }
        }
    });

    // Producer (main thread)
    T item{};

    // Wait a bit for consumer to be ready
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto start_time = Clock::now();
    start.store(true, std::memory_order_release);

    for (std::size_t i = 0; i < num_ops; ++i) {
        while (!queue.try_push(item)) {
            // Spin if full
        }
    }

    consumer.join();
    auto end_time = Clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
            end_time - start_time).count();

    return static_cast<double>(num_ops) / (duration / 1e9);
}

/**
 * @brief Cost of one push and one pop on the same thread, in ns
 *
 * Times `pairs` push + pop pairs between two clock reads and keeps the best
 * of 5 runs. Noise from the OS only ever adds time.
 */
template <typename T>
double benchmark_roundtrip_ns(std::size_t pairs) {
    SPSCQueue<T, QUEUE_CAPACITY> queue;
    keep(queue);  // as if another thread could see it, as a real queue's is
    T item{};
    T result;
    double best = 1e30;

    for (int run = 0; run < 5; ++run) {
        auto start = Clock::now();
        for (std::size_t i = 0; i < pairs; ++i) {
            queue.try_push(item);
            queue.try_pop(result);
            keep(result);
            keep(queue);
        }
        auto end = Clock::now();

        double ns = std::chrono::duration<double, std::nano>(end - start).count() / pairs;
        if (ns < best) best = ns;
    }
    return best;
}

/**
 * @brief Verify queue memory layout (cache line separation)
 */
void verify_layout() {
    std::cout << "\n=== Memory Layout Verification ===\n";
    std::cout << "Cache line size: " << CACHE_LINE_SIZE << " bytes\n";
    std::cout << "Queue capacity:  " << QUEUE_CAPACITY << " elements\n";
    std::cout << "sizeof(SPSCQueue<uint64_t>): "
              << sizeof(SPSCQueue<uint64_t, QUEUE_CAPACITY>) << " bytes\n";

    // The queue should be at least 4 cache lines (head, cached_tail, tail, cached_head)
    // plus the buffer pointer
    static_assert(sizeof(SPSCQueue<uint64_t, QUEUE_CAPACITY>) >= 4 * CACHE_LINE_SIZE,
                  "Queue layout may have false sharing issues");
    std::cout << "✓ Cache line padding verified\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "  SPSC Queue Performance Benchmark\n";
    std::cout << "========================================\n";

    verify_layout();

    // Throughput benchmarks
    std::cout << "\n=== Throughput Benchmarks ===\n";
    std::cout << "Operations: " << NUM_OPERATIONS / 1'000'000 << "M\n\n";

    // uint64_t throughput
    std::cout << "uint64_t throughput:\n";
    for (int run = 0; run < 3; ++run) {
        double ops_per_sec = benchmark_throughput<uint64_t>(NUM_OPERATIONS);
        std::cout << "  Run " << (run + 1) << ": "
                  << std::fixed << std::setprecision(2)
                  << ops_per_sec / 1e6 << " M ops/sec ("
                  << 1e9 / ops_per_sec << " ns per item)\n";
    }

    // MarketData throughput
    std::cout << "\nMarketData (" << sizeof(MarketData) << " bytes) throughput:\n";
    for (int run = 0; run < 3; ++run) {
        double ops_per_sec = benchmark_throughput<MarketData>(NUM_OPERATIONS);
        std::cout << "  Run " << (run + 1) << ": "
                  << std::fixed << std::setprecision(2)
                  << ops_per_sec / 1e6 << " M ops/sec ("
                  << 1e9 / ops_per_sec << " ns per item)\n";
    }

    // Round trip
    std::cout << "\n=== Round Trip (one thread, batch-timed, best of 5) ===\n";
    std::cout << "Pairs per run: " << ROUNDTRIP_PAIRS / 1'000'000 << "M\n\n";
    std::cout << "uint64_t push + pop:   " << std::fixed << std::setprecision(2)
              << benchmark_roundtrip_ns<uint64_t>(ROUNDTRIP_PAIRS) << " ns\n";
    std::cout << "MarketData push + pop: " << std::fixed << std::setprecision(2)
              << benchmark_roundtrip_ns<MarketData>(ROUNDTRIP_PAIRS) << " ns\n";

    std::cout << "\n========================================\n";
    std::cout << "  Benchmark Complete\n";
    std::cout << "========================================\n";

    return 0;
}