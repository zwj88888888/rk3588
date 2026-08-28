/*
 * Copyright (c) 2025-04-01 HeXiaotian
 *
 * This source code is licensed for learning and research purposes only.
 * Commercial use, redistribution, resale, and creation of derivative works
 * are strictly prohibited without prior written permission from the author.
 */

#pragma once
extern "C"
{
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
}
#include "mpp_decoder.h"
#include <opencv2/opencv.hpp>
#include <thread>
#include <atomic>
#include <functional>
#include <queue>
#include <vector>
#include <mutex>
#include <condition_variable>
using std::queue;
using std::vector;
#include "m_buffer.h"

#define display 0

// extern queue<cv::Mat> *queue_frame;
// extern std::mutex mtx;

using MppDecoderFrameCallback = std::function<void(void *userdata, int width_stride, int height_stride, int width, int height, int format, int fd, void *data, int id)>;

class StreamLoader
{
public:
    std::string windowsName;
    MppDecoder decoder;
    // 视频流的数据起始地址的索引
    int videoStreamIndex;
    AVDictionary *options = NULL;
    AVFormatContext *fmtCtx = NULL;
    AVCodecParameters *codecPar = NULL;

    AVBSFContext *bsf_ctx = NULL;
    const AVBitStreamFilter *bsf;

    // 是否已经读取到关键帧
    bool got_key_frame = false;
    // 存储接受流数据的临时结构，内存在构造函数中申请
    AVPacket *temp_pkt;
    // 当前包的编号
    int current_pkt_id = 0;
    // 当前对象的唯一标识号
    int stream_loader_id;
    // 流地址
    char *stream_url = nullptr;
    int width = 0;
    int height = 0;
    int status = 0;
    bool isnotAnnexB = false;
    MppDecoderFrameCallback callback;
    // 保存解码后的图像数据
    // cv::Mat *mat_ptr;
    Mbuffer buffer;

    // 保存几秒视频的队列
    queue<cv::Mat> *queue_frame;
    const int MAX_FRAMES = 150;
    // std::mutex mtx_mat;
    std::atomic<bool> stopFlag;
    std::atomic<bool> inferFlag;
    // std::condition_variable condition;
    // public:
    void close();
    bool read_frame();
    StreamLoader(char *url, int id);
    ~StreamLoader();
    int open();
    void operator()();
    void update_queue();
};

class StreamLoaderManager
{
public:
    // -----------------------------------------------

    char *url105 = "rtsp://admin:jhx12345@192.168.1.105:554/Streaming/Channels/101";
    char *url104 = "rtsp://admin:jhx12345@192.168.1.104:554/Streaming/Channels/101";
    char *url220 = "rtsp://admin:jhx12345@192.168.1.220:554/Streaming/Channels/101";
    vector<char *> urls = {url105, url104, url105, url104, url105, url104};
    // vector<char *> urls = {url105, url104, url220, url105, url104, url220};
    int num_stream = 4;
    // -----------------------------------------------
    // 禁止拷贝构造和赋值操作
    StreamLoaderManager(const StreamLoaderManager &) = delete;
    StreamLoaderManager &operator=(const StreamLoaderManager &) = delete;

    // 获取单例实例的静态方法
    static StreamLoaderManager &getInstance()
    {
        static StreamLoaderManager instance; // C++11 保证了静态局部变量的线程安全性
        return instance;
    }

    // 加载流
    void load_stream(int id);
    // 停止流
    void unload_stream(int id);

    vector<StreamLoader *> stream_loaders;
    vector<std::thread> threads;

private:
    // 私有构造函数，防止从外部创建对象
    StreamLoaderManager()
    {
        std::cout << "StreamLoaderManager created" << std::endl;
    }

    // 私有析构函数，防止外部删除对象
    ~StreamLoaderManager()
    {
        std::cout << "StreamLoaderManager destroyed" << std::endl;
    }

    std::mutex mutex_; // 保护共享资源的互斥锁
};