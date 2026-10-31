#include <benchmark/benchmark.h>

#include <tsq/thread_pool.h>

#include <cstddef>
#include <cstdint>
#include <future>
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

static void BM_SimpleCycleInPool(benchmark::State &state) {
  const std::size_t nThreads = state.range(1);
  const std::size_t nTasks = state.range(2);
  const int64_t workLoad = state.range(0) / nTasks ;
  tsq::thread_pool pool{nThreads};
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

BENCHMARK(BM_SimpleCycleInPool)
    ->ArgsProduct({{1000000}, {1, 4, 8, 28}, {1, 28, 100, 300}})
    ->ArgNames({"work", "nThreads", "nTasks"})
    ->UseRealTime();

// Strong scaling: the same total work split into one task per thread.
// ArgsProduct cannot tie nTasks to nThreads, so the arguments are built by
// hand. Goes past 28 threads to show oversubscription.
BENCHMARK(BM_SimpleCycleInPool)
    ->Name("BM_StrongScaling")
    ->Apply([](benchmark::internal::Benchmark *b) {
      for (int64_t n = 1; n <= 35; ++n) {
        b->Args({10000000, n, n});
      }
    })
    ->ArgNames({"work", "nThreads", "nTasks"})
    ->UseRealTime();

// Each iteration submits a batch of nTasks tasks of taskSize units each and
// waits for all of them. Unlike BM_SimpleCycleInPool, the size of one task
// is a parameter of its own, so the registrations below can vary one thing
// at a time.
static void BM_Batch(benchmark::State &state) {
  const std::size_t nThreads = state.range(0);
  const std::size_t nTasks = state.range(1);
  const int64_t taskSize = state.range(2);
  tsq::thread_pool pool{nThreads};
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
BENCHMARK(BM_Batch)
    ->Name("BM_EmptyTasks")
    ->ArgsProduct({{1, 4, 8, 28}, {1, 10, 100, 1000, 10000}, {0}})
    ->ArgNames({"nThreads", "nTasks", "taskSize"})
    ->UseRealTime();

// 2. Fixed task size (~2 us), the batch size changes: total work grows with
// nTasks, but every task looks the same.
BENCHMARK(BM_Batch)
    ->Name("BM_FixedTaskSize")
    ->ArgsProduct({{1, 4, 8, 28}, {10, 100, 1000, 10000}, {10000}})
    ->ArgNames({"nThreads", "nTasks", "taskSize"})
    ->UseRealTime();

// 3. Fixed batch size, the task size changes: shows how the overhead of one
// task depends on how long the task runs.
BENCHMARK(BM_Batch)
    ->Name("BM_TaskSizeSweep")
    ->ArgsProduct({{1, 4, 8, 28}, {100}, {0, 100, 1000, 10000, 100000}})
    ->ArgNames({"nThreads", "nTasks", "taskSize"})
    ->UseRealTime();
