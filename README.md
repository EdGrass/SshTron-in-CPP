# TronGame with cpp

## Project Overview
TronGame is a multiplayer real-time game based on the classic game "Tron," allowing multiple players to participate through network connections. The game involves players controlling their "light trails" on a grid and colliding with other players' trails to eliminate them. Each player controls a light ball and needs to change the movement direction by inputting commands to avoid hitting other players' trails or boundaries. The project includes both server and client parts.

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
- **Operating System**: Supports Linux and macOS.
- **Compiler**: Uses g++ compiler, supporting C++17 standard.
- **Dependencies**:
	- `<sys/socket.h>`: For network communication.
	- `<fcntl.h>`: For controlling non-blocking sockets.
	- `<termios.h>`: For handling terminal input.
	- `<unistd.h>`: For file descriptor operations.


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

Arrow keys: Control the movement direction of the light ball.

'q' key: Exit the game.

## Project Structure
```bash
.
├── client.cpp             # Client implementation
├── server.cpp             # Server implementation
├── config.h               # Configuration file (IP, port, etc.)
├── Makefile               # Build file (optional)
└── README.md              # Project documentation
```

## Notes
Ensure stable network connections when multiple players are connected to avoid packet loss or latency affecting the game experience.
This project does not include a graphical interface and uses the terminal to display game content and accept player input.

## Acknowledgements

Thanks to [Zach Latta](https://github.com/zachlatta) for the inspiration from [sshtron](https://github.com/zachlatta/sshtron).