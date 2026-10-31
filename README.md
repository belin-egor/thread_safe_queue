# tsq: thread-safe queue and thread pool

A small header-only C++20 library written as a learning project on
multithreading. The goal is to build the classic primitives by hand,
understand why each piece is needed, and verify them with sanitizers:

- `tsq::ts_queue<T>`: a blocking queue built on `std::mutex` and
  `std::condition_variable`, with graceful shutdown via `close()`;
- `tsq::thread_pool`: a fixed-size pool of `std::jthread` workers that
  takes tasks from that queue and returns results via `std::future`;
- `tsq::task`: a move-only type-erased `void()` callable used as the
  pool's task type (unlike `std::function`, it accepts move-only lambdas).

## Roadmap

The project grows in small stages; each stage is merged into `main` as one
squashed pull request. Want to build it yourself? See
[docs/learning-path.md](docs/learning-path.md): what each stage builds
and which limitation leads to the next one.

- [x] **1. Build setup.** CMake, C++20, CMake presets, ThreadSanitizer and
  AddressSanitizer builds.
- [x] **2. Thread-safe queue.** `push`, blocking `pop`, non-blocking
  `try_pop`, `empty`, `size`.
- [x] **3. Closing the queue.** `close()` rejects new items, wakes all
  waiting consumers and lets them drain what is left.
- [x] **4. Thread pool.** Workers run `std::function<void()>` tasks; the
  destructor finishes all pending tasks before joining.
- [x] **5. Results via `std::future`.** `submit` returns a future with the
  task's result or its exception (`std::packaged_task`); tasks are stored
  in the move-only `tsq::task` instead of `std::function`.
- [x] **6. Benchmarks.** Google Benchmark: overhead per task, scaling with
  threads, nested `submit`, and a comparison with `asio::thread_pool`.
- [ ] **7. Extras (optional).** Work stealing, waiting for all tasks,
  immediate shutdown, nested `submit` deadlock.

## Usage

```cpp
#include <tsq/thread_pool.h>
#include <tsq/ts_queue.h>

#include <iostream>
#include <stdexcept>

int main() {
  tsq::ts_queue<int> queue;
  (void)queue.push(42);
  queue.close();
  while (auto value = queue.pop()) {  // std::nullopt once closed and empty
    std::cout << *value << '\n';
  }

  tsq::thread_pool pool(4);
  std::future<int> answer = pool.submit([] { return 6 * 7; });
  std::cout << answer.get() << '\n';  // 42

  auto failed = pool.submit([]() -> int {
    throw std::runtime_error("task failed");
  });
  try {
    failed.get();  // rethrows the exception from the task
  } catch (const std::runtime_error &e) {
    std::cout << e.what() << '\n';
  }
}  // the pool destructor runs every pending task, then joins the workers
```

Contracts worth knowing:

- `push` returns `false` once the queue is closed.
- `thread_pool(0)` and submitting an empty task (a null function pointer
  or an empty `std::function`) throw `std::invalid_argument`.
- An exception thrown by a task is stored in its future and rethrown by
  `get()`; the worker keeps running.
- `submit` may be called from several threads at once, including from
  inside a task. A task that submits while the pool is being destroyed
  gets `std::runtime_error`.
- Dropping a future is fine: the task still runs, and the future's
  destructor does not block (unlike futures from `std::async`).

## Building and testing

Requirements: CMake 3.21+, a C++20 compiler. The presets use `clang++-20`:
on recent Linux kernels the ThreadSanitizer runtime of older toolchains
fails with "unexpected memory mapping".

```sh
cmake --preset tsan              # or: debug, asan, release
cmake --build --preset tsan
ctest --preset tsan
```

Tests use GoogleTest, fetched automatically by CMake. CI builds and tests
all four presets on every pull request.

## Benchmarks

The `release` preset (optimized, no sanitizers) also builds the
benchmarks. They use Google Benchmark and standalone Asio, both fetched by
CMake for the benchmarks only; the library itself has no dependencies.

```sh
cmake --preset release
cmake --build --preset release
./build/release/benchmarks/pool_benchmark --benchmark_filter=BM_EmptyTasks \
    --benchmark_out=results.json --benchmark_out_format=json
python3 benchmarks/plot.py results.json -o plots/   # needs matplotlib
```

Every pool benchmark runs for both `tsq::thread_pool` (`pool:tsq`) and
`asio::thread_pool` (`pool:asio`). What they measure:

- `BM_EmptyTasks`: pure overhead of one task, by batch size;
- `BM_FixedTaskSize`, `BM_TaskSizeSweep`: how the overhead depends on the
  number of tasks and on the size of one task;
- `BM_SimpleCycleInPool`, `BM_StrongScaling`: a fixed amount of work split
  into tasks, by number of threads;
- `BM_NestedSubmit`: tasks that submit tasks from inside the pool.

On an Intel i7-14700HX (8 P-cores + 12 E-cores) one task costs about
0.35 us with one worker and about 1 us with 4 or more: every task passes
through one mutex. Both pools behave about the same, since `asio` also
uses a single shared queue for `post`. For stable numbers pin the run to
one core type (`taskset`) and use `--benchmark_repetitions`; CI builds the
benchmarks but does not run them.

## Using it in another CMake project

```cmake
add_subdirectory(thread_safe_queue)
target_link_libraries(my_app PRIVATE tsq)
```

Tests are built only when this is the top-level project (`TSQ_BUILD_TESTS`).

## Layout

```
docs/          the learning path guide
include/tsq/   the library headers
tests/         GoogleTest unit tests
benchmarks/    Google Benchmark benchmarks and the plot script
examples/      a producer/consumer example
```
