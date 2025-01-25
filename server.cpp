#include <map>          
#include <mutex>        
#include <locale>       
#include <thread>       
#include <vector>      
#include <random>       
#include <fcntl.h>      
#include <errno.h>      
#include <codecvt>      
#include <fstream>      
#include <cstring>      
#include <unistd.h>     
#include <iostream>      
#include <sys/socket.h> 
#include <netinet/in.h> 
#include <sys/select.h> 
#include "config.h"    

#ifdef DEBUG_MODE
#define DEBUG_LOG(msg, ...) printf("[DEBUG] " msg "\n", ##__VA_ARGS__)
#else
#define DEBUG_LOG(msg, ...)
#endif

struct Player {
    int x, y;            
    int dx, dy;          
    bool alive;          
    int socket;          
    int playerIndex;     
    int score;           
    int highScore;       
    time_t lastScoreTime;
    int colorIndex;      
};

class TronGame {
private:
    std::vector<std::vector<int>> board;    
    std::vector<Player> players;            
    std::mutex gameMutex;                   
    bool gameRunning;                       
    std::map<int, int> highScores;          
    std::vector<bool> usedPlayerIndices;    
    std::vector<bool> usedColorIndices;     
    std::map<int, int> socketToHighScores;  

    struct PlayerScore {
        int current;     
        int high;        
        int colorIndex;  
    };
    std::map<int, PlayerScore> playerScores;  

    bool isSafePosition(int x, int y) {
        for (int i = -INIT_SPACE_CHECK; i <= INIT_SPACE_CHECK; i++) {
            for (int j = -INIT_SPACE_CHECK; j <= INIT_SPACE_CHECK; j++) {
                int checkX = x + i;
                int checkY = y + j;
                if (checkX >= 0 && checkX < BOARD_WIDTH &&
                    checkY >= 0 && checkY < BOARD_HEIGHT &&
                    board[checkY][checkX] != 0) {
                    return false;
                }
            }
        }
        return true;
    }

    std::pair<int, int> getRandomSafePosition() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> disX(INIT_SPACE_CHECK, BOARD_WIDTH - INIT_SPACE_CHECK - 1);
        std::uniform_int_distribution<> disY(INIT_SPACE_CHECK, BOARD_HEIGHT - INIT_SPACE_CHECK - 1);
        std::uniform_int_distribution<> disDir(0, 3);

