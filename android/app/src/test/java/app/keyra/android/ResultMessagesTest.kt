package app.keyra.android

import app.keyra.android.ui.ResultMessages
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.w3c.dom.Element
import java.io.File
import javax.xml.parsers.DocumentBuilderFactory

class ResultMessagesTest {
    private val resultCodes = listOf(
        "no_usb", "no_host", "host_changed", "unsupported_char", "entry_changed", "failed",
    )
    private val agentErrors = listOf(
        "not_cancelled", "unsupported_format", "corrupt", "busy", "locked", "rate_limited",
        "invalid_token", "not_found", "forbidden", "no_time", "invalid", "host_mismatch",
        "too_large", "method_not_allowed", "full", "csrf",
    )

    @Test
    fun everyDeviceCodeHasAnExplicitLocalizedMessage() {
        val english = resources("values")
        val arabic = resources("values-ar")
        for (code in resultCodes + agentErrors) {
            val expected = if (code == "rate_limited") R.plurals.err_rate_limited
                else R.string::class.java.getField("err_$code").getInt(null)
            assertEquals("wrong message for $code", expected, ResultMessages.resource(code))
            val name = "err_$code"
            assertTrue("English message missing for $code", english.contains(name))
            assertTrue("Arabic message missing for $code", arabic.contains(name))
        }
        assertNull(ResultMessages.resource("future_code"))
    }

    @Test
    fun rateLimitUsesTheWholeWaitRoundedUp() {
        assertEquals(1L, ResultMessages.retrySeconds(0))
        assertEquals(1L, ResultMessages.retrySeconds(1))
        assertEquals(1L, ResultMessages.retrySeconds(1000))
        assertEquals(2L, ResultMessages.retrySeconds(1001))
        assertEquals(61L, ResultMessages.retrySeconds(60_001))
    }

    @Test
    fun countdownAndRateLimitHaveArabicPluralForms() {
        val arabic = resources("values-ar")
        for (name in listOf("seconds_left", "err_rate_limited", "connection_ok", "field_password")) {
            val values = arabic[name] ?: error("missing $name")
            assertEquals("$name needs all Arabic plural forms", setOf("zero", "one", "two", "few", "many", "other"), values)
        }
    }

    private fun resources(folder: String): Map<String, Set<String>> {
        val file = File("src/main/res/$folder/strings.xml")
        val doc = DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(file)
        val nodes = doc.documentElement.childNodes
        return buildMap {
            for (i in 0 until nodes.length) {
                val element = nodes.item(i) as? Element ?: continue
                val name = element.getAttribute("name")
                val quantities = if (element.tagName == "plurals") {
                    val items = element.getElementsByTagName("item")
                    (0 until items.length).map { (items.item(it) as Element).getAttribute("quantity") }.toSet()
                } else emptySet()
                put(name, quantities)
            }
        }
    }
}
