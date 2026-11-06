package io.github.jimmyuns.pocketloomy

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import java.nio.ByteBuffer
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

// What this phone remembers about its PC. The keys are sealed with an AES key that lives in the
// Android Keystore (it never leaves the secure hardware / OS), so the stored blob is useless if copied.
class Trust(val sk: ByteArray, val pk: ByteArray, val pcPk: ByteArray, val pairedAt: Long, val pcName: String) {
    val expiresAt get() = pairedAt + TRUST_MS
    fun valid(now: Long = System.currentTimeMillis()) = now < expiresAt && now + 3_600_000 >= pairedAt
    companion object { const val TRUST_MS = 7L * 24 * 3600 * 1000 }   // the PC uses the same 7 days
}

class Store(ctx: Context) {
    private val prefs = ctx.getSharedPreferences("pocket", Context.MODE_PRIVATE)

    var pcAddress: String?
        get() = prefs.getString("pc_address", null)
        set(v) { prefs.edit().putString("pc_address", v).apply() }
    var pcLabel: String?
        get() = prefs.getString("pc_label", null)
        set(v) { prefs.edit().putString("pc_label", v).apply() }

    /** the On switch (notification, tile, app): a foreground service keeps checking in while it's on */
    var on: Boolean
        get() = prefs.getBoolean("on", false)
        set(v) { prefs.edit().putBoolean("on", v).apply() }
    var askedBattery: Boolean
        get() = prefs.getBoolean("asked_battery", false)
        set(v) { prefs.edit().putBoolean("asked_battery", v).apply() }

    // settings (his 2.18 feedback)
    /** Pocket Loomy in the notifications: off = no "off" notification, and a minimized one while it runs */
    var showNotif: Boolean
        get() = prefs.getBoolean("show_notif", true)
        set(v) { prefs.edit().putBoolean("show_notif", v).apply() }
    /** the small card over other apps when Talk is pressed in the notification */
    var talkCard: Boolean
        get() = prefs.getBoolean("talk_card", true)
        set(v) { prefs.edit().putBoolean("talk_card", v).apply() }
    /** the Listening mode (2.22): false = Say "Loomy" (everything after her name), true = Solo (everything said) */
    var solo: Boolean
        get() = prefs.getBoolean("solo", false)
        set(v) { prefs.edit().putBoolean("solo", v).apply() }
    /** 2.24d: she speaks her answers out loud (quiet anyway on silent / vibrate) */
    var talkBack: Boolean
        get() = prefs.getBoolean("talk_back", true)
        set(v) { prefs.edit().putBoolean("talk_back", v).apply() }
    /** 2.22: how Pocket Loomy reaches the PC. false (default) = through Ellune (online first, his call): no
     *  Bluetooth checks at all. true = Bluetooth mode (the blue theme): Bluetooth only, Ellune ignored entirely.
     *  Only he switches it (the Bluetooth button over the mic); nothing switches it by itself. */
    var btMode: Boolean
        get() = prefs.getBoolean("bt_mode", false)
        set(v) { prefs.edit().putBoolean("bt_mode", v).apply() }
    /** 2.22: the background grid's strength, 0..100 (Visuals in the settings) */
    var gridOpacity: Int
        get() = prefs.getInt("grid_opacity", 50)
        set(v) { prefs.edit().putInt("grid_opacity", v.coerceIn(0, 100)).apply() }
    /** 2.23 (his feedback: Ellune listening was slow and a motorcycle outside kept it open): unticked (default) =
     *  the phone hears you (Android's recognizer, fast) and Ellune's AI only works out what the phone's rules
     *  don't know. Ticked = "Ellune hears you": she records the command herself (Whisper), slower. Replaces 2.22's
     *  "Use the phone's recognizer" (phone_ear), whose unticked default was Whisper. */
    var elluneHears: Boolean
        get() = prefs.getBoolean("ellune_hears", false)
        set(v) { prefs.edit().putBoolean("ellune_hears", v).apply() }
    /** the phone's up/down speed in the status bar */
    var netSpeed: Boolean
        get() = prefs.getBoolean("net_speed", false)
        set(v) { prefs.edit().putBoolean("net_speed", v).apply() }

    fun trust(): Trust? {
        val sealed = prefs.getString("trust", null) ?: return null
        return try {
            val raw = Base64.decode(sealed, Base64.NO_WRAP)
            val c = Cipher.getInstance("AES/GCM/NoPadding")
            c.init(Cipher.DECRYPT_MODE, key(), GCMParameterSpec(128, raw, 0, 12))
            val b = ByteBuffer.wrap(c.doFinal(raw, 12, raw.size - 12))
            val sk = ByteArray(32).also { b.get(it) }
            val pk = ByteArray(32).also { b.get(it) }
            val pcPk = ByteArray(32).also { b.get(it) }
            val at = b.long
            val name = ByteArray(b.remaining()).also { b.get(it) }
            Trust(sk, pk, pcPk, at, String(name, Charsets.UTF_8))
        } catch (e: Exception) { null }
    }

    fun save(t: Trust) {
        val name = t.pcName.toByteArray(Charsets.UTF_8)
        val plain = ByteBuffer.allocate(96 + 8 + name.size).put(t.sk).put(t.pk).put(t.pcPk).putLong(t.pairedAt).put(name).array()
        val c = Cipher.getInstance("AES/GCM/NoPadding")
        c.init(Cipher.ENCRYPT_MODE, key())
        val sealed = c.iv + c.doFinal(plain)
        plain.fill(0)
        prefs.edit().putString("trust", Base64.encodeToString(sealed, Base64.NO_WRAP)).apply()
    }

    fun forget() { prefs.edit().remove("trust").apply() }

    private fun key(): SecretKey {
        val ks = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        (ks.getKey(ALIAS, null) as? SecretKey)?.let { return it }
        val g = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore")
        g.init(KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
            .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
            .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
            .setKeySize(256)
            .build())
        return g.generateKey()
    }

    private companion object { const val ALIAS = "pocket_trust" }
}
