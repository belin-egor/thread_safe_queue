#include <gtest/gtest.h>

#include <tsq/task.h>

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace {
  int freeFunctionCalls = 0;

  void freeFunction() {
    ++freeFunctionCalls;
  }
}

// Move-only, like std::unique_ptr.
static_assert(!std::is_copy_constructible_v<tsq::task>);
static_assert(!std::is_copy_assignable_v<tsq::task>);
static_assert(std::is_nothrow_move_constructible_v<tsq::task>);
static_assert(std::is_nothrow_move_assignable_v<tsq::task>);

// Only callables with no arguments are accepted, and they convert implicitly.
static_assert(!std::is_constructible_v<tsq::task, int>);
static_assert(!std::is_constructible_v<tsq::task, void (*)(int)>);
static_assert(std::is_convertible_v<void (*)(), tsq::task>);

TEST(TaskTest, DefaultIsEmpty) {
  tsq::task t;
  EXPECT_FALSE(t);
}

TEST(TaskTest, CallingEmptyTaskThrows) {
  tsq::task t;
  EXPECT_THROW(t(), std::bad_function_call);
}

TEST(TaskTest, CallsLambda) {
  int calls = 0;
  tsq::task t([&calls]{ ++calls; });
  ASSERT_TRUE(t);
  t();
  t();
  EXPECT_EQ(calls, 2);
}

TEST(TaskTest, CallsFreeFunction) {
  freeFunctionCalls = 0;
  void (&functionRef)() = freeFunction;
  tsq::task byName(freeFunction);
  tsq::task byPointer(&freeFunction);
  tsq::task byReference(functionRef);
  ASSERT_TRUE(byName);
  ASSERT_TRUE(byPointer);
  ASSERT_TRUE(byReference);
  byName();
  byPointer();
  byReference();
  EXPECT_EQ(freeFunctionCalls, 3);
}

TEST(TaskTest, LvalueCallableIsCopied) {
  auto counter = std::make_shared<int>(0);
  auto increment = [counter]{ ++*counter; };
  ASSERT_EQ(counter.use_count(), 2);

  tsq::task t(increment);
  // The task holds its own copy; the original lambda is left untouched.
  EXPECT_EQ(counter.use_count(), 3);
  t();
  increment();
  EXPECT_EQ(*counter, 2);
}

TEST(TaskTest, MoveOnlyCallable) {
  int result = 0;
  auto value = std::make_unique<int>(42);
  tsq::task t([&result, value = std::move(value)]{ result = *value; });
  t();
  EXPECT_EQ(result, 42);
}

TEST(TaskTest, MoveLeavesSourceEmpty) {
  int calls = 0;
  tsq::task a([&calls]{ ++calls; });

  tsq::task b(std::move(a));
  EXPECT_FALSE(a);
  ASSERT_TRUE(b);

  tsq::task c;
  c = std::move(b);
  EXPECT_FALSE(b);
  ASSERT_TRUE(c);

  c();
  EXPECT_EQ(calls, 1);
}

TEST(TaskTest, DestroysStoredCallable) {
  // Checks that the callable is destroyed through the base class pointer.
  auto resource = std::make_shared<int>(0);
  {
    tsq::task t([resource]{});
    EXPECT_EQ(resource.use_count(), 2);
  }
  EXPECT_EQ(resource.use_count(), 1);
}

TEST(TaskTest, NullFunctionPointerGivesEmptyTask) {
  void (*nullFunction)() = nullptr;
  tsq::task t(nullFunction);
  EXPECT_FALSE(t);
}

TEST(TaskTest, EmptyStdFunctionGivesEmptyTask) {
  std::function<void()> emptyVoid;
  std::function<int()> emptyInt;
  EXPECT_FALSE(tsq::task(emptyVoid));
  EXPECT_FALSE(tsq::task(emptyInt));
}

TEST(TaskTest, NonEmptyStdFunctionGivesNonEmptyTask) {
  int calls = 0;
  std::function<void()> f = [&calls]{ ++calls; };
  tsq::task t(f);
  ASSERT_TRUE(t);
  t();
  EXPECT_EQ(calls, 1);
}
