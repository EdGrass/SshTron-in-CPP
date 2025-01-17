#ifndef TRON_CONFIG_H
#define TRON_CONFIG_H

// 游戏区域设置
#define BOARD_WIDTH 78
#define BOARD_HEIGHT 22

// 玩家设置
#define MAX_PLAYERS 4  // 增加最大玩家数
#define PLAYER_ONE_CHAR '1'
#define PLAYER_TWO_CHAR '2'
#define MIN_SAFE_DISTANCE 5  // 最小安全距离
#define INIT_SPACE_CHECK 3   // 初始位置检查范围

// 网络设置
#define SERVER_PORT 8888
#define SERVER_IP "127.0.0.1"
#define BUFFER_SIZE 1024

// 添加网络传输相关定义
#define STATE_START "STATE\n"
#define BOARD_START "BOARD\n"
#define STATE_END "END\n"

// 网络相关定义
#define RECONNECT_ATTEMPTS 3
#define RECONNECT_DELAY 1000000  // 1秒 (微秒)
#define SOCKET_TIMEOUT 5         // 5秒
#define SELECT_TIMEOUT_MS 100    // select超时（毫秒）

// 1. 添加缺失的网络配置
#define MAX_PENDING_CONNECTIONS 5
#define HEARTBEAT_INTERVAL 1     // 秒
#define MAX_DATA_BUFFER 8192     // 最大数据缓冲区大小

// 游戏控制
#define GAME_SPEED_MS 1000       // 游戏刷新间隔（毫秒）
#define RESET_DELAY_MS 3000     // 重置游戏延迟（毫秒）
#define EMPTY_CELL ' '     // 空白区域字符

// 控制键设置
#define KEY_UP 'w'
#define KEY_DOWN 's'
#define KEY_LEFT 'a'
#define KEY_RIGHT 'd'

// 玩家方向字符
#define PLAYER_UP L"⇡"
#define PLAYER_LEFT L"⇠"
#define PLAYER_DOWN L"⇣"
#define PLAYER_RIGHT L"⇢"

// 轨迹字符
#define TRAIL_HORIZONTAL L"┄"
#define TRAIL_VERTICAL L"┆"
#define TRAIL_CORNER_LEFT_UP L"╭"
#define TRAIL_CORNER_LEFT_DOWN L"╰"
#define TRAIL_CORNER_RIGHT_DOWN L"╯"
#define TRAIL_CORNER_RIGHT_UP L"╮"

// 边框字符
#define WALL_VERTICAL L"║"
#define WALL_HORIZONTAL L"═"
#define WALL_TOP_LEFT L"╔"
#define WALL_TOP_RIGHT L"╗"
#define WALL_BOTTOM_RIGHT L"╝"
#define WALL_BOTTOM_LEFT L"╚"

// 调整实际游戏区域（考虑边框）
#define GAME_WIDTH (BOARD_WIDTH - 2)
#define GAME_HEIGHT (BOARD_HEIGHT - 2)

// ANSI 颜色代码
#define COLOR_RESET std::string("\033[0m")
#define COLOR_RED std::string("\033[31m")
#define COLOR_GREEN std::string("\033[32m")
#define COLOR_YELLOW std::string("\033[33m")
#define COLOR_BLUE std::string("\033[34m")
#define COLOR_MAGENTA std::string("\033[35m")
#define COLOR_CYAN std::string("\033[36m")
#define COLOR_WHITE std::string("\033[37m")

// 玩家颜色数组
#define PLAYER_COLORS COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_YELLOW

// 修改分数设置
#define SCORE_SURVIVAL_TIME 2     // 每存活1秒得分
#define SCORE_KILL_POINTS 50      // 基础击杀得分
#define SCORE_TRANSFER_RATE 1.0   // 获得死亡玩家分数的比例
#define HIGH_SCORE_FILE "highscores.txt"  // 高分记录文件

// 添加游戏状态标记
#define GAME_WAITING 0
#define GAME_RUNNING 1
#define GAME_OVER 2

// 添加错误处理相关定义
#define MAX_RETRIES 3
#define RETRY_DELAY 1  // seconds

// 2. 添加错误处理定义
#define ERROR_INVALID_STATE -1
#define ERROR_CONNECTION_LOST -2
#define ERROR_TIMEOUT -3

// 游戏界面相关定义
#define GAME_TITLE L"TRON 光域争霸战"
#define GAME_HEADER_FORMAT "Score: %d : Your High Score: %d : Game High Score: %d"
#define GAME_FOOTER "Warning: Other Players Must be in This Game for You to Score!"
#define PLAYER_NAME_MAX_LENGTH 20

// 状态标记
#define STATE_SYNC_BEGIN "BEGIN\n"
#define STATE_SYNC_END "END\n"

// 调试信息
#define DEBUG_MODE 1  // 启用调试输出

// 3. 添加调试级别定义
#ifdef DEBUG_MODE
    #define DEBUG_LEVEL_INFO 1
    #define DEBUG_LEVEL_WARNING 2
    #define DEBUG_LEVEL_ERROR 3
#endif

// 添加心跳包设置
#define HEARTBEAT_INTERVAL_MS 1000  // 心跳间隔（毫秒）
#define CONNECTION_TIMEOUT_MS 5000  // 连接超时（毫秒）

// 添加游戏状态同步设置
#define GAME_STATE_SYNC_MS 100  // 状态同步间隔（毫秒）

// 调整复活相关设置
#define RESPAWN_DELAY 1          // 复活延迟（秒）
#define RESPAWN_PROTECTION 1     // 复活保护时间（秒）
#define RESPAWN_SAFE_RADIUS 5    // 复活安全半径

#endif // TRON_CONFIG_H
