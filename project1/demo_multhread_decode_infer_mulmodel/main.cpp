
/*
 * Copyright (c) 2025-04-01 HeXiaotian
 *
 * This source code is licensed for learning and research purposes only.
 * Commercial use, redistribution, resale, and creation of derivative works
 * are strictly prohibited without prior written permission from the author.
 */

#include <iostream>
#include <cstring>
#include <chrono>
#include <thread>
#include "stream_loader.h"
#include "rknnPool.hpp"
#include "streaming_manager.h"
#include "detection_fusion_manager.h"
#include "im2d.h"

char *model_person = "../../model/person_relu.rknn";
char *model_helmet = "../../model/helmet_relu.rknn";
//char *model_tired = "../../model/tired_relu.rknn";
char *model_callplay = "../../model/callplay_relu.rknn";
StreamLoaderManager &manager = StreamLoaderManager::getInstance();
// 创建RKNN模型的集合，用于存储多个模型实例
vector<rknn_lite *> rk_pool;
// 创建线程池对象，使用n个线程
vector<std::thread> rk_threads;
// 用于存储显示图像的Mat
vector<cv::Mat> images(6);
// 管理images的互斥锁
vector<std::mutex> mutexes(6);

// 推流管理器
StreamingManager streaming_manager;

// 检测融合管理器
DetectionFusionManager fusion_manager;

void combineImage(StreamLoaderManager &manager)
{
    cv::Mat combinedImage(1080, 1280, CV_8UC3, cv::Scalar(0, 0, 0)); // 初始化为黑色
    cv::Mat lastCombinedImage; // 保存最后一帧
    bool hasLastFrame = false;
    const int target_fps = 24; // 目标帧率
    const int frame_interval_ms = 1000 / target_fps; // 每帧间隔（毫秒）
    auto last_frame_time = std::chrono::steady_clock::now();
    
    while (true)
    {
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_frame_time).count();
        
        // 控制帧率：如果还没到时间，等待
        if (elapsed < frame_interval_ms) {
            std::this_thread::sleep_for(std::chrono::milliseconds(frame_interval_ms - elapsed));
            current_time = std::chrono::steady_clock::now();
        }
        last_frame_time = current_time;
        
        bool hasNewFrame = false;
        const int tile_w = 640, tile_h = 360;

        for (int i = 0; i < manager.num_stream; ++i)
        {
            cv::Mat local_img;
            {
                std::lock_guard<std::mutex> lock(mutexes[i]);
                if (images[i].empty())
                    continue;
                local_img = std::move(images[i]);
                images[i] = cv::Mat();
            }

            cv::Mat resizedImage(tile_h, tile_w, CV_8UC3);
            int src_w = local_img.cols, src_h = local_img.rows;

            rga_buffer_t src_buf = wrapbuffer_virtualaddr(local_img.data, src_w, src_h, RK_FORMAT_BGR_888);
            rga_buffer_t dst_buf = wrapbuffer_virtualaddr(resizedImage.data, tile_w, tile_h, RK_FORMAT_BGR_888);

            IM_STATUS status = imresize(src_buf, dst_buf);
            if (status != IM_STATUS_SUCCESS) {
                cv::resize(local_img, resizedImage, cv::Size(tile_w, tile_h));
            }

            int row = i / 2, col = i % 2;
            int x = col * tile_w, y = row * tile_h;
            resizedImage.copyTo(combinedImage(cv::Rect(x, y, tile_w, tile_h)));
            hasNewFrame = true;
        }

        cv::Mat frameToSend;
        if (hasNewFrame) {
            lastCombinedImage = combinedImage.clone();
            frameToSend = lastCombinedImage;
            hasLastFrame = true;
        } else if (hasLastFrame) {
            frameToSend = lastCombinedImage;
        } else {
            frameToSend = combinedImage.clone();
        }
        
        // 显示合成图像
        //cv::imshow("Combined Image", frameToSend);
        
        // 发送到推流管理器
        StreamingData stream_data;
        stream_data.stream_id = 0;
        stream_data.frame = frameToSend;
        stream_data.timestamp = std::chrono::system_clock::now();
        // 初始化检测结果（如果需要绘制检测结果，需要从推理结果中获取）
        memset(&stream_data.person_results, 0, sizeof(detect_result_group_t));
        memset(&stream_data.helmet_results, 0, sizeof(detect_result_group_t));
        memset(&stream_data.tired_results, 0, sizeof(detect_result_group_t));
        memset(&stream_data.callplay_results, 0, sizeof(detect_result_group_t));
        
        streaming_manager.addStreamingData(stream_data);
        
        // 等待按键
        //if (cv::waitKey(10) == 27)
            //break;
    }
    cv::destroyAllWindows(); // 销毁所有窗口
}

