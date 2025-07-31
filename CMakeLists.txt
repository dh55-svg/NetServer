cmake_minimum_required(VERSION 3.10)
project(MyNetServer)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -pthread -O3")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
file(GLOB_RECURSE SRC "./*.cpp" "./*.h")

# 添加可执行文件
add_executable(MyNetServer ${SRC})

# 链接所需的库
target_link_libraries(MyNetServer PRIVATE
    pthread
)