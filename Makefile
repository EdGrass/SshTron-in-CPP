# 编译器设置
CXX = g++
CXXFLAGS = -std=c++17 -Wall -pthread -D_GLIBCXX_USE_WCHAR_T -DUNICODE -D_UNICODE

# 目标文件
SERVER = server
CLIENT = client

# 源文件
SERVER_SRC = server.cpp
CLIENT_SRC = client.cpp

# 头文件依赖
HEADERS = config.h

# 默认目标
all: $(SERVER) $(CLIENT)

# 编译服务器
$(SERVER): $(SERVER_SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(SERVER_SRC) -o $(SERVER)

# 编译客户端
$(CLIENT): $(CLIENT_SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(CLIENT_SRC) -o $(CLIENT)

# 清理编译文件
clean:
	rm -f $(SERVER) $(CLIENT)

# 运行服务器
run-server: $(SERVER)
	./$(SERVER)

# 运行客户端
run-client: $(CLIENT)
	./$(CLIENT)

.PHONY: all clean run-server run-client
