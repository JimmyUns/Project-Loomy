package io.github.jimmyuns.pocketloomy

import android.content.Context
import android.media.AudioManager
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.speech.tts.TextToSpeech
import android.speech.tts.UtteranceProgressListener
import java.util.Locale

// She talks back (2.24d, step 1 of the helper plan): the phone's own voice (Samsung's, his pick), through Android's
// text-to-speech. Only her own lines: questions, short answers, game answers. Quiet when the phone is on silent or
// vibrate, or when "Talk back" is off. Voice keeps the mic paused while she speaks, so she doesn't hear herself.
object Speaker {
    private var tts: TextToSpeech? = null
    @Volatile private var ready = false
    private val main = Handler(Looper.getMainLooper())
    private var done: (() -> Unit)? = null

    fun warm(ctx: Context) {
        if (tts != null) return
        tts = TextToSpeech(ctx.applicationContext) { st ->
            ready = st == TextToSpeech.SUCCESS
            if (ready) {
                tts?.language = Locale.US
                tts?.setOnUtteranceProgressListener(object : UtteranceProgressListener() {
                    override fun onStart(id: String?) {}
                    override fun onDone(id: String?) { finish() }
                    @Deprecated("old API") override fun onError(id: String?) { finish() }
                    override fun onError(id: String?, code: Int) { finish() }
                })
            }
        }
    }

    /** would she speak right now (setting on, engine ready, phone not on silent/vibrate) */
    fun canSpeak(ctx: Context): Boolean {
        if (!Store(ctx).talkBack || !ready) return false
        val am = ctx.getSystemService(Context.AUDIO_SERVICE) as? AudioManager ?: return false
        return am.ringerMode == AudioManager.RINGER_MODE_NORMAL
    }

    /** says it; then() runs on the main thread when she's done (or right away if she can't speak) */
    fun say(ctx: Context, text: String, then: () -> Unit) {
        warm(ctx)
        if (!canSpeak(ctx) || text.isBlank()) { then(); return }
        finish()                                          // anything still pending finishes first
        done = then
        val p = Bundle().apply { putInt(TextToSpeech.Engine.KEY_PARAM_STREAM, AudioManager.STREAM_MUSIC) }
        if (tts?.speak(text.take(300), TextToSpeech.QUEUE_FLUSH, p, "loomy") != TextToSpeech.SUCCESS) { finish(); return }
        main.postDelayed({ finish() }, 15_000)            // never keep the mic paused forever
    }

    fun stop() { tts?.stop(); finish() }

    private fun finish() {
        main.post {
            val d = done ?: return@post
            done = null
            d()
        }
    }
}
