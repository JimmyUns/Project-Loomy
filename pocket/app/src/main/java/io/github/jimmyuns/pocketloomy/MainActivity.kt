package io.github.jimmyuns.pocketloomy

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.PowerManager
import android.provider.Settings
import android.text.InputType
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.WindowInsets
import android.view.inputmethod.EditorInfo
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.Switch
import android.widget.TextView
import java.text.DateFormat
import java.util.Date

// The app screen. It's drawn from state every time something changes (render()): setup steps until
// the phone is paired, then the PC, the On switch, a big Talk button that shows what it's doing
// (listening with the live transcript and mic level, talking to Loomy, the answer), typing, history.
@SuppressLint("MissingPermission", "UseSwitchCompatOrMaterialCode")
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

    private enum class Phase { IDLE, LISTEN_CODE, LISTEN_CMD, PAIRING, SENDING }

    private lateinit var store: Store
    private lateinit var ear: Ear
    private var phase = Phase.IDLE
    private var transcript = ""          // live while listening, then what was sent
    private var answer = ""              // Loomy's last answer (or the last problem)
    private var answerBad = false
    private var level = 0f

    // views
    private lateinit var chip: TextView
    private lateinit var setup: LinearLayout
    private lateinit var step1: TextView
    private lateinit var step2: TextView
    private lateinit var codeBtn: Button
    private lateinit var paired: LinearLayout
    private lateinit var pcName: TextView
    private lateinit var pcSub: TextView
    private lateinit var onSwitch: Switch
    private lateinit var onLine: TextView
    private lateinit var talk: Button
    private lateinit var talkBg: GradientDrawable
    private lateinit var live: TextView
    private lateinit var answerBox: TextView
    private lateinit var input: EditText
    private lateinit var log: LinearLayout
    private lateinit var logScroll: ScrollView
    private var settingSwitch = false

    private val onChange: () -> Unit = { render() }

    // ------------------------------------------------------------------ lifecycle
    override fun onCreate(saved: Bundle?) {
        super.onCreate(saved)
        store = Store(this)
        Pocket.fixStaleOn(this)
        ear = Ear(this,
            partial = { transcript = it; render() },
            level = { level = it; renderTalk() },
            heard = { gotSpeech(it) },
            missed = { missedSpeech(it) })
        setContentView(buildUi())
        askPermissions()
        if (!store.on) Notes.off(this)
        render()
    }

    override fun onResume() { super.onResume(); Pocket.listen(onChange); Pocket.fixStaleOn(this); render() }
    override fun onPause() { Pocket.unlisten(onChange); super.onPause() }
    override fun onDestroy() { ear.destroy(); super.onDestroy() }

    override fun onRequestPermissionsResult(code: Int, perms: Array<out String>, results: IntArray) {
        if (!store.on && Notes.allowed(this)) Notes.off(this)
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

    private fun buildUi(): View {
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setBackgroundColor(bg) }
        root.setOnApplyWindowInsetsListener { v, insets ->   // Android 15 draws under the bars: keep clear (and of the keyboard)
            val (t, b) = if (Build.VERSION.SDK_INT >= 30) {
                val i = insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.ime()); i.top to i.bottom
            } else @Suppress("DEPRECATION") (insets.systemWindowInsetTop to insets.systemWindowInsetBottom)
            v.setPadding(dp(18), t + dp(14), dp(18), b + dp(12))
            insets
        }

        // header: name + a chip saying what's going on
        chip = text("", dim, 12f).apply { setPadding(dp(10), dp(4), dp(10), dp(4)) }
        root.addView(LinearLayout(this).apply {
            gravity = Gravity.CENTER_VERTICAL
            addView(text("Pocket Loomy", accent, 24f, true), LinearLayout.LayoutParams(0, -2, 1f))
            addView(chip)
        })
        root.addView(gap(14))

        // setup: two steps, shown until the phone is paired
        step1 = text("", textC)
        step2 = text("", textC)
        codeBtn = button("Say the code", true) { startPairing() }
        setup = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(box(text("1  Your PC", accent, 13f, true), gap(6), step1, gap(10), button("Choose PC", false) { choosePc() }))
            addView(gap(12))
            addView(box(text("2  Pair", accent, 13f, true), gap(6), step2, gap(10), codeBtn))
        }
        root.addView(setup)

        // paired: PC + On switch, Talk, live text, answer, typing
        pcName = text("", bright, 17f, true)
        pcSub = text("", dim, 12f)
        onSwitch = Switch(this).apply {
            text = "On  "
            setTextColor(textC)
            typeface = Typeface.MONOSPACE
            setOnCheckedChangeListener { _, checked -> if (!settingSwitch) toggleOn(checked) }
        }
        onLine = text("", dim, 12f)
        talkBg = rounded(accent, radius = 20)
        talk = Button(this).apply {
            isAllCaps = false
            textSize = 22f
            typeface = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD)
            setTextColor(Color.WHITE)
            background = talkBg
            setOnClickListener { talkPressed() }
        }
        live = text("", accent, 16f).apply { gravity = Gravity.CENTER_HORIZONTAL; setPadding(0, dp(10), 0, 0) }
        answerBox = text("", textC, 17f).apply { background = rounded(card); setPadding(dp(14), dp(12), dp(14), dp(12)) }
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
        paired = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(box(
                LinearLayout(this@MainActivity).apply {
                    gravity = Gravity.CENTER_VERTICAL
                    addView(LinearLayout(this@MainActivity).apply {
                        orientation = LinearLayout.VERTICAL
                        addView(pcName); addView(pcSub)
                    }, LinearLayout.LayoutParams(0, -2, 1f))
                    addView(onSwitch)
                },
                gap(6), onLine))
            addView(talk, LinearLayout.LayoutParams(-1, dp(110)).apply { topMargin = dp(16) })
            addView(live)
            addView(answerBox, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(10) })
            addView(LinearLayout(this@MainActivity).apply {
                gravity = Gravity.CENTER_VERTICAL
                addView(input, LinearLayout.LayoutParams(0, -2, 1f))
                addView(View(this@MainActivity), LinearLayout.LayoutParams(dp(8), 1))
                addView(button("Send", false) { sendTyped() })
            }, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(12) })
        }
        root.addView(paired)

        // history + the rarely used buttons
        log = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        logScroll = ScrollView(this).apply { addView(log) }
        root.addView(text("History", dim, 12f).apply { setPadding(0, dp(14), 0, dp(2)) })
        root.addView(logScroll, LinearLayout.LayoutParams(-1, 0, 1f))
        root.addView(LinearLayout(this).apply {
            gravity = Gravity.END
            addView(button("Change PC", false) { choosePc() })
            addView(View(this@MainActivity), LinearLayout.LayoutParams(dp(8), 1))
            addView(button("Forget", false) { confirmForget() })
            tag = "footer"
        })
        return root
    }

    // ------------------------------------------------------------------ drawing from state
    private fun render() {
        val t = store.trust()
        val isPaired = t != null && t.valid()
        val busy = phase != Phase.IDLE

        setup.visibility = if (isPaired) View.GONE else View.VISIBLE
        paired.visibility = if (isPaired) View.VISIBLE else View.GONE
        (logScroll.parent as LinearLayout).findViewWithTag<View>("footer").visibility = if (t != null) View.VISIBLE else View.GONE

        // chip
        val (chipText, chipColor) = when {
            phase == Phase.LISTEN_CMD || phase == Phase.LISTEN_CODE -> "listening" to accent
            phase == Phase.PAIRING -> "pairing" to accent
            phase == Phase.SENDING -> "talking to Loomy" to accent
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

        if (!isPaired) {
            step1.text = store.pcLabel?.let { "\u2713 $it" } ?: "Pick your PC from the phone's Bluetooth devices. (Pair it in Android's Bluetooth settings first.)"
            step1.setTextColor(if (store.pcLabel != null) ok else textC)
            step2.text = when {
                t != null -> "The pairing ran out after 7 days. On the PC type /phone, then tap below and say the 8 digits."
                phase == Phase.LISTEN_CODE -> "Listening... say the 8 digits.  ${transcript}"
                phase == Phase.PAIRING -> "Pairing with ${store.pcLabel}..."
                else -> "On the PC type /phone. Then tap below and say the 8 digits it shows."
            }
            codeBtn.isEnabled = store.pcLabel != null && !busy
            codeBtn.alpha = if (codeBtn.isEnabled) 1f else 0.4f
            codeBtn.text = if (phase == Phase.LISTEN_CODE) "Listening..." else if (phase == Phase.PAIRING) "Pairing..." else "Say the code"
            return
        }

        pcName.text = t!!.pcName
        pcSub.text = "paired until " + DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT).format(Date(t.expiresAt))
        settingSwitch = true
        onSwitch.isChecked = store.on
        settingSwitch = false
        val seen = if (Pocket.lastSeen > 0) " Last answer from the PC at " + DateFormat.getTimeInstance(DateFormat.SHORT).format(Date(Pocket.lastSeen)) + "." else ""
        onLine.text = if (store.on) "On: in your notifications and the Loomy tile, checks in every 5 minutes.$seen"
                      else "Off: turn on to Talk from the notification or the Loomy quick-settings tile.$seen"
        live.text = when (phase) {
            Phase.LISTEN_CMD -> transcript.ifEmpty { "say a command" }
            Phase.SENDING -> "\u203A $transcript"
            else -> if (transcript.isNotEmpty()) "\u203A $transcript" else "try \"display extend\" or \"web gmail\""
        }
        live.setTextColor(if (phase == Phase.IDLE && transcript.isEmpty()) dim else accent)
        answerBox.visibility = if (answer.isEmpty()) View.GONE else View.VISIBLE
        answerBox.text = answer
        answerBox.setTextColor(if (answerBad) err else bright)
        renderTalk()
    }

    private fun renderTalk() {
        if (!::talk.isInitialized) return
        val listening = phase == Phase.LISTEN_CMD
        talk.text = when (phase) {
            Phase.LISTEN_CMD -> "Listening..."
            Phase.SENDING -> "Talking to Loomy..."
            else -> "Talk"
        }
        talk.isEnabled = phase == Phase.IDLE || listening      // tap while listening = stop
        // while listening the button glows with your voice
        val k = if (listening) (level.coerceIn(0f, 10f) / 10f) else 0f
        talkBg.setColor(if (phase == Phase.SENDING) accentDim else blend(accent, Color.WHITE, k * 0.35f))
        talkBg.setStroke(dp(if (listening) 3 else 0), bright)
    }

    private fun blend(a: Int, b: Int, k: Float) = Color.rgb(
        (Color.red(a) + (Color.red(b) - Color.red(a)) * k).toInt(),
        (Color.green(a) + (Color.green(b) - Color.green(a)) * k).toInt(),
        (Color.blue(a) + (Color.blue(b) - Color.blue(a)) * k).toInt())

    private fun history(s: String, color: Int) {
        if (log.childCount >= 60) log.removeViewAt(0)
        log.addView(text(s, color, 13f).apply { setPadding(0, dp(3), 0, dp(3)) })
        logScroll.post { logScroll.fullScroll(View.FOCUS_DOWN) }
    }

    private fun problem(s: String) { answer = s; answerBad = true; history(s, err); render() }

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
                    history("New PC chosen, so the old pairing is gone. Pair again.", dim)
                }
                store.pcAddress = d.address
                store.pcLabel = d.name ?: d.address
                render()
            }
            .show()
    }

    // ------------------------------------------------------------------ listening
    private fun listen(p: Phase) {
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            problem("Pocket Loomy needs the microphone to hear you."); askPermissions(); return
        }
        if (!ear.available()) { problem("This phone has no speech recognizer (Google app / Speech Services). You can still type."); return }
        phase = p
        transcript = ""
        level = 0f
        render()
        ear.start()
    }

    private fun gotSpeech(heard: List<String>) {
        val was = phase
        phase = Phase.IDLE
        if (was == Phase.LISTEN_CODE) gotCode(heard) else gotCommand(heard)
    }

    private fun missedSpeech(error: Int) {
        val was = phase
        phase = Phase.IDLE
        transcript = ""
        problem(Ear.message(error))
        if (was == Phase.LISTEN_CMD && Ear.isMiss(error)) Pocket.reportMiss(this, "")   // the PC shows misses too
    }

    // ------------------------------------------------------------------ pairing
    private fun startPairing() {
        if (!bluetoothOk() || Pocket.device(this) == null) return
        listen(Phase.LISTEN_CODE)
    }

    private fun gotCode(heard: List<String>) {
        val code = heard.firstNotNullOfOrNull { Words.code(it) }
        if (code == null) { problem("Didn't hear 8 digits (heard: ${heard.firstOrNull() ?: "nothing"}). Try again."); return }
        val device = Pocket.device(this) ?: return
        phase = Phase.PAIRING
        transcript = "${code.substring(0, 4)} ${code.substring(4)}"
        history("Heard $transcript. Pairing...", textC)
        render()
        Pocket.link.pair(device, code, Pocket.myName(this)) { result ->
            runOnUiThread {
                phase = Phase.IDLE
                transcript = ""
                result.onSuccess { t ->
                    store.save(t)
                    Pocket.mark(true)
                    answer = "Paired with ${t.pcName}. Trusted for 7 days."
                    answerBad = false
                    history(answer, ok)
                    render()
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
        if (why != null) { problem(why); render(); return }
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
            Pocket.turnOff(this)
            store.forget()
            Pocket.lastSeen = 0
            answer = if (told) "Forgot ${t?.pcName ?: "the PC"}, and the PC forgot this phone too."
                     else "Forgot the pairing here. The PC wasn't reachable, so type /phone forget on it too."
            answerBad = !told
            history(answer, if (told) ok else err)
            render()
        }
        if (device == null || t == null || !t.valid() || !Pocket.canConnect(this)) { done(false); return }
        phase = Phase.SENDING
        transcript = "forget"
        render()
        Pocket.link.forget(device, t, Pocket.myName(this)) { r -> runOnUiThread { phase = Phase.IDLE; transcript = ""; done(r.isSuccess) } }
    }

    // ------------------------------------------------------------------ commands
    private fun talkPressed() {
        if (phase == Phase.LISTEN_CMD) { ear.cancel(); phase = Phase.IDLE; transcript = ""; render(); return }
        Pocket.ready(this)?.let { problem(it); return }
        listen(Phase.LISTEN_CMD)
    }

    private fun gotCommand(heard: List<String>) {
        val cmd = heard.firstOrNull()?.let { Words.command(it) } ?: ""
        if (cmd.isEmpty()) {
            transcript = ""
            problem("Didn't catch a command.")
            Pocket.reportMiss(this, heard.firstOrNull() ?: "")
            return
        }
        send(cmd)
    }

    private fun sendTyped() {
        val cmd = Words.command(input.text.toString())
        if (cmd.isEmpty()) return
        input.setText("")
        send(cmd)
    }

    private fun send(cmd: String) {
        Pocket.ready(this)?.let { problem(it); return }
        val device = Pocket.device(this) ?: return
        val t = store.trust() ?: return
        phase = Phase.SENDING
        transcript = cmd
        history("\u203A $cmd", accent)
        render()
        Pocket.link.send(device, t, Pocket.myName(this), cmd) { result ->
            Pocket.mark(result.isSuccess)
            runOnUiThread {
                phase = Phase.IDLE
                result.onSuccess {
                    answer = it.ifEmpty { "Done." }
                    answerBad = false
                    history(answer, textC)
                    render()
                }.onFailure { problem(it.message ?: "Failed.") }
            }
        }
    }
}
