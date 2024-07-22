#include "MP4Writer.h"

MP4Writer::MP4Writer(const char *mp4)
{
    fp = fopen(mp4, "wb+");
    mov = mov_writer_create(mov_file_buffer(), fp, MOV_FLAG_FASTSTART);
}
MP4Writer::~MP4Writer()
{
    if (mov) {
        mov_writer_destroy(mov);
        mov = NULL;
    }
    if (fp) {
        fclose(fp);
        fp = NULL;
    }
}
int MP4Writer::AddVideo(int width, int height, const void *extra_data, size_t extra_data_size, MEDIATYP type)
{
    std::unique_lock<std::mutex> guard(mtx);
    if (type == H264) {
        s_video_track = mov_writer_add_video(mov, MOV_OBJECT_H264, width, height, extra_data, extra_data_size);
    } else if (type == HEVC) {
        s_video_track = mov_writer_add_video(mov, MOV_OBJECT_HEVC, width, height, extra_data, extra_data_size);
    } else {
        return -1;
    }
    v_object = type;
    return s_video_track;
}
int MP4Writer::AddAudio(int channel_count, int bits_per_sample, int sample_rate, const void *extra_data, size_t extra_data_size, MEDIATYP type)
{
    std::unique_lock<std::mutex> guard(mtx);
    if (type == AAC) {
        s_audio_track = mov_writer_add_audio(mov, MOV_OBJECT_AAC, channel_count, bits_per_sample, sample_rate, extra_data, extra_data_size); // bits_per_sample==16
    } else if (type == G711A) {
        s_audio_track = mov_writer_add_audio(mov, MOV_OBJECT_G711a, 1, 16, 8000, NULL, 0);
    } else if (type == G711U) {
        s_audio_track = mov_writer_add_audio(mov, MOV_OBJECT_G711u, 1, 16, 8000, NULL, 0);
    } else {
        return -1;
    }
    a_object = type;
    return s_audio_track;
}
int MP4Writer::WriteMedia(const void *data, size_t size, int trackId, int64_t pts, int64_t dts, bool isKey)
{
    std::unique_lock<std::mutex> guard(mtx);
    if (trackId == s_video_track) {
        mov_writer_write(mov, s_video_track, data, size, pts, dts, isKey ? MOV_AV_FLAG_KEYFREAME : 0);
    } else if (trackId == s_audio_track) {
        if (a_object == MOV_OBJECT_AAC) {
            mov_writer_write(mov, s_audio_track, data, size, pts, dts, isKey ? MOV_AV_FLAG_KEYFREAME : 0);
        } else if (a_object == MOV_OBJECT_G711a || a_object == MOV_OBJECT_G711u) {
            mov_writer_write(mov, s_audio_track, data, size, pts, dts, 0);
        } else {
            return -1;
        }
    } else {
        return -1;
    }
    return 0;
}
int h264_video_record_config(unsigned char *buffer, unsigned char *sps, int sps_len, unsigned char *pps, int pps_len)
{
    int pos = 0;
    buffer[0] = 0x01;
    buffer[1] = sps[1];
    buffer[2] = sps[2];
    buffer[3] = sps[3];

    buffer[4] = 0xff;
    buffer[5] = 0xe1;
    buffer[6] = sps_len >> 8;
    buffer[7] = (unsigned char)sps_len;
    memcpy(&buffer[8], sps, sps_len);
    pos = 8 + sps_len;

    buffer[pos++] = 0x01;
    buffer[pos++] = pps_len >> 8;
    buffer[pos++] = (unsigned char)pps_len;
    memcpy(&buffer[pos], pps, pps_len);
    pos += pps_len;
    return pos;
}

int hevc_video_record_config(unsigned char *buffer, unsigned char *sps, int sps_len, unsigned char *pps, int pps_len, unsigned char *vps, int vps_len)
{

    int i = 0;
    // buffer[i++] = 0x1C;
    // buffer[i++] = 0x00;
    // buffer[i++] = 0x00;
    // buffer[i++] = 0x00;
    // buffer[i++] = 0x00;
    buffer[i++] = 0x01;

    // general_profile_idc 8bit
    buffer[i++] = 0x00;
    // general_profile_compatibility_flags 32 bit
    buffer[i++] = 0x00;
    buffer[i++] = 0x00;
    buffer[i++] = 0x00;
    buffer[i++] = 0x00;

    // 48 bit NUll nothing deal in rtmp
    buffer[i++] = 0x00;
    buffer[i++] = 0x00;
    buffer[i++] = 0x00;
    buffer[i++] = 0x00;
    buffer[i++] = 0x00;
    buffer[i++] = 0x00;

    // general_level_idc
    buffer[i++] = 0x00;

    // 48 bit NUll nothing deal in rtmp
    buffer[i++] = 0xf0;
    buffer[i++] = 0x00;
    buffer[i++] = 0xfc;
    buffer[i++] = 0xfc;
    buffer[i++] = 0xf8;
    buffer[i++] = 0xf8;

    // bit(16) avgFrameRate;
    buffer[i++] = 0x00;
    buffer[i++] = 0x00;

    /* bit(2) constantFrameRate; */
    /* bit(3) numTemporalLayers; */
    /* bit(1) temporalIdNested; */
    buffer[i++] = 0x03;

    /* unsigned int(8) numOfArrays; 03 */
    buffer[i++] = 0x03;

    // printf("HEVCDecoderConfigurationRecord data = %s\n", buffer);
    buffer[i++] = 0xa0; // vps 32
    buffer[i++] = (1 >> 8) & 0xff;
    buffer[i++] = 1 & 0xff;
    buffer[i++] = (vps_len >> 8) & 0xff;
    buffer[i++] = (vps_len) & 0xff;
    memcpy(&buffer[i], vps, vps_len);
    i += vps_len;

    // sps
    buffer[i++] = 0xa1; // sps 33
    buffer[i++] = (1 >> 8) & 0xff;
    buffer[i++] = 1 & 0xff;
    buffer[i++] = (sps_len >> 8) & 0xff;
    buffer[i++] = sps_len & 0xff;
    memcpy(&buffer[i], sps, sps_len);
    i += sps_len;

    // pps
    buffer[i++] = 0xa2; // pps 34
    buffer[i++] = (1 >> 8) & 0xff;
    buffer[i++] = 1 & 0xff;
    buffer[i++] = (pps_len >> 8) & 0xff;
    buffer[i++] = (pps_len) & 0xff;
    memcpy(&buffer[i], pps, pps_len);
    i += pps_len;
    return i;
}
int aac_audio_record_config()
{
    return 0; 
}