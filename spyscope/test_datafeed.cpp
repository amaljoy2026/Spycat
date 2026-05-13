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
        double z = sin(2 * M_PI * 2.0 * t);
        m.set("data", y);
        m.set("rpm", z);
        m.set("Body.vx", y - z);
        m.set("Body.vy", y + z);
        std::this_thread::sleep_for(std::chrono::milliseconds(17));
    }
    return 0;
}

// build with: c++ ../spyscope/test_datafeed.cpp ../spymap/spymap.cpp -I/opt/homebrew/include -std=c++17
