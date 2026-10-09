package app.keyra.android

import app.keyra.android.api.Entry
import app.keyra.android.match.HostMatcher
import app.keyra.android.match.Target
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class HostMatcherTest {
    @Test
    fun baseDomain() {
        assertEquals("google.com", HostMatcher.baseDomain("accounts.google.com"))
        assertEquals("github.com", HostMatcher.baseDomain("www.github.com"))
        assertEquals("rafidain-bank.gov.iq", HostMatcher.baseDomain("rafidain-bank.gov.iq"))
        assertEquals("rafidain-bank.gov.iq", HostMatcher.baseDomain("online.rafidain-bank.gov.iq"))
        assertEquals("bbc.co.uk", HostMatcher.baseDomain("account.bbc.co.uk"))
        assertEquals("zain.com", HostMatcher.baseDomain("iq.zain.com"))
        assertEquals("alice.github.io", HostMatcher.baseDomain("docs.alice.github.io"))
        assertEquals("192.168.1.1", HostMatcher.baseDomain("192.168.1.1"))
    }

    @Test
    fun webMatchesSameSite() {
        assertTrue(HostMatcher.matchesWeb("accounts.google.com", "google.com"))
        assertTrue(HostMatcher.matchesWeb("github.com", "WWW.GitHub.com."))
        assertTrue(HostMatcher.matchesWeb("store.steampowered.com", "login.steampowered.com"))
        assertTrue(HostMatcher.matchesWeb("192.168.1.1", "192.168.1.1"))
    }

    @Test
    fun webRejectsOtherSites() {
        assertFalse(HostMatcher.matchesWeb("github.com", "github.com.evil.example"))
        assertFalse(HostMatcher.matchesWeb("github.com", "gitlab.com"))
        assertFalse(HostMatcher.matchesWeb("alice.github.io", "mallory.github.io"))
        assertFalse(HostMatcher.matchesWeb("bbc.co.uk", "evil.co.uk"))
        assertFalse(HostMatcher.matchesWeb("192.168.1.1", "192.168.1.2"))
        assertFalse(HostMatcher.matchesWeb("", "github.com"))
    }

    @Test
    fun appMatchesByPackageLabel() {
        assertTrue(HostMatcher.matchesApp("github.com", "com.github.android"))
        assertTrue(HostMatcher.matchesApp("instagram.com", "com.instagram.android"))
        assertTrue(HostMatcher.matchesApp("accounts.google.com", "com.google.android.gm"))
        assertTrue(HostMatcher.matchesApp("netflix.com", "com.netflix.mediaclient"))
        assertTrue(HostMatcher.matchesApp("spotify.com", "com.spotify.music"))
        assertTrue(HostMatcher.matchesApp("rafidain-bank.gov.iq", "iq.rafidainbank.mobile"))
        assertTrue(HostMatcher.matchesApp("discord.com", "com.discordapp.chat"))
    }

    @Test
    fun appMatchesSavedPackageHost() {
        // Logins saved from an app use androidapp://<package>; Keyra's host is the package.
        assertTrue(HostMatcher.matchesApp("com.example.notes", "com.example.notes"))
        assertFalse(HostMatcher.matchesApp("com.example.notes", "com.example.notes2"))
    }

    @Test
    fun appRejectsUnrelatedPackages() {
        assertFalse(HostMatcher.matchesApp("x.com", "com.twitter.android")) // pin it instead
        assertFalse(HostMatcher.matchesApp("x.com", "com.xing.android"))
        assertFalse(HostMatcher.matchesApp("github.com", "com.gitlab.app"))
        assertFalse(HostMatcher.matchesApp("app.com", "com.example.app"))
        assertFalse(HostMatcher.matchesApp("192.168.1.1", "com.router.app"))
        assertFalse(HostMatcher.matchesApp("netflix.com", "com.net.android"))
    }

    @Test
    fun matchPutsPinsFirstAndLimits() {
        val entries = listOf(
            Entry(1, "GitHub", "github.com"),
            Entry(2, "GitHub work", "github.com"),
            Entry(3, "X", "x.com"),
            Entry(4, "Google", "accounts.google.com"),
        )
        val app = Target(null, "com.twitter.android")
        assertEquals(emptyList<Entry>(), HostMatcher.match(entries, app, emptyList()))
        assertEquals(listOf(3L), HostMatcher.match(entries, app, listOf(3L)).map { it.id })

        val gh = Target("github.com", "com.android.chrome")
        assertEquals(listOf(2L, 1L), HostMatcher.match(entries, gh, listOf(2L)).map { it.id })
        assertEquals(listOf(1L), HostMatcher.match(entries, gh, emptyList(), limit = 1).map { it.id })
    }

    @Test
    fun targetKey() {
        assertEquals("web:github.com", Target("www.GitHub.com", "com.android.chrome").key)
        assertEquals("app:com.github.android", Target(null, "com.github.android").key)
    }
}
