package app.keyra.android.ui

import android.os.SystemClock
import app.keyra.android.Keyra
import app.keyra.android.api.AgentStatus
import app.keyra.android.api.ApiException
import app.keyra.android.api.KeyraApi
import app.keyra.android.api.State

/**
 * Counts down to Keyra's deadline and polls this token's status until it is final.
 * Polls every 2 s: the token budget is 10 requests per 10 s (SPEC §17). Main thread only.
 */
class PressWaiter(private val api: KeyraApi, private val listener: Listener) {
    interface Listener {
        fun onTick(secondsLeft: Int)
        fun onStatus(status: AgentStatus)
        /** [status] is null when Keyra could not be asked any more ([error] says why). */
        fun onFinished(status: AgentStatus?, error: Throwable?)
    }

    private var deadline = 0L
    private var stopped = false
    private val tick = Runnable { tick() }
    private val poll = Runnable { poll() }

    fun start(expiresInMs: Long) {
        deadline = SystemClock.elapsedRealtime() + expiresInMs
        tick()
        Keyra.main.postDelayed(poll, POLL_MS)
    }

    fun stop() {
        stopped = true
        Keyra.main.removeCallbacks(tick)
        Keyra.main.removeCallbacks(poll)
    }

    private fun secondsLeft() = ((deadline - SystemClock.elapsedRealtime() + 999) / 1000).toInt().coerceAtLeast(0)

    private fun tick() {
        if (stopped) return
        listener.onTick(secondsLeft())
        Keyra.main.postDelayed(tick, 1000)
    }

    private fun poll() {
        if (stopped) return
        Keyra.async({ api.status() }) { r ->
            if (stopped) return@async
            val late = SystemClock.elapsedRealtime() > deadline + GRACE_MS
            r.onSuccess { s ->
                when {
                    s.state.finished -> finish(s, null)
                    s.state == State.NONE && late -> finish(AgentStatus(State.EXPIRED), null)
                    else -> {
                        s.expiresInMs?.let { deadline = SystemClock.elapsedRealtime() + it }
                        listener.onStatus(s)
                        Keyra.main.postDelayed(poll, POLL_MS)
                    }
                }
            }.onFailure { e ->
                when {
                    e is ApiException && e.code == "rate_limited" -> Keyra.main.postDelayed(poll, maxOf(POLL_MS, e.retryAfterMs))
                    // Wi-Fi hiccups are common while Keyra types over Bluetooth; keep trying until the deadline.
                    e is ApiException || late -> finish(null, e)
                    else -> Keyra.main.postDelayed(poll, POLL_MS)
                }
            }
        }
    }

    private fun finish(status: AgentStatus?, error: Throwable?) {
        stop()
        listener.onFinished(status, error)
    }

    private companion object {
        const val POLL_MS = 2000L
        const val GRACE_MS = 5000L
    }
}
