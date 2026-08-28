/*
 * Copyright (c) 2025-04-01 HeXiaotian
 *
 * This source code is licensed for learning and research purposes only.
 * Commercial use, redistribution, resale, and creation of derivative works
 * are strictly prohibited without prior written permission from the author.
 */

#include "mpp_encoder.h"
#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include "im2d.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <thread>
#include <chrono>

MppEncoder::MppEncoder()
    : mpp_ctx_(NULL), mpp_mpi_(NULL), enc_cfg_(NULL),
      frame_(NULL), packet_(NULL), frm_grp_(NULL), pkt_grp_(NULL),
      width_(0), height_(0), fps_(0), bitrate_(0),
      mpp_type_(MPP_VIDEO_CodingAVC), initialized_(false),
      yuv_buffer_(NULL), yuv_buffer_size_(0) {
}

MppEncoder::~MppEncoder() {
    Release();
}

int MppEncoder::Init(int width, int height, int fps, int bitrate, int codec_type) {
    MPP_RET ret = MPP_OK;

    width_ = width;
    height_ = height;
    fps_ = fps;
    bitrate_ = bitrate;

    if (codec_type == 264) {
        mpp_type_ = MPP_VIDEO_CodingAVC;
    } else if (codec_type == 265) {
        mpp_type_ = MPP_VIDEO_CodingHEVC;
    } else {
        fprintf(stderr, "Unsupported codec type: %d\n", codec_type);
        return -1;
    }

    // 创建 MPP 上下文
    ret = mpp_create(&mpp_ctx_, &mpp_mpi_);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpp_create failed ret %d\n", ret);
        return -1;
    }

    // 初始化编码器
    ret = mpp_init(mpp_ctx_, MPP_CTX_ENC, mpp_type_);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpp_init failed ret %d\n", ret);
        mpp_destroy(mpp_ctx_);
        mpp_ctx_ = NULL;
        return -1;
    }

    // 创建编码配置
    ret = mpp_enc_cfg_init(&enc_cfg_);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpp_enc_cfg_init failed ret %d\n", ret);
        goto FAIL;
    }

    // 获取默认配置
    ret = mpp_mpi_->control(mpp_ctx_, MPP_ENC_GET_CFG, enc_cfg_);
    if (ret != MPP_OK) {
        fprintf(stderr, "get enc cfg failed ret %d\n", ret);
        goto FAIL;
    }

    // 基本图像参数
    mpp_enc_cfg_set_s32(enc_cfg_, "prep:width", width);
    mpp_enc_cfg_set_s32(enc_cfg_, "prep:height", height);
    mpp_enc_cfg_set_s32(enc_cfg_, "prep:hor_stride", width);
    mpp_enc_cfg_set_s32(enc_cfg_, "prep:ver_stride", height);
    mpp_enc_cfg_set_s32(enc_cfg_, "prep:format", MPP_FMT_YUV420P);

    // 码率控制：简单的 VBR 设置，偏向速度
    mpp_enc_cfg_set_s32(enc_cfg_, "rc:mode", MPP_ENC_RC_MODE_VBR);
    mpp_enc_cfg_set_s32(enc_cfg_, "rc:bps_target", bitrate);
    mpp_enc_cfg_set_s32(enc_cfg_, "rc:bps_max", bitrate * 2);
    mpp_enc_cfg_set_s32(enc_cfg_, "rc:bps_min", bitrate / 2);
    mpp_enc_cfg_set_s32(enc_cfg_, "rc:fps_in_flex", 0);
    mpp_enc_cfg_set_s32(enc_cfg_, "rc:fps_in_num", fps);
    mpp_enc_cfg_set_s32(enc_cfg_, "rc:fps_in_denom", 1);
    mpp_enc_cfg_set_s32(enc_cfg_, "rc:fps_out_flex", 0);
    mpp_enc_cfg_set_s32(enc_cfg_, "rc:fps_out_num", fps);
    mpp_enc_cfg_set_s32(enc_cfg_, "rc:fps_out_denom", 1);
    mpp_enc_cfg_set_s32(enc_cfg_, "rc:gop", fps);

    if (mpp_type_ == MPP_VIDEO_CodingAVC) {
        mpp_enc_cfg_set_s32(enc_cfg_, "codec:type", MPP_VIDEO_CodingAVC);
        mpp_enc_cfg_set_s32(enc_cfg_, "h264:profile", 66);
        mpp_enc_cfg_set_s32(enc_cfg_, "h264:level", 40);
        mpp_enc_cfg_set_s32(enc_cfg_, "h264:cabac_en", 0);
        mpp_enc_cfg_set_s32(enc_cfg_, "h264:qp_init", 26);
        mpp_enc_cfg_set_s32(enc_cfg_, "h264:qp_min", 20);
        mpp_enc_cfg_set_s32(enc_cfg_, "h264:qp_max", 35);
    } else if (mpp_type_ == MPP_VIDEO_CodingHEVC) {
        mpp_enc_cfg_set_s32(enc_cfg_, "codec:type", MPP_VIDEO_CodingHEVC);
        mpp_enc_cfg_set_s32(enc_cfg_, "h265:profile", 1);
        mpp_enc_cfg_set_s32(enc_cfg_, "h265:level", 120);
    }

    // 应用配置
    ret = mpp_mpi_->control(mpp_ctx_, MPP_ENC_SET_CFG, enc_cfg_);
    if (ret != MPP_OK) {
        fprintf(stderr, "set enc cfg failed ret %d\n", ret);
        goto FAIL;
    }

    // 输入/输出缓冲区组
    ret = mpp_buffer_group_get_internal(&frm_grp_, MPP_BUFFER_TYPE_ION);
    if (ret != MPP_OK) {
        fprintf(stderr, "failed to get buffer group for input frame ret %d\n", ret);
        goto FAIL;
    }

    ret = mpp_buffer_group_get_internal(&pkt_grp_, MPP_BUFFER_TYPE_ION);
    if (ret != MPP_OK) {
        fprintf(stderr, "failed to get buffer group for output packet ret %d\n", ret);
        goto FAIL;
    }

    // 预分配 YUV 缓冲区
    yuv_buffer_size_ = width * height * 3 / 2;
    yuv_buffer_ = (uint8_t*)malloc(yuv_buffer_size_);
    if (!yuv_buffer_) {
        fprintf(stderr, "failed to allocate YUV buffer (%d bytes)\n", yuv_buffer_size_);
        goto FAIL;
    }

    initialized_ = true;
    return 0;

