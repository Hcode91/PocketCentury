package com.hcode91.pocketcentury

import android.app.Activity
import android.content.Intent
import android.graphics.Color
import android.graphics.Typeface
import android.os.Bundle
import android.view.Gravity
import android.widget.*

class MainActivity : Activity() {

    private lateinit var status: TextView
    private lateinit var play: Button

    companion object {
        private const val REQ_IMPORT = 1
        private const val ACCENT = 0xFFE11D48.toInt()

        /** The request another app (BacklogQuest's home-screen tiles) sends to boot the console straight away. */
        const val ACTION_LAUNCH_GAME = "com.hcode91.pocket.LAUNCH_GAME"
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val pad = dp(20)
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(0xFF10141A.toInt())
            setPadding(pad, pad, pad, pad)
        }
        root.addView(TextView(this).apply {
            text = "PocketCentury"
            textSize = 30f
            setTypeface(typeface, Typeface.BOLD)
            setTextColor(Color.WHITE)
        })
        root.addView(TextView(this).apply {
            text = "Emulator for the 100-in-1 Retro Gaming Console (SY-909)"
            setTextColor(0xFF9AA4B2.toInt()); textSize = 14f
        })
        status = TextView(this).apply {
            setTextColor(0xFFD0D6DE.toInt()); textSize = 15f; setPadding(0, dp(20), 0, dp(20))
        }
        root.addView(status)

        root.addView(button("Import ROM files") {
            startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
                addCategory(Intent.CATEGORY_OPENABLE); type = "*/*"
                putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true)
            }, REQ_IMPORT)
        }, full())
        play = button("Play") { startActivity(Intent(this, EmulatorActivity::class.java)) }
        root.addView(play, full().apply { topMargin = dp(10) })

        root.addView(TextView(this).apply {
            text = "Pick the MAME 100in1rg .zip, or both flash dumps (by25q80awt.bin, 1 MB and p25d32.bin, 4 MB). " +
                "They're copied into the app, so you only need to do this once."
            setTextColor(0xFF7D8794.toInt()); textSize = 13f; gravity = Gravity.START
            setPadding(0, dp(24), 0, 0)
        })
        setContentView(root)

        // Started from a tile: boot straight away. Without the ROM files the menu stays open so
        // they can be imported.
        if (savedInstanceState == null && launchFrom(intent)) finish()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        if (launchFrom(intent)) finish()
    }

    private fun launchFrom(i: Intent?): Boolean {
        if (i?.action != ACTION_LAUNCH_GAME) return false
        if (!Storage.ready(this)) {
            Toast.makeText(this, "Import the ROM files first", Toast.LENGTH_LONG).show()
            return false
        }
        startActivity(Intent(this, EmulatorActivity::class.java))
        return true
    }

    override fun onResume() {
        super.onResume()
        refresh()
    }

    private fun refresh() {
        val s1 = Storage.spi1File(this).length() == Storage.SPI1_SIZE.toLong()
        val s2 = Storage.spi2File(this).length() == Storage.SPI2_SIZE.toLong()
        status.text = "by25q80awt.bin (1 MB): " + (if (s1) "ready" else "missing") +
            "\np25d32.bin (4 MB): " + (if (s2) "ready" else "missing")
        play.isEnabled = s1 && s2
        play.alpha = if (s1 && s2) 1f else 0.4f
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != REQ_IMPORT || resultCode != RESULT_OK || data == null) return
        val uris = mutableListOf<android.net.Uri>()
        data.clipData?.let { c -> for (i in 0 until c.itemCount) uris += c.getItemAt(i).uri }
        data.data?.let { if (it !in uris) uris += it }
        val result = uris.joinToString("\n") { Storage.import(this, it) }
        Toast.makeText(this, result, Toast.LENGTH_LONG).show()
        refresh()
    }

    private fun button(label: String, onClick: () -> Unit) = Button(this).apply {
        text = label
        isAllCaps = false
        textSize = 17f
        setTextColor(Color.WHITE)
        setBackgroundColor(ACCENT)
        setOnClickListener { onClick() }
    }

    private fun full() = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(52))

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()
}
