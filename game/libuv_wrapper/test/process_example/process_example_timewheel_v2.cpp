#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <random>
#include <set>
#include <functional>
#include "uv/include/uv11.hpp"

// 定时任务包装器 - 带有执行回调的任务
class TimerTask
{
public:
    using TaskCallback = std::function<void()>;
    
    TimerTask(TaskCallback callback) : callback_(callback), executed_(false) {}
    
    void execute() {
        if (callback_ && !executed_) {
            callback_();
            executed_ = true;
        }
    }
    
    bool isExecuted() const { return executed_; }
    
private:
    TaskCallback callback_;
    bool executed_;
};

// 改进的时间轮，支持任务执行
template<typename Type>
class ExecutableTimerWheel
{
public:
    ExecutableTimerWheel(uv::EventLoop* loop, unsigned int timeoutSec)
        : index_(0)
        , timeoutSec_(timeoutSec)
        , timer_(loop, 1000, 1000, std::bind(&ExecutableTimerWheel::wheelCallback, this))
    {
        if (timeoutSec_ > 0) {
            wheel_.resize(timeoutSec_);
        }
    }
    
    void start() {
        if (timeoutSec_ > 0) {
            timer_.start();
        }
    }
    
    void insert(std::shared_ptr<Type> value) {
        if (timeoutSec_ > 0 && nullptr != value) {
            wheel_[index_].insert(value);
        }
    }
    
    void insertAtSlot(std::shared_ptr<Type> value, unsigned int slot) {
        if (timeoutSec_ > 0 && nullptr != value && slot < timeoutSec_) {
            wheel_[slot].insert(value);
        }
    }
    
    unsigned int getCurrentIndex() const { return index_; }
    unsigned int getTimeoutSec() const { return timeoutSec_; }
    
private:
    unsigned int index_;
    unsigned int timeoutSec_;
    uv::Timer timer_;
    std::vector<std::set<std::shared_ptr<Type>>> wheel_;
    
    void wheelCallback() {
        if (!timeoutSec_) return;
        
        // 执行当前槽位的所有任务
        for (auto& task : wheel_[index_]) {
            if (task) {
                task->execute();
            }
        }
        
        // 清空当前槽位
        wheel_[index_].clear();
        
        // 移动到下一个槽位
        if (++index_ == timeoutSec_) {
            index_ = 0;
        }
    }
};

class ProcessStateMachineTimeWheel
{
public:
    enum State {
        STATE_1 = 1,    // 初始状态，等待10秒
        STATE_2 = 2,    // 随机选数字状态
        STATE_3 = 3     // 模拟写数据库状态
    };

    ProcessStateMachineTimeWheel(uv::EventLoop* loop)
        : loop_(loop)
        , currentState_(STATE_1)
        , cycleCount_(0)
        , randomEngine_(std::chrono::steady_clock::now().time_since_epoch().count())
        , randomDist_(1, 21)
        , timeWheel_(loop, 60)  // 创建60秒的时间轮
    {
        std::cout << "=== 进程状态机启动 (时间轮版本) ===" << std::endl;
        timeWheel_.start();
        enterState1();
    }

private:
    void enterState1()
    {
        currentState_ = STATE_1;
        cycleCount_++;
        std::cout << "\n[循环 " << cycleCount_ << "] 进入状态1 - 开始10秒倒计时" << std::endl;
        
        // 清空已使用的数字集合
        usedNumbers_.clear();
        
        // 创建10秒后的倒计时完成任务
        auto countdownTask = std::make_shared<TimerTask>([this]() {
            this->onCountdownComplete();
        });
        
        // 计算10秒后的槽位
        unsigned int targetSlot = (timeWheel_.getCurrentIndex() + 10) % timeWheel_.getTimeoutSec();
        timeWheel_.insertAtSlot(countdownTask, targetSlot);
        
        // 开始倒计时显示
        countdownRemaining_ = 10;
        scheduleCountdownDisplay();
    }
    
    void scheduleCountdownDisplay()
    {
        if (countdownRemaining_ > 0) {
            auto displayTask = std::make_shared<TimerTask>([this]() {
                std::cout << "倒计时: " << countdownRemaining_ << " 秒" << std::endl;
                countdownRemaining_--;
                if (countdownRemaining_ > 0) {
                    this->scheduleCountdownDisplay();
                }
            });
            
            // 插入到下一秒的槽位
            unsigned int nextSlot = (timeWheel_.getCurrentIndex() + 1) % timeWheel_.getTimeoutSec();
            timeWheel_.insertAtSlot(displayTask, nextSlot);
        }
    }
    
