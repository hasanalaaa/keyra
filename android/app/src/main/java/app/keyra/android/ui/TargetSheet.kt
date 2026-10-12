package app.keyra.android.ui

import android.content.Intent
import android.os.SystemClock
import android.provider.Settings
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.BluetoothSearching
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material3.AssistChip
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.ListItem
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Text
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import app.keyra.android.Keyra
import app.keyra.android.R
import app.keyra.android.api.Bond
import app.keyra.android.match.TargetKind
import app.keyra.android.match.newBond
import app.keyra.android.match.targetKind
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * Where Keyra types: its own choice, USB, or one of its Bluetooth hosts, one of which the user
 * marks as this phone (Android does not tell apps the phone's own Bluetooth address).
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun TargetSheet(
    info: TargetInfo,
    target: String?,
    phone: String?,
    onChoose: (String?) -> Unit,
    onPhone: (String?) -> Unit,
    refresh: () -> Unit,
    close: () -> Unit,
) {
    var pairing by remember { mutableStateOf(false) }
    ModalBottomSheet(onDismissRequest = close, sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)) {
        Column(
            Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).padding(horizontal = 16.dp).navigationBarsPadding().padding(bottom = 16.dp),
        ) {
            if (pairing) {
                PairFlow(
                    known = (info as? TargetInfo.Known)?.targets?.bonds.orEmpty(),
                    paired = { bond ->
                        onPhone(bond.addr)
                        onChoose(bond.addr)
                    },
                    close = {
                        pairing = false
                        refresh()
                    },
                )
                return@Column
            }
            Text(stringResource(R.string.targets_title), style = MaterialTheme.typography.titleLarge, modifier = Modifier.padding(8.dp))
            val known = (info as? TargetInfo.Known)?.targets
            Choice(
                selected = target == null,
                kind = TargetKind.AUTO,
                title = stringResource(R.string.target_auto),
                detail = stringResource(R.string.target_auto_detail),
            ) { onChoose(null); close() }
            Choice(
                selected = target == "usb",
                kind = TargetKind.USB,
                title = stringResource(R.string.target_usb),
                detail = known?.let { stringResource(if (it.usb) R.string.target_usb_on else R.string.target_usb_off) },
            ) { onChoose("usb"); close() }
            when {
                info is TargetInfo.Unsupported -> Note(stringResource(R.string.targets_old_firmware))
                known == null -> Note(stringResource(R.string.targets_unreachable))
                !known.ble -> Note(stringResource(R.string.targets_ble_off))
                else -> {
                    for (b in known.bonds) {
                        val isPhone = b.addr.equals(phone, ignoreCase = true)
                        Choice(
                            selected = target.equals(b.addr, ignoreCase = true),
                            kind = targetKind(b.addr, phone),
                            title = if (isPhone) stringResource(R.string.target_phone) else b.name.ifBlank { stringResource(R.string.target_bluetooth) },
                            detail = listOfNotNull(
                                if (isPhone) b.name.ifBlank { null } else null,
                                stringResource(if (b.connected) R.string.target_connected else R.string.target_paired),
                            ).joinToString(" · "),
                            extra = {
                                if (isPhone) {
                                    AssistChip(
                                        onClick = { onPhone(null) },
                                        label = { Text(stringResource(R.string.mark_phone_done)) },
                                        leadingIcon = { Icon(Icons.Rounded.CheckCircle, contentDescription = null) },
                                    )
                                } else {
                                    AssistChip(onClick = { onPhone(b.addr) }, label = { Text(stringResource(R.string.mark_phone)) })
                                }
                            },
                        ) { onChoose(b.addr); close() }
                    }
                    HorizontalDivider(Modifier.padding(vertical = 8.dp))
                    FilledTonalButton(onClick = { pairing = true }, modifier = Modifier.fillMaxWidth().padding(8.dp)) {
                        Icon(Icons.AutoMirrored.Rounded.BluetoothSearching, contentDescription = null)
                        Text(stringResource(R.string.pair_phone), modifier = Modifier.padding(start = 8.dp))
                    }
                }
            }
        }
    }
}

