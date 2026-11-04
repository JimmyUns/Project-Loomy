package io.github.jimmyuns.pocketloomy

import android.content.Context
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

// The connection bar on top (2.22, his five states). Online first: in Ellune mode (the default) Bluetooth isn't
// looked at at all; in Bluetooth mode (the blue theme) Ellune isn't.
//   Offline         no internet (Ellune mode) / Bluetooth off or the PC not answering over it (Bluetooth mode)
//   Ellune offline  internet, but Ellune doesn't answer
//   PC offline      Ellune answers, but the PC isn't connected to her
//   Online          Ellune answers and says the PC is connected to her
//   Online BT       the PC answered over Bluetooth
// "Online" in Ellune mode = Ellune says the PC is in its relay room (/status), i.e. remote can reach it.
object Net {
    enum class State(val label: String) {
        CHECKING("checking"), OFFLINE("Offline"), ELLUNE_OFFLINE("Ellune offline"),
        PC_OFFLINE("PC offline"), ONLINE("Online"), ONLINE_BT("Online BT")
    }

    @Volatile var state = State.CHECKING
    @Volatile private var busy = false

    /** check again, in the background (the bar updates when it's done) */
    fun refresh(ctx: Context) {
        if (busy) return
        busy = true
        val app = ctx.applicationContext
        Thread({
            val s = try { check(app) } catch (_: Exception) { State.OFFLINE }
            state = s
            busy = false
            Pocket.changed()
        }, "net-check").start()
    }

    /** a Bluetooth exchange just happened (a command, a ping): that's the answer in Bluetooth mode */
    fun sawBt(ctx: Context, ok: Boolean) {
        if (Store(ctx).btMode) state = if (ok) State.ONLINE_BT else State.OFFLINE
    }

    private fun check(ctx: Context): State {
        val s = Store(ctx)
        if (s.btMode) {
            if (Pocket.readyBt(ctx) != null) return State.OFFLINE
            val d = Pocket.device(ctx) ?: return State.OFFLINE
            val t = s.trust() ?: return State.OFFLINE
            val done = CountDownLatch(1)
            var ok = false
            Pocket.link.ping(d, t, Pocket.myName(ctx)) { r -> ok = r.isSuccess; Pocket.mark(ok); done.countDown() }
            if (!done.await(20, TimeUnit.SECONDS)) return State.OFFLINE
            return if (ok) State.ONLINE_BT else State.OFFLINE
        }
        if (!ElluneEar.online(ctx)) return State.OFFLINE
        if (Ellune.pingNow() !is Ellune.Result.Up) return State.ELLUNE_OFFLINE
        val t = s.trust() ?: return State.PC_OFFLINE
        val k = ElluneKey.peek() ?: return State.PC_OFFLINE
        return if (Ellune.pcThere(k.priv, k.pub, Ellune.room(t.pcPk)) == true) State.ONLINE else State.PC_OFFLINE
    }
}
