package io.github.jimmyuns.pocketloomy

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.os.SystemClock

// Listening, in one place, run by PocketService (so it keeps going while you use the phone, with the
// microphone shown as in use). The app screen, the Talk card and the notification only show its state.
// Two modes (2.22, the Listening dropdown; Store.solo):
//   Say "Loomy": only what's said TO Loomy counts: her name opening the sentence ("Loomy show screen", "okay Lumi
//          system info", "ayo Loomy ..."). Everything else is dropped on the spot: never shown, never sent.
//          Her name on its own ("Loomy" ... pause ... "show"): the next thing said (within 8 s) is the command.
//          Tapping the mic counts as saying her name for the first sentence.
//   Solo:  everything said is a command, no name needed (for when he's alone).
//   In both a sentence ends on a pause or "thank you" (what comes after it is dropped); "please" is just a filler.
//   input mode ("Loomy input mode"): bare key words work ("next tab", "back", "ok", "down three").
//   By voice: "solo mode" / "solo mode off", "input mode" / "input mode off" (name optional).
//   "turn off the mic" / "stop listening" = mic off until Talk; "stop the connection" = Loomy off.
//   What means what is in Phrases.kt.
// Who hears (2.23, his feedback: Whisper felt slow and noise kept it open): by default the phone hears everything
//   (Android's recognizer: fast, ends the sentence itself). If none of what it heard is a command the phone's rules
//   know ("display extent", "make the screens dark"), the text goes to Ellune's AI (/understand) and her answer is
//   sent as is (his call; the PC refuses anything that isn't a real command). The setting "Ellune hears you"
//   (Store.elluneHears) is the 2.21/2.22 way: she records the command herself (ElluneEar, Whisper). Waiting for
//   "Loomy" always stays on the phone. No silent switching: if Ellune fails, it says so and tries her again next
//   time. Offline, or in Bluetooth mode, it's the phone and its rules only.
object Voice {
    enum class State { OFF, LISTENING }

    class Entry(val cmd: String, var answer: String, var bad: Boolean)

    @Volatile var state = State.OFF
    @Volatile var keep = false
    @Volatile var sending = false
    @Volatile var partial = ""
    @Volatile var level = 0f
    @Volatile var armed = false            // heard just "ayo Loomy": the next thing said is the command
    @Volatile var inputMode = false        // "Loomy input mode": bare key words (next, back, ok...) work
    @Volatile var lastAt = 0L              // when the last answer came (the notification shows it for a while)
    @Volatile var viaEllune = false        // this listen is going to Ellune
    @Volatile var thinking = false         // the mic is paused while Ellune works out what was said / meant
    val history = mutableListOf<Entry>()  // touched on the main thread only

    /** PocketService hears about start/stop here, to add or drop the microphone */
    var onChange: () -> Unit = {}

    private val main = Handler(Looper.getMainLooper())
    private var ear: Ear? = null
    private var eEar: ElluneEar? = null
    private var elluneKey: ElluneKey.Key? = null
    private var whisper = false                // "Ellune hears you" is on (and not Bluetooth mode)
    private lateinit var ctx: Context
    private var armedUntil = 0L
    private var followUp = false
    private var errors = 0

    fun last(): Entry? = history.lastOrNull()

    /** 2.22, Ellune mode: a tap on the mic while listening = "send it now" (Ellune's ear stops and sends what was said;
     *  the phone's ear waiting for her name: the tap counts as her name, Ellune takes the next sentence). Listening
     *  then carries on; holding the mic is what mutes. */
    fun tap(c: Context) {
        if (state != State.LISTENING || thinking) return
        val e = eEar
        if (viaEllune && e != null) { e.finishNow(); return }
        ctx = c.applicationContext
        armed = true
        armedUntil = SystemClock.elapsedRealtime() + 8000
        ear?.cancel()
        partial = ""
        startEar()
        changed()
    }

    /** listening for the command itself right now (after her name, a mic tap, or in Solo), not for her name */
    fun takingCommand(c: Context): Boolean =
        state == State.LISTENING && (inputMode || !keep || (armed && SystemClock.elapsedRealtime() < armedUntil) || Store(c).solo)

