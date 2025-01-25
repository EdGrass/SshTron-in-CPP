#include <map>          
#include <string>       
#include <cstring>      
#include <thread>       
#include <vector>       
#include <atomic>       
#include <codecvt>      
#include <sstream>      
#include <fcntl.h>      
#include <iostream>     
#include <unistd.h>     
#include <locale.h>     
#include <termios.h>    
#include <arpa/inet.h>  
#include <sys/socket.h> 
#include <netinet/in.h> 
#include "config.h"     

#ifdef DEBUG_MODE
#define DEBUG_LOG(msg, ...) printf("[DEBUG] " msg "\n", ##__VA_ARGS__)
#else
#define DEBUG_LOG(msg, ...)
#endif

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
    result.reserve(len * 4);
    
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
    
    return result;
}

std::string addBorder(const std::string& boardStr) {
    std::string result;
    std::vector<std::string> rows;
    std::string row;
    std::stringstream ss(boardStr);
    std::string line;

    if (std::getline(ss, line)) {
        result += COLOR_WHITE;
        result += wstrToStr(WALL_TOP_LEFT);
        
        std::string scoreInfo = line;
        size_t startEsc = scoreInfo.find("\033[");
        while (startEsc != std::string::npos) {
            size_t endEsc = scoreInfo.find('m', startEsc);
            if (endEsc != std::string::npos) {
                scoreInfo.erase(startEsc, endEsc - startEsc + 1);
            }
            startEsc = scoreInfo.find("\033[");
        }

        while (!scoreInfo.empty() && (scoreInfo.back() == '\n' || scoreInfo.back() == '\r')) {
            scoreInfo.pop_back();
        }

        if (scoreInfo.length() > BOARD_WIDTH) {
            scoreInfo = scoreInfo.substr(0, BOARD_WIDTH);
        }

        result += scoreInfo;
        
        int padding = BOARD_WIDTH - scoreInfo.length();
        if (padding > 0) {
            for (int i = 0; i < padding; i++) {
                result += wstrToStr(WALL_HORIZONTAL);
            }
        }
        
        result += wstrToStr(WALL_TOP_RIGHT);
        result += COLOR_RESET + "\n";
    }

    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        if (line.find(GAME_FOOTER) != std::string::npos) {
            rows.push_back(line);
            continue;
        }
        rows.push_back(line);
    }

    for (size_t i = 0; i < BOARD_HEIGHT; i++) {
        result += COLOR_WHITE + wstrToStr(WALL_VERTICAL) + COLOR_RESET;
        if (i < rows.size()) {
            result += rows[i];
            int lineLength = 0;
            size_t pos = 0;
            while (pos < rows[i].length()) {
                if (rows[i][pos] == '\033') {
                    while (pos < rows[i].length() && rows[i][pos] != 'm') pos++;
                    pos++;
                    continue;
                }
                lineLength++;
                pos++;
            }
            if (lineLength < BOARD_WIDTH) {
                result.append(BOARD_WIDTH - lineLength, ' ');
            }
        } else {
            result.append(BOARD_WIDTH, ' ');
        }
        result += COLOR_WHITE + wstrToStr(WALL_VERTICAL) + COLOR_RESET + "\n";
    }

    result += COLOR_WHITE;
    result += wstrToStr(WALL_BOTTOM_LEFT);
    for (size_t i = 0; i < BOARD_WIDTH; i++) {
        result += wstrToStr(WALL_HORIZONTAL);
    }
    result += wstrToStr(WALL_BOTTOM_RIGHT);
    result += COLOR_RESET + "\n";

    if (!rows.empty() && rows.back().find(GAME_FOOTER) != std::string::npos) {
        result += "\n" + rows.back();
    }

    return result;
}

struct PlayerState {
    int playerIndex;   
    int colorIndex;    
    int score;         
    int highScore;     
    bool alive;        
};

