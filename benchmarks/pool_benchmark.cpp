#include <benchmark/benchmark.h>

#include <tsq/thread_pool.h>

#include <asio/post.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>

#include <cstddef>
#include <cstdint>
#include <future>
#include <utility>
#include <vector>

static void burn(int64_t n) {
  int64_t sum = 0;
  for (int64_t i = 0; i < n; i++) {
    benchmark::DoNotOptimize(sum);
    sum++;
  }
}

static void BM_SimpleCycle(benchmark::State &state) {
  const int64_t workLoad = state.range(0);
  for (auto _ : state) {
    burn(workLoad);
  }
  state.SetItemsProcessed(state.iterations() * workLoad);
}
BENCHMARK(BM_SimpleCycle)->Arg(10000)->Arg(100000)->Arg(1000000)->UseRealTime();

// The pool benchmarks are templates over these adapters, so tsq and asio run
// exactly the same code. Every benchmark is registered once per pool, with
// "pool:tsq" or "pool:asio" in its name.
struct tsq_pool {
  tsq::thread_pool pool;
  explicit tsq_pool(std::size_t nThreads) : pool{nThreads} {}
  template <typename F> auto submit(F &&func) {
    return pool.submit(std::forward<F>(func));
  }
};

// asio::thread_pool has no submit: post() with use_future wraps the callable
// in a packaged_task and returns its future, like tsq does. Its destructor
// drops pending tasks, which does not matter here: every benchmark waits for
// all its futures first.
struct asio_pool {
  asio::thread_pool pool;
  explicit asio_pool(std::size_t nThreads) : pool{nThreads} {}
  template <typename F> auto submit(F &&func) {
    return asio::post(pool, asio::use_future(std::forward<F>(func)));
  }
};

template <typename Pool>
static void BM_SimpleCycleInPool(benchmark::State &state) {
  const std::size_t nThreads = state.range(1);
  const std::size_t nTasks = state.range(2);
  const int64_t workLoad = state.range(0) / nTasks ;
  Pool pool{nThreads};
  std::vector<std::future<void>> results;
  results.reserve(nTasks);

  for (auto _ : state) {
    for (std::size_t i = 0; i < nTasks; ++i) {
      auto fut = pool.submit([workLoad]{ burn(workLoad); });
      results.push_back(std::move(fut));
    }
    for (auto &fut : results) {
      fut.get();
    }
    results.clear();
  }
  state.SetItemsProcessed(state.iterations() * workLoad * nTasks);
}

static void simple_cycle_args(benchmark::internal::Benchmark *b) {
  b->ArgsProduct({{1000000}, {1, 4, 8, 28}, {1, 28, 100, 300}})
      ->ArgNames({"work", "nThreads", "nTasks"})
      ->UseRealTime();
}
BENCHMARK_TEMPLATE(BM_SimpleCycleInPool, tsq_pool)
    ->Name("BM_SimpleCycleInPool/pool:tsq")
    ->Apply(simple_cycle_args);
BENCHMARK_TEMPLATE(BM_SimpleCycleInPool, asio_pool)
    ->Name("BM_SimpleCycleInPool/pool:asio")
    ->Apply(simple_cycle_args);

// Strong scaling: the same total work split into one task per thread.
// ArgsProduct cannot tie nTasks to nThreads, so the arguments are built by
// hand. Goes past 28 threads to show oversubscription.
static void strong_scaling_args(benchmark::internal::Benchmark *b) {
  for (int64_t n = 1; n <= 35; ++n) {
    b->Args({10000000, n, n * 2});
  }
  b->ArgNames({"work", "nThreads", "nTasks"})->UseRealTime();
}
BENCHMARK_TEMPLATE(BM_SimpleCycleInPool, tsq_pool)
    ->Name("BM_StrongScaling/pool:tsq")
    ->Apply(strong_scaling_args);
BENCHMARK_TEMPLATE(BM_SimpleCycleInPool, asio_pool)
    ->Name("BM_StrongScaling/pool:asio")
    ->Apply(strong_scaling_args);

