#include "media_player.h"

extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
}
#include "android/native_window.h"
#include "android/native_window_jni.h"
#include "media_time.h"

PLAYER_OPT_RESULT MediaPlayerContext::setup_media_player( const char *file_path) {
    LOGD("Setup media player file path: %s", file_path);

    this->media_file = file_path;
    // Find audio and video streams.
    this->format_ctx = avformat_alloc_context();
    int format_open_result = avformat_open_input(&format_ctx, file_path, nullptr, nullptr);
    if (format_open_result != 0) {
        LOGE("Format open file fail: %d", format_open_result);
        return OPT_FAIL;
    }
    int stream_find_result = avformat_find_stream_info(format_ctx, nullptr);
    if (stream_find_result < 0) {
        LOGE("Format find stream error: %d", stream_find_result);
        return OPT_FAIL;
    }
    for (int i = 0; i < format_ctx->nb_streams; i++) {
        auto stream = format_ctx->streams[i];
        auto codec_type = stream->codecpar->codec_type;
        switch (codec_type) {
            case AVMEDIA_TYPE_AUDIO:
                this->audio_stream = stream;
                LOGD("Find Stream: %s", "AVMEDIA_TYPE_AUDIO");
                break;
            case AVMEDIA_TYPE_VIDEO:
                this->video_stream = stream;
                LOGD("Find Stream: %s", "AVMEDIA_TYPE_VIDEO");
                break;
            case AVMEDIA_TYPE_UNKNOWN:
                LOGD("Find Stream: %s", "AVMEDIA_TYPE_UNKNOWN");
                break;
            case AVMEDIA_TYPE_DATA:
                LOGD("Find Stream: %s", "AVMEDIA_TYPE_DATA");
                break;
            case AVMEDIA_TYPE_SUBTITLE:
                LOGD("Find Stream: %s", "AVMEDIA_TYPE_SUBTITLE");
                break;
            case AVMEDIA_TYPE_ATTACHMENT:
                LOGD("Find Stream: %s", "AVMEDIA_TYPE_ATTACHMENT");
                break;
            case AVMEDIA_TYPE_NB:
                LOGD("Find Stream: %s", "AVMEDIA_TYPE_NB");
                break;
        }
    }

    this->pkt = av_packet_alloc();
    this->frame = av_frame_alloc();

    // Video decode
    if (this->video_stream != nullptr) {
        auto video_codec = video_stream->codecpar->codec_id;
        this->video_decoder = avcodec_find_decoder(video_codec);
        if (this->video_decoder == nullptr) {
            LOGE("Do not find video decoder");
        } else {
            this->video_decoder_ctx = avcodec_alloc_context3(video_decoder);

            if (avcodec_parameters_to_context(video_decoder_ctx, video_stream->codecpar) < 0) {
                LOGE("Set video stream params fail");
                return OPT_FAIL;
            }
            if (avcodec_open2(video_decoder_ctx, video_decoder, nullptr) < 0) {
                LOGE("Open video decoder fail");
                return OPT_FAIL;
            }
            this->video_width = video_decoder_ctx->width;
            this->video_height = video_decoder_ctx->height;
            this->video_fps = av_q2d(video_stream->r_frame_rate);
            this->video_base_time = av_q2d(video_stream->time_base);
            this->video_time_den = video_stream->time_base.den;
            long v_duration = video_stream->duration * 1000 / video_time_den;
            this->duration = v_duration;
            LOGD("Width: %d, Height: %d, Fps: %.1f, Base time: %.1f, Duration: %ld",
                 video_width,
                 video_height, video_fps,
                 video_base_time,
                 duration);

            this->sws_ctx = sws_getContext(
                    video_width, video_height, video_decoder_ctx->pix_fmt,
                    video_width, video_height, AV_PIX_FMT_RGBA,
                    SWS_BICUBIC, nullptr, nullptr, nullptr
            );
            this->native_window_buffer = new ANativeWindow_Buffer;
        }
    }

    // Audio decode
    if (audio_stream != nullptr) {
        this->audio_decoder = avcodec_find_decoder(audio_stream->codecpar->codec_id);
        if (audio_decoder == nullptr) {
            LOGE("%s", "Do not find audio decoder");
            return OPT_FAIL;
        } else {
            this->audio_decoder_ctx = avcodec_alloc_context3(audio_decoder);
        }

        if (avcodec_parameters_to_context(audio_decoder_ctx, audio_stream->codecpar) < 0) {
            LOGE("Set audio stream params fail");
            return OPT_FAIL;
        }
        if (avcodec_open2(audio_decoder_ctx, audio_decoder, nullptr) < 0) {
            LOGE("Open audio decoder fail");
            return OPT_FAIL;
        }

        long a_duration = audio_stream->duration * 1000 / av_q2d(audio_stream->time_base);
        if (a_duration > duration) {
            this->duration = a_duration;
        }

        audio_channels = av_get_channel_layout_nb_channels(audio_decoder_ctx->channel_layout);
        audio_pre_sample_bytes = av_get_bytes_per_sample(audio_decoder_ctx->sample_fmt);
        LOGD("Audio channel size: %d, simple size: %d", audio_channels, audio_pre_sample_bytes);
    }
    if (video_stream == nullptr && audio_stream == nullptr) {
        return OPT_FAIL;
    } else {
        return OPT_SUCCESS;
    }
}

