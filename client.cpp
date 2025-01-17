#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <thread>
#include <termios.h>
#include "config.h"
#include <locale.h>
#include <vector>
#include <codecvt>
#include <sstream>
#include <map>
#include <atomic>
#include <fcntl.h>

void clearScreen() {
    std::cout << "\033[2J\033[H";
}

char getch() {
    char buf = 0;
    struct termios old = {0};
    if (tcgetattr(0, &old) < 0)
        perror("tcsetattr()");
    old.c_lflag &= ~ICANON;
    old.c_lflag &= ~ECHO;
    old.c_cc[VMIN] = 1;
    old.c_cc[VTIME] = 0;
    if (tcsetattr(0, TCSANOW, &old) < 0)
        perror("tcsetattr ICANON");
    if (read(0, &buf, 1) < 0)
        perror("read()");
    old.c_lflag |= ICANON;
    old.c_lflag |= ECHO;
    if (tcsetattr(0, TCSADRAIN, &old) < 0)
        perror("tcsetattr ~ICANON");
    return buf;
}

std::string wstrToStr(const wchar_t* wstr) {
    std::string result;
    size_t len = wcslen(wstr);
    result.reserve(len * 4); // 为 UTF-8 分配足够空间
    
    #ifdef __APPLE__
    for (size_t i = 0; i < len; ++i) {
        wchar_t wc = wstr[i];
        if (wc < 0x80) {
            result += static_cast<char>(wc);
        } else if (wc < 0x800) {
            result += static_cast<char>((wc >> 6) | 0xC0);
            result += static_cast<char>((wc & 0x3F) | 0x80);
        } else if (wc < 0x10000) {
            result += static_cast<char>((wc >> 12) | 0xE0);
            result += static_cast<char>(((wc >> 6) & 0x3F) | 0x80);
            result += static_cast<char>((wc & 0x3F) | 0x80);
        } else {
            result += static_cast<char>((wc >> 18) | 0xF0);
            result += static_cast<char>(((wc >> 12) & 0x3F) | 0x80);
            result += static_cast<char>(((wc >> 6) & 0x3F) | 0x80);
            result += static_cast<char>((wc & 0x3F) | 0x80);
        }
    }
    #else
    char mb[MB_CUR_MAX + 1];
    mbstate_t state = mbstate_t();
    for (size_t i = 0; i < len; ++i) {
        size_t ret = wcrtomb(mb, wstr[i], &state);
        if (ret != (size_t)-1) {
            mb[ret] = '\0';
            result += mb;
        }
    }
    #endif
    
    return result;
}

std::string addBorder(const std::string& boardStr) {
    std::string result;
    std::vector<std::string> rows;
    std::string row;
    
    // 分割接收到的游戏板字符串为行
    for (const char& c : boardStr) {
        if (c == '\n') {
            rows.push_back(row);
            row.clear();
        } else {
            row += c;
        }
    }
    if (!row.empty()) {
        rows.push_back(row);
    }

    // 提取分数板信息（前几行）
    std::string scoreBoard;
    while (!rows.empty() && rows[0].find("玩家") != std::string::npos) {
        scoreBoard += rows[0] + "\n";
        rows.erase(rows.begin());
    }
    
    // 移除空行
    while (!rows.empty() && rows[0].empty()) {
        rows.erase(rows.begin());
    }

    // 显示分数板
    result += scoreBoard + "\n";

    // 添加带颜色的边框
    result.append(COLOR_WHITE);
    result.append(wstrToStr(WALL_TOP_LEFT));
    for (size_t i = 0; i < BOARD_WIDTH; i++) {
        result.append(wstrToStr(WALL_HORIZONTAL));
    }
    result.append(wstrToStr(WALL_TOP_RIGHT));
    result.append("\n");

    // 添加游戏区域（带左右边框）
    for (const auto& row : rows) {
        result.append(COLOR_WHITE);
        result.append(wstrToStr(WALL_VERTICAL));
        result.append(COLOR_RESET);
        result.append(row);
        result.append(COLOR_WHITE);
        result.append(wstrToStr(WALL_VERTICAL));
        result.append(COLOR_RESET);
        result.append("\n");
    }

    // 添加底部边框
    result.append(COLOR_WHITE);
    result.append(wstrToStr(WALL_BOTTOM_LEFT));
    for (size_t i = 0; i < BOARD_WIDTH; i++) {
        result.append(wstrToStr(WALL_HORIZONTAL));
    }
    result.append(wstrToStr(WALL_BOTTOM_RIGHT));
    result.append(COLOR_RESET);
    result.append("\n");

    return result;
}

struct PlayerState {
    int index;
    int score;
    int highScore;
    bool alive;
};

class GameDisplay {
private:
    bool gameRunning;
    std::vector<PlayerState> players;
    std::map<int, std::tuple<int, int, int, int>> playerPositions;  // 新增：存储玩家位置和方向
    std::vector<std::vector<int>> board;
    const std::string playerColors[4] = {PLAYER_COLORS};
    
