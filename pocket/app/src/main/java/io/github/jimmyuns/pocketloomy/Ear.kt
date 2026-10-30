package io.github.jimmyuns.pocketloomy

import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.speech.RecognitionListener
import android.speech.RecognizerIntent
import android.speech.SpeechRecognizer

// Android's speech recognizer, wrapped: live partial text and mic level for the screen, then the result.
class Ear(
    private val ctx: Context,
    private val partial: (String) -> Unit,
    private val level: (Float) -> Unit,
    private val heard: (List<String>) -> Unit,
    private val missed: (Int) -> Unit,
) {
    private var r: SpeechRecognizer? = null

    fun available() = SpeechRecognizer.isRecognitionAvailable(ctx)

    fun start() {
        val rec = r ?: SpeechRecognizer.createSpeechRecognizer(ctx).also { r = it }
        rec.setRecognitionListener(object : RecognitionListener {
            override fun onResults(results: Bundle?) {
                heard(results?.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION) ?: arrayListOf())
            }
            override fun onPartialResults(results: Bundle?) {
                results?.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION)?.firstOrNull()?.let { partial(it) }
            }
            override fun onError(error: Int) { missed(error) }
            override fun onRmsChanged(rmsdB: Float) { level(rmsdB) }
            override fun onReadyForSpeech(params: Bundle?) {}
            override fun onBeginningOfSpeech() {}
            override fun onBufferReceived(buffer: ByteArray?) {}
            override fun onEndOfSpeech() {}
            override fun onEvent(eventType: Int, params: Bundle?) {}
        })
        rec.startListening(Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH).apply {
            putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM)
            putExtra(RecognizerIntent.EXTRA_PREFER_OFFLINE, true)
            putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, true)
            putExtra(RecognizerIntent.EXTRA_MAX_RESULTS, 5)
            putExtra(RecognizerIntent.EXTRA_CALLING_PACKAGE, ctx.packageName)
        })
    }

    fun cancel() { r?.cancel() }
    fun destroy() { r?.destroy(); r = null }

    companion object {
        /** true when nothing usable was heard (worth telling the PC about) */
        fun isMiss(error: Int) = error == SpeechRecognizer.ERROR_NO_MATCH || error == SpeechRecognizer.ERROR_SPEECH_TIMEOUT

        fun message(error: Int) = when (error) {
            SpeechRecognizer.ERROR_NO_MATCH, SpeechRecognizer.ERROR_SPEECH_TIMEOUT -> "Didn't catch that."
            SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS -> "No microphone permission."
            SpeechRecognizer.ERROR_NETWORK, SpeechRecognizer.ERROR_NETWORK_TIMEOUT ->
                "The speech recognizer needs the internet or an offline language pack."
            SpeechRecognizer.ERROR_RECOGNIZER_BUSY -> "The speech recognizer is busy, try again."
            SpeechRecognizer.ERROR_CLIENT -> "Stopped."
            else -> "Speech recognition failed (error $error)."
        }
    }
}
