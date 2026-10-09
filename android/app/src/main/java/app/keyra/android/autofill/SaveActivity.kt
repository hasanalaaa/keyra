package app.keyra.android.autofill

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.view.View
import android.view.WindowManager
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import app.keyra.android.Keyra
import app.keyra.android.R
import app.keyra.android.api.AgentStatus
import app.keyra.android.api.KeyraApi
import app.keyra.android.match.HostMatcher
import app.keyra.android.match.Target
import app.keyra.android.ui.PressPanel
import app.keyra.android.ui.PressWaiter
import app.keyra.android.ui.statusText
import java.security.GeneralSecurityException

/**
 * "Save to Keyra?" after a sign-in or sign-up. The values live only in this screen's memory
 * (and the intent that opened it); they are sent to Keyra once and Keyra stores them only
 * after its button is pressed. Nothing is written on the phone.
 */
class SaveActivity : Activity(), PressWaiter.Listener {
    private lateinit var panel: PressPanel
    private var username = ""
    private var password = ""
    private var url = ""
    private var waiter: PressWaiter? = null
    private var api: KeyraApi? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_SECURE)
        setContentView(R.layout.activity_save)
        setTitle(R.string.save_title)
        panel = PressPanel(findViewById(R.id.root))
        panel.panel.visibility = View.GONE
        panel.button.setOnClickListener { finish() }
        // Rebuilt after the process died: the values are gone on purpose.
        if (savedInstanceState != null) return finish()

        username = intent.getStringExtra(EXTRA_USERNAME).orEmpty()
        password = intent.getStringExtra(EXTRA_PASSWORD).orEmpty()
        url = intent.getStringExtra(EXTRA_URL).orEmpty()
        intent.removeExtra(EXTRA_PASSWORD)
        findViewById<EditText>(R.id.title).setText(intent.getStringExtra(EXTRA_TITLE))
        findViewById<TextView>(R.id.url).text = url
        findViewById<TextView>(R.id.username).text = username
        findViewById<TextView>(R.id.password_info).text = resources.getQuantityString(R.plurals.field_password, password.length, password.length)
        findViewById<Button>(R.id.dismiss).setOnClickListener { finish() }
        findViewById<Button>(R.id.save).setOnClickListener { save() }
        api = try {
            Keyra.api(this)
        } catch (_: GeneralSecurityException) {
            return formError(getString(R.string.err_token_unreadable))
        } ?: return formError(getString(R.string.not_set_up))
    }

    override fun onDestroy() {
        waiter?.stop()
        password = ""
        username = ""
        super.onDestroy()
    }

    private fun save() {
        val api = api ?: return
        val title = findViewById<EditText>(R.id.title).text.toString().trim()
        if (title.isEmpty()) return formError(getString(R.string.err_title_required))
        findViewById<View>(R.id.form).visibility = View.GONE
        panel.panel.visibility = View.VISIBLE
        panel.arming()
        panel.button.setOnClickListener { cancel() }
        val u = username
        val p = password
        Keyra.async({ api.save(title, url, u, p) }) { r ->
            if (isDestroyed) return@async
            password = "" // sent; Keyra holds it until the press or the timeout
            r.onSuccess {
                panel.waiting(R.string.press_body_save)
                waiter = PressWaiter(api, this).also { w -> w.start(it) }
            }.onFailure { onFinished(null, it) }
        }
    }

    private fun cancel() {
        val api = api ?: return finish()
        Keyra.async({ api.cancel() }) { r ->
            if (isDestroyed) return@async
            r.onFailure { onFinished(null, it) }
        }
    }

    private fun formError(text: String) {
        findViewById<TextView>(R.id.form_error).apply {
            this.text = text
            visibility = View.VISIBLE
        }
    }

    override fun onTick(secondsLeft: Int) {
        panel.countdown.text = getString(R.string.seconds_left, secondsLeft)
    }

    override fun onStatus(status: AgentStatus) {
        panel.title.text = statusText(this, status)
    }

    override fun onFinished(status: AgentStatus?, error: Throwable?) {
        waiter?.stop()
        panel.done(status?.let { statusText(this, it) } ?: Keyra.message(this, error ?: IllegalStateException()))
        panel.button.setOnClickListener { finish() }
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
