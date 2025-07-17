#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include "uv/include/uv11.hpp"

class TimerExample
{
public:
    TimerExample(uv::EventLoop* loop)
        : loop_(loop)
        , timerCount_(0)
        , oneTimeTimer_(nullptr)
        , repeatTimer_(nullptr)
    {
    }
    
    void createOneTimeTimer(uint64_t timeoutMs)
    {
        std::cout << "创建一次性定时器，" << timeoutMs << "毫秒后触发" << std::endl;
        
        // 创建一次性定时器 (timeout=timeoutMs, repeat=0)
        oneTimeTimer_ = std::make_shared<uv::Timer>(
            loop_, 
            timeoutMs, 
            0,  // repeat=0 表示只执行一次
            [this](uv::Timer* timer) {
                std::cout << "一次性定时器触发！" << std::endl;
                this->onOneTimeTimeout(timer);
            }
        );
        
        // 启动定时器
        oneTimeTimer_->start();
    }
    
    void createRepeatTimer(uint64_t timeoutMs, uint64_t repeatMs)
    {
        std::cout << "创建重复定时器，" << timeoutMs << "毫秒后首次触发，然后每" << repeatMs << "毫秒重复" << std::endl;
        
        // 创建重复定时器
        repeatTimer_ = std::make_shared<uv::Timer>(
            loop_, 
            timeoutMs,  // 首次触发延迟
            repeatMs,   // 重复间隔
            [this](uv::Timer* timer) {
                this->onRepeatTimeout(timer);
            }
        );
        
        // 启动定时器
        repeatTimer_->start();
    }
    
    void stopRepeatTimer()
    {
        if (repeatTimer_) {
            std::cout << "停止重复定时器..." << std::endl;
            repeatTimer_->close([this](uv::Timer* timer) {
                std::cout << "重复定时器已关闭" << std::endl;
                repeatTimer_.reset();
            });
        }
    }
    
    void changeRepeatInterval(uint64_t newRepeatMs)
    {
        if (repeatTimer_) {
            std::cout << "修改重复间隔为 " << newRepeatMs << " 毫秒" << std::endl;
            repeatTimer_->setTimerRepeat(newRepeatMs);
        }
    }

private:
    void onOneTimeTimeout(uv::Timer* timer)
    {
        std::cout << "一次性定时器执行完毕，自动清理" << std::endl;
        // 一次性定时器执行完后关闭
        timer->close([this](uv::Timer* timer) {
            std::cout << "一次性定时器已关闭" << std::endl;
            oneTimeTimer_.reset();
        });
    }
    
    void onRepeatTimeout(uv::Timer* timer)
    {
        timerCount_++;
        std::cout << "重复定时器第 " << timerCount_ << " 次触发" << std::endl;
        
        // 演示：触发5次后停止
        if (timerCount_ >= 5) {
            stopRepeatTimer();
        }
    }

private:
    uv::EventLoop* loop_;
    int timerCount_;
    std::shared_ptr<uv::Timer> oneTimeTimer_;
    std::shared_ptr<uv::Timer> repeatTimer_;
};

int main()
{
    std::cout << "=== libuv-cpp 定时器模块演示 ===" << std::endl;
    
    uv::EventLoop loop;
    TimerExample example(&loop);
    
    // 启动事件循环线程
    std::thread loopThread([&loop]() {
        loop.run();
    });
    
    // 演示1：创建一次性定时器
    example.createOneTimeTimer(2000);  // 2秒后触发
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 演示2：创建重复定时器
    example.createRepeatTimer(1000, 1500);  // 1秒后首次触发，然后每1.5秒重复
    
    // 等待3秒后修改重复间隔
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    example.changeRepeatInterval(800);  // 改为每800毫秒重复
    
    // 等待定时器执行完成
    std::this_thread::sleep_for(std::chrono::seconds(10));
    
    // 清理并退出
    std::cout << "程序即将退出..." << std::endl;
    loop.stop();
    loopThread.join();
    
    std::cout << "程序退出" << std::endl;
    return 0;
} 