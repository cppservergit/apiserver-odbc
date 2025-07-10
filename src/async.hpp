// fire_n_go.hpp
#pragma once

#include "logger.hpp" // For logging
#include <stdexcept>    // For std::runtime_error
#include <concepts>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <stop_token> // For std::stop_source and std::stop_token
#include <string>     // For std::string
#include <string_view>
#include <thread> // For std::jthread
#include <utility>
#include <vector>

namespace util {

// SONARCLOUD FIX: Define a dedicated exception type directly in this header
// to make it available to both the implementation and the client (main.cpp).
struct TaskFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};


// Forward declaration for the ThreadPool class
class ThreadPool;

// Internal-only function to get the singleton instance of the pool.
// The definition is in fire_n_go.cpp.
ThreadPool* get_thread_pool_instance();

/**
 * @class ThreadPool
 * @brief Manages a pool of worker jthreads to execute tasks concurrently.
 *
 * @note This class is an internal implementation detail.
 */
class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads);
    ~ThreadPool();

    // Delete copy and move operations to enforce singleton-like behavior.
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    // The implementation of this template member function is now directly in the header.
    template<typename F>
    void enqueue(F&& task) {
        {
            std::scoped_lock lock(m_queue_mutex);
            m_tasks.emplace(std::forward<F>(task));
        }
        m_condition.notify_one();
    }

private:
    void start(size_t num_threads);
    void worker_loop(std::stop_token stoken);

    std::vector<std::jthread> m_workers;
    std::queue<std::function<void()>> m_tasks;
    std::mutex m_queue_mutex;
    std::condition_variable m_condition;
    std::stop_source m_stop_source;
};


/**
 * @brief Dispatches a task to the global thread pool for immediate, asynchronous execution.
 *
 * The implementation is now directly in the header to resolve linking and ambiguity issues.
 *
 * @tparam Callable The deduced type of the callable object.
 * @param task_name A descriptive name for the task, used for logging.
 * @param task The callable object (lambda, function pointer, etc.) to be executed.
 */
template<typename Callable>
void async_launch(std::string_view task_name, Callable&& task)
    // This requires clause is a more precise way to constrain a forwarding reference.
    requires std::invocable<Callable&&>
{
    ThreadPool* pool_instance = get_thread_pool_instance();
    if (!pool_instance) {
        log::print<log::Level::Error>("TaskRunner", "fire_and_forget called but thread pool is not available.");
        return;
    }

    std::string name_copy(task_name);

    auto wrapped_task = [name = std::move(name_copy), work = std::forward<Callable>(task)]() mutable {
        using enum log::Level;
        log::print<Info>("TaskRunner", "Starting task: '{}'", name);
        try {
            std::invoke(std::move(work));
            log::print<Info>("TaskRunner", "Finished task: '{}'", name);
        } 
        // SONARCLOUD FIX: Catch the most specific exception type first.
        catch (const TaskFailure& e) {
            const char* error_what = e.what();
            log::print<Error>("TaskRunner", "A known task failure occurred in '{}': {}", name, error_what);
        }
        // Catch other standard exceptions next.
        /*NO SONAR*/ catch (const std::exception& e) {
            const char* error_what = e.what();
            log::print<Error>("TaskRunner", "An unknown standard exception caught in task '{}': {}", name, error_what);
        } 
        // Finally, catch anything else to prevent the worker from crashing.
        /*NO SONAR*/ catch (...) {
            log::print<Error>("TaskRunner", "A non-standard, unknown exception caught in task '{}'", name);
        }
    };

    pool_instance->enqueue(std::move(wrapped_task));
}

} // namespace util
