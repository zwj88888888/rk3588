/*
 * Copyright (c) 2025-04-01 HeXiaotian
 *
 * This source code is licensed for learning and research purposes only.
 * Commercial use, redistribution, resale, and creation of derivative works
 * are strictly prohibited without prior written permission from the author.
 */

#pragma once
#include <cstring>
#include "mpp_frame.h"
#include "rk_mpi.h"
#include <functional>
#include <mutex>
#define FRAME_SYNC_TIME 0

// typedef void (*MppDecoderFrameCallback)(void* userdata, int width_stride, int height_stride, int width, int height, int format, int fd, void* data,int id);
using MppDecoderFrameCallback = std::function<void(void *userdata, int width_stride, int height_stride, int width, int height, int format, int fd, void *data, int id)>;

typedef struct
{
    MppCtx ctx;
    MppApi *mpi;
    RK_U32 eos;
    MppBufferGroup frm_grp;
    MppBufferGroup pkt_grp;
    MppPacket packet;
    MppFrame frame;
    size_t max_usage;
} MpiDecLoopData;

class MppDecoder
{
public:
    MppCtx mpp_ctx = NULL;
    MppApi *mpp_mpi = NULL;
    MppDecoder();
    ~MppDecoder();
    int Init(int video_type, int fps, void *userdata, int id);
    int SetCallback(MppDecoderFrameCallback callback);
    int Decode(uint8_t *pkt_data, int pkt_size, int pkt_eos);
    int Reset();

private:
    MppParam mpp_param1 = NULL;
    RK_U32 need_split = 1;
    RK_U32 width_mpp;
    RK_U32 height_mpp;
    MppCodingType mpp_type;
    size_t packet_size = 2400 * 1300 * 3 / 2;
    MpiDecLoopData loop_data;
    MppPacket packet = NULL;
    MppFrame frame = NULL;
    MppDecoderFrameCallback callback;
    int fps = -1;
    unsigned long last_frame_time_ms = 0;
    void *userdata = NULL;
    int id = 0;
    std::mutex mtx;
};
