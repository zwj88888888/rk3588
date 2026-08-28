/*
 * Copyright (c) 2025-04-01 HeXiaotian
 *
 * This source code is licensed for learning and research purposes only.
 * Commercial use, redistribution, resale, and creation of derivative works
 * are strictly prohibited without prior written permission from the author.
 */

#include "stream_loader.h"

int is_annexb(const uint8_t *buf, size_t buf_size)
{
    // Annex B 格式以 0x000001 或 0x00000001 开头
    if (buf_size >= 4)
    {
        if ((buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0x01) ||
            (buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0x00 && buf[3] == 0x01))
        {
            return 1; // 是 Annex B 格式
        }
    }
    return 0; // 不是 Annex B 格式
}

void mpp_decoder_frame_callback(void *buffer, int width_stride, int height_stride, int width, int height, int format, int fd, void *data, int id)
{
    Mbuffer *mbuffer = (Mbuffer *)buffer;
    size_t yuv_size = width_stride * height_stride * 3 / 2;
    uint8_t *yuv_data = (uint8_t *)malloc(yuv_size);

    if (yuv_data == nullptr)
    {
        std::cerr << "YUV数据分配失败！" << std::endl;
        return;
    }

    uint8_t *base_y = (uint8_t *)data;
    uint8_t *base_c = base_y + width_stride * height_stride;
    int idx = 0;

    for (int i = 0; i < height; i++, base_y += width_stride)
    {
        memcpy(yuv_data + idx, base_y, width);
        idx += width;
    }

    for (int i = 0; i < height / 2; i++, base_c += width_stride)
    {
        memcpy(yuv_data + idx, base_c, width);
        idx += width;
    }

    std::shared_ptr<uint8_t> yuv_ptr(yuv_data, free);
    cv::Mat yuvMat(height + height / 2, width, CV_8UC1, yuv_ptr.get());

    if (yuvMat.empty())
    {
        std::cerr << "YUV Mat 为空！无法进行颜色转换。" << std::endl;
        return;
    }

    cv::Mat bgrMat;
    cv::cvtColor(yuvMat, bgrMat, cv::COLOR_YUV2BGR_NV12);

    if (bgrMat.empty())
    {
        std::cerr << "颜色转换失败，输出的BGR Mat为空！" << std::endl;
        return;
    }

    std::unique_lock<std::mutex> mlock(mbuffer->mtx);
    mbuffer->img = std::move(bgrMat);
    mlock.unlock();
}

void StreamLoader::close()
{
    decoder.Reset();
    if (temp_pkt)
    {
        av_packet_free(&temp_pkt); // 释放 temp_pkt 并将指针置为 nullptr
    }

    if (fmtCtx)
    {
        avformat_close_input(&fmtCtx); // 关闭输入流
        fmtCtx = nullptr;              // 确保指针在关闭后被设置为 nullptr
    }

    if (codecPar)
    {
        avcodec_parameters_free(&codecPar); // 释放 codecPar 结构
    }
}

bool StreamLoader::read_frame()
{
    using namespace std::chrono_literals;
    int retry_times = 0;
    do
    {
        // 从流中取一帧的数据，存储到temp_pkt
        int x = av_read_frame(fmtCtx, temp_pkt);
        if (x < 0)
        {
            std::cerr << "av_read_frame 失败 "<< retry_times << std::endl;
            status = x;
            std::this_thread::sleep_for(2ms);
            av_packet_unref(temp_pkt);
            continue;
        }
        if (temp_pkt->stream_index == videoStreamIndex)
        {
            if (isnotAnnexB)
            {
                int ret = av_bsf_send_packet(bsf_ctx, temp_pkt);
                if (ret < 0)
                {
                    fprintf(stderr, "Error sending packet to filter\n");
                    break;
                }
                ret = av_bsf_receive_packet(bsf_ctx, temp_pkt);
                if (ret < 0)
                {
                    fprintf(stderr, "Error receiving packet from filter\n");
                    break;
                }
            }
            // std::cout << "video" << std::endl;

            // 传递视频包给解码器
            bool decode_success = decoder.Decode(temp_pkt->data, temp_pkt->size, 0);

            // 释放帧数据包
            av_packet_unref(temp_pkt);

            if (decode_success)
            {
                status = 0;
                return true;
            }
            else
            {
                std::this_thread::sleep_for(2ms);
            }
        }
        else
        {
            // 如果检测到是音频包，可以处理音频包或丢弃
            // std::cout << "audio" << std::endl;

            // 音频包的处理或丢弃
            av_packet_unref(temp_pkt);
            continue;
        }
    } while (++retry_times < 10);
    return false;
}

StreamLoader::StreamLoader(char *url, int id)
{
    stream_loader_id = id;
    windowsName = std::to_string(id);
    std::cout << "StreamLoader: " << windowsName << std::endl;
    callback = mpp_decoder_frame_callback;
    // mat_ptr = new cv::Mat();

    stream_url = url;
    status = 0;
    stopFlag = false;
    inferFlag = false;
}

StreamLoader::~StreamLoader()
{
    std::cout << "destory stream loader: " << stream_loader_id << std::endl;
    close();
    // delete mat_ptr;
}

