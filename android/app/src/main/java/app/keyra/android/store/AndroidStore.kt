package app.keyra.android.store

import android.content.Context
import android.content.SharedPreferences
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import java.security.KeyStore
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey

class PrefsKeyValue(private val prefs: SharedPreferences) : KeyValueStore {
    override fun get(key: String): String? = prefs.getString(key, null)

    override fun put(key: String, value: String?) {
        // commit(): the setup screen reports success only after the value is on disk.
        val ok = prefs.edit().apply { if (value == null) remove(key) else putString(key, value) }.commit()
        check(ok) { "Could not write app settings" }
    }
}

/** A non-exportable AES-256 key in the Android Keystore, created on first use. */
object KeystoreKey {
    private const val ALIAS = "keyra-token"

    @Synchronized
    fun get(): SecretKey {
        val ks = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        (ks.getKey(ALIAS, null) as SecretKey?)?.let { return it }
        val gen = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore")
        gen.init(
            KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .build(),
        )
        return gen.generateKey()
    }
}

fun appStore(context: Context): AppStore = AppStore(
    PrefsKeyValue(context.applicationContext.getSharedPreferences("keyra", Context.MODE_PRIVATE)),
    AesGcmBox(KeystoreKey::get),
)
