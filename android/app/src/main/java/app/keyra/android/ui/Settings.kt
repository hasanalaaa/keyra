package app.keyra.android.ui

import android.content.ClipboardManager
import android.content.Intent
import android.net.Uri
import android.provider.Settings
import android.view.autofill.AutofillManager
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.ArrowBack
import androidx.compose.material.icons.automirrored.outlined.KeyboardArrowRight
import androidx.compose.material.icons.outlined.CheckCircle
import androidx.compose.material.icons.outlined.Close
import androidx.compose.material.icons.outlined.ContentPaste
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.style.TextDirection
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.LifecycleResumeEffect
import app.keyra.android.BuildConfig
import app.keyra.android.Keyra
import app.keyra.android.R
import app.keyra.android.api.KeyraAddress
import app.keyra.android.api.KeyraApi
import app.keyra.android.store.appStore
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.security.GeneralSecurityException

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(back: () -> Unit, changeConnection: () -> Unit, disconnected: () -> Unit) {
    val context = LocalContext.current
    val store = remember { appStore(context) }
    var pins by remember { mutableStateOf(store.pins()) }
    var autofill by remember { mutableStateOf(false) }
    var confirm by remember { mutableStateOf(false) }
    // This phone's name on Keyra (its address until Keyra answers).
    var phoneName by remember { mutableStateOf(store.phone) }
    LaunchedEffect(Unit) {
        val phone = store.phone ?: return@LaunchedEffect
        val api = runCatching { Keyra.api(context) }.getOrNull() ?: return@LaunchedEffect
        runCatching { withContext(Dispatchers.IO) { api.targets() } }.getOrNull()
            ?.bonds?.firstOrNull { it.addr.equals(phone, ignoreCase = true) }?.name?.ifBlank { null }?.let { phoneName = it }
    }
    LifecycleResumeEffect(Unit) {
        autofill = context.getSystemService(AutofillManager::class.java)?.hasEnabledAutofillServices() == true
        onPauseOrDispose {}
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(stringResource(R.string.settings_title), style = MaterialTheme.typography.titleLarge) },
                navigationIcon = { IconButton(onClick = back) { Icon(Icons.AutoMirrored.Outlined.ArrowBack, stringResource(R.string.back)) } },
            )
        },
    ) { padding ->
        Column(
            Modifier.padding(padding).fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 16.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(24.dp),
        ) {
            Section(stringResource(R.string.settings_connection)) {
                ListItem(
                    headlineContent = {
                        Text(
                            store.address.orEmpty(),
                            style = MaterialTheme.typography.bodyLarge.copy(fontFamily = FontFamily.Monospace, textDirection = TextDirection.Ltr),
                        )
                    },
                    overlineContent = { Text(stringResource(R.string.address_label)) },
                    colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                )
                RowDivider()
                LinkRow(stringResource(R.string.settings_change_connection), onClick = changeConnection)
            }
            Section(stringResource(R.string.settings_phone)) {
                Text(
                    phoneName?.let { stringResource(R.string.settings_phone_set, it) } ?: stringResource(R.string.settings_phone_none),
                    style = MaterialTheme.typography.bodyMedium,
                    modifier = Modifier.padding(16.dp),
                )
            }
            Section(stringResource(R.string.settings_autofill)) {
                if (autofill) {
                    Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(16.dp)) {
                        Icon(Icons.Outlined.CheckCircle, contentDescription = null, tint = okColor, modifier = Modifier.size(20.dp))
                        Text(stringResource(R.string.autofill_on), style = MaterialTheme.typography.bodyMedium, modifier = Modifier.padding(start = 12.dp))
                    }
                } else {
                    Button(
                        onClick = { context.startActivity(Intent(Settings.ACTION_REQUEST_SET_AUTOFILL_SERVICE, Uri.parse("package:${context.packageName}"))) },
                        modifier = Modifier.fillMaxWidth().padding(16.dp).height(52.dp),
                    ) { Text(stringResource(R.string.enable_autofill)) }
                }
            }
            Section(stringResource(R.string.pins_title)) {
                if (pins.isEmpty()) {
                    Text(
                        stringResource(R.string.pins_none),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.padding(16.dp),
                    )
                }
                pins.forEachIndexed { i, p ->
                    if (i > 0) RowDivider()
                    val row = stringResource(R.string.pin_row, p.key.substringAfter(':'), p.title)
                    ListItem(
                        headlineContent = { Text(row) },
                        colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                        trailingContent = {
                            IconButton(onClick = {
                                store.unpin(p.key)
                                pins = store.pins()
                            }) { Icon(Icons.Outlined.Close, stringResource(R.string.pin_remove_row, row)) }
                        },
                    )
                }
            }
            Section(stringResource(R.string.settings_privacy)) {
                Text(stringResource(R.string.privacy_note), style = MaterialTheme.typography.bodyMedium, modifier = Modifier.padding(16.dp))
            }
            Group {
                Text(
                    stringResource(R.string.disconnect),
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.error,
                    modifier = Modifier.fillMaxWidth().clickable(role = Role.Button) { confirm = true }.padding(16.dp),
                )
            }
            Text(
                stringResource(R.string.settings_version, BuildConfig.VERSION_NAME),
                style = MaterialTheme.typography.bodySmall.copy(fontFamily = FontFamily.Monospace),
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.align(Alignment.CenterHorizontally).padding(bottom = 16.dp),
            )
        }
    }

    if (confirm) {
        AlertDialog(
            onDismissRequest = { confirm = false },
            text = { Text(stringResource(R.string.disconnect)) },
            confirmButton = {
                TextButton(onClick = {
                    store.clear()
                    confirm = false
                    disconnected()
                }) { Text(stringResource(R.string.disconnect), color = MaterialTheme.colorScheme.error) }
            },
            dismissButton = { TextButton(onClick = { confirm = false }) { Text(stringResource(R.string.cancel)) } },
        )
    }
}

