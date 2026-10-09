package app.keyra.android

import app.keyra.android.store.AesGcmBox
import app.keyra.android.store.AppStore
import app.keyra.android.store.KeyValueStore
import app.keyra.android.store.Pin
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import java.security.GeneralSecurityException
import java.util.Base64
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey

class AppStoreTest {
    private class MapStore : KeyValueStore {
        val map = mutableMapOf<String, String>()
        override fun get(key: String) = map[key]
        override fun put(key: String, value: String?) {
            if (value == null) map.remove(key) else map[key] = value
        }
    }

    private fun key(): SecretKey = KeyGenerator.getInstance("AES").apply { init(256) }.generateKey()

    /** A box with its own fresh key, standing in for the Keystore key. */
    private fun box(): AesGcmBox = key().let { k -> AesGcmBox { k } }

    private val token = "keyra_abcdefghijklmnopqrstuvwxyz234567"

    @Test
    fun tokenIsStoredSealed() {
        val kv = MapStore()
        val k = key()
        val store = AppStore(kv, AesGcmBox { k })
        assertFalse(store.configured)
        assertNull(store.token)
        store.address = "http://keyra.local"
        store.token = token
        assertTrue(store.configured)
        assertEquals(token, store.token)
        assertFalse(kv.map.values.any { it.contains("keyra_") })
        // Same plaintext, fresh IV: two seals differ.
        val first = kv.map["token"]
        store.token = token
        assertFalse(first == kv.map["token"])
    }

    @Test
    fun tamperedOrForeignTokenFailsLoudly() {
        val kv = MapStore()
        val store = AppStore(kv, box())
        store.token = token
        val raw = Base64.getDecoder().decode(kv.map["token"])
        raw[raw.size - 1] = (raw.last().toInt() xor 1).toByte()
        kv.map["token"] = Base64.getEncoder().encodeToString(raw)
        assertThrows(GeneralSecurityException::class.java) { store.token }

        val other = AppStore(kv, box())
        other.token = token
        val stranger = AppStore(kv, box())
        assertThrows(GeneralSecurityException::class.java) { stranger.token }
    }

    @Test
    fun pinsAndClear() {
        val kv = MapStore()
        val k = key()
        val store = AppStore(kv, AesGcmBox { k })
        store.pin("app:com.twitter.android", 3, "X")
        store.pin("web:github.com", 1, "GitHub")
        store.pin("web:github.com", 2, "GitHub work")
        assertEquals(listOf(Pin("app:com.twitter.android", 3, "X"), Pin("web:github.com", 2, "GitHub work")), store.pins())
        assertEquals(2L, store.pinnedFor("web:github.com"))
        assertNull(store.pinnedFor("web:gitlab.com"))
        store.unpin("web:github.com")
        assertEquals(1, store.pins().size)

        store.address = "http://keyra.local"
        store.token = token
        store.clear()
        assertTrue(kv.map.isEmpty())
        assertFalse(store.configured)
    }
}
