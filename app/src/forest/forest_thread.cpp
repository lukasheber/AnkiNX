#include "forest/forest_thread.hpp"

#include <cstddef>

namespace
{

// QuickJS needs room for the scenes' recursion (forest_script.cpp STACK_LIMIT).
constexpr size_t STACK_SIZE = 1u << 20;

} // namespace

#ifdef __SWITCH__
bool ForestThread::start(std::function<void()> run)
{
    body       = std::move(run);
    auto entry = [](void* arg) { ((ForestThread*)arg)->body(); };
    started    = R_SUCCEEDED(threadCreate(&thread, entry, this, nullptr, STACK_SIZE, 0x2C, 2)) &&
              R_SUCCEEDED(threadStart(&thread));
    return started;
}

void ForestThread::join()
{
    if (!started)
        return;
    threadWaitForExit(&thread);
    threadClose(&thread);
    started = false;
}
#else
bool ForestThread::start(std::function<void()> run)
{
    body = std::move(run);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, STACK_SIZE);
    auto entry = [](void* arg) -> void* {
        ((ForestThread*)arg)->body();
        return nullptr;
    };
    started = pthread_create(&thread, &attr, entry, this) == 0;
    pthread_attr_destroy(&attr);
    return started;
}

void ForestThread::join()
{
    if (!started)
        return;
    pthread_join(thread, nullptr);
    started = false;
}
#endif
