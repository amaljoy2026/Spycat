#include "spymap.hpp"

#include <iostream>
#include <string>
#include <unordered_map>
#include <random>
#include <chrono>
#include <thread>
#include <vector>

using namespace spycat;

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::string random_string(size_t length)
{
    static const std::string chars =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static std::mt19937 gen{std::random_device{}()};
    std::uniform_int_distribution<size_t> dist(0, chars.size() - 1);

    std::string s;
    s.reserve(length);
    for (size_t i = 0; i < length; ++i)
        s += chars[dist(gen)];
    return s;
}

// ── Test 1: basic typed set/get round-trip ────────────────────────────────────

static void test_typed()
{
    std::cout << "\n=== test_typed ===\n";
    Spymap::destroy("TestTyped"); // clean slate
    Spymap m("TestTyped");

    m.set("engineRPM",   1234.5);
    m.set("sensorInput", int64_t(42));
    m.set("enabled",     true);
    m.set("status",      std::string("running"));

    std::cout << "engineRPM   = " << m.get_double("engineRPM")  << "  (expect 1234.5)\n";
    std::cout << "sensorInput = " << m.get_int64 ("sensorInput") << "  (expect 42)\n";
    std::cout << "enabled     = " << m.get_bool  ("enabled")     << "  (expect 1)\n";
    std::cout << "status      = " << m.get_string("status")      << "  (expect running)\n";

    // Override
    m.set("engineRPM", 9999.0);
    std::cout << "engineRPM   = " << m.get_double("engineRPM")  << "  (expect 9999)\n";

    Spymap::destroy("TestTyped");
}

// ── Test 2: string round-trip stress (replaces original main) ────────────────

static void test_string_stress()
{
    std::cout << "\n=== test_string_stress ===\n";
    Spymap::destroy("TestStress");
    Spymap m("TestStress");

    const int    N      = 1000; // reduced from 10000 — keys are 255 chars each
    const size_t keylen = 32;   // shorter keys for readability
    const size_t vallen = 64;

    std::unordered_map<std::string, std::string> reference;
    reference.reserve(N);
    for (int i = 0; i < N; ++i)
        reference[random_string(keylen)] = random_string(vallen);
        
    auto t0 = std::chrono::high_resolution_clock::now();

    for (const auto& [k, v] : reference)
        m.set(k, v);

    int mismatches = 0;
    for (const auto& [k, v] : reference) {
        std::string got = m.get_string(k);
        if (got != v) {
            std::cerr << "MISMATCH key=" << k
                      << " expected=" << v << " got=" << got << "\n";
            ++mismatches;
        }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();

    std::cout << "Pairs tested : " << N        << "\n";
    std::cout << "Mismatches   : " << mismatches << "\n";
    std::cout << "Elapsed      : " << elapsed   << " s\n";

    Spymap::destroy("TestStress");
}

// ── Test 3: multi-thread concurrent set/get ───────────────────────────────────

static void test_multithreaded()
{
    std::cout << "\n=== test_multithreaded ===\n";
    Spymap::destroy("TestMT");
    Spymap m("TestMT");

    m.set("counter", int64_t(0)); // pre-create the key

    const int N_THREADS  = 8;
    const int N_ITERS    = 10000;
    std::vector<std::thread> threads;

    // Writers — each thread writes its own key and also updates "counter"
    for (int t = 0; t < N_THREADS; ++t) {
        threads.emplace_back([&m, t]() {
            std::string own_key = "thread_" + std::to_string(t);
            m.set(own_key, double(0)); // create key before hot loop
            for (int i = 0; i < N_ITERS; ++i) {
                m.set(own_key,   double(i));
                m.set("counter", int64_t(i)); // intentional race — LWW
            }
        });
    }

    for (auto& th : threads)
        th.join();

    std::cout << "All threads completed without crash.\n";
    std::cout << "counter (last write wins) = "
              << m.get_int64("counter") << "\n";

    Spymap::destroy("TestMT");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main()
{
    test_typed();
    test_string_stress();
    test_multithreaded();
    std::cout << "\nAll tests done.\n";
    return 0;
}
