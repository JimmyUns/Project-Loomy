package io.github.jimmyuns.pocketloomy

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.app.AlertDialog
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.speech.RecognitionListener
import android.speech.RecognizerIntent
import android.speech.SpeechRecognizer
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
import android.widget.TextView
import java.text.DateFormat
import java.util.Date

// One link for the whole app, so turning the phone sideways doesn't drop the connection
object Pocket { val link = Link() }

@SuppressLint("MissingPermission")   // every Bluetooth call goes through haveBluetooth() first
class MainActivity : Activity() {
    private val bg = Color.rgb(0x13, 0x09, 0x1C)
    private val inputBg = Color.rgb(0x1A, 0x0E, 0x27)
    private val textC = Color.rgb(0xD9, 0xCE, 0xE6)
    private val dim = Color.rgb(0x84, 0x73, 0x99)
    private val accent = Color.rgb(0xA8, 0x55, 0xF7)
    private val ok = Color.rgb(0x8E, 0xE3, 0xB8)
    private val err = Color.rgb(0xFF, 0x7C, 0x96)

    private lateinit var store: Store
    private lateinit var pcLine: TextView
    private lateinit var trustLine: TextView
    private lateinit var pairBtn: Button
    private lateinit var forgetBtn: Button
    private lateinit var talkBtn: Button
    private lateinit var input: EditText
    private lateinit var log: LinearLayout
    private lateinit var logScroll: ScrollView
    private var recognizer: SpeechRecognizer? = null
    private var listeningFor = 0          // 0 = idle, 1 = a command, 2 = the pairing code
    private var busy = false

    // ------------------------------------------------------------------ setup
    override fun onCreate(saved: Bundle?) {
        super.onCreate(saved)
        store = Store(this)
        setContentView(buildUi())
        askPermissions()
        refresh()
        say("Tap Talk and say a command, like \"display extend\" or \"web gmail\".", dim)
    }

    override fun onDestroy() {
        recognizer?.destroy()
        recognizer = null
        super.onDestroy()
    }

