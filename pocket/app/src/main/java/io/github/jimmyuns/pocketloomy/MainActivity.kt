package io.github.jimmyuns.pocketloomy

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.content.pm.PackageManager
import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.PowerManager
import android.provider.Settings
import android.text.InputType
import android.text.TextUtils
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.WindowInsets
import android.view.inputmethod.EditorInfo
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.Switch
import android.widget.TextView
import java.text.DateFormat
import java.util.Date

// The app screen (redesigned in 2.22, his brief): a big mic in the middle with Loomy's halftone dots rippling
// out of it (MicView), the latest line of the history at the top (tap it: the whole history over the screen),
// the Listening mode right under the mic, typing at the bottom, and everything else behind the gear (your PC
// and the On switch, Listening, Ellune, app settings). Until the phone is paired, the two setup steps sit over
// the mic. Drawn from state every time something changes (render()). Listening itself runs in PocketService
// (Voice), so it carries on outside the app. The notification is untouched (he loves it).
@SuppressLint("MissingPermission", "UseSwitchCompatOrMaterialCode", "SetTextI18n")
class MainActivity : Activity() {
    private val bg = Color.rgb(0x13, 0x09, 0x1C)
    private val card = Color.rgb(0x1A, 0x0E, 0x27)
    private val textC = Color.rgb(0xD9, 0xCE, 0xE6)
    private val bright = Color.rgb(0xF4, 0xEE, 0xFB)
    private val dim = Color.rgb(0x84, 0x73, 0x99)
    private val accent = Color.rgb(0xA8, 0x55, 0xF7)
    private val accentDim = Color.rgb(0x5C, 0x33, 0x87)
    private val ok = Color.rgb(0x8E, 0xE3, 0xB8)
    private val err = Color.rgb(0xFF, 0x7C, 0x96)

    private enum class Phase { IDLE, LISTEN_CODE, PAIRING, FORGETTING }   // the screen's own work; talking is Voice

    private lateinit var store: Store
    private lateinit var ear: Ear
    private var phase = Phase.IDLE
    private var transcript = ""          // the pairing code while it's heard
    private var answer = ""              // a problem or a note from the screen itself (pairing, forget)
    private var answerBad = false
    private var shownHistory = -1
    private var confirmId: String? = null    // pairing: the code was right, the PC is asking (this phone's ID)

    // the main screen
    private lateinit var chip: TextView
    private lateinit var pill: LinearLayout
    private lateinit var pillCmd: TextView
    private lateinit var pillAns: TextView
    private lateinit var mic: MicView
    private lateinit var live: TextView
    private lateinit var modeBtn: TextView
    private lateinit var typing: LinearLayout
    private lateinit var input: EditText
    private lateinit var setup: ScrollView
    private lateinit var step1: TextView
    private lateinit var step2: TextView
    private lateinit var codeBtn: Button
    // the layers over it
    private lateinit var logLayer: View
    private lateinit var log: LinearLayout
    private lateinit var settingsLayer: View
    // settings
    private var settingSwitch = false
    private lateinit var pcName: TextView
    private lateinit var pcSub: TextView
    private lateinit var onSwitch: Switch
    private lateinit var onLine: TextView
    private lateinit var pcButtons: LinearLayout
    private lateinit var modeSolo: CheckBox
    private lateinit var modeName: CheckBox
    private lateinit var cbPhone: CheckBox
    private lateinit var swNotif: Switch
    private lateinit var swCard: Switch
    private lateinit var swSpeed: Switch
    private lateinit var elluneLine: TextView
    private lateinit var elluneGuest: TextView
    private lateinit var elluneId: TextView
    private lateinit var elluneBtn: Button
    private var pinging = false

    private val onChange: () -> Unit = { render() }

    // ------------------------------------------------------------------ lifecycle
    override fun onCreate(saved: Bundle?) {
        super.onCreate(saved)
        store = Store(this)
        Pocket.fixStaleOn(this)
        ear = Ear(this,                      // only for hearing the pairing code
            partial = { transcript = it; render() },
            level = {},
            heard = { gotCode(it) },
            missed = { phase = Phase.IDLE; transcript = ""; problem(Ear.message(it)) })
        setContentView(buildUi())
        askPermissions()
        Notes.syncOff(this)
        render()
    }

    override fun onResume() { super.onResume(); Pocket.listen(onChange); Pocket.fixStaleOn(this); render() }
    override fun onPause() { Pocket.unlisten(onChange); super.onPause() }
    override fun onDestroy() { ear.destroy(); super.onDestroy() }

