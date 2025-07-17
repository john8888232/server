# Game Server

A modular game server implementation built with C++11 and libuv for handling various game types.

## Features

- TCP server for handling game requests from gateway
- Service discovery with Consul
- Database integration (MySQL, Redis, Elasticsearch)
- Modular game architecture
- Mines Pro game implementation

## Dependencies

- libuv_cpp - Network and event handling
- nlohmann/json - JSON parsing
- mysql-connector-cpp - MySQL client
- redis-plus-plus - Redis client
- elasticlient - Elasticsearch client
- ppconsul - Consul client
- Google Protocol Buffers - Message serialization
- uuid - For generating unique IDs

## Building

```bash
mkdir build
cd build
cmake ..
make
```

## Configuration

Edit the configuration files in the `config` directory:

- `server_config.json` - Main server configuration
- `games/mines_pro_config.json` - Mines Pro game configuration

## Running

```bash
./game-server
```

## Architecture

The server follows a layered architecture:

- **Core** - Base framework and interfaces
  - **Application** - Business logic and services
  - **Domain** - Core business rules and models
  - **Infrastructure** - Technical capabilities (network, databases, etc.)
  - **Interfaces** - Input/output adapters (message handlers, etc.)

- **Games** - Game implementations
  - **mines_pro** - Mines Pro game
  - **[new_game]** - Template for adding new games

- **Shared** - Common components used across games

## Adding a New Game

To add a new game:

1. Create a new directory under `games/[new_game]` using the template
2. Implement the game factory, services, and handlers
3. Register the game factory in `GameRegistry::initializeBuiltInGames()`
4. Add game configuration in `config/games/[new_game]_config.json`

## Protocol

Game server communicates with the gateway using a binary protocol:

```
//------------------------------------------------
//  length   |  msgid   | sessionid  |  data   |
//  4 bytes  |  4 bytes |  32 bytes  | N bytes |
//------------------------------------------------
```

- `length` - Total message length (4 bytes)
- `msgid` - Message ID (4 bytes)
- `sessionid` - Session ID from gateway (32 bytes)
- `data` - Protocol Buffer encoded message payload