FAIL:
    if (yuv_buffer_) {
        free(yuv_buffer_);
        yuv_buffer_ = NULL;
        yuv_buffer_size_ = 0;
    }
    if (enc_cfg_) {
        mpp_enc_cfg_deinit(enc_cfg_);
        enc_cfg_ = NULL;
    }
    if (mpp_ctx_) {
        mpp_destroy(mpp_ctx_);
        mpp_ctx_ = NULL;
    }
    initialized_ = false;
    return -1;
}

int MppEncoder::EncodeFrame(uint8_t* bgr_data, int width, int height,
                            uint8_t* packet_data, int* packet_size, int bgr_stride) {
    if (!initialized_) {
        fprintf(stderr, "Encoder not initialized\n");
        return -1;
    }

    if (width != width_ || height != height_) {
        fprintf(stderr, "Frame size mismatch: %dx%d vs %dx%d\n",
                width, height, width_, height_);
        return -1;
    }

    if (!yuv_buffer_ || yuv_buffer_size_ < width * height * 3 / 2) {
        fprintf(stderr, "YUV buffer not allocated or too small\n");
        return -1;
    }

    // 使用 RGA 做 BGR -> YUV420P 转换（硬件加速）
    // wstride/hstride 为像素 stride；BGR 每像素 3 字节，故 wstride_pix = byte_stride/3
    int wstride_pix = (bgr_stride > 0) ? (bgr_stride / 3) : width;
    int hstride_pix = height;
    rga_buffer_t src_buf = wrapbuffer_virtualaddr_t((void*)bgr_data, width, height, wstride_pix, hstride_pix, RK_FORMAT_BGR_888);
    rga_buffer_t dst_buf = wrapbuffer_virtualaddr_t((void*)yuv_buffer_, width, height, width, height, RK_FORMAT_YCbCr_420_P);

    IM_STATUS status = imcvtcolor(src_buf, dst_buf,
                                  RK_FORMAT_BGR_888, RK_FORMAT_YCbCr_420_P,
                                  IM_COLOR_SPACE_DEFAULT);
    if (status != IM_STATUS_SUCCESS) {
        // RGA 失败时回退到 OpenCV 软件转换（常见原因：虚拟地址 DMA 限制、驱动版本等）
        static int fallback_count = 0;
        if (fallback_count++ < 3) {
            fprintf(stderr, "RGA color conversion failed (%d), using OpenCV fallback\n", (int)status);
        }
        int row_stride = (bgr_stride > 0) ? bgr_stride : (width * 3);
        cv::Mat bgr(height, width, CV_8UC3, bgr_data, row_stride);
        if (!bgr.isContinuous()) {
            bgr = bgr.clone();
        }
        cv::Mat yuv(height * 3 / 2, width, CV_8UC1, yuv_buffer_);
        cv::cvtColor(bgr, yuv, cv::COLOR_BGR2YUV_I420);
    }

    int yuv_size = width * height * 3 / 2;
    return Encode(yuv_buffer_, yuv_size, packet_data, packet_size);
}

