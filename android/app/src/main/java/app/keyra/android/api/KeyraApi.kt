package app.keyra.android.api

import org.json.JSONException
import org.json.JSONObject
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL

/** A login as a token sees it (SPEC §17): never a username or a secret. */
data class Entry(val id: Long, val title: String, val host: String)

enum class What(val wire: String) { USERNAME("username"), PASSWORD("password"), BOTH("both"), TOTP("totp") }

enum class State {
    NONE, ARMED, WAITING, TYPED, SAVED, CANCELLED, EXPIRED, FAILED;

    val finished: Boolean get() = this == TYPED || this == SAVED || this == CANCELLED || this == EXPIRED || this == FAILED

    companion object {
        fun parse(s: String): State = entries.firstOrNull { it.name.equals(s, ignoreCase = true) }
            ?: throw IOException("Unknown status \"$s\" from Keyra")
    }
}

/** GET /api/agent/status: this token's last request, or the one named by its requestId. */
data class AgentStatus(
    val state: State,
    val request: String? = null,
    val id: Long? = null,
    val title: String? = null,
    /** The §5 result code; "superseded" when a newer request of this token replaced it. */
    val code: String? = null,
    val expiresInMs: Long? = null,
    val requestId: String? = null,
)

/**
 * A request now waiting for the press: how long Keyra waits, in ms, and the id that names it
 * (SPEC §12.6a; null from firmware before 0.5, which then answers status for the newest request).
 */
data class Armed(val expiresInMs: Long, val requestId: String?)

private val REQUEST_ID_RE = Regex("^[0-9a-f]{32}$")

/** A requestId as Keyra hands them out; anything else is dropped. */
fun requestIdOf(s: String?): String? = s?.takeIf { REQUEST_ID_RE.matches(it) }

data class GenerateOptions(
    val length: Int = 20,
    val lower: Boolean = true,
    val upper: Boolean = true,
    val digits: Boolean = true,
    val symbols: Boolean = true,
    val avoidAmbiguous: Boolean = false,
)

/** An error answer from Keyra: [code] is the API's `error` field (SPEC §5). */
class ApiException(val httpStatus: Int, val code: String, message: String, val retryAfterMs: Long = 0) :
    IOException(message)

/** Opens a connection; on Android it binds to the Wi-Fi network so Keyra's own access point works. */
fun interface ConnectionOpener {
    fun open(url: URL): HttpURLConnection
}

/**
 * Client for Keyra's token API (SPEC §17). Blocking: call it off the main thread.
 * No endpoint it calls returns a stored secret; [generate] returns a fresh password that
 * was never stored.
 */
