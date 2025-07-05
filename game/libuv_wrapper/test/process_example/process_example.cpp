#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <random>
#include <set>
#include "uv/include/uv11.hpp"

class ProcessStateMachine
{
public:
    enum State {
        STATE_1 = 1,    // 初始状态，等待10秒
        STATE_2 = 2,    // 随机选数字状态
        STATE_3 = 3     // 模拟写数据库状态
    };

    ProcessStateMachine(uv::EventLoop* loop)
        : loop_(loop)
        , currentState_(STATE_1)
        , countdownTimer_(nullptr)
        , randomTimer_(nullptr)
        , dbTimer_(nullptr)
        , cycleCount_(0)
        , randomEngine_(std::chrono::steady_clock::now().time_since_epoch().count())
        , randomDist_(1, 21)
    {
        std::cout << "=== 进程状态机启动 ===" << std::endl;
        enterState1();
    }

private:
    void enterState1()
    {
        currentState_ = STATE_1;
        cycleCount_++;
        std::cout << "\n[循环 " << cycleCount_ << "] 进入状态1 - 开始10秒倒计时" << std::endl;
        
        // 清空已使用的数字集合（每次进入状态1时重置）
        usedNumbers_.clear();
        
        // 创建10秒倒计时定时器
        countdownTimer_ = std::make_shared<uv::Timer>(
            loop_,
            10000,  // 10秒后触发
            0,      // 不重复
            [this](uv::Timer* timer) {
                this->onCountdownComplete();
            }
        );
        
        countdownTimer_->start();
        
        // 创建每秒显示倒计时的定时器
        int* countdown = new int(10);
        auto displayTimer = std::make_shared<uv::Timer>(
            loop_,
            1000,   // 1秒后开始
            1000,   // 每秒重复
            [this, countdown](uv::Timer* timer) {
                (*countdown)--;
                std::cout << "倒计时: " << *countdown << " 秒" << std::endl;
                
                if (*countdown <= 0) {
                    delete countdown;
                    timer->close([](uv::Timer* t) {
                        // 显示定时器关闭
                    });
                }
            }
        );
        displayTimer->start();
    }
    
    void onCountdownComplete()
    {
        std::cout << "倒计时完成！" << std::endl;
        
        // 关闭倒计时定时器
        if (countdownTimer_) {
            countdownTimer_->close([this](uv::Timer* timer) {
                countdownTimer_.reset();
                enterState2();
            });
        }
    }
    
    void enterState2()
    {
        currentState_ = STATE_2;
        std::cout << "\n进入状态2 - 开始每秒随机选择数字 (1-21)" << std::endl;
        std::cout << "目标: 选中数字11则进入状态3" << std::endl;
        
        // 创建每秒执行的随机数定时器
        randomTimer_ = std::make_shared<uv::Timer>(
            loop_,
            1000,   // 1秒后开始
            1000,   // 每秒重复
            [this](uv::Timer* timer) {
                this->onRandomTick();
            }
        );
        
        randomTimer_->start();
    }
    
    void onRandomTick()
    {
        // 如果所有数字都用完了，重新开始
        if (usedNumbers_.size() >= 21) {
            usedNumbers_.clear();
            std::cout << "所有数字都已使用，重新开始选择" << std::endl;
        }
        
        // 生成一个未使用的随机数
        int randomNum;
        do {
            randomNum = randomDist_(randomEngine_);
        } while (usedNumbers_.find(randomNum) != usedNumbers_.end());
        
        // 记录已使用的数字
        usedNumbers_.insert(randomNum);
        
        std::cout << "随机选择数字: " << randomNum;
        std::cout << " (已使用: " << usedNumbers_.size() << "/21)" << std::endl;
        
        // 检查是否选中了11
        if (randomNum == 11) {
            std::cout << "🎉 选中了11！立即停止定时器，进入状态3" << std::endl;
            
            // 停止随机数定时器
            if (randomTimer_) {
                randomTimer_->close([this](uv::Timer* timer) {
                    randomTimer_.reset();
                    enterState3();
                });
            }
        }
    }
    
    void enterState3()
    {
        currentState_ = STATE_3;
        std::cout << "\n进入状态3 - 模拟写数据库操作" << std::endl;
        std::cout << "数据库操作进行中..." << std::endl;
        
        // 模拟数据库写入操作的进度显示
        int* progress = new int(0);
        auto progressTimer = std::make_shared<uv::Timer>(
            loop_,
            500,    // 0.5秒后开始
            500,    // 每0.5秒更新进度
            [this, progress](uv::Timer* timer) {
                (*progress) += 20;
                std::cout << "数据库写入进度: " << *progress << "%" << std::endl;
                
                if (*progress >= 100) {
                    std::cout << "数据库写入完成！" << std::endl;
                    delete progress;
                    timer->close([](uv::Timer* t) {
                        // 进度定时器关闭
                    });
                }
            }
        );
        progressTimer->start();
        
        // 创建3秒后返回状态1的定时器
        dbTimer_ = std::make_shared<uv::Timer>(
            loop_,
            3000,   // 3秒后触发
            0,      // 不重复
            [this](uv::Timer* timer) {
                this->onDatabaseComplete();
            }
        );
        
        dbTimer_->start();
    }
    
    void onDatabaseComplete()
    {
        std::cout << "数据库操作完成！3秒后返回状态1" << std::endl;
        
        // 关闭数据库定时器
        if (dbTimer_) {
            dbTimer_->close([this](uv::Timer* timer) {
                dbTimer_.reset();
                
                // 检查是否需要继续循环
                if (cycleCount_ >= 3) {  // 演示：执行3个循环后退出
                    std::cout << "\n=== 完成3个循环，程序结束 ===" << std::endl;
                    loop_->stop();
                } else {
                    enterState1();  // 返回状态1，开始新的循环
                }
            });
        }
    }
    
public:
    State getCurrentState() const { return currentState_; }
    int getCycleCount() const { return cycleCount_; }

private:
    uv::EventLoop* loop_;
    State currentState_;
    int cycleCount_;
    
    // 定时器
    std::shared_ptr<uv::Timer> countdownTimer_;
    std::shared_ptr<uv::Timer> randomTimer_;
    std::shared_ptr<uv::Timer> dbTimer_;
    
    // 随机数生成
    std::mt19937 randomEngine_;
    std::uniform_int_distribution<int> randomDist_;
    std::set<int> usedNumbers_;  // 记录已使用的数字
};

int main()
{
    std::cout << "=== 进程状态机定时器示例 ===" << std::endl;
    std::cout << "状态1: 倒计时10秒" << std::endl;
    std::cout << "状态2: 每秒随机选择1-21的数字，选中11则进入状态3" << std::endl;
    std::cout << "状态3: 模拟写数据库3秒，然后返回状态1" << std::endl;
    std::cout << "========================================" << std::endl;
    
    uv::EventLoop loop;
    ProcessStateMachine process(&loop);
    
    // 启动事件循环
    std::thread loopThread([&loop]() {
        loop.run();
    });
    
    // 等待用户中断或程序自然结束
    loopThread.join();
    
    std::cout << "\n程序退出" << std::endl;
    return 0;
} 