class GameDisplay {
private:
    std::vector<PlayerState> players;        						
    std::map<int, std::tuple<int, int, int, int>> playerPositions;  
    std::vector<std::vector<int>> board;     						
    const std::string playerColors[4] = {PLAYER_COLORS};  			
    int myPlayerIndex = -1;                  						
    int myColorIndex = -1;                   						
    int socket;                              						

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
        
        return wstrToStr(TRAIL_HORIZONTAL);  
    }

    std::string getDirectionSymbol(int dx, int dy) {
        if (dy < 0) return wstrToStr(PLAYER_UP);      
        if (dy > 0) return wstrToStr(PLAYER_DOWN);    
        if (dx < 0) return wstrToStr(PLAYER_LEFT);    
        if (dx > 0) return wstrToStr(PLAYER_RIGHT);   
        return wstrToStr(PLAYER_RIGHT);  				
    }

    bool isPlayerHead(int x, int y, int colorIndex) {
        auto it = playerPositions.find(colorIndex);
        if (it != playerPositions.end()) {
            auto [px, py, _, __] = it->second;
            return (x == px && y == py);
        }
        return false;
    }

public:
    GameDisplay(int sock) : 
        players(),
        playerPositions(),
        board(BOARD_HEIGHT, std::vector<int>(BOARD_WIDTH, 0)),
        socket(sock) {}

    void setMyIndices(int pIndex, int cIndex) {
        myPlayerIndex = pIndex;
        myColorIndex = cIndex;

        DEBUG_LOG("Set indices - player: %d, color: %d", pIndex, cIndex);
    }

    void updateState(const std::string& stateStr) {
        try {
            players.clear();
            playerPositions.clear();
            board = std::vector<std::vector<int>>(BOARD_HEIGHT, std::vector<int>(BOARD_WIDTH, 0));
            
            std::stringstream ss(stateStr);
            std::string line;
            bool foundPlayers = false;
            
            while (std::getline(ss, line)) {
                if (line == "PLAYERS") {
                    foundPlayers = true;
                    break;
                }
            }
            
            if (!foundPlayers) return;

            while (std::getline(ss, line) && line != "BOARD") {
                if (line.empty()) continue;
                
                std::stringstream playerStream(line);
                std::string colorStr;
                std::getline(playerStream, colorStr, ':');
                
                int colorIndex = std::stoi(colorStr);
                
                std::string data;
                std::getline(playerStream, data);
                std::stringstream dataStream(data);
                std::string value;
                std::vector<int> values;
                
                while (std::getline(dataStream, value, ',')) {
                    if (!value.empty()) {
                        values.push_back(std::stoi(value));
                    }
                }
                
                if (values.size() >= 8) {
                    PlayerState p;
                    p.colorIndex = colorIndex;  
                    p.playerIndex = values[0];
                    p.score = values[1];
                    p.highScore = values[2];
                    p.alive = values[3] != 0;
                    
                    players.push_back(p);
                    
                    playerPositions[colorIndex] = std::make_tuple(
                        values[4], values[5],  
                        values[6], values[7]   
                    );
                }
            }

            int row = 0;
            while (std::getline(ss, line) && line != "END" && row < BOARD_HEIGHT) {
                if (line.empty()) continue;
                
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
        } catch (const std::exception& e) {
            std::cerr << "Error updating state: " << e.what() << std::endl;
        }
    }

    std::string render() {
        std::string display;
        char scoreBuffer[100]; 
        const PlayerState* currentPlayer = nullptr;
        int maxScore = 0;
        
        for (const auto& player : players) {
            if (player.highScore > maxScore) {
                maxScore = player.highScore;
            }
            if (player.colorIndex == myColorIndex) {
                currentPlayer = &player;
            }
        }

        if (currentPlayer) {
            snprintf(scoreBuffer, sizeof(scoreBuffer), GAME_HEADER_FORMAT,
                    currentPlayer->score, currentPlayer->highScore, maxScore);
            display = std::string(scoreBuffer) + "\n";  
        }

        for (size_t y = 0; y < board.size(); ++y) {
            for (size_t x = 0; x < board[y].size(); ++x) {
                int colorIndex = board[y][x] - 1; 
                if (colorIndex >= 0) {
                    if (colorIndex < MAX_PLAYERS) {
                        display += playerColors[colorIndex];
                        if (isPlayerHead(x, y, colorIndex)) {
                            auto it = playerPositions.find(colorIndex);
                            if (it != playerPositions.end()) {
                                auto [_, __, dx, dy] = it->second;
                                display += getDirectionSymbol(dx, dy);
                            }
                        } else {
                            display += getTrailSymbol(colorIndex + 1, x, y);
                        }
                        display += COLOR_RESET;
                    }
                } else {
                    display += " ";
                }
            }
            display += "\n";
        }

        display += "\n" + std::string(GAME_FOOTER);
        
        return display;
    }

    void handleInput(char input) {
        send(socket, &input, 1, 0);
    }
}; 

