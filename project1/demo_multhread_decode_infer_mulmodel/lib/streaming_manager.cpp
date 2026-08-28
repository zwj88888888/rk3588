/*
 * Copyright (c) 2025-04-01 HeXiaotian
 *
 * This source code is licensed for learning and research purposes only.
 * Commercial use, redistribution, resale, and creation of derivative works
 * are strictly prohibited without prior written permission from the author.
 */

#include "streaming_manager.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <chrono>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
}

StreamingManager::StreamingManager() 
    : streaming_active_(false)
    , should_stop_(false)
    , avformat_context_(nullptr)
    , mpp_encoder_(nullptr)
    , rtmp_frame_index_(0)
    , frame_count_(0)
{
}

StreamingManager::~StreamingManager() {
    stopStreaming();
}

bool StreamingManager::initialize(const StreamingConfig& config) {
    config_ = config;
    
    if (config_.enable_rtmp) {
        if (!initializeRTMP()) {
            std::cerr << "Failed to initialize RTMP streaming" << std::endl;
            return false;
        }
    }
    
    if (config_.enable_rtsp) {
        if (!initializeRTSP()) {
            std::cerr << "Failed to initialize RTSP streaming" << std::endl;
            return false;
        }
    }
    
    return true;
}

void StreamingManager::addStreamingData(const StreamingData& data) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    // 限制队列大小，避免内存溢出
    if (streaming_queue_.size() > 10) {
        streaming_queue_.pop(); // 丢弃最旧的数据
        stats_.frames_dropped++;
    }
    
    streaming_queue_.push(data);
    queue_cv_.notify_one();
    
    // 调试信息：每100帧打印一次
    static int frame_count = 0;
    if (++frame_count % 100 == 0) {
        std::cout << "Added frame to streaming queue, queue size: " << streaming_queue_.size() << std::endl;
    }
}

void StreamingManager::startStreaming() {
    if (streaming_active_.load()) {
        return;
    }
    
    streaming_active_ = true;
    should_stop_ = false;
    streaming_thread_ = std::thread(&StreamingManager::streamingWorker, this);
    
    std::cout << "Streaming started" << std::endl;
}

void StreamingManager::stopStreaming() {
    if (!streaming_active_.load()) {
        return;
    }
    
    should_stop_ = true;
    queue_cv_.notify_all();
    
    if (streaming_thread_.joinable()) {
        streaming_thread_.join();
    }
    
    streaming_active_ = false;
    
    // 清理FFmpeg / MPP 资源
    if (avformat_context_) {
        AVFormatContext* fmt_ctx = (AVFormatContext*)avformat_context_;
        av_write_trailer(fmt_ctx);
        if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE) && fmt_ctx->pb) {
            avio_closep(&fmt_ctx->pb);
        }
        avformat_free_context(fmt_ctx);
        avformat_context_ = nullptr;
    }

    if (mpp_encoder_) {
        mpp_encoder_->Release();
        delete mpp_encoder_;
        mpp_encoder_ = nullptr;
    }
    
    std::cout << "Streaming stopped" << std::endl;
}

void StreamingManager::streamingWorker() {
    cv::Mat last_frame;  // 保存最后一帧，用于超时情况
    bool has_last_frame = false;
    
    while (!should_stop_.load()) {
        StreamingData data;
        bool got_data = false;
        
        // 等待数据，使用超时机制防止连接超时
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            auto timeout = std::chrono::milliseconds(1000 / config_.fps);  // 按帧率计算超时时间
            if (queue_cv_.wait_for(lock, timeout, [this] { 
                return !streaming_queue_.empty() || should_stop_.load(); 
            })) {
                // 有数据或需要停止
                if (should_stop_.load()) {
                    break;
                }
                
                if (!streaming_queue_.empty()) {
                    data = streaming_queue_.front();
                    streaming_queue_.pop();
                    got_data = true;
                }
            } else {
                // 超时，使用最后一帧或跳过
                if (has_last_frame && !last_frame.empty()) {
                    // 使用最后一帧保持连接活跃
                    data.frame = last_frame.clone();
                    data.stream_id = 0;
                    data.timestamp = std::chrono::system_clock::now();
                    memset(&data.person_results, 0, sizeof(detect_result_group_t));
                    memset(&data.helmet_results, 0, sizeof(detect_result_group_t));
                    memset(&data.tired_results, 0, sizeof(detect_result_group_t));
                    memset(&data.callplay_results, 0, sizeof(detect_result_group_t));
                    got_data = true;
                } else {
                    // 没有数据，继续等待
                    continue;
                }
            }
        }
        
        if (!got_data) {
            continue;
        }
        
        // 处理推流数据
        cv::Mat frame = data.frame.clone();
        
        // 绘制检测结果
        if (config_.draw_detections) {
            drawDetections(frame, data);
        }
        
        // 调整分辨率
        if (frame.cols != config_.width || frame.rows != config_.height) {
            cv::resize(frame, frame, cv::Size(config_.width, config_.height));
        }
        
        // 保存当前帧作为最后一帧
        last_frame = frame.clone();
        has_last_frame = true;
        
        // 推流
        bool success = false;
        if (config_.enable_rtmp) {
            success |= sendRTMPFrame(frame);
        }
        if (config_.enable_rtsp) {
            success |= sendRTSPFrame(frame);
        }
        
        // 更新统计信息
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            if (success) {
                stats_.frames_sent++;
            } else {
                stats_.frames_dropped++;
            }
            
            // 计算FPS
            auto now = std::chrono::system_clock::now();
            frame_count_++;
            if (frame_count_ % config_.fps == 0) {
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - stats_.last_frame_time).count();
                if (duration > 0) {
                    stats_.fps = (config_.fps * 1000.0) / duration;
                }
                stats_.last_frame_time = now;
            }
        }
        
        // 控制推流帧率
        std::this_thread::sleep_for(std::chrono::milliseconds(1000 / config_.fps));
    }
}

