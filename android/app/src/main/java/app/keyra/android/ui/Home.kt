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
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.KeyboardArrowRight
import androidx.compose.material.icons.automirrored.outlined.Login
import androidx.compose.material.icons.outlined.AutoMode
import androidx.compose.material.icons.outlined.Bluetooth
import androidx.compose.material.icons.outlined.Close
import androidx.compose.material.icons.outlined.CloudOff
import androidx.compose.material.icons.outlined.Key
import androidx.compose.material.icons.outlined.Password
import androidx.compose.material.icons.outlined.Person
import androidx.compose.material.icons.outlined.PhoneAndroid
import androidx.compose.material.icons.outlined.Pin
import androidx.compose.material.icons.outlined.Search
import androidx.compose.material.icons.outlined.Settings
import androidx.compose.material.icons.outlined.Usb
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
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
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextDirection
import androidx.compose.ui.unit.Dp
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
                title = {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Contour(Modifier.size(24.dp), bold = true)
                        Spacer(Modifier.width(10.dp))
                        Text(stringResource(R.string.app_name), style = MaterialTheme.typography.titleLarge)
                    }
                },
                actions = {
                    IconButton(onClick = openGenerator) { Icon(Icons.Outlined.Password, stringResource(R.string.menu_generate)) }
                    IconButton(onClick = openSettings) { Icon(Icons.Outlined.Settings, stringResource(R.string.menu_settings)) }
                },
            )
        },
    ) { padding ->
        PullToRefreshBox(isRefreshing = refreshing, onRefresh = { load() }, modifier = Modifier.padding(padding).fillMaxSize()) {
            LazyColumn(contentPadding = PaddingValues(start = 16.dp, end = 16.dp, top = 4.dp, bottom = 32.dp), modifier = Modifier.fillMaxSize()) {
                item { TargetCard(target, phone, info) { pickTarget = true } }
                error?.let { msg -> item { OfflineCard(msg) { load() } } }
                item { SearchField(query) { query = it } }
                item { SectionLabel(stringResource(R.string.home_title), Modifier.padding(top = 8.dp)) }
                val list = entries
                when {
                    list == null && error == null -> item {
                        Box(Modifier.fillMaxWidth().padding(48.dp), contentAlignment = Alignment.Center) {
                            CircularProgressIndicator(Modifier.size(24.dp), strokeWidth = 2.dp)
                        }
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
                itemsIndexed(shown, key = { _, e -> e.id }) { i, e ->
                    Column(Modifier.clip(groupShape(i, shown.size)).background(MaterialTheme.colorScheme.surfaceContainerLowest)) {
                        if (i > 0) RowDivider(inset = 72.dp)
                        AccountRow(e) { chosen = e }
                    }
                }
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
        leadingIcon = { Icon(Icons.Outlined.Search, contentDescription = null) },
        trailingIcon = {
            if (query.isNotEmpty()) IconButton(onClick = { onChange("") }) { Icon(Icons.Outlined.Close, stringResource(R.string.search_clear)) }
        },
        singleLine = true,
        shape = CircleShape,
        colors = TextFieldDefaults.colors(
            focusedContainerColor = MaterialTheme.colorScheme.surfaceContainer,
            unfocusedContainerColor = MaterialTheme.colorScheme.surfaceContainer,
            focusedIndicatorColor = Color.Transparent,
            unfocusedIndicatorColor = Color.Transparent,
        ),
        modifier = Modifier.fillMaxWidth().padding(vertical = 12.dp),
    )
}

fun targetIcon(kind: TargetKind): ImageVector = when (kind) {
    TargetKind.AUTO -> Icons.Outlined.AutoMode
    TargetKind.USB -> Icons.Outlined.Usb
    TargetKind.PHONE -> Icons.Outlined.PhoneAndroid
    TargetKind.BLUETOOTH -> Icons.Outlined.Bluetooth
}

@Composable
fun targetLabel(target: String?, phone: String?, info: TargetInfo): String = when (targetKind(target, phone)) {
    TargetKind.AUTO -> stringResource(R.string.target_auto)
    TargetKind.USB -> stringResource(R.string.target_usb)
    TargetKind.PHONE -> stringResource(R.string.target_phone)
    TargetKind.BLUETOOTH -> (info as? TargetInfo.Known)?.targets?.bonds?.firstOrNull { it.addr.equals(target, true) }?.name
        ?.ifBlank { null } ?: stringResource(R.string.target_bluetooth)
}

/** Where Keyra types: the first thing on Home, since every action depends on it. */
@Composable
private fun TargetCard(target: String?, phone: String?, info: TargetInfo, onClick: () -> Unit) {
    Group(Modifier.padding(top = 8.dp).clickable(onClickLabel = stringResource(R.string.targets_title), onClick = onClick)) {
        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(horizontal = 16.dp, vertical = 18.dp)) {
            IconTile(targetIcon(targetKind(target, phone)), size = 48.dp)
            Column(Modifier.weight(1f).padding(horizontal = 16.dp)) {
                Text(stringResource(R.string.types_into), style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                Text(targetLabel(target, phone, info), style = MaterialTheme.typography.titleLarge)
            }
            ChangePill()
        }
        if (phone == null) {
            RowDivider()
            Text(
                stringResource(R.string.target_card_phone_hint),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp),
            )
        }
    }
}

/** "Change", shaped like a small neutral button; the whole row is the touch target. */
@Composable
fun ChangePill() {
    Text(
        stringResource(R.string.change),
        style = MaterialTheme.typography.labelLarge,
        modifier = Modifier.clip(CircleShape).background(MaterialTheme.colorScheme.secondaryContainer).padding(horizontal = 14.dp, vertical = 8.dp),
    )
}

/** An outlined icon on a neutral rounded tile. */
@Composable
fun IconTile(icon: ImageVector, size: Dp = 40.dp) {
    Box(
        Modifier.size(size).clip(MaterialTheme.shapes.small).background(MaterialTheme.colorScheme.secondaryContainer),
        contentAlignment = Alignment.Center,
    ) { Icon(icon, contentDescription = null, modifier = Modifier.size(22.dp)) }
}

@Composable
private fun OfflineCard(message: String, retry: () -> Unit) {
    Group(Modifier.padding(top = 12.dp)) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(Icons.Outlined.CloudOff, contentDescription = null, tint = MaterialTheme.colorScheme.error, modifier = Modifier.size(20.dp))
                Spacer(Modifier.width(10.dp))
                Text(stringResource(R.string.home_offline), style = MaterialTheme.typography.titleSmall, color = MaterialTheme.colorScheme.error)
            }
            Text(message, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            FilledTonalButton(onClick = retry, modifier = Modifier.padding(top = 6.dp)) { Text(stringResource(R.string.retry)) }
        }
    }
}

