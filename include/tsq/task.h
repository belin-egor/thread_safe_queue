#pragma once

#include <concepts>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace tsq {

namespace detail {

template <typename T>
struct is_std_function : std::false_type {};

template <typename Sig>
struct is_std_function<std::function<Sig>> : std::true_type {};

template <typename F>
bool is_null_callable(const F &func) {
  using raw_t = std::remove_cvref_t<F>;
  if constexpr (std::is_pointer_v<raw_t> || is_std_function<raw_t>::value) {
    if (!func) {
      return true;
    }
  }
  return false;
}

} // namespace detail

// Move-only type-erased wrapper for a callable with signature void().
// Unlike std::function, it also accepts move-only callables.
class task {
  class callable_base {
  public:
    virtual void call() = 0;
    virtual ~callable_base() = default;
  };

  template <typename F>
  class callable_impl final : public callable_base {
    F func_;
  public:
    explicit callable_impl(F func): func_(std::move(func)) {}
    void call() override {
      func_();
    }
  };

  std::unique_ptr<callable_base> ptr_;

public:
  task() = default;

  // Not explicit: lets pool.submit([] { ... }) convert a lambda to task.
  template <typename F>
    requires (!std::same_as<std::remove_cvref_t<F>, task>) &&
             std::invocable<std::decay_t<F>&>
  task(F &&func) {
    // Like std::function: a null function pointer or an empty std::function
    // gives an empty task instead of a task that fails when called.
    if (detail::is_null_callable(func)) {
      return;
    }

    ptr_ = std::make_unique<callable_impl<std::decay_t<F>>>(
        std::forward<F>(func));
  }

  // Throws std::bad_function_call if the task is empty.
  void operator()() {
    if (!ptr_) {
      throw std::bad_function_call{};
    }
    ptr_->call();
  }

  explicit operator bool() const {return ptr_ != nullptr;}
};

}  // namespace tsq
