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

// Talk, from the notification. Listening itself runs in PocketService (so it can carry on in the
// background); this only starts it, and shows a small card over whatever you're doing unless the
// card is turned off in settings (then it gets out of the way right away and the notification shows it).
// Tap the card to talk again, tap outside to close it (keep-listening carries on).
class TalkActivity : Activity() {
    private val accent = Color.rgb(0xA8, 0x55, 0xF7)
    private val textC = Color.rgb(0xD9, 0xCE, 0xE6)
    private val dim = Color.rgb(0x84, 0x73, 0x99)
    private val ok = Color.rgb(0x8E, 0xE3, 0xB8)
    private val err = Color.rgb(0xFF, 0x7C, 0x96)

    private lateinit var title: TextView
    private lateinit var said: TextView
    private lateinit var meter: View
    private val main = Handler(Looper.getMainLooper())
    private var card = true
    private var startedAt = 0
    private var sawListening = false
    private val onChange: () -> Unit = { render() }

    private fun dp(v: Int) = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v.toFloat(), resources.displayMetrics).toInt()

    override fun onCreate(saved: Bundle?) {
        super.onCreate(saved)
        card = Store(this).talkCard
        val root = FrameLayout(this).apply { setOnClickListener { finish() } }
        val box = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(22), dp(18), dp(22), dp(20))
            background = GradientDrawable().apply { cornerRadius = dp(18).toFloat(); setColor(Color.rgb(0x1A, 0x0E, 0x27)); setStroke(dp(1), accent) }
            setOnClickListener { if (Voice.state == Voice.State.OFF) talk() }
        }
        title = TextView(this).apply { textSize = 20f; typeface = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD); setTextColor(accent) }
        said = TextView(this).apply { textSize = 15f; typeface = Typeface.MONOSPACE; setTextColor(textC); setPadding(0, dp(8), 0, 0) }
        meter = View(this).apply { background = GradientDrawable().apply { cornerRadius = dp(3).toFloat(); setColor(accent) } }
        box.addView(title)
        box.addView(said)
        box.addView(meter, LinearLayout.LayoutParams(dp(8), dp(6)).apply { topMargin = dp(12) })
        root.addView(box, FrameLayout.LayoutParams(-1, -2, Gravity.BOTTOM).apply { setMargins(dp(14), 0, dp(14), dp(24)) })
        root.setOnApplyWindowInsetsListener { v, insets ->
            val b = if (Build.VERSION.SDK_INT >= 30) insets.getInsets(WindowInsets.Type.systemBars()).bottom
                    else @Suppress("DEPRECATION") insets.systemWindowInsetBottom
            v.setPadding(0, 0, 0, b)
            insets
        }
        if (!card) { root.visibility = View.INVISIBLE; window.setDimAmount(0f) }
        setContentView(root)
        Pocket.listen(onChange)
        talk()
    }

    override fun onDestroy() {
        Pocket.unlisten(onChange)
        main.removeCallbacksAndMessages(null)
        super.onDestroy()
    }

    private fun talk() {
        main.removeCallbacksAndMessages(null)
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            if (!card) { finish(); return }
            requestPermissions(arrayOf(Manifest.permission.RECORD_AUDIO), 1)
            show("Microphone", "Allow the microphone, then tap here.", dim)
            return
        }
        val why = Pocket.talk(this, Store(this).keepListening)
        if (why != null) {
            if (!card) { Notes.off(this, why); finish(); return }
            show("Can't talk yet", why, err)
            closeSoon(4000)
            return
        }
        startedAt = Voice.history.size
        sawListening = false
        if (!card) main.postDelayed({ finish() }, 2500)     // in case listening never starts
        render()
    }

    override fun onRequestPermissionsResult(code: Int, perms: Array<out String>, results: IntArray) {
        if (results.isNotEmpty() && results[0] == PackageManager.PERMISSION_GRANTED) talk()
        else { show("No microphone", "Pocket Loomy needs it to hear you.", err); closeSoon(4000) }
    }

    private fun show(head: String, body: String, color: Int) {
        title.text = head
        title.setTextColor(color)
        said.text = body
    }

    private fun closeSoon(ms: Long) { main.removeCallbacksAndMessages(null); main.postDelayed({ finish() }, ms) }

    private fun head() = when {
        Voice.inputMode -> "Input mode"
        Voice.keep && Store(this).solo -> "Solo mode"
        Voice.keep -> "Listening for \u201CLoomy\u201D"
        else -> "Listening..."
    }

    private fun render() {
        val listening = Voice.state == Voice.State.LISTENING
        if (listening) sawListening = true
        if (!card) {                                   // no card: as soon as it listens, get out of the way
            if (sawListening) finish()
            return
        }
        val last = if (Voice.history.size > startedAt) Voice.history.last() else null
        when {
            Voice.sending && last != null -> show("Talking to Loomy...", "\u203A ${last.cmd}", accent)
            listening && Voice.partial.isNotEmpty() -> show(head(), Voice.partial, accent)
            last != null -> {
                show("\u203A ${last.cmd}", last.answer, if (last.bad) err else ok)
                if (!listening) closeSoon(4500)
            }
            listening -> show(head(), when {
                                  Voice.inputMode -> "Say next, back, ok, up, down..."
                                  Voice.keep && Store(this).solo -> "Just say a command"
                                  Voice.keep -> "Say \u201CLoomy\u201D and a command"
                                  else -> "Say a command" }, accent)
            sawListening -> closeSoon(1500)
        }
        val lvl = if (listening) Voice.level.coerceIn(0f, 10f) else 0f
        meter.layoutParams = meter.layoutParams.apply { width = dp(8 + (lvl * 22).toInt()) }
        meter.requestLayout()
    }
}
