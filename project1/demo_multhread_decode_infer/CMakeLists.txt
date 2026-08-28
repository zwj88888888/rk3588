cmake_minimum_required(VERSION 3.4.1)
project(rk_platform)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "-pthread")

# 设置 RKNN API 相关路径和库
set(RKNN_API_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../rknn)
set(LIB_ARCH aarch64)
set(LIB_RKNN_RT ${RKNN_API_PATH}/lib/librknnrt.so)
set(LIB_RKNN_API ${RKNN_API_PATH}/lib/librknn_api.so)
set(LIB_RKMPP ${CMAKE_CURRENT_SOURCE_DIR}/../mpp/lib/librockchip_mpp.so)
set(LIB_RGA ${CMAKE_CURRENT_SOURCE_DIR}/../rga/lib/librga.so)

# 设置项目相关的头文件路径
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../rga/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../mpp/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../rknn/include)
set(LIB_ROOT ${CMAKE_SOURCE_DIR}/../lib)

# 包含 OpenCV 头文件
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

# ----------------------------------------------
# 设置model
# rknn api
set(RKNN_API_PATH ${CMAKE_SOURCE_DIR}/../../runtime/RK3588/${CMAKE_SYSTEM_NAME}/librknn_api)
set(LIB_ARCH aarch64)
set(RKNN_RT_LIB ${CMAKE_SOURCE_DIR}/include/librknnrt.so)
include_directories(${RKNN_API_PATH}/include)
include_directories(${CMAKE_SOURCE_DIR}/include/3rdparty)

# rga
set(RGA_PATH ${CMAKE_SOURCE_DIR}/include/3rdparty/rga/RK3588)
set(LIB_ARCH aarch64)
set(RGA_LIB ${RGA_PATH}/lib/Linux/${LIB_ARCH}/librga.so)
include_directories(${RGA_PATH}/include)

include_directories(${CMAKE_SOURCE_DIR}/include)

# ----------------------------------------------
# 设置demo相关路径
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
set(DEMO_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib)
file(GLOB DEMO_LIB_SOURCES ${DEMO_LIB_DIR}/*.cpp)

# message(STATUS "${DEMO_LIB_SOURCES}")
add_executable(myDemo
    ${DEMO_LIB_SOURCES}

    demo_multhread_decode_infer.cpp
    # main.cc
)

target_link_libraries(myDemo
    avcodec
    avformat
    avutil
    ${LIB_RKMPP}
    ${OpenCV_LIBS}

    # model
    ${RKNN_RT_LIB}
    ${RGA_LIB}
)