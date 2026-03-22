#include <iostream>
#include <atomic>
#include <thread>
#include <vector>
#include <chrono>
#include <mutex>

// 1. Standard Mutex (OS-level blocking)
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

// 2. Atomic Spinlock (Acquire/Release)
class AtomicSpinlockRelaxed {
    std::atomic_flag lock_flag = ATOMIC_FLAG_INIT;
    int counter = 0;
public:
    void increment() {
        while (lock_flag.test_and_set(std::memory_order_acquire)); 
        counter++;
        lock_flag.clear(std::memory_order_release);
    }
    int get() { return counter; }
};

// 3. fetch_add RELAXED (Fastest on ARM)
// Uses a single hardware instruction without expensive barriers
class AtomicFetchAddRelaxed {
    alignas(64) std::atomic<int> counter{0};
public:
    void increment() {
        counter.fetch_add(1, std::memory_order_relaxed);
    }
    int get() { return counter.load(); }
};

// 4. fetch_add SEQ_CST (Expensive on ARM)
// Forces a full memory barrier (DMB) on ARM architectures
class AtomicFetchAddStrict {
    alignas(64) std::atomic<int> counter{0};
public:
    void increment() {
        counter.fetch_add(1, std::memory_order_seq_cst);
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
    const int threads = 4;
    const int iters = 1000000;

    // Detect Architecture
    #if defined(__aarch64__) || defined(_M_ARM64)
        std::cout << "Detected: ARM (Weak Memory Model)\n";
    #else
        std::cout << "Detected: x86 (Strong Memory Model)\n";
    #endif

    std::cout << "Threads: " << threads << " | Iters: " << iters << "\n";
    std::cout << "--------------------------------------------------\n";

    run_benchmark<MutexLock>("Standard Mutex          ", threads, iters);
    run_benchmark<AtomicSpinlockRelaxed>("Spinlock (Acq/Rel)      ", threads, iters);
    run_benchmark<AtomicFetchAddRelaxed>("fetch_add (Relaxed)     ", threads, iters);
    run_benchmark<AtomicFetchAddStrict>("fetch_add (Seq_Cst)     ", threads, iters);

    return 0;
}