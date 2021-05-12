cmake_minimum_required(VERSION 3.0)
project(memtest)


#设定源码
set(ROOT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(SRC_PATH "${ROOT_PATH}/test")
set(MEM_SRC_PATH "${ROOT_PATH}/src")

set(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wextra -Wall -g2  -fPIC")
set(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -Wextra -O0 -Wall -g2  -fPIC")

set(CMAKE_C_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wextra -Wall -g2  -ggdb3 -fPIC")
set(CMAKE_C_FLAGS_RELEASE "$ENV{CXXFLAGS} -Wextra -O0 -Wall -g2  -fPIC")

set(CMAKE_BUILD_TYPE "Debug")
set(DEBUG_FLAG ${CMAKE_C_FLAGS_DEBUG})

SET(EXECUTABLE_OUTPUT_PATH ${ROOT_PATH}/build_out/bin)       #设置可执行文件的输出目录
SET(LIBRARY_OUTPUT_PATH ${ROOT_PATH}/build_out/lib)           #设置库文件的输出目录

set(SOURCE_FILES "")
aux_source_directory(${SRC_PATH} SOURCE_FILES)

#设定头文件路径
include_directories(${SRC_PATH} ${MEM_SRC_PATH})
 
 
#设定链接库的路径（一般使用第三方非系统目录下的库）
LINK_DIRECTORIES(${LIBRARY_OUTPUT_PATH})
 
 
#添加依赖项子目录
 
#生成可执行文件
add_executable(memtest ${SOURCE_FILES})
target_link_libraries(memtest -lmpm -lpthread)