@Composable
private fun AccountRow(e: Entry, onClick: () -> Unit) {
    val description = stringResource(R.string.account_row, e.title, e.host)
    ListItem(
        headlineContent = { Text(e.title, style = MaterialTheme.typography.bodyLarge.copy(fontWeight = FontWeight.Medium)) },
        supportingContent = if (e.host.isNotEmpty()) ({ Host(e.host) }) else null,
        leadingContent = { Avatar(e.title) },
        trailingContent = {
            Icon(Icons.AutoMirrored.Outlined.KeyboardArrowRight, contentDescription = null, tint = MaterialTheme.colorScheme.outline)
        },
        colors = ListItemDefaults.colors(containerColor = Color.Transparent),
        modifier = Modifier.clickable(onClick = onClick).semantics(mergeDescendants = true) { contentDescription = description },
    )
}

/** Host names read left to right whatever the app's language. */
@Composable
private fun Host(host: String) {
    Text(host, style = MaterialTheme.typography.bodyMedium.copy(textDirection = TextDirection.Ltr), color = MaterialTheme.colorScheme.onSurfaceVariant)
}

/** The login's first letter on a neutral tile: monochrome, so colour keeps its one meaning. */
@Composable
private fun Avatar(title: String) {
    val letter = title.trim().firstOrNull()?.uppercase() ?: "?"
    Box(
        Modifier.size(40.dp).clip(MaterialTheme.shapes.small).background(MaterialTheme.colorScheme.secondaryContainer),
        contentAlignment = Alignment.Center,
    ) {
        Text(letter, style = MaterialTheme.typography.titleMedium)
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

    ModalBottomSheet(
        onDismissRequest = { withdraw(); close() },
        sheetState = sheet,
        containerColor = MaterialTheme.colorScheme.background,
    ) {
        Column(Modifier.fillMaxWidth().padding(horizontal = 16.dp).navigationBarsPadding().padding(bottom = 16.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(horizontal = 4.dp)) {
                Avatar(e.title)
                Column(Modifier.padding(start = 16.dp)) {
                    Text(e.title, style = MaterialTheme.typography.titleLarge)
                    if (e.host.isNotEmpty()) Host(e.host)
                }
            }
            Spacer(Modifier.height(20.dp))
            val p = press
            if (p == null) {
                Group(Modifier.clickable(onClickLabel = stringResource(R.string.targets_title), onClick = changeTarget)) {
                    Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(16.dp)) {
                        IconTile(targetIcon(targetKind(target, phone)))
                        Column(Modifier.weight(1f).padding(horizontal = 16.dp)) {
                            Text(stringResource(R.string.types_into), style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                            Text(targetLabel(target, phone, info), style = MaterialTheme.typography.titleMedium)
                        }
                        ChangePill()
                    }
                }
                Spacer(Modifier.height(20.dp))
                SectionLabel(stringResource(R.string.account_open))
                Button(
                    onClick = { start(What.BOTH) },
                    contentPadding = PaddingValues(horizontal = 20.dp),
                    modifier = Modifier.fillMaxWidth().height(56.dp),
                ) {
                    Icon(Icons.AutoMirrored.Outlined.Login, contentDescription = null)
                    Spacer(Modifier.width(12.dp))
                    Text(stringResource(R.string.type_both), modifier = Modifier.weight(1f), textAlign = TextAlign.Start)
                }
                Spacer(Modifier.height(12.dp))
                Group {
                    ActionRow(Icons.Outlined.Person, R.string.type_username) { start(What.USERNAME) }
                    RowDivider(inset = 56.dp)
                    ActionRow(Icons.Outlined.Key, R.string.type_password) { start(What.PASSWORD) }
                    RowDivider(inset = 56.dp)
                    ActionRow(Icons.Outlined.Pin, R.string.type_totp) { start(What.TOTP) }
                }
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
private fun ActionRow(icon: ImageVector, label: Int, onClick: () -> Unit) {
    Row(
        verticalAlignment = Alignment.CenterVertically,
        modifier = Modifier.fillMaxWidth().clickable(role = Role.Button, onClick = onClick).padding(horizontal = 16.dp, vertical = 16.dp),
    ) {
        Icon(icon, contentDescription = null, tint = MaterialTheme.colorScheme.onSurfaceVariant)
        Spacer(Modifier.width(16.dp))
        Text(stringResource(label), style = MaterialTheme.typography.bodyLarge, modifier = Modifier.weight(1f))
    }
}
