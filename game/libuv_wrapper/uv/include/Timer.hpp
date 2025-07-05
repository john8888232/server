#ifndef UV_TIMER_HPP
#define UV_TIMER_HPP

#include <functional>
#include <mutex>
#include "EventLoop.hpp"
namespace uv
{

class Timer
{
public:
    using TimerCallback = std::function<void(Timer*)>;
    using TimerCloseComplete = std::function<void(Timer*)>;

    Timer(EventLoop* loop, uint64_t timeout, uint64_t repeat, TimerCallback callback);
    virtual ~Timer();

    void start();
    void close(TimerCloseComplete callback);
    void setTimerRepeat(uint64_t ms);

private:
    bool started_;
    uv_timer_t* handle_;
    uint64_t timeout_;
    uint64_t repeat_;
    TimerCallback callback_;

    TimerCloseComplete closeComplete_;

private:
    void onTimeOut();
    void closeComplete();

    static void process(uv_timer_t* handle);

};

}
#endif