    /** main thread */
    fun start(c: Context, keepListening: Boolean) {
        ctx = c
        keep = keepListening
        errors = 0
        val st = Store(c)
        whisper = st.elluneHears && !st.btMode
        elluneKey = if (!st.btMode) ElluneKey.peek() else null    // Bluetooth mode: Ellune isn't used at all
        if (state == State.LISTENING) { changed(); return }
        if (keep && !st.solo) { armed = true; armedUntil = SystemClock.elapsedRealtime() + 8000 }   // the tap = her name
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
        eEar?.destroy()
        eEar = null
        viaEllune = false
        thinking = false
        if (state != State.OFF) { state = State.OFF; partial = ""; level = 0f; changed() }
    }

    private fun changed() { Pocket.changed(); onChange() }

    private fun listenNow() {
        state = State.LISTENING
        partial = ""
        changed()
        startEar()
    }

    // ---- which ear: Android's (on the phone, the default) or Ellune's ("Ellune hears you")
    /** Ellune can help right now: not Bluetooth mode, a key, internet */
    private fun elluneUp() = elluneKey != null && ElluneEar.online(ctx)

    /** this listen goes to Ellune's ear: only with "Ellune hears you" on, and never while waiting for "Loomy"
     *  (that stays on the phone) or in input mode */
    private fun wantEllune(): Boolean {
        if (!whisper || inputMode || !elluneUp()) return false
        val armedNow = armed && SystemClock.elapsedRealtime() < armedUntil
        return !keep || armedNow || Store(ctx).solo
    }

    private fun startEar() {
        if (wantEllune()) {
            ear?.cancel()
            viaEllune = true
            val e = eEar ?: ElluneEar(ctx,
                partial = { partial = it; Pocket.changed() },
                level = { level = it; Pocket.changed() },
                heard = { heard(it) },
                missed = { missed(it) },
                failed = { elluneFailed(it) },
                busy = { thinking = it; Pocket.changed() }).also { eEar = it }
            if (partial.isEmpty()) partial = "Loomy is listening..."
            Pocket.changed()
            e.start()
            return
        }
        eEar?.cancel()
        viaEllune = false
        val e = ear ?: Ear(ctx, Words.HINTS,
            partial = { partial = it; Pocket.changed() },
            level = { level = it; Pocket.changed() },
            heard = { heard(it) },
            missed = { missed(it) }).also { ear = it }
        // Ellune would hear this one but can't: say why (the phone's recognizer only knows set phrases)
        val armedNow = armed && SystemClock.elapsedRealtime() < armedUntil
        if (partial.isEmpty() && whisper && !inputMode && (!keep || armedNow || Store(ctx).solo))
            partial = when {
                !ElluneEar.online(ctx) -> "Offline: the phone's recognizer, set phrases only"
                elluneKey == null -> "Not on Ellune's guest list yet: the phone's recognizer"
                else -> ""
            }
        Pocket.changed()
        e.start()
    }

    // Ellune didn't answer: say so, nothing sent, and keep listening (she's tried again next time; no switching)
    private fun elluneFailed(a: Ellune.Answer.Down) {
        viaEllune = false
        add("(Ellune)", if (a.notGuest) "Ellune didn't let this phone in (${a.why}). Check the Ellune part of the settings."
                        else "Ellune didn't answer (${a.why}). Nothing was sent: say it again.", true)
        if (state != State.LISTENING) return
        if (!keep) { next(100); return }
        partial = ""
        main.postDelayed({ if (state == State.LISTENING) startEar() }, 300)
    }

    // after a result: keep mode listens again, once mode is done (unless her name came alone: then once more)
    private fun next(delayMs: Long) {
        if (!keep && followUp) {
            followUp = false
            partial = "Yes?"
            main.postDelayed({ if (state == State.LISTENING) startEar() }, delayMs)
            return
        }
        if (!keep) { state = State.OFF; partial = ""; level = 0f; ear?.destroy(); ear = null; eEar?.destroy(); eEar = null; viaEllune = false; changed(); return }
        partial = ""
        main.postDelayed({ if (keep && state == State.LISTENING) startEar() }, delayMs)
    }