// Each iteration submits a batch of nTasks tasks of taskSize units each and
// waits for all of them. Unlike BM_SimpleCycleInPool, the size of one task
// is a parameter of its own, so the registrations below can vary one thing
// at a time.
template <typename Pool>
static void BM_Batch(benchmark::State &state) {
  const std::size_t nThreads = state.range(0);
  const std::size_t nTasks = state.range(1);
  const int64_t taskSize = state.range(2);
  Pool pool{nThreads};
  std::vector<std::future<void>> results;
  results.reserve(nTasks);

  for (auto _ : state) {
    for (std::size_t i = 0; i < nTasks; ++i) {
      results.push_back(pool.submit([taskSize] { burn(taskSize); }));
    }
    for (auto &fut : results) {
      fut.get();
    }
    results.clear();
  }
  // items_per_second is tasks per second; per_task is its inverse, the wall
  // time of one task including the work (divide by nThreads for CPU time).
  state.SetItemsProcessed(state.iterations() * nTasks);
  state.counters["per_task"] = benchmark::Counter(
      static_cast<double>(nTasks),
      benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
}

// 1. Pure overhead: empty tasks, only the batch size changes.
static void empty_tasks_args(benchmark::internal::Benchmark *b) {
  b->ArgsProduct({{1, 4, 8, 28}, {1, 10, 100, 1000, 10000}, {0}})
      ->ArgNames({"nThreads", "nTasks", "taskSize"})
      ->UseRealTime();
}
BENCHMARK_TEMPLATE(BM_Batch, tsq_pool)
    ->Name("BM_EmptyTasks/pool:tsq")
    ->Apply(empty_tasks_args);
BENCHMARK_TEMPLATE(BM_Batch, asio_pool)
    ->Name("BM_EmptyTasks/pool:asio")
    ->Apply(empty_tasks_args);

// 2. Fixed task size (~2 us), the batch size changes: total work grows with
// nTasks, but every task looks the same.
static void fixed_task_size_args(benchmark::internal::Benchmark *b) {
  b->ArgsProduct({{1, 4, 8, 28}, {10, 100, 1000, 10000}, {10000}})
      ->ArgNames({"nThreads", "nTasks", "taskSize"})
      ->UseRealTime();
}
BENCHMARK_TEMPLATE(BM_Batch, tsq_pool)
    ->Name("BM_FixedTaskSize/pool:tsq")
    ->Apply(fixed_task_size_args);
BENCHMARK_TEMPLATE(BM_Batch, asio_pool)
    ->Name("BM_FixedTaskSize/pool:asio")
    ->Apply(fixed_task_size_args);

// 3. Fixed batch size, the task size changes: shows how the overhead of one
// task depends on how long the task runs.
static void task_size_sweep_args(benchmark::internal::Benchmark *b) {
  b->ArgsProduct({{1, 4, 8, 28}, {100}, {0, 100, 1000, 10000, 100000}})
      ->ArgNames({"nThreads", "nTasks", "taskSize"})
      ->UseRealTime();
}
BENCHMARK_TEMPLATE(BM_Batch, tsq_pool)
    ->Name("BM_TaskSizeSweep/pool:tsq")
    ->Apply(task_size_sweep_args);
BENCHMARK_TEMPLATE(BM_Batch, asio_pool)
    ->Name("BM_TaskSizeSweep/pool:asio")
    ->Apply(task_size_sweep_args);

// Nested submit: nThreads parent tasks each submit nChildren child tasks
// from inside the pool (nested:1), compared with the main thread submitting
// the same children itself (nested:0). Parents do not wait for their
// children, that could block every worker; they return the children's
// futures, and the main thread waits for them.
template <typename Pool>
static void BM_NestedSubmit(benchmark::State &state) {
  const std::size_t nThreads = state.range(0);
  const std::size_t nChildren = state.range(1);
  const int64_t taskSize = state.range(2);
  const bool nested = state.range(3) != 0;
  Pool pool{nThreads};

  using children_t = std::vector<std::future<void>>;
  auto spawn = [&pool, nChildren, taskSize] {
    children_t children;
    children.reserve(nChildren);
    for (std::size_t i = 0; i < nChildren; ++i) {
      children.push_back(pool.submit([taskSize] { burn(taskSize); }));
    }
    return children;
  };
  std::vector<std::future<children_t>> parents;
  parents.reserve(nThreads);
  std::vector<children_t> batches;
  batches.reserve(nThreads);

  for (auto _ : state) {
    for (std::size_t i = 0; i < nThreads; ++i) {
      if (nested) {
        parents.push_back(pool.submit(spawn));
      } else {
        batches.push_back(spawn());
      }
    }
    for (auto &parent : parents) {
      batches.push_back(parent.get());
    }
    for (auto &children : batches) {
      for (auto &fut : children) {
        fut.get();
      }
    }
    parents.clear();
    batches.clear();
  }
  // Counted per child task, as in BM_Batch; parent tasks are not counted.
  const std::size_t nTasks = nThreads * nChildren;
  state.SetItemsProcessed(state.iterations() * nTasks);
  state.counters["per_task"] = benchmark::Counter(
      static_cast<double>(nTasks),
      benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
}

static void nested_submit_args(benchmark::internal::Benchmark *b) {
  b->ArgsProduct({{1, 2, 4, 8, 16}, {1000}, {0, 10000}, {0, 1}})
      ->ArgNames({"nThreads", "nChildren", "taskSize", "nested"})
      ->UseRealTime();
}
BENCHMARK_TEMPLATE(BM_NestedSubmit, tsq_pool)
    ->Name("BM_NestedSubmit/pool:tsq")
    ->Apply(nested_submit_args);
BENCHMARK_TEMPLATE(BM_NestedSubmit, asio_pool)
    ->Name("BM_NestedSubmit/pool:asio")
    ->Apply(nested_submit_args);
