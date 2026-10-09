package app.keyra.android.ui

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.widget.TextView
import app.keyra.android.Keyra
import app.keyra.android.R
import app.keyra.android.api.AgentStatus
import app.keyra.android.api.Entry
import app.keyra.android.api.KeyraApi
import app.keyra.android.api.What

/** Arms one typing action, counts down to Keyra's deadline and shows how it ended. */
class ArmActivity : Activity(), PressWaiter.Listener {
    private lateinit var panel: PressPanel
    private var api: KeyraApi? = null
    private var waiter: PressWaiter? = null
    private var finished = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_arm)
        fitSystemBars(findViewById(R.id.root))
        panel = PressPanel(findViewById(R.id.root))
        val id = intent.getLongExtra(EXTRA_ID, 0)
        val what = What.valueOf(requireNotNull(intent.getStringExtra(EXTRA_WHAT)))
        findViewById<TextView>(R.id.arm_entry).text = intent.getStringExtra(EXTRA_TITLE)
        panel.button.setOnClickListener { if (finished) finish() else cancel() }
        // Re-created (e.g. rotation): the request is already armed; arming again would replace it.
        if (savedInstanceState != null) {
            finished = true
            panel.done(getString(R.string.state_unknown))
            return
        }
        api = try {
            Keyra.api(this) ?: return finish()
        } catch (_: java.security.GeneralSecurityException) {
            finished = true
            return panel.done(getString(R.string.err_token_unreadable))
        }
        arm(id, what)
    }

    override fun onDestroy() {
        waiter?.stop()
        super.onDestroy()
    }

    private fun arm(id: Long, what: What) {
        val api = api ?: return
        panel.arming()
        Keyra.async({ api.type(id, what) }) { r ->
            if (isDestroyed) return@async
            r.onSuccess { expiresIn ->
                panel.waiting(R.string.press_body_type)
                waiter = PressWaiter(api, this).also { it.start(expiresIn) }
            }.onFailure { onFinished(null, it) }
        }
    }

    private fun cancel() {
        val api = api ?: return finish()
        panel.button.isEnabled = false
        Keyra.async({ api.cancel() }) { r ->
            if (isDestroyed) return@async
            panel.button.isEnabled = true
            // The next status poll reports "cancelled"; a failure here is shown directly.
            r.onFailure { onFinished(null, it) }
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
        finished = true
        panel.done(status?.let { statusText(this, it) } ?: Keyra.message(this, error ?: IllegalStateException()))
    }

    companion object {
        private const val EXTRA_ID = "id"
        private const val EXTRA_TITLE = "title"
        private const val EXTRA_WHAT = "what"

        fun intent(context: Context, e: Entry, what: What): Intent = Intent(context, ArmActivity::class.java)
            .putExtra(EXTRA_ID, e.id)
            .putExtra(EXTRA_TITLE, e.title)
            .putExtra(EXTRA_WHAT, what.name)
    }
}
