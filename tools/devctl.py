"""Control the DrPasswords board over its native USB port.

  devctl.py dl                 -> app CDC 1200-baud touch -> ROM download mode (/dev/cu.usbmodem101)
  devctl.py run [secs]         -> watchdog reset from ROM into the app, then capture app log for secs
  devctl.py flash <dir> [secs] -> dl, write bins from <dir>/flash_files.txt (offset path per line), run+log
  devctl.py log <secs>         -> read app CDC log without resetting
"""
import glob, subprocess, sys, time
import serial

PY = sys.executable
ET = __import__("os").path.expanduser("~/.platformio/packages/tool-esptoolpy/esptool.py")
ROM = "/dev/cu.usbmodem101"


def app_port():
    ports = [p for p in glob.glob("/dev/cu.usbmodem*") if p != ROM]
    return ports[0] if ports else None


def wait(pred, secs):
    end = time.time() + secs
    while time.time() < end:
        v = pred()
        if v:
            return v
        time.sleep(0.1)
    return None


def to_dl():
    if glob.glob(ROM):
        print("dl-ok")
        return True
    p = app_port()
    if p:
        try:
            s = serial.Serial(p, 1200)
            s.dtr = False
            time.sleep(0.3)
            s.close()
        except Exception as e:
            print("touch error", e)
    ok = wait(lambda: glob.glob(ROM), 20)
    print("dl-ok" if ok else "dl-fail")
    return bool(ok)


def esptool(*args):
    r = subprocess.run([PY, ET, "--chip", "esp32s3", "-p", ROM, "--before", "no_reset", *args],
                       capture_output=True, text=True)
    out = (r.stdout + r.stderr).strip().splitlines()
    print("\n".join(out[-4:]))
    return r.returncode == 0


def read_log(port, secs):
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.2
    s.open()
    s.dtr = True  # TinyUSB CDC drops writes unless DTR is asserted
    buf = b""
    end = time.time() + secs
    while time.time() < end:
        try:
            buf += s.read(4096)
        except serial.SerialException:
            time.sleep(0.2)
    s.close()
    sys.stdout.write(buf.decode("utf-8", "replace"))


def run(secs):
    esptool("--after", "watchdog_reset", "chip_id")
    p = wait(app_port, 40)
    print("run-ok", p if p else "run-fail")
    if p and secs > 0:
        time.sleep(0.3)
        read_log(p, secs)


def flash(d, secs):
    if not to_dl():
        sys.exit(1)
    args = []
    for line in open(f"{d}/flash_files.txt"):
        line = line.strip()
        if line:
            off, path = line.split(None, 1)
            args += [off, path if path.startswith("/") else f"{d}/{path}"]
    if not esptool("--after", "no_reset", "write_flash", "-z", "--flash_size", "keep", *args):
        print("FLASH FAILED")
    run(secs)


cmd = sys.argv[1]
if cmd == "dl":
    to_dl()
elif cmd == "run":
    run(float(sys.argv[2]) if len(sys.argv) > 2 else 0)
elif cmd == "flash":
    flash(sys.argv[2], float(sys.argv[3]) if len(sys.argv) > 3 else 12)
elif cmd == "log":
    p = app_port()
    read_log(p, float(sys.argv[2]))