class KeyraApi(
    baseUrl: String,
    private val token: String,
    private val opener: ConnectionOpener = ConnectionOpener { it.openConnection() as HttpURLConnection },
    private val timeoutMs: Int = 8000,
) {
    private val base = requireNotNull(KeyraAddress.normalize(baseUrl)) { "Not a local Keyra address: $baseUrl" }

    init {
        require(TOKEN_RE.matches(token)) { "Malformed access token" }
    }

    fun entries(): List<Entry> = answer {
        val list = call("GET", "entries").getJSONArray("entries")
        (0 until list.length()).map { i ->
            val o = list.getJSONObject(i)
            Entry(o.getLong("id"), o.getString("title"), o.optString("host", ""))
        }
    }

    /** Arms typing; Keyra then waits for the press. */
    fun type(id: Long, what: What): Armed {
        val body = JSONObject().put("id", id).put("what", what.wire)
        return answer { armed(call("POST", "type", body)) }
    }

    /** With [requestId]: that request of this token; without: its newest one (as before 0.5). */
    fun status(requestId: String? = null): AgentStatus = answer {
        val rid = requestIdOf(requestId)
        val o = call("GET", if (rid != null) "status?requestId=$rid" else "status")
        AgentStatus(
            state = State.parse(o.getString("state")),
            request = o.optStringOrNull("request"),
            id = if (o.has("id")) o.getLong("id") else null,
            title = o.optStringOrNull("title"),
            code = o.optStringOrNull("code"),
            expiresInMs = if (o.has("expiresIn")) o.getLong("expiresIn") else null,
            requestId = o.optStringOrNull("requestId"),
        )
    }

    /**
     * Withdraws this token's waiting item, or with [requestId] only that request;
     * [ApiException] `not_cancelled` when nothing (of that request) waits, `superseded`
     * when a newer request of this token had replaced it.
     */
    fun cancel(requestId: String? = null) {
        val rid = requestIdOf(requestId)
        call("POST", "cancel", rid?.let { JSONObject().put("requestId", it) })
    }

    /** Asks Keyra to store a new login; the press saves it. */
    fun save(title: String, url: String, username: String, password: String): Armed {
        val body = JSONObject().put("title", title)
        if (url.isNotEmpty()) body.put("url", url)
        if (username.isNotEmpty()) body.put("username", username)
        if (password.isNotEmpty()) body.put("password", password)
        return answer { armed(call("POST", "save", body)) }
    }

    private fun armed(o: JSONObject) = Armed(o.getLong("expiresIn"), requestIdOf(o.optStringOrNull("requestId")))

    fun generate(o: GenerateOptions): String {
        val body = JSONObject()
            .put("length", o.length)
            .put("lower", o.lower)
            .put("upper", o.upper)
            .put("digits", o.digits)
            .put("symbols", o.symbols)
            .put("avoidAmbiguous", o.avoidAmbiguous)
        return answer { call("POST", "generate", body).getString("password") }
    }

    /** A well-formed answer missing a field is a Keyra problem like any other: an IOException. */
    private inline fun <T> answer(read: () -> T): T = try {
        read()
    } catch (e: JSONException) {
        throw IOException("Keyra sent an unexpected answer", e)
    }

    private fun call(method: String, path: String, body: JSONObject? = null): JSONObject {
        val conn = opener.open(URL("$base/api/agent/$path"))
        try {
            conn.requestMethod = method
            conn.connectTimeout = timeoutMs
            conn.readTimeout = timeoutMs
            conn.useCaches = false
            conn.instanceFollowRedirects = false
            conn.setRequestProperty("Authorization", "Bearer $token")
            conn.setRequestProperty("Accept", "application/json")
            if (body != null) {
                val bytes = body.toString().toByteArray(Charsets.UTF_8)
                conn.doOutput = true
                conn.setRequestProperty("Content-Type", "application/json")
                conn.setFixedLengthStreamingMode(bytes.size)
                conn.outputStream.use { it.write(bytes) }
            }
            val status = conn.responseCode
            val stream = if (status >= 400) conn.errorStream else conn.inputStream
            val text = stream?.use { it.readBytes().toString(Charsets.UTF_8) } ?: ""
            if (status >= 400) throw error(status, text, conn.getHeaderField("Retry-After"))
            if (status == 204 || text.isBlank()) return JSONObject()
            return try {
                JSONObject(text)
            } catch (e: JSONException) {
                throw IOException("Keyra sent an unreadable answer ($status)", e)
            }
        } finally {
            conn.disconnect()
        }
    }

    private fun error(status: Int, text: String, retryAfter: String?): ApiException {
        val o = try {
            JSONObject(text)
        } catch (_: JSONException) {
            JSONObject()
        }
        val retryMs = if (o.has("retryAfterMs")) o.optLong("retryAfterMs") else (retryAfter?.toLongOrNull() ?: 0) * 1000
        return ApiException(
            httpStatus = status,
            code = o.optStringOrNull("error") ?: "http_$status",
            message = o.optStringOrNull("message") ?: "HTTP $status",
            retryAfterMs = retryMs,
        )
    }

    companion object {
        val TOKEN_RE = Regex("^keyra_[a-z2-7]{32}$")
    }
}

private fun JSONObject.optStringOrNull(key: String): String? =
    if (has(key) && !isNull(key)) getString(key) else null