    std::string getTrailSymbol(int playerIndex, int x, int y) {
        bool up = (y > 0 && board[y-1][x] == playerIndex);
        bool down = (y < board.size()-1 && board[y+1][x] == playerIndex);
        bool left = (x > 0 && board[y][x-1] == playerIndex);
        bool right = (x < board[y].size()-1 && board[y][x+1] == playerIndex);
        
        if ((up || down) && !left && !right) return wstrToStr(TRAIL_VERTICAL);
        if (!up && !down && (left || right)) return wstrToStr(TRAIL_HORIZONTAL);
        if (up && right) return wstrToStr(TRAIL_CORNER_LEFT_DOWN);
        if (up && left) return wstrToStr(TRAIL_CORNER_RIGHT_DOWN);
        if (down && right) return wstrToStr(TRAIL_CORNER_LEFT_UP);
        if (down && left) return wstrToStr(TRAIL_CORNER_RIGHT_UP);
        
        return wstrToStr(TRAIL_HORIZONTAL);  // 默认
    }

    std::string getDirectionSymbol(int dx, int dy) {
        if (dy < 0) return wstrToStr(PLAYER_UP);
        if (dy > 0) return wstrToStr(PLAYER_DOWN);
        if (dx < 0) return wstrToStr(PLAYER_LEFT);
        if (dx > 0) return wstrToStr(PLAYER_RIGHT);
        return wstrToStr(PLAYER_DOWN);  // 默认朝下
    }

public:
    // 修复初始化列表的顺序，使其与成员声明顺序一致
    GameDisplay() : 
        gameRunning(false),
        players(),
        playerPositions(),
        board(BOARD_HEIGHT, std::vector<int>(BOARD_WIDTH, 0)) {}

    // 修复状态解析
    void updateState(const std::string& stateStr) {
        #ifdef DEBUG_MODE
        std::cout << "Received state length: " << stateStr.length() << std::endl;
        #endif

        std::stringstream ss(stateStr);
        std::string line;
        
        // 检查状态开始标记
        while (std::getline(ss, line) && line != "BEGIN");
        
        // 读取状态
        while (std::getline(ss, line) && line != "PLAYERS") {
            if (line.find("STATUS:") == 0) {
                gameRunning = (line[7] == '1');
            }
        }
        
        players.clear();
        playerPositions.clear();
        
        // 读取玩家信息
        while (std::getline(ss, line) && line != "BOARD") {
            if (line.empty()) continue;
            
            std::vector<int> values;
            std::stringstream ls(line);
            std::string value;
            
            while (std::getline(ls, value, ',')) {
                if (!value.empty()) {
                    values.push_back(std::stoi(value));
                }
            }
            
            if (values.size() >= 8) {
                PlayerState p{values[0], values[1], values[2], 
                            static_cast<bool>(values[3])};
                players.push_back(p);
                playerPositions[p.index] = std::make_tuple(
                    values[4], values[5], values[6], values[7]
                );
            }
        }

        // 清空并读取游戏板
        board = std::vector<std::vector<int>>(BOARD_HEIGHT, 
                std::vector<int>(BOARD_WIDTH, 0));
        int row = 0;
        
        while (std::getline(ss, line) && line != "END" && row < BOARD_HEIGHT) {
            std::stringstream ls(line);
            std::string value;
            int col = 0;
            
            while (std::getline(ls, value, ',') && col < BOARD_WIDTH) {
                if (!value.empty()) {
                    board[row][col] = std::stoi(value);
                }
                col++;
            }
            row++;
        }
    }

    std::string render() {
        std::string display;
        
        // 添加头部信息
        char header[100];
        PlayerState* currentPlayer = nullptr;
        int maxScore = 0;
        
        // 查找当前玩家和最高分
        for (const auto& player : players) {
            if (player.highScore > maxScore) {
                maxScore = player.highScore;
            }
            if (player.index == 1) { // 假设当前玩家始终是索引1
                currentPlayer = (PlayerState*)&player;
            }
        }

        if (currentPlayer) {
            snprintf(header, sizeof(header), GAME_HEADER_FORMAT,
                    currentPlayer->score, currentPlayer->highScore, maxScore);
        }
        
        display = header;
        display += "\n\n";

        // 渲染游戏区域
        for (size_t y = 0; y < board.size(); ++y) {
            for (size_t x = 0; x < board[y].size(); ++x) {
                int playerIndex = board[y][x];
                if (playerIndex > 0) {
                    display += playerColors[playerIndex - 1];
                    
                    // 检查是否是玩家当前位置
                    auto it = playerPositions.find(playerIndex);
                    if (it != playerPositions.end() && 
                        std::get<0>(it->second) == x && 
                        std::get<1>(it->second) == y) {
                        // 显示玩家方向
                        display += getDirectionSymbol(
                            std::get<2>(it->second),
                            std::get<3>(it->second)
                        );
                    } else {
                        display += getTrailSymbol(playerIndex, x, y);
                    }
                    display += COLOR_RESET;
                } else {
                    display += " ";
                }
            }
            display += "\n";
        }

        // 添加底部警告信息
        display += "\n" + std::string(GAME_FOOTER);
        
        return display;
    }
}; // 2. 添加缺失的分号

