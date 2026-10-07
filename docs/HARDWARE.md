# Hardware

Keyra runs on a stock ESP32-S3 development board. No soldering and no extra parts
are needed for the basic build: the board's own BOOT button is the "press to type"
button and its onboard RGB LED is the status light.

## Supported boards

| Requirement | Why |
|---|---|
| ESP32-S3 (not S2, C3, or the original ESP32) | The native USB-OTG controller types as a keyboard, and the project targets `esp32s3`. |
| 8 MB flash or more | The partition table is an 8 MB layout (two 3 MB app slots plus a vault partition). A 16 MB board runs the same image and leaves the tail unused. 4 MB boards are not supported. |
| A native USB port (GPIO19/20) wired to a connector | This is the port that talks to the computer as a keyboard. |
| A BOOT button on GPIO0 | Keyra uses it as the physical confirmation button. |
| An addressable RGB LED (WS2812) | Status light. Optional in practice: without one, everything works but you lose the visual feedback. |

PSRAM is optional. The firmware uses it when present (for example larger backup
restores) and boots without it.

**Reference board:** ESP32-S3-DevKitC-1 **N16R8** (16 MB flash, 8 MB octal PSRAM).
Other DevKitC-1 variants (N8, N8R2, N8R8, N16R8) and compatible clones should work;
reports are welcome.

## Pins

| Function | GPIO | Notes |
|---|---|---|
| Button | **0** (BOOT) | Active low, internal pull-up. The firmware never restarts while it is held low, because a reset with GPIO0 low would enter ROM download mode. |
| Status LED (WS2812) | **38** by default | ESP32-S3-DevKitC-1 **v1.1** uses GPIO38. **v1.0** boards use GPIO48. Change it with `idf.py menuconfig` under *Keyra I/O* → *Onboard WS2812 RGB LED GPIO* (`CONFIG_KEYRA_LED_GPIO`). |
| USB D- / D+ | 19 / 20 | The native USB port. Reserved by the USB peripheral. |

On boards with octal PSRAM (the N16R8 included), GPIO35 to GPIO37 belong to the
PSRAM, so they are not available for other uses.

### LED meanings

| Colour and pattern | Meaning |
|---|---|
| Dim red, slow breathing | Locked |
| Brief soft green blink every 4 s | Unlocked and idle |
| Blue, pulsing | A typing action is ready and waiting for the button |
| Violet, pulsing | Waiting for a button press to approve setup, a Wi-Fi change, Bluetooth pairing, a replacing restore or a factory reset |
| White, two short blinks per second | A website asks for a passkey or security key: short press approves, long press refuses (also shown briefly when a computer asks "which key is this?") |
| Cyan, pulsing | Bluetooth pairing window open (up to 2 minutes): pick Keyra in your device's Bluetooth settings |
| White, steady | Typing |
| Green flash | Typed successfully |
| Red flash | Error (expired, nothing connected to type into, character not typable) |
| Amber, slow breathing | First-time setup |

Exact patterns are defined in `firmware/components/keyra_io/` and are the source of truth.

## The two USB ports

Most ESP32-S3-DevKitC-1 boards have two USB-C connectors:

| Label | Chip | Use |
|---|---|---|
| **USB** | the S3's native USB (GPIO19/20) | **Plug this into the computer you want Keyra to type into.** Also usable for flashing. |
| **UART** | a USB-to-serial bridge (CP210x) | Flashing and the serial console. Does not make Keyra a keyboard. |

Power through either port. Over a cable, Keyra only types through the **USB** port.

## Bluetooth

The ESP32-S3's radio also runs Bluetooth LE, so Keyra can type into phones,
tablets and computers without a cable (pair it from **Settings → Bluetooth** in
the app). No extra hardware is needed. Notes:

