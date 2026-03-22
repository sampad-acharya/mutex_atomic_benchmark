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

// 2. Atomic Spinlock (Weak/Relaxed - Fast on ARM, but specific to this use case)
class AtomicSpinlockRelaxed {
    std::atomic_flag lock_flag = ATOMIC_FLAG_INIT;
    int counter = 0;
public:
    void increment() {
        // memory_order_acquire/release is the "native" way for ARM
        while (lock_flag.test_and_set(std::memory_order_acquire)); 
        counter++;
        lock_flag.clear(std::memory_order_release);
    }
    int get() { return counter; }
};

// 3. Atomic Spinlock (Sequentially Consistent - Slow on ARM)
class AtomicSpinlockStrict {
    std::atomic_flag lock_flag = ATOMIC_FLAG_INIT;
    int counter = 0;
public:
    void increment() {
        // memory_order_seq_cst forces ARM to flush buffers/insert barriers
        while (lock_flag.test_and_set(std::memory_order_seq_cst)); 
        counter++;
        lock_flag.clear(std::memory_order_seq_cst);
    }
    int get() { return counter; }
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
    
    std::cout << name << ": " << ms.count() << " ms (Final Val: " << bench.get() << ")\n";
}

int main() {
    const int threads = 400;
    const int iters = 10000;

    std::cout << "Running benchmark with " << threads << " threads and " << iters << " iterations.\n";
    std::cout << "--------------------------------------------------\n";

    run_benchmark<MutexLock>("Standard Mutex         ", threads, iters);
    run_benchmark<AtomicSpinlockRelaxed>("Atomic Spinlock (Acq/Rel)", threads, iters);
    run_benchmark<AtomicSpinlockStrict>("Atomic Spinlock (Seq_Cst)", threads, iters);

    return 0;
}
