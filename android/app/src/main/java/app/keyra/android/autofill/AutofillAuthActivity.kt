package app.keyra.android.autofill

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Bundle
import android.text.Editable
import android.text.TextWatcher
import android.view.View
import android.widget.CheckBox
import android.widget.EditText
import android.widget.ListView
import android.widget.ProgressBar
import android.widget.TextView
import android.widget.Toast
import app.keyra.android.Keyra
import app.keyra.android.R
import app.keyra.android.api.Entry
import app.keyra.android.api.KeyraApi
import app.keyra.android.api.What
import app.keyra.android.match.Target
import app.keyra.android.store.appStore
import app.keyra.android.ui.EntryAdapter
import java.security.GeneralSecurityException

/**
 * Opened when an autofill suggestion is chosen. It never returns a value to the form: it asks
 * Keyra to arm typing and closes, so the field the user was in gets focus back and Keyra types
 * into it after the button press. In "pick" mode the user first chooses the login.
 */
class AutofillAuthActivity : Activity() {
    private lateinit var progress: ProgressBar
    private lateinit var message: TextView
    private var api: KeyraApi? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_autofill)
        setResult(RESULT_CANCELED) // nothing is ever filled by the system
        progress = findViewById(R.id.progress)
        message = findViewById(R.id.message)
        findViewById<View>(R.id.close).setOnClickListener { finish() }
        if (savedInstanceState != null) return finish()

        api = try {
            Keyra.api(this)
        } catch (_: GeneralSecurityException) {
            return fail(getString(R.string.err_token_unreadable))
        } ?: return fail(getString(R.string.not_set_up))

        val what = What.valueOf(requireNotNull(intent.getStringExtra(EXTRA_WHAT)))
        val key = intent.getStringExtra(EXTRA_KEY)
        if (key == null) {
            setTitle(R.string.app_name)
            arm(intent.getLongExtra(EXTRA_ID, 0), requireNotNull(intent.getStringExtra(EXTRA_TITLE)), what)
        } else {
            setTitle(R.string.pick_title)
            pick(key, requireNotNull(intent.getStringExtra(EXTRA_LABEL)), what)
        }
    }

    private fun arm(id: Long, title: String, what: What) {
        val api = api ?: return
        progress.visibility = View.VISIBLE
        message.setText(R.string.arming)
        Keyra.async({ api.type(id, what) }) { r ->
            if (isDestroyed) return@async
            r.onSuccess {
                Toast.makeText(applicationContext, getString(R.string.af_armed, title), Toast.LENGTH_LONG).show()
                finish()
            }.onFailure { fail(Keyra.message(this, it)) }
        }
    }

    private fun pick(key: String, label: String, what: What) {
        val api = api ?: return
        val adapter = EntryAdapter(this)
        val remember = findViewById<CheckBox>(R.id.remember)
        remember.text = getString(R.string.af_remember, label)
        val list = findViewById<ListView>(R.id.list)
        list.adapter = adapter
        list.setOnItemClickListener { _, _, position, _ ->
            val e: Entry = adapter.getItem(position)
            if (remember.isChecked) appStore(this).pin(key, e.id, e.title)
            findViewById<View>(R.id.pick_panel).visibility = View.GONE
            arm(e.id, e.title, what)
        }
        findViewById<EditText>(R.id.search).addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) = Unit
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) = Unit
            override fun afterTextChanged(s: Editable?) = adapter.filter(s?.toString().orEmpty())
        })
        progress.visibility = View.VISIBLE
        message.setText(R.string.loading)
        Keyra.async({ api.entries() }) { r ->
            if (isDestroyed) return@async
            r.onSuccess {
                adapter.set(it)
                progress.visibility = View.GONE
                message.text = if (it.isEmpty()) getString(R.string.empty_list) else ""
                findViewById<View>(R.id.pick_panel).visibility = View.VISIBLE
            }.onFailure { fail(Keyra.message(this, it)) }
        }
    }

    private fun fail(text: String) {
        progress.visibility = View.GONE
        message.text = text
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
