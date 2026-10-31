include(FetchContent)

FetchContent_Declare(
  benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG v1.9.4
  GIT_SHALLOW TRUE
)
# Build only the library: no tests of Google Benchmark itself, no install.
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(benchmark)

# Standalone (non-Boost) Asio, header-only: its thread_pool is the baseline
# tsq::thread_pool is compared with. The repository has no CMake support, so
# it is only downloaded and wrapped in an INTERFACE target here.
FetchContent_Declare(
  asio
  GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
  GIT_TAG asio-1-36-0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(asio)

add_library(asio_standalone INTERFACE)
# SYSTEM: our -Wall -Wextra -Wpedantic should not report warnings in asio.
target_include_directories(asio_standalone SYSTEM INTERFACE
                           ${asio_SOURCE_DIR}/asio/include)
target_compile_definitions(asio_standalone INTERFACE ASIO_STANDALONE
                                                     ASIO_NO_DEPRECATED)
target_link_libraries(asio_standalone INTERFACE Threads::Threads)

add_executable(pool_benchmark pool_benchmark.cpp)
target_link_libraries(pool_benchmark PRIVATE tsq asio_standalone
                                             benchmark::benchmark_main)
