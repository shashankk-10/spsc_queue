/**
 * @file spsc_queue.hpp
 * @brief Wait-free Single Producer Single Consumer Queue
 *
 * A high-performance lock-free SPSC queue optimized for low-latency systems.
 *
 * Key optimizations:
 * - Power-of-2 capacity for bitwise AND wrap-around (avoids modulo)
 * - Cache-line padded head/tail to prevent false sharing
 * - Cached indices to reduce atomic loads across cache lines
 * - Acquire-release memory ordering (not seq_cst)
 * - Pre-allocated ring buffer for zero-allocation hot path
 *
 * @author Shashank
 */

#ifndef SPSC_QUEUE_HPP
#define SPSC_QUEUE_HPP

#include <atomic>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

// Cache line size - 64 bytes on most modern architectures
// Apple Silicon (M1/M2/M3) also uses 64-byte cache lines for L1
#ifdef __cpp_lib_hardware_interference_size
constexpr std::size_t CACHE_LINE_SIZE = std::hardware_destructive_interference_size;
#else
constexpr std::size_t CACHE_LINE_SIZE = 64;
#endif

namespace hft {

/**
 * @brief Wait-free SPSC queue with cache-optimized design
 *
 * @tparam T Element type (should be trivially copyable for best performance)
 * @tparam Capacity Queue capacity (must be power of 2)
 *
 * Memory Layout:
 * [Cache Line 0]: head_ (producer writes, consumer reads)
 * [Cache Line 1]: cached_tail_ (producer's local cache of tail)
 * [Cache Line 2]: tail_ (consumer writes, producer reads)
 * [Cache Line 3]: cached_head_ (consumer's local cache of head)
 * [Separate allocation]: ring buffer
 */
    template <typename T, std::size_t Capacity>
    class SPSCQueue {
        static_assert(Capacity > 0, "Capacity must be greater than 0");
        static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

    public:
        SPSCQueue() : head_(0), cached_tail_(0), tail_(0), cached_head_(0) {
            // Allocate aligned storage for the ring buffer
            // Using aligned allocation to ensure cache-friendly access patterns
            buffer_ = static_cast<T*>(::operator new(sizeof(T) * Capacity,
                                                     std::align_val_t{alignof(T)}));
        }

        ~SPSCQueue() {
            // Destroy any remaining elements
            T dummy;
            while (try_pop(dummy)) {}
            ::operator delete(buffer_, std::align_val_t{alignof(T)});
        }

        // Non-copyable, non-movable (indices are atomics)
        SPSCQueue(const SPSCQueue&) = delete;
        SPSCQueue& operator=(const SPSCQueue&) = delete;
        SPSCQueue(SPSCQueue&&) = delete;
        SPSCQueue& operator=(SPSCQueue&&) = delete;

        /**
         * @brief Try to push an element (producer only)
         *
         * @param value Element to push
         * @return true if successful, false if queue is full
         *
         * Memory ordering:
         * - Release on head_ store ensures the written element is visible
         *   before the head update is visible to the consumer
         */
        template <typename U>
        bool try_push(U&& value) {
            const std::size_t current_head = head_.load(std::memory_order_relaxed);
            const std::size_t next_head = current_head + 1;

            // Check if queue is full using cached tail
            // This avoids an atomic load in the common case (queue not full)
            if (next_head - cached_tail_ > Capacity) {
                // Cache miss - reload the actual tail
                cached_tail_ = tail_.load(std::memory_order_acquire);
                if (next_head - cached_tail_ > Capacity) {
                    return false; // Queue is actually full
                }
            }

            // Construct element in-place
            new (&buffer_[current_head & mask_]) T(std::forward<U>(value));

            // Publish the element - release ensures element write is visible
            // before head update
            head_.store(next_head, std::memory_order_release);
            return true;
        }

        /**
         * @brief Push with busy-wait (blocks until space available)
         *
         * Use with caution - only appropriate when you can guarantee
         * the consumer will eventually make progress
         */
        template <typename U>
        void push(U&& value) {
            while (!try_push(std::forward<U>(value))) {
                // Spin - in production, might add _mm_pause() or yield
            }
        }

        /**
         * @brief Try to pop an element (consumer only)
         *
         * @param value Output parameter for popped element
         * @return true if successful, false if queue is empty
         *
         * Memory ordering:
         * - Acquire on head_ load ensures we see the element written by producer
         * - Release on tail_ store ensures element destruction is complete
         *   before we signal the slot is free
         */
        bool try_pop(T& value) {
            const std::size_t current_tail = tail_.load(std::memory_order_relaxed);

            // Check if queue is empty using cached head
            if (cached_head_ == current_tail) {
                // Cache miss - reload the actual head
                cached_head_ = head_.load(std::memory_order_acquire);
                if (cached_head_ == current_tail) {
                    return false; // Queue is actually empty
                }
            }

            // Read the element
            value = std::move(buffer_[current_tail & mask_]);

            // Destroy the element in the buffer
            buffer_[current_tail & mask_].~T();

            // Signal slot is free - release ensures destruction is complete
            tail_.store(current_tail + 1, std::memory_order_release);
            return true;
        }

        /**
         * @brief Pop with busy-wait (blocks until element available)
         */
        T pop() {
            T value;
            while (!try_pop(value)) {
                // Spin
            }
            return value;
        }

        /**
         * @brief Check if queue is empty (approximate - may have false positives)
         *
         * Note: This is inherently racy in a concurrent context.
         * Only use for debugging/monitoring, not for control flow.
         */
        [[nodiscard]] bool empty() const noexcept {
            return head_.load(std::memory_order_acquire) ==
                   tail_.load(std::memory_order_acquire);
        }

        /**
         * @brief Get approximate size (may be stale)
         */
        [[nodiscard]] std::size_t size() const noexcept {
            const std::size_t head = head_.load(std::memory_order_acquire);
            const std::size_t tail = tail_.load(std::memory_order_acquire);
            return head - tail;
        }

        /**
         * @brief Get queue capacity
         */
        [[nodiscard]] constexpr std::size_t capacity() const noexcept {
            return Capacity;
        }

    private:
        // Mask for wrap-around (Capacity - 1 for power-of-2)
        static constexpr std::size_t mask_ = Capacity - 1;

        // === Cache Line 0: Producer write, Consumer read ===
        alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> head_;

        // === Cache Line 1: Producer local cache (no sharing) ===
        alignas(CACHE_LINE_SIZE) std::size_t cached_tail_;

        // === Cache Line 2: Consumer write, Producer read ===
        alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> tail_;

        // === Cache Line 3: Consumer local cache (no sharing) ===
        alignas(CACHE_LINE_SIZE) std::size_t cached_head_;

        // Ring buffer - separately allocated
        T* buffer_;
    };

} // namespace hft

#endif // SPSC_QUEUE_HPP