@Composable
private fun Choice(
    selected: Boolean,
    kind: TargetKind,
    title: String,
    detail: String?,
    extra: (@Composable () -> Unit)? = null,
    onClick: () -> Unit,
) {
    Column(Modifier.padding(vertical = 2.dp).clip(MaterialTheme.shapes.large).clickable(role = Role.RadioButton, onClick = onClick)) {
        ListItem(
            headlineContent = { Text(title) },
            supportingContent = detail?.let { { Text(it) } },
            leadingContent = { Icon(targetIcon(kind), contentDescription = null) },
            trailingContent = { RadioButton(selected = selected, onClick = null) },
            colors = ListItemDefaults.colors(containerColor = MaterialTheme.colorScheme.surfaceContainerLow),
        )
        if (extra != null) Column(Modifier.padding(start = 72.dp, bottom = 8.dp)) { extra() }
    }
}

@Composable
private fun Note(text: String) {
    Text(text, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.padding(16.dp))
}

/**
 * Pairing this phone. A press opens Keyra's pairing window (SPEC §8.1); the user then picks Keyra
 * in the phone's Bluetooth settings, and the bond that appears in Keyra's list is this phone.
 * Followed through GET /api/agent/targets (pairing.active, bonds): token status covers typing
 * and saving only.
 */
@Composable
private fun PairFlow(known: List<Bond>, paired: (Bond) -> Unit, close: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val api = remember { runCatching { Keyra.api(context) }.getOrNull() }
    var press by remember { mutableStateOf<Press?>(null) }
    var open by remember { mutableStateOf(false) } // the window is open: waiting for the phone
    var job by remember { mutableStateOf<Job?>(null) }

    fun start() {
        val a = api ?: return
        job = scope.launch {
            press = Press.Arming
            val armed = try {
                withContext(Dispatchers.IO) { a.pairBle() }
            } catch (e: Exception) {
                press = Press.Done(false, Keyra.message(context, e))
                return@launch
            }
            val deadline = SystemClock.elapsedRealtime() + armed.expiresInMs
            press = Press.Waiting(deadline, armed.expiresInMs, armed.requestId)
            var windowEnds = 0L
            while (true) {
                delay(2000)
                val now = SystemClock.elapsedRealtime()
                val t = runCatching { withContext(Dispatchers.IO) { a.targets() } }.getOrNull()
                val bond = t?.let { newBond(known, it.bonds) }
                if (bond != null) {
                    paired(bond)
                    open = false
                    press = Press.Done(true, context.getString(R.string.pair_done))
                    return@launch
                }
                if (!open && t?.pairingActive == true) {
                    open = true
                    windowEnds = now + PAIR_WINDOW_MS
                }
                val over = if (open) now > windowEnds else now > deadline + 5000
                if (over) {
                    open = false
                    press = Press.Done(false, context.getString(if (windowEnds > 0) R.string.pair_timeout else R.string.state_expired))
                    return@launch
                }
            }
        }
    }

    Column(Modifier.padding(8.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
        Text(stringResource(R.string.pair_title), style = MaterialTheme.typography.titleLarge)
        val p = press
        when {
            p is Press.Done -> PressPanel(state = p, hint = "", onCancel = {}, onClose = close)
            open -> {
                Text(stringResource(R.string.pair_waiting), style = MaterialTheme.typography.bodyLarge)
                Button(
                    onClick = { context.startActivity(Intent(Settings.ACTION_BLUETOOTH_SETTINGS).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) },
                    modifier = Modifier.fillMaxWidth(),
                ) { Text(stringResource(R.string.pair_open_settings)) }
                LinearProgressIndicator(Modifier.fillMaxWidth())
            }
            p != null -> PressPanel(
                state = p,
                hint = stringResource(R.string.pair_press_hint),
                onCancel = {
                    val w = p as? Press.Waiting ?: return@PressPanel
                    job?.cancel()
                    scope.launch {
                        api?.let { cancelPress(it, w.requestId) }
                        press = Press.Done(false, context.getString(R.string.state_cancelled))
                    }
                },
                onClose = close,
            )
            else -> {
                Text(stringResource(R.string.pair_intro), style = MaterialTheme.typography.bodyLarge)
                Button(enabled = api != null, onClick = { start() }, modifier = Modifier.fillMaxWidth()) {
                    Text(stringResource(R.string.pair_start))
                }
            }
        }
    }
}

private const val PAIR_WINDOW_MS = 120_000L // Keyra's pairing window after the press (SPEC §8.1)
