package io.github.jimmyuns.pocketloomy

import android.annotation.SuppressLint
import android.content.Context
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.os.Handler
import android.os.Looper
import android.speech.SpeechRecognizer

// The other ear (2.21): records what you say itself and asks Ellune (Whisper) what it was. Same callbacks
// as Ear, so Voice treats both the same. Used only when Voice says so: a Talk tap, "Loomy" + pause, or solo
// mode while awake, with internet and this phone on her guest list. Everything else (waiting for "Loomy" in
// keep-listening) stays on Android's own recognizer, so what isn't said to Loomy never leaves the phone.
// The clip goes to Ellune once and isn't kept anywhere (not on the phone, not on Ellune).
class ElluneEar(
    private val ctx: Context,
    private val partial: (String) -> Unit,
    private val level: (Float) -> Unit,
    private val heard: (List<String>) -> Unit,
    private val missed: (Int) -> Unit,
    private val failed: (Ellune.Answer.Down) -> Unit,      // Ellune didn't answer: Voice says so
    private val busy: (Boolean) -> Unit = {},               // the mic is off and Ellune is working it out
) {
    private val main = Handler(Looper.getMainLooper())
    @Volatile private var run = 0                          // bumped by cancel(): an old thread's results are dropped

    @SuppressLint("MissingPermission")                     // RECORD_AUDIO is asked for before Talk works at all
    fun start() {
        val me = ++run
        Thread({
            val key = ElluneKey.peek()
            if (key == null) { post(me) { failed(Ellune.Answer.Down("no key", notGuest = true)) }; return@Thread }
            // the phone's recognizer may still be letting go of the mic: a few tries
            val min = AudioRecord.getMinBufferSize(Sound.RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT)
            var rec: AudioRecord? = null
            for (attempt in 0 until 4) {
                if (run != me) return@Thread
                rec = try {
                    AudioRecord(MediaRecorder.AudioSource.VOICE_RECOGNITION, Sound.RATE, AudioFormat.CHANNEL_IN_MONO,
                                AudioFormat.ENCODING_PCM_16BIT, maxOf(min, Sound.FRAME * 2 * 8))
                } catch (_: Exception) { null }
                if (rec != null && rec.state == AudioRecord.STATE_INITIALIZED) break
                rec?.release(); rec = null
                Thread.sleep(200)
            }
            if (rec == null) { post(me) { failed(Ellune.Answer.Down("the mic is busy")) }; return@Thread }
            val vad = Vad()
            val pcm = ShortArray(Sound.RATE * 17)          // 6 s waiting + 10 s talking + slack
            var n = 0
            var ended = false
            try {
                rec.startRecording()
                if (rec.recordingState != AudioRecord.RECORDSTATE_RECORDING) throw IllegalStateException("not recording")
                val frame = ShortArray(Sound.FRAME)
                while (run == me && n + Sound.FRAME <= pcm.size) {
                    var got = 0
                    while (got < Sound.FRAME && run == me) {
                        val r = rec.read(frame, got, Sound.FRAME - got)
                        if (r <= 0) break
                        got += r
                    }
                    if (got < Sound.FRAME) break
                    System.arraycopy(frame, 0, pcm, n, Sound.FRAME)
                    n += Sound.FRAME
                    val rms = Sound.rms(frame)
                    val lv = Sound.level(rms)
                    post(me) { level(lv) }
                    when (vad.push(rms, Sound.zcr(frame))) {
                        Vad.Event.STARTED -> post(me) { partial("Loomy is listening...") }
                        Vad.Event.TIMEOUT -> { post(me) { missed(SpeechRecognizer.ERROR_SPEECH_TIMEOUT) }; return@Thread }
                        Vad.Event.ENDED -> { ended = true; break }
                        Vad.Event.NOISE -> { post(me) { missed(SpeechRecognizer.ERROR_NO_MATCH) }; return@Thread }   // typing, a knock: never sent
                        Vad.Event.NONE -> {}
                    }
                }
            } catch (_: Exception) {
                post(me) { failed(Ellune.Answer.Down("the mic is busy")) }
                return@Thread
            } finally {
                try { rec.stop() } catch (_: Exception) {}
                rec.release()
            }
            if (run != me) return@Thread
            if (!ended) { post(me) { missed(SpeechRecognizer.ERROR_SPEECH_TIMEOUT) }; return@Thread }
            post(me) { partial("Ellune is understanding..."); level(-2f); busy(true) }
            val clip = Sound.wav(pcm, vad.start * Sound.FRAME, minOf(vad.end * Sound.FRAME, n))
            when (val a = Ellune.hearNow(key.priv, key.pub, clip)) {
                is Ellune.Answer.Ok ->
                    post(me) { busy(false); if (a.text.isBlank()) missed(SpeechRecognizer.ERROR_NO_MATCH) else heard(listOf(a.text)) }
                is Ellune.Answer.Down -> post(me) { busy(false); failed(a) }
            }
        }, "ellune-ear").start()
    }

    private fun post(me: Int, f: () -> Unit) { main.post { if (run == me) f() } }

    /** main thread. An upload still running is dropped, and the "working on it" state with it. */
    fun cancel() { run++; busy(false) }
    fun destroy() = cancel()

    companion object {
        /** the phone has a working internet connection right now (not just Wi-Fi without internet) */
        fun online(ctx: Context): Boolean = try {
            val cm = ctx.getSystemService(ConnectivityManager::class.java)
            val caps = cm?.getNetworkCapabilities(cm.activeNetwork)
            caps != null && caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) &&
                caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED)
        } catch (_: Exception) { false }
    }
}
