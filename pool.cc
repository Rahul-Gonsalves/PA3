// ThreadPool implementation
#include "pool.h"
#include <stdexcept>

Task::Task() = default;
Task::~Task() = default;

ThreadPool::ThreadPool(int num_threads) {
    if (num_threads <= 0) {
        throw std::invalid_argument("num_threads must be > 0");
    }
    threads_.reserve(num_threads);
    for (int i = 0; i < num_threads; ++i) {
        threads_.emplace_back(&ThreadPool::WorkerLoop, this);
    }
}

ThreadPool::~ThreadPool() {
    // Ensure shutdown; safe if already stopped.
    if (!stopping_) {
        try { Stop(); } catch (...) { /* swallow in destructor */ }
    }
    std::lock_guard<std::mutex> lk(mtx_);
    // Delete any remaining tasks (e.g., if user never waited after Stop()).
    for (auto &kv : tasks_) {
        delete kv.second->task;
    }
    tasks_.clear();
}

void ThreadPool::SubmitTask(const std::string &name, Task *task) {
    if (!task) {
        throw std::invalid_argument("SubmitTask: task pointer is null");
    }
    std::unique_lock<std::mutex> lk(mtx_);
    if (stopping_) {
        // Pool stopping: ignore new task (assignment guarantees this shouldn't happen).
        // Leave ownership with caller to avoid double delete scenarios.
        lk.unlock();
        return;       // silently ignore
    }
    if (tasks_.find(name) != tasks_.end()) {
        lk.unlock();
        delete task;
        throw std::runtime_error("Duplicate task name submitted: " + name);
    }
    auto info = std::make_shared<TaskInfo>();
    info->task = task;
    tasks_[name] = info;
    queue_.push_back(name);
    lk.unlock();
    cv_queue_.notify_one();
}

void ThreadPool::WaitForTask(const std::string &name) {
    std::unique_lock<std::mutex> lk(mtx_);
    auto it = tasks_.find(name);
    if (it == tasks_.end()) {
        throw std::runtime_error("WaitForTask: unknown task name: " + name);
    }
    auto info = it->second;
    info->waited = true; // mark that WaitForTask called
    info->cv.wait(lk, [&]{ return info->finished; });
    // Task finished; delete and erase to free memory.
    Task *tptr = info->task;
    tasks_.erase(it); // erase first to avoid re-entrancy issues
    lk.unlock();
    delete tptr;
}

void ThreadPool::WorkerLoop() {
    for (;;) {
        std::string name;
        std::shared_ptr<TaskInfo> info;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_queue_.wait(lk, [&]{ return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty()) {
                break; // normal shutdown
            }
            name = queue_.front();
            queue_.pop_front();
            info = tasks_[name];
            info->started = true;
            // keep pointer outside lock during Run()
        }
        // Execute task without holding lock.
        try {
            info->task->Run();
        } catch (...) {
            // Swallow exceptions from tasks to avoid terminating workers.
        }
        // Mark finished and notify any waiters.
        {
            std::lock_guard<std::mutex> lk(mtx_);
            info->finished = true;
            info->cv.notify_all();
        }
    }
}

void ThreadPool::Stop() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (stopping_) return; // already stopping
        stopping_ = true;
    }
    cv_queue_.notify_all();
    for (auto &t : threads_) {
        if (t.joinable()) t.join();
    }
}
