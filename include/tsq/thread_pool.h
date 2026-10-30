#pragma once

#include "tsq/task.h"
#include "tsq/ts_queue.h"

#include <cstddef>
#include <future>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace tsq {

class thread_pool final {
private:
  ts_queue<tsq::task> tasks_;
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

  template <typename F>
  [[nodiscard]] auto submit(F &&func) {
    if (detail::is_null_callable(func)) {
      throw std::invalid_argument("empty function");
    }
    using R = std::invoke_result_t<std::decay_t<F>&>;
    std::packaged_task<R()> task{std::forward<F>(func)};
    std::future<R> fut = task.get_future();
    if (!tasks_.push(std::move(task))) {
      throw std::runtime_error("thread_pool is shutting down");
    }
    return fut;
  }
};

}  // namespace tsq
