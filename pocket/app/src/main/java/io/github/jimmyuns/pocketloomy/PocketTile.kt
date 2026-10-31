package io.github.jimmyuns.pocketloomy

import android.app.PendingIntent
import android.content.Intent
import android.os.Build
import android.service.quicksettings.Tile
import android.service.quicksettings.TileService

// The "Loomy" quick-settings tile: tap to turn Pocket Loomy on or off.
class PocketTile : TileService() {
    override fun onStartListening() { show() }

    override fun onClick() {
        if (Store(this).on) { Pocket.turnOff(this); show(); return }
        if (Pocket.turnOn(this) != null) openApp() else show()
    }

    private fun show() {
        val t = qsTile ?: return
        val on = Store(this).on
        t.state = if (on) Tile.STATE_ACTIVE else Tile.STATE_INACTIVE
        t.label = "Loomy"
        if (Build.VERSION.SDK_INT >= 29) t.subtitle = if (on) "On" else "Off"
        t.updateTile()
    }

    private fun openApp() {
        val i = Intent(this, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        if (Build.VERSION.SDK_INT >= 34) startActivityAndCollapse(PendingIntent.getActivity(this, 0, i, PendingIntent.FLAG_IMMUTABLE))
        else @Suppress("DEPRECATION") startActivityAndCollapse(i)
    }
}
