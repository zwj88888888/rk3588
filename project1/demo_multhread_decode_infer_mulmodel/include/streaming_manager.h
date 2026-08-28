/*
 * Copyright (c) 2025-04-01 HeXiaotian
 *
 * This source code is licensed for learning and research purposes only.
 * Commercial use, redistribution, resale, and creation of derivative works
 * are strictly prohibited without prior written permission from the author.
 */

#pragma once
#include <opencv2/opencv.hpp>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include "postprocess.h"
#include <condition_variable>
#include "mpp_encoder.h"
// 推流数据结构
struct StreamingData {
    int stream_id;
    cv::Mat frame;
    detect_result_group_t person_results;    // 人员检测结果
    detect_result_group_t helmet_results;   // 安全帽检测结果
    detect_result_group_t tired_results;    // 疲劳检测结果
    detect_result_group_t callplay_results; // 通话/游戏检测结果
    std::chrono::system_clock::time_point timestamp;
};

// 推流配置
struct StreamingConfig {
    std::string rtmp_url;           // RTMP推流地址
    std::string rtsp_url;           // RTSP推流地址
    int width = 1280;              // 推流分辨率宽度
    int height = 720;               // 推流分辨率高度
    int fps = 25;                   // 推流帧率
    int bitrate = 2000000;          // 推流码率
    bool enable_rtmp = true;         // 是否启用RTMP推流
    bool enable_rtsp = false;        // 是否启用RTSP推流
    bool draw_detections = true;    // 是否在推流中绘制检测框
};

class StreamingManager {
public:
    StreamingManager();
    ~StreamingManager();
    
    // 初始化推流
    bool initialize(const StreamingConfig& config);
    
    // 添加推理结果数据到推流队列
    void addStreamingData(const StreamingData& data);
    
    // 启动推流线程
    void startStreaming();
    
    // 停止推流
    void stopStreaming();
    
    // 检查推流状态
    bool isStreaming() const { return streaming_active_.load(); }
    
    // 获取推流统计信息
    struct StreamingStats {
        int frames_sent = 0;
        int frames_dropped = 0;
        double fps = 0.0;
        std::chrono::system_clock::time_point last_frame_time;
    };
    StreamingStats getStats() const;

private:
    // 推流线程函数
    void streamingWorker();
    
    // 绘制检测结果到图像上
    void drawDetections(cv::Mat& frame, const StreamingData& data);
    
    // 创建JSON格式的检测结果
    std::string createDetectionJSON(const StreamingData& data);
    
    // RTMP推流相关
    bool initializeRTMP();
    bool sendRTMPFrame(const cv::Mat& frame);
    
    // RTSP推流相关
    bool initializeRTSP();
    bool sendRTSPFrame(const cv::Mat& frame);

private:
    StreamingConfig config_;
    std::atomic<bool> streaming_active_;
    std::atomic<bool> should_stop_;
    
    // 推流队列
    std::queue<StreamingData> streaming_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    // 推流线程
    std::thread streaming_thread_;
    
    // FFmpeg相关
    void* avformat_context_;
    // 使用 MPP 进行硬编码，不再使用 FFmpeg 软编码
    MppEncoder* mpp_encoder_ = nullptr;
    int64_t     rtmp_frame_index_ = 0;
    
    // 统计信息
    mutable std::mutex stats_mutex_;
    StreamingStats stats_;
    
    // 帧率计算
    std::chrono::system_clock::time_point last_fps_time_;
    int frame_count_;
};
