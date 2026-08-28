/*
 * Copyright (c) 2025-04-01 HeXiaotian
 *
 * This source code is licensed for learning and research purposes only.
 * Commercial use, redistribution, resale, and creation of derivative works
 * are strictly prohibited without prior written permission from the author.
 */

#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#include <iostream>
#include "mpp_decoder.h"

MppDecoder::MppDecoder() {}

MppDecoder::~MppDecoder()
{
    if (loop_data.packet)
    {
        mpp_packet_deinit(&loop_data.packet);
        loop_data.packet = NULL;
    }
    if (frame)
    {
        mpp_frame_deinit(&frame);
        frame = NULL;
    }
    if (mpp_ctx)
    {
        mpp_destroy(mpp_ctx);
        mpp_ctx = NULL;
    }
    if (loop_data.frm_grp)
    {
        mpp_buffer_group_put(loop_data.frm_grp);
        loop_data.frm_grp = NULL;
    }
}

int MppDecoder::Init(int video_type, int fps, void *userdata,int id)
{
    this->id=id;
    MPP_RET ret = MPP_OK;
    this->userdata = userdata;
    this->fps = fps;
    this->last_frame_time_ms = 0;
    if (video_type == 264)
    {
        mpp_type = MPP_VIDEO_CodingAVC;
    }
    else if (video_type == 265)
    {
        mpp_type = MPP_VIDEO_CodingHEVC;
    }
    else
    {
        // printf("unsupport video_type %d", video_type);
        return -1;
    }
    // printf("mpi_dec_test start ");
    memset(&loop_data, 0, sizeof(loop_data));
    // printf("mpi_dec_test decoder test start mpp_type %d ", mpp_type);
    MppDecCfg cfg = NULL;
    MppCtx mpp_ctx = NULL;
    mpp_mpi = NULL;
    ret = mpp_create(&mpp_ctx, &mpp_mpi);
    if (MPP_OK != ret)
    {
        // printf("mpp_create failed ");
        return 0;
    }
    ret = mpp_init(mpp_ctx, MPP_CTX_DEC, mpp_type);
    if (ret)
    {
        // printf("%p mpp_init failed ", mpp_ctx);
        return -1;
    }
    mpp_dec_cfg_init(&cfg);
    /* get default config from decoder context */
    ret = mpp_mpi->control(mpp_ctx, MPP_DEC_GET_CFG, cfg);
    if (ret)
    {
        // printf("%p failed to get decoder cfg ret %d ", mpp_ctx, ret);
        return -1;
    }
    /*
     * split_parse is to enable mpp internal frame spliter when the input
     * packet is not aplited into frames.
     */
    ret = mpp_dec_cfg_set_u32(cfg, "base:split_parse", need_split);
    if (ret)
    {
        // printf("%p failed to set split_parse ret %d ", mpp_ctx, ret);
        return -1;
    }
    ret = mpp_mpi->control(mpp_ctx, MPP_DEC_SET_CFG, cfg);
    if (ret)
    {
        // printf("%p failed to set cfg %p ret %d ", mpp_ctx, cfg, ret);
        return -1;
    }
    mpp_dec_cfg_deinit(cfg);
    loop_data.ctx = mpp_ctx;
    loop_data.mpi = mpp_mpi;
    loop_data.eos = 0;
    loop_data.frame = NULL;
    return 1;
}

int MppDecoder::Reset()
{
    if (mpp_mpi != NULL)
    {
        mpp_mpi->reset(mpp_ctx);
    }
    return 0;
}

int MppDecoder::SetCallback(MppDecoderFrameCallback callback)
{
    this->callback = callback;
    return 0;
}

