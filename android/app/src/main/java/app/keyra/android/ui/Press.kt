package app.keyra.android.ui

import android.content.Context
import android.os.SystemClock
import androidx.compose.animation.AnimatedContent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.ErrorOutline
import androidx.compose.material.icons.rounded.TouchApp
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
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

/** The press card: a countdown ring while Keyra waits, then how it ended. */
@Composable
fun PressPanel(state: Press, hint: String, onCancel: () -> Unit, onClose: () -> Unit, modifier: Modifier = Modifier) {
    AnimatedContent(targetState = state is Press.Done, label = "press", modifier = modifier) { done ->
        Column(
            Modifier.fillMaxWidth().padding(vertical = 8.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            if (done && state is Press.Done) {
                Icon(
                    if (state.ok) Icons.Rounded.CheckCircle else Icons.Rounded.ErrorOutline,
                    contentDescription = null,
                    tint = if (state.ok) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.error,
                    modifier = Modifier.size(56.dp),
                )
                Text(
                    state.text,
                    style = MaterialTheme.typography.titleMedium,
                    textAlign = TextAlign.Center,
                    modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite },
                )
                Button(onClick = onClose, modifier = Modifier.fillMaxWidth()) { Text(stringResource(R.string.close)) }
            } else {
                Countdown(state)
                Text(stringResource(R.string.press_title), style = MaterialTheme.typography.headlineSmall, textAlign = TextAlign.Center)
                Text(
                    (state as? Press.Waiting)?.note ?: hint,
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    textAlign = TextAlign.Center,
                    modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite },
                )
                FilledTonalButton(onClick = onCancel, enabled = state is Press.Waiting, modifier = Modifier.fillMaxWidth()) {
                    Text(stringResource(R.string.cancel))
                }
            }
        }
    }
}

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
    Box(contentAlignment = Alignment.Center, modifier = Modifier.size(112.dp)) {
        if (waiting == null) {
            CircularProgressIndicator(modifier = Modifier.size(112.dp), strokeWidth = 6.dp)
        } else {
            val left = (waiting.deadline - now).coerceAtLeast(0)
            CircularProgressIndicator(
                progress = { if (waiting.total > 0) left.toFloat() / waiting.total else 0f },
                modifier = Modifier.size(112.dp),
                strokeWidth = 6.dp,
            )
            val seconds = ((left + 999) / 1000).toInt()
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Icon(Icons.Rounded.TouchApp, contentDescription = null, tint = MaterialTheme.colorScheme.primary)
                Text(
                    pluralStringResource(R.plurals.seconds_left, seconds, seconds),
                    style = MaterialTheme.typography.labelSmall,
                    textAlign = TextAlign.Center,
                )
            }
        }
    }
}