void StreamingManager::drawDetections(cv::Mat& frame, const StreamingData& data) {
    // 绘制人员检测结果 (绿色)
    for (int i = 0; i < data.person_results.count; i++) {
        const auto& result = data.person_results.results[i];
        cv::rectangle(frame, 
            cv::Point(result.box.left, result.box.top),
            cv::Point(result.box.right, result.box.bottom),
            cv::Scalar(0, 255, 0), 2);
        cv::putText(frame, "Person", 
            cv::Point(result.box.left, result.box.top - 10),
            cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);
    }
    
    // 绘制安全帽检测结果 (红色)
    for (int i = 0; i < data.helmet_results.count; i++) {
        const auto& result = data.helmet_results.results[i];
        cv::rectangle(frame, 
            cv::Point(result.box.left, result.box.top),
            cv::Point(result.box.right, result.box.bottom),
            cv::Scalar(0, 0, 255), 2);
        cv::putText(frame, "Helmet", 
            cv::Point(result.box.left, result.box.top - 10),
            cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 255), 1);
    }
    
    // 绘制疲劳检测结果 (黄色)
    for (int i = 0; i < data.tired_results.count; i++) {
        const auto& result = data.tired_results.results[i];
        cv::rectangle(frame, 
            cv::Point(result.box.left, result.box.top),
            cv::Point(result.box.right, result.box.bottom),
            cv::Scalar(0, 255, 255), 2);
        cv::putText(frame, "Tired", 
            cv::Point(result.box.left, result.box.top - 10),
            cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 255), 1);
    }
    
    // 绘制通话/游戏检测结果 (蓝色)
    for (int i = 0; i < data.callplay_results.count; i++) {
        const auto& result = data.callplay_results.results[i];
        cv::rectangle(frame, 
            cv::Point(result.box.left, result.box.top),
            cv::Point(result.box.right, result.box.bottom),
            cv::Scalar(255, 0, 0), 2);
        cv::putText(frame, "Call/Play", 
            cv::Point(result.box.left, result.box.top - 10),
            cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 0, 0), 1);
    }
    
    // 添加时间戳和统计信息
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    
    cv::putText(frame, ss.str(), cv::Point(10, 30),
        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);
    
    // 添加检测统计
    std::string stats = "P:" + std::to_string(data.person_results.count) +
                       " H:" + std::to_string(data.helmet_results.count) +
                       " T:" + std::to_string(data.tired_results.count) +
                       " C:" + std::to_string(data.callplay_results.count);
    
    cv::putText(frame, stats, cv::Point(10, 60),
        cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255, 255, 255), 2);
}

std::string StreamingManager::createDetectionJSON(const StreamingData& data) {
    std::stringstream json;
    json << "{";
    json << "\"stream_id\":" << data.stream_id << ",";
    json << "\"timestamp\":" << std::chrono::duration_cast<std::chrono::milliseconds>(
        data.timestamp.time_since_epoch()).count() << ",";
    
    json << "\"detections\":{";
    json << "\"person\":" << data.person_results.count << ",";
    json << "\"helmet\":" << data.helmet_results.count << ",";
    json << "\"tired\":" << data.tired_results.count << ",";
    json << "\"callplay\":" << data.callplay_results.count;
    json << "}";
    
    json << "}";
    return json.str();
}