- Wi-Fi (Keyra's access point) and Bluetooth share one 2.4 GHz radio and antenna.
  The firmware enables ESP-IDF software coexistence, which time-shares the radio,
  so Wi-Fi throughput can drop while Bluetooth is busy.
- Bluetooth adds about 220 KB to the firmware image (still under half of the
  3 MB app slot) and roughly 25 KB of static RAM, plus the Bluetooth controller's
  heap at run time. It works without PSRAM; the boot log prints the free internal
  heap ("Keyra up … internal heap free").
- Turning Bluetooth off in the app stops advertising and drops the link; the
  Bluetooth stack itself stays loaded.

## Flashing

### Over the UART port (easiest, first flash)

Plug the **UART** port into your computer, then:

```sh
cd firmware
idf.py -p <PORT> flash          # dev profile
```

`<PORT>` is `/dev/ttyUSB0` on Linux, `/dev/cu.usbserial-*` on macOS, `COMx` on Windows.
The bridge handles the reset sequence automatically.

### Over the native USB port

The native port has no auto-reset circuit, so you put the chip in ROM download mode:

1. Unplug the board.
2. Hold **BOOT**, plug in the **USB** port, release BOOT.
3. A serial port appears (`/dev/cu.usbmodem*`, `/dev/ttyACM0`, `COMx`).
4. `idf.py -p <PORT> flash`
5. Press **RESET** (or unplug and replug) to boot the app. Do not hold BOOT while you do.

If the app is already running a dev-profile build, you can skip the button dance (next section).

### Prebuilt images

Release assets (when published) include the bootloader, partition table, OTA data and
app image with offsets in `flasher_args.json`. Flash them with `esptool.py` (or
Espressif's web flasher) at those offsets.

## Dev hook: 1200-baud reboot to ROM

The **dev profile** (`CONFIG_KEYRA_DEV_CDC=y`, the default) exposes a USB CDC serial
port next to the keyboard. It does two things:

- mirrors the firmware log, because TinyUSB takes over the USB pins so the chip's built-in USB serial console cannot be used while Keyra runs;
- reboots into ROM download mode when a host opens the port at **1200 baud** and drops DTR (the same "touch" convention Arduino boards use).

```sh
# Example (pyserial): reboot a running dev build into ROM download mode
python3 -c "import serial; s=serial.Serial('<APP-CDC-PORT>', 1200); s.dtr=False; s.close()"
# a second port (the ROM bootloader) appears; flash it, then reset
```

`tools/devctl.py` wraps this flow (`dl`, `flash`, `run`, `log`). It is a maintainer
tool written for macOS serial port names and an esptool install path, so expect to
edit the constants at its top on other systems.

The **release profile** (`sdkconfig.release`) leaves the CDC interface out. The device
enumerates as a plain keyboard, logging is off, and the hook does not exist. To
re-flash a release build, use the BOOT-button method above.

## Enclosure ideas

There is no official enclosure yet. Ideas, roughly from easiest to most involved:

- **Bare board.** Perfectly usable, and the BOOT button is already accessible.
- **Sleeve or case for the DevKitC-1** (3D printed or off the shelf) with two openings: one for the USB port, one over the BOOT button. The button is tiny, so print a short plunger or use a rubber cap that presses it.
- **Light pipe.** A small clear pin or a 1.75 mm filament offcut guides the onboard LED to the case surface so the status colour is visible.
- **Capsule or stick form factor.** A custom PCB with a USB-C plug directly on the S3 module, a large tactile button on top and the LED next to it. Contributions welcome; open an issue first so the pin map stays compatible.
- **Tamper evidence.** A sticker or epoxy over the screws deters casual access. It does not replace flash encryption.

Keep the antenna area free: no metal over the end of the module that carries the PCB antenna.

## Optional hardening (irreversible)

> **WARNING. These steps permanently change one-time-programmable eFuses on your
> chip. They cannot be undone. A mistake can leave the board unflashable
> ("bricked"). Practise on a spare board. Keyra does not enable any of this by default,
> and it is not part of the project's tested configuration.**

What they buy: with **flash encryption** the flash contents (including your vault
files, salts and the failed-unlock counter) are encrypted with a key that lives
in eFuses and cannot be read out. Dumping the flash of a lost device then no longer yields
data you can attack offline with a fast PBKDF2 rig. **Secure boot** stops someone
flashing modified firmware that, for example, leaks the vault once you unlock it.

What they cost:

- Plain `idf.py flash` over serial stops working (images must be encrypted or signed).
- The 1200-baud ROM hook and `tools/devctl.py flash` stop working.
- In "Release" modes, ROM download mode can be restricted or disabled, which also removes your recovery path.
- Keyra's `vault` partition would need the `encrypted` flag in `partitions.csv`, and the NVS partition (it holds the unlock counter) needs NVS encryption. Neither is validated for Keyra yet.

If you still want to go ahead, read Espressif's documents first and follow them exactly:

1. Read **Flash Encryption** and **Secure Boot v2** in the ESP-IDF Programming Guide for ESP32-S3 (for your exact IDF version).
2. Confirm your chip's current state: `espefuse.py --chip esp32s3 summary`.
3. First try **Development mode** flash encryption on a spare board. It still permits a limited number of re-flashes.
4. Only after you have rehearsed the whole update and recovery process, consider **Release mode**, which locks it permanently.
5. Back up your vault (**Backup & restore** in the web app) before any step.
6. Never lose the signing key for secure boot; without it you can never update the firmware.

You are responsible for your own hardware. The Keyra project cannot recover a board
you have locked.

## Troubleshooting

| Symptom | Try |
|---|---|
| Board not detected for flashing | Use the **UART** port, or enter ROM mode by holding BOOT while plugging in. Use a data cable, not a charge-only cable. |
| Computer sees no keyboard | The **USB** port must be connected to the computer, not the UART port. |
| Browser says "use your security key" but the light does not double-blink | Keyra must be plugged into the computer by USB (Bluetooth cannot carry passkeys). If Keyra is locked, unlock it on the phone within 30 s. |
| macOS shows "Keyboard Setup Assistant" | Expected the first time any new keyboard connects; closing it is fine. Keyra types US layout regardless of the answer you choose. |
| LED is the wrong colour or dark | Your board is probably v1.0: set `KEYRA_LED_GPIO` to 48 and rebuild. |
| Board stuck in download mode after reset | BOOT (GPIO0) was held low at reset. Release it and reset again. |
| Keyra does not appear in a phone's Bluetooth list | Open the pairing window first (**Settings → Bluetooth → Pair a new device**, then press the button; LED pulses cyan). Outside the window Keyra is invisible to new devices. |
| A paired device stopped connecting | Forget Keyra on that device and forget the device in Keyra's app, then pair again. |
| "Pair a new device" is greyed out | Keyra remembers up to 4 devices. Forget one first. |
| iPhone/iPad on-screen keyboard disappeared | iOS hides it while a hardware keyboard is connected. With **Connect: When typing** (default) Keyra lets go about 20 s after typing; with **Always** set it back to When typing, or tap the keyboard button at the bottom of the screen. |
| Ready says "Connecting to …" for a long time | The chosen device is off, asleep with Bluetooth off, or out of range. Wake it and keep it near Keyra; after 60 s the action ends with "Nothing to type into". |