void receiveGameState(int sock) {
    char buffer[BUFFER_SIZE];
    GameDisplay display(sock);  
    std::string accumulatedData;
    time_t lastHeartbeat = time(nullptr);
    
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    
    try {
        while (true) {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(sock, &readfds);
            
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 100000;  
            
            int selectResult = select(sock + 1, &readfds, NULL, NULL, &tv);
            
            if (selectResult < 0) {
                std::cerr << "Select error: " << strerror(errno) << std::endl;
                break;
            }
            
            time_t now = time(nullptr);
            
            if (now - lastHeartbeat >= HEARTBEAT_INTERVAL) {
                char heartbeat = 'h';
                send(sock, &heartbeat, 1, 0);
                lastHeartbeat = now;
            }
            
            if (selectResult > 0 && FD_ISSET(sock, &readfds)) {
                memset(buffer, 0, BUFFER_SIZE);
                int bytesRead = recv(sock, buffer, BUFFER_SIZE - 1, 0);
                
                if (bytesRead <= 0) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        throw std::runtime_error("Connection lost");
                    }
                    continue;
                }
                
                #ifdef DEBUG_MODE
                std::cout << "Received " << bytesRead << " bytes" << std::endl;
                #endif
                
                std::string data(buffer, bytesRead);
                
                if (data.find("INDEX:") == 0) {
                    size_t comma = data.find(",", 6);
                    if (comma != std::string::npos) {
                        int playerIndex = std::stoi(data.substr(6, comma - 6));
                        int colorIndex = std::stoi(data.substr(comma + 1));
                        display.setMyIndices(playerIndex, colorIndex);
                        
                        DEBUG_LOG("Received indices - player:%d color:%d", playerIndex, colorIndex);
                    }
                    continue;
                }
                
                accumulatedData.append(data);
                
                size_t beginPos = 0;
                while ((beginPos = accumulatedData.find("BEGIN\n")) != std::string::npos) {
                    size_t endPos = accumulatedData.find("END\n", beginPos);
                    if (endPos == std::string::npos) break;
                    
                    endPos += 4; 
                    std::string statePacket = accumulatedData.substr(beginPos, endPos - beginPos);
                    
                    try {
                        display.updateState(statePacket);
                        clearScreen();
                        std::cout << wstrToStr(GAME_TITLE) << "\n\n";
                        std::cout << addBorder(display.render()) << std::flush;
                    } catch (const std::exception& e) {
                        std::cerr << "Error updating state: " << e.what() << std::endl;
                    }
                    
                    accumulatedData.erase(0, endPos);
                }
                
                if (accumulatedData.length() > MAX_DATA_BUFFER) {
                    accumulatedData.clear();
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error in receiveGameState: " << e.what() << std::endl;
    }
}

int main() {
    setlocale(LC_ALL, "");
    std::cout << "\033[?25l";  

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);

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

    shutdown(sock, SHUT_RDWR); 
    close(sock);
    receiveThread.join();      
    
    std::cout << "\033[?25h";  
    return 0;
}