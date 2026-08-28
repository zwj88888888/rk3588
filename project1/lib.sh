#!/bin/bash
# extract_libs.sh
# 自动解压 lib.tar.gz 到相对 lib 目录（去掉压缩包顶层目录）

set -e

# FFmpeg
FFMPEG_TAR="./ffmpeg7-1/lib.tar.gz"
FFMPEG_DEST="./ffmpeg7-1/lib"

# OpenCV
OPENCV_TAR="./opencv4-1/lib.tar.gz"
OPENCV_DEST="./opencv4-1/lib"

# 检查压缩包是否存在
if [ ! -f "$FFMPEG_TAR" ]; then
    echo "Error: $FFMPEG_TAR not found!"
    exit 1
fi

if [ ! -f "$OPENCV_TAR" ]; then
    echo "Error: $OPENCV_TAR not found!"
    exit 1
fi

# 创建目标目录（如果不存在）
mkdir -p "$FFMPEG_DEST"
mkdir -p "$OPENCV_DEST"

# 解压（去掉压缩包最顶层目录）
echo "Extracting FFmpeg libraries to $FFMPEG_DEST ..."
tar --strip-components=1 -xzf "$FFMPEG_TAR" -C "$FFMPEG_DEST"

echo "Extracting OpenCV libraries to $OPENCV_DEST ..."
tar --strip-components=1 -xzf "$OPENCV_TAR" -C "$OPENCV_DEST"

echo "All libraries extracted successfully."
