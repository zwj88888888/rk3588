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
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavutil/imgutils.h>
#include <libavutil/rational.h>
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
#include "m_buffer.hpp"


using MppDecoderFrameCallback = std::function<void(void *userdata, int width_stride, int height_stride, int width, int height, int format, int fd, void *data, int id)>;


class StreamLoader
{
public:
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

    // 管理图像数据
    Mbuffer buffer;

    std::atomic<bool> stopFlag;

    // 本地文件播放时按源视频帧率限速，避免倍速
    double source_fps_ = 25.0;
    bool is_local_file_ = false;

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
    // 本地测试用例
    // char *url105 = "rtsp://admin:jhx12345@192.168.1.105:554/Streaming/Channels/101";
    // char *url104 = "rtsp://admin:jhx12345@192.168.1.104:554/Streaming/Channels/101";
    // vector<char *> urls = {url105, url104, url105, url104, url105, url104};
    char* url1= "../../1.mp4";
    char* url2= "../../2.mp4";
    char* url3= "../../3.mp4";
    char* url4= "../../4.mp4";
    vector<char *> urls = {url1, url2, url3, url4, url2, url3};
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
    // 该函数没有使用
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
};