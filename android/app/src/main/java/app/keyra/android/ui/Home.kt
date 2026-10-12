package app.keyra.android.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.Login
import androidx.compose.material.icons.rounded.AutoMode
import androidx.compose.material.icons.rounded.Bluetooth
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.CloudOff
import androidx.compose.material.icons.rounded.Key
import androidx.compose.material.icons.rounded.Password
import androidx.compose.material.icons.rounded.Person
import androidx.compose.material.icons.rounded.PhoneAndroid
import androidx.compose.material.icons.rounded.Pin
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material.icons.rounded.Settings
import androidx.compose.material.icons.rounded.SwapHoriz
import androidx.compose.material.icons.rounded.Usb
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextField
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextDirection
import androidx.compose.ui.unit.dp
import app.keyra.android.Keyra
import app.keyra.android.R
import app.keyra.android.api.ApiException
import app.keyra.android.api.Entry
import app.keyra.android.api.KeyraApi
import app.keyra.android.api.Targets
import app.keyra.android.api.What
import app.keyra.android.match.TargetKind
import app.keyra.android.match.stillValid
import app.keyra.android.match.targetKind
import app.keyra.android.store.appStore
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.security.GeneralSecurityException

/** What Keyra said about its devices: known, not supported by its firmware, or not asked yet. */
sealed interface TargetInfo {
    data object Unknown : TargetInfo
    data object Unsupported : TargetInfo
    data class Known(val targets: Targets) : TargetInfo
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun HomeScreen(generation: Int, openSettings: () -> Unit, openGenerator: () -> Unit) {
    val context = LocalContext.current
    val store = remember { appStore(context) }
    val scope = rememberCoroutineScope()
    var entries by remember { mutableStateOf<List<Entry>?>(null) }
    var error by remember { mutableStateOf<String?>(null) }
    var refreshing by remember { mutableStateOf(false) }
    var info by remember { mutableStateOf<TargetInfo>(TargetInfo.Unknown) }
    var query by rememberSaveable { mutableStateOf("") }
    var target by remember { mutableStateOf(store.target) }
    var phone by remember { mutableStateOf(store.phone) }
    var chosen by remember { mutableStateOf<Entry?>(null) }
    var pickTarget by remember { mutableStateOf(false) }

    fun api(): KeyraApi? = try {
        Keyra.api(context)
    } catch (_: GeneralSecurityException) {
        error = context.getString(R.string.err_token_unreadable)
        null
    }

    fun load() {
        val api = api() ?: return
        scope.launch {
            refreshing = true
            try {
                entries = withContext(Dispatchers.IO) { api.entries() }.sortedBy { it.title.lowercase() }
                error = null
            } catch (e: Exception) {
                error = Keyra.message(context, e)
            }
            info = try {
                TargetInfo.Known(withContext(Dispatchers.IO) { api.targets() })
            } catch (e: ApiException) {
                if (e.code == "not_found") TargetInfo.Unsupported else info
            } catch (_: java.io.IOException) {
                info
            }
            (info as? TargetInfo.Known)?.let { known ->
                val valid = stillValid(target, known.targets.bonds)
                if (valid != target) {
                    target = valid
                    store.target = valid
                }
            }
            refreshing = false
        }
    }
    LaunchedEffect(generation) { load() }

    val shown = remember(entries, query) {
        val q = query.trim().lowercase()
        entries.orEmpty().filter { q.isEmpty() || q in it.title.lowercase() || q in it.host }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(stringResource(R.string.home_title)) },
                actions = {
                    IconButton(onClick = openGenerator) { Icon(Icons.Rounded.Password, stringResource(R.string.menu_generate)) }
                    IconButton(onClick = openSettings) { Icon(Icons.Rounded.Settings, stringResource(R.string.menu_settings)) }
                },
            )
        },
    ) { padding ->
        PullToRefreshBox(isRefreshing = refreshing, onRefresh = { load() }, modifier = Modifier.padding(padding).fillMaxSize()) {
            LazyColumn(contentPadding = PaddingValues(bottom = 24.dp), modifier = Modifier.fillMaxSize()) {
                item { SearchField(query) { query = it } }
                item { TargetCard(target, phone, info) { pickTarget = true } }
                error?.let { msg -> item { OfflineCard(msg) { load() } } }
                val list = entries
                when {
                    list == null && error == null -> item {
                        Box(Modifier.fillMaxWidth().padding(48.dp), contentAlignment = Alignment.Center) { CircularProgressIndicator() }
                    }
                    list != null && shown.isEmpty() -> item {
                        Text(
                            stringResource(if (list.isEmpty()) R.string.empty_list else R.string.no_results),
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            textAlign = TextAlign.Center,
                            modifier = Modifier.fillMaxWidth().padding(32.dp),
                        )
                    }
                }
                items(shown, key = { it.id }) { e -> AccountRow(e) { chosen = e } }
            }
        }
    }

    chosen?.let { e ->
        AccountSheet(e, target, phone, info, changeTarget = { pickTarget = true }, close = { chosen = null })
    }
    if (pickTarget) {
        TargetSheet(
            info = info,
            target = target,
            phone = phone,
            onChoose = {
                target = it
                store.target = it
            },
            onPhone = {
                phone = it
                store.phone = it
            },
            refresh = { load() },
            close = { pickTarget = false },
        )
    }
}

