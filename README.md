# spsc_queue

A wait-free single-producer, single-consumer queue in C++17 (header-only):
`include/spsc_queue.hpp`.

- A fixed ring of `Capacity` slots (a power of two, so the index wraps with a
  mask).
- The producer owns `head_`, the consumer owns `tail_`. Each side keeps a cached
  copy of the other's index and reloads it only when the queue looks full or
  empty.
- Acquire and release ordering only. `try_push` and `try_pop` never wait;
  `push` and `pop` spin until they succeed.
- The producer's and the consumer's fields sit on separate cache lines: 128 B
  on Apple Silicon (`sysctl hw.cachelinesize` on an M1), 64 B elsewhere.

## Build, test, run

    cmake -S . -B build && cmake --build build -j
    ctest --test-dir build --output-on-failure
    ./build/benchmark

The default build is Release (-O3). The tests use a `CHECK` macro, not
`assert()`, so they also check in Release.

The tests cover push and pop, full and empty, FIFO order, wrap-around, moving a
`std::string` through, and two threads: a producer pushes 10 million items
through a 64-slot queue and the consumer checks that each one arrives once, in
order and whole.

## Results

Machine: Apple M1 (MacBookAir10,1, 4 P-cores + 4 E-cores, 8 GB), macOS 15.6,
Apple clang 17.0.0, Release build. Raw output of four runs:
`results/benchmark.txt`.

| Measurement | Result |
|---|---|
| Producer thread to consumer thread, `uint64_t` | median 45.6 ns per item, range 13 to 60 ns over 12 runs |
| Producer thread to consumer thread, 40 B struct | median 72.5 ns per item, range 33 to 80 ns over 12 runs |
| One push + one pop on one thread, `uint64_t` | 8.1 to 9.5 ns per pair |
| One push + one pop on one thread, 40 B struct | 10.2 to 11.9 ns per pair |

- Throughput moves a lot from run to run. macOS cannot pin a thread to a core,
  so the two threads may land on performance or efficiency cores, and other
  programs on the machine compete for them.
- Padding at 128 B instead of 64 B made no difference that alternating runs
  could resolve. For `uint64_t` the medians were 21.8 M items per second at
  128 B and 23.8 M at 64 B, with overlapping ranges (`results/padding_ab.txt`).

## Limits of the timer

On Apple Silicon `std::chrono::high_resolution_clock` ticks every 41.67 ns, which
is longer than a push and a pop together. So the round trip is timed in batches:
10 million pairs between two clock reads, best of 5. The throughput figures time
10 million items between two clock reads.
