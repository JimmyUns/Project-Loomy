package io.github.jimmyuns.pocketloomy

import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import java.security.KeyPairGenerator
import java.security.KeyStore
import java.security.PrivateKey
import java.security.interfaces.ECPublicKey
import java.security.spec.ECGenParameterSpec

// This phone's own key for Ellune's guest list (2.21). P-256 in the Android Keystore: it can sign, but the
// private half never leaves the Keystore (not even to this app), so a copied app or backup can't pretend to
// be this phone. Separate from the pairing keys (those change every 7 days; this one stays, so the list entry stays valid).
object ElluneKey {
    private const val ALIAS = "ellune_device"

    class Key(val priv: PrivateKey, val pub: ByteArray) {
        val id get() = Ellune.deviceId(pub)
        val line get() = Ellune.guestLine(pub)
    }

    private fun store() = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }

    // The key if it exists, without making one
    fun peek(): Key? = try {
        val ks = store()
        val priv = ks.getKey(ALIAS, null) as? PrivateKey
        val pub = ks.getCertificate(ALIAS)?.publicKey as? ECPublicKey
        if (priv != null && pub != null) Key(priv, Ellune.rawPublic(pub)) else null
    } catch (_: Exception) { null }

    // The key, made the first time it's needed. null if the Keystore refuses (rare, broken phones).
    fun get(): Key? = peek() ?: try {
        val g = KeyPairGenerator.getInstance(KeyProperties.KEY_ALGORITHM_EC, "AndroidKeyStore")
        g.initialize(KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_SIGN)
            .setAlgorithmParameterSpec(ECGenParameterSpec("secp256r1"))
            .setDigests(KeyProperties.DIGEST_SHA256)
            .build())
        g.generateKeyPair()
        peek()
    } catch (_: Exception) { null }
}
