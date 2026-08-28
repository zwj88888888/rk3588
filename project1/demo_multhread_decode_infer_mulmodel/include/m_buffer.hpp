/*
 * Copyright (c) 2025-04-01 HeXiaotian
 *
 * This source code is licensed for learning and research purposes only.
 * Commercial use, redistribution, resale, and creation of derivative works
 * are strictly prohibited without prior written permission from the author.
 */

#include <opencv2/opencv.hpp>
#include <mutex>
#include <vector>

struct Mbuffer{
    cv::Mat img;
    std::mutex mtx;
    // 本地文件限速：每输出一帧后 sleep，避免倍速（解码可能一包多帧）
    int frame_interval_ms = 0;  // 0 = 不限速
    bool throttle = false;
    // 预分配缓冲区，避免每帧 malloc
    std::vector<uint8_t> yuv_work;
    cv::Mat bgr_work;
};