int StreamLoader::open()
{
    temp_pkt = av_packet_alloc();
    av_init_packet(temp_pkt);
    codecPar = avcodec_parameters_alloc();
    // pFrame = av_frame_alloc();
    // temp_frame = av_frame_alloc();
    av_dict_set(&options, "rtbufsize", "8192000", 0);
    av_dict_set(&options, "start_time_realtime", 0, 0);
    av_dict_set(&options, "rtsp_transport", "tcp", 0);
    av_dict_set(&options, "stimeout", "2000000", 0);
    av_dict_set(&options, "max_delay", "500000", 0);

    // 打开RTSP流
    if (avformat_open_input(&fmtCtx, stream_url, NULL, &options) != 0)
    {
        std::cout << "open rtsp stream failed" << std::endl;
        return -1;
    }
    // 查找RTSP流信息
    if (avformat_find_stream_info(fmtCtx, NULL) < 0)
    {
        return -1;
    }

    // 打印视频相关信息
    av_dump_format(fmtCtx, 0, stream_url, 0);
    // 获取视频的信息
    videoStreamIndex = -1;
    for (unsigned int i = 0; i < fmtCtx->nb_streams; i++)
    {
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            width = fmtCtx->streams[i]->codecpar->width;
            height = fmtCtx->streams[i]->codecpar->height;
            videoStreamIndex = i;
            break;
        }
    }
    std::cout << "videoindex: " << videoStreamIndex << std::endl;
    // std::cout << "width: " << width << ", height: " << height << std::endl;
    if (videoStreamIndex < 0)
    {
        return -2;
    }

    AVCodecID rtsp_format = fmtCtx->streams[videoStreamIndex]->codecpar->codec_id;
    if (status == 0)
    {
        int ret = 0;
        // void *src_buffer = this->mat_ptr;
        void *src_buffer = &(this->buffer);
        switch (rtsp_format)
        {
        case AV_CODEC_ID_H264:
            ret = decoder.Init(264, 25, src_buffer, stream_loader_id);
            // ----------------------------------------------------------
            // 查找H.264比特流过滤器
            bsf = av_bsf_get_by_name("h264_mp4toannexb");
            if (!bsf)
            {
                fprintf(stderr, "Could not find h264_mp4toannexb filter\n");
                avformat_close_input(&fmtCtx);
                return -3;
            }

            // 初始化比特流过滤器上下文
            if (av_bsf_alloc(bsf, &bsf_ctx) < 0)
            {
                fprintf(stderr, "Could not allocate bsf context\n");
                avformat_close_input(&fmtCtx);
                return -3;
            }
            // 设置过滤器参数
            avcodec_parameters_copy(bsf_ctx->par_in, fmtCtx->streams[0]->codecpar);
            bsf_ctx->time_base_in = fmtCtx->streams[0]->time_base;

            if (av_bsf_init(bsf_ctx) < 0)
            {
                fprintf(stderr, "Could not initialize bsf context\n");
                av_bsf_free(&bsf_ctx);
                avformat_close_input(&fmtCtx);
                return -3;
            }
            isnotAnnexB = true;
            // ----------------------------------------------------------
            std::cout << "H264 " << ret << std::endl;
            break;
        case AV_CODEC_ID_HEVC:
            ret = decoder.Init(265, 25, src_buffer, stream_loader_id);
            std::cout << "HEVC " << ret << std::endl;
            break;
        }
    }

    decoder.SetCallback(this->callback);
    avcodec_parameters_copy(codecPar, fmtCtx->streams[videoStreamIndex]->codecpar);
    return 0;
}

// void StreamLoader::update_queue()
// {
//     if (queue_frame->size() < MAX_FRAMES)
//     {
//         queue_frame->push((*mat_ptr).clone());
//     }
//     else
//     {
//         queue_frame->pop();
//         queue_frame->push((*mat_ptr).clone());
//     }
// }

void StreamLoader::operator()()
{
    while (stopFlag == false)
    {
#if display
        if (read_frame())
        {
            if (!(*mat_ptr).empty())
            {
                cv::imshow(windowsName, (*mat_ptr));
                if (cv::waitKey(1) == 27)
                {
                    break;
                }
            }
        }
#else
        try
        {
            if (read_frame())
            {
                // update_queue();
                // std::cout<< "read frame success " << stream_loader_id << std::endl;
            }
            else
            {
                std::cout << "read frame error " << stream_loader_id << std::endl;
                // av_usleep(1000 * 100);
            }
        }
        catch (std::exception &e)
        {
            std::cout << "exception ............" << std::endl;
            std::cout << e.what() << std::endl;
        }
#endif
        if (status)
        {
            std::cout << "\nstatus: " << status << std::endl;
            std::cout << "Reconnecting " << stream_loader_id << std::endl;
            close();
            // 如果打开流失败，则10s后重新打开
            while (open() != 0)
            {
                std::cout << "Reconnect failed" << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(10000));
            }
            status = 0;
        }
    }
    // std::cout << "StreamLoader: " << windowsName << " exit" << std::endl;
}

// ==================================================================================================

void StreamLoaderManager::load_stream(int id)
{
    // std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "Loading stream id: " << id << std::endl;
    StreamLoader *loader = new StreamLoader(urls[id], id);
    loader->open();
    stream_loaders.push_back(loader);
    threads.emplace_back(std::thread(std::ref(*loader)));
}

// 卸载流
void StreamLoaderManager::unload_stream(int id)
{
    // std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "Unloading stream id: " << id << std::endl;
    stream_loaders[id]->stopFlag = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    delete stream_loaders[id];
}