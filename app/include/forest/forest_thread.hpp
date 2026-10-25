#pragma once

#include <functional>

#ifdef __SWITCH__
#include <switch.h>
#else
#include <pthread.h>
#endif

// A thread for the forest's QuickJS (app/forest): with room for the scenes'
// recursion (1 MB of stack), and on the Switch on core 2 - the UI's is 0, and
// newlib threads start on the process' default core.
class ForestThread
{
  public:
    ~ForestThread() { join(); }

    // Run `body` on the thread; false if it could not be made.
    bool start(std::function<void()> body);
    // Wait for it to end (nothing if it never started).
    void join();

  private:
    std::function<void()> body;
    bool started = false;
#ifdef __SWITCH__
    Thread thread;
#else
    pthread_t thread;
#endif
};
