#include "include/EventLoop.hpp"
#include "include/TcpConnection.hpp"
#include "include/Async.hpp"

using namespace uv;

EventLoop::EventLoop()
    :EventLoop(EventLoop::Mode::New)
{
}

EventLoop::EventLoop(EventLoop::Mode mode)
    :loop_(nullptr),
    async_(nullptr),
    status_(NotStarted)
{
    if (mode == EventLoop::Mode::New)
    {
        loop_ = new uv_loop_t();
        ::uv_loop_init(loop_);
    }
    else
    {
        loop_ = uv_default_loop();
    }
    async_ = new Async(this);
}

EventLoop::~EventLoop()
{
    if (loop_ != uv_default_loop())
    {   
        int closeResult = uv_loop_close(loop_);
        // In libuv 1.51.0, it's possible the loop still has pending operations
        if (closeResult != 0)
        {
            // Try to run the loop once more to clean up any pending operations
            ::uv_run(loop_, UV_RUN_NOWAIT);
            uv_loop_close(loop_);
        }
        delete async_;
        delete loop_;
    }
}

EventLoop* uv::EventLoop::DefaultLoop()
{
    static EventLoop defaultLoop(EventLoop::Mode::Default);
    return &defaultLoop;
}

uv_loop_t* EventLoop::handle()
{
    return loop_;
}

int EventLoop::run()
{
    if (status_ == Status::NotStarted)
    {
        async_->init();
        loopThreadId_ = std::this_thread::get_id();
        status_ = Status::Started;
        auto rst = ::uv_run(loop_, UV_RUN_DEFAULT);
        status_ = Status::Stopped;
        return rst;
    }
    return -1;
}

int uv::EventLoop::runNoWait()
{
    if (status_ == Status::NotStarted)
    {
        async_->init();
        loopThreadId_ = std::this_thread::get_id();
        status_ = Status::Started;
        auto rst = ::uv_run(loop_, UV_RUN_NOWAIT);
        status_ = Status::NotStarted;
        return rst;
    }
    return -1;
}

int uv::EventLoop::stop()
{
    if (status_ == Status::Started)
    {
        async_->close([](Async* ptr)
        {
            ::uv_stop(ptr->Loop()->handle());
        });
        return 0;
    }
    return -1;
}

bool EventLoop::isStopped()
{
    return status_ == Status::Stopped;
}

EventLoop::Status EventLoop::getStatus()
{
    return status_;
}


bool EventLoop::isRunInLoopThread()
{
    if (status_ == Status::Started)
    {
        return std::this_thread::get_id() == loopThreadId_;
    }
    //EventLoop未运行.
    return false;
}

void uv::EventLoop::runInThisLoop(const DefaultCallback func)
{
    if (nullptr == func)
        return;

    if (isRunInLoopThread() || isStopped())
    {
        func();
        return;
    }
    async_->runInThisLoop(func);
}

const char* EventLoop::GetErrorMessage(int status)
{
    if (WriteInfo::Disconnected == status)
    {
        static char info[] = "the connection is disconnected";
        return info;
    }
    return uv_strerror(status);
}