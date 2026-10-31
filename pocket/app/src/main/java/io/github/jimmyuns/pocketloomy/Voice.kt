package io.github.jimmyuns.pocketloomy

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.os.SystemClock

// Listening, in one place, run by PocketService (so it keeps going while you use the phone, with the
// microphone shown as in use). The app screen, the Talk card and the notification only show its state.
//   once:  listen for one command, send it, done. Her name in front is optional.
//   keep:  listen again after every result until stopped, and only act on what's said TO Loomy: her name
//          opening the sentence ("Loomy show screen", "okay Lumi system info", "ayo Loomy ..."). Everything
//          else it hears is dropped on the spot: never shown, never sent.
//   the name on its own ("Loomy" ... pause ... "show"): the recognizer often cuts there, so she takes the
//          next thing said (within 8 s) as the command. Works for a single Talk tap too.
//   input mode ("Loomy input mode"): bare key words work ("next tab", "back", "ok", "down three").
//   solo mode (setting, or "Loomy solo mode"): no name needed at all, everything heard is a command.
//   Both end with "... mode off" / "disable ... mode" (name optional), or by stopping.
//   Key words with the name work any time: "Loomy down", "Loomy ok".
object Voice {
    enum class State { OFF, LISTENING }

    class Entry(val cmd: String, var answer: String, var bad: Boolean)

    @Volatile var state = State.OFF
    @Volatile var keep = false
    @Volatile var sending = false
    @Volatile var partial = ""
    @Volatile var level = 0f
    @Volatile var armed = false            // heard just "ayo Loomy": the next thing said is the command
    @Volatile var inputMode = false        // "ayo Loomy input mode": bare key words (next, back, ok...) work
    @Volatile var lastAt = 0L              // when the last answer came (the notification shows it for a while)
    val history = mutableListOf<Entry>()  // touched on the main thread only

    /** PocketService hears about start/stop here, to add or drop the microphone */
    var onChange: () -> Unit = {}

    private val main = Handler(Looper.getMainLooper())
    private var ear: Ear? = null
    private lateinit var ctx: Context
    private var armedUntil = 0L
    private var followUp = false
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
        followUp = false
        armed = false
        inputMode = false
        main.removeCallbacksAndMessages(null)
        ear?.cancel()
        ear?.destroy()
        ear = null
        if (state != State.OFF) { state = State.OFF; partial = ""; level = 0f; changed() }
    }

    private fun changed() { Pocket.changed(); onChange() }

    private fun listenNow() {
        val e = ear ?: Ear(ctx, Words.HINTS,
            partial = { partial = it; Pocket.changed() },
            level = { level = it; Pocket.changed() },
            heard = { heard(it) },
            missed = { missed(it) }).also { ear = it }
        state = State.LISTENING
        partial = ""
        changed()
        e.start()
    }

    // after a result: keep mode listens again, once mode is done (unless her name came alone: then once more)
    private fun next(delayMs: Long) {
        if (!keep && followUp) {
            followUp = false
            partial = "Yes?"
            main.postDelayed({ if (state == State.LISTENING) ear?.start() }, delayMs)
            return
        }
        if (!keep) { state = State.OFF; partial = ""; level = 0f; ear?.destroy(); ear = null; changed(); return }
        partial = ""
        main.postDelayed({ if (keep && state == State.LISTENING) ear?.start() }, delayMs)
    }

    private fun heard(all: List<String>) {
        errors = 0
        val now = SystemClock.elapsedRealtime()
        val first = all.firstOrNull() ?: ""
        val solo = Store(ctx).solo

        // the modes, by voice (name optional: you might be mid-flow)
        val plain = all.map { Words.command(it) }
        if (plain.any { Words.isInputModeOff(it) }) { switchInputMode(false); next(100); return }
        if (plain.any { Words.isSoloOff(it) }) { switchSolo(false); next(100); return }
        if (plain.any { Words.isInputModeOn(it) }) { keep = true; switchInputMode(true); next(100); return }
        if (plain.any { Words.isSoloOn(it) }) { keep = true; switchSolo(true); next(100); return }

        // was it said to Loomy? her name, a follow-up after her name alone, solo mode, or a Talk tap (once)
        // (mute / stop connection need that too, except in input mode, so "that's it" from a friend can't mute you)
        var said: String? = all.firstNotNullOfOrNull { Words.addressed(it) }
        val following = armed && now < armedUntil
        if (said == null && (following || solo || !keep)) said = first
        if (said == null) {
            // not for Loomy... unless it's bare keys (or mute / stop connection) in input mode
            if (inputMode && all.any { control(it) }) return
            if (inputMode) all.firstNotNullOfOrNull { Words.key(it) }?.let { armed = false; send(it) }
            else if (all.any { Words.endsWithName(it) }) arm()           // "... okay Loomy" (cut off there)
            next(100)
            return
        }
        if (said.isBlank()) {                                       // just her name: the command is coming
            arm()
            if (!keep) followUp = true
            next(100)
            return
        }
        armed = false
        if (control(said)) return

        // a key word ("down", "ok", "next tab") goes to the PC's open screen
        val key = all.firstNotNullOfOrNull { h -> (Words.addressed(h) ?: if (following || solo || inputMode || !keep) h else null)?.let { Words.key(it) } }
        if (key != null) { send(key); next(150); return }

        val cmd = Words.command(said)
        if (cmd.isEmpty()) {
            add(said.ifEmpty { "(nothing)" }, "Didn't catch a command.", true)
            Pocket.reportMiss(ctx, said)                            // the PC shows misses too
            next(150)
            return
        }
        send(cmd)
        next(150)
    }

    // phone-side controls: "mute" / "ok that's it" stop listening (Talk starts it again);
    // "stop connection" turns Pocket Loomy off (the app, the tile or the notification turn it back on)
    private fun control(text: String): Boolean {
        val c = Words.command(text)
        if (Words.isDisconnect(c)) {
            add("stop connection", "Connection off. Turn Pocket Loomy on again from the app, the Loomy tile or the notification.", false)
            Pocket.turnOff(ctx)
            return true
        }
        if (Words.isStop(c)) {
            add("mute", "Muted. Tap Talk to listen again.", false)
            stop()
            return true
        }
        return false
    }

    private fun arm() {
        armed = true
        armedUntil = SystemClock.elapsedRealtime() + 8000
        partial = "Yes?"
        Pocket.changed()
    }

    private fun switchSolo(on: Boolean) {
        Store(ctx).solo = on
        armed = false
        add(if (on) "solo mode" else "solo mode off",
            if (on) "Solo mode on: no need to say Loomy, everything you say is a command. \u201CSolo mode off\u201D to stop."
            else "Solo mode off: start with \u201CLoomy\u201D again.", false)
    }

    private fun missed(error: Int) {
        if (Ear.isMiss(error)) {
            if (!keep && followUp) { followUp = false; armed = false; add("Loomy", "Heard her name, then nothing.", true); next(100); return }
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

    private fun switchInputMode(on: Boolean) {
        if (inputMode == on) return
        inputMode = on
        armed = false
        add(if (on) "input mode" else "input mode off",
            if (on) "Input mode on. Just say next, next tab, back, ok, cancel, up, down (\u201Cdown three\u201D works). \u201CInput mode off\u201D to stop."
            else "Input mode off. Commands need \u201Cayo Loomy\u201D again.", false)
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
