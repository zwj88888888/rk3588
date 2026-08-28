cmake_minimum_required(VERSION 3.4.1)
project(rk_platform)

# ================= 编译选项 =================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "-pthread")

# ================= 项目根路径 =================
set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# ================= RK 相关库 =================
set(RKNN_API_PATH ${PROJECT_ROOT}/../rknn)
set(MPP_PATH      ${PROJECT_ROOT}/../mpp)
set(RGA_PATH      ${PROJECT_ROOT}/../rga)

set(LIB_RKNN_RT   ${RKNN_API_PATH}/lib/librknnrt.so)
set(LIB_RKNN_API  ${RKNN_API_PATH}/lib/librknn_api.so)
set(LIB_RKMPP     ${MPP_PATH}/lib/librockchip_mpp.so)
set(LIB_RGA       ${RGA_PATH}/lib/librga.so)

# ================= FFmpeg（只用本地的精简版 ffmpeg-lite，不查找系统） =================
# 你已经把编译好的 ffmpeg-lite 拷贝到 project1 目录，所以这里直接指向它
set(FFMPEG_ROOT        ${PROJECT_ROOT}/../ffmpeg-lite)
set(FFMPEG_INCLUDE_DIR ${FFMPEG_ROOT}/include)
set(FFMPEG_LIB_DIR     ${FFMPEG_ROOT}/lib)

# 检查FFmpeg目录是否存在
if(NOT EXISTS ${FFMPEG_ROOT})
    message(FATAL_ERROR "FFmpeg directory not found: ${FFMPEG_ROOT}")
endif()

# 检查 FFmpeg 头文件和库目录是否存在
if(NOT EXISTS ${FFMPEG_INCLUDE_DIR})
    message(FATAL_ERROR "FFmpeg include directory not found: ${FFMPEG_INCLUDE_DIR}")
endif()

if(NOT EXISTS ${FFMPEG_LIB_DIR})
    message(FATAL_ERROR "FFmpeg lib directory not found: ${FFMPEG_LIB_DIR}")
endif()

message(STATUS "Using local ffmpeg-lite from: ${FFMPEG_ROOT}")

# ================= OpenCV（只用本地，不查找系统） =================
set(OPENCV_ROOT        ${PROJECT_ROOT}/../opencv4-1)
set(OPENCV_INCLUDE_DIR ${OPENCV_ROOT}/include)
set(OPENCV_INCLUDE_OPENCV4_DIR ${OPENCV_ROOT}/include/opencv4)
set(OPENCV_LIB_DIR     ${OPENCV_ROOT}/lib)

# 检查OpenCV目录是否存在
if(NOT EXISTS ${OPENCV_ROOT})
    message(FATAL_ERROR "OpenCV directory not found: ${OPENCV_ROOT}")
endif()

# 检查OpenCV头文件目录
if(NOT EXISTS ${OPENCV_INCLUDE_DIR})
    message(FATAL_ERROR "OpenCV include directory not found: ${OPENCV_INCLUDE_DIR}")
endif()

# 检查opencv4子目录（OpenCV 4.x的头文件结构）
if(NOT EXISTS ${OPENCV_INCLUDE_OPENCV4_DIR})
    message(FATAL_ERROR "OpenCV opencv4 include directory not found: ${OPENCV_INCLUDE_OPENCV4_DIR}")
endif()

# 检查OpenCV库目录
if(NOT EXISTS ${OPENCV_LIB_DIR})
    message(FATAL_ERROR "OpenCV lib directory not found: ${OPENCV_LIB_DIR}")
endif()

# 查找OpenCV库文件（不使用find_package，直接查找.so文件）
file(GLOB OpenCV_LIBS_FILES "${OPENCV_LIB_DIR}/libopencv_*.so")
if(NOT OpenCV_LIBS_FILES)
    message(FATAL_ERROR "No OpenCV library files found in ${OPENCV_LIB_DIR}")
endif()

set(OpenCV_LIBS ${OpenCV_LIBS_FILES})
list(LENGTH OpenCV_LIBS_FILES OPENCV_LIBS_COUNT)
message(STATUS "OpenCV found: ${OPENCV_ROOT} (${OPENCV_LIBS_COUNT} libraries)")

# ================= 头文件 =================
include_directories(
    ${PROJECT_ROOT}
    ${PROJECT_ROOT}/include

    ${RKNN_API_PATH}/include
    ${MPP_PATH}/include
    ${RGA_PATH}/include

    ${FFMPEG_INCLUDE_DIR}
    ${OPENCV_INCLUDE_DIR}
    ${OPENCV_INCLUDE_OPENCV4_DIR}
)

# ================= demo 源码 =================
file(GLOB DEMO_LIB_SOURCES
    ${PROJECT_ROOT}/lib/*.cpp
)

add_executable(myDemo
    main.cpp
    ${DEMO_LIB_SOURCES}
)

# ================= 链接（全部用绝对路径） =================
target_link_libraries(myDemo
    # -------- FFmpeg --------
    ${FFMPEG_LIB_DIR}/libavcodec.so
    ${FFMPEG_LIB_DIR}/libavformat.so
    ${FFMPEG_LIB_DIR}/libavutil.so
    # 如果你的 ffmpeg-lite 只构建了 avformat/avcodec/avutil，可以把多余的几个库去掉

    # -------- OpenCV --------
    ${OpenCV_LIBS}

    # -------- Rockchip --------
    ${LIB_RKMPP}
    ${LIB_RGA}
    ${LIB_RKNN_API}
    ${LIB_RKNN_RT}
)

# ================= 运行期 so 搜索路径（关键） =================
# 构建RPATH列表（全部使用本地路径 + 系统库路径用于x264/x265）
set(RUNTIME_RPATH_LIST
    ${FFMPEG_LIB_DIR}
    ${OPENCV_LIB_DIR}
    ${RKNN_API_PATH}/lib
    ${MPP_PATH}/lib
    ${RGA_PATH}/lib
    /usr/lib/aarch64-linux-gnu  # 系统库路径，用于x264/x265
)

set_target_properties(myDemo PROPERTIES
    BUILD_RPATH "${RUNTIME_RPATH_LIST}"
    INSTALL_RPATH "${RUNTIME_RPATH_LIST}"
)