@Composable
private fun SearchField(query: String, onChange: (String) -> Unit) {
    TextField(
        value = query,
        onValueChange = onChange,
        placeholder = { Text(stringResource(R.string.search_hint)) },
        leadingIcon = { Icon(Icons.Rounded.Search, contentDescription = null) },
        trailingIcon = {
            if (query.isNotEmpty()) IconButton(onClick = { onChange("") }) { Icon(Icons.Rounded.Close, stringResource(R.string.search_clear)) }
        },
        singleLine = true,
        shape = CircleShape,
        colors = TextFieldDefaults.colors(focusedIndicatorColor = Color.Transparent, unfocusedIndicatorColor = Color.Transparent),
        modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
    )
}

fun targetIcon(kind: TargetKind): ImageVector = when (kind) {
    TargetKind.AUTO -> Icons.Rounded.AutoMode
    TargetKind.USB -> Icons.Rounded.Usb
    TargetKind.PHONE -> Icons.Rounded.PhoneAndroid
    TargetKind.BLUETOOTH -> Icons.Rounded.Bluetooth
}

@Composable
fun targetLabel(target: String?, phone: String?, info: TargetInfo): String = when (targetKind(target, phone)) {
    TargetKind.AUTO -> stringResource(R.string.target_auto)
    TargetKind.USB -> stringResource(R.string.target_usb)
    TargetKind.PHONE -> stringResource(R.string.target_phone)
    TargetKind.BLUETOOTH -> (info as? TargetInfo.Known)?.targets?.bonds?.firstOrNull { it.addr.equals(target, true) }?.name
        ?.ifBlank { null } ?: stringResource(R.string.target_bluetooth)
}

@Composable
private fun TargetCard(target: String?, phone: String?, info: TargetInfo, onClick: () -> Unit) {
    Card(
        onClick = onClick,
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.primaryContainer),
        modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 4.dp),
    ) {
        val on = MaterialTheme.colorScheme.onPrimaryContainer
        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(16.dp)) {
            Icon(targetIcon(targetKind(target, phone)), contentDescription = null, tint = on)
            Column(Modifier.weight(1f).padding(horizontal = 16.dp)) {
                Text(stringResource(R.string.types_into), style = MaterialTheme.typography.labelMedium, color = on)
                Text(targetLabel(target, phone, info), style = MaterialTheme.typography.titleMedium, color = on)
            }
            Icon(Icons.Rounded.SwapHoriz, contentDescription = stringResource(R.string.targets_title), tint = on)
        }
    }
}

@Composable
private fun OfflineCard(message: String, retry: () -> Unit) {
    Card(
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.errorContainer),
        modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp),
    ) {
        val on = MaterialTheme.colorScheme.onErrorContainer
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(Icons.Rounded.CloudOff, contentDescription = null, tint = on)
                Spacer(Modifier.size(12.dp))
                Text(stringResource(R.string.home_offline), style = MaterialTheme.typography.titleSmall, color = on)
            }
            Text(message, style = MaterialTheme.typography.bodyMedium, color = on)
            FilledTonalButton(onClick = retry) { Text(stringResource(R.string.retry)) }
        }
    }
}

@Composable
private fun AccountRow(e: Entry, onClick: () -> Unit) {
    val description = stringResource(R.string.account_row, e.title, e.host)
    ListItem(
        headlineContent = { Text(e.title) },
        supportingContent = if (e.host.isNotEmpty()) ({ Host(e.host) }) else null,
        leadingContent = { Avatar(e.title) },
        modifier = Modifier.clickable(onClick = onClick).semantics(mergeDescendants = true) { contentDescription = description },
    )
}

