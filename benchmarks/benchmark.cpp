/**
 * @file benchmark.cpp
 * @brief Performance benchmarks for SPSC Queue
 *
 * Measures:
 * - Operations per second (throughput)
 * - Round-trip latency
 * - Single-threaded enqueue/dequeue latency
 */

#include "spsc_queue.hpp"
#include <chrono>
#include <thread>
#include <iostream>
#include <iomanip>
#include <vector>
#include <numeric>
#include <algorithm>

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
constexpr std::size_t LATENCY_SAMPLES = 100'000;

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
 * @brief Measure round-trip latency (push then immediate pop)
 */
template <typename T>
std::vector<uint64_t> benchmark_roundtrip_latency(std::size_t samples) {
    SPSCQueue<T, QUEUE_CAPACITY> queue;
    std::vector<uint64_t> latencies;
    latencies.reserve(samples);

    T item{};
    T result;

    // Warmup
    for (std::size_t i = 0; i < 10000; ++i) {
        queue.try_push(item);
        queue.try_pop(result);
    }

    // Measure
    for (std::size_t i = 0; i < samples; ++i) {
        auto start = Clock::now();
        queue.try_push(item);
        queue.try_pop(result);
        auto end = Clock::now();

        latencies.push_back(
                std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()
        );
    }

    return latencies;
}

/**
 * @brief Compute percentile from sorted vector
 */
uint64_t percentile(std::vector<uint64_t>& sorted_data, double p) {
    std::size_t idx = static_cast<std::size_t>(p * sorted_data.size());
    if (idx >= sorted_data.size()) idx = sorted_data.size() - 1;
    return sorted_data[idx];
}

void print_latency_stats(std::vector<uint64_t>& latencies, const std::string& name) {
    std::sort(latencies.begin(), latencies.end());

    uint64_t sum = std::accumulate(latencies.begin(), latencies.end(), uint64_t{0});
    double mean = static_cast<double>(sum) / latencies.size();

    std::cout << "\n" << name << " Latency (ns):\n";
    std::cout << "  Mean:   " << std::fixed << std::setprecision(1) << mean << "\n";
    std::cout << "  Median: " << percentile(latencies, 0.50) << "\n";
    std::cout << "  P90:    " << percentile(latencies, 0.90) << "\n";
    std::cout << "  P99:    " << percentile(latencies, 0.99) << "\n";
    std::cout << "  P99.9:  " << percentile(latencies, 0.999) << "\n";
    std::cout << "  Min:    " << latencies.front() << "\n";
    std::cout << "  Max:    " << latencies.back() << "\n";
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
                  << ops_per_sec / 1e6 << " M ops/sec\n";
    }

    // MarketData throughput
    std::cout << "\nMarketData (" << sizeof(MarketData) << " bytes) throughput:\n";
    for (int run = 0; run < 3; ++run) {
        double ops_per_sec = benchmark_throughput<MarketData>(NUM_OPERATIONS);
        std::cout << "  Run " << (run + 1) << ": "
                  << std::fixed << std::setprecision(2)
                  << ops_per_sec / 1e6 << " M ops/sec\n";
    }

    // Latency benchmarks
    std::cout << "\n=== Latency Benchmarks ===\n";
    std::cout << "Samples: " << LATENCY_SAMPLES / 1000 << "K\n";

    auto uint64_latencies = benchmark_roundtrip_latency<uint64_t>(LATENCY_SAMPLES);
    print_latency_stats(uint64_latencies, "uint64_t Round-trip");

    auto market_latencies = benchmark_roundtrip_latency<MarketData>(LATENCY_SAMPLES);
    print_latency_stats(market_latencies, "MarketData Round-trip");

    std::cout << "\n========================================\n";
    std::cout << "  Benchmark Complete\n";
    std::cout << "========================================\n";

    return 0;
}