// 修改 receiveGameState 函数，添加调试输出
void receiveGameState(int sock) {
    char buffer[BUFFER_SIZE];
    GameDisplay display;
    std::string accumulatedData;
    time_t lastHeartbeat = time(nullptr);
    bool firstStateReceived = false;  // 添加标志来检查是否收到初始状态
    
    // 设置socket为非阻塞模式
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    
    while (true) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;  // 100ms超时
        
        int selectResult = select(sock + 1, &readfds, NULL, NULL, &tv);
        if (selectResult < 0) {
            std::cerr << "Select error: " << strerror(errno) << std::endl;
            break;
        }
        
        // 发送心跳包
        time_t now = time(nullptr);
        if (now - lastHeartbeat >= HEARTBEAT_INTERVAL) {
            char heartbeat = 'h';
            if (send(sock, &heartbeat, 1, 0) <= 0) {
                std::cerr << "Failed to send heartbeat" << std::endl;
                break;
            }
            lastHeartbeat = now;
            
            // 只在调试模式下显示心跳信息
            #ifdef DEBUG_MODE
            std::cout << "Heartbeat sent" << std::endl;
            #endif
        }
        
        if (selectResult > 0 && FD_ISSET(sock, &readfds)) {
            int bytesRead = recv(sock, buffer, sizeof(buffer) - 1, 0);
            if (bytesRead <= 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    std::cerr << "Connection lost (errno: " << errno << "): " 
                             << strerror(errno) << std::endl;
                    break;
                }
                continue;
            }
            
            buffer[bytesRead] = '\0';
            accumulatedData += buffer;
            
            #ifdef DEBUG_MODE
            if (!firstStateReceived) {
                std::cout << "Received initial data: " << bytesRead << " bytes" << std::endl;
                std::cout << "Data: " << accumulatedData.substr(0, 100) << "..." << std::endl;
            }
            #endif
            
            // 查找完整的状态包
            size_t beginPos = accumulatedData.find("BEGIN\n");
            size_t endPos = accumulatedData.find("END\n");
            
            while (beginPos != std::string::npos && endPos != std::string::npos && beginPos < endPos) {
                std::string state = accumulatedData.substr(beginPos, endPos - beginPos + 4);
                display.updateState(state);
                firstStateReceived = true;
                
                clearScreen();
                std::cout << wstrToStr(GAME_TITLE) << "\n";
                std::cout << "控制: " << KEY_UP << "(上) " << KEY_DOWN << "(下) " 
                         << KEY_LEFT << "(左) " << KEY_RIGHT << "(右)\n";
                std::cout << "目标: 避免撞击墙壁和对手的轨迹\n\n";
                std::cout << addBorder(display.render());
                
                accumulatedData = accumulatedData.substr(endPos + 4);
                beginPos = accumulatedData.find("BEGIN\n");
                endPos = accumulatedData.find("END\n");
            }
        }
        
        // 如果长时间没有收到初始状态，显示提示信息
        if (!firstStateReceived && time(nullptr) - lastHeartbeat > 5) {
            clearScreen();
            std::cout << "等待游戏状态...\n";
            std::cout << "如果长时间没有响应，请检查服务器是否正常运行。\n";
        }
    }
    
    std::cout << "服务器断开连接" << std::endl;
}

// 修复main函数的连接逻辑
int main() {
    // 设置locale以支持Unicode字符
    setlocale(LC_ALL, "");
    std::cout << "\033[?25l";  // 隐藏光标

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);

    // 添加连接重试逻辑
    int maxRetries = 3;
    int retryCount = 0;
    while (retryCount < maxRetries) {
        if (connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == 0) {
            break;
        }
        std::cerr << "连接失败，重试中... (" << retryCount + 1 << "/" << maxRetries << ")" << std::endl;
        sleep(1);
        retryCount++;
    }
    
    if (retryCount >= maxRetries) {
        std::cerr << "无法连接到服务器" << std::endl;
        close(sock);
        return 1;
    }

    std::cout << "已连接到服务器" << std::endl;

    std::atomic<bool> running{true};
    std::thread receiveThread([sock, &running]() {
        receiveGameState(sock);
        running = false;
    });

    while (running) {
        char input = getch();
        if (input == 'q' || input == 'Q') {
            running = false;
            break;
        }
        if (send(sock, &input, 1, 0) <= 0) {
            running = false;
            break;
        }
    }

    shutdown(sock, SHUT_RDWR);  // 正确关闭socket
    close(sock);
    receiveThread.join();  // 等待接收线程结束
    
    std::cout << "\033[?25h";  // 恢复光标
    return 0;
}
