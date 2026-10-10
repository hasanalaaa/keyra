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
    fun webMatchesSameHostOrSubdomain() {
        assertTrue(HostMatcher.matchesWeb("github.com", "WWW.GitHub.com."))
        assertTrue(HostMatcher.matchesWeb("github.com", "gist.github.com"))
        assertTrue(HostMatcher.matchesWeb("accounts.google.com", "google.com"))
        assertTrue(HostMatcher.matchesWeb("rafidain-bank.gov.iq", "online.rafidain-bank.gov.iq"))
        assertTrue(HostMatcher.matchesWeb("192.168.1.1", "192.168.1.1"))
    }

    @Test
    fun webRejectsOtherSites() {
        assertFalse(HostMatcher.matchesWeb("github.com", "github.com.evil.example"))
        assertFalse(HostMatcher.matchesWeb("github.com", "evilgithub.com"))
        assertFalse(HostMatcher.matchesWeb("github.com", "gitlab.com"))
        // Siblings and shared hosting: never across owners, even ones no list knows.
        assertFalse(HostMatcher.matchesWeb("alice.github.io", "mallory.github.io"))
        assertFalse(HostMatcher.matchesWeb("alice.some-new-host.dev", "mallory.some-new-host.dev"))
        assertFalse(HostMatcher.matchesWeb("mail.google.com", "accounts.google.com"))
        assertFalse(HostMatcher.matchesWeb("bbc.co.uk", "evil.co.uk"))
        assertFalse(HostMatcher.matchesWeb("co.uk", "evil.co.uk"))
        assertFalse(HostMatcher.matchesWeb("com", "github.com"))
        assertFalse(HostMatcher.matchesWeb("192.168.1.1", "192.168.1.2"))
        assertFalse(HostMatcher.matchesWeb("1.1", "192.168.1.1"))
        assertFalse(HostMatcher.matchesWeb("", "github.com"))
        // Only one trailing dot is dropped, as on the device.
        assertFalse(HostMatcher.matchesWeb("github.com", "github.com.."))
        assertFalse(HostMatcher.matchesWeb("a..com", "x.a..com"))
    }

    @Test
    fun appNeverMatchesByPackageName() {
        // Any app may pick a package name like com.github.evil: no guessing from it.
        assertFalse(HostMatcher.matchesApp("github.com", "com.github.android"))
        assertFalse(HostMatcher.matchesApp("github.com", "com.github.evil"))
        assertFalse(HostMatcher.matchesApp("instagram.com", "com.instagram.android"))
    }

    @Test
    fun appMatchesSavedPackageHost() {
        // Logins saved from an app use androidapp://<package>; Keyra's host is the package.
        assertTrue(HostMatcher.matchesApp("com.example.notes", "com.example.notes"))
        assertFalse(HostMatcher.matchesApp("com.example.notes", "com.example.notes2"))
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
    fun browsersAreAnExactList() {
        assertTrue(app.keyra.android.match.Browsers.isBrowser("com.android.chrome"))
        assertTrue(app.keyra.android.match.Browsers.isBrowser("org.mozilla.firefox"))
        assertFalse(app.keyra.android.match.Browsers.isBrowser("com.android.chrome.evil"))
        assertFalse(app.keyra.android.match.Browsers.isBrowser("com.github.android"))
    }

    @Test
    fun targetKey() {
        assertEquals("web:github.com", Target("www.GitHub.com", "com.android.chrome").key)
        assertEquals("app:com.github.android", Target(null, "com.github.android").key)
    }
}
