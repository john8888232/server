#include <iostream>
#include <chrono>
#include "uv/include/uv11.hpp"
#include <thread>

int main()
{
    // Set buffer mode for HTTP client to work properly
    uv::TcpServer::SetBufferMode(uv::GlobalConfig::ListBuffer);
    
    uv::EventLoop loop;
    uv::http::HttpClient client(&loop);
    
    bool responseReceived = false;
    
    // Set response callback
    client.setOnResp([&responseReceived](uv::http::HttpClient::ReqResult result, uv::http::Response* resp) {
        std::cout << "Response callback called!" << std::endl;
        responseReceived = true;
        
        if (result == uv::http::HttpClient::ReqResult::Success) {
            std::cout << "Status: " << static_cast<int>(resp->getStatusCode()) << std::endl;
            std::cout << "Content: " << resp->getContent() << std::endl;
        } else {
            std::cout << "Request failed with result: " << result << std::endl;
        }
    });
    
    // Create request
    uv::http::Request req;
    req.setMethod(uv::http::Method::Get);
    std::string path = "/json";
    req.setPath(path);
    
    std::string userAgent = "User-Agent";
    std::string userAgentValue = "Simple-Test-Client";
    req.appendHead(userAgent, userAgentValue);
    
    std::string host = "Host";
    std::string hostValue = "127.0.0.1:8080";
    req.appendHead(host, hostValue);
    
    // Send request
    uv::SocketAddr addr("127.0.0.1", 8080);
    client.Req(addr, req);
    
    std::cout << "Request sent, starting event loop..." << std::endl;
    
    // Run event loop for a limited time
    auto start = std::chrono::steady_clock::now();
    while (!responseReceived) {
        int result = loop.runNoWait();
        if (result == 0) {
            // No more events to process, wait a bit
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - start).count() > 5) {
            std::cout << "Timeout waiting for response" << std::endl;
            break;
        }
    }
    
    std::cout << "Test completed" << std::endl;
    return 0;
} 