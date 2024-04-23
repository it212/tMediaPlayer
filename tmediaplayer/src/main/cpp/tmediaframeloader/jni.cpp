//
// Created by pengcheng.tan on 2024/4/23.
//
#include <jni.h>
#include "tmediaframeloader.h"
#include "tmediaplayer.h"

extern "C" {
#include "libavcodec/jni.h"
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_tans_tmediaplayer_frameloader_tMediaFrameLoader_createFrameLoaderNative(
        JNIEnv * env,
        jobject j_frame_loader) {
    JavaVM * jvm = nullptr;
    env->GetJavaVM(&jvm);
    auto loader = new tMediaFrameLoaderContext;
    loader->jvm = jvm;
    return reinterpret_cast<jlong>(loader);
}


extern "C" JNIEXPORT jint JNICALL
Java_com_tans_tmediaplayer_frameloader_tMediaFrameLoader_prepareNative(
        JNIEnv * env,
        jobject j_frame_loader,
        jlong native_loader,
        jstring file_path) {
    auto *loader = reinterpret_cast<tMediaFrameLoaderContext*>(native_loader);
    if (loader == nullptr) {
        return OptFail;
    }
    av_jni_set_java_vm(loader->jvm, nullptr);
    const char * file_path_chars = env->GetStringUTFChars(file_path, 0);
    return loader->prepare(file_path_chars);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_tans_tmediaplayer_frameloader_tMediaFrameLoader_getFrameNative(
        JNIEnv * env,
        jobject j_frame_loader,
        jlong native_loader,
        jlong position,
        jboolean needRealTime) {
    auto *loader = reinterpret_cast<tMediaFrameLoaderContext*>(native_loader);
    if (loader == nullptr) {
        return OptFail;
    }
    return loader->getFrame(position, needRealTime);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_tans_tmediaplayer_frameloader_tMediaFrameLoader_videoWidthNative(
        JNIEnv * env,
        jobject j_loader,
        jlong native_loader) {
    auto *loader = reinterpret_cast<tMediaFrameLoaderContext *>(native_loader);
    return loader->video_width;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_tans_tmediaplayer_frameloader_tMediaFrameLoader_videoHeightNative(
        JNIEnv * env,
        jobject j_loader,
        jlong native_loader) {
    auto *loader = reinterpret_cast<tMediaFrameLoaderContext *>(native_loader);
    return loader->video_height;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_tans_tmediaplayer_frameloader_tMediaFrameLoader_getVideoFrameRgbaSizeNative(
        JNIEnv * env,
        jobject j_loader,
        jlong native_loader) {
    auto *loader = reinterpret_cast<tMediaFrameLoaderContext *>(native_loader);
    return loader->videoBuffer->rgbaSize;
}

extern "C" JNIEXPORT void JNICALL
Java_com_tans_tmediaplayer_frameloader_tMediaFrameLoader_getVideoFrameRgbaBytesNative(
        JNIEnv * env,
        jobject j_player,
        jlong native_loader,
        jbyteArray j_bytes) {
    auto *loader = reinterpret_cast<tMediaFrameLoaderContext *>(native_loader);
    env->SetByteArrayRegion(j_bytes, 0, loader->videoBuffer->rgbaSize,
                            reinterpret_cast<const jbyte *>(loader->videoBuffer->rgbaBuffer));
}

extern "C" JNIEXPORT void JNICALL
Java_com_tans_tmediaplayer_frameloader_tMediaFrameLoader_releaseNative(
        JNIEnv * env,
        jobject j_frame_loader,
        jlong native_loader) {
    auto *loader = reinterpret_cast<tMediaFrameLoaderContext*>(native_loader);
    if (loader == nullptr) {
        return;
    }
    return loader->release();
}


