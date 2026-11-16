cmake_minimum_required(VERSION 3.16)
project(spsc_queue VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Release unless told otherwise: the benchmark numbers in the README are -O3.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Optimization flags for release builds
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")

# Include directory
include_directories(${CMAKE_SOURCE_DIR}/include)

# Unit tests executable
add_executable(test_spsc_queue tests/test_spsc_queue.cpp)
target_link_libraries(test_spsc_queue PRIVATE pthread)

# Benchmark executable
add_executable(benchmark benchmarks/benchmark.cpp)
target_link_libraries(benchmark PRIVATE pthread)

# Enable testing
enable_testing()
add_test(NAME unit_tests COMMAND test_spsc_queue)

# Custom targets for convenience
add_custom_target(run_tests
        COMMAND ${CMAKE_BINARY_DIR}/test_spsc_queue
        DEPENDS test_spsc_queue
        COMMENT "Running unit tests..."
)

add_custom_target(run_benchmark
        COMMAND ${CMAKE_BINARY_DIR}/benchmark
        DEPENDS benchmark
        COMMENT "Running benchmarks..."
)