int MppEncoder::Encode(uint8_t* yuv_data, int yuv_size, uint8_t* packet_data, int* packet_size) {
    if (!initialized_) {
        fprintf(stderr, "Encoder not initialized\n");
        return -1;
    }

    MPP_RET ret = MPP_OK;
    MppFrame frame = NULL;
    MppPacket packet = NULL;
    MppBuffer buffer = NULL;

    ret = mpp_frame_init(&frame);
    if (ret != MPP_OK) {
        fprintf(stderr, "mpp_frame_init failed\n");
        return -1;
    }

    int buf_size = width_ * height_ * 3 / 2;
    ret = mpp_buffer_get(frm_grp_, &buffer, buf_size);
    if (ret != MPP_OK) {
        fprintf(stderr, "failed to get buffer for input frame ret %d\n", ret);
        mpp_frame_deinit(&frame);
        return -1;
    }

    void* buf_ptr = mpp_buffer_get_ptr(buffer);
    if (!buf_ptr) {
        fprintf(stderr, "failed to get buffer pointer\n");
        mpp_buffer_put(buffer);
        mpp_frame_deinit(&frame);
        return -1;
    }
    memcpy(buf_ptr, yuv_data, yuv_size);

    mpp_frame_set_width(frame, width_);
    mpp_frame_set_height(frame, height_);
    mpp_frame_set_hor_stride(frame, width_);
    mpp_frame_set_ver_stride(frame, height_);
    mpp_frame_set_fmt(frame, MPP_FMT_YUV420P);
    mpp_frame_set_buffer(frame, buffer);
    mpp_frame_set_eos(frame, 0);

    ret = mpp_mpi_->encode_put_frame(mpp_ctx_, frame);
    if (ret != MPP_OK) {
        fprintf(stderr, "encode_put_frame failed ret %d\n", ret);
        mpp_buffer_put(buffer);
        mpp_frame_deinit(&frame);
        return -1;
    }

    // 轮询获取编码输出，避免帧在编码器内堆积导致后续卡顿
    const int max_retries = 50;
    for (int r = 0; r < max_retries; r++) {
        ret = mpp_mpi_->encode_get_packet(mpp_ctx_, &packet);
        if (ret == MPP_OK && packet) {
            break;
        }
        if (ret != MPP_ERR_TIMEOUT) {
            *packet_size = 0;
            mpp_buffer_put(buffer);
            mpp_frame_deinit(&frame);
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (ret != MPP_OK || !packet) {
        *packet_size = 0;
        mpp_buffer_put(buffer);
        mpp_frame_deinit(&frame);
        return 0;
    }

    void* pkt_data = mpp_packet_get_data(packet);
    size_t pkt_size = mpp_packet_get_length(packet);

    if (pkt_data && pkt_size > 0) {
        if (packet_data && *packet_size >= (int)pkt_size) {
            memcpy(packet_data, pkt_data, pkt_size);
            *packet_size = pkt_size;
        } else {
            *packet_size = 0;
            mpp_packet_deinit(&packet);
            mpp_buffer_put(buffer);
            mpp_frame_deinit(&frame);
            return -1;
        }
    } else {
        *packet_size = 0;
    }

    mpp_packet_deinit(&packet);
    mpp_buffer_put(buffer);
    mpp_frame_deinit(&frame);

    return 0;
}

int MppEncoder::GetHeader(uint8_t* header_data, int* header_size) {
    if (!initialized_) {
        fprintf(stderr, "Encoder not initialized\n");
        return -1;
    }

    MPP_RET ret = MPP_OK;
    MppPacket packet = NULL;

    ret = mpp_mpi_->control(mpp_ctx_, MPP_ENC_GET_EXTRA_INFO, &packet);
    if (ret != MPP_OK || !packet) {
        fprintf(stderr, "get extra info failed ret %d\n", ret);
        return -1;
    }

    void* pkt_data = mpp_packet_get_data(packet);
    size_t pkt_size = mpp_packet_get_length(packet);

    if (pkt_data && pkt_size > 0) {
        if (header_data && *header_size >= (int)pkt_size) {
            memcpy(header_data, pkt_data, pkt_size);
            *header_size = pkt_size;
            ret = MPP_OK;
        } else {
            fprintf(stderr, "Header buffer too small: %d < %zu\n", *header_size, pkt_size);
            *header_size = 0;
            ret = MPP_NOK;
        }
    } else {
        *header_size = 0;
        ret = MPP_NOK;
    }

    mpp_packet_deinit(&packet);
    return (ret == MPP_OK) ? 0 : -1;
}

void MppEncoder::Release() {
    if (enc_cfg_) {
        mpp_enc_cfg_deinit(enc_cfg_);
        enc_cfg_ = NULL;
    }

    if (frm_grp_) {
        mpp_buffer_group_put(frm_grp_);
        frm_grp_ = NULL;
    }

    if (pkt_grp_) {
        mpp_buffer_group_put(pkt_grp_);
        pkt_grp_ = NULL;
    }

    if (mpp_ctx_) {
        mpp_destroy(mpp_ctx_);
        mpp_ctx_ = NULL;
    }

    if (yuv_buffer_) {
        free(yuv_buffer_);
        yuv_buffer_ = NULL;
        yuv_buffer_size_ = 0;
    }

    initialized_ = false;
}

