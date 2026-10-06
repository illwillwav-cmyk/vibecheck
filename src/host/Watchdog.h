#pragma once

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace vibecheck
{
/** Hosting third-party code in-process means a plugin can wedge on load - waiting on a licence
    server, or on a dialog that a headless process will never show. The app's message thread
    stays free, but this command would otherwise never return, so give it a deadline. */
class Watchdog
{
public:
    explicit Watchdog (int seconds)
    {
        thread = std::thread ([this, seconds]
        {
            for (int elapsed = 0; elapsed < seconds; ++elapsed)
            {
                if (done.load())
                    return;

                std::this_thread::sleep_for (std::chrono::seconds (1));
            }

            if (! done.load())
            {
                std::cout << "\ngave up after " << seconds << "s - the plugin never finished loading." << std::endl;
                std::cout.flush();
                std::_Exit (2);
            }
        });
    }

    ~Watchdog()
    {
        done.store (true);

        if (thread.joinable())
            thread.join();
    }

private:
    std::atomic<bool> done { false };
    std::thread thread;
};
} // namespace vibecheck
