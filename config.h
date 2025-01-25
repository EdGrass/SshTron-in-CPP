#ifndef TRON_CONFIG_H
#define TRON_CONFIG_H

#define BOARD_WIDTH 78
#define BOARD_HEIGHT 22

#define MAX_PLAYERS 4
#define PLAYER_ONE_CHAR '1'
#define PLAYER_TWO_CHAR '2'
#define MIN_SAFE_DISTANCE 5
#define INIT_SPACE_CHECK 3

#define SERVER_PORT 8888
#define SERVER_IP "127.0.0.1"
#define BUFFER_SIZE 4096

#define STATE_START "STATE\n"
#define BOARD_START "BOARD\n"
#define STATE_END "END\n"

#define RECONNECT_ATTEMPTS 3
#define RECONNECT_DELAY 1000000
#define SOCKET_TIMEOUT 10
#define SELECT_TIMEOUT_MS 100

#define MAX_PENDING_CONNECTIONS 5
#define HEARTBEAT_INTERVAL 1
#define MAX_DATA_BUFFER 16384

#define GAME_SPEED_MS 300
#define RESET_DELAY_MS 3000
#define EMPTY_CELL ' '

#define KEY_UP 'w'
#define KEY_DOWN 's'
#define KEY_LEFT 'a'
#define KEY_RIGHT 'd'

#define PLAYER_UP L"⇡"
#define PLAYER_LEFT L"⇠"
#define PLAYER_DOWN L"⇣"
#define PLAYER_RIGHT L"⇢"

#define TRAIL_HORIZONTAL L"┄"
#define TRAIL_VERTICAL L"┆"
#define TRAIL_CORNER_LEFT_UP L"╭"
#define TRAIL_CORNER_LEFT_DOWN L"╰"
#define TRAIL_CORNER_RIGHT_DOWN L"╯"
#define TRAIL_CORNER_RIGHT_UP L"╮"

#define WALL_VERTICAL L"║"
#define WALL_HORIZONTAL L"═"
#define WALL_TOP_LEFT L"╔"
#define WALL_TOP_RIGHT L"╗"
#define WALL_BOTTOM_RIGHT L"╝"
#define WALL_BOTTOM_LEFT L"╚"

#define GAME_WIDTH (BOARD_WIDTH - 2)
#define GAME_HEIGHT (BOARD_HEIGHT - 2)

#define COLOR_RESET std::string("\033[0m")
#define COLOR_RED std::string("\033[31m")
#define COLOR_GREEN std::string("\033[32m")
#define COLOR_YELLOW std::string("\033[33m")
#define COLOR_BLUE std::string("\033[34m")
#define COLOR_MAGENTA std::string("\033[35m")
#define COLOR_CYAN std::string("\033[36m")
#define COLOR_WHITE std::string("\033[37m")

#define PLAYER_COLORS COLOR_RED, COLOR_GREEN, COLOR_BLUE, COLOR_YELLOW

#define SCORE_SURVIVAL_TIME 2
#define SCORE_KILL_POINTS 50
#define SCORE_TRANSFER_RATE 1.0
#define HIGH_SCORE_FILE "highscores.txt"

#define GAME_WAITING 0
#define GAME_RUNNING 1
#define GAME_OVER 2

#define MAX_RETRIES 3
#define RETRY_DELAY 1

#define ERROR_INVALID_STATE -1
#define ERROR_CONNECTION_LOST -2
#define ERROR_TIMEOUT -3

#define GAME_TITLE L"Welcome to TronGame"
#define GAME_HEADER_FORMAT "Score:%d | Your High Score:%d | Game High Score:%d "
#define GAME_FOOTER "Warning: Other Players Must be in This Game for You to Score!"
#define PLAYER_NAME_MAX_LENGTH 20

#define DEBUG_MODE 1

#ifdef DEBUG_MODE
    #define DEBUG_LEVEL_INFO 1
    #define DEBUG_LEVEL_WARNING 2
    #define DEBUG_LEVEL_ERROR 3
    
    #define DEBUG_FORMAT        "[DEBUG] %s:%d: "
    #define WARNING_FORMAT      "[WARNING] %s:%d: "
    #define ERROR_FORMAT        "[ERROR] %s:%d: "
#endif

#define HEARTBEAT_INTERVAL_MS 300
#define CONNECTION_TIMEOUT_MS 5000

#define GAME_STATE_SYNC_MS 100

#define RESPAWN_DELAY 1
#define RESPAWN_PROTECTION 1
#define RESPAWN_SAFE_RADIUS 5

#endif 