package app.keyra.android.ui

import android.app.Activity
import android.content.ClipData
import android.content.ClipDescription
import android.content.ClipboardManager
import android.content.Context
import android.os.Build
import android.os.PersistableBundle
import android.view.WindowManager
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ArrowBack
import androidx.compose.material.icons.rounded.ContentCopy
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextDirection
import androidx.compose.ui.unit.dp
import app.keyra.android.Keyra
import app.keyra.android.R
import app.keyra.android.api.GenerateOptions
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlin.math.roundToInt

private const val MIN_LENGTH = 8
private const val MAX_LENGTH = 64

/** Keyra's hardware generator (/api/agent/generate). The password lives in this screen only. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun GeneratorScreen(back: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var length by remember { mutableFloatStateOf(20f) }
    var lower by remember { mutableStateOf(true) }
    var upper by remember { mutableStateOf(true) }
    var digits by remember { mutableStateOf(true) }
    var symbols by remember { mutableStateOf(true) }
    var avoid by remember { mutableStateOf(false) }
    var password by remember { mutableStateOf("") }
    var message by remember { mutableStateOf<String?>(null) }
    var busy by remember { mutableStateOf(false) }

    // Keeps the password out of screenshots and the recent-apps thumbnail while shown.
    DisposableEffect(Unit) {
        val window = (context as? Activity)?.window
        window?.addFlags(WindowManager.LayoutParams.FLAG_SECURE)
        onDispose { window?.clearFlags(WindowManager.LayoutParams.FLAG_SECURE) }
    }

    fun generate() {
        val api = runCatching { Keyra.api(context) }.getOrNull()
        if (api == null) {
            message = context.getString(R.string.not_set_up)
            return
        }
        val options = GenerateOptions(length.roundToInt(), lower, upper, digits, symbols, avoid)
        scope.launch {
            busy = true
            message = null
            try {
                password = withContext(Dispatchers.IO) { api.generate(options) }
            } catch (e: Exception) {
                message = Keyra.message(context, e)
            }
            busy = false
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(stringResource(R.string.gen_title)) },
                navigationIcon = { IconButton(onClick = back) { Icon(Icons.AutoMirrored.Rounded.ArrowBack, stringResource(R.string.back)) } },
            )
        },
    ) { padding ->
        Column(
            Modifier.padding(padding).fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    Text(
                        password.ifEmpty { "••••••••••••" },
                        style = MaterialTheme.typography.headlineSmall.copy(fontFamily = FontFamily.Monospace, textDirection = TextDirection.Ltr),
                        color = if (password.isEmpty()) MaterialTheme.colorScheme.outline else MaterialTheme.colorScheme.onSurface,
                    )
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Button(onClick = { generate() }, enabled = !busy) {
                            Icon(Icons.Rounded.Refresh, contentDescription = null)
                            Text(stringResource(R.string.gen_button), modifier = Modifier.padding(start = 8.dp))
                        }
                        FilledTonalButton(
                            onClick = {
                                ClipboardGuard.copySensitive(context, context.getString(R.string.clip_label), password)
                                message = context.getString(R.string.copied)
                            },
                            enabled = password.isNotEmpty(),
                        ) {
                            Icon(Icons.Rounded.ContentCopy, contentDescription = null)
                            Text(stringResource(R.string.copy), modifier = Modifier.padding(start = 8.dp))
                        }
                    }
                    message?.let { Text(it, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant) }
                }
            }
            Text(stringResource(R.string.gen_length, length.roundToInt()), style = MaterialTheme.typography.titleSmall)
            val sliderName = stringResource(R.string.gen_length_slider)
            Slider(
                value = length,
                onValueChange = { length = it },
                valueRange = MIN_LENGTH.toFloat()..MAX_LENGTH.toFloat(),
                steps = MAX_LENGTH - MIN_LENGTH - 1,
                modifier = Modifier.semantics { contentDescription = sliderName },
            )
            Toggle(R.string.gen_lower, lower) { lower = it }
            Toggle(R.string.gen_upper, upper) { upper = it }
            Toggle(R.string.gen_digits, digits) { digits = it }
            Toggle(R.string.gen_symbols, symbols) { symbols = it }
            Toggle(R.string.gen_avoid, avoid) { avoid = it }
            Text(stringResource(R.string.gen_note), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}

@Composable
private fun Toggle(label: Int, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(
        verticalAlignment = Alignment.CenterVertically,
        modifier = Modifier.fillMaxWidth().toggleable(value = checked, role = Role.Switch, onValueChange = onChange).padding(vertical = 8.dp),
    ) {
        Text(stringResource(label), modifier = Modifier.weight(1f))
        Switch(checked = checked, onCheckedChange = null)
    }
}

/**
 * Copies a value marked sensitive (hidden from the clipboard preview and keyboard
 * suggestions) and clears it after 60 s unless something else was copied meanwhile.
 * Best effort: if Android ends the process first, the clear does not happen; Android
 * itself clears the clipboard after about an hour.
 */
object ClipboardGuard {
    private const val CLEAR_MS = 60_000L
    private var pending: Runnable? = null

    fun copySensitive(context: Context, label: String, text: String) {
        val app = context.applicationContext
        val cm = app.getSystemService(ClipboardManager::class.java)
        val clip = ClipData.newPlainText(label, text)
        clip.description.extras = PersistableBundle().apply {
            val key = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) ClipDescription.EXTRA_IS_SENSITIVE else "android.content.extra.IS_SENSITIVE"
            putBoolean(key, true)
        }
        cm.setPrimaryClip(clip)
        val stamp = cm.primaryClipDescription?.timestamp
        pending?.let(Keyra.main::removeCallbacks)
        val clear = Runnable {
            pending = null
            // Only clear our own copy: the timestamp changes when anything else is copied.
            // In the background Android hides the clipboard (null); then clear, like other
            // password managers do, rather than leave the password there.
            val now = cm.primaryClipDescription
            if (now == null || now.timestamp == stamp) cm.clearPrimaryClip()
        }
        pending = clear
        Keyra.main.postDelayed(clear, CLEAR_MS)
    }
}
