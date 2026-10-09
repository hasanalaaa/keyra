package app.keyra.android.match

import app.keyra.android.api.Entry

/**
 * What a form belongs to: a web page or an app. [webDomain] is set only when the form is in a
 * known browser (see [Browsers]); any other app's web content counts as that app, so an app
 * cannot claim to be a site by putting it in a WebView.
 */
data class Target(val webDomain: String?, val packageName: String) {
    /** Key for a remembered choice: "web:<domain>" for pages, "app:<package>" for apps. */
    val key: String
        get() = webDomain?.let { "web:" + HostMatcher.normalize(it) } ?: "app:$packageName"
}

/**
 * Browsers whose reported page domain is trusted. Android package names are unique on a device,
 * and these come preinstalled or from the store, so another app cannot reuse them alongside.
 */
object Browsers {
    val PACKAGES = setOf(
        "com.android.chrome", "com.chrome.beta", "com.chrome.dev", "com.chrome.canary",
        "org.chromium.chrome", "com.google.android.apps.chrome",
        "org.mozilla.firefox", "org.mozilla.firefox_beta", "org.mozilla.fenix", "org.mozilla.focus",
        "org.mozilla.klar", "com.microsoft.emmx", "com.brave.browser", "com.brave.browser_beta",
        "com.sec.android.app.sbrowser", "com.sec.android.app.sbrowser.beta",
        "com.opera.browser", "com.opera.mini.native", "com.opera.gx", "com.vivaldi.browser",
        "com.duckduckgo.mobile.android", "com.kiwibrowser.browser", "com.mi.globalbrowser",
        "com.huawei.browser", "com.ecosia.android", "org.torproject.torbrowser",
    )

    fun isBrowser(packageName: String) = packageName in PACKAGES
}

/**
 * Which logins to offer for a form. Choosing an offer and pressing Keyra's button types the
 * login into whatever has focus, so an offer must never be made to the wrong app or site:
 * - web pages (known browsers only): the same host, or one is a subdomain of the other
 *   ("github.com" ↔ "gist.github.com"); siblings such as "mail.google.com" ↔
 *   "accounts.google.com" are left to "Choose a login…", which can remember the choice;
 * - apps: only logins saved from that very app (host = package, from androidapp://…) —
 *   no guessing from package names, which any app can choose;
 * - remembered choices first.
 */
object HostMatcher {
    /** Second-level labels under a two-letter country code that are not names ("gov.iq", "co.uk"). */
    private val COUNTRY_SLD = setOf("ac", "co", "com", "edu", "gob", "gov", "go", "mil", "ne", "net", "or", "org", "sch")

    fun normalize(host: String): String = host.trim().lowercase().trimEnd('.').removePrefix("www.")

    private fun isIp(host: String) = host.contains(':') || host.split('.').let { p -> p.size == 4 && p.all { it.isNotEmpty() && it.all(Char::isDigit) } }

    /** "com", "co.uk", "gov.iq": never treated as a site of its own. */
    private fun isPublicSuffixLike(host: String): Boolean {
        val labels = host.split('.')
        return labels.size < 2 || (labels.size == 2 && labels[1].length == 2 && labels[0] in COUNTRY_SLD)
    }

    fun matchesWeb(entryHost: String, webDomain: String): Boolean {
        val a = normalize(entryHost)
        val b = normalize(webDomain)
        if (a.isEmpty() || b.isEmpty()) return false
        if (a == b) return true
        if (isIp(a) || isIp(b)) return false
        val (short, long) = if (a.length < b.length) a to b else b to a
        return !isPublicSuffixLike(short) && long.endsWith(".$short")
    }

    fun matchesApp(entryHost: String, packageName: String): Boolean {
        val host = normalize(entryHost)
        return host.isNotEmpty() && host == packageName.lowercase()
    }

    /** Matching entries, remembered choices first, at most [limit]. */
    fun match(entries: List<Entry>, target: Target, pinned: Collection<Long>, limit: Int = 5): List<Entry> {
        val pins = entries.filter { it.id in pinned }
        val found = entries.filter { e ->
            e.id !in pinned && (target.webDomain?.let { matchesWeb(e.host, it) } ?: matchesApp(e.host, target.packageName))
        }
        return (pins + found).take(limit)
    }
}
