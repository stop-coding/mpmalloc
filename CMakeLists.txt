###############################################################
#*【项目】
#*【描述】
#*【作者】hongchunhua
#*【时间】2021.04.22
###############################################################

cmake_minimum_required(VERSION 3.0)
project(mpm)
 
#宏定义
add_definitions(-D USR_FLAG)
add_definitions(-D CLANG)

set(COM_ROOT_PATH "${CMAKE_CURRENT_SOURCE_DIR}")
set(SRC_PATH "${COM_ROOT_PATH}/src")


#设置输出路径
SET(EXECUTABLE_OUTPUT_PATH ${COM_ROOT_PATH}/build_out/bin)       #设置可执行文件的输出目录
SET(LIBRARY_OUTPUT_PATH ${COM_ROOT_PATH}/build_out/lib)           #设置库文件的输出目录

set(SOURCE_FILES "")
aux_source_directory(${SRC_PATH} SOURCE_FILES)


#设定头文件路径
include_directories(${SRC_PATH})
 
 
#设定链接库的路径（一般使用第三方非系统目录下的库）
set(LINK_LIB_PATH ${COM_ROOT_PATH}/lib)
LINK_DIRECTORIES(${LIBRARY_OUTPUT_PATH} ${LINK_LIB_PATH})
 
 
#动态库
add_library(mpm SHARED ${SOURCE_FILES})
target_link_libraries(mpm)

add_subdirectory("${COM_ROOT_PATH}/test")

