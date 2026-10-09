package app.keyra.android.match

import app.keyra.android.api.Entry

/** What a form belongs to: a web page (browser with webDomain) or an app. */
data class Target(val webDomain: String?, val packageName: String) {
    /** Key for a remembered choice: "web:<domain>" for pages, "app:<package>" for apps. */
    val key: String
        get() = webDomain?.let { "web:" + HostMatcher.normalize(it) } ?: "app:$packageName"
}

/**
 * Which logins to offer for a form. A wrong offer cannot leak anything (the dataset holds no
 * value and typing still waits for the button), so this favours simple, predictable rules:
 * - web pages: same registrable domain ("accounts.google.com" ↔ "google.com");
 * - apps: a package label equals the domain's name ("com.github.android" ↔ "github.com"),
 *   or the entry's host is the package itself (logins saved from an app use androidapp://);
 * - remembered choices first.
 */
object HostMatcher {
    /** Second-level labels under a two-letter country code that are not names ("gov.iq", "co.uk"). */
    private val COUNTRY_SLD = setOf("ac", "co", "com", "edu", "gob", "gov", "go", "mil", "ne", "net", "or", "org", "sch")

    /** Shared hosting where every subdomain is a different owner: never match across them. */
    private val SHARED_SUFFIXES = listOf(
        "github.io", "gitlab.io", "blogspot.com", "appspot.com", "herokuapp.com", "netlify.app",
        "vercel.app", "pages.dev", "workers.dev", "web.app", "firebaseapp.com", "azurewebsites.net",
        "cloudfront.net", "glitch.me", "onrender.com", "fly.dev", "ngrok.io", "ngrok-free.app",
    )

    /** Package labels that say nothing about the owner. */
    private val NOISE = setOf(
        "com", "org", "net", "io", "app", "apps", "android", "mobile", "client", "www", "co", "de", "uk",
        "us", "lite", "main", "free", "pro", "official",
    )

    /** First labels of package names that no web host starts with. */
    private val PACKAGE_ROOTS = setOf("com", "org", "net", "io", "edu", "gov")

    fun normalize(host: String): String = host.trim().lowercase().trimEnd('.').removePrefix("www.")

    private fun isIp(host: String) = host.contains(':') || host.split('.').let { p -> p.size == 4 && p.all { it.isNotEmpty() && it.all(Char::isDigit) } }

    /** The registrable domain, approximately (no full public-suffix list on purpose). */
    fun baseDomain(rawHost: String): String {
        val host = normalize(rawHost)
        if (host.isEmpty() || isIp(host)) return host
        SHARED_SUFFIXES.firstOrNull { host == it || host.endsWith(".$it") }?.let { suffix ->
            val rest = host.removeSuffix(suffix).trimEnd('.')
            return if (rest.isEmpty()) host else rest.substringAfterLast('.') + "." + suffix
        }
        val labels = host.split('.')
        if (labels.size <= 2) return host
        val n = labels.size
        val keep = if (labels[n - 1].length == 2 && labels[n - 2] in COUNTRY_SLD) 3 else 2
        return labels.takeLast(keep).joinToString(".")
    }

    fun matchesWeb(entryHost: String, webDomain: String): Boolean {
        val a = normalize(entryHost)
        val b = normalize(webDomain)
        if (a.isEmpty() || b.isEmpty()) return false
        if (a == b) return true
        if (isIp(a) || isIp(b)) return false
        return baseDomain(a) == baseDomain(b)
    }

    fun matchesApp(entryHost: String, packageName: String): Boolean {
        val host = normalize(entryHost)
        val pkg = packageName.lowercase()
        if (host.isEmpty() || pkg.isEmpty()) return false
        if (host == pkg) return true
        // A package name saved as androidapp://… ("com.example.notes") matches only that app.
        if (isIp(host) || host.substringBefore('.') in PACKAGE_ROOTS) return false
        val name = baseDomain(host).substringBefore('.').replace("-", "")
        if (name.length < 2 || name in NOISE) return false
        return pkg.split('.').any { label ->
            label !in NOISE && (label == name || (name.length >= 4 && label.startsWith(name)))
        }
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
