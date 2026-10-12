package app.keyra.android.ui

import android.content.Intent
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.togetherWith
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import app.keyra.android.store.appStore

/** The whole app in one activity: accounts, settings (and the connection), generator. */
class MainActivity : ComponentActivity() {
    /** A token shared to the app as text (e.g. copied from the camera's QR scanner). */
    private var shared by mutableStateOf<String?>(null)

    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)
        shared = sharedToken(intent)
        setContent { KeyraTheme { KeyraApp(shared) { shared = null } } }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        sharedToken(intent)?.let { shared = it }
    }

    private fun sharedToken(intent: Intent?): String? {
        if (intent?.action != Intent.ACTION_SEND) return null
        return TOKEN_IN_TEXT.find(intent.getStringExtra(Intent.EXTRA_TEXT) ?: return null)?.value
    }

    private companion object {
        val TOKEN_IN_TEXT = Regex("keyra_[a-z2-7]{32}")
    }
}

enum class Screen { HOME, SETTINGS, CONNECT, GENERATOR }

@Composable
fun KeyraApp(sharedToken: String?, consumeShared: () -> Unit) {
    val store = appStore(LocalContext.current)
    var screen by remember { mutableStateOf(if (store.configured) Screen.HOME else Screen.CONNECT) }
    // Bumped when the connection changes, so the accounts load again.
    var generation by remember { mutableIntStateOf(0) }
    LaunchedEffect(sharedToken) { if (sharedToken != null) screen = Screen.CONNECT }

    BackHandler(enabled = screen != Screen.HOME && store.configured) {
        consumeShared()
        screen = Screen.HOME
    }
    AnimatedContent(targetState = screen, transitionSpec = { fadeIn() togetherWith fadeOut() }, label = "screen") { s ->
        when (s) {
            Screen.HOME -> HomeScreen(
                generation = generation,
                openSettings = { screen = Screen.SETTINGS },
                openGenerator = { screen = Screen.GENERATOR },
            )
            Screen.SETTINGS -> SettingsScreen(
                back = { screen = Screen.HOME },
                changeConnection = { screen = Screen.CONNECT },
                disconnected = {
                    generation++
                    screen = Screen.CONNECT
                },
            )
            Screen.CONNECT -> ConnectScreen(
                sharedToken = sharedToken,
                back = if (store.configured) ({ consumeShared(); screen = Screen.HOME }) else null,
                saved = {
                    consumeShared()
                    generation++
                    screen = Screen.HOME
                },
            )
            Screen.GENERATOR -> GeneratorScreen(back = { screen = Screen.HOME })
        }
    }
}
