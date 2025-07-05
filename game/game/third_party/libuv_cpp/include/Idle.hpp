
#ifndef  UV_IDLE_HPP
#define  UV_IDLE_HPP

#include  "EventLoop.hpp"

namespace uv
{

class Idle
{
public:
    Idle(EventLoop* loop);
    virtual ~Idle();

    
    void setCallback(DefaultCallback callback);

private:
    uv_idle_t* idle_;
	
    DefaultCallback callback_;

private:
    void onCallback();
    static void idleCallback(uv_idle_t *handle);
};

}
#endif