    @Deprecated("still called with targetSdk 35 (no predictive back opt-in)")
    override fun onBackPressed() {
        when {
            settingsLayer.visibility == View.VISIBLE -> { settingsLayer.visibility = View.GONE; render() }
            logLayer.visibility == View.VISIBLE -> { logLayer.visibility = View.GONE; render() }
            else -> @Suppress("DEPRECATION") super.onBackPressed()
        }
    }

    override fun onRequestPermissionsResult(code: Int, perms: Array<out String>, results: IntArray) {
        Notes.syncOff(this)
        render()
    }

    // ------------------------------------------------------------------ building the views
    private fun dp(v: Int) = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v.toFloat(), resources.displayMetrics).toInt()

    private fun text(s: String, color: Int, sp: Float = 15f, bold: Boolean = false) = TextView(this).apply {
        text = s
        setTextColor(color)
        textSize = sp
        typeface = if (bold) Typeface.create(Typeface.MONOSPACE, Typeface.BOLD) else Typeface.MONOSPACE
    }

    private fun rounded(color: Int, stroke: Int = 0, radius: Int = 12) = GradientDrawable().apply {
        cornerRadius = dp(radius).toFloat()
        setColor(color)
        if (stroke != 0) setStroke(dp(1), stroke)
    }

    private fun button(s: String, filled: Boolean, onClick: () -> Unit) = Button(this).apply {
        text = s
        isAllCaps = false
        typeface = Typeface.MONOSPACE
        setTextColor(if (filled) Color.WHITE else accent)
        background = if (filled) rounded(accent) else rounded(card, accent)
        setOnClickListener { onClick() }
    }

    private fun box(vararg views: View) = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        background = rounded(card)
        setPadding(dp(16), dp(14), dp(16), dp(14))
        for (v in views) addView(v)
    }

    private fun gap(h: Int) = View(this).apply { layoutParams = LinearLayout.LayoutParams(1, dp(h)) }
    private fun row(vararg views: View) = LinearLayout(this).apply { gravity = Gravity.CENTER_VERTICAL; for (v in views) addView(v) }
    private fun space(w: Int) = View(this).apply { layoutParams = LinearLayout.LayoutParams(dp(w), 1) }
    private fun heading(s: String) = text(s, accent, 13f, true).apply { setPadding(0, 0, 0, dp(6)) }

    // switches and checkboxes: purple when on, grey when off (the dark theme's own colours made "off" look on)
    private val states = arrayOf(intArrayOf(android.R.attr.state_checked), intArrayOf())
    private fun tint(sw: Switch) {
        sw.thumbTintList = ColorStateList(states, intArrayOf(accent, Color.rgb(0x9A, 0x8C, 0xA8)))
        sw.trackTintList = ColorStateList(states, intArrayOf(accentDim, Color.rgb(0x3A, 0x30, 0x44)))
    }
    private fun check(label: String, onFlip: (Boolean) -> Unit) = CheckBox(this).apply {
        text = label
        setTextColor(textC)
        typeface = Typeface.MONOSPACE
        buttonTintList = ColorStateList(states, intArrayOf(accent, dim))
        setOnCheckedChangeListener { _, on -> if (!settingSwitch) onFlip(on) }
    }

    /** a full-screen layer over the main screen: a title, a close button, and its content scrolling */
    private fun layer(title: String, close: String, content: View, onClose: () -> Unit): View {
        val col = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        col.addView(row(text(title, accent, 20f, true).apply { layoutParams = LinearLayout.LayoutParams(0, -2, 1f) },
                        button(close, false) { onClose() }))
        col.addView(gap(12))
        col.addView(ScrollView(this).apply { addView(content); isFillViewport = true }, LinearLayout.LayoutParams(-1, 0, 1f))
        return FrameLayout(this).apply {
            setBackgroundColor(bg)
            visibility = View.GONE
            isClickable = true                       // nothing under it gets touched
            addView(col)
            insetsPadding(col)
        }
    }

    // Android 15 draws under the bars: keep clear of them (and of the keyboard)
    private fun insetsPadding(v: View) {
        v.setOnApplyWindowInsetsListener { view, insets ->
            val (t, b) = if (Build.VERSION.SDK_INT >= 30) {
                val i = insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.ime()); i.top to i.bottom
            } else @Suppress("DEPRECATION") (insets.systemWindowInsetTop to insets.systemWindowInsetBottom)
            view.setPadding(dp(18), t + dp(12), dp(18), b + dp(12))
            insets
        }
    }

    private fun buildUi(): View {
        val root = FrameLayout(this).apply { setBackgroundColor(bg) }
        val main = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        insetsPadding(main)
        root.addView(main)

        // top: name, what's going on, the gear
        chip = text("", dim, 12f).apply { setPadding(dp(10), dp(4), dp(10), dp(4)) }
        val gear = GearView(this, textC).apply { setOnClickListener { openSettings() } }
        main.addView(row(text("Pocket Loomy", accent, 20f, true).apply { layoutParams = LinearLayout.LayoutParams(0, -2, 1f) },
                         chip, space(6), gear.apply { layoutParams = LinearLayout.LayoutParams(dp(42), dp(42)) }))
        main.addView(gap(10))

        // the latest line of the history; tap: all of it
        pillCmd = text("", accent, 13f).apply { maxLines = 1; ellipsize = TextUtils.TruncateAt.END; gravity = Gravity.CENTER_HORIZONTAL }
        pillAns = text("", textC, 13f).apply { maxLines = 2; ellipsize = TextUtils.TruncateAt.END; gravity = Gravity.CENTER_HORIZONTAL }
        pill = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            background = rounded(card, accentDim, 16)
            setPadding(dp(14), dp(10), dp(14), dp(10))
            addView(pillCmd); addView(pillAns)
            setOnClickListener { if (Voice.history.isNotEmpty()) { logLayer.visibility = View.VISIBLE; renderHistory(true); render() } }
        }
        main.addView(pill, LinearLayout.LayoutParams(-1, -2))

        // the middle: the mic (or, until paired, the setup steps over it)
        mic = MicView(this, accent, card, bright).apply { onTap = { talkPressed() } }
        step1 = text("", textC)
        step2 = text("", textC)
        codeBtn = button("Say the code", true) { startPairing() }
        setup = ScrollView(this).apply {
            addView(LinearLayout(this@MainActivity).apply {
                orientation = LinearLayout.VERTICAL
                gravity = Gravity.CENTER_VERTICAL
                addView(box(text("1  Your PC", accent, 13f, true), gap(6), step1, gap(10), button("Choose PC", false) { choosePc() }))
                addView(gap(12))
                addView(box(text("2  Pair", accent, 13f, true), gap(6), step2, gap(10), codeBtn))
            })
            isFillViewport = true
        }
        main.addView(FrameLayout(this).apply {
            addView(mic, FrameLayout.LayoutParams(-1, -1))
            addView(setup, FrameLayout.LayoutParams(-1, -2, Gravity.CENTER))
        }, LinearLayout.LayoutParams(-1, 0, 1f))

        // under the mic: what's happening, and the mode
        live = text("", accent, 15f).apply { gravity = Gravity.CENTER_HORIZONTAL; minLines = 2; maxLines = 3 }
        main.addView(live, LinearLayout.LayoutParams(-1, -2))
        modeBtn = text("", textC, 13f).apply {
            background = rounded(card, accentDim, 20)
            setPadding(dp(16), dp(8), dp(16), dp(8))
            setOnClickListener { chooseMode() }
        }
        main.addView(modeBtn, LinearLayout.LayoutParams(-2, -2).apply { gravity = Gravity.CENTER_HORIZONTAL; topMargin = dp(8) })

        // typing
        input = EditText(this).apply {
            hint = "or type a command"
            setHintTextColor(dim)
            setTextColor(textC)
            typeface = Typeface.MONOSPACE
            isSingleLine = true
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            imeOptions = EditorInfo.IME_ACTION_SEND
            background = rounded(card)
            setPadding(dp(12), dp(10), dp(12), dp(10))
            setOnEditorActionListener { _, action, _ -> if (action == EditorInfo.IME_ACTION_SEND) { sendTyped(); true } else false }
        }
        typing = row(input.apply { layoutParams = LinearLayout.LayoutParams(0, -2, 1f) }, space(8), button("Send", false) { sendTyped() })
        main.addView(typing, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(14) })

        // the history, over everything
        log = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        logLayer = layer("History", "Close", log) { logLayer.visibility = View.GONE; render() }
        root.addView(logLayer, FrameLayout.LayoutParams(-1, -1))

        // the settings, over everything
        settingsLayer = layer("Settings", "Done", buildSettings()) { settingsLayer.visibility = View.GONE; render() }
        root.addView(settingsLayer, FrameLayout.LayoutParams(-1, -1))
        return root
    }

    private fun buildSettings(): View {
        val col = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        fun section(v: View) = col.addView(v, LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = dp(14) })

        // your PC: the pairing, the On switch, change / forget
        pcName = text("", bright, 17f, true)
        pcSub = text("", dim, 12f)
        onSwitch = Switch(this).apply {
            text = "On  "
            setTextColor(textC)
            typeface = Typeface.MONOSPACE
            setOnCheckedChangeListener { _, checked -> if (!settingSwitch) toggleOn(checked) }
        }
        tint(onSwitch)
        onLine = text("", dim, 12f)
        pcButtons = row(button("Change PC", false) { choosePc() }, space(8), button("Forget", false) { confirmForget() })
        section(box(heading("Your PC"),
                    row(LinearLayout(this).apply {
                        orientation = LinearLayout.VERTICAL
                        layoutParams = LinearLayout.LayoutParams(0, -2, 1f)
                        addView(pcName); addView(pcSub)
                    }, onSwitch),
                    gap(6), onLine, gap(10), pcButtons))

        // listening: the mode (same as the button under the mic) and who hears you
        modeName = check("Say \u201CLoomy\u201D") { if (it) setMode(false) else render() }
        modeSolo = check("Solo") { if (it) setMode(true) else render() }
        cbPhone = check("Use the phone's recognizer") { store.phoneEar = it; render() }
        section(box(heading("Listening"),
                    modeName, text("Everything you say after her name is the command. Tapping the mic counts as saying it once.", dim, 12f),
                    gap(6), modeSolo, text("For when you're alone: everything you say is a command, no name needed.", dim, 12f),
                    gap(4), text("In both, a pause or \u201Cthank you\u201D ends the sentence. \u201CPlease\u201D is fine anywhere.", dim, 12f),
                    gap(12), cbPhone,
                    text("Off (normal): Ellune hears what you say to Loomy (much better with accents) and her AI understands phrases the phone doesn't know. Listening for \u201CLoomy\u201D stays on the phone. With no internet the phone's recognizer is the only option.\nOn: only the phone's recognizer and its set phrases.", dim, 12f)))

        // Ellune
        elluneLine = text("", dim, 13f).apply { setPadding(0, dp(6), 0, 0); visibility = View.GONE }
        elluneGuest = text("", dim, 13f).apply { visibility = View.GONE }
        elluneId = text("", dim, 12f).apply { setPadding(0, dp(6), 0, 0) }
        showElluneId(ElluneKey.peek())
        elluneBtn = button("Ping Ellune", false) { pingEllune() }
        section(box(heading("Ellune"),
                    text("Loomy's online sister. Ping checks this phone can reach her and is on her guest list.", dim, 12f),
                    elluneId, elluneLine, elluneGuest, gap(10),
                    row(elluneBtn, space(8), button("Add to guest list", false) { addToGuestList() })))

        // the app itself
        fun setting(label: String, about: String, onFlip: (Boolean) -> Unit) = Switch(this).apply {
            text = label
            setTextColor(textC)
            typeface = Typeface.MONOSPACE
            setPadding(0, dp(8), 0, 0)
            setOnCheckedChangeListener { _, on -> if (!settingSwitch) onFlip(on) }
            tint(this)
        } to text(about, dim, 12f)
        val (n1, n1a) = setting("Notification", "Pocket Loomy in your notifications. Off: none while it's off, and a tucked-away one while it runs.") {
            store.showNotif = it; Pocket.sync(this) }
        val (n3, n3a) = setting("Talk card", "The small card when you Talk from the notification. Off: it just listens, answers show in the notification.") {
            store.talkCard = it }
        val (n4, n4a) = setting("Net speed", "Your phone's down/up speed in the status bar, like a speed meter app. Works with Pocket Loomy off, paused while the screen is off.") {
            store.netSpeed = it
            if (!Pocket.sync(this)) { store.netSpeed = false; problem("Android didn't let the speed meter start.") }
            render()
        }
        swNotif = n1; swCard = n3; swSpeed = n4
        section(box(heading("App"), n1, n1a, n3, n3a, n4, n4a))
        return col
    }

    private fun openSettings() { settingsLayer.visibility = View.VISIBLE; render() }

    // ------------------------------------------------------------------ the mode
    private fun modeName(solo: Boolean) = if (solo) "Solo" else "Say \u201CLoomy\u201D"

    private fun chooseMode() {
        val items = arrayOf("Say \u201CLoomy\u201D: what you say after her name", "Solo: everything you say (when you're alone)")
        AlertDialog.Builder(this)
            .setTitle("Listening")
            .setSingleChoiceItems(items, if (store.solo) 1 else 0) { d, i -> setMode(i == 1); d.dismiss() }
            .show()
    }

    private fun setMode(solo: Boolean) {
        store.solo = solo
        render()
    }

    // ------------------------------------------------------------------ drawing from state
    private fun render() {
        val t = store.trust()
        val isPaired = t != null && t.valid()
        val listening = Voice.state == Voice.State.LISTENING

        settingSwitch = true
        onSwitch.text = if (store.on) "On  " else "Off  "
        onSwitch.isChecked = store.on
        swNotif.isChecked = store.showNotif
        swCard.isChecked = store.talkCard
        swSpeed.isChecked = store.netSpeed
        modeName.isChecked = !store.solo
        modeSolo.isChecked = store.solo
        cbPhone.isChecked = store.phoneEar
        settingSwitch = false

        // chip
        val (chipText, chipColor) = when {
            phase == Phase.LISTEN_CODE -> "listening" to accent
            phase == Phase.PAIRING -> "pairing" to accent
            phase == Phase.FORGETTING -> "forgetting" to accent
            Voice.sending -> "talking to Loomy" to accent
            listening && Voice.inputMode -> "input mode" to accent
            listening && Voice.viaEllune -> "Ellune is listening" to accent
            listening && Voice.keep && store.solo -> "solo" to accent
            listening && Voice.keep -> "listening for Loomy" to accent
            listening -> "listening" to accent
            !isPaired -> (if (t != null) "pairing ran out" else "not paired") to dim
            Pocket.linkState == LinkState.CONNECTING -> "connecting" to accent
            !Pocket.reachable -> "can't reach PC" to err
            Pocket.linkState == LinkState.CONNECTED -> "connected" to ok
            store.on -> "on" to ok
            else -> "off" to dim
        }
        chip.text = chipText
        chip.setTextColor(chipColor)
        chip.background = rounded(card, chipColor, 20)

        // the pill: the latest line of the history
        val last = Voice.last()
        if (last == null) {
            pillCmd.text = "History"
            pillCmd.setTextColor(dim)
            pillAns.text = "What you say and what Loomy answers shows here."
            pillAns.setTextColor(dim)
        } else {
            pillCmd.text = "\u203A ${last.cmd}"
            pillCmd.setTextColor(accent)
            pillAns.text = last.answer
            pillAns.setTextColor(if (last.bad) err else textC)
        }
        renderHistory(false)

        // settings: your PC
        if (t != null) {
            pcName.text = t.pcName
            pcSub.text = if (isPaired) "paired until " + DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT).format(Date(t.expiresAt))
                         else "the pairing ran out: pair again"
        } else {
            pcName.text = store.pcLabel ?: "No PC yet"
            pcSub.text = "not paired"
        }
        val seen = if (Pocket.lastSeen > 0) " Last answer from the PC at " + DateFormat.getTimeInstance(DateFormat.SHORT).format(Date(Pocket.lastSeen)) + "." else ""
        onLine.text = if (store.on) "On: Talk is in your notifications and the Loomy tile, and it checks in every 5 minutes.$seen"
                      else "Off: turn on to Talk from the notification or the Loomy quick-settings tile.$seen"
        onSwitch.visibility = if (isPaired) View.VISIBLE else View.GONE
        pcButtons.visibility = if (t != null) View.VISIBLE else View.GONE

        modeBtn.text = "${modeName(store.solo)}  \u25BE"

        if (!isPaired) {
            setup.visibility = View.VISIBLE
            mic.visibility = View.INVISIBLE
            typing.visibility = View.GONE
            modeBtn.visibility = View.GONE
            live.text = ""
            step1.text = store.pcLabel?.let { "\u2713 $it" } ?: "Pick your PC from the phone's Bluetooth devices. (Pair it in Android's Bluetooth settings first.)"
            step1.setTextColor(if (store.pcLabel != null) ok else textC)
            step2.text = when {
                t != null -> "The pairing ran out after 7 days. On the PC type /phone, then tap below and say the 8 digits."
                phase == Phase.LISTEN_CODE -> "Listening... say the 8 digits.  $transcript"
                phase == Phase.PAIRING && confirmId != null ->
                    "Right code. Now look at the PC: it shows this phone's ID. If it says $confirmId, press Enter on the PC."
                phase == Phase.PAIRING -> "Pairing with ${store.pcLabel}..."
                else -> "On the PC type /phone. Then tap below and say the 8 digits it shows."
            }
            codeBtn.isEnabled = store.pcLabel != null && phase == Phase.IDLE
            codeBtn.alpha = if (codeBtn.isEnabled) 1f else 0.4f
            codeBtn.text = when {
                phase == Phase.LISTEN_CODE -> "Listening..."
                phase == Phase.PAIRING && confirmId != null -> "This phone's ID: $confirmId"
                phase == Phase.PAIRING -> "Pairing..."
                else -> "Say the code"
            }
            step2.setTextColor(textC)
            if (answer.isNotEmpty() && phase == Phase.IDLE) {          // what went wrong (or a note), right where he looks
                step2.text = answer
                step2.setTextColor(if (answerBad) err else ok)
            }
            return
        }

        setup.visibility = View.GONE
        mic.visibility = View.VISIBLE
        typing.visibility = View.VISIBLE
        modeBtn.visibility = View.VISIBLE
        mic.mode = when {
            Voice.sending && !listening -> MicView.Mode.SENDING
            listening -> MicView.Mode.LISTENING
            else -> MicView.Mode.IDLE
        }
        mic.level = Voice.level
        mic.running = settingsLayer.visibility != View.VISIBLE && logLayer.visibility != View.VISIBLE

        val (liveText, liveColor) = when {
            listening && Voice.partial.isNotEmpty() -> Voice.partial to accent
            listening && Voice.inputMode -> "input mode: say next, back, ok, up, down... (\u201Cinput mode off\u201D to stop)" to accent
            listening && Voice.armed && !store.solo -> "Go ahead, I'm listening." to accent
            listening && store.solo -> "Just talk. A pause or \u201Cthank you\u201D ends a sentence. Tap the mic to stop." to accent
            listening -> "Say \u201CLoomy\u201D and a command. Tap the mic to stop." to accent
            Voice.sending -> "Talking to Loomy..." to accent
            answer.isNotEmpty() -> answer to (if (answerBad) err else ok)
            store.solo -> "Tap the mic and just talk." to dim
            else -> "Tap the mic and talk, or say \u201CLoomy ...\u201D while it listens." to dim
        }
        live.text = liveText
        live.setTextColor(liveColor)
    }

    // the full history (newest first), rebuilt when it changes and only while it's being looked at
    private fun renderHistory(force: Boolean) {
        if (!force && logLayer.visibility != View.VISIBLE) return
        val h = Voice.history
        val sig = h.size * 31 + (h.lastOrNull()?.answer?.length ?: 0)
        if (sig == shownHistory && !force) return
        shownHistory = sig
        log.removeAllViews()
        for (e in h.asReversed()) {
            log.addView(LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                background = rounded(card)
                setPadding(dp(14), dp(10), dp(14), dp(10))
                addView(text("\u203A ${e.cmd}", accent, 13f))
                addView(text(e.answer, if (e.bad) err else textC, 13f))
            }, LinearLayout.LayoutParams(-1, -2).apply { bottomMargin = dp(8) })
        }
    }

    private fun problem(s: String) { answer = s; answerBad = true; render() }
    private fun note(s: String) { answer = s; answerBad = false; render() }

    // ------------------------------------------------------------------ permissions, PC
    private fun askPermissions() {
        val p = mutableListOf(Manifest.permission.RECORD_AUDIO)
        if (Build.VERSION.SDK_INT >= 31) p += Manifest.permission.BLUETOOTH_CONNECT
        if (Build.VERSION.SDK_INT >= 33) p += Manifest.permission.POST_NOTIFICATIONS
        val missing = p.filter { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }
        if (missing.isNotEmpty()) requestPermissions(missing.toTypedArray(), 1)
    }

    private fun bluetoothOk(): Boolean {
        if (!Pocket.canConnect(this)) { problem("Pocket Loomy needs the Nearby devices permission to reach your PC."); askPermissions(); return false }
        val a = Pocket.adapter(this)
        if (a == null) { problem("This phone has no Bluetooth."); return false }
        if (!a.isEnabled) { problem("Turn Bluetooth on first."); return false }
        return true
    }

    private fun choosePc() {
        if (!bluetoothOk()) return
        val devices = Pocket.adapter(this)?.bondedDevices?.toList()?.sortedBy { it.name ?: it.address } ?: emptyList()
        if (devices.isEmpty()) {
            AlertDialog.Builder(this)
                .setTitle("No paired devices")
                .setMessage("Pair your PC in Android's Bluetooth settings first (and accept it on the PC), then come back.")
                .setPositiveButton("Bluetooth settings") { _, _ -> startActivity(Intent(Settings.ACTION_BLUETOOTH_SETTINGS)) }
                .setNegativeButton("Cancel", null)
                .show()
            return
        }
        val names = devices.map { "${it.name ?: "(no name)"}  ${it.address}" }.toTypedArray()
        AlertDialog.Builder(this)
            .setTitle("Which one is your PC?")
            .setItems(names) { _, i ->
                val d = devices[i]
                if (d.address != store.pcAddress && store.trust() != null) {
                    Pocket.turnOff(this)
                    store.forget()
                    Pocket.link.close()
                    note("New PC chosen, so the old pairing is gone. Pair again.")
                }
                store.pcAddress = d.address
                store.pcLabel = d.name ?: d.address
                render()
            }
            .show()
    }

    // ------------------------------------------------------------------ pairing
    private fun startPairing() {
        if (!bluetoothOk() || Pocket.device(this) == null) return
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            problem("Pocket Loomy needs the microphone to hear the code."); askPermissions(); return
        }
        if (!ear.available()) { problem("This phone has no speech recognizer (Google app / Speech Services)."); return }
        phase = Phase.LISTEN_CODE
        transcript = ""
        answer = ""
        render()
        ear.start()
    }

    private fun gotCode(heard: List<String>) {
        val code = heard.firstNotNullOfOrNull { Words.code(it) }
        if (code == null) { phase = Phase.IDLE; problem("Didn't hear 8 digits (heard: ${heard.firstOrNull() ?: "nothing"}). Try again."); return }
        val device = Pocket.device(this) ?: return
        phase = Phase.PAIRING
        transcript = "${code.substring(0, 4)} ${code.substring(4)}"
        render()
        confirmId = null
        Pocket.link.pair(device, code, Pocket.myName(this), waiting = { id -> runOnUiThread { confirmId = id; render() } }) { result ->
            runOnUiThread {
                phase = Phase.IDLE
                transcript = ""
                confirmId = null
                result.onSuccess { t ->
                    store.save(t)
                    Pocket.mark(true)
                    note("Paired with ${t.pcName}. Trusted for 7 days.")
                    offerOn()
                }.onFailure { problem(it.message ?: "Pairing failed.") }
            }
        }
    }

    // ------------------------------------------------------------------ on / off
    private fun offerOn() {
        if (store.on) return
        AlertDialog.Builder(this)
            .setTitle("Turn Pocket Loomy on?")
            .setMessage("Then Talk is one tap away in your notifications (and the Loomy quick-settings tile), and your PC sees the phone check in every 5 minutes.")
            .setPositiveButton("Turn on") { _, _ -> toggleOn(true) }
            .setNegativeButton("Not now", null)
            .show()
    }

    private fun toggleOn(want: Boolean) {
        if (!want) { Pocket.turnOff(this); render(); return }
        val why = Pocket.turnOn(this)
        if (why != null) { problem(why); return }
        askBatteryOnce()
        render()
    }

    // Samsung, Xiaomi and friends kill background apps hard; ask once to be left alone
    @SuppressLint("BatteryLife")
    private fun askBatteryOnce() {
        val pm = getSystemService(PowerManager::class.java)
        if (store.askedBattery || pm == null || pm.isIgnoringBatteryOptimizations(packageName)) return
        store.askedBattery = true
        AlertDialog.Builder(this)
            .setTitle("Keep Pocket Loomy running?")
            .setMessage("Some phones stop background apps to save battery, which would turn Pocket Loomy off. It only checks in every 5 minutes, so it costs very little.")
            .setPositiveButton("Allow") { _, _ ->
                try { startActivity(Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS, Uri.parse("package:$packageName"))) } catch (_: Exception) {}
            }
            .setNegativeButton("No thanks", null)
            .show()
    }

    // ------------------------------------------------------------------ forget
    private fun confirmForget() {
        AlertDialog.Builder(this)
            .setTitle("Forget ${Pocket.pcName(this)}?")
            .setMessage("This phone drops the pairing and tells the PC to drop it too. You'll need a new code to pair again.")
            .setPositiveButton("Forget") { _, _ -> forget() }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun forget() {
        val device = Pocket.device(this)
        val t = store.trust()
        val done = { told: Boolean ->
            phase = Phase.IDLE
            Pocket.turnOff(this)
            store.forget()
            Pocket.lastSeen = 0
            if (told) note("Forgot ${t?.pcName ?: "the PC"}, and the PC forgot this phone too.")
            else problem("Forgot the pairing here. The PC wasn't reachable, so type /phone forget on it too.")
        }
        if (device == null || t == null || !t.valid() || !Pocket.canConnect(this)) { done(false); return }
        Pocket.stopTalking(this)
        phase = Phase.FORGETTING
        render()
        Pocket.link.forget(device, t, Pocket.myName(this)) { r -> runOnUiThread { done(r.isSuccess) } }
    }

    // ------------------------------------------------------------------ Ellune
    private fun showElluneId(k: ElluneKey.Key?) {
        elluneId.text = if (k != null) "This phone's Ellune ID: ${k.id}" else "This phone gets its Ellune ID the first time you tap."
    }

    private fun pingEllune() {
        if (pinging) return
        pinging = true
        elluneBtn.isEnabled = false
        elluneBtn.text = "Pinging..."
        elluneGuest.visibility = View.GONE
        Thread({
            val r = Ellune.pingNow()
            val k = ElluneKey.get()
            val me = when {
                r !is Ellune.Result.Up -> null
                k == null -> Ellune.Me.NotGuest("this phone couldn't make its key")
                else -> Ellune.meNow(k.priv, k.pub)
            }
            runOnUiThread {
                if (isDestroyed) return@runOnUiThread
                pinging = false
                elluneBtn.isEnabled = true
                elluneBtn.text = "Ping Ellune"
                showElluneId(k)
                elluneLine.text = Ellune.message(r)
                elluneLine.setTextColor(if (r is Ellune.Result.Up) ok else err)
                elluneLine.visibility = View.VISIBLE
                if (me != null) {
                    elluneGuest.text = Ellune.meMessage(me)
                    elluneGuest.setTextColor(if (me is Ellune.Me.Guest) ok else err)
                    elluneGuest.visibility = View.VISIBLE
                }
            }
        }, "ellune-ping").start()
    }

    // Copies this phone's line for Ellune's DEVICES list and says where it goes. Only public data: the
    // private half of the key can't leave the Keystore.
    private fun addToGuestList() {
        Thread({
            val k = ElluneKey.get()
            runOnUiThread {
                if (isDestroyed) return@runOnUiThread
                if (k == null) { problem("This phone couldn't make its Ellune key."); return@runOnUiThread }
                showElluneId(k)
                getSystemService(android.content.ClipboardManager::class.java)
                    ?.setPrimaryClip(android.content.ClipData.newPlainText("Ellune guest line", k.line))
                AlertDialog.Builder(this)
                    .setTitle("Copied ${k.id}")
                    .setMessage("Put it on Ellune's guest list. The Cloudflare dashboard works in the phone's browser too, or tap Share to send the line to your PC.\n\n" +
                        "Cloudflare dashboard \u2192 Workers & Pages \u2192 ellune \u2192 Settings \u2192 Variables and Secrets.\n\n" +
                        "No DEVICES yet: Add, type Text, name DEVICES, paste as the value.\n" +
                        "Already there: edit it and paste on a new line.\n\n" +
                        "Deploy, then tap Ping Ellune here to check.\n\n" +
                        "It's only the public half of this phone's key, nothing secret.")
                    .setPositiveButton("OK", null)
                    .setNeutralButton("Share") { _, _ ->
                        startActivity(Intent.createChooser(Intent(Intent.ACTION_SEND).apply {
                            type = "text/plain"; putExtra(Intent.EXTRA_TEXT, k.line) }, "Send the guest line"))
                    }
                    .show()
            }
        }, "ellune-key").start()
    }

    // ------------------------------------------------------------------ commands
    private fun talkPressed() {
        answer = ""
        if (Voice.state != Voice.State.OFF) { Pocket.stopTalking(this); return }
        Pocket.talk(this, true)?.let { problem(it) }
    }

    private fun sendTyped() {
        val cmd = Words.command(input.text.toString())
        if (cmd.isEmpty()) return
        input.setText("")
        answer = ""
        Voice.send(this, cmd)
    }
}
