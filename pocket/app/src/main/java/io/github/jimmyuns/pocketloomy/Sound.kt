package io.github.jimmyuns.pocketloomy

import kotlin.math.sqrt

// The plain parts of recording for Ellune (2.21), no Android classes so tests/jni runs them:
// a WAV writer, and Vad, which decides when you started and stopped talking.
// ElluneEar.kt feeds it 20 ms frames from the mic.
object Sound {
    const val RATE = 16000
    const val FRAME = RATE / 50                 // 20 ms
    const val MAX_SAMPLES = RATE * 10           // Ellune takes at most 10 s

    /** 16 kHz mono 16-bit PCM -> a WAV file (what Ellune's /hear expects) */
    fun wav(pcm: ShortArray, from: Int = 0, to: Int = pcm.size, rate: Int = RATE): ByteArray {
        val n = (to - from) * 2
        val b = ByteArray(44 + n)
        fun s4(i: Int, t: String) { for (k in 0 until 4) b[i + k] = t[k].code.toByte() }
        fun u32(i: Int, v: Int) { for (k in 0 until 4) b[i + k] = (v ushr (8 * k)).toByte() }
        fun u16(i: Int, v: Int) { b[i] = v.toByte(); b[i + 1] = (v ushr 8).toByte() }
        s4(0, "RIFF"); u32(4, 36 + n); s4(8, "WAVE"); s4(12, "fmt "); u32(16, 16)
        u16(20, 1); u16(22, 1); u32(24, rate); u32(28, rate * 2); u16(32, 2); u16(34, 16)
        s4(36, "data"); u32(40, n)
        var o = 44
        for (i in from until to) { val v = pcm[i].toInt(); b[o++] = v.toByte(); b[o++] = (v shr 8).toByte() }
        return b
    }

    fun rms(f: ShortArray, n: Int = f.size): Double {
        if (n <= 0) return 0.0
        var s = 0.0
        for (i in 0 until n) { val v = f[i].toDouble(); s += v * v }
        return sqrt(s / n)
    }

    /** how often the wave crosses zero, 0..1. Voices (vowels) are low, around 0.02-0.15 at 16 kHz; clicks, keyboard
     *  taps and hiss are high (0.3+). Vad uses it to tell talking from typing. */
    fun zcr(f: ShortArray, n: Int = f.size): Double {
        if (n < 2) return 0.0
        var c = 0
        for (i in 1 until n) if ((f[i] >= 0) != (f[i - 1] >= 0)) c++
        return c.toDouble() / (n - 1)
    }

    /** rms -> roughly the -2..10 range Android's recognizer reports, so the mic bars look the same */
    fun level(rms: Double) = ((20 * kotlin.math.log10(maxOf(rms, 1.0)) - 40) / 2.5).coerceIn(-2.0, 10.0).toFloat()
}

/**
 * Talking or not, frame by frame (20 ms each). Learns the room's noise while waiting, starts when the level
 * stays well above it for 80 ms, ends after a second of quiet, or at 10 s. Indices are frame numbers.
 * 2.22: a clip must also hold real voice (at least 160 ms of loud, low-zero-crossing frames, i.e. vowels):
 * typing and knocks are loud but crackly, so they end as NOISE and are never sent anywhere.
 */
class Vad(private val waitFrames: Int = 300,       // 6 s to start talking
          private val endFrames: Int = 50,         // 1 s of quiet ends it
          private val maxFrames: Int = 500,        // 10 s of talking at most
          private val preFrames: Int = 15) {       // keep 300 ms before the start (the first sound)
    enum class Event { NONE, STARTED, ENDED, TIMEOUT, NOISE }

    var frame = 0; private set
    var start = -1; private set                    // first frame kept (pre-roll included)
    var end = -1; private set                      // one past the last frame kept
    private var noise = -1.0
    private var loud = 0
    private var quiet = 0
    private var talking = false
    private var done = false
    var voiced = 0; private set                    // loud frames that sound like a voice

    fun push(rms: Double, zcr: Double = 0.1): Event {
        if (done) return Event.NONE
        val i = frame++
        if (!talking) {
            noise = if (noise < 0) rms else if (rms < noise * 4) noise * 0.95 + rms * 0.05 else noise
            val voice = rms > maxOf(noise * 2.5, 300.0) && i >= 3
            if (voice) { loud++; if (zcr < 0.25) voiced++ } else { loud = 0; voiced = 0 }
            if (loud >= 4) {
                talking = true
                start = maxOf(0, i - 3 - preFrames)
                return Event.STARTED
            }
            if (i + 1 >= waitFrames) { done = true; return Event.TIMEOUT }
            return Event.NONE
        }
        quiet = if (rms < maxOf(noise * 1.8, 200.0)) quiet + 1 else 0
        if (quiet == 0 && rms > maxOf(noise * 2.5, 300.0) && zcr < 0.25) voiced++
        if (quiet >= endFrames || i + 1 - start >= maxFrames) {
            done = true
            end = if (quiet >= endFrames) i + 1 - quiet + 15 else i + 1     // keep 300 ms of the quiet
            end = minOf(end, start + maxFrames)
            return if (voiced >= 8) Event.ENDED else Event.NOISE
        }
        return Event.NONE
    }
}