PLAYER_OPT_RESULT MediaPlayerContext::set_window(ANativeWindow *native_window_l) {
    this->native_window = native_window_l;
    return OPT_SUCCESS;
}

PLAYER_OPT_RESULT MediaPlayerContext::reset_play_progress() {
    if (video_stream != nullptr) {
        av_seek_frame(format_ctx, video_stream->index, 0, AVSEEK_FLAG_BACKWARD);
    }
    if (audio_stream != nullptr) {
        av_seek_frame(format_ctx, audio_stream->index, 0, AVSEEK_FLAG_BACKWARD);
    }
    return OPT_SUCCESS;
}

DECODE_FRAME_RESULT MediaPlayerContext::decode_next_frame(RenderRawData* render_data) {

    if (pkt != nullptr &&
        frame != nullptr &&
        format_ctx != nullptr &&
        render_data != nullptr) {
        av_packet_unref(pkt);
        // 1. read pkt.
        int read_frame_result = av_read_frame(format_ctx, pkt);
        if (read_frame_result < 0) {
            LOGD("%s", "Decode video read frame result.");
            return DECODE_FRAME_FINISHED;
        }

        // decode video
        if (video_decoder_ctx != nullptr &&
            video_decoder != nullptr &&
            video_stream != nullptr &&
            pkt->stream_index == video_stream->index) {

            long decode_frame_start = get_time_millis();
            // 2. send pkt to decoder.
            int send_pkg_result = avcodec_send_packet(video_decoder_ctx, pkt);
            if (send_pkg_result < 0 && send_pkg_result != AVERROR(EAGAIN)) {
                LOGE("Decode video send pkt fail: %d", send_pkg_result);
                return DECODE_FRAME_FAIL;
            }
            av_frame_unref(frame);

            // 3. receive frame
            int receive_frame_result = avcodec_receive_frame(video_decoder_ctx, frame);
            if (receive_frame_result == AVERROR(EAGAIN)) {
                return decode_next_frame(render_data);
            }
            if (receive_frame_result < 0) {
                LOGE("%s", "Decode video frame fail");
                return DECODE_FRAME_FAIL;
            }

            // 4.scale
            sws_scale(sws_ctx, frame->data, frame->linesize, 0, frame->height, render_data->video_data->rgba_frame->data, render_data->video_data->rgba_frame->linesize);
            int64_t pts_millis = frame->pts * 1000 / video_time_den;
            LOGD("Decode video frame success: %lld, time cost: %ld", pts_millis, get_time_millis() - decode_frame_start);
            render_data->video_data->pts = pts_millis;
            render_data->is_video = true;
            return DECODE_FRAME_SUCCESS;
        }

        // decode audio
        if (audio_decoder_ctx != nullptr &&
            audio_decoder != nullptr &&
            audio_stream != nullptr &&
            pkt->stream_index == audio_stream->index) {
            render_data->is_video = false;

            long decode_frame_start = get_time_millis();

            int send_pkg_result = avcodec_send_packet(audio_decoder_ctx, pkt);
            if (send_pkg_result < 0 && send_pkg_result != AVERROR(EAGAIN)) {
                LOGE("Decode video send pkt fail: %d", send_pkg_result);
                return DECODE_FRAME_FAIL;
            }

            int frame_count = 0;
            while (true) {
                int receive_frame_result = avcodec_receive_frame(audio_decoder_ctx, frame);
                if (receive_frame_result < 0) {
                    break;
                }
                frame_count ++;
            }
            LOGD("Decode audio frame success: %d, time cost: %ld", frame_count, get_time_millis() - decode_frame_start);
            if (frame_count <= 0) {
                return DECODE_FRAME_FAIL;
            } else {
                return DECODE_FRAME_SUCCESS;
            }
        }
        LOGD("Unknown pkt.");
        return DECODE_FRAME_FAIL;
    } else {
        LOGE("Decode video fail, video_stream, video_decoder and context is null.");
        return DECODE_FRAME_FAIL;
    }

}

