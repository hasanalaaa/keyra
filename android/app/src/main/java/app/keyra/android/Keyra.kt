package app.keyra.android

import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.os.Handler
import android.os.Looper
import app.keyra.android.api.ApiException
import app.keyra.android.api.ConnectionOpener
import app.keyra.android.api.KeyraApi
import app.keyra.android.store.appStore
import java.io.IOException
import java.net.HttpURLConnection
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

/** App wiring: the API client from stored settings, a background pool and error texts. */
object Keyra {
    val io: ExecutorService = Executors.newCachedThreadPool()
    val main = Handler(Looper.getMainLooper())

    /** Runs [work] off the main thread and hands its result back on the main thread. */
    fun <T> async(work: () -> T, done: (Result<T>) -> Unit) {
        io.execute {
            val r = runCatching(work)
            main.post { done(r) }
        }
    }

    /** The client for the stored address and token, or null when the app is not set up. */
    fun api(context: Context): KeyraApi? {
        val store = appStore(context)
        if (!store.configured) return null
        return api(context, store.address ?: return null, store.token ?: return null)
    }

    fun api(context: Context, address: String, token: String): KeyraApi =
        KeyraApi(address, token, wifiOpener(context.applicationContext))

    /**
     * Keyra's own access point has no internet, so Android may route requests over mobile
     * data instead. Prefer a Wi-Fi network when there is one; otherwise the default route.
     */
    private fun wifiOpener(context: Context) = ConnectionOpener { url ->
        val cm = context.getSystemService(ConnectivityManager::class.java)
        @Suppress("DEPRECATION") // allNetworks: still the simplest way to find the Wi-Fi network.
        val wifi = cm.allNetworks.firstOrNull { cm.getNetworkCapabilities(it)?.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) == true }
        (wifi?.openConnection(url) ?: url.openConnection()) as HttpURLConnection
    }

    fun message(context: Context, e: Throwable): String = when (e) {
        is ApiException -> when (e.code) {
            "locked" -> context.getString(R.string.err_locked)
            "invalid_token" -> context.getString(R.string.err_invalid_token)
            "busy" -> context.getString(R.string.err_busy)
            "rate_limited" -> context.getString(R.string.err_rate_limited, ((e.retryAfterMs + 999) / 1000).toInt().coerceAtLeast(1))
            "not_found" -> context.getString(R.string.err_not_found)
            "forbidden" -> context.getString(R.string.err_forbidden)
            "no_time" -> context.getString(R.string.err_no_time)
            "not_cancelled" -> context.getString(R.string.err_not_cancelled)
            else -> context.getString(R.string.err_other, e.message ?: e.code)
        }
        is IOException -> context.getString(R.string.err_unreachable)
        else -> context.getString(R.string.err_other, e.message ?: e.javaClass.simpleName)
    }
}
