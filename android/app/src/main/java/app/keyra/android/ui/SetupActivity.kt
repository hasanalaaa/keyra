package app.keyra.android.ui

import android.app.Activity
import android.app.AlertDialog
import android.content.ClipboardManager
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.provider.Settings
import android.view.View
import android.view.autofill.AutofillManager
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.TextView
import app.keyra.android.Keyra
import app.keyra.android.R
import app.keyra.android.api.KeyraAddress
import app.keyra.android.api.KeyraApi
import app.keyra.android.store.AppStore
import app.keyra.android.store.appStore

/** Keyra's address and the access token; also the autofill service's settings screen. */
class SetupActivity : Activity() {
    private lateinit var store: AppStore
    private lateinit var address: EditText
    private lateinit var token: EditText
    private lateinit var result: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_setup)
        fitSystemBars(findViewById(R.id.root))
        store = appStore(this)
        setTitle(if (store.configured) R.string.settings_title else R.string.setup_title)
        address = findViewById(R.id.address)
        token = findViewById(R.id.token)
        result = findViewById(R.id.result)
        if (savedInstanceState == null) address.setText(store.address ?: KeyraAddress.DEFAULT)
        sharedToken(intent)?.let(token::setText)

        findViewById<Button>(R.id.paste).setOnClickListener {
            val clip = getSystemService(ClipboardManager::class.java).primaryClip
            val text = clip?.takeIf { it.itemCount > 0 }?.getItemAt(0)?.coerceToText(this)?.toString()
            if (text != null) token.setText(text.trim())
        }
        findViewById<Button>(R.id.test).setOnClickListener { test() }
        findViewById<Button>(R.id.save).setOnClickListener { save() }
        findViewById<Button>(R.id.enable_autofill).setOnClickListener {
            startActivity(Intent(Settings.ACTION_REQUEST_SET_AUTOFILL_SERVICE, Uri.parse("package:$packageName")))
        }
        findViewById<Button>(R.id.disconnect).setOnClickListener {
            AlertDialog.Builder(this)
                .setMessage(R.string.disconnect)
                .setPositiveButton(R.string.disconnect) { _, _ ->
                    store.clear()
                    address.setText(KeyraAddress.DEFAULT)
                    token.setText("")
                    refresh()
                }
                .setNegativeButton(R.string.cancel, null)
                .show()
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        sharedToken(intent)?.let(token::setText)
    }

    override fun onResume() {
        super.onResume()
        refresh()
    }

    private fun sharedToken(intent: Intent?): String? {
        if (intent?.action != Intent.ACTION_SEND) return null
        val text = intent.getStringExtra(Intent.EXTRA_TEXT) ?: return null
        return TOKEN_IN_TEXT.find(text)?.value
    }

    private fun refresh() {
        findViewById<View>(R.id.token_kept).visibility = if (store.configured) View.VISIBLE else View.GONE
        findViewById<View>(R.id.disconnect).visibility = if (store.configured) View.VISIBLE else View.GONE
        val enabled = getSystemService(AutofillManager::class.java)?.hasEnabledAutofillServices() == true
        findViewById<View>(R.id.autofill_state).visibility = if (enabled) View.VISIBLE else View.GONE
        findViewById<View>(R.id.enable_autofill).visibility = if (enabled) View.GONE else View.VISIBLE

        val pins = findViewById<LinearLayout>(R.id.pins)
        pins.removeAllViews()
        val list = store.pins()
        if (list.isEmpty()) {
            pins.addView(TextView(this).apply {
                setText(R.string.pins_none)
                setTextAppearance(R.style.Keyra_Muted)
            })
        }
        for (p in list) {
            val row = layoutInflater.inflate(android.R.layout.simple_list_item_1, pins, false) as TextView
            row.text = getString(R.string.pin_row, p.key.substringAfter(':'), p.title)
            row.setOnClickListener {
                AlertDialog.Builder(this)
                    .setMessage(row.text)
                    .setPositiveButton(R.string.remove) { _, _ ->
                        store.unpin(p.key)
                        refresh()
                    }
                    .setNegativeButton(R.string.cancel, null)
                    .show()
            }
            pins.addView(row)
        }
    }

    /** The validated (address, token), or null after showing what is wrong. */
    private fun input(): Pair<String, String>? {
        val addr = KeyraAddress.normalize(address.text.toString())
        if (addr == null) {
            show(getString(R.string.err_address), error = true)
            return null
        }
        val typed = token.text.toString().trim()
        val tok = if (typed.isEmpty() && store.configured) {
            try {
                store.token
            } catch (_: java.security.GeneralSecurityException) {
                show(getString(R.string.err_token_unreadable), error = true)
                return null
            }
        } else {
            typed
        }
        if (tok == null || !KeyraApi.TOKEN_RE.matches(tok)) {
            show(getString(R.string.err_token_format), error = true)
            return null
        }
        return addr to tok
    }

    private fun test() {
        val (addr, tok) = input() ?: return
        show(getString(R.string.testing), error = false)
        val api = Keyra.api(this, addr, tok)
        Keyra.async({ api.entries().size }) { r ->
            if (isDestroyed) return@async
            r.onSuccess { show(resources.getQuantityString(R.plurals.connection_ok, it, it), error = false) }
                .onFailure { show(Keyra.message(this, it), error = true) }
        }
    }

    private fun save() {
        val (addr, tok) = input() ?: return
        store.address = addr
        store.token = tok
        token.setText("")
        address.setText(addr)
        show(getString(R.string.saved_settings), error = false)
        if (isTaskRoot) startActivity(Intent(this, MainActivity::class.java))
        finish()
    }

    private fun show(text: String, error: Boolean) {
        result.text = text
        result.setTextColor(getColor(if (error) R.color.danger else R.color.accent))
        result.visibility = View.VISIBLE
    }

    private companion object {
        val TOKEN_IN_TEXT = Regex("keyra_[a-z2-7]{32}")
    }
}
