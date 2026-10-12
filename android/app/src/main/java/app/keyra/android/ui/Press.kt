package app.keyra.android.ui

import android.content.Context
import android.os.SystemClock
import androidx.compose.animation.AnimatedContent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Button
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import app.keyra.android.Keyra
import app.keyra.android.R
import app.keyra.android.api.AgentStatus
import app.keyra.android.api.ApiException
import app.keyra.android.api.Armed
import app.keyra.android.api.KeyraApi
import app.keyra.android.api.State
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext

/** Where one "press Keyra's button" request stands. */
sealed interface Press {
    data object Arming : Press

    /** [deadline] in SystemClock.elapsedRealtime() ms. */
    data class Waiting(val deadline: Long, val total: Long, val requestId: String?, val note: String? = null) : Press

    data class Done(val ok: Boolean, val text: String) : Press
}

private const val POLL_MS = 2000L // the token budget is 10 requests per 10 s (SPEC §17)
private const val GRACE_MS = 5000L

/**
 * Arms with [arm], then polls the request by its requestId (SPEC §12.6a) until it ends, the
 * deadline passes or Keyra cannot be asked any more. Every step goes to [show]. Wi-Fi hiccups
 * are common while Keyra types over Bluetooth, so network errors are retried until the deadline.
 */
suspend fun runPress(context: Context, api: KeyraApi, arm: () -> Armed, show: (Press) -> Unit) {
    show(Press.Arming)
    val armed = try {
        withContext(Dispatchers.IO) { arm() }
    } catch (e: Exception) {
        return show(Press.Done(false, Keyra.message(context, e)))
    }
    var deadline = SystemClock.elapsedRealtime() + armed.expiresInMs
    show(Press.Waiting(deadline, armed.expiresInMs, armed.requestId))
    while (true) {
        delay(POLL_MS)
        val late = SystemClock.elapsedRealtime() > deadline + GRACE_MS
        val status = try {
            withContext(Dispatchers.IO) { api.status(armed.requestId) }
        } catch (e: ApiException) {
            if (e.code != "rate_limited") return show(Press.Done(false, Keyra.message(context, e)))
            delay(e.retryAfterMs)
            continue
        } catch (e: java.io.IOException) {
            if (late) return show(Press.Done(false, context.getString(R.string.state_unknown)))
            continue
        }
        when {
            status.state.finished -> return show(Press.Done(status.state == State.TYPED || status.state == State.SAVED, statusText(context, status)))
            status.state == State.NONE && late -> return show(Press.Done(false, context.getString(R.string.state_expired)))
            else -> {
                status.expiresInMs?.let { deadline = SystemClock.elapsedRealtime() + it }
                val note = if (status.state == State.WAITING) context.getString(R.string.state_waiting) else null
                show(Press.Waiting(deadline, armed.expiresInMs, armed.requestId, note))
            }
        }
    }
}

/** Withdraws the waiting request; the next poll reports how it ended. */
suspend fun cancelPress(api: KeyraApi, requestId: String?): Result<Unit> = withContext(Dispatchers.IO) {
    runCatching { api.cancel(requestId) }
}

fun statusText(context: Context, s: AgentStatus): String = when (s.state) {
    State.TYPED -> context.getString(R.string.state_typed)
    State.SAVED -> context.getString(R.string.state_saved)
    State.CANCELLED -> context.getString(if (s.code == "superseded") R.string.state_superseded else R.string.state_cancelled)
    State.EXPIRED -> context.getString(R.string.state_expired)
    State.FAILED -> s.code?.let { ResultMessages.text(context, it) } ?: context.getString(R.string.err_failed)
    State.WAITING -> context.getString(R.string.state_waiting)
    State.ARMED, State.NONE -> context.getString(R.string.press_title)
}

/** The press card: the Contour while Keyra waits, then how it ended. */
@Composable
fun PressPanel(state: Press, hint: String, onCancel: () -> Unit, onClose: () -> Unit, modifier: Modifier = Modifier) {
    val contour = when (state) {
        is Press.Arming -> ContourState.IDLE
        is Press.Waiting -> ContourState.WAITING
        is Press.Done -> if (state.ok) ContourState.DONE else ContourState.FAILED
    }
    Column(
        modifier.fillMaxWidth().padding(vertical = 8.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Contour(Modifier.size(96.dp), state = contour, color = MaterialTheme.colorScheme.outline)
        Spacer(Modifier.height(24.dp))
        AnimatedContent(targetState = state is Press.Done, label = "press") { done ->
            Column(
                Modifier.fillMaxWidth(),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                if (done && state is Press.Done) {
                    Text(
                        state.text,
                        style = MaterialTheme.typography.titleLarge,
                        textAlign = TextAlign.Center,
                        modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite },
                    )
                    Spacer(Modifier.height(16.dp))
                    Button(onClick = onClose, modifier = Modifier.fillMaxWidth().height(52.dp)) { Text(stringResource(R.string.close)) }
                } else {
                    Text(stringResource(R.string.press_title), style = MaterialTheme.typography.headlineSmall, textAlign = TextAlign.Center)
                    Text(
                        (state as? Press.Waiting)?.note ?: hint,
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        textAlign = TextAlign.Center,
                        modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite },
                    )
                    Countdown(state)
                    Spacer(Modifier.height(8.dp))
                    FilledTonalButton(onClick = onCancel, enabled = state is Press.Waiting, modifier = Modifier.fillMaxWidth().height(52.dp)) {
                        Text(stringResource(R.string.cancel))
                    }
                }
            }
        }
    }
}

/** Seconds left, in monospace so the line does not jitter as it counts. */
@Composable
private fun Countdown(state: Press) {
    var now by remember { mutableLongStateOf(SystemClock.elapsedRealtime()) }
    LaunchedEffect(state) {
        while (true) {
            now = SystemClock.elapsedRealtime()
            delay(250)
        }
    }
    val waiting = state as? Press.Waiting
    val seconds = waiting?.let { (((it.deadline - now).coerceAtLeast(0) + 999) / 1000).toInt() }
    Text(
        seconds?.let { pluralStringResource(R.plurals.seconds_left, it, it) } ?: stringResource(R.string.arming),
        style = MaterialTheme.typography.labelMedium.copy(fontFamily = FontFamily.Monospace),
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        textAlign = TextAlign.Center,
    )
}
