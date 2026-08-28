/*
 * Copyright (c) 2025-04-01 HeXiaotian
 *
 * This source code is licensed for learning and research purposes only.
 * Commercial use, redistribution, resale, and creation of derivative works
 * are strictly prohibited without prior written permission from the author.
 */

#include <stdio.h>
#include <sys/time.h>
#include <thread>
#include <queue>
#include <vector>
#define _BASETSD_H

#include "opencv2/core/core.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "rknnPool.hpp"
#include "ThreadPool.hpp"
#include "mpp_decoder.h"
#include "stream_loader.h"
#include <mutex>
#include <utility>
#include <chrono>
using std::queue;
using std::time;
using std::time_t;
using std::vector;

static StreamLoader *loader;
queue<cv::Mat> *queue_frame = new queue<cv::Mat>();
std::mutex mtx;

// 主函数入口
int main(int argc, char **argv)
{
  // 模型文件路径
  char *model_name = NULL;
  // 检查命令行参数，确保输入了模型路径和视频路径
  if (argc != 3 && argc != 2)
  {
    printf("Usage: %s <rknn model> <jpg> \n", argv[0]);
    return -1;
  }
  // 参数二为RKNN模型路径
  model_name = (char *)argv[1];
  // 参数三为视频路径或摄像头编号
  char image_name[100];
  // char *url105 = "rtsp://admin:jhx12345@192.168.1.220:554/Streaming/Channels/101";
  sprintf(image_name, "rtsp://admin:jhx12345@192.168.1.%s:554/Streaming/Channels/101", argv[2]);
  // image_name = argc >= 3 ? argv[2] : url105;
  printf("模型名称:\t%s\n", model_name);

  // // 创建VideoCapture对象，用于捕获视频帧
  // cv::VideoCapture capture;
  // cv::namedWindow("Camera FPS");
  // // 判断是摄像头输入还是视频文件输入
  // if (strlen(image_name) == 1)
  //   capture.open((int)(image_name[0] - '0')); // 使用摄像头
  // else
  //   capture.open(image_name); // 使用视频文件

  // 设置线程数量和初始化帧计数
  int n = 1, frames = 0;
  printf("线程数:\t%d\n", n);

  // if (capture.isOpened())
  // {
  //   cv::Mat frame;
  //   capture >> frame;
  //   printf("视频宽度:\t%d\n", frame.cols);
  //   printf("视频高度:\t%d\n", frame.rows);
  // }

  printf("-------------------\n");
  loader = new StreamLoader(image_name, 999);
  loader->open();
  std::thread read_frame_thread(std::ref(*loader));
  read_frame_thread.detach();
  using namespace std::chrono_literals;
  std::this_thread::sleep_for(1000ms);
  // printf("size :%d", queue_frame->size());
  printf("-------------------\n");
  // 创建RKNN模型的集合，用于存储多个模型实例
  vector<rknn_lite *> rkpool;
  // 创建线程池对象，使用n个线程
  dpool::ThreadPool pool(n);
  // 线程任务队列，用于存储异步任务的返回结果
  queue<std::future<int>> futs;
  // 初始化模型和线程任务
  for (int i = 0; i < n; i++)
  {
    // 创建一个rknn_lite实例，并传入模型路径和配置参数
    rknn_lite *ptr = new rknn_lite(model_name, i % 3);
    rkpool.push_back(ptr);
    // 读取第一帧图像
    std::unique_lock<std::mutex> lock(mtx); // 获取互斥锁
    // capture >> ptr->ori_img;
    ptr->ori_img = std::move(queue_frame->front());
    // ptr->ori_img = queue_frame->front();
    queue_frame->pop();
    lock.unlock(); // 释放互斥锁
    // 将推理任务提交到线程池中，并将任务结果存入队列
    futs.push(pool.submit(&rknn_lite::interf, &(*ptr)));
  }

  // 获取程序开始运行时的时间，用于计算帧率
  struct timeval time;
  gettimeofday(&time, nullptr);
  auto initTime = time.tv_sec * 1000 + time.tv_usec / 1000;

  // 初始化循环内的时间戳
  gettimeofday(&time, nullptr);
  long tmpTime, lopTime = time.tv_sec * 1000 + time.tv_usec / 1000;

  // 主循环，持续处理视频帧
  // while (capture.isOpened())
  while (1)
  {
    // 检查队列中任务是否完成
    if (futs.front().get() != 0)
      break;    // 如果推理任务失败，退出循环
    futs.pop(); // 移除已完成的任务

    // 显示当前处理的图像帧
    cv::Mat resize;
    cv::resize(rkpool[frames % n]->ori_img, resize, cv::Size(), 0.5, 0.5);
    cv::imshow("Camera FPS", resize);
    if (cv::waitKey(1) == 'q') // 每次延迟1毫秒，按下q键退出
      break;

    // 读取下一帧图像，放入模型中进行处理
    // if (!capture.read(rkpool[frames % n]->ori_img))
    //   break; // 如果读取失败，退出循环
    std::unique_lock<std::mutex> lock(mtx); // 获取互斥锁
    // capture >> rkpool[frames % n]->ori_img;
    using namespace std::chrono_literals;
    printf("size :%d\n", queue_frame->size());
    if (queue_frame->size() < 10)
    {
      std::this_thread::sleep_for(50ms);
    }
    // rkpool[frames % n]->ori_img.release();
    // rkpool[frames % n]->ori_img = queue_frame->front();
    rkpool[frames % n]->ori_img = std::move(queue_frame->front());
    queue_frame->pop();
    lock.unlock(); // 释放互斥锁

    // // 提交新的推理任务到线程池，并将任务结果存入队列
    // futs.push(pool.submit(&rknn_lite::interf, &(*rkpool[frames++ % n])));
    futs.push(pool.submit(&rknn_lite::interf, &(*rkpool[frames % n])));
    // cv::imshow("output", rkpool[frames % n]->ori_img);
    frames++;

    // 每处理60帧，计算一次平均帧率
    if (frames % 60 == 0)
    {
      gettimeofday(&time, nullptr);
      tmpTime = time.tv_sec * 1000 + time.tv_usec / 1000;
      printf("60帧平均帧率:\t%f帧\n", 60000.0 / (float)(tmpTime - lopTime));
      lopTime = tmpTime;
    }
  }

  // 获取程序结束时的时间，并计算总的平均帧率
  gettimeofday(&time, nullptr);
  printf("\n平均帧率:\t%f帧\n", float(frames) / (float)(time.tv_sec * 1000 + time.tv_usec / 1000 - initTime + 0.0001) * 1000.0);

  // 在退出前，等待所有剩余的任务完成
  while (!futs.empty())
  {
    if (futs.front().get())
      break;
    futs.pop();
  }

  // 释放所有模型实例的内存
  for (int i = 0; i < n; i++)
    delete rkpool[i];
  // capture.release();       // 释放视频捕获资源
  cv::destroyAllWindows(); // 销毁所有窗口

  return 0;
}
