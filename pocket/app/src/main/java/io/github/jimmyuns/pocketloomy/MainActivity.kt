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
    // the theme colour: Loomy purple in Ellune mode, a blue next to it in Bluetooth mode (2.22), animated between
    private val purple = Color.rgb(0xA8, 0x55, 0xF7)
    private val purpleDim = Color.rgb(0x5C, 0x33, 0x87)
    private val blue = Color.rgb(0x5A, 0x8B, 0xFF)
    private val blueDim = Color.rgb(0x2C, 0x46, 0x8A)
    private var accent = purple
    private var accentDim = purpleDim
    private val themers = mutableListOf<() -> Unit>()       // re-colour what was drawn in the theme colour
    private val grids = mutableListOf<GridDrawable>()
    private var themeAnim: android.animation.ValueAnimator? = null
    private val amber = Color.rgb(0xFF, 0xC8, 0x6B)
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
    private lateinit var bt: BtIconView
    private lateinit var powerBig: PowerView         // 2.22: off = a beating power button where the mic is
    private lateinit var powerSmall: PowerView       // on = a small power button at the top left (tap: off)
    private lateinit var gridLabel: TextView
    private val ticker = android.os.Handler(android.os.Looper.getMainLooper())
    private val recheck = object : Runnable {             // the connection bar, every 30 s while the app is open
        override fun run() { Net.refresh(this@MainActivity); ticker.postDelayed(this, if (store.btMode) 60_000 else 30_000) }
    }
    private lateinit var live: TextView
    private lateinit var modeBtn: TextView
    private lateinit var typing: LinearLayout
    private lateinit var input: EditText
    private lateinit var setup: ScrollView
    private lateinit var step1: TextView
    private lateinit var step2: TextView
    private lateinit var codeBtn: Button
    private lateinit var setupBt: LinearLayout       // Bluetooth mode: your PC + the code (said or typed)
    private lateinit var setupNet: LinearLayout      // Ellune mode: scan the QR in /remote
    private lateinit var netStep: TextView
    private lateinit var codeInput: EditText
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
    private lateinit var modeBtn2: TextView
    private var onProblem = ""             // why the On switch couldn't turn on (shown right under it)
    private var flashSeen: String? = null  // the history line the mic last reacted to (null = not drawn yet)
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
        Pocket.appCtx = applicationContext
        if (store.btMode) { accent = blue; accentDim = blueDim }
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
        handleLink(intent)
    }

    override fun onNewIntent(i: Intent) { super.onNewIntent(i); setIntent(i); handleLink(i) }

    // ------------------------------------------------------------------ setup actions (2.22)
    /** the phone's own camera app (it reads QR codes and opens the pocketloomy:// link back into here) */
    private fun openCamera() {
        answer = ""
        try { startActivity(Intent(android.provider.MediaStore.INTENT_ACTION_STILL_IMAGE_CAMERA)) }
        catch (_: Exception) { problem("Couldn't open the camera. Open it yourself and point it at the QR.") }
    }

    /** Bluetooth mode: the code typed instead of said */
    private fun typedCode() {
        val digits = codeInput.text.toString().filter { it.isDigit() }
        if (digits.length != 8) { problem("The code is 8 digits."); return }
        if (store.pcLabel == null) { problem("Choose your PC first (step 1)."); return }
        if (phase != Phase.IDLE || !bluetoothOk()) return
        codeInput.setText("")
        phase = Phase.LISTEN_CODE
        gotCode(listOf(digits))
    }

    // ------------------------------------------------------------------ pairing by QR (2.22)
    // The QR in /remote opens this with pocketloomy://pair?c=<code>&r=<pairing room>&n=<PC name>. The phone must be
    // on Ellune's guest list first (his call: the Authy code, the QR alone isn't enough), then it pairs through
    // that room: the same pairing as over Bluetooth, the same Enter on the PC.
    private fun handleLink(i: Intent?) {
        val u = i?.data ?: return
        if (u.scheme != "pocketloomy" || u.host != "pair") return
        i.data = null                                        // once
        val code = u.getQueryParameter("c") ?: ""
        val room = u.getQueryParameter("r") ?: ""
        val name = u.getQueryParameter("n") ?: "your PC"
        if (!Regex("\\d{8}").matches(code) || !Regex("[0-9a-f]{32}").matches(room) || !Regex("[A-Za-z0-9_-]{1,40}").matches(name)) {
            problem("That QR isn't a Loomy pairing code."); return
        }
        if (phase != Phase.IDLE) return
        phase = Phase.PAIRING
        confirmId = null
        render()
        Thread({
            val k = ElluneKey.get()
            val me = if (k != null) Ellune.meNow(k.priv, k.pub) else null
            runOnUiThread {
                if (isDestroyed) return@runOnUiThread
                when {
                    k == null -> { phase = Phase.IDLE; problem("This phone couldn't make its Ellune key.") }
                    me is Ellune.Me.Guest -> qrPair(k, code, room, name)
                    me is Ellune.Me.NotGuest -> askAuthyThenPair(k, code, room, name)
                    else -> { phase = Phase.IDLE; problem("Couldn't reach Ellune. QR pairing needs internet.") }
                }
            }
        }, "qr-pair").start()
    }

    private fun askAuthyThenPair(k: ElluneKey.Key, code: String, room: String, name: String) {
        val box = codeBox()
        AlertDialog.Builder(this)
            .setTitle("First: add this phone to Ellune")
            .setMessage("Type the code Authy shows for Ellune.")
            .setView(LinearLayout(this).apply { setPadding(dp(20), 0, dp(20), 0); addView(box, LinearLayout.LayoutParams(-1, -2)) })
            .setPositiveButton("Add and pair") { _, _ ->
                val c = box.text.toString()
                Thread({
                    val why = Ellune.registerNow(k.priv, k.pub, c, "phone-${k.id}")
                    runOnUiThread {
                        if (isDestroyed) return@runOnUiThread
                        if (why != null) { phase = Phase.IDLE; problem(why) } else qrPair(k, code, room, name)
                    }
                }, "qr-register").start()
            }
            .setNegativeButton("Cancel") { _, _ -> phase = Phase.IDLE; render() }
            .setOnCancelListener { phase = Phase.IDLE; render() }
            .show()
    }

    private fun qrPair(k: ElluneKey.Key, code: String, room: String, name: String) {
        store.pcLabel = name
        Pocket.link.pair(null, code, Pocket.myName(this), waiting = { id -> runOnUiThread { confirmId = id; render() } },
                         via = { EllunePipe.open(room, k) }) { result ->
            runOnUiThread {
                phase = Phase.IDLE
                confirmId = null
                result.onSuccess { t ->
                    store.save(t)
                    if (store.btMode) { store.btMode = false; bt.active = false; animateTheme(false) }   // a QR pairing is online
                    Pocket.syncPipe(this)
                    Net.refresh(this)
                    note("Paired with ${t.pcName} through Ellune. Trusted for 7 days.")
                    offerOn()
                }.onFailure { problem(it.message ?: "Pairing failed.") }
            }
        }
    }

    override fun onResume() { super.onResume(); Pocket.listen(onChange); Pocket.fixStaleOn(this); ticker.post(recheck); render() }
    override fun onPause() { ticker.removeCallbacks(recheck); Pocket.unlisten(onChange); super.onPause() }
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
        if (color == accent) themers += { setTextColor(accent) }
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
        val paint = {
            setTextColor(if (filled) Color.WHITE else accent)
            background = if (filled) rounded(accent) else rounded(card, accent)
        }
        paint()
        themers += paint
        setOnClickListener { onClick() }
    }

    /** a rounded card with a theme-coloured edge (kept in step with the theme) */
    private fun edged(v: View, radius: Int) { val p = { v.background = rounded(card, accentDim, radius) }; p(); themers += p }

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
        val p = {
            sw.thumbTintList = ColorStateList(states, intArrayOf(accent, Color.rgb(0x9A, 0x8C, 0xA8)))
            sw.trackTintList = ColorStateList(states, intArrayOf(accentDim, Color.rgb(0x3A, 0x30, 0x44)))
        }
        p(); themers += p
    }
    private fun check(label: String, onFlip: (Boolean) -> Unit) = CheckBox(this).apply {
        text = label
        setTextColor(textC)
        typeface = Typeface.MONOSPACE
        val p = { buttonTintList = ColorStateList(states, intArrayOf(accent, dim)) }
        p(); themers += p
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
            background = grid()
            visibility = View.GONE
            isClickable = true                       // nothing under it gets touched
            addView(col)
            insetsPadding(col)
        }
    }

    private fun gridColor() = Color.argb((store.gridOpacity * 255 / 100), Color.red(accent), Color.green(accent), Color.blue(accent))
    private fun grid() = GridDrawable(gridColor(), bg, dp(26).toFloat()).also { grids += it }

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
        val root = FrameLayout(this).apply { background = grid() }
        val main = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        insetsPadding(main)
        root.addView(main)

        // top: name, what's going on, the gear
        chip = text("", dim, 12f).apply { setPadding(dp(10), dp(4), dp(10), dp(4)); setOnClickListener { Net.state = Net.State.CHECKING; Net.refresh(this@MainActivity); render() } }
        val gear = GearView(this, textC).apply { setOnClickListener { openSettings() } }
        powerSmall = PowerView(this, accent, card, false).apply { setOnClickListener { toggleOn(false) } }
        main.addView(row(powerSmall.apply { layoutParams = LinearLayout.LayoutParams(dp(34), dp(34)).apply { rightMargin = dp(8) } },
                         text("Pocket Loomy", accent, 20f, true).apply { layoutParams = LinearLayout.LayoutParams(0, -2, 1f) },
                         chip, space(6), gear.apply { layoutParams = LinearLayout.LayoutParams(dp(42), dp(42)) }))
        main.addView(gap(10))

        // the latest line of the history; tap: all of it
        pillCmd = text("", accent, 13f).apply { maxLines = 1; ellipsize = TextUtils.TruncateAt.END; gravity = Gravity.CENTER_HORIZONTAL }
        pillAns = text("", textC, 13f).apply { maxLines = 2; ellipsize = TextUtils.TruncateAt.END; gravity = Gravity.CENTER_HORIZONTAL }
        pill = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            edged(this, 16)
            setPadding(dp(14), dp(10), dp(14), dp(10))
            addView(pillCmd); addView(pillAns)
            setOnClickListener { if (Voice.history.isNotEmpty()) { logLayer.visibility = View.VISIBLE; renderHistory(true); render() } }
        }
        main.addView(pill, LinearLayout.LayoutParams(-1, -2))

        // the middle: the mic (or, until paired, the setup steps over it)
        mic = MicView(this, accent, card, bright).apply {
            onTap = { talkPressed() }
            onHold = { if (Voice.state != Voice.State.OFF) Pocket.stopTalking(this@MainActivity) }   // hold = mute
        }
        powerBig = PowerView(this, accent, card, true).apply { setOnClickListener { toggleOn(true) } }
        step1 = text("", textC)
        step2 = text("", textC)
        codeBtn = button("Say the code", true) { startPairing() }
        // 2.22: the setup follows the mode (his ask). Ellune mode: scan the QR. Bluetooth mode: the PC, then the code
        // said or typed.
        codeInput = EditText(this).apply {
            hint = "8-digit code"
            inputType = InputType.TYPE_CLASS_NUMBER
            filters = arrayOf(android.text.InputFilter.LengthFilter(9))
            setTextColor(textC); setHintTextColor(dim); typeface = Typeface.MONOSPACE
            background = rounded(bg)
            setPadding(dp(12), dp(10), dp(12), dp(10))
            imeOptions = EditorInfo.IME_ACTION_DONE
            setOnEditorActionListener { _, _, _ -> typedCode(); true }
        }
        setupBt = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(box(text("1  Your PC", accent, 13f, true), gap(6), step1, gap(10), button("Choose PC", false) { choosePc() }))
            addView(gap(12))
            addView(box(text("2  Pair", accent, 13f, true), gap(6), step2, gap(10), codeBtn, gap(10),
                        row(codeInput.apply { layoutParams = LinearLayout.LayoutParams(0, -2, 1f) }, space(8), button("Pair", false) { typedCode() })))
        }
        netStep = text("On the PC type /remote and press N: a QR shows up. Scan it with your camera, then press Enter on the PC when it asks.", textC)
        setupNet = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(box(text("Pair with your PC", accent, 13f, true), gap(6), netStep, gap(12),
                        button("Scan the QR", true) { openCamera() }))
        }
        setup = ScrollView(this).apply {
            addView(LinearLayout(this@MainActivity).apply {
                orientation = LinearLayout.VERTICAL
                gravity = Gravity.CENTER_VERTICAL
                addView(setupNet)
                addView(setupBt)
            })
            isFillViewport = true
        }
        bt = BtIconView(this, accent).apply { active = store.btMode; setOnClickListener { toggleBt() } }
        main.addView(FrameLayout(this).apply {
            addView(mic, FrameLayout.LayoutParams(-1, -1))
            addView(powerBig, FrameLayout.LayoutParams(-1, -1))
            addView(bt, FrameLayout.LayoutParams(dp(40), dp(40), Gravity.TOP or Gravity.END).apply { topMargin = dp(8) })
            addView(setup, FrameLayout.LayoutParams(-1, -2, Gravity.CENTER))
            bt.bringToFront()                                   // the mode button works on the setup screen too
        }, LinearLayout.LayoutParams(-1, 0, 1f))

        // under the mic: what's happening, and the mode
        live = text("", accent, 15f).apply { gravity = Gravity.CENTER_HORIZONTAL; minLines = 2; maxLines = 3 }
        main.addView(live, LinearLayout.LayoutParams(-1, -2))
        modeBtn = text("", textC, 13f).apply {
            edged(this, 20)
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
        modeBtn2 = text("", textC, 13f).apply {
            edged(this, 20)
            setPadding(dp(16), dp(8), dp(16), dp(8))
            setOnClickListener { chooseMode() }
        }
        cbPhone = check("Use the phone's recognizer") { store.phoneEar = it; render() }
        section(box(heading("Listening"),
                    row(modeBtn2), gap(6),
                    text("Say \u201CLoomy\u201D: everything you say after her name is the command (tapping the mic counts as saying it). Solo: everything you say is a command. A pause or \u201Cthank you\u201D ends the sentence.", dim, 12f),
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
                    row(elluneBtn, space(8), button("Add to guest list", false) { addToGuestList() }),
                    gap(8), button("Devices on Ellune", false) { showDevices() }))

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

        // visuals: how strong the background grid is
        gridLabel = text("", textC, 14f)
        val slider = android.widget.SeekBar(this).apply {
            max = 100
            progress = store.gridOpacity
            val p = { progressTintList = ColorStateList.valueOf(accent); thumbTintList = ColorStateList.valueOf(accent) }
            p(); themers += p
            setOnSeekBarChangeListener(object : android.widget.SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(sb: android.widget.SeekBar, v: Int, user: Boolean) {
                    if (!user) return
                    store.gridOpacity = v
                    gridLabel.text = "Background grid: $v%"
                    for (g in grids) g.setLine(gridColor())
                }
                override fun onStartTrackingTouch(sb: android.widget.SeekBar) {}
                override fun onStopTrackingTouch(sb: android.widget.SeekBar) {}
            })
        }
        gridLabel.text = "Background grid: ${store.gridOpacity}%"
        section(box(heading("Visuals"), gridLabel, gap(4), slider))
        return col
    }

    private fun openSettings() { settingsLayer.visibility = View.VISIBLE; render() }

    // ------------------------------------------------------------------ Ellune mode / Bluetooth mode
    private fun toggleBt() {
        val toBt = !store.btMode
        store.btMode = toBt
        bt.active = toBt
        Net.state = Net.State.CHECKING
        Net.refresh(this)
        if (Voice.state != Voice.State.OFF) { Pocket.stopTalking(this); Pocket.talk(this, true) }   // listen the new way
        Pocket.sync(this)
        animateTheme(toBt)
    }

    private fun animateTheme(toBt: Boolean) {
        themeAnim?.cancel()
        val from = accent
        val fromDim = accentDim
        val to = if (toBt) blue else purple
        val toDim = if (toBt) blueDim else purpleDim
        themeAnim = android.animation.ValueAnimator.ofFloat(0f, 1f).apply {
            duration = 650
            interpolator = android.view.animation.DecelerateInterpolator()
            addUpdateListener {
                val k = it.animatedValue as Float
                accent = blend(from, to, k)
                accentDim = blend(fromDim, toDim, k)
                applyTheme()
            }
            start()
        }
    }

    private fun applyTheme() {
        for (t in themers) t()
        for (g in grids) g.setLine(gridColor())
        mic.accent = accent
        bt.accent = accent
        powerBig.accent = accent
        powerSmall.accent = accent
        render()
    }

    private fun blend(a: Int, b: Int, k: Float) = Color.rgb(
        (Color.red(a) + (Color.red(b) - Color.red(a)) * k).toInt(),
        (Color.green(a) + (Color.green(b) - Color.green(a)) * k).toInt(),
        (Color.blue(a) + (Color.blue(b) - Color.blue(a)) * k).toInt())

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
        cbPhone.isChecked = store.phoneEar || store.btMode
        cbPhone.isEnabled = !store.btMode                       // Bluetooth mode never uses Ellune
        cbPhone.alpha = if (store.btMode) 0.45f else 1f
        settingSwitch = false

        // the bar on top: the connection (his five states); the mic shows the listening
        val (chipText, chipColor) = when {
            phase == Phase.LISTEN_CODE -> "pairing" to accent
            phase == Phase.PAIRING -> "pairing" to accent
            phase == Phase.FORGETTING -> "forgetting" to accent
            !isPaired -> (if (t != null) "pairing ran out" else "not paired") to dim
            else -> Net.state.label to when (Net.state) {
                Net.State.ONLINE, Net.State.ONLINE_BT -> ok
                Net.State.PC_OFFLINE -> amber
                Net.State.CHECKING -> dim
                else -> err
            }
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
        if (store.on) onProblem = ""
        onLine.text = when {
            onProblem.isNotEmpty() -> "Couldn't turn on: $onProblem"
            store.on && store.btMode -> "On: Talk is in your notifications and the Loomy tile, and it checks in with the PC over Bluetooth every 5 minutes.$seen"
            store.on -> "On: Talk is in your notifications and the Loomy tile, and it checks on Ellune every 5 minutes."
            else -> "Off: turn on to Talk from the notification or the Loomy quick-settings tile.$seen"
        }
        onLine.setTextColor(if (onProblem.isNotEmpty()) err else dim)
        onSwitch.visibility = if (isPaired) View.VISIBLE else View.GONE
        pcButtons.visibility = if (t != null) View.VISIBLE else View.GONE

        modeBtn.text = "${modeName(store.solo)}  \u25BE"
        modeBtn2.text = modeBtn.text

        if (!isPaired || phase == Phase.PAIRING) {
            powerBig.visibility = View.GONE
            powerSmall.visibility = View.INVISIBLE
            setup.visibility = View.VISIBLE
            setupBt.visibility = if (store.btMode) View.VISIBLE else View.GONE
            setupNet.visibility = if (store.btMode) View.GONE else View.VISIBLE
            netStep.text = when {
                phase == Phase.PAIRING && confirmId != null -> "Right code. Look at the PC: it shows this phone's ID. If it says $confirmId, press Enter on the PC."
                phase == Phase.PAIRING -> "Pairing with ${store.pcLabel ?: "your PC"} through Ellune\u2026"
                answer.isNotEmpty() -> answer
                else -> "On the PC type /remote and press N: a QR shows up. Scan it with your camera, then press Enter on the PC when it asks."
            }
            netStep.setTextColor(if (answer.isNotEmpty() && phase == Phase.IDLE) (if (answerBad) err else ok) else if (phase == Phase.PAIRING) accent else textC)
            mic.visibility = View.INVISIBLE
            typing.visibility = View.GONE
            modeBtn.visibility = View.GONE
            live.text = ""
            step1.text = store.pcLabel?.let { "\u2713 $it" } ?: "Easiest: on the PC type /remote and scan the QR with your camera. Or pick your PC from the phone's Bluetooth devices."
            step1.setTextColor(if (store.pcLabel != null) ok else textC)
            step2.text = when {
                t != null -> "The pairing ran out after 7 days. On the PC type /phone, then tap below and say the 8 digits."
                phase == Phase.LISTEN_CODE -> "Listening... say the 8 digits.  $transcript"
                phase == Phase.PAIRING && confirmId != null ->
                    "Right code. Now look at the PC: it shows this phone's ID. If it says $confirmId, press Enter on the PC."
                phase == Phase.PAIRING -> "Pairing with ${store.pcLabel ?: "your PC"}..."
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
        // off: the beating power button instead of the mic, and no off button at the top
        mic.visibility = if (store.on) View.VISIBLE else View.INVISIBLE
        powerBig.visibility = if (store.on) View.GONE else View.VISIBLE
        powerSmall.visibility = if (store.on) View.VISIBLE else View.INVISIBLE
        typing.visibility = View.VISIBLE
        modeBtn.visibility = View.VISIBLE
        val taking = Voice.takingCommand(this)
        mic.mode = when {
            Voice.thinking || Voice.sending -> MicView.Mode.THINKING
            listening && taking -> MicView.Mode.LISTENING
            listening -> MicView.Mode.WAITING
            else -> MicView.Mode.IDLE
        }
        mic.level = Voice.level
        mic.running = settingsLayer.visibility != View.VISIBLE && logLayer.visibility != View.VISIBLE
        // a flash when an answer comes back: green it worked, red it didn't
        val lineKey = last?.let { "${System.identityHashCode(it)}|${it.answer}" } ?: ""
        if (flashSeen != null && lineKey != flashSeen && last != null && last.answer != "...") mic.flash(!last.bad)
        flashSeen = lineKey                     // first draw: just remember, nothing to flash

        // a few words at most: the mic shows the rest
        val heardWords = Voice.partial.takeIf { listening && taking && it.isNotEmpty() && !it.endsWith("...") && it != "Yes?" }
        val (liveText, liveColor) = when {
            // pairing by QR while an old pairing is still on this phone: say what to do, right here
            phase == Phase.PAIRING && confirmId != null -> "Look at the PC: press Enter there if it shows $confirmId" to accent
            phase == Phase.PAIRING -> "Pairing with ${store.pcLabel ?: "your PC"} through Ellune\u2026" to accent
            Voice.thinking -> "Ellune is understanding\u2026" to accent
            Voice.sending -> "Sending to your PC\u2026" to accent
            heardWords != null -> "\u201C$heardWords\u201D" to textC
            listening && Voice.inputMode -> "Input mode" to accent
            listening && taking && !store.btMode -> "Loomy is listening \u00B7 tap: send \u00B7 hold: mute" to accent
            listening && taking -> "Loomy is listening" to accent
            listening -> "Say \u201CLoomy\u201D" to dim
            answer.isNotEmpty() -> answer to (if (answerBad) err else ok)
            !store.on -> "Off. Tap the power button to turn Pocket Loomy on." to dim
            else -> "Tap the mic" to dim
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
        onProblem = ""
        if (!want) { Pocket.turnOff(this); render(); return }
        val why = Pocket.turnOn(this)
        if (why != null) {                       // say why where he's looking (it used to land behind the settings)
            onProblem = why
            problem(why)
            return
        }
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

    // 2.22: this phone puts itself on Ellune's guest list with the current Authy code (his idea). Copying the line
    // for her DEVICES list stays as the backup.
    private fun codeBox(): EditText = EditText(this).apply {
        hint = "6-digit Authy code (Ellune)"
        inputType = InputType.TYPE_CLASS_NUMBER
        filters = arrayOf(android.text.InputFilter.LengthFilter(6))
        setTextColor(textC); setHintTextColor(dim); typeface = Typeface.MONOSPACE
    }

    private fun addToGuestList() {
        Thread({
            val k = ElluneKey.get()
            runOnUiThread {
                if (isDestroyed) return@runOnUiThread
                if (k == null) { problem("This phone couldn't make its Ellune key."); return@runOnUiThread }
                showElluneId(k)
                val box = codeBox()
                AlertDialog.Builder(this)
                    .setTitle("Add ${k.id} to Ellune")
                    .setMessage("Type the code Authy shows for Ellune.")
                    .setView(LinearLayout(this).apply { setPadding(dp(20), 0, dp(20), 0); addView(box, LinearLayout.LayoutParams(-1, -2)) })
                    .setPositiveButton("Add") { _, _ ->
                        val code = box.text.toString()
                        Thread({
                            val why = Ellune.registerNow(k.priv, k.pub, code, "phone-${k.id}")
                            runOnUiThread {
                                if (isDestroyed) return@runOnUiThread
                                elluneGuest.text = why ?: "This phone is on her guest list now (${k.id})."
                                elluneGuest.setTextColor(if (why == null) ok else err)
                                elluneGuest.visibility = View.VISIBLE
                                if (why == null) { Net.refresh(this); Pocket.syncPipe(this) }
                            }
                        }, "ellune-register").start()
                    }
                    .setNeutralButton("Copy the line instead") { _, _ ->
                        getSystemService(android.content.ClipboardManager::class.java)
                            ?.setPrimaryClip(android.content.ClipData.newPlainText("Ellune guest line", k.line))
                        elluneGuest.text = "Copied. Add it to DEVICES in Cloudflare, after a comma."
                        elluneGuest.setTextColor(dim)
                        elluneGuest.visibility = View.VISIBLE
                    }
                    .setNegativeButton("Cancel", null)
                    .show()
            }
        }, "ellune-key").start()
    }

    // everyone on Ellune's guest list; tap one added from the app to remove it (needs an Authy code)
    private fun showDevices() {
        val k = ElluneKey.peek()
        if (k == null) { problem("This phone isn't on Ellune yet: Add to guest list first."); return }
        Thread({
            val list = Ellune.devicesNow(k.priv, k.pub)
            runOnUiThread {
                if (isDestroyed) return@runOnUiThread
                if (list == null) { elluneGuest.text = "Couldn't get the list from Ellune."; elluneGuest.setTextColor(err); elluneGuest.visibility = View.VISIBLE; return@runOnUiThread }
                val mine = Ellune.guestLine(k.pub).substringAfter("=")
                val items = list.map { it.name + (if (it.key == mine) "  (this phone)" else "") + (if (it.fromApp) "" else "  \u00B7 in Cloudflare") }.toTypedArray()
                AlertDialog.Builder(this)
                    .setTitle("On Ellune's guest list")
                    .setItems(items) { _, i -> askRemove(k, list[i]) }
                    .setNegativeButton("Close", null)
                    .show()
            }
        }, "ellune-devices").start()
    }

    private fun askRemove(k: ElluneKey.Key, g: Ellune.Guest) {
        if (!g.fromApp) {
            AlertDialog.Builder(this).setTitle(g.name)
                .setMessage("This one was added in Cloudflare (DEVICES). Remove its line there.")
                .setPositiveButton("OK", null).show()
            return
        }
        val box = codeBox()
        AlertDialog.Builder(this)
            .setTitle("Remove ${g.name}?")
            .setMessage("It won't get into Ellune any more. Type the code Authy shows for Ellune.")
            .setView(LinearLayout(this).apply { setPadding(dp(20), 0, dp(20), 0); addView(box, LinearLayout.LayoutParams(-1, -2)) })
            .setPositiveButton("Remove") { _, _ ->
                val code = box.text.toString()
                Thread({
                    val why = Ellune.removeNow(k.priv, k.pub, g.key, code)
                    runOnUiThread {
                        if (isDestroyed) return@runOnUiThread
                        elluneGuest.text = why ?: "Removed ${g.name}."
                        elluneGuest.setTextColor(if (why == null) ok else err)
                        elluneGuest.visibility = View.VISIBLE
                    }
                }, "ellune-remove").start()
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    // ------------------------------------------------------------------ commands
    private fun talkPressed() {
        answer = ""
        if (Voice.state != Voice.State.OFF) {
            if (store.btMode) Pocket.stopTalking(this) else Voice.tap(this)   // Ellune mode: tap = send now, hold = mute
            return
        }
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
