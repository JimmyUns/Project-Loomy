package io.github.jimmyuns.pocketloomy

import android.Manifest
import android.app.Activity
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.WindowInsets
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView

// The Talk button in the notification: a small card over whatever you're doing. It listens right away,
// sends what you said, shows Loomy's answer for a moment and goes away. Tap the card to talk again,
// tap outside to close.
class TalkActivity : Activity() {
    private val accent = Color.rgb(0xA8, 0x55, 0xF7)
    private val textC = Color.rgb(0xD9, 0xCE, 0xE6)
    private val dim = Color.rgb(0x84, 0x73, 0x99)
    private val ok = Color.rgb(0x8E, 0xE3, 0xB8)
    private val err = Color.rgb(0xFF, 0x7C, 0x96)

    private lateinit var title: TextView
    private lateinit var said: TextView
    private lateinit var meter: View
    private lateinit var card: LinearLayout
    private lateinit var ear: Ear
    private val main = Handler(Looper.getMainLooper())
    private var busy = false

    private fun dp(v: Int) = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v.toFloat(), resources.displayMetrics).toInt()

    override fun onCreate(saved: Bundle?) {
        super.onCreate(saved)
        val root = FrameLayout(this).apply { setOnClickListener { finish() } }
        card = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(22), dp(18), dp(22), dp(20))
            background = GradientDrawable().apply { cornerRadius = dp(18).toFloat(); setColor(Color.rgb(0x1A, 0x0E, 0x27)); setStroke(dp(1), accent) }
            setOnClickListener { if (!busy) listen() }
        }
        title = TextView(this).apply { textSize = 20f; typeface = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD); setTextColor(accent) }
        said = TextView(this).apply { textSize = 15f; typeface = Typeface.MONOSPACE; setTextColor(textC); setPadding(0, dp(8), 0, 0) }
        meter = View(this).apply { background = GradientDrawable().apply { cornerRadius = dp(3).toFloat(); setColor(accent) } }
        card.addView(title)
        card.addView(said)
        card.addView(meter, LinearLayout.LayoutParams(dp(8), dp(6)).apply { topMargin = dp(12) })
        root.addView(card, FrameLayout.LayoutParams(-1, -2, Gravity.BOTTOM).apply { setMargins(dp(14), 0, dp(14), dp(24)) })
        root.setOnApplyWindowInsetsListener { v, insets ->
            val b = if (Build.VERSION.SDK_INT >= 30) insets.getInsets(WindowInsets.Type.systemBars()).bottom
                    else @Suppress("DEPRECATION") insets.systemWindowInsetBottom
            v.setPadding(0, 0, 0, b)
            insets
        }
        setContentView(root)

        ear = Ear(this,
            partial = { said.text = it; said.setTextColor(textC) },
            level = { rms -> meter.layoutParams = meter.layoutParams.apply { width = dp(8 + (rms.coerceIn(0f, 10f) * 22).toInt()) }; meter.requestLayout() },
            heard = { got(it) },
            missed = { e ->
                show(Ear.message(e), "Tap to try again", err)
                if (Ear.isMiss(e)) Pocket.reportMiss(this, "")
                closeSoon(4000)
            })
        listen()
    }

    override fun onDestroy() {
        ear.destroy()
        main.removeCallbacksAndMessages(null)
        super.onDestroy()
    }

    private fun show(head: String, body: String, color: Int) {
        title.text = head
        title.setTextColor(color)
        said.text = body
        said.setTextColor(textC)
    }

    override fun onRequestPermissionsResult(code: Int, perms: Array<out String>, results: IntArray) {
        if (results.isNotEmpty() && results[0] == PackageManager.PERMISSION_GRANTED) listen()
        else { show("No microphone", "Pocket Loomy needs it to hear you.", err); closeSoon(4000) }
    }

    private fun closeSoon(ms: Long) { main.removeCallbacksAndMessages(null); main.postDelayed({ finish() }, ms) }

    private fun listen() {
        main.removeCallbacksAndMessages(null)
        Pocket.ready(this)?.let { show("Can't talk yet", it, err); closeSoon(4000); return }
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(arrayOf(Manifest.permission.RECORD_AUDIO), 1)
            show("Microphone", "Allow the microphone, then tap here.", dim)
            return
        }
        if (!ear.available()) { show("No speech recognizer", "Open the app and type instead.", err); closeSoon(4000); return }
        show("Listening...", "Say a command", accent)
        ear.start()
    }

    private fun got(heard: List<String>) {
        val cmd = heard.firstOrNull()?.let { Words.command(it) } ?: ""
        if (cmd.isEmpty()) {
            show("Didn't catch a command", heard.firstOrNull() ?: "", err)
            Pocket.reportMiss(this, heard.firstOrNull() ?: "")
            closeSoon(4000)
            return
        }
        val device = Pocket.device(this)
        val trust = Store(this).trust()
        if (device == null || trust == null) { show("Can't talk yet", "Pair with your PC in the app first.", err); closeSoon(4000); return }
        busy = true
        show("Talking to Loomy...", "\u203A $cmd", accent)
        Pocket.link.send(device, trust, Pocket.myName(this), cmd) { r ->
            Pocket.mark(r.isSuccess)
            runOnUiThread {
                busy = false
                r.onSuccess { show("\u203A $cmd", it.ifEmpty { "Done." }, ok) }.onFailure { show("\u203A $cmd", it.message ?: "Failed.", err) }
                closeSoon(4500)
            }
        }
    }
}
