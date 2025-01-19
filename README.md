# TronGame with cpp

## Project Overview
TronGame is a multiplayer real-time game based on the terminal, where players control the movement of the light ball through keyboard input. The game interface is displayed via text, and it doesn't rely on a graphical user interface. This allows the game to run in a **command-line** environment and enables real-time multiplayer interactions over the network. Inspired by the classic game "Tron," it allows multiple players to participate via network connections. In the game, players control their "light trails" on a grid, colliding with other players' trails to eliminate them. Each player controls a light ball and needs to change its movement direction by inputting commands to avoid hitting other players' trails or boundaries. The project includes both server and client parts.

Here is a screenshot of my game running:
![](https://s2.loli.net/2025/01/17/UaFogqOIjxB5skS.jpg)

## Features
- **Multiplayer**: Supports multiple players playing the game simultaneously.
- **Light Trail Mechanism**: Players control light balls moving on the grid, leaving light trails. Other players will be eliminated if they hit the trails.
- **Scoring System**: Players earn points based on survival time and eliminating other players.
- **Player Management**: Each player has unique information such as color, score, and high score.
- **Real-time Updates**: The game state is updated in real-time and sent to all connected clients via the network.
- **Server**: Provides game logic, player connections, and management.

## Environment
- **Operating System**: Supports macOS and Unix/Linux.
- **Compiler**: Uses g++ compiler, supporting C++11 standard.
- **Dependencies**:
	- `<sys/socket.h>`: For network communication.
	- `<fcntl.h>`: For controlling non-blocking sockets.
	- `<termios.h>`: For handling terminal input.
	- `<unistd.h>`: For file descriptor operations.
	- `<sys/select.h>`: For using the select() system call, allowing multiplexed I/O operations (to monitor multiple file descriptors).
	- `<unistd.h>`: For various system-level operations, including file descriptor operations like read(), write(), and close().


## Configuration
1. Configure the server IP address and port number in `config.h`:
	 ```cpp
	 #define SERVER_IP "127.0.0.1"  // Server IP address
	 #define SERVER_PORT 8080      // Server port
	 ```
2. Configure other parameters such as the maximum number of players, game board width, and height.


## Compilation and Build
Navigate to the project directory and compile using the following command:

```bash
make
```

## Running the Game

1. Run the Server
On the server machine, run the server:
```
./server
```

2. Run the Client
On the client machine, run the client:
```
./client
```
The client will connect to the specified server, and players can control the light ball on the game board and compete with other players.

## Game Controls

- "WSAD" keys: Control the movement direction of the light ball.

- 'Q' key: Exit the game.

## Project Structure
```bash
.
├── client.cpp             # Client implementation
├── server.cpp             # Server implementation
├── config.h               # Configuration file (IP, port, etc.)
├── Makefile               # Build file (optional)
└── README.md              # Project documentation
```

## Acknowledgements

Thanks to [Zach Latta](https://github.com/zachlatta) for the inspiration from [sshtron](https://github.com/zachlatta/sshtron).