package com.hcode91.pocketcentury

import android.graphics.Bitmap

object Native {
    init { System.loadLibrary("pocketcentury") }

    // must match Sy909::Button
    const val UP = 1 shl 0
    const val DOWN = 1 shl 1
    const val LEFT = 1 shl 2
    const val RIGHT = 1 shl 3
    const val A = 1 shl 4
    const val B = 1 shl 5
    const val START = 1 shl 6
    const val SELECT = 1 shl 7
    const val EXTRA = 1 shl 8

    @JvmStatic external fun create()
    /** Returns null on success, or an error message. */
    @JvmStatic external fun load(spi1: ByteArray, spi2: ByteArray): String?
    @JvmStatic external fun reset()
    @JvmStatic external fun setButtons(buttons: Int)
    @JvmStatic external fun runFrame(bitmap: Bitmap, audio: ShortArray): Int
    @JvmStatic external fun audioRate(): Int
    @JvmStatic external fun saveState(): ByteArray?
    @JvmStatic external fun loadState(data: ByteArray): Boolean
}