int MppDecoder::Decode(uint8_t *pkt_data, int pkt_size, int pkt_eos)
{
    MpiDecLoopData *data = &loop_data;
    RK_U32 pkt_done = 0;
    RK_U32 err_info = 0;
    MPP_RET ret = MPP_OK;
    MppCtx ctx = data->ctx;
    MppApi *mpi = data->mpi;
    int got_frames = 0;
    if (packet == NULL)
    {
        ret = mpp_packet_init(&packet, NULL, 0);
    }
    ///////////////////////////////////////////////
    mpp_packet_set_data(packet, pkt_data);
    mpp_packet_set_size(packet, pkt_size);
    mpp_packet_set_pos(packet, pkt_data);
    mpp_packet_set_length(packet, pkt_size);
    // setup eos flag
    if (pkt_eos)
        mpp_packet_set_eos(packet);
    do
    {
        RK_S32 times = 5;
        // send the packet first if packet is not done
        if (!pkt_done)
        {
            ret = mpi->decode_put_packet(ctx, packet);
            if (MPP_OK == ret)
                pkt_done = 1;
        }
        // then get all available frame and release
        do
        {
            RK_S32 get_frm = 0;
            RK_U32 frm_eos = 0;
        try_again:
            ret = mpi->decode_get_frame(ctx, &frame);
            // std::cout << "ret :" << ret << std::endl;
            if (MPP_ERR_TIMEOUT == ret)
            {
                if (times > 0)
                {
                    times--;
                    usleep(2000);
                    goto try_again;
                }
            }
            if (MPP_OK != ret)
            {
                // printf("decode_get_frame failed ret %d ", ret);
                break;
            }
            if (frame)
            {
                RK_U32 buf_size = mpp_frame_get_buf_size(frame);
                if (mpp_frame_get_info_change(frame))
                {
                    // printf("decode_get_frame get info changed found ");
                    if (NULL == data->frm_grp)
                    {
                        /* If buffer group is not set create one and limit it */
                        ret = mpp_buffer_group_get_internal(&data->frm_grp, MPP_BUFFER_TYPE_DRM);
                        if (ret)
                        {
                            // printf("%p get mpp buffer group failed ret %d ", ctx, ret);
                            break;
                        }
                        /* Set buffer to mpp decoder */
                        ret = mpi->control(ctx, MPP_DEC_SET_EXT_BUF_GROUP, data->frm_grp);
                        if (ret)
                        {
                            // printf("%p set buffer group failed ret %d ", ctx, ret);
                            break;
                        }
                    }
                    else
                    {
                        /* If old buffer group exist clear it */
                        ret = mpp_buffer_group_clear(data->frm_grp);
                        if (ret)
                        {
                            // printf("%p clear buffer group failed ret %d ", ctx, ret);
                            break;
                        }
                    }
                    /* Use limit config to limit buffer count to 24 with buf_size */
                    ret = mpp_buffer_group_limit_config(data->frm_grp, buf_size, 24);
                    if (ret)
                    {
                        // printf("%p limit buffer group failed ret %d ", ctx, ret);
                        break;
                    }
                    /*
                     * All buffer group config done. Set info change ready to let
                     * decoder continue decoding
                     */
                    ret = mpi->control(ctx, MPP_DEC_SET_INFO_CHANGE_READY, NULL);
                    if (ret)
                    {
                        // printf("%p info change ready failed ret %d ", ctx, ret);
                        break;
                    }

                }
                else
                {
                    // err_info = mpp_frame_get_errinfo(frame) | mpp_frame_get_discard(frame);
                    // if (err_info) {
                    //     // printf("decoder_get_frame get err info:%d discard:%d. ", mpp_frame_get_errinfo(frame), mpp_frame_get_discard(frame));
                    // }
                    RK_U32 hor_stride = mpp_frame_get_hor_stride(frame);
                    RK_U32 ver_stride = mpp_frame_get_ver_stride(frame);
                    RK_U32 hor_width = mpp_frame_get_width(frame);
                    RK_U32 ver_height = mpp_frame_get_height(frame);
                    RK_S64 pts = mpp_frame_get_pts(frame);
                    RK_S64 dts = mpp_frame_get_dts(frame);
                    // std::cout<<hor_width<<" "<<ver_height<<" "<<hor_stride<<" "<<ver_stride<<std::endl;
                    // // printf("decoder require buffer w:h [%d:%d] stride [%d:%d] buf_size %d pts=%lld dts=%lld ", hor_width, ver_height, hor_stride,
                    //      ver_stride, buf_size, pts, dts);
                    got_frames++;
                    if (callback != nullptr)
                    {
                        MppFrameFormat format = mpp_frame_get_fmt(frame);
                        char *data_vir = (char *)mpp_buffer_get_ptr(mpp_frame_get_buffer(frame));
                        callback(this->userdata, hor_stride, ver_stride, hor_width, ver_height, format, 0, data_vir,this->id);
                    }
                }
                frm_eos = mpp_frame_get_eos(frame);
                ret = mpp_frame_deinit(&frame);
                frame = NULL;
                get_frm = 1;
            }
            // if last packet is send but last frame is not found continue
            if (pkt_eos && pkt_done && !frm_eos)
            {
                usleep(1 * 1000);
                continue;
            }
            if (frm_eos)
            {
                // printf("found last frame ");
                break;
            }
            if (get_frm)
                continue;
            break;
        } while (1);

        if (pkt_done)
            break;
        usleep(3 * 1000);
    } while (1);
    mpp_packet_deinit(&packet);
    return got_frames > 0;
}
