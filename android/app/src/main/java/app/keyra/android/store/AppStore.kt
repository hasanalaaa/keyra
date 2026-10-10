package app.keyra.android.store

import org.json.JSONObject
import java.security.GeneralSecurityException
import java.util.Base64
import javax.crypto.Cipher
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/** String key-value storage (SharedPreferences on Android, a map in tests). */
interface KeyValueStore {
    fun get(key: String): String?
    fun put(key: String, value: String?)
}

/** Encrypts small values. */
interface SecretBox {
    fun seal(plain: ByteArray): ByteArray
    fun open(sealed: ByteArray): ByteArray
}

/**
 * AES-256-GCM with a 12-byte IV the cipher picks (Android Keystore keys refuse caller IVs).
 * Format: iv(12) || ciphertext+tag. [key] is asked for on every use so a Keystore key is
 * created lazily and never leaves the Keystore.
 */
class AesGcmBox(private val key: () -> SecretKey) : SecretBox {
    override fun seal(plain: ByteArray): ByteArray {
        val c = Cipher.getInstance(TRANSFORMATION)
        c.init(Cipher.ENCRYPT_MODE, key())
        val iv = c.iv
        check(iv.size == IV_BYTES) { "Unexpected IV length ${iv.size}" }
        return iv + c.doFinal(plain)
    }

    override fun open(sealed: ByteArray): ByteArray {
        require(sealed.size > IV_BYTES + TAG_BYTES) { "Sealed value too short" }
        val c = Cipher.getInstance(TRANSFORMATION)
        c.init(Cipher.DECRYPT_MODE, key(), GCMParameterSpec(TAG_BYTES * 8, sealed, 0, IV_BYTES))
        return c.doFinal(sealed, IV_BYTES, sealed.size - IV_BYTES)
    }

    companion object {
        const val TRANSFORMATION = "AES/GCM/NoPadding"
        private const val IV_BYTES = 12
        private const val TAG_BYTES = 16
    }
}

/** A remembered choice: this app or site gets this login offered first. */
data class Pin(val key: String, val entryId: Long, val title: String)

/**
 * Everything the app keeps: Keyra's address, the access token (sealed) and remembered
 * app/site → login choices. Never a password, a username or anything else from the vault.
 */
class AppStore(private val kv: KeyValueStore, private val box: SecretBox) {
    var address: String?
        get() = kv.get(K_ADDRESS)
        set(v) = kv.put(K_ADDRESS, v)

    /**
     * The token, or null when none is stored. A value that no longer opens (the Keystore key
     * was lost, e.g. after a lock-screen reset on some devices) is an error, not "no token".
     */
    var token: String?
        get() = kv.get(K_TOKEN)?.let {
            try {
                String(box.open(Base64.getDecoder().decode(it)), Charsets.UTF_8)
            } catch (e: IllegalArgumentException) {
                // Not Base64, or too short to be sealed: as unreadable as a wrong key.
                throw GeneralSecurityException("Stored token is damaged", e)
            }
        }
        set(v) = kv.put(K_TOKEN, v?.let { Base64.getEncoder().encodeToString(box.seal(it.toByteArray(Charsets.UTF_8))) })

    val configured: Boolean get() = address != null && kv.get(K_TOKEN) != null

    fun pins(): List<Pin> {
        val o = kv.get(K_PINS)?.let(::JSONObject) ?: return emptyList()
        return o.keys().asSequence().map { k ->
            val p = o.getJSONObject(k)
            Pin(k, p.getLong("id"), p.getString("title"))
        }.sortedBy { it.key }.toList()
    }

    fun pin(key: String, entryId: Long, title: String) {
        val o = kv.get(K_PINS)?.let(::JSONObject) ?: JSONObject()
        o.put(key, JSONObject().put("id", entryId).put("title", title))
        kv.put(K_PINS, o.toString())
    }

    fun unpin(key: String) {
        val o = kv.get(K_PINS)?.let(::JSONObject) ?: return
        o.remove(key)
        kv.put(K_PINS, o.toString())
    }

    fun pinnedFor(key: String): Long? = pins().firstOrNull { it.key == key }?.entryId

    /** Forgets everything (Settings → Disconnect). */
    fun clear() {
        listOf(K_ADDRESS, K_TOKEN, K_PINS).forEach { kv.put(it, null) }
    }

    private companion object {
        const val K_ADDRESS = "address"
        const val K_TOKEN = "token"
        const val K_PINS = "pins"
    }
}
