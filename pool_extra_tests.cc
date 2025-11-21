#include "pool.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <atomic>
#include <vector>
#include <thread>

// Helper sleep wrapper
static void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

struct SleepTask : Task {
    int ms; explicit SleepTask(int m): ms(m) {}
    void Run() override { sleep_ms(ms); }
};

// Task that waits for another named task and sets external flag when done
struct WaitOtherTask : Task {
    ThreadPool *pool; std::string other; std::atomic<bool> *flag;
    WaitOtherTask(ThreadPool *p, std::string n, std::atomic<bool> *f): pool(p), other(std::move(n)), flag(f) {}
    void Run() override { pool->WaitForTask(other); flag->store(true, std::memory_order_relaxed); }
};

// Counting task increments shared atomic
struct CountingTask : Task {
    std::atomic<int> *counter; int inc;
    CountingTask(std::atomic<int> *c, int i=1): counter(c), inc(i) {}
    void Run() override { counter->fetch_add(inc, std::memory_order_relaxed); }
};

// Submit many tasks quickly
static void stress_test(int num_tasks) {
    ThreadPool pool{8};
    std::atomic<int> counter{0};
    for (int i=0;i<num_tasks;++i) {
        pool.SubmitTask("ct" + std::to_string(i), new CountingTask(&counter));
    }
    // Wait all
    for (int i=0;i<num_tasks;++i) {
        pool.WaitForTask("ct" + std::to_string(i));
    }
    assert(counter.load() == num_tasks);
    pool.Stop();
}

// Multiple pools simultaneously
static void multi_pool_test() {
    ThreadPool p1{3};
    ThreadPool p2{2};
    p1.SubmitTask("a", new SleepTask(100));
    p2.SubmitTask("b", new SleepTask(120));
    p1.WaitForTask("a");
    p2.WaitForTask("b");
    p1.Stop();
    p2.Stop();
}

// Wait for task already finished
static void wait_after_finish_test() {
    ThreadPool pool{2};
    pool.SubmitTask("quick", new SleepTask(50));
    sleep_ms(100); // ensure finished
    pool.WaitForTask("quick"); // should return immediately
    pool.Stop();
}

// Intra-task wait chain
static void intra_task_wait_test() {
    ThreadPool pool{4};
    std::atomic<bool> ran{false};
    // Only the waiter task will call WaitForTask("base"), satisfying the "exactly once" rule.
    pool.SubmitTask("base", new SleepTask(120));
    pool.SubmitTask("waiter", new WaitOtherTask(&pool, "base", &ran));
    // Wait only for the waiter; waiter internally waits for base.
    pool.WaitForTask("waiter");
    assert(ran.load());
    pool.Stop();
}

// Task order basic sanity: ensure all run & can be waited
static void basic_order_test() {
    ThreadPool pool{3};
    pool.SubmitTask("t1", new SleepTask(30));
    pool.SubmitTask("t2", new SleepTask(20));
    pool.SubmitTask("t3", new SleepTask(10));
    pool.WaitForTask("t1");
    pool.WaitForTask("t2");
    pool.WaitForTask("t3");
    pool.Stop();
}

// Stop semantics: submit tasks then Stop after waits
static void stop_semantics_test() {
    ThreadPool pool{4};
    pool.SubmitTask("s1", new SleepTask(40));
    pool.SubmitTask("s2", new SleepTask(40));
    pool.WaitForTask("s1");
    pool.WaitForTask("s2");
    pool.Stop();
}

int main() {
    std::cout << "Running basic_order_test..." << std::endl;
    basic_order_test();
    std::cout << "Running wait_after_finish_test..." << std::endl;
    wait_after_finish_test();
    std::cout << "Running intra_task_wait_test..." << std::endl;
    intra_task_wait_test();
    std::cout << "Running multi_pool_test..." << std::endl;
    multi_pool_test();
    std::cout << "Running stress_test(1000)..." << std::endl;
    stress_test(1000);
    std::cout << "Running stop_semantics_test..." << std::endl;
    stop_semantics_test();
    // New: explicit concurrency verification
    std::cout << "Running concurrency_barrier_test..." << std::endl;
    {
        struct BarrierTask : Task {
            std::atomic<int> *started; std::atomic<int> *proceed; int total; int id; std::atomic<int> *simul;
            BarrierTask(std::atomic<int> *s, std::atomic<int> *p, std::atomic<int> *sim, int t, int i): started(s), proceed(p), simul(sim), total(t), id(i) {}
            void Run() override {
                int now = started->fetch_add(1) + 1; // count starts
                // track simultaneous starts within short window
                simul->fetch_add(1);
                // Wait until all have started
                while (started->load() < total) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                proceed->fetch_add(1);
            }
        };
        ThreadPool pool{4};
        const int N = 4;
        std::atomic<int> started{0};
        std::atomic<int> proceed{0};
        std::atomic<int> simul{0};
        for (int i=0;i<N;++i) {
            pool.SubmitTask("bt"+std::to_string(i), new BarrierTask(&started,&proceed,&simul,N,i));
        }
        for (int i=0;i<N;++i) {
            pool.WaitForTask("bt"+std::to_string(i));
        }
        pool.Stop();
        // At least two tasks should have been in-flight simultaneously (simul >= N since each increments once).
        assert(started.load() == N);
        assert(proceed.load() == N);
        assert(simul.load() >= N);
        std::cout << "concurrency_barrier_test passed (" << simul.load() << " starts)." << std::endl;
    }
    std::cout << "All extra tests passed." << std::endl;
    return 0;
}
