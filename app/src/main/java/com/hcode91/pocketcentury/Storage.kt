package com.hcode91.pocketcentury

import android.content.Context
import android.net.Uri
import java.io.File
import java.util.zip.CRC32
import java.util.zip.ZipInputStream

object Storage {
    // CRC32s of the MAME 100in1rg set
    const val SPI1_CRC = 0x18db16f2L
    const val SPI2_CRC = 0x25e81e82L
    const val SPI1_SIZE = 0x100000
    const val SPI2_SIZE = 0x400000

    fun spi1File(ctx: Context) = File(ctx.filesDir, "by25q80awt.bin")
    fun spi2File(ctx: Context) = File(ctx.filesDir, "p25d32.bin")
    fun stateFile(ctx: Context, slot: Int) = File(File(ctx.filesDir, "states").apply { mkdirs() }, "sy909.st$slot")

    fun ready(ctx: Context) = spi1File(ctx).length() == SPI1_SIZE.toLong() && spi2File(ctx).length() == SPI2_SIZE.toLong()

    fun crc(data: ByteArray): Long = CRC32().apply { update(data) }.value

    private fun isSpi1(b: ByteArray) = b.size == SPI1_SIZE && b[0] == 'G'.code.toByte() && b[1] == 'P'.code.toByte() &&
        b[2] == '3'.code.toByte() && b[3] == 'X'.code.toByte()
    private fun isSpi2(b: ByteArray) = b.size == SPI2_SIZE

    /**
     * Imports a picked file: either the MAME set zip (containing both chips) or one of the
     * two .bin dumps. Returns a human readable result line.
     */
    fun import(ctx: Context, uri: Uri): String {
        val raw = ctx.contentResolver.openInputStream(uri)?.use { it.readBytes() } ?: return "Couldn't read that file"
        val found = mutableListOf<ByteArray>()
        if (raw.size >= 4 && raw[0] == 'P'.code.toByte() && raw[1] == 'K'.code.toByte()) {
            ZipInputStream(raw.inputStream()).use { zip ->
                while (true) {
                    val e = zip.nextEntry ?: break
                    if (!e.isDirectory) found += zip.readBytes()
                }
            }
        } else if (raw.size >= 6 && raw[0] == '7'.code.toByte() && raw[1] == 'z'.code.toByte()) {
            return "7z archives aren't supported. Use the .zip set or the two .bin files."
        } else {
            found += raw
        }
        val msgs = mutableListOf<String>()
        for (b in found) {
            when {
                isSpi1(b) -> { spi1File(ctx).writeBytes(b); msgs += "by25q80awt.bin " + if (crc(b) == SPI1_CRC) "OK" else "saved (unknown CRC)" }
                isSpi2(b) -> { spi2File(ctx).writeBytes(b); msgs += "p25d32.bin " + if (crc(b) == SPI2_CRC) "OK" else "saved (unknown CRC)" }
            }
        }
        return if (msgs.isEmpty()) "No SY-909 flash dumps found in that file" else msgs.joinToString(", ")
    }
}
