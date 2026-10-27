#include <gtest/gtest.h>

#include <tsq/ts_queue.h>

#include <atomic>
#include <chrono>
#include <latch>
#include <memory>
#include <thread>
#include <vector>

TEST(TsQueueTest, TryPopOnEmptyQueue) {
  tsq::ts_queue<int> q;

  EXPECT_FALSE(q.try_pop().has_value());
}

TEST(TsQueueTest, PushPop) {
  tsq::ts_queue<int> q;
  const int origValue = 100;
  bool open = q.push(origValue);
  ASSERT_TRUE(open);
  auto optVal = q.pop();
  ASSERT_TRUE(optVal.has_value());
  EXPECT_EQ(optVal.value(), origValue);
}

TEST(TsQueueTest, QueueOrdering) {
  tsq::ts_queue<int> q;
  const int N = 10;
  for (int i = 0; i < N; ++i) {
    ASSERT_TRUE(q.push(i));
  }

  for (int i = 0; i < N; ++i) {
    auto optVal = q.pop();
    ASSERT_TRUE(optVal.has_value());
    EXPECT_EQ(optVal.value(), i);
  }
}

TEST(TsQueueTest, PushPopChangeSize) {
  tsq::ts_queue<int> q;
  EXPECT_TRUE(q.empty());
  EXPECT_EQ(q.size(), 0);

  const int N = 10;
  for (int i = 0; i < N; ++i) {
    ASSERT_TRUE(q.push(i));
  }
  EXPECT_EQ(q.size(), N);

  for (int i = 0; i < N/2; ++i) {
    q.pop();
  }
  EXPECT_EQ(q.size(), N - N/2);

  int rest = N - N /2;
  for (int i = 0; i < rest; ++i) {
    q.pop();
  }
  EXPECT_TRUE(q.empty());
}

TEST(TsQueueTest, MoveOnlyType) {
  tsq::ts_queue<std::unique_ptr<int>> q;
  const int n = 100;
  {
    auto ptr = std::make_unique<int>(n);
    ASSERT_TRUE(q.push(std::move(ptr)));
  }
  auto optVal = q.pop();
  ASSERT_TRUE(optVal.has_value());
  EXPECT_EQ(*optVal.value(), n);
}

TEST(TsQueueTest, PushReturnsFalseAfterClose) {
  tsq::ts_queue<int> q;
  q.close();
  EXPECT_FALSE(q.push(100));
}

TEST(TsQueueTest, PopReturnsNulloptAfterClose) {
  tsq::ts_queue<int> q;
  q.close();
  EXPECT_FALSE(q.pop().has_value());
}

TEST(TsQueueTest, PopRestValuesAfterClose) {
  tsq::ts_queue<int> q;
  const int n = 100;
  ASSERT_TRUE(q.push(n));
  q.close();
  EXPECT_EQ(q.pop().value_or(-1), n);
  EXPECT_FALSE(q.pop().has_value());
}

TEST(TsQueueTest, MultipleClose) {
  tsq::ts_queue<int> q;
  const int n = 100;
  ASSERT_TRUE(q.push(n));
  q.close();
  q.close();
  EXPECT_FALSE(q.push(n));
  EXPECT_TRUE(q.pop().has_value());
  EXPECT_FALSE(q.pop().has_value());
}

namespace {
  void pushRange(int64_t start, int64_t end, tsq::ts_queue<int64_t> &tsq) {
    for (auto i = start; i < end; ++i) {
      bool open = tsq.push(i);
      if (!open) return;
    }
  }

  void popAndSum(tsq::ts_queue<int64_t> &tsq, std::atomic<int64_t> &sum) {
    while (auto optVal = tsq.pop()) {
      sum += optVal.value();
    }
  }
}

TEST(TsQueueTest, MultipleThreads) {
  tsq::ts_queue<int64_t> q;
  const int prodCount = 4;
  const int consCount = 4;
  const int N = 100000;
  std::atomic<int64_t> queueSum = 0;
  {
    std::vector<std::jthread> consumers;
    consumers.reserve(consCount);
    for (int i = 0; i < consCount; ++i) {
      consumers.emplace_back(popAndSum, std::ref(q), std::ref(queueSum));
    }

    {
      std::vector<std::jthread> producers;
      producers.reserve(prodCount);
      const int rangeStep = N / prodCount;
      int64_t l = 1;
      int64_t r = rangeStep;
      for (int i = 0; i < prodCount; ++i) {
        r = i == (prodCount - 1) ? (N + 1) : r;
        producers.emplace_back(pushRange, l, r, std::ref(q));
        l = r;
        r += rangeStep;
      }
    }
    q.close();
  }
  const int64_t expected = int64_t(N) * (N + 1) / 2;
  EXPECT_EQ(queueSum, expected);
}

TEST(TsQueueTest, CloseFromMultipleThreads) {
  tsq::ts_queue<int> q;
  const int consCount = 10;
  {
    std::latch start{consCount};
    std::vector<std::jthread> threads;
    threads.reserve(consCount);

    for (int i = 0; i < consCount; ++i) {
      threads.emplace_back([&q, &start]{
        start.arrive_and_wait();
        q.close();
      });
    }
  }
  EXPECT_FALSE(q.pop().has_value());
  EXPECT_FALSE(q.push(100));
}

TEST(TsQueueTest, CloseWakesConsumerThreads) {
  tsq::ts_queue<int> q;
  const int consCount = 10;
  {
    std::vector<std::jthread> threads;
    threads.reserve(consCount);

    for (int i = 0; i < consCount; ++i) {
      threads.emplace_back([&q]{
        EXPECT_FALSE(q.pop().has_value());
      });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    q.close();
  }
}