        for (int attempts = 0; attempts < 100; attempts++) {
            int x = disX(gen);
            int y = disY(gen);
            if (isSafePosition(x, y)) {
                return {x, y};
            }
        }
        throw std::runtime_error("Unable to find a safe starting position");
    }

    std::pair<int, int> getRandomDirection() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 3);
        int dir = dis(gen);
        switch (dir) {
            case 0: return {0, -1};  
            case 1: return {0, 1};   
            case 2: return {-1, 0};  
            default: return {1, 0};  
        }
    }

    void loadHighScores() {
        std::ifstream file(HIGH_SCORE_FILE);
        int socket, score;
        while (file >> socket >> score) {
            socketToHighScores[socket] = score;
        }
    }

    void saveHighScores() {
        std::ofstream file(HIGH_SCORE_FILE);
        for (const auto& [socket, score] : playerScores) {
            file << socket << " " << score.high << std::endl;
        }
    }

    void updatePlayerScore(Player& player) {
        if (!player.alive) return;

        time_t now = time(nullptr);
        int timeDiff = static_cast<int>(now - player.lastScoreTime);

        if (timeDiff > 0) {
            int scoreIncrease = timeDiff * SCORE_SURVIVAL_TIME;
            player.score += scoreIncrease;
            player.lastScoreTime = now;

            DEBUG_LOG("Player %d score increased by %d new score: %d", 
                      player.playerIndex + 1, scoreIncrease, player.score);
        }
    }

    bool checkCollision(int x, int y, const Player& player, Player** killer) {
        if (x < 0 || x >= BOARD_WIDTH || y < 0 || y >= BOARD_HEIGHT) {
            return true;
        }
        
        if (board[y][x] != 0) {
            int killerColorIndex = board[y][x] - 1;  
            if (killerColorIndex == player.colorIndex) {
                return false;  
            }
            for (auto& p : players) {
                if (p.colorIndex == killerColorIndex) {
                    *killer = &p;
                    break;
                }
            }
            return true;
        }
        return false;
    }

    std::string serializeGameState() {	// DEBUG USE
        std::string state;
        state += "BEGIN\n";
        state += "STATUS:" + std::to_string(gameRunning) + "\n";
        state += "PLAYERS\n";
        for (const auto& player : players) {
            state += std::to_string(player.colorIndex) + ":" +  
                     std::to_string(player.playerIndex) + "," +
                     std::to_string(player.score) + "," +
                     std::to_string(player.highScore) + "," +
                     std::to_string(player.alive) + "," +
                     std::to_string(player.x) + "," +
                     std::to_string(player.y) + "," +
                     std::to_string(player.dx) + "," +
                     std::to_string(player.dy) + "\n";
        }
        state += "BOARD\n";
        for (const auto& row : board) {
            for (int cell : row) {
                state += std::to_string(cell) + ",";
            }
            state += "\n";
        }
        state += "END\n";
        return state;
    }

    void clearPlayerTrail(int colorIndex) {  
        for (auto& row : board) {
            for (auto& cell : row) {
                if (cell == colorIndex + 1) {  
                    cell = 0;
                }
            }
        }
    }

    void respawnPlayer(Player& player) {
        try {
            auto [x, y] = getRandomSafePosition();
            auto [dx, dy] = getRandomDirection();
            
            clearPlayerTrail(player.colorIndex); 
            player.x = x;
            player.y = y;
            player.dx = dx;
            player.dy = dy;
            player.alive = true;
            player.score = 0;  
            board[y][x] = player.colorIndex + 1;  

            DEBUG_LOG("Player %d (color: %d) respawned at position (%d,%d)", 
                      player.playerIndex + 1, player.colorIndex + 1, x, y);

        } catch (const std::runtime_error& e) {
            std::cerr << "Error respawning player " << player.playerIndex + 1 
                      << ": " << e.what() << std::endl;
        }
    }

    void handlePlayerDeath(Player& player, Player* killer, const std::string& cause) {
        if (!player.alive) return;

        updatePlayerScore(player);
        int finalScore = player.score;

        if (finalScore > player.highScore) {
            player.highScore = finalScore;
            highScores[player.socket] = finalScore;
            saveHighScores();
        }

        if (killer != nullptr && killer != &player && killer->alive) {
            int scoreTransfer = SCORE_KILL_POINTS + 
                              static_cast<int>(finalScore * SCORE_TRANSFER_RATE);
            killer->score += scoreTransfer;
            
            std::cout << "Player " << killer->playerIndex + 1 
                     << " killed Player " << player.playerIndex + 1 
                     << " [score:" << scoreTransfer << " = " 
                     << SCORE_KILL_POINTS << " + " 
                     << static_cast<int>(finalScore * SCORE_TRANSFER_RATE) 
                     << "(" << (SCORE_TRANSFER_RATE * 100) << "% of " << finalScore 
                     << ")]" << std::endl;
        } else {
            std::cout << "Player " << player.playerIndex + 1 
                     << " died by " << cause 
                     << " with score " << finalScore << std::endl;
        }

        player.alive = false;
        player.score = 0;
        player.lastScoreTime = time(nullptr);
        clearPlayerTrail(player.colorIndex);

        std::string state = serializeGameState();
        for (const auto& p : players) {
            send(p.socket, state.c_str(), state.length(), 0);
        }
    }

    int findAvailablePlayerIndex() {
        std::vector<bool> used(MAX_PLAYERS, false);
        for (const auto& player : players) {
            if (player.playerIndex < MAX_PLAYERS) {
                used[player.playerIndex] = true;
            }
        }
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!used[i]) return i;
        }
        return -1;
    }

    void occupyColorIndex(int colorIndex) {
        if (colorIndex >= 0 && colorIndex < MAX_PLAYERS) {
            usedColorIndices[colorIndex] = true;
        }
    }

    void debugPrintState() {	// DEBUG USE
        DEBUG_LOG("\nCurrent game state:");
        DEBUG_LOG("Used color indices: ");
        for (int i = 0; i < MAX_PLAYERS; i++) {
            DEBUG_LOG("%d:%d ", i, usedColorIndices[i] ? 1 : 0);
        }
        DEBUG_LOG("\nPlayers:");
        for (const auto& p : players) {
            DEBUG_LOG("Player %d (color:%d, socket:%d, score:%d)", 
                      p.playerIndex, p.colorIndex, p.socket, p.score);
        }
        DEBUG_LOG("");
    }

    void initializeNewPlayer(Player& p) {
        PlayerScore score = {0, 0, p.colorIndex};
        if (playerScores.find(p.socket) != playerScores.end()) {
            score.high = playerScores[p.socket].high;
        }
        playerScores[p.socket] = score;
    }