/** Host names read left to right whatever the app's language. */
@Composable
private fun Host(host: String) {
    Text(host, style = MaterialTheme.typography.bodyMedium.copy(textDirection = TextDirection.Ltr), color = MaterialTheme.colorScheme.onSurfaceVariant)
}

private val AVATAR_COLORS = listOf(0xFF0B57F0, 0xFF0F7B6C, 0xFF8E44AD, 0xFFC0392B, 0xFFB45309, 0xFF2C3E50, 0xFF15803D, 0xFF7F5539)

@Composable
private fun Avatar(title: String) {
    val letter = title.trim().firstOrNull()?.uppercase() ?: "?"
    val color = Color(AVATAR_COLORS[(title.lowercase().hashCode() and 0x7fffffff) % AVATAR_COLORS.size])
    Box(Modifier.size(40.dp).clip(CircleShape).background(color), contentAlignment = Alignment.Center) {
        Text(letter, color = Color.White, style = MaterialTheme.typography.titleMedium)
    }
}

/** One account: what to type, where, then the press. Closing while Keyra waits withdraws the request. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun AccountSheet(e: Entry, target: String?, phone: String?, info: TargetInfo, changeTarget: () -> Unit, close: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val sheet = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    var press by remember { mutableStateOf<Press?>(null) }
    val api = remember { runCatching { Keyra.api(context) }.getOrNull() }

    fun withdraw() {
        val waiting = press as? Press.Waiting ?: return
        // Not tied to this sheet's scope: it must still go out after the sheet is gone.
        api?.let { Keyra.io.execute { runCatching { it.cancel(waiting.requestId) } } }
    }

    fun start(what: What) {
        val api = api ?: return
        scope.launch { runPress(context, api, { api.type(e.id, what, target) }) { press = it } }
    }

    val hint = when (targetKind(target, phone)) {
        TargetKind.AUTO -> stringResource(R.string.hint_auto)
        TargetKind.USB -> stringResource(R.string.hint_usb)
        TargetKind.PHONE -> stringResource(R.string.hint_phone)
        TargetKind.BLUETOOTH -> stringResource(R.string.hint_ble, targetLabel(target, phone, info))
    }

    ModalBottomSheet(onDismissRequest = { withdraw(); close() }, sheetState = sheet) {
        Column(Modifier.fillMaxWidth().padding(horizontal = 24.dp).navigationBarsPadding().padding(bottom = 16.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Avatar(e.title)
                Column(Modifier.padding(start = 16.dp)) {
                    Text(e.title, style = MaterialTheme.typography.titleLarge)
                    if (e.host.isNotEmpty()) Host(e.host)
                }
            }
            Spacer(Modifier.height(16.dp))
            val p = press
            if (p == null) {
                ListItem(
                    headlineContent = { Text(targetLabel(target, phone, info)) },
                    overlineContent = { Text(stringResource(R.string.types_into)) },
                    leadingContent = { Icon(targetIcon(targetKind(target, phone)), contentDescription = null) },
                    trailingContent = { Icon(Icons.Rounded.SwapHoriz, contentDescription = stringResource(R.string.targets_title)) },
                    modifier = Modifier.clip(MaterialTheme.shapes.large).clickable(onClick = changeTarget),
                )
                HorizontalDivider(Modifier.padding(vertical = 8.dp))
                Text(stringResource(R.string.account_open), style = MaterialTheme.typography.labelLarge, modifier = Modifier.padding(vertical = 8.dp))
                ActionButton(Icons.Rounded.Person, R.string.type_username) { start(What.USERNAME) }
                ActionButton(Icons.Rounded.Key, R.string.type_password) { start(What.PASSWORD) }
                ActionButton(Icons.AutoMirrored.Rounded.Login, R.string.type_both) { start(What.BOTH) }
                ActionButton(Icons.Rounded.Pin, R.string.type_totp) { start(What.TOTP) }
            } else {
                PressPanel(
                    state = p,
                    hint = hint,
                    onCancel = {
                        val w = p as? Press.Waiting
                        if (w != null && api != null) scope.launch { cancelPress(api, w.requestId) }
                    },
                    onClose = close,
                )
            }
        }
    }
}

@Composable
private fun ActionButton(icon: ImageVector, label: Int, onClick: () -> Unit) {
    OutlinedButton(
        onClick = onClick,
        contentPadding = PaddingValues(horizontal = 16.dp, vertical = 14.dp),
        modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp),
    ) {
        Icon(icon, contentDescription = null)
        Spacer(Modifier.size(12.dp))
        Text(stringResource(label), modifier = Modifier.weight(1f), textAlign = TextAlign.Start)
    }
}
