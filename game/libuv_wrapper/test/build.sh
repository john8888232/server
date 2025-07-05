#!/bin/bash

# Text colors
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Check if libuv_cpp.a exists
if [ ! -f "../lib/libuv_cpp.a" ]; then
    echo -e "${RED}Error: ../lib/libuv_cpp.a not found. Please build the main library first.${NC}"
    exit 1
fi

# Create build directory
mkdir -p build
cd build

# Build using CMake
echo -e "${GREEN}Configuring examples with CMake...${NC}"
cmake .. || { echo -e "${RED}CMake configuration failed.${NC}"; exit 1; }

echo -e "${GREEN}Building examples...${NC}"
make || { echo -e "${RED}Build failed.${NC}"; exit 1; }

echo -e "${GREEN}Build successful! Executables are in build/bin directory.${NC}"
echo -e "Run examples with:"
echo -e "${GREEN}  HTTP Server:${NC} ./bin/http_server [ip] [port]"
echo -e "${GREEN}  HTTP Client:${NC} ./bin/http_client [ip] [port]"
echo -e "${GREEN}  TCP Server:${NC} ./bin/basic_server [ip] [port]"
echo -e "${GREEN}  TCP Client:${NC} ./bin/basic_client [ip] [port]"
echo -e "${GREEN}  UDP Example:${NC} ./bin/udp_example [ip] [port]"
echo -e "${GREEN}  DNS Example:${NC} ./bin/dns_example [hostname1] [hostname2] ..." 