public:
    TronGame() {
        board = std::vector<std::vector<int>>(BOARD_HEIGHT, 
                std::vector<int>(BOARD_WIDTH, 0));
        gameRunning = true;
        loadHighScores();
        usedColorIndices = std::vector<bool>(MAX_PLAYERS, false);
    }

    void addPlayer(int socket) {
        std::lock_guard<std::mutex> lock(gameMutex);
        if (players.size() < MAX_PLAYERS) {
            try {
                auto [x, y] = getRandomSafePosition();
                auto [dx, dy] = getRandomDirection();
                int colorIndex = -1;
                for (int i = 0; i < MAX_PLAYERS; i++) {
                    if (!usedColorIndices[i]) {
                        colorIndex = i;
                        break;
                    }
                }
                int playerIndex = findAvailablePlayerIndex();
                if (playerIndex < 0 || colorIndex < 0) {
                    std::cerr << "No available slots" << std::endl;
                    close(socket);
                    return;
                }
                usedColorIndices[colorIndex] = true;
                Player p = {x, y, dx, dy, true, socket, playerIndex,
                          0, socketToHighScores[socket], time(nullptr)};
                p.colorIndex = colorIndex;

                DEBUG_LOG("Adding new player - socket:%d playerIndex:%d colorIndex:%d", 
                         socket, playerIndex, colorIndex);

                std::string indexMsg = "INDEX:" + std::to_string(playerIndex) + 
                                     "," + std::to_string(colorIndex) + "\n";
                send(socket, indexMsg.c_str(), indexMsg.length(), 0);

                players.push_back(p);
                board[y][x] = colorIndex + 1;
                initializeNewPlayer(p);  
                debugPrintState();

                std::string state = serializeGameState();
                if (send(socket, state.c_str(), state.length(), 0) <= 0) {
                    std::cerr << "Failed to send initial state to player" << std::endl;
                    close(socket);
                    return;
                }
                std::cout << "Player " << playerIndex + 1 << " joined the game" << std::endl;
            } catch (const std::runtime_error& e) {
                DEBUG_LOG("Failed to add new player: %s", e.what());
                close(socket);
            }
        }
    }

    void handleInput(int colorIndex, char input) {  
        std::lock_guard<std::mutex> lock(gameMutex);
        
        auto it = std::find_if(players.begin(), players.end(),
            [colorIndex](const Player& p) { return p.colorIndex == colorIndex; });
        if (it == players.end() || !it->alive) return;
        Player& player = *it;
        
        DEBUG_LOG("Received input from player %d (color:%d): %c", 
                  player.playerIndex + 1, player.colorIndex, input);
        
        int newDx = player.dx;
        int newDy = player.dy;
        switch(input) {
            case KEY_UP:    if (player.dy != 1)  { newDx = 0; newDy = -1; } break;
            case KEY_DOWN:  if (player.dy != -1) { newDx = 0; newDy = 1; }  break;
            case KEY_LEFT:  if (player.dx != 1)  { newDx = -1; newDy = 0; } break;
            case KEY_RIGHT: if (player.dx != -1) { newDx = 1; newDy = 0; }  break;
        }
        
        if (newDx != player.dx || newDy != player.dy) {
            player.dx = newDx;
            player.dy = newDy;

            DEBUG_LOG("Player %d direction changed to: (%d,%d)", 
                      player.playerIndex + 1, player.dx, player.dy);
        }
    }
	
    int getPlayerSocket(int playerIndex) {
        std::lock_guard<std::mutex> lock(gameMutex);
        if (playerIndex >= 0 && playerIndex < players.size()) {
            return players[playerIndex].socket;
        }
        return -1;
    }

    size_t getPlayerCount() const {
        return players.size();
    }

    void removePlayer(int playerIndex) {
        std::lock_guard<std::mutex> lock(gameMutex);
        auto it = std::find_if(players.begin(), players.end(),
            [playerIndex](const Player& p) { return p.playerIndex == playerIndex; });
        
        if (it != players.end()) {
            DEBUG_LOG("Removing player - index:%d color:%d", 
                     playerIndex, it->colorIndex);

            usedColorIndices[it->colorIndex] = false;
            
            socketToHighScores[it->socket] = std::max(
                socketToHighScores[it->socket], 
                it->score
            );
            saveHighScores();
            clearPlayerTrail(it->colorIndex);
            players.erase(it);
            debugPrintState();

            std::string state = serializeGameState();
            std::vector<int> failedPlayers;
            for (const auto& p : players) {
                if (send(p.socket, state.c_str(), state.length(), 0) < 0) {
                    failedPlayers.push_back(p.playerIndex);
                }
            }
            for (int index : failedPlayers) {
                removePlayer(index);
            }
        }
    }

    void updateGame() {
        if (!gameRunning) return;
        std::lock_guard<std::mutex> lock(gameMutex);

        std::vector<int> disconnectedPlayers;
        for (const auto& player : players) {
            int testSend = send(player.socket, "", 0, MSG_NOSIGNAL);
            if (testSend < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                disconnectedPlayers.push_back(player.playerIndex);
            }
        }
        for (int index : disconnectedPlayers) {
            removePlayer(index);
        }

        static std::map<int, time_t> deathTimes;
        bool stateChanged = false;

        for (auto& player : players) {
            if (player.alive) {
                updatePlayerScore(player);
            }
        }

        for (auto& player : players) {
            if (player.alive) {
                int newX = player.x + player.dx;
                int newY = player.y + player.dy;

                DEBUG_LOG("Moving player %d from (%d,%d) to (%d,%d)", 
                          player.playerIndex + 1, player.x, player.y, newX, newY);

                Player* killer = nullptr;
                bool willCollide = checkCollision(newX, newY, player, &killer);
                board[player.y][player.x] = player.colorIndex + 1;
                if (willCollide) {
                    handlePlayerDeath(player, killer, killer ? "be killde" : "crash");
                    stateChanged = true;
                    continue;
                }
                player.x = newX;
                player.y = newY;
                board[player.y][player.x] = player.colorIndex + 1;
                stateChanged = true;
            } else {
                time_t now = time(nullptr);
                auto it = deathTimes.find(player.playerIndex);
                if (it == deathTimes.end()) {
                    deathTimes[player.playerIndex] = now;
                } else if (now - it->second >= RESPAWN_DELAY) {
                    respawnPlayer(player);
                    deathTimes.erase(it);
                    stateChanged = true;
                }
            }
        }

        if (stateChanged) {
            DEBUG_LOG("Game state updated. Active players: ");
            for (const auto& player : players) {
                DEBUG_LOG("Player %d(%s at %d,%d moving %d,%d) ", 
                          player.playerIndex + 1, player.alive ? "alive" : "dead", 
                          player.x, player.y, player.dx, player.dy);
            }
            DEBUG_LOG("");

            std::string state = serializeGameState();
            std::vector<int> failedPlayers;
            for (const auto& player : players) {
                if (send(player.socket, state.c_str(), state.length(), 0) < 0) {
                    failedPlayers.push_back(player.playerIndex); 
                }
            }
            
            for (int index : failedPlayers) {
                removePlayer(index);
            }
        }
    }

    int getColorIndexBySocket(int socket) {
        std::lock_guard<std::mutex> lock(gameMutex);
        for (const auto& player : players) {
            if (player.socket == socket) {
                return player.colorIndex;
            }
        }
        return -1;
    }
};

