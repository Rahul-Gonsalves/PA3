#include "pool.h"

#include <atomic>
#include <iostream>
#include <sstream>
#include <vector>

struct SleepTask : Task {
    int ms; explicit SleepTask(int m): ms(m) {}
    void Run() override { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
};

// Barrier task to force concurrent start; all tasks wait until N have started.
struct BarrierTask : Task {
    std::atomic<int> *started; int total; int work_ms;
    BarrierTask(std::atomic<int> *s, int t, int w): started(s), total(t), work_ms(w) {}
    void Run() override {
        int c = started->fetch_add(1) + 1;
        // spin lightly until all started (short, bounded)
        while (started->load() < total) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(work_ms));
    }
};

int main(int argc, char **argv) {
    // Core pool for baseline tests
    ThreadPool pool{5};

    // Submit several simple sleep tasks rapidly (no delay) to allow concurrency.
    pool.SubmitTask("first", new SleepTask(300));
    pool.SubmitTask("second", new SleepTask(300));
    pool.SubmitTask("third", new SleepTask(300));
    pool.SubmitTask("fourth", new SleepTask(300));

    // Explicit barrier group to guarantee simultaneous start of a batch.
    std::atomic<int> started{0};
    const int N = 3;
    pool.SubmitTask("b0", new BarrierTask(&started, N, 200));
    pool.SubmitTask("b1", new BarrierTask(&started, N, 200));
    pool.SubmitTask("b2", new BarrierTask(&started, N, 200));

    // Wait for initial tasks (exercise WaitForTask API) before stopping.
    pool.WaitForTask("first");
    pool.WaitForTask("second");
    pool.WaitForTask("third");
    pool.WaitForTask("fourth");
    pool.WaitForTask("b0");
    pool.WaitForTask("b1");
    pool.WaitForTask("b2");

    // Stop after tasks are done to ensure all start logs flushed.
    pool.Stop();

    // Post-stop submission should be ignored with message.
    pool.SubmitTask("after-stop", new SleepTask(10));

    return 0;
}
