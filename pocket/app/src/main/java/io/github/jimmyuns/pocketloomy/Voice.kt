package io.github.jimmyuns.pocketloomy

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.os.SystemClock

// Listening, in one place, run by PocketService (so it keeps going while you use the phone, with the
// microphone shown as in use). The app screen, the Talk card and the notification only show its state.
//   once:  listen for one command, send it, done. "Loomy"/"ayo Loomy" in front is optional.
//   keep:  listen again after every result until stopped, and only act on "yo/ayo/hey Loomy ...".
//          Everything else it hears is dropped on the spot: never shown, never sent.
//          "ayo Loomy" alone = she waits 8 s for the command. "ayo Loomy stop listening" ends it.
object Voice {
    enum class State { OFF, LISTENING }

    class Entry(val cmd: String, var answer: String, var bad: Boolean)

    @Volatile var state = State.OFF
    @Volatile var keep = false
    @Volatile var sending = false
    @Volatile var partial = ""
    @Volatile var level = 0f
    @Volatile var armed = false            // heard just "ayo Loomy": the next thing said is the command
    @Volatile var lastAt = 0L              // when the last answer came (the notification shows it for a while)
    val history = mutableListOf<Entry>()  // touched on the main thread only

    /** PocketService hears about start/stop here, to add or drop the microphone */
    var onChange: () -> Unit = {}

    private val main = Handler(Looper.getMainLooper())
    private var ear: Ear? = null
    private lateinit var ctx: Context
    private var armedUntil = 0L
    private var errors = 0

    fun last(): Entry? = history.lastOrNull()

    /** main thread */
    fun start(c: Context, keepListening: Boolean) {
        ctx = c
        keep = keepListening
        errors = 0
        if (state == State.LISTENING) { changed(); return }
        listenNow()
    }

    /** main thread */
    fun stop() {
        keep = false
        armed = false
        main.removeCallbacksAndMessages(null)
        ear?.cancel()
        ear?.destroy()
        ear = null
        if (state != State.OFF) { state = State.OFF; partial = ""; level = 0f; changed() }
    }

    private fun changed() { Pocket.changed(); onChange() }

    private fun listenNow() {
        val e = ear ?: Ear(ctx,
            partial = { partial = it; Pocket.changed() },
            level = { level = it; Pocket.changed() },
            heard = { heard(it) },
            missed = { missed(it) }).also { ear = it }
        state = State.LISTENING
        partial = ""
        changed()
        e.start()
    }

    // after a result: keep mode listens again, once mode is done
    private fun next(delayMs: Long) {
        if (!keep) { state = State.OFF; partial = ""; level = 0f; ear?.destroy(); ear = null; changed(); return }
        partial = ""
        main.postDelayed({ if (keep && state == State.LISTENING) ear?.start() }, delayMs)
    }

    private fun heard(all: List<String>) {
        errors = 0
        var cmdText: String? = null
        if (keep) {
            val now = SystemClock.elapsedRealtime()
            var said = all.firstNotNullOfOrNull { Words.addressed(it) }
            if (said == null && armed && now < armedUntil) said = all.firstOrNull() ?: ""
            if (said == null) { next(100); return }                  // not for Loomy: dropped, never sent
            if (said.isBlank()) {                                    // just "ayo Loomy": wait for the command
                armed = true
                armedUntil = now + 8000
                partial = "Yes?"
                changed()
                next(100)
                return
            }
            armed = false
            cmdText = said
        } else cmdText = all.firstOrNull() ?: ""

        val cmd = Words.command(cmdText ?: "")
        if (keep && Words.isStop(cmd)) { add("stop listening", "Stopped listening.", false); stop(); return }
        if (cmd.isEmpty()) {
            add((cmdText ?: "").ifEmpty { "(nothing)" }, "Didn't catch a command.", true)
            Pocket.reportMiss(ctx, cmdText ?: "")                         // the PC shows misses too
            next(150)
            return
        }
        send(cmd)
        next(150)
    }

    private fun missed(error: Int) {
        if (Ear.isMiss(error)) {
            if (!keep) { add("(nothing)", Ear.message(error), true); Pocket.reportMiss(ctx, "") }
            else if (armed && SystemClock.elapsedRealtime() > armedUntil) { armed = false; partial = "" }
            next(100)
            return
        }
        if (keep && errors++ < 5 && (error == 8 /*BUSY*/ || error == 5 /*CLIENT*/ || error == 11 /*SERVER_DISCONNECTED*/)) {
            next(800)
            return
        }
        add("(listening)", Ear.message(error), true)
        stop()
    }

    private fun add(cmd: String, answer: String, bad: Boolean) {
        history += Entry(cmd, answer, bad)
        if (history.size > 60) history.removeAt(0)
        lastAt = SystemClock.elapsedRealtime()
        changed()
    }

    /** send one command (also used for typing in the app) */
    fun send(c: Context, cmd: String) { ctx = c; send(cmd) }

    private fun send(cmd: String) {
        val why = Pocket.ready(ctx)
        if (why != null) { add(cmd, why, true); return }
        val device = Pocket.device(ctx) ?: return
        val trust = Store(ctx).trust() ?: return
        val e = Entry(cmd, "...", false)
        history += e
        if (history.size > 60) history.removeAt(0)
        sending = true
        changed()
        Pocket.link.send(device, trust, Pocket.myName(ctx), cmd) { r ->
            Pocket.mark(r.isSuccess)
            main.post {
                sending = false
                r.onSuccess { e.answer = it.ifEmpty { "Done." }; e.bad = false }
                 .onFailure { e.answer = it.message ?: "Failed."; e.bad = true }
                lastAt = SystemClock.elapsedRealtime()
                changed()
            }
        }
    }
}
