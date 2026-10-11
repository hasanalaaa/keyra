package app.keyra.android

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.w3c.dom.Element
import java.io.File
import javax.xml.parsers.DocumentBuilderFactory

/** Checks the XML contract that plain JVM tests can verify without a device. */
class AccessibilityResourcesTest {
    private val android = "http://schemas.android.com/apk/res/android"

    @Test
    fun allButtonsAndCheckBoxesHaveNamesAndFortyEightDpTargets() {
        File("src/main/res/layout").listFiles()!!.filter { it.extension == "xml" }.forEach { file ->
            val doc = DocumentBuilderFactory.newInstance().apply { isNamespaceAware = true }
                .newDocumentBuilder().parse(file)
            for (type in listOf("Button", "CheckBox")) {
                val views = doc.getElementsByTagName(type)
                for (i in 0 until views.length) {
                    val view = views.item(i) as Element
                    val name = view.getAttributeNS(android, "contentDescription")
                        .ifEmpty { view.getAttributeNS(android, "text") }
                    val dynamicRemember = view.getAttributeNS(android, "id") == "@+id/remember"
                    assertTrue("$file $type has no accessible name", name.isNotEmpty() || dynamicRemember)
                    assertEquals("$file $type target", "48dp", view.getAttributeNS(android, "minHeight"))
                }
            }
        }
    }

    @Test
    fun largeTextScreensCanScrollWithoutNestedScrollers() {
        for (name in listOf("activity_arm", "activity_setup", "activity_save", "activity_generate")) {
            assertEquals("ScrollView", root(name).tagName)
        }
        val autofill = root("activity_autofill")
        assertEquals("LinearLayout", autofill.tagName)
        val list = autofill.getElementsByTagName("ListView").item(0) as Element
        assertEquals("1", list.getAttributeNS(android, "layout_weight"))
        for (name in listOf("entry_item", "activity_main")) {
            val xml = File("src/main/res/layout/$name.xml").readText()
            assertFalse("$name truncates enlarged text", xml.contains("maxLines=") || xml.contains("singleLine="))
        }
    }

    @Test
    fun appTextColorsMeetNormalTextContrastOnConservativeSurfaces() {
        for ((folder, background) in listOf("values" to 0xF5F5F5, "values-night" to 0x424242)) {
            val doc = DocumentBuilderFactory.newInstance().newDocumentBuilder()
                .parse(File("src/main/res/$folder/colors.xml"))
            val colors = doc.getElementsByTagName("color")
            for (i in 0 until colors.length) {
                val color = colors.item(i) as Element
                if (color.getAttribute("name") !in setOf("accent", "muted", "danger")) continue
                val foreground = color.textContent.trim().removePrefix("#").toInt(16)
                assertTrue("$folder ${color.getAttribute("name")} contrast", contrast(foreground, background) >= 4.5)
            }
        }
    }

    private fun contrast(a: Int, b: Int): Double {
        fun luminance(color: Int): Double {
            val channels = (listOf(16, 8, 0)).map { ((color shr it) and 0xFF) / 255.0 }
                .map { if (it <= 0.04045) it / 12.92 else Math.pow((it + 0.055) / 1.055, 2.4) }
            return 0.2126 * channels[0] + 0.7152 * channels[1] + 0.0722 * channels[2]
        }
        val light = maxOf(luminance(a), luminance(b))
        val dark = minOf(luminance(a), luminance(b))
        return (light + 0.05) / (dark + 0.05)
    }

    private fun root(name: String): Element = DocumentBuilderFactory.newInstance().apply { isNamespaceAware = true }.newDocumentBuilder()
        .parse(File("src/main/res/layout/$name.xml")).documentElement
}
