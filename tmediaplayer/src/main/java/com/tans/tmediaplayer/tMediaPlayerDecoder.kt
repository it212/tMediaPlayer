package com.tans.tmediaplayer

import android.os.Handler
import android.os.HandlerThread
import android.os.Message
import android.os.SystemClock
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicReference

@Suppress("ClassName")
internal class tMediaPlayerDecoder(
    private val player: tMediaPlayer,
    private val bufferManager: tMediaPlayerBufferManager
) {

    private val isLooperPrepared: AtomicBoolean by lazy { AtomicBoolean(false) }

    private val decoderThread: HandlerThread by lazy {
        object : HandlerThread("tMediaPlayerDecoderThread", Thread.MAX_PRIORITY) {
            override fun onLooperPrepared() {
                super.onLooperPrepared()
                isLooperPrepared.set(true)
            }
        }.apply { start() }
    }

    private val decoderHandler: Handler by lazy {
        while (!isLooperPrepared.get()) {}
        object : Handler(decoderThread.looper) {
            override fun dispatchMessage(msg: Message) {
                super.dispatchMessage(msg)
                val mediaInfo = player.getMediaInfo()
                if (mediaInfo == null) {
                    MediaLog.e(TAG, "DecoderHandler decode error media info is null.")
                    return
                }
                val state = getState()
                if (state == tMediaPlayerDecoderState.Released || state == tMediaPlayerDecoderState.NotInit) {
                    MediaLog.e(TAG, "DecoderHandler wrong state: $state")
                    return
                }
                when (msg.what) {
                    DECODE_MEDIA_FRAME -> {
                        if (state == tMediaPlayerDecoderState.Decoding) {
                            val buffer = bufferManager.requestDecodeBuffer()
                            if (buffer != null) {
                                synchronized(buffer) {
                                    if (getState() == tMediaPlayerDecoderState.Released) { return }
                                    val nativePlayer = player.getMediaInfo()?.nativePlayer
                                    if (nativePlayer != null) {
                                        when (player.decodeNativeInternal(nativePlayer, buffer.nativeBuffer).toDecodeResult()) {
                                            DecodeResult.Success -> {
                                                bufferManager.enqueueRenderBuffer(buffer)
                                                player.decodeSuccess()
                                                this.sendEmptyMessage(DECODE_MEDIA_FRAME)
                                            }
                                            DecodeResult.DecodeEnd -> {
                                                MediaLog.d(TAG, "Decode end.")
                                                bufferManager.enqueueRenderBuffer(buffer)
                                                this@tMediaPlayerDecoder.state.set(tMediaPlayerDecoderState.DecodingEnd)
                                                player.decodeSuccess()
                                            }
                                            DecodeResult.Fail -> {
                                                bufferManager.enqueueDecodeBuffer(buffer)
                                                MediaLog.e(TAG, "Decode fail.")
                                                this.sendEmptyMessage(DECODE_MEDIA_FRAME)
                                            }
                                        }
                                    } else {
                                        bufferManager.enqueueDecodeBuffer(buffer)
                                        MediaLog.e(TAG, "Native player is null.")
                                    }
                                }
                            } else {
                                this@tMediaPlayerDecoder.state.set(tMediaPlayerDecoderState.WaitingRender)
                                MediaLog.d(TAG, "Waiting render buffer.")
                            }
                        } else {
                            MediaLog.d(TAG, "Skip decode frame, because of state: $state")
                        }
                    }
                    REQUEST_DECODE -> {
                        if (state in listOf(
                                tMediaPlayerDecoderState.DecodingEnd,
                                tMediaPlayerDecoderState.WaitingRender,
                                tMediaPlayerDecoderState.Paused,
                                tMediaPlayerDecoderState.Prepared
                            )
                        ) {
                            this@tMediaPlayerDecoder.state.set(tMediaPlayerDecoderState.Decoding)
                            this.sendEmptyMessage(DECODE_MEDIA_FRAME)
                        } else {
                            MediaLog.d(TAG, "Skip request decode, because of state: $state")
                        }
                    }
                    REQUEST_PAUSE -> {
                        if (state == tMediaPlayerDecoderState.Decoding || state == tMediaPlayerDecoderState.WaitingRender) {
                            this@tMediaPlayerDecoder.state.set(tMediaPlayerDecoderState.Paused)
                        } else {
                            MediaLog.d(TAG, "Skip request pause, because of state: $state")
                        }
                    }
                    SEEK_TO -> {
                        val position = msg.obj as? Long
                        if (position != null) {
                            val start = SystemClock.uptimeMillis()
                            val buffer = bufferManager.requestDecodeBufferForce()
                            val seekResult = synchronized(buffer) {
                                player.seekToNativeInternal(
                                    nativePlayer = mediaInfo.nativePlayer,
                                    videoNativeBuffer = buffer.nativeBuffer,
                                    targetPtsInMillis = position
                                ).toOptResult()
                            }
                            val end = SystemClock.uptimeMillis()
                            player.handleSeekingBuffer(buffer, seekResult)
                            if (seekResult == OptResult.Success) {
                                MediaLog.d(TAG, "Seek to $position success, cost: ${end - start} ms.")
                            } else {
                                MediaLog.e(TAG, "Seek to $position fail, cost ${end - start} ms.")
                                bufferManager.enqueueDecodeBuffer(buffer)
                            }
                        }
                    }
                    else -> {}
                }
            }
        }
    }

    private val state: AtomicReference<tMediaPlayerDecoderState> by lazy { AtomicReference(tMediaPlayerDecoderState.NotInit) }

    fun prepare() {
        val lastState = getState()
        if (lastState == tMediaPlayerDecoderState.Released) {
            MediaLog.e(TAG, "Prepare fail, decoder has released.")
            return
        }
        decoderThread
        decoderHandler
        decoderHandler.removeMessages(DECODE_MEDIA_FRAME)
        decoderHandler.removeMessages(REQUEST_DECODE)
        decoderHandler.removeMessages(REQUEST_PAUSE)
        state.set(tMediaPlayerDecoderState.Prepared)
    }

    fun decode() {
        val state = getState()
        if (state != tMediaPlayerDecoderState.NotInit && state != tMediaPlayerDecoderState.Released) {
            decoderHandler.sendEmptyMessage(REQUEST_DECODE)
        }
    }

    fun pause() {
        val state = getState()
        if (state != tMediaPlayerDecoderState.NotInit && state != tMediaPlayerDecoderState.Released) {
            decoderHandler.sendEmptyMessage(REQUEST_PAUSE)
        }
    }

    fun seekTo(position: Long) {
        val msg = Message.obtain()
        msg.what = SEEK_TO
        msg.obj = position
        decoderHandler.sendMessage(msg)
    }

    fun checkDecoderBufferIfWaiting() {
        if (getState() == tMediaPlayerDecoderState.WaitingRender) {
            decode()
        }
    }

    fun release() {
        decoderHandler.removeMessages(DECODE_MEDIA_FRAME)
        decoderHandler.removeMessages(REQUEST_DECODE)
        decoderHandler.removeMessages(REQUEST_PAUSE)
        decoderThread.quit()
        decoderThread.quitSafely()
        this.state.set(tMediaPlayerDecoderState.Released)
    }

    fun getState(): tMediaPlayerDecoderState = state.get()

    companion object {

        private const val DECODE_MEDIA_FRAME = 0
        private const val REQUEST_DECODE = 1
        private const val REQUEST_PAUSE = 2
        private const val SEEK_TO = 3
        private const val TAG = "tMediaPlayerDecoder"
    }
}