PLAYER_OPT_RESULT MediaPlayerContext::render_raw_data(RenderRawData* raw_data) {
    if (raw_data->is_video) {
        auto window = native_window;
        if (window != nullptr) {
            try {
                if (ANativeWindow_setBuffersGeometry(window, video_width, video_height, WINDOW_FORMAT_RGBA_8888) != 0) {
                    return OPT_FAIL;
                }
                auto window_buffer = native_window_buffer;
                auto rgba_buffer = raw_data->video_data->rgba_frame_buffer;
                auto rgba_frame = raw_data->video_data->rgba_frame;
                if (ANativeWindow_lock(window, window_buffer, nullptr) != 0) {
                    return OPT_FAIL;
                }
                auto bits = (uint8_t*) window_buffer->bits;
                for (int h = 0; h < video_height; h++) {
                    memcpy(bits + h * window_buffer->stride * 4,
                           rgba_buffer + h * rgba_frame->linesize[0],
                           rgba_frame->linesize[0]);
                }
                if (ANativeWindow_unlockAndPost(window) < 0) {
                    return OPT_FAIL;
                }
                return OPT_SUCCESS;
            } catch (...) {
                LOGE("%s", "Render frame error.");
                return OPT_FAIL;
            }
        } else {
            LOGD("%s", "Native window is null, skip render.");
            return OPT_SUCCESS;
        }
    } else {
        // TODO: AUDIO
        return OPT_SUCCESS;
    }
}

void MediaPlayerContext::release_media_player() {
    // Common release.
    if (pkt != nullptr) {
        av_packet_unref(pkt);
        av_packet_free(&pkt);
    }
    if (format_ctx != nullptr) {
        avformat_close_input(&format_ctx);
        avformat_free_context(format_ctx);
    }
    if (frame != nullptr) {
        av_frame_free(&frame);
    }

    // Video Release.

    if (video_decoder_ctx != nullptr) {
        avcodec_close(video_decoder_ctx);
        avcodec_free_context(&video_decoder_ctx);
    }

    if (native_window != nullptr) {
        ANativeWindow_release(native_window);
    }

    if (native_window_buffer != nullptr) {
        free(native_window_buffer);
    }

    if (sws_ctx != nullptr) {
        sws_freeContext(sws_ctx);
    }

    // Audio free.
    if (audio_decoder_ctx != nullptr) {
        avcodec_close(audio_decoder_ctx);
        avcodec_free_context(&audio_decoder_ctx);
    }
    free(this);
    LOGD("%s", "Release media player");
}


RenderRawData* MediaPlayerContext::new_render_raw_data() {
    auto raw_data = new RenderRawData;
    auto video_data = new RenderVideoRawData;

    // Video
    video_data->rgba_frame = av_frame_alloc();
    int buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, video_width, video_height,1);
    video_data->rgba_frame_buffer = static_cast<uint8_t *>(av_malloc(
            buffer_size * sizeof(uint8_t)));
    av_image_fill_arrays(video_data->rgba_frame->data, video_data->rgba_frame->linesize, video_data->rgba_frame_buffer,
                         AV_PIX_FMT_RGBA, video_width, video_height, 1);
    raw_data->video_data = video_data;

    // Audio
    raw_data->audio_data = new RenderAudioRawData;
    LOGD("Create RAW Data: %ld", raw_data);
    return raw_data;
}

void MediaPlayerContext::release_render_raw_data(RenderRawData* render_data) {
    auto video = render_data->video_data;
    if (video != nullptr) {
        av_frame_free(&video->rgba_frame);
        av_free(video->rgba_frame_buffer);
        free(video);
    }
    auto audio = render_data->audio_data;
    if (audio != nullptr) {
        free(audio);
    }
    LOGD("Release RAW Data: %ld", render_data);
    free(render_data);
}