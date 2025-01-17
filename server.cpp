#include <iostream>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <thread>
#include <mutex>
#include <fstream>
#include <map>
#include <locale>
#include <codecvt>
#include <random>
#include <sys/select.h>
#include "config.h"
#include <fcntl.h>   // 为 fcntl, F_GETFL, F_SETFL
#include <errno.h>   // 为 errno

struct Player {
    int x, y;
    int dx, dy;
    bool alive;
    int socket;
    int playerIndex;  // 玩家编号，用于客户端区分显示
    int score;
    int highScore;
    time_t lastScoreTime;
};

class TronGame {
private:
    std::vector<std::vector<int>> board;  // 使用整数存储状态：0=空，>0=玩家编号
    std::vector<Player> players;
    std::mutex gameMutex;
    bool gameRunning;
    std::map<int, int> highScores;

    // 检查位置是否安全
    bool isSafePosition(int x, int y) {
        // 检查周围是否有其他玩家或轨迹
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

    // 生成随机安全位置
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
        throw std::runtime_error("无法找到安全的初始位置");
    }

    // 获取随机方向
    std::pair<int, int> getRandomDirection() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 3);
        int dir = dis(gen);
        switch (dir) {
            case 0: return {0, -1};  // 上
            case 1: return {0, 1};   // 下
            case 2: return {-1, 0};  // 左
            default: return {1, 0};  // 右
        }
    }

    void loadHighScores() {
        std::ifstream file(HIGH_SCORE_FILE);
        int socket, score;
        while (file >> socket >> score) {
            highScores[socket] = score;
        }
    }

    void saveHighScores() {
        std::ofstream file(HIGH_SCORE_FILE);
        for (const auto& score : highScores) {
            file << score.first << " " << score.second << std::endl;
        }
    }

    void updatePlayerScore(Player& player) {
        if (!player.alive) return;

        time_t now = time(nullptr);
        int timeDiff = static_cast<int>(now - player.lastScoreTime);
        
        #ifdef DEBUG_MODE
        std::cout << "Updating score for player " << player.playerIndex + 1 
                  << " time diff: " << timeDiff 
                  << " last score time: " << player.lastScoreTime 
                  << " current time: " << now << std::endl;
        #endif

        if (timeDiff > 0) {
            int scoreIncrease = timeDiff * SCORE_SURVIVAL_TIME;
            player.score += scoreIncrease;
            player.lastScoreTime = now;

            #ifdef DEBUG_MODE
            std::cout << "Player " << player.playerIndex + 1 
                      << " score increased by " << scoreIncrease 
                      << " new score: " << player.score << std::endl;
            #endif
        }
    }

    void resetGame() {
        std::lock_guard<std::mutex> lock(gameMutex);
        std::cout << "Resetting game..." << std::endl;

        // 保存最高分
        for (const auto& player : players) {
            if (player.score > highScores[player.socket]) {
                highScores[player.socket] = player.score;
                saveHighScores();
            }
        }

        // 清空游戏板
        board = std::vector<std::vector<int>>(BOARD_HEIGHT, 
                std::vector<int>(BOARD_WIDTH, 0));

        // 重置玩家状态
        for (auto& player : players) {
            try {
                auto [x, y] = getRandomSafePosition();
                auto [dx, dy] = getRandomDirection();
                player.x = x;
                player.y = y;
                player.dx = dx;
                player.dy = dy;
                player.alive = true;
                player.score = 0;  // 重置分数
                player.lastScoreTime = time(nullptr);
                board[y][x] = player.playerIndex + 1;
            } catch (const std::runtime_error& e) {
                std::cerr << "Error resetting player " << player.playerIndex + 1 
                         << ": " << e.what() << std::endl;
            }
        }
    }

    bool checkCollision(int x, int y, const Player& player, Player** killer) {
        // 检查边界碰撞
        if (x < 0 || x >= BOARD_WIDTH || y < 0 || y >= BOARD_HEIGHT) {
            return true;
        }
        
        // 检查与其他玩家或轨迹的碰撞
        if (board[y][x] != 0) {
            // 找出击杀者
            int killerIndex = board[y][x] - 1;  // 转换为玩家索引
            if (killerIndex >= 0 && killerIndex < players.size() && 
                killerIndex != player.playerIndex) {
                *killer = &players[killerIndex];
            }
            return true;
        }
        
        return false;
    }

    // 修改序列化格式，添加校验和同步标记
    std::string serializeGameState() {
        std::string state;
        state += "BEGIN\n";  // 添加开始标记
        
        // 添加游戏状态
        state += "STATUS:" + std::to_string(gameRunning) + "\n";
        
        // 添加玩家信息
        state += "PLAYERS\n";
        for (size_t i = 0; i < players.size(); ++i) {
            const auto& player = players[i];
            state += std::to_string(i) + ":" +  // 添加玩家索引标识符
                    std::to_string(player.playerIndex) + "," +
                    std::to_string(player.score) + "," +
                    std::to_string(player.highScore) + "," +
                    std::to_string(player.alive) + "," +
                    std::to_string(player.x) + "," +
                    std::to_string(player.y) + "," +
                    std::to_string(player.dx) + "," +
                    std::to_string(player.dy) + "\n";
            
            #ifdef DEBUG_MODE
            std::cout << "序列化玩家 " << player.playerIndex 
                     << " 位置:(" << player.x << "," << player.y 
                     << ") 方向:(" << player.dx << "," << player.dy << ")" << std::endl;
            #endif
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

    // 添加新方法：复活单个玩家
    void respawnPlayer(Player& player) {
        try {
            auto [x, y] = getRandomSafePosition();
            auto [dx, dy] = getRandomDirection();
            
            clearPlayerTrail(player.playerIndex);
            
            player.x = x;
            player.y = y;
            player.dx = dx;
            player.dy = dy;
            player.alive = true;
            player.score = 0;  // 确保复活时分数从0开始
            player.lastScoreTime = time(nullptr);  // 重置计时器
            board[y][x] = player.playerIndex + 1;

            #ifdef DEBUG_MODE
            std::cout << "Player " << player.playerIndex + 1 
                      << " respawned at position (" << x << "," << y 
                      << ") with reset score" << std::endl;
            #endif
        } catch (const std::runtime_error& e) {
            std::cerr << "Error respawning player " << player.playerIndex + 1 
                      << ": " << e.what() << std::endl;
        }
    }

    // 修改：只清理单个玩家的轨迹
    void clearPlayerTrail(int playerIndex) {
        for (auto& row : board) {
            for (auto& cell : row) {
                if (cell == playerIndex + 1) {  // +1是因为玩家索引从0开始，但显示从1开始
                    cell = 0;
                }
            }
        }
    }

    // 新增：单个玩家死亡处理
    void handlePlayerDeath(Player& player, Player* killer, const std::string& cause) {
        if (!player.alive) return;

        // 先计算当前分数以确保包含所有存活时间
        updatePlayerScore(player);
        int finalScore = player.score;

        // 更新最高分记录
        if (finalScore > player.highScore) {
            player.highScore = finalScore;
            highScores[player.socket] = finalScore;
            saveHighScores();
        }

        // 处理击杀奖励
        if (killer != nullptr && killer != &player && killer->alive) {
            // 基础击杀分数 + 被击杀者分数的一定比例
            int scoreTransfer = SCORE_KILL_POINTS + 
                              static_cast<int>(finalScore * SCORE_TRANSFER_RATE);
            killer->score += scoreTransfer;
            
            std::cout << "Player " << killer->playerIndex + 1 
                     << " killed Player " << player.playerIndex + 1 
                     << " [得分:" << scoreTransfer << " = " 
                     << SCORE_KILL_POINTS << " + " 
                     << static_cast<int>(finalScore * SCORE_TRANSFER_RATE) 
                     << "(" << (SCORE_TRANSFER_RATE * 100) << "% of " << finalScore 
                     << ")]" << std::endl;
        } else {
            std::cout << "Player " << player.playerIndex + 1 
                     << " died by " << cause 
                     << " with score " << finalScore << std::endl;
        }

        // 标记玩家死亡并重置分数
        player.alive = false;
        player.score = 0;
        player.lastScoreTime = time(nullptr);
        
        // 清理轨迹
        clearPlayerTrail(player.playerIndex);

        // 立即向所有玩家发送更新后的游戏状态
        std::string state = serializeGameState();
        for (const auto& p : players) {
            send(p.socket, state.c_str(), state.length(), 0);
        }
    }

public:
    TronGame() {
        board = std::vector<std::vector<int>>(BOARD_HEIGHT, 
                std::vector<int>(BOARD_WIDTH, 0));
        gameRunning = true;
        loadHighScores();
    }

    void addPlayer(int socket) {
        std::lock_guard<std::mutex> lock(gameMutex);
        if (players.size() < MAX_PLAYERS) {
            try {
                auto [x, y] = getRandomSafePosition();
                auto [dx, dy] = getRandomDirection();
                
                int playerIndex = players.size();
                Player p = {x, y, dx, dy, true, socket, playerIndex,
                          0, highScores[socket], time(nullptr)};
                players.push_back(p);
                board[p.y][p.x] = playerIndex + 1;
                
                // 发送玩家索引信息
                std::string indexMsg = "INDEX:" + std::to_string(playerIndex) + "\n";
                send(socket, indexMsg.c_str(), indexMsg.length(), 0);
                
                // 发送欢迎消息
                std::string welcomeMsg = "Welcome to TRON!\n";
                send(socket, welcomeMsg.c_str(), welcomeMsg.length(), 0);
                
                // 发送初始游戏状态
                std::string state = serializeGameState();
                if (send(socket, state.c_str(), state.length(), 0) <= 0) {
                    std::cerr << "Failed to send initial state to player" << std::endl;
                    close(socket);
                    return;
                }
                
                std::cout << "Player " << playerIndex + 1 << " joined the game" << std::endl;
            } catch (const std::runtime_error& e) {
                std::cerr << "无法添加新玩家: " << e.what() << std::endl;
                close(socket);
            }
        }
    }

    void handleInput(int playerIndex, char input) {
        std::lock_guard<std::mutex> lock(gameMutex);
        if (playerIndex < 0 || playerIndex >= players.size()) return;
        
        Player& player = players[playerIndex];
        if (!player.alive) return;
        
        #ifdef DEBUG_MODE
        std::cout << "Received input from player " << playerIndex + 1 << ": " << input << std::endl;
        #endif
        
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
            #ifdef DEBUG_MODE
            std::cout << "Player " << playerIndex + 1 
                      << " direction changed to: (" << player.dx << "," << player.dy << ")" << std::endl;
            #endif
        }
    }

    bool isGameOver() {
        int alive = 0;
        for (const auto& player : players) {
            if (player.alive) alive++;
        }
        return alive <= 1;
    }
	
    // 添加新的公共方法
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
        if (playerIndex >= 0 && playerIndex < players.size()) {
            // 清理玩家轨迹
            for (auto& row : board) {
                for (auto& cell : row) {
                    if (cell == players[playerIndex].playerIndex) {
                        cell = 0;
                    }
                }
            }
            players.erase(players.begin() + playerIndex);
        }
    }

    // 将updateGame移动到public部分
    void updateGame() {
        if (!gameRunning) return;
        std::lock_guard<std::mutex> lock(gameMutex);

        static std::map<int, time_t> deathTimes;
        bool stateChanged = false;

        // 更新每个存活玩家的分数
        for (auto& player : players) {
            if (player.alive) {
                updatePlayerScore(player);
            }
        }

        // 更新所有玩家的位置
        for (auto& player : players) {
            if (player.alive) {
                int newX = player.x + player.dx;
                int newY = player.y + player.dy;

                #ifdef DEBUG_MODE
                std::cout << "Moving player " << player.playerIndex + 1 
                          << " from (" << player.x << "," << player.y 
                          << ") to (" << newX << "," << newY << ")" << std::endl;
                #endif

                Player* killer = nullptr;
                bool willCollide = checkCollision(newX, newY, player, &killer);
                board[player.y][player.x] = player.playerIndex + 1;

                if (willCollide) {
                    handlePlayerDeath(player, killer, killer ? "被击杀" : "碰撞");
                    stateChanged = true;
                    continue;
                }

                player.x = newX;
                player.y = newY;
                board[player.y][player.x] = player.playerIndex + 1;
                stateChanged = true;
            } else {
                // 处理死亡玩家复活
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

        // 如果游戏状态发生变化，发送更新
        if (stateChanged) {
            std::string state = serializeGameState();
            std::vector<int> failedPlayers;
            
            #ifdef DEBUG_MODE
            std::cout << "Game state updated. Active players: ";
            for (const auto& player : players) {
                std::cout << "Player " << player.playerIndex + 1 
                          << "(" << (player.alive ? "alive" : "dead") 
                          << " at " << player.x << "," << player.y 
                          << " moving " << player.dx << "," << player.dy 
                          << ") ";
            }
            std::cout << std::endl;
            #endif

            // 发送状态给所有玩家
            for (const auto& player : players) {
                if (send(player.socket, state.c_str(), state.length(), 0) < 0) {
                    failedPlayers.push_back(player.playerIndex);
                }
            }
            
            // 清理断开连接的玩家
            for (int index : failedPlayers) {
                removePlayer(index);
            }
        }
    }

    // 如果需要，可以添加一个公共方法来重置游戏
    void resetGameState() {
        std::lock_guard<std::mutex> lock(gameMutex);
        resetGame();
    }
}; // 添加分号

void handleClient(TronGame& game, int playerIndex) {
    char buffer[BUFFER_SIZE];
    int playerSocket = game.getPlayerSocket(playerIndex);
    time_t lastHeartbeat = time(nullptr);
    bool connectionAlive = true;
    
    // 设置非阻塞模式
    #ifdef __APPLE__
        int flags = fcntl(playerSocket, F_GETFL, 0);
        fcntl(playerSocket, F_SETFL, flags | O_NONBLOCK);
    #else
        // Linux 和其他系统
        int flags = 1;
        ioctl(playerSocket, FIONBIO, &flags);
    #endif
    
    while (connectionAlive) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(playerSocket, &readfds);
        
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;  // 100ms超时
        
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
            
            // 处理心跳包
            if (buffer[0] == 'h') {
                #ifdef DEBUG_MODE
                std::cout << "Heartbeat received from player " << playerIndex + 1 << std::endl;
                #endif
                continue;
            }
            
            game.handleInput(playerIndex, buffer[0]);
        }
    }
    
    std::cout << "Player " << playerIndex + 1 << " disconnected" << std::endl;
    game.removePlayer(playerIndex);
    close(playerSocket);
}

int main() {
    // 添加服务器socket初始化
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return 1;
    }

    // 设置socket选项
    int opt = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        std::cerr << "setsockopt failed" << std::endl;
        return 1;
    }

    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(SERVER_PORT);  // 使用SERVER_PORT而不是PORT

    bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    listen(serverSocket, 2);

    TronGame game;
    std::vector<std::thread> clientThreads;

    std::cout << "等待玩家连接..." << std::endl;

    // 修改主循环，允许随时加入
    while (true) {
        // 接受新连接
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(serverSocket, &readfds);
        
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 1000;  // 1ms超时
        
        if (select(serverSocket + 1, &readfds, NULL, NULL, &timeout) > 0) {
            int clientSocket = accept(serverSocket, nullptr, nullptr);
            if (clientSocket >= 0) {
                game.addPlayer(clientSocket);
                clientThreads.emplace_back(handleClient, std::ref(game), game.getPlayerCount() - 1);
            }
        }
        
        // 更新游戏状态
        game.updateGame();
        
        usleep(GAME_SPEED_MS * 1000);
    }

    for (auto& thread : clientThreads) {
        thread.join();
        
    }

    close(serverSocket);
    return 0;
}