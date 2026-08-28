/*
 * Copyright (c) 2025-04-01 HeXiaotian
 *
 * This source code is licensed for learning and research purposes only.
 * Commercial use, redistribution, resale, and creation of derivative works
 * are strictly prohibited without prior written permission from the author.
 */

#include <iostream>
#include "stream_loader.h"
#include "rknnPool.hpp"
std::atomic<bool> stop(false);
char *model_name = "../model/RK3588/person_relu.rknn";
StreamLoaderManager &manager = StreamLoaderManager::getInstance();
// 创建RKNN模型的集合，用于存储多个模型实例
vector<rknn_lite *> rk_pool;
// 创建线程池对象，使用n个线程
vector<std::thread> rk_threads;
vector<cv::Mat> images(6);
vector<std::mutex> mutexes(6);
void combineImage(StreamLoaderManager &manager)
{
    cv::Mat combinedImage(1080, 1280, CV_8UC3, cv::Scalar(0, 0, 0)); // 初始化为黑色
    while (true)
    {
        // 将每张图像调整为640x360，并将其放置在合适的位置
        for (int i = 0; i < manager.num_stream; ++i)
        {
            cv::Mat resizedImage;
            // 当前没有推理完成的图像
            std::unique_lock<std::mutex> lock(mutexes[i]);
            // std::unique_lock<std::mutex> lock(manager.stream_loaders[i]->buffer.mtx);
            // if (manager.stream_loaders[i]->buffer.img.empty()){
            // if (rk_pool[i]->ori_img.empty()){
            if (images[i].empty())
            {
                std::cerr << "Error: combine image rk_pool[" << i << "]->buffer.img is empty!" << std::endl;
                continue;
            }
            // 调整图像大小为640x360
            cv::resize(images[i], resizedImage, cv::Size(640, 360));
            lock.unlock();

            // 计算图像在合成图像中的位置
            int row = i / 2;   // 行号
            int col = i % 2;   // 列号
            int x = col * 640; // x坐标
            int y = row * 360; // y坐标

            // 将调整大小后的图像复制到合成图像的相应位置
            resizedImage.copyTo(combinedImage(cv::Rect(x, y, 640, 360)));
        }
        // 显示合成图像
        cv::imshow("Combined Image", combinedImage);
        // 等待按键
        if (cv::waitKey(10) == 27)
            break;
    }
    cv::destroyAllWindows(); // 销毁所有窗口
}

void rknn_infer(rknn_lite *ptr, int i)
{
    // int count = 0;
    while (!stop)
    {
        // if (!ptr->ori_img.empty())
        // {
        // 使用std::unique_lock来锁定互斥锁
        std::unique_lock<std::mutex> lock(manager.stream_loaders[i]->buffer.mtx);
        if (!manager.stream_loaders[i]->buffer.img.empty())
        {
            ptr->ori_img = manager.stream_loaders[i]->buffer.img.clone();
        }
        else
        {
            std::cerr << "Error: manager.stream_loaders[" << i << "]->buffer.img is empty!" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
        lock.unlock();
        // count = (count + 1) % 3;
        // if (count == 0)
            ptr->interf();
        std::unique_lock<std::mutex> lockimage(mutexes[i]);
        images[i] = std::move(ptr->ori_img);
        lockimage.unlock();
        // }
        // else
        // {
        //     break;
        // }
    }
}

int main(int argc, char *argv[])
{
    manager.num_stream = std::stoi(argv[1]); // 获取输入视频流数量
    // 解码与推理线程开启
    for (int i = 0; i < manager.num_stream; ++i)
    {
        // 解码线程
        manager.load_stream(i);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        // 推理线程
        // 创建一个rknn_lite实例，并传入模型路径和配置参数
        rknn_lite *ptr = new rknn_lite(model_name, i % 3);
        rk_pool.push_back(ptr);
        ptr->ori_img = manager.stream_loaders[i]->buffer.img.clone();
        rk_threads.push_back(std::thread(rknn_infer, &(*ptr), i));
    }
    std::thread readerThread(combineImage, std::ref(manager));
    readerThread.join();
    // for (int i = 0; i < manager.num_stream; ++i)
    // {
    //     manager.unload_stream(i);
    // }
    // stop = true;
    for (auto &t : manager.threads)
    {
        if (t.joinable())
            t.join();
    }
    // for (auto &t : rk_threads)
    // {
    //     if (t.joinable())
    //         t.join();
    // }
    // rk_pool.clear();
    return 0;
}
