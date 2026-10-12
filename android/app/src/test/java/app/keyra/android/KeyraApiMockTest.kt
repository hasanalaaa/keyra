package app.keyra.android

import app.keyra.android.api.AgentStatus
import app.keyra.android.api.ApiException
import app.keyra.android.api.GenerateOptions
import app.keyra.android.api.KeyraApi
import app.keyra.android.api.State
import app.keyra.android.api.What
import app.keyra.android.api.requestIdOf
import org.json.JSONArray
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import java.net.HttpURLConnection
import java.net.URL

/** The client against web/mock/server.mjs, which mirrors the firmware's token API (SPEC §17). */
class KeyraApiMockTest {
    private lateinit var mock: MockKeyra
    private lateinit var api: KeyraApi

    @Before
    fun setUp() {
        val why = MockKeyra.unavailable()
        assumeTrue("API tests skipped: $why", why == null)
        mock = MockKeyra.start()
        val probe = URL("${mock.base}/api/agent/entries").openConnection() as HttpURLConnection
        if (probe.responseCode == 404) {
            mock.close()
            assumeTrue("API tests skipped: ${MockKeyra.mockPath} predates the token API (SPEC §17)", false)
        }
        mock.unlock()
        api = KeyraApi(mock.base, mock.createToken("app"))
    }

    @After
    fun tearDown() {
        if (::mock.isInitialized) mock.close()
    }

    private fun github() = api.entries().first { it.host == "github.com" }

    private fun finalStatus(): AgentStatus {
        repeat(40) {
            try {
                val s = api.status()
                if (s.state.finished) return s
                Thread.sleep(500)
            } catch (e: ApiException) {
                if (e.code != "rate_limited") throw e
                Thread.sleep(e.retryAfterMs)
            }
        }
        fail("no final status")
        error("unreachable")
    }

    private fun expectError(code: String, status: Int, block: () -> Unit): ApiException {
        try {
            block()
        } catch (e: ApiException) {
            assertEquals(code, e.code)
            assertEquals(status, e.httpStatus)
            return e
        }
        fail("expected $code")
        error("unreachable")
    }

    @Test
    fun entriesGiveTitlesAndHosts() {
        val entries = api.entries()
        assertTrue(entries.size > 5)
        assertEquals("GitHub", github().title)
        assertTrue(entries.all { it.id > 0 && it.title.isNotEmpty() })
    }

    @Test
    fun typeWaitsForThePressThenTypes() {
        val e = github()
        val armedAt = api.type(e.id, What.BOTH)
        assertTrue(armedAt.expiresInMs in 1..60_000)
        assertTrue(armedAt.requestId!!.matches(Regex("^[0-9a-f]{32}$")))
        val armed = api.status(armedAt.requestId)
        assertEquals(State.ARMED, armed.state)
        assertEquals("GitHub", armed.title)
        assertEquals("type", armed.request)
        assertEquals(armedAt.requestId, armed.requestId)
        mock.press()
        assertEquals(State.TYPED, finalStatus().state)
    }

    // SPEC §12.6a: status and cancel follow one request by its requestId; a request the same
    // token replaced reports "superseded" and cannot cancel the newer one.
    @Test
    fun requestIdFollowsExactlyOneRequest() {
        val id = github().id
        val first = api.type(id, What.USERNAME)
        val second = api.type(id, What.PASSWORD)
        assertNotEquals(first.requestId, second.requestId)
        val old = api.status(first.requestId)
        assertEquals(State.CANCELLED, old.state)
        assertEquals("superseded", old.code)
        expectError("superseded", 409) { api.cancel(first.requestId) }
        assertEquals(State.ARMED, api.status(second.requestId).state)
        api.cancel(second.requestId)
        // Another token knows nothing of it and cannot withdraw it.
        val other = KeyraApi(mock.base, mock.createToken("app")) // before arming: making it needs the slot
        val third = api.type(id, What.PASSWORD)
        assertEquals(State.NONE, other.status(third.requestId).state)
        expectError("not_cancelled", 409) { other.cancel(third.requestId) }
        assertEquals(State.ARMED, api.status(third.requestId).state)
    }

    @Test
    fun requestIdIsDroppedWhenMalformed() {
        assertEquals(null, requestIdOf("nope"))
        assertEquals(null, requestIdOf(null))
        assertEquals("0123456789abcdef0123456789abcdef", requestIdOf("0123456789abcdef0123456789abcdef"))
    }

    @Test
    fun cancelEndsTheWaitingRequest() {
        api.type(github().id, What.PASSWORD)
        api.cancel()
        assertEquals(State.CANCELLED, api.status().state)
        expectError("not_cancelled", 409) { api.cancel() }
    }

    @Test
    fun busyWhileABrowserRequestWaits() {
        val id = github().id
        mock.armFromBrowser(id)
        expectError("busy", 409) { api.type(id, What.USERNAME) }
    }

    @Test
    fun unknownTokenIsRefused() {
        val stranger = KeyraApi(mock.base, "keyra_" + "a".repeat(32))
        expectError("invalid_token", 401) { stranger.entries() }
    }

    @Test
    fun lockedKeyraAnswersLocked() {
        mock.lock()
        expectError("locked", 401) { api.entries() }
    }

    @Test
    fun saveStoresAfterThePress() {
        val saving = api.save("Example app", "androidapp://com.example.app", "sara", "s3cret-pass")
        val armed = api.status(saving.requestId)
        assertEquals(State.ARMED, armed.state)
        assertEquals("save", armed.request)
        mock.press()
        val done = finalStatus()
        assertEquals(State.SAVED, done.state)
        assertNotNull(done.id)
        val id = done.id!!
        val saved = api.entries().first { it.id == id }
        assertEquals("Example app", saved.title)
        assertEquals("com.example.app", saved.host)
    }

    @Test
    fun generateReturnsAFreshPassword() {
        val pw = api.generate(GenerateOptions(length = 24, symbols = false))
        assertEquals(24, pw.length)
        assertTrue(pw.all { it.isLetterOrDigit() })
        assertFalse(pw == api.generate(GenerateOptions(length = 24, symbols = false)))
    }

    @Test
    fun agentTokenCannotSaveOrGenerate() {
        val agent = KeyraApi(mock.base, mock.createToken("agent"))
        expectError("forbidden", 403) { agent.save("x", "", "", "") }
        expectError("forbidden", 403) { agent.generate(GenerateOptions()) }
    }

    @Test
    fun scopedTokenSeesOnlyItsLogins() {
        val all = api.entries()
        val gh = all.first { it.host == "github.com" }
        val other = all.first { it.id != gh.id }
        val scoped = KeyraApi(mock.base, mock.createToken("app", JSONArray().put(gh.id)))
        assertEquals(listOf(gh), scoped.entries())
        expectError("not_found", 404) { scoped.type(other.id, What.PASSWORD) }
    }

    @Test
    fun tooManyRequestsAreRateLimited() {
        val e = expectError("rate_limited", 429) { repeat(12) { api.status() } }
        assertTrue(e.retryAfterMs > 0)
    }
}
