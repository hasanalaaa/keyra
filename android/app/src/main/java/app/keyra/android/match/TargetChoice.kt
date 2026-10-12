package app.keyra.android.match

import app.keyra.android.api.Bond

/** What the chosen target is, for its label and the hint shown while Keyra waits. */
enum class TargetKind { AUTO, USB, PHONE, BLUETOOTH }

/** [target]: null = Keyra's own choice, "usb", or a bond address; [phone]: this phone's bond. */
fun targetKind(target: String?, phone: String?): TargetKind = when {
    target == null -> TargetKind.AUTO
    target == "usb" -> TargetKind.USB
    target.equals(phone, ignoreCase = true) -> TargetKind.PHONE
    else -> TargetKind.BLUETOOTH
}

/** The bond that appeared between two listings (a pairing just finished), if exactly one did. */
fun newBond(before: List<Bond>, after: List<Bond>): Bond? {
    val known = before.map { it.addr.uppercase() }.toSet()
    return after.filter { it.addr.uppercase() !in known }.singleOrNull()
}

/**
 * A chosen Bluetooth target that Keyra no longer knows (forgotten in its web app) falls back to
 * Keyra's own choice rather than failing every request with not_found.
 */
fun stillValid(target: String?, bonds: List<Bond>): String? =
    if (target == null || target == "usb" || bonds.any { it.addr.equals(target, ignoreCase = true) }) target else null
