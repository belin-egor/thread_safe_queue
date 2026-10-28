# tsq: thread-safe queue and thread pool

A small header-only C++20 library written as a learning project on
multithreading. The goal is to build the classic primitives by hand,
understand why each piece is needed, and verify them with sanitizers:

- `tsq::ts_queue<T>`: a blocking queue built on `std::mutex` and
  `std::condition_variable`, with graceful shutdown via `close()`;
- `tsq::thread_pool`: a fixed-size pool of `std::jthread` workers that
  takes tasks from that queue.

## Roadmap

The project grows in small stages; each stage is merged into `main` as one
squashed pull request.

- [x] **1. Build setup.** CMake, C++20, CMake presets, ThreadSanitizer and
  AddressSanitizer builds.
- [x] **2. Thread-safe queue.** `push`, blocking `pop`, non-blocking
  `try_pop`, `empty`, `size`.
- [x] **3. Closing the queue.** `close()` rejects new items, wakes all
  waiting consumers and lets them drain what is left.
- [x] **4. Thread pool.** Workers run `std::function<void()>` tasks; the
  destructor finishes all pending tasks before joining.
- [ ] **5. Results via `std::future`.** `submit` returns a future with the
  task's result or its exception (`std::packaged_task`).
- [ ] **6. Extras (optional).** Waiting for all tasks, immediate shutdown,
  nested `submit` deadlock, benchmarks.

## Usage

```cpp
#include <tsq/thread_pool.h>
#include <tsq/ts_queue.h>

#include <atomic>
#include <iostream>

int main() {
  tsq::ts_queue<int> queue;
  (void)queue.push(42);
  queue.close();
  while (auto value = queue.pop()) {  // std::nullopt once closed and empty
    std::cout << *value << '\n';
  }

  std::atomic<int> counter = 0;
  {
    tsq::thread_pool pool(4);
    for (int i = 0; i < 100; ++i) {
      (void)pool.submit([&counter] { ++counter; });
    }
  }  // the destructor runs every pending task, then joins the workers
  std::cout << counter << '\n';  // 100
}
```

Contracts worth knowing:

- `push` and `submit` return `false` once the queue or the pool is closed.
- `thread_pool(0)` and submitting an empty task throw
  `std::invalid_argument`.
- A task must not throw: an exception escaping a task calls
  `std::terminate` (until stage 5).

## Building and testing

Requirements: CMake 3.21+, a C++20 compiler. The presets use `clang++-20`:
on recent Linux kernels the ThreadSanitizer runtime of older toolchains
fails with "unexpected memory mapping".

```sh
cmake --preset tsan              # or: debug, asan
cmake --build --preset tsan
ctest --preset tsan
```

Tests use GoogleTest, fetched automatically by CMake. CI runs all three
presets on every pull request.

## Using it in another CMake project

```cmake
add_subdirectory(thread_safe_queue)
target_link_libraries(my_app PRIVATE tsq)
```

Tests are built only when this is the top-level project (`TSQ_BUILD_TESTS`).

## Layout

```
include/tsq/   the library headers
tests/         GoogleTest unit tests
examples/      a producer/consumer example
```
