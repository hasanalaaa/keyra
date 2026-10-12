package app.keyra.android

import app.keyra.android.api.Bond
import app.keyra.android.match.TargetKind
import app.keyra.android.match.newBond
import app.keyra.android.match.stillValid
import app.keyra.android.match.targetKind
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class TargetChoiceTest {
    private val phone = Bond("AA:BB:CC:00:11:22", "Pixel", connected = false)
    private val mac = Bond("11:22:33:44:55:66", "MacBook", connected = true)

    @Test
    fun kindNamesTheChoice() {
        assertEquals(TargetKind.AUTO, targetKind(null, phone.addr))
        assertEquals(TargetKind.USB, targetKind("usb", phone.addr))
        assertEquals(TargetKind.PHONE, targetKind("aa:bb:cc:00:11:22", phone.addr))
        assertEquals(TargetKind.BLUETOOTH, targetKind(mac.addr, phone.addr))
        assertEquals(TargetKind.BLUETOOTH, targetKind(mac.addr, null))
    }

    @Test
    fun newBondIsTheOneThatAppeared() {
        assertEquals(phone, newBond(listOf(mac), listOf(mac, phone)))
        assertNull(newBond(listOf(mac), listOf(mac)))
        assertNull("two at once: ambiguous", newBond(emptyList(), listOf(mac, phone)))
        assertNull("case only", newBond(listOf(phone.copy(addr = phone.addr.lowercase())), listOf(phone)))
    }

    @Test
    fun aForgottenBondFallsBackToKeyrasChoice() {
        assertEquals(mac.addr, stillValid(mac.addr, listOf(mac)))
        assertNull(stillValid(phone.addr, listOf(mac)))
        assertEquals("usb", stillValid("usb", emptyList()))
        assertNull(stillValid(null, listOf(mac)))
    }
}
