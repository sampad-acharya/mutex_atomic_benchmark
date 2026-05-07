// Dekker-style mutual exclusion using two flags.
//
// Each thread:
//   1) raises ITS OWN flag                  (write)
//   2) checks the OTHER thread's flag       (read)
//   3) if other flag is clear, enter the critical section
//
// For mutual exclusion to hold, it MUST be impossible for both threads
// to (a) raise their flag and then (b) see the other's flag as clear.
//
// With acq_rel (release store + acquire load):
//   each store can sit in the local store buffer while the load reads
//   the still-old value of the other flag. BOTH threads enter the CS.
//   --> mutual exclusion is BROKEN.
//
// With seq_cst:
//   the single total order across all seq_cst ops guarantees that one
//   of the two stores is visible before the other thread's load.
//   --> mutual exclusion HOLDS.
//
// We detect a violation with a non-atomic shared counter that both
// threads increment when they think they have exclusive access. If
// they truly were exclusive, the final count == 2 * iterations. If
// they raced inside the CS, increments are lost.

#include <atomic>
#include <thread>
#include <iostream>
#include <iomanip>
#include <cstdint>

template <std::memory_order StoreOrd, std::memory_order LoadOrd>
void run(const char* label, std::uint64_t iters) {
    alignas(64) std::atomic<bool> wantA{false};
    alignas(64) std::atomic<bool> wantB{false};
    alignas(64) std::atomic<int>  go{0};

    // Plain (non-atomic) shared counter. Only safe if the lock works.
    alignas(64) std::uint64_t shared_counter = 0;

    // Count how often we *detected* both threads in the CS at once.
    alignas(64) std::atomic<std::uint64_t> double_entry{0};
    alignas(64) std::atomic<int> in_cs{0};   // sanity tripwire

    auto thread_fn = [&](std::atomic<bool>& mine, std::atomic<bool>& other) {
        // wait for start
        while (go.load(std::memory_order_acquire) == 0) {}

        for (std::uint64_t i = 0; i < iters; ++i) {
            mine.store(true, StoreOrd);

            // spin until the other has clearly yielded
            while (other.load(LoadOrd)) {
                // simple back-off: drop our flag, wait, retry
                mine.store(false, StoreOrd);
                while (other.load(LoadOrd)) {}
                mine.store(true, StoreOrd);
            }

            // ---- critical section ----
            int now = in_cs.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (now > 1) double_entry.fetch_add(1, std::memory_order_relaxed);
            shared_counter += 1;       // racy if mutex is broken
            in_cs.fetch_sub(1, std::memory_order_acq_rel);
            // --------------------------

            mine.store(false, StoreOrd);
        }
    };

    std::thread tA(thread_fn, std::ref(wantA), std::ref(wantB));
    std::thread tB(thread_fn, std::ref(wantB), std::ref(wantA));

    go.store(1, std::memory_order_release);

    tA.join();
    tB.join();

    std::uint64_t expected = 2 * iters;
    std::int64_t  lost     = (std::int64_t)expected - (std::int64_t)shared_counter;

    std::cout << std::left << std::setw(34) << label
              << "  expected=" << expected
              << "  got=" << shared_counter
              << "  lost_increments=" << lost
              << "  double_entry_observed=" << double_entry.load() << "\n";
}

int main() {
    constexpr std::uint64_t ITERS = 200'000;

    std::cout << "Dekker-style mutual exclusion test  --  iters per thread: " << ITERS << "\n";
    std::cout << "If lost_increments > 0 OR double_entry_observed > 0,\n";
    std::cout << "the lock FAILED -- both threads were in the critical section.\n";
    std::cout << "----------------------------------------------------------------------\n";

    run<std::memory_order_relaxed, std::memory_order_relaxed>(
        "relaxed / relaxed", ITERS);

    run<std::memory_order_release, std::memory_order_acquire>(
        "release / acquire (acq_rel)", ITERS);

    run<std::memory_order_seq_cst, std::memory_order_seq_cst>(
        "seq_cst / seq_cst", ITERS);

    return 0;
}
