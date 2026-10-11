package app.keyra.android.ui

import android.content.Context
import app.keyra.android.R

/** One vocabulary for agent HTTP errors and the failure codes in agent/status. */
object ResultMessages {
    fun resource(code: String): Int? = when (code) {
        "entry_changed" -> R.string.err_entry_changed
        "no_usb" -> R.string.err_no_usb
        "no_host" -> R.string.err_no_host
        "host_changed" -> R.string.err_host_changed
        "unsupported_char" -> R.string.err_unsupported_char
        "failed" -> R.string.err_failed
        "not_cancelled" -> R.string.err_not_cancelled
        "unsupported_format" -> R.string.err_unsupported_format
        "corrupt" -> R.string.err_corrupt
        "busy" -> R.string.err_busy
        "locked" -> R.string.err_locked
        "rate_limited" -> R.plurals.err_rate_limited
        "invalid_token" -> R.string.err_invalid_token
        "not_found" -> R.string.err_not_found
        "forbidden" -> R.string.err_forbidden
        "no_time" -> R.string.err_no_time
        "invalid" -> R.string.err_invalid
        "host_mismatch" -> R.string.err_host_mismatch
        "too_large" -> R.string.err_too_large
        "method_not_allowed" -> R.string.err_method_not_allowed
        "full" -> R.string.err_full
        "csrf" -> R.string.err_csrf
        else -> null
    }

    fun retrySeconds(retryAfterMs: Long): Long =
        (retryAfterMs.coerceAtLeast(1) / 1000) + if (retryAfterMs.coerceAtLeast(1) % 1000 == 0L) 0 else 1

    fun text(context: Context, code: String, retryAfterMs: Long = 0): String {
        val id = resource(code) ?: return context.getString(R.string.err_other, code)
        return if (code == "rate_limited") {
            val seconds = retrySeconds(retryAfterMs).coerceAtMost(Int.MAX_VALUE.toLong()).toInt()
            context.resources.getQuantityString(id, seconds, seconds)
        } else context.getString(id)
    }
}