    private fun heard(raw: List<String>) {
        errors = 0
        val now = SystemClock.elapsedRealtime()
        val solo = Store(ctx).solo
        // Whisper on a noise writes things like "[typing]", "you", "Thanks for watching!": not speech
        if (viaEllune && raw.all { Words.isNoise(it) }) { next(100); return }
        // "thank you" ends the sentence: keep what came before it
        val cut = raw.map { Words.untilThanks(it) }
        val all = cut.map { it.first }
        val first = all.firstOrNull() ?: ""
        if (cut.any { it.second } && all.all { (Words.addressed(it) ?: it).isBlank() }) { next(100); return }  // just "thank you"

        // the modes, by voice (name optional: you might be mid-flow)
        val plain = all.map { Words.plain(it) }
        if (plain.any { Words.isInputModeOff(it) }) { switchInputMode(false); next(100); return }
        if (plain.any { Words.isSoloOff(it) }) { switchSolo(false); next(100); return }
        if (plain.any { Words.isInputModeOn(it) }) { keep = true; switchInputMode(true); next(100); return }
        if (plain.any { Words.isSoloOn(it) }) { keep = true; switchSolo(true); next(100); return }

        // was it said to Loomy? her name; or, without it: right after her name alone (or a mic tap), solo mode,
        // or a single Talk. Mute / stop connection need that too, except in input mode.
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
            if (solo && keep) { next(100); return }                 // solo doesn't need it
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

        // the phone's recognizer gives a few guesses ("display extent", "display extend"): the first one the rules
        // know wins, so a slip of the accent costs nothing when one of the guesses got it right
        var text: String = said
        var cmd = Words.command(text)
        if (!Words.knows(cmd)) {
            for (h in all.drop(1)) {
                val t = Words.addressed(h) ?: (if (following || solo || !keep) h else null) ?: continue
                val c = Words.command(t)
                if (t.isNotBlank() && Words.knows(c)) { text = t; cmd = c; break }
            }
        }
        // the rules don't know it: ask Ellune's AI (only what was said TO Loomy gets here)
        val k = elluneKey
        if (!Words.knows(cmd) && k != null && elluneUp()) {
            understand(k, text, quiet = solo && !following)          // listening resumes when she's answered
            return
        }
        // Solo hears everything: what isn't a command is dropped quietly (talking, typing, the TV), not a red miss
        if (solo && !following && !Words.knows(cmd)) { next(150); return }
        if (cmd.isEmpty()) { miss(text); next(150); return }
        send(cmd)
        next(150)
    }

    private fun miss(said: String) {
        add(said.ifEmpty { "(nothing)" }, "Didn't catch a command.", true)
        Pocket.reportMiss(ctx, said)                                // the PC shows misses too
    }

    // Ellune's AI picks the command; it's sent as is ("said -> sent" in the history). If she can't answer,
    // it says so and nothing is sent (no falling back to the rules' guess: his call, 2.22).
    private fun understand(k: ElluneKey.Key, said: String, quiet: Boolean = false) {
        // the mic is off meanwhile (his ask: a loading sign, then back to how it was)
        ear?.cancel(); eEar?.cancel()
        thinking = true
        partial = "Ellune is understanding..."
        changed()
        Thread({
            val a = Ellune.understandNow(k.priv, k.pub, said)
            main.post {
                thinking = false
                partial = ""
                if (state == State.LISTENING) next(150) else changed()
                when (a) {
                    is Ellune.Answer.Ok -> if (a.text.isNotEmpty()) send(a.text, "$said \u2192 ${a.text}") else if (!quiet) miss(said)
                    is Ellune.Answer.Down -> add(said, "Ellune couldn't answer (${a.why}). Nothing was sent.", true)
                }
            }
        }, "ellune-understand").start()
    }

    // phone-side controls: "turn off the mic" / "stop listening" stop listening (Talk starts it again);
    // "stop connection" turns Pocket Loomy off (the app, the tile or the notification turn it back on)
    private fun control(text: String): Boolean {
        val c = Words.plain(text)
        if (Words.isDisconnect(c)) {
            add("stop connection", "Connection off. Turn Pocket Loomy on again from the app, the Loomy tile or the notification.", false)
            Pocket.turnOff(ctx)
            return true
        }
        if (Words.isMute(c)) {
            add("mic off", "Mic off. Tap Talk to listen again.", false)
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
            if (on) "Solo: everything you say is a command, no need to say Loomy. \u201CSolo mode off\u201D ends it."
            else "Say \u201CLoomy\u201D mode: start with her name again.", false)
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

    private fun send(cmd: String, shown: String = cmd) {
        val why = Pocket.ready(ctx)
        if (why != null) { add(shown, why, true); return }
        Pocket.syncPipe(ctx)                                        // Ellune mode: through her relay; Bluetooth mode: Bluetooth
        val device = Pocket.device(ctx)
        if (device == null && Store(ctx).btMode) { add(shown, "Choose your PC (Bluetooth) in the app first.", true); return }
        val trust = Store(ctx).trust() ?: return
        val e = Entry(shown, "...", false)
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
