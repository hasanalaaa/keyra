package app.keyra.android.autofill

import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.Search
import androidx.compose.material3.Button
import androidx.compose.material3.Checkbox
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.ListItem
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TextField
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextDirection
import androidx.compose.ui.unit.dp
import app.keyra.android.Keyra
import app.keyra.android.R
import app.keyra.android.api.Entry
import app.keyra.android.api.What
import app.keyra.android.match.Target
import app.keyra.android.store.appStore
import app.keyra.android.ui.Contour
import app.keyra.android.ui.KeyraTheme
import app.keyra.android.ui.MainActivity
import app.keyra.android.ui.RowDivider
import app.keyra.android.ui.groupShape
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.security.GeneralSecurityException

/**
 * Opened when an autofill suggestion is chosen. It never returns a value to the form: it asks
 * Keyra to arm typing into this phone and closes, so the field the user was in gets focus back
 * and Keyra types into it as a Bluetooth keyboard after the button press. In "pick" mode the
 * user first chooses the login. Until this phone is chosen in the app nothing is armed: Keyra's
 * own choice would usually be a computer on USB.
 */
class AutofillAuthActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setResult(RESULT_CANCELED) // nothing is ever filled by the system
        if (savedInstanceState != null) return finish()
        val store = appStore(this)
        val phone = store.phone
        val apiResult = runCatching { Keyra.api(this) }
        val api = apiResult.getOrNull()
        val problem = when {
            apiResult.exceptionOrNull() is GeneralSecurityException -> getString(R.string.err_token_unreadable)
            api == null -> getString(R.string.not_set_up)
            phone == null -> getString(R.string.af_no_phone)
            else -> null
        }
        val what = What.valueOf(requireNotNull(intent.getStringExtra(EXTRA_WHAT)))
        val key = intent.getStringExtra(EXTRA_KEY)
        val label = intent.getStringExtra(EXTRA_LABEL).orEmpty()
        val direct = if (key == null) Entry(intent.getLongExtra(EXTRA_ID, 0), intent.getStringExtra(EXTRA_TITLE).orEmpty(), "") else null

        setContent {
            KeyraTheme {
                var message by remember { mutableStateOf(problem) }
                var entries by remember { mutableStateOf<List<Entry>?>(null) }
                var arming by remember { mutableStateOf(false) }
                var query by remember { mutableStateOf("") }
                var rememberChoice by remember { mutableStateOf(false) }
                var chosen by remember { mutableStateOf(direct) }

                LaunchedEffect(chosen) {
                    val e = chosen ?: return@LaunchedEffect
                    if (problem != null || api == null || phone == null) return@LaunchedEffect
                    if (key != null && rememberChoice) store.pin(key, e.id, e.title)
                    arming = true
                    message = getString(R.string.arming)
                    try {
                        withContext(Dispatchers.IO) { api.type(e.id, what, phone) }
                        Toast.makeText(applicationContext, getString(R.string.af_armed, e.title), Toast.LENGTH_LONG).show()
                        finish()
                    } catch (err: Exception) {
                        message = Keyra.message(this@AutofillAuthActivity, err)
                        arming = false
                    }
                }
                LaunchedEffect(Unit) {
                    if (key == null || problem != null || api == null) return@LaunchedEffect
                    try {
                        entries = withContext(Dispatchers.IO) { api.entries() }.sortedBy { it.title.lowercase() }
                    } catch (err: Exception) {
                        message = Keyra.message(this@AutofillAuthActivity, err)
                    }
                }

                Surface(Modifier.fillMaxSize(), shape = MaterialTheme.shapes.extraLarge, color = MaterialTheme.colorScheme.background) {
                    Column(Modifier.padding(24.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Contour(Modifier.size(24.dp), bold = true)
                            Text(
                                stringResource(if (key != null) R.string.pick_title else R.string.app_name),
                                style = MaterialTheme.typography.headlineSmall,
                                modifier = Modifier.padding(start = 12.dp),
                            )
                        }
                        if (arming) CircularProgressIndicator(Modifier.align(Alignment.CenterHorizontally).size(28.dp), strokeWidth = 2.dp)
                        message?.let { Text(it, style = MaterialTheme.typography.bodyLarge) }
                        if (phone == null && api != null) {
                            Button(
                                onClick = {
                                    startActivity(Intent(this@AutofillAuthActivity, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
                                    finish()
                                },
                                modifier = Modifier.fillMaxWidth().height(52.dp),
                            ) { Text(stringResource(R.string.pair_phone)) }
                        }
                        val list = entries
                        if (key != null && list != null && !arming) {
                            TextField(
                                value = query,
                                onValueChange = { query = it },
                                placeholder = { Text(stringResource(R.string.search_hint)) },
                                leadingIcon = { Icon(Icons.Outlined.Search, contentDescription = null) },
                                singleLine = true,
                                shape = CircleShape,
                                colors = TextFieldDefaults.colors(
                                    focusedContainerColor = MaterialTheme.colorScheme.surfaceContainer,
                                    unfocusedContainerColor = MaterialTheme.colorScheme.surfaceContainer,
                                    focusedIndicatorColor = Color.Transparent,
                                    unfocusedIndicatorColor = Color.Transparent,
                                ),
                                modifier = Modifier.fillMaxWidth(),
                            )
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                modifier = Modifier.fillMaxWidth().toggleable(value = rememberChoice, role = Role.Checkbox) { rememberChoice = it },
                            ) {
                                Checkbox(checked = rememberChoice, onCheckedChange = null)
                                Text(stringResource(R.string.af_remember, label), style = MaterialTheme.typography.bodyMedium, modifier = Modifier.padding(start = 8.dp))
                            }
                            if (list.isEmpty()) Text(stringResource(R.string.empty_list), color = MaterialTheme.colorScheme.onSurfaceVariant)
                            val q = query.trim().lowercase()
                            val shown = list.filter { q.isEmpty() || q in it.title.lowercase() || q in it.host }
                            LazyColumn(Modifier.weight(1f)) {
                                itemsIndexed(shown, key = { _, e -> e.id }) { i, e ->
                                    Column(Modifier.clip(groupShape(i, shown.size)).background(MaterialTheme.colorScheme.surfaceContainerLowest)) {
                                        if (i > 0) RowDivider()
                                        ListItem(
                                            headlineContent = { Text(e.title, style = MaterialTheme.typography.bodyLarge.copy(fontWeight = FontWeight.Medium)) },
                                            supportingContent = {
                                                Text(
                                                    e.host,
                                                    style = MaterialTheme.typography.bodyMedium.copy(textDirection = TextDirection.Ltr),
                                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                                )
                                            },
                                            colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                                            modifier = Modifier.clickable { chosen = e },
                                        )
                                    }
                                }
                            }
                        }
                        TextButton(onClick = { finish() }, modifier = Modifier.align(Alignment.End)) { Text(stringResource(R.string.close)) }
                    }
                }
            }
        }
    }

    companion object {
        private const val EXTRA_ID = "id"
        private const val EXTRA_TITLE = "title"
        private const val EXTRA_WHAT = "what"
        private const val EXTRA_KEY = "key"
        private const val EXTRA_LABEL = "label"

        fun armIntent(context: Context, e: Entry, what: What): Intent = Intent(context, AutofillAuthActivity::class.java)
            .putExtra(EXTRA_ID, e.id)
            .putExtra(EXTRA_TITLE, e.title)
            .putExtra(EXTRA_WHAT, what.name)

        fun pickIntent(context: Context, target: Target, what: What): Intent = Intent(context, AutofillAuthActivity::class.java)
            .putExtra(EXTRA_KEY, target.key)
            .putExtra(EXTRA_LABEL, pickLabel(context, target))
            .putExtra(EXTRA_WHAT, what.name)

        /** For apps the package follows the name: any app can call itself "GitHub", not reuse its package. */
        private fun pickLabel(context: Context, target: Target): String = target.webDomain
            ?: label(context, target).let { if (it == target.packageName) it else "$it (${target.packageName})" }

        /** The site's domain, or the app's name when this app may see it, else its package. */
        fun label(context: Context, target: Target): String = target.webDomain ?: try {
            val pm = context.packageManager
            pm.getApplicationLabel(pm.getApplicationInfo(target.packageName, 0)).toString()
        } catch (_: PackageManager.NameNotFoundException) {
            target.packageName
        }
    }
}
