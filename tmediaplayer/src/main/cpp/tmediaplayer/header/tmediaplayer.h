//
// Created by pengcheng.tan on 2023/7/13.
//

#ifndef TMEDIAPLAYER_TMEDIAPLAYER_H
#define TMEDIAPLAYER_TMEDIAPLAYER_H

#include <android/log.h>
#include <jni.h>

extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
#include "libswresample/swresample.h"
}

#define LOG_TAG "tMediaPlayerNative"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

enum ImageRawType {
    Yuv420p,
    Nv12,
    Nv21,
    Rgba,
    Unknown
};

typedef struct tMediaVideoBuffer {
    int width = 0;
    int height = 0;
    ImageRawType type = Unknown;
    int rgbaSize = 0;
    AVFrame *rgbaFrame = nullptr;
    uint8_t *rgbaBuffer = nullptr;
    int ySize = 0;
    uint8_t *yBuffer = nullptr;
    int uSize = 0;
    uint8_t  *uBuffer = nullptr;
    int vSize = 0;
    uint8_t  *vBuffer = nullptr;
    int uvSize = 0;
    uint8_t  *uvBuffer = nullptr;
} tMediaVideoBuffer;

typedef struct tMediaAudioBuffer {
    int size = 0;
    uint8_t  *pcmBuffer = nullptr;
} tMediaAudioBuffer;

enum tMediaDecodeBufferType {
    BufferTypeVideo,
    BufferTypeAudio
};

enum tMediaDecodeResult {
    DecodeSuccess,
    DecodeEnd,
    DecodeFail
};

typedef struct tMediaDecodeBuffer {
    tMediaDecodeBufferType type = BufferTypeAudio;
    tMediaDecodeResult decodeResult = DecodeFail;
    bool is_last_frame = false;
    long pts;
    tMediaVideoBuffer *videoBuffer = nullptr;
    tMediaAudioBuffer *audioBuffer = nullptr;
} tMediaDecodeBuffer;

enum tMediaOptResult {
    OptSuccess,
    OptFail
};

typedef struct tMediaPlayerContext {
    const char *media_file = nullptr;

    AVFormatContext *format_ctx = nullptr;
    AVPacket *pkt = nullptr;
    AVFrame *frame = nullptr;
    long duration = 0;
    bool skipPktRead = false;


    int metadataCount = 0;
    char ** metadata = nullptr;

    /**
     * Java
     */
    JavaVM* jvm = nullptr;
    jobject jplayer = nullptr;
    jclass jplayerClazz = nullptr;
    jmethodID callRequestVideoBufferMethodId = nullptr;
    jmethodID callRequestAudioBufferMethodId = nullptr;
    jmethodID callEnqueueVideoBufferMethodId = nullptr;
    jmethodID callEnqueueAudioBufferMethodId = nullptr;

    /**
     * Video
     */
    AVStream *video_stream = nullptr;
    AVBufferRef *hardware_ctx = nullptr;
    const AVCodec *video_decoder = nullptr;
    SwsContext * sws_ctx = nullptr;
    int video_width = 0;
    int video_height = 0;
    double video_fps = 0.0;
    long video_duration = 0;
    AVCodecID video_codec_id = AV_CODEC_ID_NONE;
    AVCodecContext *video_decoder_ctx = nullptr;

    /**
     * Audio
     */
    AVStream *audio_stream = nullptr;
    const AVCodec *audio_decoder = nullptr;
    AVCodecContext *audio_decoder_ctx = nullptr;
    SwrContext *swr_ctx = nullptr;
    int audio_channels = 0;
    int audio_per_sample_bytes = 0;
    int audio_simple_rate = 0;
    long audio_duration = 0;
    long audio_output_sample_rate = 48000;
    AVSampleFormat audio_output_sample_fmt = AV_SAMPLE_FMT_S16;
    AVChannelLayout audio_output_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    int audio_output_channels = 2;
    AVCodecID audio_codec_id = AV_CODEC_ID_NONE;


    tMediaOptResult prepare(const char * media_file, bool is_request_hw, int target_audio_channels);

    tMediaOptResult resetDecodeProgress();

    tMediaOptResult seekTo(long targetPtsInMillis, tMediaDecodeBuffer* videoBuffer, tMediaDecodeBuffer* audioBuffer, bool needDecode);

    tMediaOptResult decodeForSeek(long targetPtsMillis, tMediaDecodeBuffer* videoDecodeBuffer, tMediaDecodeBuffer* audioDecodeBuffer, double minStepInMillis, bool skipAudio, bool skipVideo, int maxVideoDoCircleTimes);

    tMediaDecodeBuffer* decode(tMediaDecodeBuffer *lastBuffer);

    tMediaDecodeResult parseDecodeVideoFrameToBuffer(tMediaDecodeBuffer *buffer);

    tMediaDecodeResult parseDecodeAudioFrameToBuffer(tMediaDecodeBuffer *buffer);

    tMediaDecodeBuffer* requestVideoDecodeBufferFromJava();

    tMediaDecodeBuffer* requestAudioDecodeBufferFromJava();

    void enqueueVideoEncodeBufferFromJava(tMediaDecodeBuffer *buffer);

    void enqueueAudioEncodeBufferFromJava(tMediaDecodeBuffer *buffer);

    void release();
} tMediaPlayerContext;

tMediaDecodeBuffer* allocVideoDecodeBuffer();

tMediaDecodeBuffer* allocAudioDecodeBuffer();

void freeDecodeBuffer(tMediaDecodeBuffer *b);

void copyFrameData(uint8_t * dst, uint8_t * src, int image_width, int image_height, int line_stride, int pixel_stride);

#endif //TMEDIAPLAYER_TMEDIAPLAYER_H
