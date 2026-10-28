#include <gtest/gtest.h>

#include <tsq/thread_pool.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <latch>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
  // True if submit(nullptr) compiles for the pool type P.
  template <typename P>
  concept CanSubmitNullptr = requires(P &pool) { pool.submit(nullptr); };
}

static_assert(!CanSubmitNullptr<tsq::thread_pool>,
              "submit(nullptr) must not compile");

TEST(ThreadPoolTest, ZeroSizeThrows) {
  EXPECT_THROW(tsq::thread_pool pool(0), std::invalid_argument);
}

TEST(ThreadPoolTest, EmptyTaskThrows) {
  tsq::thread_pool pool(1);
  std::function<void()> emptyTask;
  EXPECT_THROW((void)pool.submit(emptyTask), std::invalid_argument);
}

TEST(ThreadPoolTest, AllTasksExecuted) {
  const int N = 10000;
  std::atomic<int> counter = 0;
  {
    tsq::thread_pool pool(4);
    for (int i = 0; i < N; ++i) {
      EXPECT_TRUE(pool.submit([&counter]{ ++counter; }));
    }
  }
  // The destructor waits until every submitted task has finished.
  EXPECT_EQ(counter, N);
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
    EXPECT_TRUE(pool.submit([&gate]{ gate.wait(); }));
    for (int i = 0; i < N; ++i) {
      EXPECT_TRUE(pool.submit([&counter]{ ++counter; }));
    }

    // Open the gate only after the pool destructor has closed the queue.
    releaser = std::jthread([&gate]{
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      gate.count_down();
    });
  }
  EXPECT_EQ(counter, N);
}

TEST(ThreadPoolTest, SingleThreadPreservesOrder) {
  const int N = 100;
  // No mutex: only the single worker writes, and the test reads after join().
  std::vector<int> order;
  order.reserve(N);
  {
    tsq::thread_pool pool(1);
    for (int i = 0; i < N; ++i) {
      EXPECT_TRUE(pool.submit([&order, i]{ order.push_back(i); }));
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
  std::atomic<int> counter = 0;
  {
    std::latch allStarted{threadCount};
    tsq::thread_pool pool(threadCount);
    for (int i = 0; i < threadCount; ++i) {
      EXPECT_TRUE(pool.submit([&allStarted, &counter]{
        allStarted.arrive_and_wait();
        ++counter;
      }));
    }
  }
  EXPECT_EQ(counter, threadCount);
}
