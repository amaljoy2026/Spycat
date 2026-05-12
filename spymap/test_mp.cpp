#include <iostream>
#include <thread>
#include "spymap.hpp"

using namespace spycat;

int await_value(Spymap& m, const std::string& key, int64_t expected)
{
    while (true) {
        int64_t v = m.get_int64(key, 0);
        if (v == expected) return v;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void increment(Spymap& m, const std::string& key)
{
    int64_t v = m.get_int64(key, 0);
    m.set(key, v + 1);
}

void test_multiprocess()
{
    std::cout << "\n=== test_multiprocess ===\n";
    Spymap::destroy("TestMultiprocess");
    const int N_ITERS = 50;
    int pid = fork();

    if (pid == 0) {
        Spymap m("TestMultiprocess");
        // Child process
        for (int i = 0; i < N_ITERS / 2; ++i) {
            await_value(m, "counter", 2*i+1); // wait for parent to create the key
            increment(m, "counter");
        }
        exit(0);
    } else {
        Spymap m("TestMultiprocess");
        std::cout << "Process PIDs: " << getpid() << ", " << pid << "\n";
        auto t0 = std::chrono::high_resolution_clock::now();

        // Parent process
        for (int i = 0; i < N_ITERS / 2; ++i) {
            increment(m, "counter");
            await_value(m, "counter", 2*i+2); // wait for child to increment
        }
        
        auto t1 = std::chrono::high_resolution_clock::now();
        waitpid(pid, nullptr, 0);
        std::cout << "Final counter value: " << m.get_int64("counter") << " (expect " << N_ITERS << ")\n";
        std::cout << "Elapsed time: " << std::chrono::duration<double>(t1 - t0).count() << " s\n";
        Spymap::destroy("TestMultiprocess");
    }
}

int main()
{
    test_multiprocess();
}