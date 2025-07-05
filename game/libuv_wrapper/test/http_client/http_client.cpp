#include <iostream>
#include <thread>
#include <chrono>
#include "uv/include/uv11.hpp"

void printResponse(uv::http::Response* resp)
{
    std::cout << "\n--- Response ---" << std::endl;
    std::cout << "Status: " << static_cast<int>(resp->getStatusCode()) << " " << resp->getStatusInfo() << std::endl;
    
    std::string contentType = "Content-Type";
    std::string contentLength = "Content-Length";
    std::cout << "Content-Type: " << resp->getHead(contentType) << std::endl;
    std::cout << "Content-Length: " << resp->getHead(contentLength) << std::endl;
    std::cout << "Content: " << resp->getContent() << std::endl;
    std::cout << "---------------\n" << std::endl;
}

class HttpClientExample
{
public:
    HttpClientExample(uv::EventLoop* loop)
        : loop_(loop)
    {
    }

    void makeRequest(const std::string& host, int port, const std::string& path, 
                      const std::map<std::string, std::string>& params = {})
    {
        std::cout << "Making request to http://" << host << ":" << port << path << std::endl;
        
        // Create client for this request
        auto client = std::make_shared<uv::http::HttpClient>(loop_);
        clients_.push_back(client); // Keep alive until request completes
        
        // Create the request
        uv::http::Request req;
        req.setMethod(uv::http::Method::Get);
        std::string pathCopy = path;
        req.setPath(pathCopy);
        
        // Add URL parameters
        for (const auto& param : params) {
            std::string key = param.first;
            std::string value = param.second;
            req.appendUrlParam(key, value);
        }
        
        // Set headers
        std::string userAgent = "User-Agent";
        std::string userAgentValue = "uv-cpp HTTP Client Example";
        req.appendHead(userAgent, userAgentValue);
        
        std::string hostHeader = "Host";
        std::string hostValue = host + ":" + std::to_string(port);
        req.appendHead(hostHeader, hostValue);
        
        std::string accept = "Accept";
        std::string acceptValue = "*/*";
        req.appendHead(accept, acceptValue);
        
        std::string connection = "Connection";
        std::string connectionValue = "close";
        req.appendHead(connection, connectionValue);
        
        // Set the response callback
        client->setOnResp([path](uv::http::HttpClient::ReqResult result, uv::http::Response* resp) {
            std::cout << "Response received for path: " << path << std::endl;
            if (result == uv::http::HttpClient::ReqResult::Success) {
                printResponse(resp);
            } else {
                std::cout << "Request failed for path: " << path << " with result: " << result << std::endl;
            }
        });
        
        // Send the request
        uv::SocketAddr addr(host, port);
        client->Req(addr, req);
    }

private:
    uv::EventLoop* loop_;
    std::vector<std::shared_ptr<uv::http::HttpClient>> clients_; // Keep clients alive
};

int main(int argc, char** argv)
{
    // Set buffer mode for HTTP client to work properly
    uv::TcpServer::SetBufferMode(uv::GlobalConfig::ListBuffer);
    
    uv::EventLoop loop;
    HttpClientExample client(&loop);
    
    std::string host = "127.0.0.1";
    int port = 8080;
    
    if (argc > 2) {
        host = argv[1];
        port = std::atoi(argv[2]);
    }
    
    // Start event loop in a separate thread
    std::thread loopThread([&loop]() {
        loop.run();
    });
    
    // Make various HTTP requests
    client.makeRequest(host, port, "/");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    client.makeRequest(host, port, "/json");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    client.makeRequest(host, port, "/text");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    std::map<std::string, std::string> params;
    params["message"] = "Hello from HTTP client!";
    client.makeRequest(host, port, "/echo", params);
    
    // Wait for responses
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    // Clean shutdown
    loop.stop();
    loopThread.join();
    
    return 0;
} 