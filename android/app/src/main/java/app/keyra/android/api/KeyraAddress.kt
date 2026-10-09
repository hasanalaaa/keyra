package app.keyra.android.api

import java.net.URI
import java.net.URISyntaxException

/**
 * Keyra speaks plain HTTP (SPEC §6), so the app accepts http:// only for addresses that
 * stay on the local network. The network security config cannot express IP ranges, so
 * this check is what keeps a bearer token from going to the internet in clear text.
 */
object KeyraAddress {
    const val DEFAULT = "http://keyra.local"

    /** "http://host[:port]" for a valid address, or null. */
    fun normalize(input: String): String? {
        var s = input.trim()
        if (s.isEmpty()) return null
        if (!s.contains("://")) s = "http://$s"
        val uri = try {
            URI(s)
        } catch (_: URISyntaxException) {
            return null
        }
        val scheme = uri.scheme?.lowercase() ?: return null
        if (scheme != "http" && scheme != "https") return null
        val host = uri.host?.lowercase()?.trimEnd('.') ?: return null
        if (host.isEmpty() || uri.rawUserInfo != null || uri.rawQuery != null || uri.rawFragment != null) return null
        if (!(uri.rawPath.isNullOrEmpty() || uri.rawPath == "/")) return null
        if (scheme == "http" && !isLocalHost(host)) return null
        val port = if (uri.port == -1) "" else ":${uri.port}"
        return "$scheme://$host$port"
    }

    fun isLocalHost(rawHost: String): Boolean {
        val host = rawHost.lowercase().removePrefix("[").removeSuffix("]").trimEnd('.')
        if (host == "localhost" || host.endsWith(".local") || host.endsWith(".home.arpa")) return true
        ipv4(host)?.let { (a, b) ->
            return a == 10 || a == 127 || (a == 172 && b in 16..31) || (a == 192 && b == 168) || (a == 169 && b == 254)
        }
        if (host.contains(':')) {
            // IPv6 literal: loopback, link-local fe80::/10, unique local fc00::/7.
            if (host == "::1") return true
            val first = host.substringBefore(':').toIntOrNull(16) ?: return false
            return (first and 0xffc0) == 0xfe80 || (first and 0xfe00) == 0xfc00
        }
        return false
    }

    /** First two octets of a dotted IPv4 literal. */
    private fun ipv4(host: String): Pair<Int, Int>? {
        val parts = host.split('.')
        if (parts.size != 4) return null
        val n = parts.map { p -> if (p.isEmpty() || p.length > 3 || !p.all(Char::isDigit)) return null else p.toInt() }
        if (n.any { it > 255 }) return null
        return n[0] to n[1]
    }
}