    private fun dp(v: Int) = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v.toFloat(), resources.displayMetrics).toInt()

    private fun label(text: String, color: Int, sp: Float = 15f, bold: Boolean = false) = TextView(this).apply {
        this.text = text
        setTextColor(color)
        textSize = sp
        typeface = if (bold) Typeface.create(Typeface.MONOSPACE, Typeface.BOLD) else Typeface.MONOSPACE
    }

    private fun button(text: String, filled: Boolean, onClick: () -> Unit) = Button(this).apply {
        this.text = text
        isAllCaps = false
        typeface = Typeface.MONOSPACE
        setTextColor(if (filled) Color.WHITE else accent)
        background = GradientDrawable().apply {
            cornerRadius = dp(10).toFloat()
            if (filled) setColor(accent) else { setColor(inputBg); setStroke(dp(1), accent) }
        }
        setOnClickListener { onClick() }
    }

    private fun row(vararg views: View) = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER_VERTICAL
        for (v in views) addView(v)
    }

    private fun buildUi(): View {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(bg)
            setPadding(dp(20), dp(16), dp(20), dp(16))
        }
        // Android 15 draws apps under the status and navigation bars: keep clear of them (and the keyboard)
        root.setOnApplyWindowInsetsListener { v, insets ->
            val (t, b) = if (Build.VERSION.SDK_INT >= 30) {
                val i = insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.ime())
                i.top to i.bottom
            } else {
                @Suppress("DEPRECATION")
                insets.systemWindowInsetTop to insets.systemWindowInsetBottom
            }
            v.setPadding(dp(20), t + dp(16), dp(20), b + dp(16))
            insets
        }

        root.addView(label("Pocket Loomy", accent, 26f, bold = true))
        root.addView(label("voice remote for Loomy, over Bluetooth", dim, 13f))

        pcLine = label("", textC)
        val choose = button("Choose PC", false) { choosePc() }
        root.addView(row(pcLine.apply { layoutParams = LinearLayout.LayoutParams(0, -2, 1f) }, choose).apply { setPadding(0, dp(18), 0, 0) })

        trustLine = label("", dim)
        root.addView(trustLine.apply { setPadding(0, dp(10), 0, dp(6)) })
        pairBtn = button("Pair (say the code)", false) { startPairing() }
        forgetBtn = button("Forget PC", false) { forget() }
        root.addView(row(pairBtn, View(this).apply { layoutParams = LinearLayout.LayoutParams(dp(10), 1) }, forgetBtn))

        talkBtn = button("Talk", true) { startTalking() }
        talkBtn.textSize = 22f
        root.addView(talkBtn, LinearLayout.LayoutParams(-1, dp(84)).apply { topMargin = dp(22) })

        input = EditText(this).apply {
            hint = "or type a command"
            setHintTextColor(dim)
            setTextColor(textC)
            typeface = Typeface.MONOSPACE
            isSingleLine = true
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            imeOptions = EditorInfo.IME_ACTION_SEND
            background = GradientDrawable().apply { cornerRadius = dp(10).toFloat(); setColor(inputBg) }
            setPadding(dp(12), dp(10), dp(12), dp(10))
            layoutParams = LinearLayout.LayoutParams(0, -2, 1f)
            setOnEditorActionListener { _, action, _ -> if (action == EditorInfo.IME_ACTION_SEND) { sendTyped(); true } else false }
        }
        root.addView(row(input, View(this).apply { layoutParams = LinearLayout.LayoutParams(dp(10), 1) },
                         button("Send", false) { sendTyped() }).apply { setPadding(0, dp(14), 0, 0) })

        log = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        logScroll = ScrollView(this).apply { addView(log) }
        root.addView(logScroll, LinearLayout.LayoutParams(-1, 0, 1f).apply { topMargin = dp(16) })
        return root
    }

    private fun say(text: String, color: Int) {
        if (log.childCount >= 60) log.removeViewAt(0)
        log.addView(label(text, color, 14f).apply { setPadding(0, dp(4), 0, dp(4)) })
        logScroll.post { logScroll.fullScroll(View.FOCUS_DOWN) }
    }

    private fun refresh() {
        pcLine.text = store.pcLabel?.let { "PC: $it" } ?: "No PC chosen yet"
        val t = store.trust()
        when {
            t == null -> { trustLine.text = "Not paired"; trustLine.setTextColor(dim) }
            !t.valid() -> { trustLine.text = "The pairing ran out (7 days). Pair again."; trustLine.setTextColor(err) }
            else -> {
                val until = DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT).format(Date(t.expiresAt))
                trustLine.text = "Paired with ${t.pcName}, until $until"
                trustLine.setTextColor(ok)
            }
        }
        val enabled = !busy && listeningFor == 0
        talkBtn.isEnabled = enabled
        pairBtn.isEnabled = enabled
        forgetBtn.isEnabled = !busy && t != null
        talkBtn.alpha = if (enabled) 1f else 0.5f
        talkBtn.text = when (listeningFor) { 1 -> "Listening..."; 2 -> "Say the code..."; else -> if (busy) "Talking to Loomy..." else "Talk" }
    }

    // ------------------------------------------------------------------ permissions + Bluetooth
    private fun needed(): Array<String> {
        val p = mutableListOf(Manifest.permission.RECORD_AUDIO)
        if (Build.VERSION.SDK_INT >= 31) p += Manifest.permission.BLUETOOTH_CONNECT
        return p.filter { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }.toTypedArray()
    }

    private fun askPermissions() { needed().takeIf { it.isNotEmpty() }?.let { requestPermissions(it, 1) } }

    private fun adapter(): BluetoothAdapter? = getSystemService(BluetoothManager::class.java)?.adapter

    private fun haveBluetooth(): Boolean {
        if (Build.VERSION.SDK_INT >= 31 && checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED) {
            say("Pocket Loomy needs the Nearby devices permission to reach your PC.", err)
            askPermissions()
            return false
        }
        val a = adapter()
        if (a == null) { say("This phone has no Bluetooth.", err); return false }
        if (!a.isEnabled) { say("Turn Bluetooth on first.", err); return false }
        return true
    }

    private fun pcDevice(): BluetoothDevice? {
        val addr = store.pcAddress
        if (addr == null) { say("Choose your PC first.", err); return null }
        if (!haveBluetooth()) return null
        return adapter()?.getRemoteDevice(addr)
    }

    private fun choosePc() {
        if (!haveBluetooth()) return
        val devices = adapter()?.bondedDevices?.toList()?.sortedBy { it.name ?: it.address } ?: emptyList()
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
                    store.forget()
                    Pocket.link.close()
                    say("New PC chosen, so the old pairing is gone. Pair again.", dim)
                }
                store.pcAddress = d.address
                store.pcLabel = d.name ?: d.address
                refresh()
            }
            .show()
    }

    private fun myName(): ByteArray {
        var n = Settings.Global.getString(contentResolver, "device_name") ?: Build.MODEL ?: "Android phone"
        while (n.toByteArray(Charsets.UTF_8).size > 48) n = n.dropLast(1)
        return n.toByteArray(Charsets.UTF_8)
    }

    // ------------------------------------------------------------------ speech
    private fun listen(mode: Int) {
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            say("Pocket Loomy needs the microphone permission to hear you.", err)
            askPermissions()
            return
        }
        if (!SpeechRecognizer.isRecognitionAvailable(this)) {
            say("This phone has no speech recognizer (Google app / Speech Services). You can still type.", err)
            return
        }
        val r = recognizer ?: SpeechRecognizer.createSpeechRecognizer(this).also { recognizer = it }
        r.setRecognitionListener(object : RecognitionListener {
            override fun onResults(results: Bundle?) {
                val heard = results?.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION) ?: arrayListOf()
                val was = listeningFor
                listeningFor = 0
                refresh()
                if (was == 2) gotCode(heard) else gotCommand(heard)
            }
            override fun onError(error: Int) {
                listeningFor = 0
                refresh()
                say(when (error) {
                    SpeechRecognizer.ERROR_NO_MATCH, SpeechRecognizer.ERROR_SPEECH_TIMEOUT -> "Didn't catch that."
                    SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS -> "No microphone permission."
                    SpeechRecognizer.ERROR_NETWORK, SpeechRecognizer.ERROR_NETWORK_TIMEOUT ->
                        "The speech recognizer needs the internet or an offline language pack."
                    SpeechRecognizer.ERROR_RECOGNIZER_BUSY -> "The speech recognizer is busy, try again."
                    else -> "Speech recognition failed (error $error)."
                }, err)
            }
            override fun onReadyForSpeech(params: Bundle?) {}
            override fun onBeginningOfSpeech() {}
            override fun onRmsChanged(rmsdB: Float) {}
            override fun onBufferReceived(buffer: ByteArray?) {}
            override fun onEndOfSpeech() {}
            override fun onPartialResults(partialResults: Bundle?) {}
            override fun onEvent(eventType: Int, params: Bundle?) {}
        })
        val intent = Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH).apply {
            putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM)
            putExtra(RecognizerIntent.EXTRA_PREFER_OFFLINE, true)
            putExtra(RecognizerIntent.EXTRA_MAX_RESULTS, 5)
            putExtra(RecognizerIntent.EXTRA_CALLING_PACKAGE, packageName)
        }
        listeningFor = mode
        refresh()
        r.startListening(intent)
    }

    // ------------------------------------------------------------------ pairing
    private fun startPairing() {
        if (pcDevice() == null) return
        say("On the PC: /phone shows the code. Say the 8 digits now.", dim)
        listen(2)
    }

    private fun gotCode(heard: List<String>) {
        val code = heard.firstNotNullOfOrNull { Words.code(it) }
        if (code == null) {
            say("Didn't hear 8 digits (heard: ${heard.firstOrNull() ?: "nothing"}). Tap Pair and try again.", err)
            return
        }
        val device = pcDevice() ?: return
        say("Heard ${code.substring(0, 4)} ${code.substring(4)}. Pairing...", textC)
        busy = true
        refresh()
        Pocket.link.pair(device, code, myName()) { result ->
            runOnUiThread {
                busy = false
                result.onSuccess { t ->
                    store.save(t)
                    say("Paired with ${t.pcName}. Trusted for 7 days.", ok)
                }.onFailure { say(it.message ?: "Pairing failed.", err) }
                refresh()
            }
        }
    }

    private fun forget() {
        store.forget()
        Pocket.link.close()
        say("Forgot the pairing. On the PC, /phone forget drops this phone too.", dim)
        refresh()
    }

    // ------------------------------------------------------------------ commands
    private fun startTalking() {
        val t = store.trust()
        if (t == null || !t.valid()) { say("Pair with your PC first.", err); return }
        listen(1)
    }

    private fun gotCommand(heard: List<String>) {
        val cmd = heard.firstOrNull()?.let { Words.command(it) } ?: ""
        if (cmd.isEmpty()) { say("Didn't catch a command.", err); return }
        send(cmd)
    }

    private fun sendTyped() {
        val cmd = Words.command(input.text.toString())
        if (cmd.isEmpty()) return
        input.setText("")
        send(cmd)
    }

    private fun send(cmd: String) {
        val t = store.trust()
        if (t == null || !t.valid()) { say("Pair with your PC first.", err); return }
        val device = pcDevice() ?: return
        say("\u203A $cmd", accent)
        busy = true
        refresh()
        Pocket.link.send(device, t, myName(), cmd) { result ->
            runOnUiThread {
                busy = false
                result.onSuccess { say(it.ifEmpty { "Done." }, textC) }.onFailure { say(it.message ?: "Failed.", err) }
                refresh()
            }
        }
    }
}
