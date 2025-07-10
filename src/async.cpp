// fire_n_go.cpp
#include "async.hpp"
#include <memory>
#include <mutex> // For std::mutex in lazy init

namespace util {

namespace { // Anonymous namespace for internal linkage

    // Meyers' Singleton pattern for the thread pool instance.
    // The unique_ptr is now managed entirely within this function.
    // Its destructor will be called automatically at program exit, ensuring
    // the ThreadPool is cleaned up correctly. This is RAII in action.
    std::unique_ptr<ThreadPool>& get_pool_instance_ptr() {
        /*NOSONAR*/ static std::unique_ptr<ThreadPool> global_thread_pool_ptr;
        return global_thread_pool_ptr;
    }

    // Mutex to protect the lazy initialization of the thread pool.
    std::mutex& get_pool_init_mutex() {
        /* NOSONAR */ static std::mutex pool_init_mutex;
        return pool_init_mutex;
    }

    // FIX: The manual atexit handler has been removed. The static unique_ptr's
    // destructor will now handle the shutdown automatically and safely at the
    // correct time during program termination, preventing the double-free error.

} // namespace

// --- Public function to access the pool ---
// This function now handles lazy initialization. The pool is only created
// on the first call to fire_and_forget.
ThreadPool* get_thread_pool_instance() {
    // Use a double-checked locking pattern for performance. We only take the
    // expensive lock if the pool hasn't been initialized yet.
    if (!get_pool_instance_ptr()) {
        const std::lock_guard lock(get_pool_init_mutex());
        // Check again inside the lock to handle the race condition where
        // another thread might have initialized the pool while we were waiting for the lock.
        if (!get_pool_instance_ptr()) {
            using enum log::Level;
            size_t num_threads = std::thread::hardware_concurrency();
            if (num_threads == 0) num_threads = 2; // Fallback
            get_pool_instance_ptr() = std::make_unique<ThreadPool>(num_threads);
            log::print<Info>("ThreadPool", "Lazy initialization: Thread pool created with {} threads.", num_threads);
        }
    }
    return get_pool_instance_ptr().get();
}


// --- ThreadPool Method Implementations ---

ThreadPool::ThreadPool(size_t num_threads) {
    start(num_threads);
}

ThreadPool::~ThreadPool() {
    // This destructor is now correctly called once by the unique_ptr at program exit.
    using enum log::Level;
    log::print<Info>("ThreadPool", "ThreadPool destructor called. Shutting down threads...");
    m_stop_source.request_stop();
    m_condition.notify_all();
    // jthreads in m_workers will be automatically joined here.
}

void ThreadPool::start(size_t num_threads) {
    m_workers.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        m_workers.emplace_back([this](std::stop_token stoken) {
            worker_loop(std::move(stoken));
        }, m_stop_source.get_token());
    }
}

void ThreadPool::worker_loop(std::stop_token stoken) {
    while (!stoken.stop_requested()) {
        std::function<void()> task;
        {
            std::unique_lock lock(m_queue_mutex);
            m_condition.wait(lock, [this, &stoken] {
                return stoken.stop_requested() || !m_tasks.empty();
            });

            if (stoken.stop_requested() && m_tasks.empty()) {
                return;
            }

            task = std::move(m_tasks.front());
            m_tasks.pop();
        }
        task();
    }
}

} // namespace util
