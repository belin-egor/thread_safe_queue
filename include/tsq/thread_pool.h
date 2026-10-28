#pragma once

#include "tsq/ts_queue.h"

#include <cstddef>
#include <functional>
#include <stdexcept>
#include <thread>
#include <vector>

namespace tsq {

class thread_pool final {
private:
  ts_queue<std::function<void()>> tasks_;
  std::vector<std::jthread> workers_;
public:
  explicit thread_pool(std::size_t poolSize) {
    if (poolSize == 0) {
      throw std::invalid_argument{"pool size is zero"};
    }
    workers_.reserve(poolSize);
    try {
      for (std::size_t i = 0; i < poolSize; ++i) {
        workers_.emplace_back([this]{
          while (auto optVal = tasks_.pop()) {
            (*optVal)();
          }
        });
      }
    }
    catch(...) {
      tasks_.close();
      throw;
    }
  }

  thread_pool(const thread_pool &other) = delete;
  thread_pool(thread_pool &&other) = delete;
  thread_pool &operator=(const thread_pool &other) = delete;
  thread_pool &operator=(thread_pool &&other) = delete;

  ~thread_pool() {
    tasks_.close();
  }
  bool submit(std::nullptr_t) = delete;
  // The task must not throw: an exception escaping a task calls std::terminate.
  // TODO: propagate exceptions via std::future.
  [[nodiscard]] bool submit(std::function<void()> task) {
    if (!task) {
      throw std::invalid_argument{"task is empty"};
    }
    return tasks_.push(std::move(task));
  }
};

}  // namespace tsq
