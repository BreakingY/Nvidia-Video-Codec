#pragma once
#include <cuda.h>
#include <iostream>
#include <iomanip>
#include <exception>
#include <stdexcept>
#include <memory>
#include <functional>
#include <stdint.h>
#include <opencv2/opencv.hpp>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include "NvDecoder.h"
#include "NvEncoderCuda.h"
#include "NvEncoderCLIOptions.h"
#include "NvCodecUtils.h"
#include "FFmpegDemuxer.h"
#include "ColorSpace.h"
#include "MP4Writer.h"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}

// 版本 Video_Codec_SDK_11.0.10 不同版本SDK对显卡驱动要求不同
class NvCodecRender
{
public:
    NvCodecRender(const char *input, const char *output, int gpu_idx, bool us_nv_enc = false);
    ~NvCodecRender();
    int Render();

private:
    int Draw(unsigned char *rgba_frame, int w, int h);
    int EncInit();
    int EncFrame(void *ptr, int size);
    int EncDestory();
    int Write2File(uint8_t *data, int len);

private:
    std::string in_file_;
    std::string out_file_;
    FFmpegDemuxer *demuxer_ = NULL;
    NvDecoder *dec_ = NULL;
    int gpu_idx_ = 0;
    int width_, height_;
    bool use_nvenc_ = true;
    // NVENC
    NvEncoderInitParam init_param_;
    NV_ENC_BUFFER_FORMAT eformat_;
    CUdeviceptr ptr_image_enc_ = 0;
    NvEncoderCuda *enc_ = NULL;
    // soft enc
    AVCodecContext *h264_codec_ctx_ = NULL;
    AVCodec *h264_codec_ = NULL;
    SwsContext *sws_context_ = NULL;
    AVFrame *yuv_frame_ = NULL;
    AVPacket enc_packet_;

    MP4Writer *mp4_ = NULL;
    unsigned char sps_buffer_[1024];
    int sps_len_ = 0;
    unsigned char pps_buffer_[1024];
    int pps_len_ = 0;
    int video_track_ = -1;
    int v_fps = 25;
    int v_bitrate = 0;
    int v_pts = 0;
    int out_fps = 25; // 输出帧率
    int64_t total_frames_ = 0;
    unsigned char *v_packet = NULL;
    int64_t packet_len_ = 4 * 1024 * 1024;
};