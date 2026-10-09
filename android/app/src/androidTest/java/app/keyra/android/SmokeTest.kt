package app.keyra.android

import android.widget.ListView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import app.keyra.android.api.GenerateOptions
import app.keyra.android.store.appStore
import app.keyra.android.ui.MainActivity
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * On a device or emulator, against a running mock (android/README.md):
 *   -e keyraUrl http://10.0.2.2:8787 -e keyraToken keyra_…
 */
@RunWith(AndroidJUnit4::class)
class SmokeTest {
    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val args = InstrumentationRegistry.getArguments()
    private val url: String? = args.getString("keyraUrl")
    private val token: String? = args.getString("keyraToken")

    @Before
    fun setUp() {
        assumeTrue("pass -e keyraUrl and -e keyraToken", url != null && token != null)
    }

    @After
    fun tearDown() {
        appStore(context).clear()
    }

    @Test
    fun tokenRoundTripsThroughTheKeystore() {
        val store = appStore(context)
        store.address = url
        store.token = token
        assertEquals(token, appStore(context).token)
        val raw = context.getSharedPreferences("keyra", 0).all.values.joinToString()
        assertTrue("token stored in clear", !raw.contains(token!!))
    }

    @Test
    fun apiWorksOverTheDeviceNetwork() {
        val api = Keyra.api(context, url!!, token!!)
        assertTrue(api.entries().isNotEmpty())
        assertEquals(16, api.generate(GenerateOptions(length = 16)).length)
    }

    @Test
    fun mainScreenListsLogins() {
        val store = appStore(context)
        store.address = url
        store.token = token
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            var count = 0
            repeat(50) {
                scenario.onActivity { count = it.findViewById<ListView>(R.id.list).adapter.count }
                if (count > 0) return@use
                Thread.sleep(200)
            }
            assertTrue("no logins shown", count > 0)
        }
    }
}
