#include <gtest/gtest.h>

#include <tsq/thread_pool.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <latch>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
  // True if submit(nullptr) compiles for the pool type P.
  template <typename P>
  concept CanSubmitNullptr = requires(P &pool) { pool.submit(nullptr); };

  template <typename F>
  using submit_result_t =
      decltype(std::declval<tsq::thread_pool&>().submit(std::declval<F>()));
}

static_assert(!CanSubmitNullptr<tsq::thread_pool>,
              "submit(nullptr) must not compile");

// submit() returns a future of whatever the task returns.
static_assert(std::is_same_v<submit_result_t<int (*)()>, std::future<int>>);
static_assert(std::is_same_v<submit_result_t<void (*)()>, std::future<void>>);
static_assert(std::is_same_v<submit_result_t<std::string (*)()>,
                             std::future<std::string>>);

TEST(ThreadPoolTest, ZeroSizeThrows) {
  EXPECT_THROW(tsq::thread_pool pool(0), std::invalid_argument);
}

TEST(ThreadPoolTest, EmptyTaskThrows) {
  tsq::thread_pool pool(1);
  std::function<void()> emptyTask;
  EXPECT_THROW((void)pool.submit(emptyTask), std::invalid_argument);
}

TEST(ThreadPoolTest, NullFunctionPointerThrows) {
  tsq::thread_pool pool(1);
  void (*nullFunction)() = nullptr;
  EXPECT_THROW((void)pool.submit(nullFunction), std::invalid_argument);
}

TEST(ThreadPoolTest, ReturnsResult) {
  tsq::thread_pool pool(2);
  auto future = pool.submit([]{ return 6 * 7; });
  EXPECT_EQ(future.get(), 42);
}

TEST(ThreadPoolTest, ReturnsMoveOnlyResult) {
  tsq::thread_pool pool(2);
  auto future = pool.submit([]{ return std::make_unique<int>(42); });
  std::unique_ptr<int> result = future.get();
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(*result, 42);
}

TEST(ThreadPoolTest, VoidTaskCompletes) {
  tsq::thread_pool pool(2);
  // No atomic needed: get() synchronizes with the end of the task.
  int value = 0;
  auto future = pool.submit([&value]{ value = 42; });
  future.get();
  EXPECT_EQ(value, 42);
}

TEST(ThreadPoolTest, MoveOnlyTask) {
  tsq::thread_pool pool(2);
  auto value = std::make_unique<int>(42);
  auto future = pool.submit([value = std::move(value)]{ return *value; });
  EXPECT_EQ(future.get(), 42);
}

TEST(ThreadPoolTest, ExceptionPropagatesToFuture) {
  tsq::thread_pool pool(2);
  auto future = pool.submit([]() -> int {
    throw std::runtime_error("task failed");
  });
  EXPECT_THROW(future.get(), std::runtime_error);
}

TEST(ThreadPoolTest, WorkerSurvivesThrowingTask) {
  // With a single worker, the second task can only run if the first
  // exception did not kill the worker thread.
  tsq::thread_pool pool(1);
  auto failing = pool.submit([]{ throw std::runtime_error("task failed"); });
  auto working = pool.submit([]{ return 7; });
  EXPECT_THROW(failing.get(), std::runtime_error);
  EXPECT_EQ(working.get(), 7);
}

TEST(ThreadPoolTest, AllTasksExecuted) {
  const int N = 10000;
  tsq::thread_pool pool(4);
  std::vector<std::future<int>> futures;
  futures.reserve(N);
  for (int i = 0; i < N; ++i) {
    futures.push_back(pool.submit([i]{ return i; }));
  }

  int64_t sum = 0;
  for (auto &future : futures) {
    sum += future.get();
  }
  EXPECT_EQ(sum, int64_t(N) * (N - 1) / 2);
}

TEST(ThreadPoolTest, ConcurrentSubmitters) {
  const int submitterCount = 4;
  const int tasksPerSubmitter = 1000;
  std::atomic<int> counter = 0;
  {
    tsq::thread_pool pool(4);
    std::latch start{submitterCount};
    std::vector<std::jthread> submitters;
    submitters.reserve(submitterCount);
    for (int i = 0; i < submitterCount; ++i) {
      submitters.emplace_back([&pool, &start, &counter]{
        start.arrive_and_wait();
        for (int j = 0; j < tasksPerSubmitter; ++j) {
          (void)pool.submit([&counter]{ ++counter; });
        }
      });
    }
  }
  EXPECT_EQ(counter, submitterCount * tasksPerSubmitter);
}

TEST(ThreadPoolTest, NestedSubmit) {
  // The outer task does not wait for the inner one, so a single worker is
  // enough: waiting inside the pool could deadlock it.
  tsq::thread_pool pool(1);
  auto outer = pool.submit([&pool]{
    return pool.submit([]{ return 42; });
  });
  std::future<int> inner = outer.get();
  EXPECT_EQ(inner.get(), 42);
}

TEST(ThreadPoolTest, DestructorRunsPendingTasks) {
  const int N = 1000;
  std::atomic<int> counter = 0;
  {
    // Declaration order matters: the pool is destroyed first and blocks in
    // join() until the releaser opens the gate, then the releaser is joined.
    std::latch gate{1};
    std::jthread releaser;
    tsq::thread_pool pool(1);

    // The only worker is stuck here, so all tasks below stay in the queue.
    // A future from packaged_task does not block in its destructor, so the
    // futures can be dropped.
    (void)pool.submit([&gate]{ gate.wait(); });
    for (int i = 0; i < N; ++i) {
      (void)pool.submit([&counter]{ ++counter; });
    }

    // Open the gate only after the pool destructor has closed the queue.
    releaser = std::jthread([&gate]{
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      gate.count_down();
    });
  }
  EXPECT_EQ(counter, N);
}

TEST(ThreadPoolTest, SubmitDuringShutdownThrows) {
  std::atomic<bool> rejected = false;
  {
    // Same trick as above: the task tries to submit only after the pool
    // destructor has closed the queue and is waiting in join().
    std::latch gate{1};
    std::jthread releaser;
    tsq::thread_pool pool(1);

    (void)pool.submit([&pool, &gate, &rejected]{
      gate.wait();
      try {
        (void)pool.submit([]{});
      } catch (const std::runtime_error &) {
        rejected = true;
      }
    });

    releaser = std::jthread([&gate]{
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      gate.count_down();
    });
  }
  EXPECT_TRUE(rejected);
}

TEST(ThreadPoolTest, SingleThreadPreservesOrder) {
  const int N = 100;
  // No mutex: only the single worker writes, and the test reads after join().
  std::vector<int> order;
  order.reserve(N);
  {
    tsq::thread_pool pool(1);
    for (int i = 0; i < N; ++i) {
      (void)pool.submit([&order, i]{ order.push_back(i); });
    }
  }
  ASSERT_EQ(order.size(), static_cast<std::size_t>(N));
  for (int i = 0; i < N; ++i) {
    EXPECT_EQ(order[i], i);
  }
}

TEST(ThreadPoolTest, TasksRunInParallel) {
  // Each task waits for all the others, so they can only finish if every
  // task runs on its own worker at the same time. Otherwise the test hangs
  // and is killed by the ctest timeout.
  const int threadCount = 4;
  std::latch allStarted{threadCount};
  tsq::thread_pool pool(threadCount);
  std::vector<std::future<void>> futures;
  futures.reserve(threadCount);
  for (int i = 0; i < threadCount; ++i) {
    futures.push_back(pool.submit([&allStarted]{
      allStarted.arrive_and_wait();
    }));
  }
  for (auto &future : futures) {
    future.get();
  }
}
