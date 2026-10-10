package app.keyra.android

import app.keyra.android.api.ConnectionOpener
import app.keyra.android.api.KeyraApi
import org.junit.Assert.assertThrows
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL

/** Answers that parse as JSON but miss a field must surface as IOException, never crash a caller. */
class KeyraApiOfflineTest {
    private class Canned(url: URL, private val body: String) : HttpURLConnection(url) {
        override fun connect() {}
        override fun disconnect() {}
        override fun usingProxy() = false
        override fun getResponseCode() = 200
        override fun getInputStream() = ByteArrayInputStream(body.toByteArray())
    }

    private fun api(body: String) =
        KeyraApi("http://keyra.local", "keyra_abcdefghijklmnopqrstuvwxyz234567", ConnectionOpener { Canned(it, body) })

    @Test
    fun missingFieldsAreIoErrors() {
        assertThrows(IOException::class.java) { api("{}").entries() }
        assertThrows(IOException::class.java) { api("""{"entries":[{"title":"x"}]}""").entries() }
        assertThrows(IOException::class.java) { api("{}").status() }
        assertThrows(IOException::class.java) { api("{}").generate(app.keyra.android.api.GenerateOptions()) }
    }
}