void handleClient(TronGame& game, int playerIndex) {
    char buffer[BUFFER_SIZE];
    int playerSocket = game.getPlayerSocket(playerIndex);
    int colorIndex = game.getColorIndexBySocket(playerSocket);  
    time_t lastHeartbeat = time(nullptr);
    bool connectionAlive = true;
    
    #ifdef __APPLE__
        int flags = fcntl(playerSocket, F_GETFL, 0);
        fcntl(playerSocket, F_SETFL, flags | O_NONBLOCK);
    #else
        int flags = 1;
        ioctl(playerSocket, FIONBIO, &flags);
    #endif
    
    if (playerIndex < 0) {
        std::cerr << "Invalid player index" << std::endl;
        close(playerSocket);
        return;
    }

    while (connectionAlive) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(playerSocket, &readfds);
        
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000; 
        
        int selectResult = select(playerSocket + 1, &readfds, NULL, NULL, &tv);
        if (selectResult < 0) {
            std::cerr << "Select error for player " << playerIndex << std::endl;
            break;
        }
        
        time_t now = time(nullptr);
        if (now - lastHeartbeat > SOCKET_TIMEOUT) {
            std::cout << "Player " << playerIndex + 1 << " timeout" << std::endl;
            break;
        }
        
        if (selectResult > 0 && FD_ISSET(playerSocket, &readfds)) {
            int bytesRead = recv(playerSocket, buffer, sizeof(buffer), 0);
            if (bytesRead <= 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    connectionAlive = false;
                }
                continue;
            }
            
            lastHeartbeat = now;
            if (buffer[0] == 'h') {
                DEBUG_LOG("Heartbeat received from player %d", playerIndex + 1);
                continue;
            }
            game.handleInput(colorIndex, buffer[0]); 
        }
    }
    
    std::cout << "Player " << playerIndex + 1 << " disconnected" << std::endl;
    game.removePlayer(playerIndex);
    close(playerSocket);
}

int main() {
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return 1;
    }

    int opt = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        std::cerr << "setsockopt failed" << std::endl;
        return 1;
    }

    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(SERVER_PORT);

    bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    listen(serverSocket, 2);

    TronGame game;
    std::vector<std::thread> clientThreads;
    std::cout << "等待玩家连接..." << std::endl;

    while (true) { 
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(serverSocket, &readfds);
        
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 1000;
        
        if (select(serverSocket + 1, &readfds, NULL, NULL, &timeout) > 0) {
            int clientSocket = accept(serverSocket, nullptr, nullptr);
            if (clientSocket >= 0) {
                game.addPlayer(clientSocket);
                int assignedIndex = -1;
                for (size_t i = 0; i < game.getPlayerCount(); i++) {
                    if (game.getPlayerSocket(i) == clientSocket) {
                        assignedIndex = i;
                        break;
                    }
                }
                if (assignedIndex >= 0) {
                    clientThreads.emplace_back(handleClient, std::ref(game), assignedIndex);
                }
            }
        }
        
        game.updateGame();
        
        usleep(GAME_SPEED_MS * 1000);
    }

    for (auto& thread : clientThreads) {
        thread.join();
    }

    close(serverSocket);
    return 0;
}