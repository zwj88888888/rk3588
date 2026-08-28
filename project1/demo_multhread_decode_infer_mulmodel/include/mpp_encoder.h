/*
 * Copyright (c) 2025-04-01 HeXiaotian
 *
 * This source code is licensed for learning and research purposes only.
 * Commercial use, redistribution, resale, and creation of derivative works
 * are strictly prohibited without prior written permission from the author.
 */

#ifndef MPP_ENCODER_H
#define MPP_ENCODER_H

#include <stdint.h>
#include "rk_mpi.h"
#include "rk_venc_cfg.h"



class MppEncoder {
public:
    MppEncoder();
    ~MppEncoder();

    // 初始化编码器
    // width/height: 编码分辨率
    // fps:          帧率
    // bitrate:      码率 (bps)
    // codec_type:   264(H.264) / 265(H.265)
    int Init(int width, int height, int fps, int bitrate, int codec_type = 264);

    // 直接编码一帧 YUV420P 数据
    int Encode(uint8_t* yuv_data, int yuv_size, uint8_t* packet_data, int* packet_size);

    // 编码一帧 BGR 图像（使用 RGA 做 BGR->YUV420P，失败时回退 OpenCV）
    // bgr_stride: 可选，BGR 行字节步长；0 表示 width*3
    int EncodeFrame(uint8_t* bgr_data, int width, int height, uint8_t* packet_data, int* packet_size, int bgr_stride = 0);

    // 获取 SPS / PPS 等头信息（可用于 FFmpeg extradata）
    int GetHeader(uint8_t* header_data, int* header_size);

    // 释放资源
    void Release();

private:
    MppCtx          mpp_ctx_;
    MppApi*         mpp_mpi_;
    MppEncCfg       enc_cfg_;
    MppFrame        frame_;
    MppPacket       packet_;
    MppBufferGroup  frm_grp_;
    MppBufferGroup  pkt_grp_;

    int             width_;
    int             height_;
    int             fps_;
    int             bitrate_;
    MppCodingType   mpp_type_;
    bool            initialized_;

    // 预分配的 YUV 缓冲区，避免每帧 malloc/free
    uint8_t*        yuv_buffer_;
    int             yuv_buffer_size_;
};

#endif // MPP_ENCODER_H

