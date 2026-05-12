#include <cmath>
#include <chrono>
#include <thread>
#include "../spymap/spymap.hpp"

int main()
{
    spycat::Spymap m("_test_");
    while (true) {
        auto time = std::chrono::steady_clock::now();
        std::chrono::duration<double> diff = time.time_since_epoch();
        double t = diff.count();
        double y = sin(2 * M_PI * t);
        m.set("data", y);
        std::this_thread::sleep_for(std::chrono::milliseconds(17));
    }
    return 0;
}