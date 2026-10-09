package app.keyra.android.ui

import android.app.Activity
import android.content.Context
import android.os.Build
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.view.WindowInsets
import android.widget.BaseAdapter
import android.widget.Button
import android.widget.ProgressBar
import android.widget.TextView
import app.keyra.android.R
import app.keyra.android.api.AgentStatus
import app.keyra.android.api.Entry
import app.keyra.android.api.State

/**
 * Android 15+ draws apps edge to edge; pad [root] so nothing sits under the status,
 * navigation or keyboard area. The insets the content gets already account for the action bar.
 */
fun Activity.fitSystemBars(root: View) {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return
    val left = root.paddingLeft
    val top = root.paddingTop
    val right = root.paddingRight
    val bottom = root.paddingBottom
    root.setOnApplyWindowInsetsListener { v, insets ->
        val bars = insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.ime() or WindowInsets.Type.displayCutout())
        v.setPadding(left + bars.left, top + bars.top, right + bars.right, bottom + bars.bottom)
        insets
    }
}

fun statusText(context: Context, s: AgentStatus): String = when (s.state) {
    State.TYPED -> context.getString(R.string.state_typed)
    State.SAVED -> context.getString(R.string.state_saved)
    State.CANCELLED -> context.getString(R.string.state_cancelled)
    State.EXPIRED -> context.getString(R.string.state_expired)
    State.FAILED -> context.getString(R.string.state_failed, s.code ?: "?")
    State.WAITING -> context.getString(R.string.state_waiting)
    State.ARMED, State.NONE -> context.getString(R.string.press_title)
}

/** Login list with a search filter (title or host contains the query). */
class EntryAdapter(private val context: Context) : BaseAdapter() {
    private var all: List<Entry> = emptyList()
    private var shown: List<Entry> = emptyList()
    private var query = ""

    fun set(entries: List<Entry>) {
        all = entries.sortedBy { it.title.lowercase() }
        apply()
    }

    fun filter(q: String) {
        query = q.trim().lowercase()
        apply()
    }

    val total: Int get() = all.size

    private fun apply() {
        shown = if (query.isEmpty()) all else all.filter { query in it.title.lowercase() || query in it.host }
        notifyDataSetChanged()
    }

    override fun getCount() = shown.size
    override fun getItem(position: Int): Entry = shown[position]
    override fun getItemId(position: Int) = shown[position].id

    override fun getView(position: Int, convertView: View?, parent: ViewGroup): View {
        val v = convertView ?: LayoutInflater.from(context).inflate(android.R.layout.simple_list_item_2, parent, false)
        val e = shown[position]
        // Align both lines to the layout's start, whatever script the title is in.
        v.findViewById<TextView>(android.R.id.text1).apply {
            text = e.title
            textAlignment = View.TEXT_ALIGNMENT_VIEW_START
        }
        v.findViewById<TextView>(android.R.id.text2).apply {
            text = e.host
            textDirection = View.TEXT_DIRECTION_LTR
            textAlignment = View.TEXT_ALIGNMENT_VIEW_START
        }
        return v
    }
}

/** The views of layout_press.xml. */
class PressPanel(root: View) {
    val panel: View = root.findViewById(R.id.press_panel)
    val title: TextView = root.findViewById(R.id.press_title)
    val body: TextView = root.findViewById(R.id.press_body)
    val progress: ProgressBar = root.findViewById(R.id.press_progress)
    val countdown: TextView = root.findViewById(R.id.press_countdown)
    val button: Button = root.findViewById(R.id.press_button)

    fun arming() {
        title.setText(R.string.arming)
        body.text = ""
        countdown.text = ""
        progress.visibility = View.VISIBLE
    }

    fun waiting(bodyText: Int) {
        title.setText(R.string.press_title)
        body.setText(bodyText)
    }

    /** The final outcome replaces the headline; the button becomes "Close". */
    fun done(text: String) {
        progress.visibility = View.GONE
        countdown.text = ""
        body.text = ""
        title.text = text
        button.setText(R.string.close)
    }
}
