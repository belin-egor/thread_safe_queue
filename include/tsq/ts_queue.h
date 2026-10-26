#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <optional>

namespace tsq {

template <typename T>
class ts_queue final {
  mutable std::mutex m_;
  std::condition_variable cv_;
  bool closed_{false};
  std::queue<T> q_;

public:
  ts_queue() = default;
  ts_queue(const ts_queue&other) = delete;
  ts_queue& operator=(const ts_queue &other) = delete;

  ts_queue(ts_queue &&other) = delete;
  ts_queue& operator=(ts_queue &&other) = delete;

  [[nodiscard]] bool push(T value) {
    {
      std::lock_guard lock(m_);
      if (closed_) {
        return false;
      }
      q_.push(std::move(value));
    }
    cv_.notify_one();
    return true;
  }

  std::optional<T> pop() {
    std::optional<T> result;
    {
      std::unique_lock lock(m_);
      cv_.wait(lock, [this]{ return !q_.empty() || closed_; });
      if (!q_.empty()) {
        result = std::move(q_.front());
        q_.pop();
      }
    }
    return result;
  }

  std::optional<T> try_pop() {
    std::optional<T> result;
    {
      std::lock_guard lock(m_);
      if (!q_.empty()) {
        result = std::move(q_.front());
        q_.pop();
      }
    }
    return result;
  }

  bool empty() const {
    std::lock_guard lock(m_);
    return q_.empty();
  }

  std::size_t size() const {
    std::lock_guard lock(m_);
    return q_.size();
  }

  void close() {
    {
      std::lock_guard lock(m_);
      closed_ = true;
    }
    cv_.notify_all();
  }
};

}  // namespace tsq
