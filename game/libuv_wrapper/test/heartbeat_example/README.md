# Heartbeat Example

This example demonstrates how to implement a heartbeat mechanism using the `packetz` packet format in uv-cpp. The protocol ID `0x10001` is used for heartbeat packets.

## Protocol Format

The packet format follows the standard packetz format:

```
------------------------------------------------
|  length   |  msgid   | sessionid  |  data   |
|  4 bytes  |  4 bytes |  32 bytes  | N bytes |
------------------------------------------------
```

For heartbeat packets:
- `length`: Total packet size (4 + 4 + 32 + data size)
- `msgid`: 0x10001 (heartbeat protocol ID)
- `sessionid`: 32 bytes (zeroed in this example)
- `data`: Empty for heartbeat packets

## Components

### Heartbeat Server

The heartbeat server listens for incoming connections and responds to heartbeat packets with the same protocol ID. It:
1. Receives packets and checks if they are heartbeat packets (protocol ID 0x10001)
2. If it's a heartbeat packet, sends a heartbeat response back to the client
3. For other packet types, logs and ignores them

### Heartbeat Client

The heartbeat client connects to the server and sends periodic heartbeat packets. It:
1. Connects to the server
2. Starts sending heartbeat packets at regular intervals (default: 5 seconds)
3. Listens for heartbeat responses from the server

## Usage

### Building the Example

```bash
cd examples
mkdir build && cd build
cmake ..
make
```

### Running the Example

1. Start the server:
```bash
./bin/heartbeat_server [ip] [port]
```

2. Start the client:
```bash
./bin/heartbeat_client [server_ip] [server_port] [interval_ms]
```

- Default server IP: 0.0.0.0 (listen on all interfaces)
- Default client IP: 127.0.0.1 (localhost)
- Default port: 10000
- Default heartbeat interval: 5000ms (5 seconds)

## Implementation Notes

- The heartbeat client will automatically send heartbeats every 5 seconds (configurable)
- The heartbeat server will respond to each heartbeat with a heartbeat packet
- Both client and server print debug information about heartbeat packets 