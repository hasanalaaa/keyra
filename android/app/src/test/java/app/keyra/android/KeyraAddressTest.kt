package app.keyra.android

import app.keyra.android.api.KeyraAddress
import app.keyra.android.api.KeyraApi
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Test

class KeyraAddressTest {
    @Test
    fun acceptsLocalAddresses() {
        assertEquals("http://keyra.local", KeyraAddress.normalize("keyra.local"))
        assertEquals("http://keyra.local", KeyraAddress.normalize(" http://Keyra.local/ "))
        assertEquals("http://192.168.4.1", KeyraAddress.normalize("192.168.4.1"))
        assertEquals("http://10.0.2.2:8787", KeyraAddress.normalize("http://10.0.2.2:8787"))
        assertEquals("http://172.20.1.5", KeyraAddress.normalize("172.20.1.5"))
        assertEquals("http://localhost:8787", KeyraAddress.normalize("localhost:8787"))
        assertEquals("http://keyra.home.arpa", KeyraAddress.normalize("keyra.home.arpa"))
        assertEquals("http://[fe80::1]", KeyraAddress.normalize("http://[fe80::1]"))
        assertEquals("https://keyra.example.com", KeyraAddress.normalize("https://keyra.example.com"))
    }

    @Test
    fun refusesPlainHttpOffTheLocalNetwork() {
        assertNull(KeyraAddress.normalize("http://example.com"))
        assertNull(KeyraAddress.normalize("8.8.8.8"))
        assertNull(KeyraAddress.normalize("172.32.0.1"))
        assertNull(KeyraAddress.normalize("192.169.1.1"))
        assertNull(KeyraAddress.normalize("keyra.local.evil.com"))
        assertNull(KeyraAddress.normalize("http://[2001:db8::1]"))
    }

    @Test
    fun refusesOddShapes() {
        assertNull(KeyraAddress.normalize(""))
        assertNull(KeyraAddress.normalize("ftp://keyra.local"))
        assertNull(KeyraAddress.normalize("http://keyra.local/api"))
        assertNull(KeyraAddress.normalize("http://user@keyra.local"))
        assertNull(KeyraAddress.normalize("http://keyra.local?x=1"))
        assertNull(KeyraAddress.normalize("http://192.168.1.300"))
    }

    @Test
    fun clientRefusesBadInput() {
        val good = "keyra_" + "a".repeat(32)
        assertThrows(IllegalArgumentException::class.java) { KeyraApi("http://example.com", good) }
        assertThrows(IllegalArgumentException::class.java) { KeyraApi("keyra.local", "keyra_short") }
        assertThrows(IllegalArgumentException::class.java) { KeyraApi("keyra.local", "keyra_" + "A".repeat(32)) }
        KeyraApi("keyra.local", good)
    }
}
