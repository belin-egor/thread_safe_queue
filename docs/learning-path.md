# Learning path

An extended version of the roadmap from the README, for anyone who wants
to build the same library from scratch. Each stage says what gets built
and which limitation of the result pushes you to the next stage.

## 1. Build setup

A header-only CMake target (`INTERFACE` library) with C++20, warnings, and
separate builds with ThreadSanitizer and AddressSanitizer, plus CMake
presets for them.

## 2. Thread-safe queue

`ts_queue<T>` on `std::mutex` and `std::condition_variable`: `push`,
blocking `pop`, non-blocking `try_pop` returning `std::optional<T>`,
`empty`, `size`. `pop` waits with a predicate to survive spurious and
lost wakeups.

**Limitation:** a consumer blocked in `pop` can never be told that no more
items will come. With several consumers the program cannot finish
cleanly: someone is always left waiting.

## 3. Closing the queue

`close()`: after it `push` returns `false`, and `pop` hands out the
remaining items, then returns `std::nullopt` once the queue is empty.
`close()` wakes every waiting consumer with `notify_all` and is safe to
call more than once and from several threads.

**Limitation:** the queue only moves data. Running work on other threads
still means creating threads by hand for every job and joining them.

## 4. Thread pool

`thread_pool` with a fixed number of `std::jthread` workers that take
`std::function<void()>` tasks from the queue. The destructor closes the
queue, workers finish all pending tasks, and the threads are joined. The
queue is declared before the threads, so it outlives them.

**Limitation:** `submit` returns only `bool`, so the caller gets no result
and cannot tell when a task is done. An exception thrown by a task escapes
the worker thread and calls `std::terminate`.

## 5. Results via `std::future`

`submit(f)` wraps the callable in `std::packaged_task<R()>` and returns
`std::future<R>`: `get()` returns the result or rethrows the task's
exception. `packaged_task` is move-only, and `std::function` requires
copyable callables, so the queue now stores `tsq::task`, a small move-only
type-erased `void()` wrapper.

**Limitation:** a task that waits on `get()` of a subtask inside the same
pool can block a worker, and with all workers blocked the pool deadlocks.
There is also no way to wait for all tasks or to stop without running the
pending ones.

## 6. Extras (optional)

- Nested `submit` and the deadlock from stage 5.
- `wait_all()`, or an immediate shutdown that drops pending tasks
  (`std::stop_token`).
- Benchmarks: the pool vs `std::async` vs a single thread; `tsq::task` vs
  `std::function` with `std::shared_ptr`.
