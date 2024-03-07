#include "NvCodecRender.h"
static CUcontext cuContext = NULL;
simplelogger::Logger *logger = simplelogger::LoggerFactory::CreateConsoleLogger();
static uint32_t find_start_code(uint8_t *buf, uint32_t zeros_in_startcode)
{
    uint32_t info;
    uint32_t i;

    info = 1;
    if ((info = (buf[zeros_in_startcode] != 1) ? 0 : 1) == 0)
        return 0;

    for (i = 0; i < zeros_in_startcode; i++)
        if (buf[i] != 0) {
            info = 0;
            break;
        };

    return info;
}
static uint8_t *get_nal(uint32_t *len, uint8_t **offset, uint8_t *start, uint32_t total, uint8_t *prefix_len)
{
    uint32_t info;
    uint8_t *q;
    uint8_t *p = *offset;
    uint8_t prefix_len_z = 0;
    *len = 0;
    *prefix_len = 0;
    while (1) {

        if (((p - start) + 3) >= total)
            return NULL;

        info = find_start_code(p, 2);
        if (info == 1) {
            prefix_len_z = 2;
            *prefix_len = prefix_len_z;
            break;
        }

        if (((p - start) + 4) >= total)
            return NULL;

        info = find_start_code(p, 3);
        if (info == 1) {
            prefix_len_z = 3;
            *prefix_len = prefix_len_z;
            break;
        }
        p++;
    }
    q = p;
    p = q + prefix_len_z + 1;
    prefix_len_z = 0;
    while (1) {
        if (((p - start) + 3) >= total) {
            *len = (start + total - q);
            *offset = start + total;
            return q;
        }

        info = find_start_code(p, 2);
        if (info == 1) {
            prefix_len_z = 2;
            break;
        }

        if (((p - start) + 4) >= total) {
            *len = (start + total - q);
            *offset = start + total;
            return q;
        }

        info = find_start_code(p, 3);
        if (info == 1) {
            prefix_len_z = 3;
            break;
        }

        p++;
    }

    *len = (p - q);
    *offset = p;
    return q;
}
static void CreateCudaContext(CUcontext *cuContext, int iGpu, unsigned int flags)
{
    CUdevice cuDevice = 0;
    ck(cuDeviceGet(&cuDevice, iGpu));
    char szDeviceName[80];
    ck(cuDeviceGetName(szDeviceName, sizeof(szDeviceName), cuDevice));
    std::cout << "GPU in use: " << szDeviceName << std::endl;
    ck(cuCtxCreate(cuContext, flags, cuDevice));
}
// 查询显卡是否支持H264编码
static bool SupportHardEnc(int iGpu)
{
    
}
NvCodecRender::NvCodecRender(const char *input, const char *output, int gpu_idx, bool use_nvenc)
{
    cudaSetDevice(gpu_idx);
    in_file_ = input;
    out_file_ = output;
    demuxer_ = new FFmpegDemuxer(input);
    width_ = demuxer_->GetWidth();
    height_ = demuxer_->GetHeight();
    static std::once_flag flag;
    gpu_idx_ = gpu_idx;

    std::call_once(flag, [this] {
        CreateCudaContext(&cuContext, this->gpu_idx_, 0);
    });
    dec_ = new NvDecoder(cuContext, true, FFmpeg2NvCodecId(demuxer_->GetVideoCodec()), true);
    //use_nvenc_ = SupportHardEnc(gpu_idx);
    use_nvenc_ = use_nvenc;
    if (use_nvenc_) {
        printf("use hard enc...\n");
    } else {
        printf("use soft enc...\n");
    }
    EncInit();
    mp4_ = new MP4Writer(output);
}
NvCodecRender::~NvCodecRender()
{
    if (demuxer_) {
        delete demuxer_;
        demuxer_ = NULL;
    }
    if (dec_) {
        delete dec_;
        dec_ = NULL;
    }
    EncDestory();
    if (mp4_) {
        delete mp4_;
        mp4_ = NULL;
    }
    if (v_packet_) {
        free(v_packet_);
        v_packet_ = NULL;
    }
    printf("~ReidRender\n");
}
int NvCodecRender::EncInit()
{
    demuxer_->GetParam(v_fps_, v_bitrate_); // 编码的时候使用原始视频的帧率和码流，尽可能的保留原始画质
    if (v_fps_ <= 0) {
        v_fps_ = 25;
    }
    if (v_bitrate_ <= 0) {
        v_bitrate_ = 4000000;
    }
    out_fps_ = v_fps_; // 输出帧率
    if (use_nvenc_) {
        std::string param1 = "-codec h264 -preset p4 -profile baseline -tuninginfo ultralowlatency -bf 0 "; // 编码参数，根据需求自行修改
        std::string param2 = "-fps " + std::to_string(v_fps_) + " -gop " + std::to_string(2 * v_fps_) + " -bitrate " + std::to_string(v_bitrate_);
        std::string sz_param = param1 + param2;
        printf("sz_param:%s\n", sz_param.c_str());
        int rgba_frame_pitch = width_ * 4;
        int rgba_frame_size = rgba_frame_pitch * height_;
        ck(cuMemAlloc(&ptr_image_enc_, rgba_frame_size));
        eformat_ = NV_ENC_BUFFER_FORMAT_ABGR; //rgb
        init_param_ = NvEncoderInitParam(sz_param.c_str());
        enc_ = new NvEncoderCuda(cuContext, width_, height_, eformat_);
        NV_ENC_INITIALIZE_PARAMS initialize_params = {NV_ENC_INITIALIZE_PARAMS_VER};
        NV_ENC_CONFIG encode_config = {NV_ENC_CONFIG_VER};
        initialize_params.encodeConfig = &encode_config;
        enc_->CreateDefaultEncoderParams(&initialize_params, init_param_.GetEncodeGUID(), init_param_.GetPresetGUID(), init_param_.GetTuningInfo());
        init_param_.SetInitParams(&initialize_params, eformat_);
        enc_->CreateEncoder(&initialize_params);
    } else {
        h264_codec_ = avcodec_find_encoder(AV_CODEC_ID_H264);
        h264_codec_ctx_ = avcodec_alloc_context3(h264_codec_);
        h264_codec_ctx_->codec_type = AVMEDIA_TYPE_VIDEO;
        h264_codec_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
        h264_codec_ctx_->width = width_;
        h264_codec_ctx_->height = height_;
        h264_codec_ctx_->time_base.num = 1;
        h264_codec_ctx_->time_base.den = v_fps_;
        h264_codec_ctx_->bit_rate = v_bitrate_;
        h264_codec_ctx_->gop_size = v_fps_ * 2;
        h264_codec_ctx_->thread_count = 1;
        h264_codec_ctx_->slices = 1; // 切片数量。 表示图片细分的数量。 用于并行解码。
        /**
         * 遇到问题：编码得到的h264文件播放时提示"non-existing PPS 0 referenced"
         *  分析原因：未将pps sps 等信息写入
         *  解决方案：加入标记AV_CODEC_FLAG2_LOCAL_HEADER
         */
        h264_codec_ctx_->flags |= AV_CODEC_FLAG2_LOCAL_HEADER;
        h264_codec_ctx_->flags |= AV_CODEC_FLAG_LOW_DELAY;
        av_opt_set(h264_codec_ctx_->priv_data, "preset", "ultrafast", 0);
        av_opt_set(h264_codec_ctx_->priv_data, "tune", "zerolatency", 0);
        av_opt_set(h264_codec_ctx_->priv_data, "profile", "baseline", 0);
        if (avcodec_open2(h264_codec_ctx_, h264_codec_, NULL) < 0) {
            printf("Failed to open encoder!\n");
            avcodec_close(h264_codec_ctx_);
            avcodec_free_context(&h264_codec_ctx_);
            h264_codec_ = NULL;
            h264_codec_ctx_ = NULL;
            return -1;
        }
        sws_context_ = sws_getContext(h264_codec_ctx_->width, h264_codec_ctx_->height, AV_PIX_FMT_RGBA,
                                      h264_codec_ctx_->width, h264_codec_ctx_->height, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, NULL, NULL, NULL);
        yuv_frame_ = av_frame_alloc();
        yuv_frame_->width = h264_codec_ctx_->width;
        yuv_frame_->height = h264_codec_ctx_->height;
        yuv_frame_->format = AV_PIX_FMT_YUV420P;
        int ret = av_image_alloc(yuv_frame_->data, yuv_frame_->linesize, yuv_frame_->width, yuv_frame_->height, AV_PIX_FMT_YUV420P, 1);
        if (ret < 0) {
            printf("av_image_alloc failed\n");
            return -1;
        }
        av_init_packet(&enc_packet_);
    }
    return 0;
}
int NvCodecRender::EncDestory()
{
    if (use_nvenc_) {
        if (enc_) {
            enc_->DestroyEncoder();
            delete enc_;
            enc_ = NULL;
        }

        ck(cuMemFree(ptr_image_enc_));
        ptr_image_enc_ = 0;

    } else {
        if (h264_codec_ctx_ != NULL) {
            avcodec_close(h264_codec_ctx_);
            avcodec_free_context(&h264_codec_ctx_);
            h264_codec_ctx_ = NULL;
        }
        if (sws_context_ != NULL) {
            sws_freeContext(sws_context_);
            sws_context_ = NULL;
        }
        if (yuv_frame_) {
            av_freep(&yuv_frame_->data[0]);
            av_frame_free(&yuv_frame_);
            yuv_frame_ = NULL;
        }
        av_packet_unref(&enc_packet_);
    }
    return 0;
}
int NvCodecRender::Write2File(uint8_t *data, int len)
{
    uint8_t *p_video = NULL;
    uint32_t nal_len;
    uint8_t *buf_sffset = data;
    uint8_t prefix_len = 0;
    uint8_t *video_data = data;
    uint32_t video_len = len;
    p_video = get_nal(&nal_len, &buf_sffset, video_data, video_len, &prefix_len);
    while (p_video != NULL) {
        prefix_len = prefix_len + 1;
        uint8_t nal_type = p_video[prefix_len] & 0x1f;
        if (nal_type == 7) {
            memcpy(sps_buffer_, p_video + prefix_len, nal_len - prefix_len);
            sps_len_ = nal_len - prefix_len;
        } else if (nal_type == 8) {
            memcpy(pps_buffer_, p_video + prefix_len, nal_len - prefix_len);
            pps_len_ = nal_len - prefix_len;
            if (video_track_ == -1) {
                unsigned char buffer[1024];
                int len = h264_video_record_config(buffer, sps_buffer_, sps_len_, pps_buffer_, pps_len_);
                video_track_ = mp4_->AddVideo(width_, height_, buffer, len, H264);
            }
        } else if (nal_type == 5 || nal_type == 1) {
            bool is_key = nal_type == 5 ? true : false;
            v_pts_ += 1000 / out_fps_;
            uint32_t packet_len = nal_len - prefix_len + 4;
            if (v_packet_ == NULL || packet_len > packet_len_) {
                packet_len_ = packet_len > packet_len_ ? packet_len : packet_len_;
                v_packet_ = (unsigned char *)realloc(v_packet_, packet_len_);
            }
            v_packet_[0] = (nal_len - prefix_len) >> 24;
            v_packet_[1] = (nal_len - prefix_len) >> 16;
            v_packet_[2] = (nal_len - prefix_len) >> 8;
            v_packet_[3] = (nal_len - prefix_len) & 0xff;
            memcpy(v_packet_ + 4, p_video + prefix_len, nal_len - prefix_len);
            mp4_->WriteMedia(v_packet_, packet_len, video_track_, v_pts_, v_pts_, is_key);
        }
        p_video = get_nal(&nal_len, &buf_sffset, video_data, video_len, &prefix_len);
    }
}
int NvCodecRender::EncFrame(void *ptr, int size)
{
    if (use_nvenc_) {
        std::vector<std::vector<uint8_t>> vPacket;
        if (ptr != NULL) {
            const NvEncInputFrame *encoder_input_frame = enc_->GetNextInputFrame();
            NvEncoderCuda::CopyToDeviceFrame(cuContext, ptr, 0, (CUdeviceptr)encoder_input_frame->inputPtr,
                                             (int)encoder_input_frame->pitch,
                                             enc_->GetEncodeWidth(),
                                             enc_->GetEncodeHeight(),
                                             CU_MEMORYTYPE_DEVICE, // CU_MEMORYTYPE_HOST,CU_MEMORYTYPE_DEVICE
                                             encoder_input_frame->bufferFormat,
                                             encoder_input_frame->chromaOffsets,
                                             encoder_input_frame->numChromaPlanes);
            enc_->EncodeFrame(vPacket);
        } else {
            enc_->EndEncode(vPacket);
        }
        for (std::vector<uint8_t> &packet : vPacket) {
            // write to file
            Write2File(packet.data(), packet.size());
        }
    } else {
        int ret;
        if (ptr != NULL) {
            // sws
            AVFrame mat_frame;
            avpicture_fill((AVPicture *)&mat_frame, ptr, AV_PIX_FMT_RGBA, width_, height_);
            mat_frame.width = width_;
            mat_frame.height = height_;
            sws_scale(sws_context_, mat_frame.data, mat_frame.linesize, 0, mat_frame.height, yuv_frame_->data, yuv_frame_->linesize);
            // enc
            ret = avcodec_send_frame(h264_codec_ctx_, yuv_frame_);
        } else { // fflush
            // 编码：int avcodec_send_frame(AVCodecContext *avctx, const AVFrame *frame);frame=NULL
            // 解码：int avcodec_send_packet(AVCodecContext *avctx, const AVPacket *avpkt);avpkt=NULL或者avpkt->data=NULL && avpkt->size=0
            ret = avcodec_send_frame(h264_codec_ctx_, NULL);
        }
        while (ret >= 0) {
            ret = avcodec_receive_packet(h264_codec_ctx_, &enc_packet_);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                printf("Error during encoding\n");
                return -1;
            }
            Write2File(enc_packet_.data, enc_packet_.size);
            av_packet_unref(&enc_packet_);
        }
    }
    return 0;
}
int NvCodecRender::Draw(unsigned char *rgba_frame, int w, int h)
{
    cv::Mat image(h, w, CV_8UC4, rgba_frame);

    cv::Rect rect(200, 200, 200, 200);
    cv::Scalar rect_color(0, 255, 0);
    cv::rectangle(image, rect, rect_color, 2);

    cv::Scalar text_color(0, 255, 0);
    cv::putText(image, "BreakingY:kxsun@163.com", cv::Point(200, 150), cv::FONT_HERSHEY_SIMPLEX, 0.7, text_color, 2);
    return 0;
}
int NvCodecRender::Render()
{
    CUdeviceptr dp_rgba_frame = 0;
    std::unique_ptr<uint8_t[]> p_rgba_frame;

    // rgba
    int rgba_frame_pitch = width_ * 4;
    int rgba_frame_size = rgba_frame_pitch * height_;
    ck(cuMemAlloc(&dp_rgba_frame, rgba_frame_size));
    p_rgba_frame.reset(new uint8_t[rgba_frame_size]);

    uint8_t *p_video = NULL;
    int n_video_bytes = 0;
    
    do {
        int64_t pts;
        demuxer_->Demux(&p_video, &n_video_bytes, &pts);
        uint8_t *p_frame;
        int n_frame_returned = 0;
        n_frame_returned = dec_->Decode(n_video_bytes > 0 ? p_video : NULL, n_video_bytes, CUVID_PKT_ENDOFPICTURE | CUVID_PKT_TIMESTAMP, pts); // CUVID_PKT_ENDOFPICTURE解码器立即输出，没有缓存，没有解码缓存时延;CUVID_PKT_TIMESTAMP返回原始时间戳
        int i_matrix = dec_->GetVideoFormatInfo().video_signal_description.matrix_coefficients;
        for (int i = 0; i < n_frame_returned; i++) {
            int64_t timestamp;
            p_frame = dec_->GetFrame(&timestamp);
            printf("Dec output timestamp:%ld\n", timestamp);
            total_frames_++;
            Nv12ToColor32<RGBA32>(p_frame, width_, (uint8_t *)dp_rgba_frame, rgba_frame_pitch, width_, height_, i_matrix);

            ck(cuMemcpyDtoH(p_rgba_frame.get(), dp_rgba_frame, rgba_frame_size));
            Draw(p_rgba_frame.get(), width_, height_);
            if (use_nvenc_) {
                ck(cuMemcpyHtoD(ptr_image_enc_, p_rgba_frame.get(), rgba_frame_size));
                EncFrame(ptr_image_enc_, rgba_frame_size);
            } else {
                EncFrame(p_rgba_frame.get(), rgba_frame_size);
            }
        }
    } while (n_video_bytes);
    EncFrame(NULL, 0);
    // clear
    ck(cuMemFree(dp_rgba_frame));
    dp_rgba_frame = 0;
    p_rgba_frame.reset(nullptr);
    return 0;
}