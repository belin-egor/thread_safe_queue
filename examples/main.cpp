#include <atomic>
#include <cstdint>
#include <functional>
#include <iostream>
#include <thread>

#include <tsq/ts_queue.h>

void produce(int64_t start, int64_t end, tsq::ts_queue<int64_t> &tsq) {
  for (auto i = start; i < end; ++i) {
    bool open = tsq.push(i);
    if (!open) return;
  }
}

void consume(tsq::ts_queue<int64_t> &tsq, std::atomic<int64_t> &sum) {
  while (auto optVal = tsq.pop()) {
    sum += optVal.value();
  }
}

int main() {
  tsq::ts_queue<int64_t> tsq;
  std::atomic<int64_t> sum = 0;
  const int64_t N = 100000;
  {
    auto consumer1 = std::jthread(consume, std::ref(tsq), std::ref(sum));
    auto consumer2 = std::jthread(consume, std::ref(tsq), std::ref(sum));
    auto consumer3 = std::jthread(consume, std::ref(tsq), std::ref(sum));
    {
      auto producer1 = std::jthread(produce, 1, N/3, std::ref(tsq));
      auto producer2 = std::jthread(produce, N/3, N/3 * 2, std::ref(tsq));
      auto producer3 = std::jthread(produce, N/3 * 2, N + 1, std::ref(tsq));
    }
    tsq.close();
  }

  const int64_t expected =  N * (N + 1) /2;
  if (sum != expected) {
    std::cerr << "FAIL: sum=" << sum << " expected=" << expected <<"\n";
    return 1;
  }
  std::cout << sum << std::endl;

  bool open = tsq.push(100);
  if (open) {
    std::cerr << "FAIL: push returned true after close\n";
    return 2;
  }

  auto popRes = tsq.pop();
  if (popRes.has_value()) {
    std::cerr << "FAIL: pop returned value " << popRes.value() << " after close\n";
    return 3;
  }

  return 0;
}
