#include <iostream>
#include <atomic>
#include <thread>
#include <vector>
#include <chrono>
#include <mutex>
#include <string>
#include <iomanip>

const int THREADS = 40;
const int ITERS = 100000;

// --- 1. Baseline Mutex ---
class BenchMutex {
    std::mutex mtx;
    int counter = 0;
public:
    void increment() {
        std::lock_guard<std::mutex> lock(mtx);
        counter++;
    }
};

// --- 2. CAS Template (Permutations) ---
template<bool Strong, std::memory_order Success, std::memory_order Failure>
class BenchCAS {
    alignas(64) std::atomic<int> counter{0};
public:
    void increment() {
        int expected = counter.load(std::memory_order_relaxed);
        if constexpr (Strong) {
            while (!counter.compare_exchange_strong(expected, expected + 1, Success, Failure));
        } else {
            while (!counter.compare_exchange_weak(expected, expected + 1, Success, Failure));
        }
    }
};

// --- 3. CAS with Exponential Backoff ---
// This prevents "Cache Thrashing" by making a thread wait longer each time it fails.
class BenchCASBackoff {
    alignas(64) std::atomic<int> counter{0};
public:
    void increment() {
        int expected = counter.load(std::memory_order_relaxed);
        int backoff = 1;
        while (!counter.compare_exchange_weak(expected, expected + 1, 
                                              std::memory_order_relaxed, 
                                              std::memory_order_relaxed)) {
            // Exponentially increase "spin" time to let the winner finish
            for (int i = 0; i < backoff; ++i) {
#if defined(__x86_64__)
                asm volatile("pause" ::: "memory"); // x86 hint to stop spinning
#elif defined(__aarch64__)
                asm volatile("isb" ::: "memory");   // ARM hint (Instruction Barrier)
#endif
            }
            if (backoff < 64) backoff <<= 1; 
        }
    }
};

template<typename T>
void run(std::string name) {
    T bench;
    std::vector<std::thread> threads;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < THREADS; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < ITERS; ++j) bench.increment();
        });
    }
    for (auto& t : threads) t.join();

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> ms = end - start;
    std::cout << std::left << std::setw(38) << name << ": " << std::fixed << std::setprecision(2) << ms.count() << " ms\n";
}

int main() {
#if defined(__aarch64__)
    std::cout << "--- ARCH: ARM64 (Weak Memory Model) ---\n";
#else
    std::cout << "--- ARCH: x86_64 (Strong Memory Model) ---\n";
#endif
    std::cout << "Threads: " << THREADS << " | Iters: " << ITERS << "\n\n";

    run<BenchMutex>("1. Mutex (OS-Level)");
    std::cout << "--------------------------------------------------------\n";

    // --- WEAK CAS PERMUTATIONS ---
    run<BenchCAS<false, std::memory_order_relaxed, std::memory_order_relaxed>>("2. Weak: Relaxed / Relaxed");
    run<BenchCAS<false, std::memory_order_acquire, std::memory_order_relaxed>>("3. Weak: Acquire / Relaxed");
    run<BenchCAS<false, std::memory_order_acq_rel, std::memory_order_relaxed>>("4. Weak: Acq_Rel / Relaxed");
    run<BenchCAS<false, std::memory_order_acq_rel, std::memory_order_acquire>>("5. Weak: Acq_Rel / Acquire");
    run<BenchCAS<false, std::memory_order_seq_cst, std::memory_order_seq_cst>>("6. Weak: Seq_Cst / Seq_Cst");

    std::cout << "--------------------------------------------------------\n";

    // --- STRONG CAS PERMUTATIONS ---
    run<BenchCAS<true, std::memory_order_relaxed, std::memory_order_relaxed>> ("7. Strong: Relaxed / Relaxed");
    run<BenchCAS<true, std::memory_order_acquire, std::memory_order_relaxed>> ("8. Strong: Acquire / Relaxed");
    run<BenchCAS<true, std::memory_order_acq_rel, std::memory_order_relaxed>> ("9. Strong: Acq_Rel / Relaxed");
    run<BenchCAS<true, std::memory_order_acq_rel, std::memory_order_acquire>> ("10. Strong: Acq_Rel / Acquire");
    run<BenchCAS<true, std::memory_order_seq_cst, std::memory_order_seq_cst>> ("11. Strong: Seq_Cst / Seq_Cst");

    std::cout << "--------------------------------------------------------\n";

    // --- OPTIMIZED ---
    run<BenchCASBackoff>("12. Weak: Relaxed + Backoff (PRO)");

    return 0;
}