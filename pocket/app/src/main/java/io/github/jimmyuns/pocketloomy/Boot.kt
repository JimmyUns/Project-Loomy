package io.github.jimmyuns.pocketloomy

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

// After a reboot or an app update: the speed meter comes back by itself (like a speed meter app).
// Pocket Loomy itself stays off until you turn it on.
class Boot : BroadcastReceiver() {
    override fun onReceive(ctx: Context, intent: Intent) {
        if (intent.action != Intent.ACTION_BOOT_COMPLETED && intent.action != Intent.ACTION_MY_PACKAGE_REPLACED) return
        Store(ctx).on = false
        if (Store(ctx).netSpeed) Pocket.sync(ctx) else Notes.syncOff(ctx)
    }
}
