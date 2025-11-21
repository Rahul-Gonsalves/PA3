// Thread pool interface per assignment specification.
// Implementation in pool.cc uses condition variables (no busy waiting)
// and supports waiting on tasks by name and clean shutdown.

#include <string>
#include <thread>
#include <vector>
#include <deque>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <memory>

class Task {
public:
    Task();
    virtual ~Task();
    virtual void Run() = 0;  // implemented by subclass
    std::string name;        // optional; not required by pool
};

class ThreadPool {
public:
    explicit ThreadPool(int num_threads);
    ~ThreadPool();

    // Submit a task with a particular name (unique per pool).
    void SubmitTask(const std::string &name, Task *task);
    // Wait for named task to finish (exactly once per submitted task).
    void WaitForTask(const std::string &name);
    // Stop threads after processing queued tasks. Safe to call once.
    void Stop();

private:
    struct TaskInfo {
        Task *task;
        bool started = false;
        bool finished = false;
        bool waited = false;              // WaitForTask invoked
        std::condition_variable cv;       // signals finish
    };

    void WorkerLoop();

    std::mutex mtx_;                      // protects all below
    std::condition_variable cv_queue_;    // tasks added or stopping
    bool stopping_ = false;
    std::deque<std::string> queue_;       // FIFO task names
    std::unordered_map<std::string, std::shared_ptr<TaskInfo>> tasks_;
    std::vector<std::thread> threads_;    // worker threads
};