void rknn_infer(rknn_lite *p1, rknn_lite *p2, rknn_lite *p3, rknn_lite *p4, int i)
{
    dpool::ThreadPool pool(4);

    detect_result_group_t g1, g2, g3, g4;
    memset(&g1, 0, sizeof(detect_result_group_t));
    memset(&g2, 0, sizeof(detect_result_group_t));
    memset(&g3, 0, sizeof(detect_result_group_t));
    memset(&g4, 0, sizeof(detect_result_group_t));

    // 跳帧推理：每 INFER_INTERVAL 帧做一次完整推理，中间帧复用上一帧检测结果，提高实时性
    const int INFER_INTERVAL = 2; 
    int frame_count = 0;
    std::vector<FusedDetection> last_fused;

    while (!manager.stream_loaders[i]->stopFlag)
    {
        std::unique_lock<std::mutex> lock(manager.stream_loaders[i]->buffer.mtx);
        if (manager.stream_loaders[i]->buffer.img.empty())
        {
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        p1->ori_img = manager.stream_loaders[i]->buffer.img.clone();
        p2->ori_img = p1->ori_img;
        if (p3) p3->ori_img = p1->ori_img;
        p4->ori_img = p1->ori_img;
        lock.unlock();

        frame_count++;
        bool do_infer = (frame_count % INFER_INTERVAL == 1) || last_fused.empty();

        if (do_infer)
        {
            auto f1 = pool.submit([&]() { p1->interf(g1); });
            auto f2 = pool.submit([&]() { p2->interf(g2); });
            std::future<void> f3;
            if (p3) f3 = pool.submit([&]() { p3->interf(g3); });
            auto f4 = pool.submit([&]() { p4->interf(g4); });

            f1.get(); f2.get();
            if (p3) f3.get();
            f4.get();

            last_fused = fusion_manager.fuseDetections(g1, g2, g3, g4);
        }

        fusion_manager.drawFusedDetections(p1->ori_img, last_fused);

        std::unique_lock<std::mutex> lockimage(mutexes[i]);
        images[i] = std::move(p1->ori_img);
        lockimage.unlock();
    }
}

int main(int argc, char *argv[])
{
    manager.num_stream = std::stoi(argv[1]); // 获取输入视频流数量
    
    // 初始化推流配置
    StreamingConfig stream_config;
    stream_config.rtmp_url = "rtmp://192.168.137.1/live/livestream"; // 替换为实际的RTMP地址
    stream_config.width = 1280;
    stream_config.height = 720;
    stream_config.fps = 24;
    stream_config.bitrate = 2000000;
    stream_config.enable_rtmp = true;
    stream_config.draw_detections = true;
    
    // 初始化推流管理器
    if (!streaming_manager.initialize(stream_config)) {
        std::cerr << "Failed to initialize streaming manager" << std::endl;
        return -1;
    }
    
    // 启动推流
    streaming_manager.startStreaming();
    
    // 解码与推理线程
    for (int i = 0; i < manager.num_stream; ++i)
    {
        manager.load_stream(i);
        // 不同模型绑定不同 NPU 核心，使 ThreadPool 内 person/helmet/callplay 可并行推理
        rknn_lite *ptr1 = new rknn_lite(model_person, 0, 1, 0);
        rknn_lite *ptr2 = new rknn_lite(model_helmet, 1, 2, 1);
        rknn_lite *ptr3 = nullptr;
        rknn_lite *ptr4 = new rknn_lite(model_callplay, 2, 2, 3);
        rk_threads.push_back(std::thread(rknn_infer, ptr1, ptr2, ptr3, ptr4, i));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::thread readerThread(combineImage, std::ref(manager));
    readerThread.join();

    // 停止推流
    streaming_manager.stopStreaming();
    
    for (int i = 0; i < manager.num_stream; ++i)
    {
        manager.unload_stream(i);
    }
    for (auto &t : rk_threads)
    {
        if (t.joinable())
            t.join();
    }

    return 0;
}

