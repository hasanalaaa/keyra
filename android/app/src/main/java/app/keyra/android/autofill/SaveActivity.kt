package app.keyra.android.autofill

import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextDirection
import androidx.compose.ui.unit.dp
import app.keyra.android.Keyra
import app.keyra.android.R
import app.keyra.android.api.KeyraApi
import app.keyra.android.match.HostMatcher
import app.keyra.android.match.Target
import app.keyra.android.ui.KeyraTheme
import app.keyra.android.ui.Press
import app.keyra.android.ui.PressPanel
import app.keyra.android.ui.cancelPress
import app.keyra.android.ui.runPress
import kotlinx.coroutines.launch
import java.security.GeneralSecurityException

/**
 * "Save to Keyra?" after a sign-in or sign-up. The values live only in this screen's memory
 * (and the intent that opened it); they are sent to Keyra once and Keyra stores them only
 * after its button is pressed. Nothing is written on the phone.
 */
class SaveActivity : ComponentActivity() {
    private var username = ""
    private var password = ""

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_SECURE)
        // Rebuilt after the process died: the values are gone on purpose.
        if (savedInstanceState != null) return finish()
        username = intent.getStringExtra(EXTRA_USERNAME).orEmpty()
        password = intent.getStringExtra(EXTRA_PASSWORD).orEmpty()
        intent.removeExtra(EXTRA_PASSWORD)
        val url = intent.getStringExtra(EXTRA_URL).orEmpty()
        val initialTitle = intent.getStringExtra(EXTRA_TITLE).orEmpty()
        val length = password.length
        val apiResult: Result<KeyraApi?> = runCatching { Keyra.api(this) }
        val setupError = when {
            apiResult.exceptionOrNull() is GeneralSecurityException -> getString(R.string.err_token_unreadable)
            apiResult.getOrNull() == null -> getString(R.string.not_set_up)
            else -> null
        }
        val api = apiResult.getOrNull()

        setContent {
            KeyraTheme {
                val scope = rememberCoroutineScope()
                var title by remember { mutableStateOf(initialTitle) }
                var error by remember { mutableStateOf(setupError) }
                var press by remember { mutableStateOf<Press?>(null) }

                Surface {
                    Column(Modifier.verticalScroll(rememberScrollState()).padding(24.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                        Text(stringResource(R.string.save_title), style = MaterialTheme.typography.headlineSmall)
                        val p = press
                        if (p != null) {
                            PressPanel(
                                state = p,
                                hint = stringResource(R.string.press_body_save),
                                onCancel = {
                                    val w = p as? Press.Waiting
                                    if (w != null && api != null) scope.launch { cancelPress(api, w.requestId) }
                                },
                                onClose = { finish() },
                            )
                        } else {
                            Text(stringResource(R.string.save_body), style = MaterialTheme.typography.bodyMedium)
                            OutlinedTextField(
                                value = title,
                                onValueChange = { title = it },
                                label = { Text(stringResource(R.string.field_title)) },
                                singleLine = true,
                                modifier = Modifier.fillMaxWidth(),
                            )
                            Labeled(stringResource(R.string.field_url), url)
                            if (username.isNotEmpty()) Labeled(stringResource(R.string.field_username), username)
                            Text(pluralStringResource(R.plurals.field_password, length, length), style = MaterialTheme.typography.bodyMedium)
                            error?.let { Text(it, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodyMedium) }
                            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.fillMaxWidth()) {
                                TextButton(onClick = { finish() }, modifier = Modifier.weight(1f)) { Text(stringResource(R.string.cancel)) }
                                Button(
                                    enabled = api != null,
                                    modifier = Modifier.weight(1f),
                                    onClick = {
                                        val t = title.trim()
                                        val a = api
                                        if (t.isEmpty()) {
                                            error = getString(R.string.err_title_required)
                                        } else if (a != null) {
                                            val u = username
                                            val pw = password
                                            password = "" // sent once; Keyra holds it until the press or the timeout
                                            scope.launch { runPress(this@SaveActivity, a, { a.save(t, url, u, pw) }) { press = it } }
                                        }
                                    },
                                ) { Text(stringResource(R.string.save_button)) }
                            }
                        }
                    }
                }
            }
        }
    }

    override fun onDestroy() {
        password = ""
        username = ""
        super.onDestroy()
    }

    companion object {
        private const val EXTRA_TITLE = "title"
        private const val EXTRA_URL = "url"
        private const val EXTRA_USERNAME = "username"
        private const val EXTRA_PASSWORD = "password"

        fun intent(context: Context, target: Target, username: String, password: String): Intent {
            val web = target.webDomain?.let(HostMatcher::normalize)
            // androidapp://<package>: Keyra's host for it is the package, which matches the app later.
            val url = if (web != null) "https://$web" else "androidapp://${target.packageName}"
            return Intent(context, SaveActivity::class.java)
                .putExtra(EXTRA_TITLE, AutofillAuthActivity.label(context, target).removePrefix("www."))
                .putExtra(EXTRA_URL, url)
                .putExtra(EXTRA_USERNAME, username)
                .putExtra(EXTRA_PASSWORD, password)
        }
    }
}

@Composable
private fun Labeled(label: String, value: String) {
    Column {
        Text(label, style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(value, style = MaterialTheme.typography.bodyLarge.copy(textDirection = TextDirection.Ltr))
    }
}
