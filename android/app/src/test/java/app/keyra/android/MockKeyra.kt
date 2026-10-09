package app.keyra.android

import org.json.JSONObject
import java.io.File
import java.net.HttpURLConnection
import java.net.ServerSocket
import java.net.URL
import java.util.concurrent.TimeUnit

/**
 * Runs web/mock/server.mjs (the device API mock) on a free port and drives it the way the
 * web app and a human would: unlock, create a token (press, then ask again), press the button.
 * Use "localhost": the mock treats 127.0.0.1 as the home network, which needs a trusted browser.
 */
class MockKeyra private constructor(private val process: Process, val port: Int) : AutoCloseable {
    val base = "http://localhost:$port"
    private var cookie = ""
    private var csrf = ""

    fun unlock() {
        val c = request("POST", "/api/unlock", JSONObject().put("passphrase", "keyra demo vault"))
        check(c.first == 200) { "unlock: ${c.first} ${c.second}" }
        csrf = c.second.getString("csrf")
    }

    fun lock() {
        val c = request("POST", "/api/lock", JSONObject())
        check(c.first in 200..299) { "lock: ${c.first} ${c.second}" }
    }

    /** POST /api/tokens → 202 press → the same call again → 201 with the token (SPEC §17). */
    fun createToken(kind: String, scope: Any = "all"): String {
        val body = JSONObject().put("name", "test-$kind").put("kind", kind).put("scope", scope)
        val first = request("POST", "/api/tokens", body)
        check(first.first == 202) { "token create: ${first.first} ${first.second}" }
        press()
        repeat(20) {
            val again = request("POST", "/api/tokens", body)
            if (again.first == 201) return again.second.getString("token")
            check(again.first == 409 && again.second.optString("error") == "busy") { "token create: ${again.first} ${again.second}" }
            Thread.sleep(100) // the press is still being handled
        }
        error("token was not created")
    }

    fun press(kind: String = "short") {
        val c = request("POST", "/__mock/button", JSONObject().put("press", kind), session = false)
        check(c.first == 200) { "button: ${c.first} ${c.second}" }
    }

    /** A browser session arms its own typing, to make Keyra busy for the token. */
    fun armFromBrowser(id: Long) {
        val c = request("POST", "/api/type", JSONObject().put("id", id).put("what", "password"))
        check(c.first == 202) { "browser type: ${c.first} ${c.second}" }
    }

    private fun request(method: String, path: String, body: JSONObject?, session: Boolean = true): Pair<Int, JSONObject> {
        val conn = URL(base + path).openConnection() as HttpURLConnection
        conn.requestMethod = method
        if (session && cookie.isNotEmpty()) conn.setRequestProperty("Cookie", cookie)
        if (session && csrf.isNotEmpty()) conn.setRequestProperty("X-Keyra-Csrf", csrf)
        if (body != null) {
            conn.doOutput = true
            conn.setRequestProperty("Content-Type", "application/json")
            conn.outputStream.use { it.write(body.toString().toByteArray()) }
        }
        val status = conn.responseCode
        conn.headerFields["Set-Cookie"]?.firstOrNull { it.startsWith("ks=") }?.let { cookie = it.substringBefore(';') }
        val text = (if (status >= 400) conn.errorStream else conn.inputStream)?.use { it.readBytes().toString(Charsets.UTF_8) } ?: ""
        return status to (if (text.isBlank()) JSONObject() else JSONObject(text))
    }

    override fun close() {
        process.destroy()
        process.waitFor(5, TimeUnit.SECONDS)
    }

    companion object {
        val mockPath: String = System.getProperty("keyra.mock") ?: ""
        private val node: String = System.getProperty("keyra.node") ?: "node"

        /** Why the mock cannot run here, or null when it can. */
        fun unavailable(): String? {
            if (mockPath.isEmpty() || !File(mockPath).isFile) return "mock not found at '$mockPath' (set KEYRA_MOCK)"
            return try {
                val p = ProcessBuilder(node, "--version").redirectErrorStream(true).start()
                if (p.waitFor(10, TimeUnit.SECONDS) && p.exitValue() == 0) null else "node did not run"
            } catch (e: Exception) {
                "node not available: ${e.message}"
            }
        }

        fun start(): MockKeyra {
            val port = ServerSocket(0).use { it.localPort }
            val pb = ProcessBuilder(node, mockPath).redirectErrorStream(true)
            pb.environment()["PORT"] = port.toString()
            pb.redirectOutput(File.createTempFile("keyra-mock", ".log"))
            val process = pb.start()
            val deadline = System.currentTimeMillis() + 15_000
            while (System.currentTimeMillis() < deadline) {
                check(process.isAlive) { "mock exited with ${process.exitValue()}" }
                try {
                    val c = URL("http://localhost:$port/api/state").openConnection() as HttpURLConnection
                    c.connectTimeout = 500
                    if (c.responseCode == 200) return MockKeyra(process, port)
                } catch (_: java.io.IOException) {
                    Thread.sleep(100)
                }
            }
            process.destroy()
            error("mock did not start")
        }
    }
}
