#pragma once
#include "mov-writer.h"
#include "mov-format.h"
#include "mov-file-buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <thread>
#include <mutex>
enum MEDIATYP {
    H264 = 0,
    HEVC,
    AAC,
    G711A,
    G711U
};
int h264_video_record_config(unsigned char *buffer, unsigned char *sps, int sps_len, unsigned char *pps, int pps_len);
int hevc_video_record_config(unsigned char *buffer, unsigned char *sps, int sps_len, unsigned char *pps, int pps_len, unsigned char *vps, int vps_len);
int aac_audio_record_config(); // TODO
// 如果媒体包含多个sps pps，需要从编码器中获取所有的sps pps，然后写入到extra_data中(eg:x264调用x264_encoder_headers函数可以获取编码参数sps pps)
// 关于extra_data定义参考：https://blog.csdn.net/weixin_43147845/article/details/137011573?spm=1001.2014.3001.5502
class MP4Writer
{
public:
    MP4Writer(const char *mp4);
    ~MP4Writer();
    int AddVideo(int width, int height, const void *extra_data, size_t extra_data_size, MEDIATYP type);
    int AddAudio(int channel_count, int bits_per_sample, int sample_rate, const void *extra_data, size_t extra_data_size, MEDIATYP type);
    int WriteMedia(const void *data, size_t size, int trackId, int64_t pts, int64_t dts, bool isKey);

private:
    int s_video_track = -1;
    uint8_t v_object;
    int s_audio_track = -1;
    uint8_t a_object;
    mov_writer_t *mov = NULL;
    FILE *fp = NULL;
    std::mutex mtx;
};