    void onCountdownComplete()
    {
        std::cout << "倒计时完成！" << std::endl;
        enterState2();
    }
    
    void enterState2()
    {
        currentState_ = STATE_2;
        std::cout << "\n进入状态2 - 开始每秒随机选择数字 (1-21)" << std::endl;
        std::cout << "目标: 选中数字11则进入状态3" << std::endl;
        
        scheduleRandomSelection();
    }
    
    void scheduleRandomSelection()
    {
        if (currentState_ != STATE_2) return;
        
        auto randomTask = std::make_shared<TimerTask>([this]() {
            this->onRandomTick();
        });
        
        // 插入到下一秒的槽位
        unsigned int nextSlot = (timeWheel_.getCurrentIndex() + 1) % timeWheel_.getTimeoutSec();
        timeWheel_.insertAtSlot(randomTask, nextSlot);
    }
    
    void onRandomTick()
    {
        if (currentState_ != STATE_2) return;
        
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
            std::cout << "🎉 选中了11！进入状态3" << std::endl;
            enterState3();
        } else {
            // 继续下一次随机选择
            scheduleRandomSelection();
        }
    }
    
    void enterState3()
    {
        currentState_ = STATE_3;
        std::cout << "\n进入状态3 - 模拟写数据库操作" << std::endl;
        std::cout << "数据库操作进行中..." << std::endl;
        
        // 模拟数据库写入进度
        dbProgress_ = 0;
        scheduleProgressUpdate();
        
        // 3秒后完成数据库操作
        auto dbCompleteTask = std::make_shared<TimerTask>([this]() {
            this->onDatabaseComplete();
        });
        
        // 插入到3秒后的槽位
        unsigned int targetSlot = (timeWheel_.getCurrentIndex() + 3) % timeWheel_.getTimeoutSec();
        timeWheel_.insertAtSlot(dbCompleteTask, targetSlot);
    }
    
    void scheduleProgressUpdate()
    {
        if (currentState_ != STATE_3 || dbProgress_ >= 100) return;
        
        auto progressTask = std::make_shared<TimerTask>([this]() {
            dbProgress_ += 20;
            std::cout << "数据库写入进度: " << dbProgress_ << "%" << std::endl;
            
            if (dbProgress_ < 100) {
                // 0.5秒后继续更新进度（这里简化为1秒）
                this->scheduleProgressUpdate();
            } else {
                std::cout << "数据库写入完成！" << std::endl;
            }
        });
        
        // 插入到下一秒的槽位
        unsigned int nextSlot = (timeWheel_.getCurrentIndex() + 1) % timeWheel_.getTimeoutSec();
        timeWheel_.insertAtSlot(progressTask, nextSlot);
    }
    
    void onDatabaseComplete()
    {
        std::cout << "数据库操作完成！返回状态1" << std::endl;
        
        // 检查是否需要继续循环
        if (cycleCount_ >= 3) {
            std::cout << "\n=== 完成3个循环，程序结束 ===" << std::endl;
            loop_->stop();
        } else {
            enterState1();
        }
    }
    
public:
    State getCurrentState() const { return currentState_; }
    int getCycleCount() const { return cycleCount_; }

private:
    uv::EventLoop* loop_;
    State currentState_;
    int cycleCount_;
    
    // 改进的时间轮
    ExecutableTimerWheel<TimerTask> timeWheel_;
    
    // 状态相关变量
    int countdownRemaining_;
    int dbProgress_;
    
    // 随机数生成
    std::mt19937 randomEngine_;
    std::uniform_int_distribution<int> randomDist_;
    std::set<int> usedNumbers_;
};

int main()
{
    std::cout << "=== 进程状态机时间轮示例 V2 ===" << std::endl;
    std::cout << "使用改进的时间轮实现定时任务管理" << std::endl;
    std::cout << "状态1: 倒计时10秒" << std::endl;
    std::cout << "状态2: 每秒随机选择1-21的数字，选中11则进入状态3" << std::endl;
    std::cout << "状态3: 模拟写数据库3秒，然后返回状态1" << std::endl;
    std::cout << "========================================" << std::endl;
    
    uv::EventLoop loop;
    ProcessStateMachineTimeWheel process(&loop);
    
    // 启动事件循环
    std::thread loopThread([&loop]() {
        loop.run();
    });
    
    // 等待用户中断或程序自然结束
    loopThread.join();
    
    std::cout << "\n程序退出" << std::endl;
    return 0;
} 