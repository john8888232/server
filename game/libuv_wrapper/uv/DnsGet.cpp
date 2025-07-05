#include "include/DnsGet.hpp"
#include "include/LogWriter.hpp"
#include <cstring>

using namespace uv;

DNSGet::DNSGet(uv::EventLoop* loop)
	:loop_(loop),
    callback_(nullptr)
{
    handle_.data = (void*) this;
}

void DNSGet::setOnDNSCallback(OnGetIPAddrCallback callback)
{
    callback_ = callback;
}

int DNSGet::GetIP(std::string& hostname, std::string service)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    return uv_getaddrinfo(loop_->handle(), &handle_, DNSGet::onDNSGet, 
                         hostname.c_str(), 
                         service.empty() ? nullptr : service.c_str(), 
                         &hints);
}

int DNSGet::GetIP(std::string&& hostname, std::string service)
{
    return GetIP(hostname, service);
}

void DNSGet::OnCallback(int status, addrinfo* res)
{
    if (nullptr != callback_)
    {
        std::string ip("");
        if (status < 0) {
            callback_(status, ip);
            return;
        }
        ip.resize(64); // Increased buffer size for IPv6 addresses
#if    _MSC_VER
        if (res->ai_family == AF_INET)
        
#else
        if (res->ai_family == AF_INET)
#endif
        {
            uv_ip4_name((struct sockaddr_in*) res->ai_addr, (char*)ip.c_str(), ip.size());
        }
        else if (res->ai_family == AF_INET6)
        {
            uv_ip6_name((struct sockaddr_in6*) res->ai_addr, (char*)ip.c_str(), ip.size());
        }
        // Resize string to actual content length
        ip.resize(strlen(ip.c_str()));
        callback_(status, ip);
    }
}

void DNSGet::onDNSGet(uv_getaddrinfo_t* addrInfo, int status, addrinfo* res)
{
    auto ptr = (DNSGet*)(addrInfo->data);
    if (ptr)
    {
        ptr->OnCallback(status, res);
        uv_freeaddrinfo(res);
    }
}

