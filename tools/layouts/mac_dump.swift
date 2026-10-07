// Dumps what macOS itself types for every printing key of a keyboard layout,
// via UCKeyTranslate on the layout data the OS ships (ANSI keyboard type).
// Usage: swift tools/layouts/mac_dump.swift com.apple.keylayout.British ...
// Output: one line per (HID usage, modifiers): "<usage hex> <mods> <U+code ...|dead:U+code>".
// mods: 0 none, 1 shift, 2 option, 3 shift+option.
import Carbon

// HID usage (Keyboard page) → macOS virtual key code, ANSI keyboard.
let hidToVk: [(UInt8, UInt16)] = [
  (0x04, 0x00), (0x05, 0x0B), (0x06, 0x08), (0x07, 0x02), (0x08, 0x0E), (0x09, 0x03), (0x0A, 0x05),
  (0x0B, 0x04), (0x0C, 0x22), (0x0D, 0x26), (0x0E, 0x28), (0x0F, 0x25), (0x10, 0x2E), (0x11, 0x2D),
  (0x12, 0x1F), (0x13, 0x23), (0x14, 0x0C), (0x15, 0x0F), (0x16, 0x01), (0x17, 0x11), (0x18, 0x20),
  (0x19, 0x09), (0x1A, 0x0D), (0x1B, 0x07), (0x1C, 0x10), (0x1D, 0x06),
  (0x1E, 0x12), (0x1F, 0x13), (0x20, 0x14), (0x21, 0x15), (0x22, 0x17), (0x23, 0x16), (0x24, 0x1A),
  (0x25, 0x1C), (0x26, 0x19), (0x27, 0x1D),
  (0x2C, 0x31), (0x2D, 0x1B), (0x2E, 0x18), (0x2F, 0x21), (0x30, 0x1E), (0x31, 0x2A), (0x33, 0x29),
  (0x34, 0x27), (0x35, 0x32), (0x36, 0x2B), (0x37, 0x2F), (0x38, 0x2C), (0x64, 0x0A),
]
let modStates: [UInt32] = [0, UInt32(shiftKey >> 8), UInt32(optionKey >> 8), UInt32((shiftKey | optionKey) >> 8)]

func hex(_ s: [UniChar]) -> String { s.map { String(format: "U+%04X", $0) }.joined(separator: ",") }

let ansiType: UInt32 = {
  for t: UInt32 in 0...255 where KBGetLayoutType(Int16(t)) == UInt32(kKeyboardANSI) { if t >= 40 { return t } }
  return 40
}()

for id in CommandLine.arguments.dropFirst() {
  let filter = [kTISPropertyInputSourceID as String: id] as CFDictionary
  guard let list = TISCreateInputSourceList(filter, true)?.takeRetainedValue() as? [TISInputSource],
        let src = list.first, let raw = TISGetInputSourceProperty(src, kTISPropertyUnicodeKeyLayoutData) else {
    print("# \(id): not found"); continue
  }
  let data = Unmanaged<CFData>.fromOpaque(raw).takeUnretainedValue()
  let layout = unsafeBitCast(CFDataGetBytePtr(data), to: UnsafePointer<UCKeyboardLayout>.self)
  print("# \(id) kbdType=\(ansiType)")
  for (usage, vk) in hidToVk {
    for (mi, mods) in modStates.enumerated() {
      var dead: UInt32 = 0
      var len = 0
      var chars = [UniChar](repeating: 0, count: 8)
      UCKeyTranslate(layout, vk, UInt16(kUCKeyActionDown), mods, ansiType, 0, &dead, 8, &len, &chars)
      if len == 0 && dead != 0 {
        var len2 = 0
        var chars2 = [UniChar](repeating: 0, count: 8)
        UCKeyTranslate(layout, 0x31, UInt16(kUCKeyActionDown), 0, ansiType, 0, &dead, 8, &len2, &chars2)
        print(String(format: "%02X %d dead:%@", usage, mi, hex(Array(chars2[0..<len2]))))
      } else if len > 0 {
        print(String(format: "%02X %d %@", usage, mi, hex(Array(chars[0..<len]))))
      }
    }
  }
}
