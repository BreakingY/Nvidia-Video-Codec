# Nvidia-Video-Codec-Demo
NVIDIA video decoding, rendering, encoding and writing to MP4 file，Nvidia视频解码、渲染、编码并写入MP4文件。


# 本项目基于英伟达 Video_Codec_SDK_11.0.10实现视频硬解码、opencv渲染、软/硬编码并把编码之后的视频写入MP4文件。
* 解码：使用Video_Codec_SDK_11.0.10解码API对视频进行解码，支持H264、H265。
* 渲染：使用opencv对图像进行渲染，涉及到了基础的cuda开发(不过都是比较简单的，没有什么难度)。
* 编码：支持软硬编码切换，硬编码使用Video_Codec_SDK_11.0.10 API,软编码使用ffmpeg API，视频编码格式为H264，如需H265可自行修改(NvCodecRender::EncInit()/NvCodecRender.cpp)。基本nvidia的所有显卡都支持视频解码、但不是所有显卡都支持编码、所以这里实现了软硬编码切换功能。
* MP4：编码后的视频写入到MP4文件中，这里使用了老陈的libmov库，项目地址https://github.com/ireader/media-server。感谢老陈，media-server是一个优秀的开源项目，推荐搞流媒体开发的朋友都可以看看，基本涵盖了流媒体开发的常用协议。


# 环境准备
* 安装驱动，注意本项目使用的是Video_Codec_SDK_11.0.10，不同SDK要求的驱动版本不一样，本项目在465.19.01和535.104.12验证过，其他版本请自行查阅官网。
* 安装cuda，版本和驱动保持一致即可，不同驱动对应不同cuda版本，驱动是向下兼容的，可以先安装cuda，在安装的时候选择一起安装驱动也可以，就这样就不需要单独安装驱动了。
* 安装ffmpeg，版本 >= 4.x 安装在/usr/local,否则请修改CMakeLists，把ffmpeg头文件和库路劲添加进去。
* 项目测试环境cuda 11.3 + 驱动 535.104.12 + ffmpeg 4.0.5、cuda 11.3 + 驱动 465.19.01 + ffmpeg 4.0.5 。


# 代码支持Linux、Windows
# Linux编译
* export PATH=$PATH:/usr/local/cuda/bin
* export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/cuda/lib64
* mkdir build
* cd build
* cmake ..
* make -j4
* 测试 ./demo ../test/1080P.mp4 out.mp4 0 0 ，参数：输入文件、输出文件、使用哪张显卡(只有一张显卡的话就写0)、指定使用软编码还是硬件编码(0-软编码 1-硬件编码)
# Windows编译
* 没有测试过，但是代码是通用的。

# 注意
* VideoCodec目录里的文件都是从Video_Codec_SDK_11.0.10中提取的，因为Video_Codec_SDK_11.0.10中文件很多，实际使用过程中并不是所有的都需要，VideoCodec里面只提取出来本项目使用的文件，并进行分类。
* 项目对FFmpegDemuxer.h进行了修改，增加了int GetParam(int &fps,int &bitrate)函数，获取视频帧率和码流，在编码中使用，这样的目的是编码时保留图像原始帧率和码流参数。


# 技术交流
* kxsun617@163.com

