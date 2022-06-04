package com.tans.tmediaplayer

import android.view.Surface
import java.util.concurrent.atomic.AtomicReference

class MediaPlayer {

    private val playerId: AtomicReference<Long?> by lazy {
        AtomicReference(null)
    }

    private val mediaWorker: MediaPlayerWorker by lazy {
        MediaPlayerWorker()
    }

    fun setupPlayer(filePath: String) {
        mediaWorker.postOpt {
            val optResult = setupPlayerNative(filePath)
            if (optResult.toInt() != OptResult.OptFail.code) {
                playerId.set(optResult)
            } else {
                playerId.set(null)
            }
        }
    }

    fun setSurface(surface: Surface?) {
        mediaWorker.postOpt {
            val id = playerId.get()
            if (id != null) {
                setWindowNative(id, surface)
            }
        }
    }

    fun releasePlayer() {
        mediaWorker.postOpt {
            val id = playerId.get()
            if (id != null) {
                releasePlayerNative(id)
            }
            mediaWorker.release()
        }
    }

    private external fun setupPlayerNative(filePath: String): Long

    private external fun getDurationNative(playerId: Long): Long

    private external fun setWindowNative(playerId: Long, surface: Surface?): Int

    private external fun resetPlayProgress(playerId: Long): Int

    private external fun decodeNextFrameNative(playerId: Long, dataId: Long): LongArray

    private external fun renderRawDataNative(playerId: Long, dataId: Long): Int

    private external fun newRawDataNative(playerId: Long): Long

    private external fun releaseRawDataNative(playerId: Long, dataId: Long): Int

    private external fun releasePlayerNative(playerId: Long)

    companion object {
        init {
            System.loadLibrary("tmediaplayer")
        }
    }
}