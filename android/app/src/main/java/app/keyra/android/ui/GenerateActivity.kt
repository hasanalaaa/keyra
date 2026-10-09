package app.keyra.android.ui

import android.app.Activity
import android.content.ClipData
import android.content.ClipDescription
import android.content.ClipboardManager
import android.content.Context
import android.os.Build
import android.os.Bundle
import android.os.PersistableBundle
import android.view.View
import android.view.WindowManager
import android.widget.Button
import android.widget.CheckBox
import android.widget.SeekBar
import android.widget.TextView
import app.keyra.android.Keyra
import app.keyra.android.R
import app.keyra.android.api.GenerateOptions

/** A fresh password from Keyra's generator; it is never stored, only shown and copied. */
class GenerateActivity : Activity() {
    private lateinit var password: TextView
    private lateinit var message: TextView
    private lateinit var copy: Button

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // Keeps the password out of screenshots and the recent-apps thumbnail.
        window.addFlags(WindowManager.LayoutParams.FLAG_SECURE)
        setContentView(R.layout.activity_generate)
        fitSystemBars(findViewById(R.id.root))
        password = findViewById(R.id.password)
        message = findViewById(R.id.message)
        copy = findViewById(R.id.copy)

        val label = findViewById<TextView>(R.id.length_label)
        val length = findViewById<SeekBar>(R.id.length)
        length.progress = DEFAULT_LENGTH - MIN_LENGTH
        label.text = getString(R.string.gen_length, DEFAULT_LENGTH)
        length.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar, progress: Int, fromUser: Boolean) {
                label.text = getString(R.string.gen_length, progress + MIN_LENGTH)
            }
            override fun onStartTrackingTouch(seekBar: SeekBar) = Unit
            override fun onStopTrackingTouch(seekBar: SeekBar) = Unit
        })

        findViewById<Button>(R.id.generate).setOnClickListener {
            val api = try {
                Keyra.api(this)
            } catch (_: java.security.GeneralSecurityException) {
                return@setOnClickListener show(getString(R.string.err_token_unreadable))
            } ?: return@setOnClickListener show(getString(R.string.not_set_up))
            val options = GenerateOptions(
                length = length.progress + MIN_LENGTH,
                lower = findViewById<CheckBox>(R.id.lower).isChecked,
                upper = findViewById<CheckBox>(R.id.upper).isChecked,
                digits = findViewById<CheckBox>(R.id.digits).isChecked,
                symbols = findViewById<CheckBox>(R.id.symbols).isChecked,
                avoidAmbiguous = findViewById<CheckBox>(R.id.avoid).isChecked,
            )
            it.isEnabled = false
            message.visibility = View.GONE
            Keyra.async({ api.generate(options) }) { r ->
                if (isDestroyed) return@async
                it.isEnabled = true
                r.onSuccess { pw ->
                    password.text = pw
                    copy.isEnabled = true
                }.onFailure { e -> show(Keyra.message(this, e)) }
            }
        }
        copy.setOnClickListener {
            ClipboardGuard.copySensitive(this, getString(R.string.clip_label), password.text.toString())
            show(getString(R.string.copied))
        }
    }

    override fun onDestroy() {
        password.text = ""
        super.onDestroy()
    }

    private fun show(text: String) {
        message.text = text
        message.visibility = View.VISIBLE
    }

    private companion object {
        const val MIN_LENGTH = 8
        const val DEFAULT_LENGTH = 20
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
