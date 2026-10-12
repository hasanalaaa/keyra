package app.keyra.android

import org.junit.Assert.assertTrue
import org.junit.Test
import org.w3c.dom.Element
import java.io.File
import javax.xml.parsers.DocumentBuilderFactory

/**
 * Checks the resource contract that plain JVM tests can verify without a device. The screens are
 * Compose: Material 3 gives every control a 48 dp touch target, and each icon-only button has a
 * content description in code.
 */
class AccessibilityResourcesTest {

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

}