bool StreamingManager::initializeRTMP() {
    // 初始化 FFmpeg 网络
    avformat_network_init();

    AVFormatContext* fmt_ctx = nullptr;

    // 创建输出格式上下文（flv over RTMP）
    if (avformat_alloc_output_context2(&fmt_ctx, nullptr, "flv", config_.rtmp_url.c_str()) < 0 || !fmt_ctx) {
        std::cerr << "Could not create output context" << std::endl;
        return false;
    }

    // 创建视频流（不再让 FFmpeg 编码，只做封装）
    AVStream* stream = avformat_new_stream(fmt_ctx, nullptr);
    if (!stream) {
        std::cerr << "Could not create stream" << std::endl;
        avformat_free_context(fmt_ctx);
        return false;
    }

    AVCodecParameters* codecpar = stream->codecpar;
    codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    codecpar->codec_id   = AV_CODEC_ID_H264;           // 我们自己用 MPP 编 H.264
    codecpar->width      = config_.width;
    codecpar->height     = config_.height;
    codecpar->format     = AV_PIX_FMT_YUV420P;         // 编码输出格式

    // 时间基：以 fps 为基准
    stream->time_base = AVRational{1, config_.fps};

    // 初始化 MPP 硬编码器
    mpp_encoder_ = new MppEncoder();
    if (mpp_encoder_->Init(config_.width, config_.height, config_.fps, config_.bitrate, 264) != 0) {
        std::cerr << "Failed to init MPP encoder" << std::endl;
        delete mpp_encoder_;
        mpp_encoder_ = nullptr;
        avformat_free_context(fmt_ctx);
        return false;
    }

    // 从 MPP 获取 SPS/PPS 等 extra info，填充到 codecpar->extradata
    uint8_t header_buf[1024];
    int header_size = sizeof(header_buf);
    if (mpp_encoder_->GetHeader(header_buf, &header_size) == 0 && header_size > 0) {
        codecpar->extradata = (uint8_t*)av_malloc(header_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (codecpar->extradata) {
            memcpy(codecpar->extradata, header_buf, header_size);
            memset(codecpar->extradata + header_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
            codecpar->extradata_size = header_size;
            std::cout << "H.264 extradata from MPP, size: " << header_size << " bytes" << std::endl;
        }
    } else {
        std::cerr << "Warning: failed to get H.264 extra info from MPP encoder" << std::endl;
    }

    // 打开输出（RTMP）
    if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&fmt_ctx->pb, config_.rtmp_url.c_str(), AVIO_FLAG_WRITE) < 0) {
            std::cerr << "Could not open output URL: " << config_.rtmp_url << std::endl;
            avformat_free_context(fmt_ctx);
            return false;
        }
    }

    if (avformat_write_header(fmt_ctx, nullptr) < 0) {
        std::cerr << "Error occurred when writing header to RTMP" << std::endl;
        if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE) && fmt_ctx->pb) {
            avio_closep(&fmt_ctx->pb);
        }
        avformat_free_context(fmt_ctx);
        return false;
    }

    avformat_context_ = fmt_ctx;
    rtmp_frame_index_ = 0;
    return true;
}

bool StreamingManager::sendRTMPFrame(const cv::Mat& frame) {
    if (!avformat_context_ || !mpp_encoder_) {
        return false;
    }

    AVFormatContext* fmt_ctx = (AVFormatContext*)avformat_context_;
    if (fmt_ctx->nb_streams == 0) {
        return false;
    }
    AVStream* stream = fmt_ctx->streams[0];

    // 使用 MPP 编码当前帧（BGR -> H.264）
    // 预估一个足够大的缓冲区（经验值：分辨率 * 2 一般足够）
    int max_packet_size = config_.width * config_.height * 2;
    std::vector<uint8_t> enc_buf(max_packet_size);
    int packet_size = max_packet_size;
    if (!frame.isContinuous()) {
        std::cerr << "Frame is not continuous, skip\n";
        return false;
    }

    int ret = mpp_encoder_->EncodeFrame(
        frame.data,
        frame.cols,
        frame.rows,
        enc_buf.data(),
        &packet_size,
        (int)frame.step
    );

    if (ret != 0 || packet_size <= 0) {
        // 本帧没有有效编码输出，直接跳过
        return false;
    }

    // 构造 AVPacket 并发送
    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.data = enc_buf.data();
    pkt.size = packet_size;
    pkt.stream_index = stream->index;

    // 简单的基于帧序号的 PTS
    pkt.pts = rtmp_frame_index_;
    pkt.dts = rtmp_frame_index_;
    rtmp_frame_index_++;

    AVRational src_tb{1, config_.fps};
    av_packet_rescale_ts(&pkt, src_tb, stream->time_base);

    ret = av_interleaved_write_frame(fmt_ctx, &pkt);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "Error writing MPP-encoded frame to RTMP: " << errbuf << std::endl;
        return false;
    }

    static int send_count = 0;
    if (++send_count % 100 == 0) {
        std::cout << "Sent frame " << send_count
                  << " (MPP encoded, size=" << packet_size << " bytes)" << std::endl;
    }

    return true;
}

bool StreamingManager::initializeRTSP() {
    // RTSP推流实现（类似RTMP，但使用不同的输出格式）
    // 这里简化实现，实际项目中需要更复杂的RTSP服务器设置
    return true;
}

bool StreamingManager::sendRTSPFrame(const cv::Mat& frame) {
    // RTSP推流实现
    return true;
}

StreamingManager::StreamingStats StreamingManager::getStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}
