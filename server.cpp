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

// 在文件开头添加调试输出宏定义
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
    int playerIndex;  // 玩家编号，用于客户端区分显示
    int score;
    int highScore;
    time_t lastScoreTime;
    int colorIndex;  // 添加颜色索引
};

class TronGame {
private:
    std::vector<std::vector<int>> board;  // 使用整数存储状态：0=空，>0=玩家编号
    std::vector<Player> players;
    std::mutex gameMutex;
    bool gameRunning;
    std::map<int, int> highScores;
    std::vector<bool> usedPlayerIndices;  // 新增：跟踪已使用的玩家索引
    std::vector<bool> usedColorIndices;  // 跟踪已使用的颜色索引
    std::map<int, int> socketToHighScores;  // 新增：存储每个socket的最高分

    struct PlayerScore {
        int current;
        int high;
        int colorIndex;  // 添加颜色索引关联
    };
    std::map<int, PlayerScore> playerScores;  // socket -> PlayerScore

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
            socketToHighScores[socket] = score;
        }
    }

    void saveHighScores() {
        std::ofstream file(HIGH_SCORE_FILE);
        for (const auto& [socket, score] : playerScores) {
            file << socket << " " << score.high << " " << score.colorIndex << std::endl;
        }
    }

    void updatePlayerScore(Player& player) {
        if (!player.alive) return;

        time_t now = time(nullptr);
        int timeDiff = static_cast<int>(now - player.lastScoreTime);
        
        DEBUG_LOG("Updating score for player %d time diff: %d last score time: %ld current time: %ld", 
                  player.playerIndex + 1, timeDiff, player.lastScoreTime, now);

        if (timeDiff > 0) {
            int scoreIncrease = timeDiff * SCORE_SURVIVAL_TIME;
            player.score += scoreIncrease;
            player.lastScoreTime = now;

            DEBUG_LOG("Player %d score increased by %d new score: %d", 
                      player.playerIndex + 1, scoreIncrease, player.score);
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
                board[y][x] = player.colorIndex + 1;  // 使用颜色索引而不是玩家索引
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
            int killerColorIndex = board[y][x] - 1;  // 获取颜色索引
            if (killerColorIndex == player.colorIndex) {
                return false;  // 不与自己的轨迹碰撞
            }
            // 根据颜色索引查找击杀者
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

    // 修改序列化格式，添加颜色索引信息
    std::string serializeGameState() {
        std::string state;
        state += "BEGIN\n";  // 添加开始标记
        
        // 添加游戏状态
        state += "STATUS:" + std::to_string(gameRunning) + "\n";
        
        // 添加玩家信息
        state += "PLAYERS\n";
        for (const auto& player : players) {
            state += std::to_string(player.colorIndex) + ":" +  // 使用颜色索引作为主键
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

    // 添加新方法：复活单个玩家
    void respawnPlayer(Player& player) {
        try {
            auto [x, y] = getRandomSafePosition();
            auto [dx, dy] = getRandomDirection();
            
            clearPlayerTrail(player.colorIndex);  // 修改：使用颜色索引
            
            player.x = x;
            player.y = y;
            player.dx = dx;
            player.dy = dy;
            player.alive = true;
            player.score = 0;  // 确保复活时分数从0开始
            board[y][x] = player.colorIndex + 1;  // 使用颜色索引

            DEBUG_LOG("Player %d (color: %d) respawned at position (%d,%d)", 
                      player.playerIndex + 1, player.colorIndex + 1, x, y);
        } catch (const std::runtime_error& e) {
            std::cerr << "Error respawning player " << player.playerIndex + 1 
                      << ": " << e.what() << std::endl;
        }
    }

    // 修改：只清理单个玩家的轨迹
    void clearPlayerTrail(int colorIndex) {  // 修改：使用颜色索引而不是玩家索引
        for (auto& row : board) {
            for (auto& cell : row) {
                if (cell == colorIndex + 1) {  // 使用颜色索引
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
        
        // 清理轨迹时使用颜色索引
        clearPlayerTrail(player.colorIndex);

        // 立即向所有玩家发送更新后的游戏状态
        std::string state = serializeGameState();
        for (const auto& p : players) {
            send(p.socket, state.c_str(), state.length(), 0);
        }
    }

    // 新增：找到最小可用的玩家索引
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

    // 新增：找到可用的颜色索引
    int findAvailableColorIndex() {
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!usedColorIndices[i]) {
                usedColorIndices[i] = true;  // 标记为已使用
                return i;
            }
        }
        return -1;
    }

    // 新增：占用一个颜色索引
    void occupyColorIndex(int colorIndex) {
        if (colorIndex >= 0 && colorIndex < MAX_PLAYERS) {
            usedColorIndices[colorIndex] = true;
        }
    }

    // 修改：在现有代码中添加日志
    void debugPrintState() {
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
            // 如果是重连的玩家，重置当前分数但保留最高分
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
        usedColorIndices = std::vector<bool>(MAX_PLAYERS, false);  // 初始化颜色使用状态
    }

    void addPlayer(int socket) {
        std::lock_guard<std::mutex> lock(gameMutex);
        if (players.size() < MAX_PLAYERS) {
            try {
                auto [x, y] = getRandomSafePosition();
                auto [dx, dy] = getRandomDirection();
                
                // 查找最小可用的颜色索引
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

                // 打印调试信息
                DEBUG_LOG("Adding new player - socket:%d playerIndex:%d colorIndex:%d", 
                         socket, playerIndex, colorIndex);

                // 发送索引信息
                std::string indexMsg = "INDEX:" + std::to_string(playerIndex) + 
                                     "," + std::to_string(colorIndex) + "\n";
                send(socket, indexMsg.c_str(), indexMsg.length(), 0);

                players.push_back(p);
                board[y][x] = colorIndex + 1;

                initializeNewPlayer(p);  // 初始化新玩家的分数

                debugPrintState();

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
                DEBUG_LOG("Failed to add new player: %s", e.what());
                close(socket);
            }
        }
    }

    void handleInput(int colorIndex, char input) {  // 修改参数为 colorIndex
        std::lock_guard<std::mutex> lock(gameMutex);
        
        // 使用颜色索引查找玩家
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
        auto it = std::find_if(players.begin(), players.end(),
            [playerIndex](const Player& p) { return p.playerIndex == playerIndex; });
        
        if (it != players.end()) {
            DEBUG_LOG("Removing player - index:%d color:%d", 
                     playerIndex, it->colorIndex);

            // 释放颜色索引
            usedColorIndices[it->colorIndex] = false;
            
            // 保存分数
            socketToHighScores[it->socket] = std::max(
                socketToHighScores[it->socket], 
                it->score
            );
            saveHighScores();

            clearPlayerTrail(it->colorIndex);
            players.erase(it);

            debugPrintState();

            // 立即发送更新后的游戏状态给所有剩余玩家
            std::string state = serializeGameState();
            std::vector<int> failedPlayers;
            
            for (const auto& p : players) {
                if (send(p.socket, state.c_str(), state.length(), 0) < 0) {
                    failedPlayers.push_back(p.playerIndex);
                }
            }
            
            // 处理发送失败的玩家
            for (int index : failedPlayers) {
                removePlayer(index);
            }
        }
    }

    // 将updateGame移动到public部分
    void updateGame() {
        if (!gameRunning) return;
        std::lock_guard<std::mutex> lock(gameMutex);

        // 首先检查并清理已断开连接的玩家
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

                DEBUG_LOG("Moving player %d from (%d,%d) to (%d,%d)", 
                          player.playerIndex + 1, player.x, player.y, newX, newY);

                Player* killer = nullptr;
                bool willCollide = checkCollision(newX, newY, player, &killer);

                // 在移动前标记当前位置为玩家的颜色
                board[player.y][player.x] = player.colorIndex + 1;

                if (willCollide) {
                    handlePlayerDeath(player, killer, killer ? "被击杀" : "碰撞");
                    stateChanged = true;
                    continue;
                }

                // 更新玩家位置并在新位置标记颜色
                player.x = newX;
                player.y = newY;
                board[player.y][player.x] = player.colorIndex + 1;
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
            
            DEBUG_LOG("Game state updated. Active players: ");
            for (const auto& player : players) {
                DEBUG_LOG("Player %d(%s at %d,%d moving %d,%d) ", 
                          player.playerIndex + 1, player.alive ? "alive" : "dead", 
                          player.x, player.y, player.dx, player.dy);
            }
            DEBUG_LOG("");

            // 发送状态给所有玩家
            for (const auto& player : players) {
                if (send(player.socket, state.c_str(), state.length(), 0) < 0) {
                    failedPlayers.push_back(player.playerIndex);  // 修复：使用 player 而不是 p
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

    // 添加新方法来获取玩家的实际索引
    int getPlayerIndexBySocket(int socket) {
        for (const auto& player : players) {
            if (player.socket == socket) {
                return player.playerIndex;
            }
        }
        return -1;
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
}; // 添加分号

void handleClient(TronGame& game, int playerIndex) {
    char buffer[BUFFER_SIZE];
    int playerSocket = game.getPlayerSocket(playerIndex);
    int colorIndex = game.getColorIndexBySocket(playerSocket);  // 获取颜色索引
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
    
    // 在连接开始时验证玩家索引
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
                DEBUG_LOG("Heartbeat received from player %d", playerIndex + 1);
                continue;
            }
            
            game.handleInput(colorIndex, buffer[0]);  // 使用颜色索引
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
                // 修改这里,使用玩家实际分配的索引而不是数量
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