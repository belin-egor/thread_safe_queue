cmake_minimum_required(VERSION 3.21)
project(thread_safe_queue LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

option(ENABLE_TSAN "Build with ThreadSanitizer" OFF)
option(ENABLE_ASAN "Build with AddressSanitizer + UBSan" OFF)

if(ENABLE_TSAN AND ENABLE_ASAN)
  message(FATAL_ERROR "TSan and ASan cannot be enabled at the same time")
endif()

add_library(tsq INTERFACE)
target_include_directories(tsq INTERFACE include)
target_link_libraries(tsq INTERFACE Threads::Threads)
target_compile_options(tsq INTERFACE -Wall -Wextra -Wpedantic)

add_executable(example examples/example.cpp)
target_link_libraries(example PRIVATE tsq)

if(ENABLE_TSAN)
  target_compile_options(tsq INTERFACE -fsanitize=thread -g -O1)
  target_link_options(tsq INTERFACE -fsanitize=thread)
endif()

if(ENABLE_ASAN)
  target_compile_options(tsq INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
  target_link_options(tsq INTERFACE -fsanitize=address,undefined)
endif()

# Tests are built only when this is the top-level project, so users who
# add tsq as a subdirectory do not download and build GoogleTest.
option(TSQ_BUILD_TESTS "Build tsq tests" ${PROJECT_IS_TOP_LEVEL})

if(TSQ_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