@Composable
private fun Section(title: String, content: @Composable ColumnScope.() -> Unit) {
    Column {
        SectionLabel(title)
        Group(content = content)
    }
}

@Composable
private fun LinkRow(text: String, onClick: () -> Unit) {
    Row(
        verticalAlignment = Alignment.CenterVertically,
        modifier = Modifier.fillMaxWidth().clickable(role = Role.Button, onClick = onClick).padding(16.dp),
    ) {
        Text(text, style = MaterialTheme.typography.bodyLarge, modifier = Modifier.weight(1f))
        Icon(Icons.AutoMirrored.Outlined.KeyboardArrowRight, contentDescription = null, tint = MaterialTheme.colorScheme.outline)
    }
}

/** First-time setup and later changes: Keyra's address and an access token of the kind "App". */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ConnectScreen(sharedToken: String?, back: (() -> Unit)?, saved: () -> Unit) {
    val context = LocalContext.current
    val store = remember { appStore(context) }
    val scope = rememberCoroutineScope()
    var address by remember { mutableStateOf(store.address ?: KeyraAddress.DEFAULT) }
    var token by remember { mutableStateOf("") }
    var result by remember { mutableStateOf<Pair<Boolean, String>?>(null) } // ok, text
    var busy by remember { mutableStateOf(false) }
    LaunchedEffect(sharedToken) { sharedToken?.let { token = it } }

    /** The validated (address, token), or null after showing what is wrong. */
    fun input(): Pair<String, String>? {
        val addr = KeyraAddress.normalize(address)
        if (addr == null) {
            result = false to context.getString(R.string.err_address)
            return null
        }
        val typed = token.trim()
        val tok = if (typed.isEmpty() && store.configured) {
            try {
                store.token
            } catch (_: GeneralSecurityException) {
                result = false to context.getString(R.string.err_token_unreadable)
                return null
            }
        } else typed
        if (tok == null || !KeyraApi.TOKEN_RE.matches(tok)) {
            result = false to context.getString(R.string.err_token_format)
            return null
        }
        return addr to tok
    }

    fun test() {
        val (addr, tok) = input() ?: return
        scope.launch {
            busy = true
            result = true to context.getString(R.string.testing)
            result = try {
                val n = withContext(Dispatchers.IO) { Keyra.api(context, addr, tok).entries().size }
                true to context.resources.getQuantityString(R.plurals.connection_ok, n, n)
            } catch (e: Exception) {
                false to Keyra.message(context, e)
            }
            busy = false
        }
    }

    // Saved even when Keyra cannot be reached now (locked, other network): the accounts screen says why.
    fun save() {
        val (addr, tok) = input() ?: return
        store.address = addr
        store.token = tok
        saved()
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {},
                navigationIcon = {
                    if (back != null) IconButton(onClick = back) { Icon(Icons.AutoMirrored.Outlined.ArrowBack, stringResource(R.string.back)) }
                },
            )
        },
    ) { padding ->
        Column(
            Modifier.padding(padding).fillMaxSize().imePadding().verticalScroll(rememberScrollState()).padding(horizontal = 24.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            Contour(Modifier.size(56.dp), bold = true)
            Text(
                stringResource(if (store.configured) R.string.setup_title else R.string.setup_welcome),
                style = MaterialTheme.typography.headlineMedium,
                modifier = Modifier.semantics { heading() },
            )
            Text(stringResource(R.string.setup_intro), style = MaterialTheme.typography.bodyLarge, color = MaterialTheme.colorScheme.onSurfaceVariant)
            Spacer(Modifier.height(8.dp))
            OutlinedTextField(
                value = address,
                onValueChange = { address = it },
                label = { Text(stringResource(R.string.address_label)) },
                singleLine = true,
                shape = MaterialTheme.shapes.medium,
                textStyle = MaterialTheme.typography.bodyLarge.copy(fontFamily = FontFamily.Monospace, textDirection = TextDirection.Ltr),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri),
                modifier = Modifier.fillMaxWidth(),
            )
            OutlinedTextField(
                value = token,
                onValueChange = { token = it },
                label = { Text(stringResource(R.string.token_label)) },
                placeholder = { Text(stringResource(R.string.token_hint)) },
                supportingText = if (store.configured) ({ Text(stringResource(R.string.token_kept)) }) else null,
                singleLine = true,
                shape = MaterialTheme.shapes.medium,
                visualTransformation = PasswordVisualTransformation(),
                textStyle = MaterialTheme.typography.bodyLarge.copy(fontFamily = FontFamily.Monospace, textDirection = TextDirection.Ltr),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
                trailingIcon = {
                    IconButton(onClick = {
                        val clip = context.getSystemService(ClipboardManager::class.java).primaryClip
                        clip?.takeIf { it.itemCount > 0 }?.getItemAt(0)?.coerceToText(context)?.toString()?.let { token = it.trim() }
                    }) { Icon(Icons.Outlined.ContentPaste, stringResource(R.string.paste)) }
                },
                modifier = Modifier.fillMaxWidth(),
            )
            Text(stringResource(R.string.token_help), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            result?.let { (ok, text) ->
                Text(text, color = if (ok) okColor else MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodyMedium)
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.fillMaxWidth().padding(top = 8.dp)) {
                FilledTonalButton(onClick = { test() }, enabled = !busy, modifier = Modifier.weight(1f).height(52.dp)) { Text(stringResource(R.string.test_connection)) }
                Button(onClick = { save() }, enabled = !busy, modifier = Modifier.weight(1f).height(52.dp)) { Text(stringResource(R.string.save)) }
            }
        }
    }
}
