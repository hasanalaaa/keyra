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
import app.keyra.android.ui.ResultMessages
import java.io.IOException
import java.net.HttpURLConnection
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

/** App wiring: the API client from stored settings, a background pool and error texts. */
object Keyra {
    val io: ExecutorService = Executors.newCachedThreadPool()
    val main = Handler(Looper.getMainLooper())

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
        is ApiException -> ResultMessages.text(context, e.code, e.retryAfterMs)
        is IOException -> context.getString(R.string.err_unreachable)
        else -> context.getString(R.string.err_other, e.message ?: e.javaClass.simpleName)
    }
}
