#include <iostream>
#include <atomic>
#include <thread>
#include <vector>
#include <chrono>
#include <mutex>

// 1. Standard Mutex (The Baseline)
class MutexLock {
    std::mutex mtx;
    int counter = 0;
public:
    void increment() {
        std::lock_guard<std::mutex> lock(mtx);
        counter++;
    }
    int get() { return counter; }
};

// 2. fetch_add (The Hardware-accelerated baseline) 
class AtomicFetchAddRelaxed {
    alignas(64) std::atomic<int> counter{0};
public:
    void increment() {
        counter.fetch_add(1, std::memory_order_relaxed);
    }
    int get() { return counter.load(); }
};


// 3. fetch_add (The Hardware-accelerated baseline) 
class AtomicFetchAddSeqCst {
    alignas(64) std::atomic<int> counter{0};
public:
    void increment() {
        counter.fetch_add(1, std::memory_order_seq_cst);
    }
    int get() { return counter.load(); }
};

// 4. Compare and Exchange (Relaxed)
// This simulates how more complex lock-free structures (stacks, queues) work.
class AtomicCASRelaxed {
    alignas(64) std::atomic<int> counter{0};
public:
    void increment() {
        int expected = counter.load(std::memory_order_relaxed);
        // compare_exchange_weak is preferred in loops because it can fail 
        // spuriously but is faster on architectures like ARM.
        while (!counter.compare_exchange_weak(expected, expected + 1, 
                                              std::memory_order_relaxed, 
                                              std::memory_order_relaxed)) {
            // If it fails, 'expected' is automatically updated with the current value.
            // We just loop until we succeed.
        }
    }
    int get() { return counter.load(); }
};

// 5. Compare and Exchange (Sequentially Consistent)
// Forces ARM to sync all core buffers on every single loop attempt.
class AtomicCASStrict {
    alignas(64) std::atomic<int> counter{0};
public:
    void increment() {
        int expected = counter.load(std::memory_order_seq_cst);
        while (!counter.compare_exchange_weak(expected, expected + 1, 
                                              std::memory_order_seq_cst, 
                                              std::memory_order_seq_cst)) {
        }
    }
    int get() { return counter.load(); }
};

template<typename T>
void run_benchmark(std::string name, int num_threads, int iterations) {
    T bench;
    std::vector<std::thread> threads;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < iterations; ++j) {
                bench.increment();
            }
        });
    }
    for (auto& t : threads) t.join();

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> ms = end - start;
    std::cout << name << ": " << ms.count() << " ms\n";
}

int main() {
    const int threads = 40;
    const int iters = 100000;

    #if defined(__aarch64__)
        std::cout << "Target Architecture: ARM64\n";
    #else
        std::cout << "Target Architecture: x86_64\n";
    #endif

    std::cout << "Threads: " << threads << " | Iters: " << iters << "\n";
    std::cout << "--------------------------------------------------\n";

    run_benchmark<MutexLock>("Standard Mutex          ", threads, iters);
    run_benchmark<AtomicFetchAddRelaxed>("Atomic fetch_add (Relaxed)  ", threads, iters);
    run_benchmark<AtomicFetchAddSeqCst>("Atomic fetch_add (Seq_Cst)  ", threads, iters);
    run_benchmark<AtomicCASRelaxed>("Atomic CAS (Relaxed)    ", threads, iters);
    run_benchmark<AtomicCASStrict>("Atomic CAS (Seq_Cst)    ", threads, iters);

    return 0;
}