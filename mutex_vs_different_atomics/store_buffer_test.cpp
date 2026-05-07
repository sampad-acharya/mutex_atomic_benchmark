// Store Buffer / Dekker test: demonstrates that acq_rel does NOT give a
// global total order, but seq_cst does.
//
// Two threads:
//   T1: x.store(1, ORD);  r1 = y.load(ORD);
//   T2: y.store(1, ORD);  r2 = x.load(ORD);
//
// With seq_cst:  outcome (r1=0, r2=0) is IMPOSSIBLE.
// With acq_rel:  outcome (r1=0, r2=0) is ALLOWED (each thread's store
//                may be observed by the other thread *after* its own load).
//
// On ARM64 (weakly ordered) you can actually observe (0,0) under acq_rel.
// On x86 it is much harder to observe because x86's hardware model is
// already close to TSO, but it is still architecturally permitted.

#include <atomic>
#include <thread>
#include <iostream>
#include <iomanip>
#include <cstdint>

enum class WriteKind { Store, Exchange };

template <WriteKind WK, std::memory_order WriteOrd, std::memory_order LoadOrd>
void run(const char* label, std::uint64_t trials) {
    alignas(64) std::atomic<int> x{0};
    alignas(64) std::atomic<int> y{0};
    alignas(64) std::atomic<int> go{0};
    alignas(64) std::atomic<int> done{0};

    int r1 = 0, r2 = 0;
    std::uint64_t both_zero = 0;

    auto do_write = [](std::atomic<int>& v) {
        if constexpr (WK == WriteKind::Store) {
            v.store(1, WriteOrd);
        } else {
            (void)v.exchange(1, WriteOrd);   // RMW — acq_rel valid here
        }
    };

    std::thread t1([&] {
        for (std::uint64_t i = 0; i < trials; ++i) {
            // wait for round start
            while (go.load(std::memory_order_acquire) != (int)(i + 1)) {}
            do_write(x);
            r1 = y.load(LoadOrd);
            done.fetch_add(1, std::memory_order_release);
        }
    });

    std::thread t2([&] {
        for (std::uint64_t i = 0; i < trials; ++i) {
            while (go.load(std::memory_order_acquire) != (int)(i + 1)) {}
            do_write(y);
            r2 = x.load(LoadOrd);
            done.fetch_add(1, std::memory_order_release);
        }
    });

    for (std::uint64_t i = 0; i < trials; ++i) {
        // reset shared state
        x.store(0, std::memory_order_relaxed);
        y.store(0, std::memory_order_relaxed);
        done.store(0, std::memory_order_relaxed);

        // release the two workers for this round
        go.store((int)(i + 1), std::memory_order_release);

        // wait for both to finish this round
        while (done.load(std::memory_order_acquire) != 2) {}

        if (r1 == 0 && r2 == 0) ++both_zero;
    }

    t1.join();
    t2.join();

    double pct = 100.0 * (double)both_zero / (double)trials;
    std::cout << std::left << std::setw(30) << label
              << "  (r1=0, r2=0) observed: "
              << both_zero << " / " << trials
              << "  (" << std::fixed << std::setprecision(4) << pct << "%)\n";
}

int main() {
    constexpr std::uint64_t TRIALS = 2'000'000;

    std::cout << "Store Buffer test  --  trials per config: " << TRIALS << "\n";
    std::cout << "If (r1=0, r2=0) is observed > 0 times, the model permits\n";
    std::cout << "the two threads to disagree on the order of the stores.\n";
    std::cout << "----------------------------------------------------------\n";

    run<WriteKind::Store, std::memory_order_relaxed, std::memory_order_relaxed>(
        "relaxed store / relaxed load", TRIALS);

    run<WriteKind::Store, std::memory_order_release, std::memory_order_acquire>(
        "release store / acquire load", TRIALS);

    run<WriteKind::Exchange, std::memory_order_acq_rel, std::memory_order_acquire>(
        "acq_rel exchange / acquire load", TRIALS);

    run<WriteKind::Store, std::memory_order_seq_cst, std::memory_order_seq_cst>(
        "seq_cst / seq_cst", TRIALS);

    return 0;
}
