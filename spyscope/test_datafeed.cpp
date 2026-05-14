#include <cmath>
#include <chrono>
#include <thread>
#include "../spymap/spymap.hpp"
#include <iostream>

int main()
{
    using Clock    = std::chrono::steady_clock;
    using Duration = std::chrono::duration<double>;

    spycat::Spymap m("_test01_");

    auto     last_write    = Clock::now();
    auto     last_report   = Clock::now();
    double   interval_sum  = 0.0;
    int      interval_count = 0;

    while (true) {
        auto     now  = Clock::now();
        Duration diff = now.time_since_epoch();
        double   t    = diff.count();

        // Track time since last write
        double interval_s = Duration(now - last_write).count();
        interval_sum  += interval_s;
        interval_count += 1;
        last_write = now;

        double y = sin(2 * M_PI * t);
        double z = sin(2 * M_PI * 2.0 * t);
        m.set("data",    y);
        m.set("rpm",     z);
        m.set("Body.vx", y - z);
        m.set("Body.vy", y + z);
        m.set("Label", "hello world");
        m.set("errcnt", 10);
        m.set("isValid", true);
        // Every 1 second write the average inter-write interval (ms)
        if (Duration(now - last_report).count() >= 1.0) {
            double avg_ms = (interval_count > 0)
                          ? (interval_sum / interval_count) * 1000.0
                          : 0.0;
            m.set("Body.avg_write_interval_ms", avg_ms);
            interval_sum   = 0.0;
            interval_count = 0;
            last_report    = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }

    return 0;
}

// build with: c++ ../spyscope/test_datafeed.cpp ../spymap/spymap.cpp -I/opt/homebrew/include -std=c++17
