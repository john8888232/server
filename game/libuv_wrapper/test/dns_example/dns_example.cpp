#include <iostream>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include "uv/include/uv11.hpp"

class DnsExample 
{
public:
    DnsExample(uv::EventLoop* loop)
        : loop_(loop), dnsGet_(loop)
    {
        // Set callback to receive DNS results
        dnsGet_.setOnDNSCallback([this](int status, std::string& ip) {
            std::lock_guard<std::mutex> lock(mutex_);
            
            if (status < 0) {
                std::cout << "DNS lookup failed: " << uv::EventLoop::GetErrorMessage(status) << std::endl;
            } else {
                std::cout << "Resolved: " << currentHostname_ << " -> " << ip << std::endl;
            }
            
            // Signal that we got a result
            lookupComplete_ = true;
            cv_.notify_one();
        });
    }
    
    void lookupHostname(const std::string& hostname) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            lookupComplete_ = false;
            currentHostname_ = hostname;
            
            std::cout << "Looking up hostname: " << hostname << std::endl;
            int result = dnsGet_.GetIP(currentHostname_, "");
            
            if (result < 0) {
                std::cout << "DNS lookup request failed: " << uv::EventLoop::GetErrorMessage(result) << std::endl;
                lookupComplete_ = true;
                cv_.notify_one();
            }
        }
        
        // Wait for lookup to complete
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this]() { return lookupComplete_; });
    }

private:
    uv::EventLoop* loop_;
    uv::DNSGet dnsGet_;
    
    std::mutex mutex_;
    std::condition_variable cv_;
    bool lookupComplete_ = false;
    std::string currentHostname_;
};

int main(int argc, char** argv)
{
    uv::EventLoop loop;
    DnsExample dnsExample(&loop);
    
    // Start event loop in a separate thread
    std::thread loopThread([&loop]() {
        loop.run();
    });
    
    // List of hostnames to look up
    std::vector<std::string> hostnames;
    
    if (argc > 1) {
        // Use hostnames from command line arguments
        for (int i = 1; i < argc; i++) {
            hostnames.push_back(argv[i]);
        }
    } else {
        // Default hostnames to look up
        hostnames = {
            "www.google.com",
            "github.com",
            "www.baidu.com",
            "localhost",
            "invalid.hostname.that.does.not.exist"
        };
    }
    
    // Look up each hostname
    for (const auto& hostname : hostnames) {
        dnsExample.lookupHostname(hostname);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    // Clean shutdown
    loop.stop();
    loopThread.join();
    
    return 0;
} 