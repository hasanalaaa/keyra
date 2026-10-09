package app.keyra.android.ui

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.os.Bundle
import android.text.Editable
import android.text.TextWatcher
import android.view.Menu
import android.view.MenuItem
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.ListView
import android.widget.TextView
import app.keyra.android.Keyra
import app.keyra.android.R
import app.keyra.android.api.Entry
import app.keyra.android.api.What
import app.keyra.android.store.appStore

/** The logins this token can use; tap one to have Keyra type it after a press. */
class MainActivity : Activity() {
    private lateinit var adapter: EntryAdapter
    private lateinit var message: TextView
    private lateinit var retry: Button
    private var loaded = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        fitSystemBars(findViewById(R.id.root))
        adapter = EntryAdapter(this)
        message = findViewById(R.id.message)
        retry = findViewById(R.id.retry)
        retry.setOnClickListener { load() }
        val list = findViewById<ListView>(R.id.list)
        list.adapter = adapter
        list.setOnItemClickListener { _, _, position, _ -> choose(adapter.getItem(position)) }
        findViewById<EditText>(R.id.search).addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) = Unit
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) = Unit
            override fun afterTextChanged(s: Editable?) {
                adapter.filter(s?.toString().orEmpty())
                showEmptyState()
            }
        })
    }

    override fun onResume() {
        super.onResume()
        if (!appStore(this).configured) {
            startActivity(Intent(this, SetupActivity::class.java))
            finish()
            return
        }
        if (!loaded) load()
    }

    override fun onCreateOptionsMenu(menu: Menu): Boolean {
        menuInflater.inflate(R.menu.main, menu)
        return true
    }

    override fun onOptionsItemSelected(item: MenuItem): Boolean = when (item.itemId) {
        R.id.action_refresh -> load().let { true }
        R.id.action_generate -> startActivity(Intent(this, GenerateActivity::class.java)).let { true }
        R.id.action_settings -> {
            loaded = false
            startActivity(Intent(this, SetupActivity::class.java))
            true
        }
        else -> super.onOptionsItemSelected(item)
    }

    private fun load() {
        val api = try {
            Keyra.api(this)
        } catch (_: java.security.GeneralSecurityException) {
            show(getString(R.string.err_token_unreadable), canRetry = false)
            return
        } ?: return
        show(getString(R.string.loading), canRetry = false)
        Keyra.async({ api.entries() }) { r ->
            if (isDestroyed) return@async
            r.onSuccess {
                loaded = true
                adapter.set(it)
                showEmptyState()
            }.onFailure { show(Keyra.message(this, it), canRetry = true) }
        }
    }

    private fun show(text: String, canRetry: Boolean) {
        message.text = text
        message.visibility = View.VISIBLE
        retry.visibility = if (canRetry) View.VISIBLE else View.GONE
    }

    private fun showEmptyState() {
        retry.visibility = View.GONE
        when {
            adapter.total == 0 -> show(getString(R.string.empty_list), canRetry = false)
            adapter.count == 0 -> show(getString(R.string.no_results), canRetry = false)
            else -> message.visibility = View.GONE
        }
    }

    private fun choose(e: Entry) {
        val options = listOf(
            What.USERNAME to R.string.type_username,
            What.PASSWORD to R.string.type_password,
            What.BOTH to R.string.type_both,
            What.TOTP to R.string.type_totp,
        )
        AlertDialog.Builder(this)
            .setTitle(getString(R.string.type_dialog_title, e.title))
            .setItems(options.map { getString(it.second) }.toTypedArray()) { _, which ->
                startActivity(ArmActivity.intent(this, e, options[which].first))
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }
}
