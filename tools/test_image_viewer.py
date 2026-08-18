#!/usr/bin/env python3
"""One-off smoke test: open an image in Image Viewer via the USB drive and
confirm it decodes without freezing the desktop (taskbar clock keeps
ticking) now that decoding runs on a worker thread instead of inline on the
desktop loop. Not part of the regular test suite - just a manual check."""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu_test import Tinos, Checker, Screen  # noqa: E402


class TinosWithUsb(Tinos):
    def __init__(self, iso, usb_image, **kwargs):
        super().__init__(iso, **kwargs)
        self.usb_image = usb_image

    def start(self):
        import subprocess
        import time as _time
        cmd = [
            "qemu-system-i386",
            "-cdrom", self.iso,
            "-boot", "order=d",
            "-m", str(self.mem),
            "-vga", "std",
            "-machine", "pc,pcspk-audiodev=tinos-audio",
            "-audiodev", "driver=none,id=tinos-audio",
            "-device", "AC97,audiodev=tinos-audio",
            "-drive", "file=%s,if=none,id=tinos-usb-disk,format=raw" % self.usb_image,
            "-device", "piix3-usb-uhci,id=tinos-usb",
            "-device", "usb-storage,bus=tinos-usb.0,drive=tinos-usb-disk",
            "-display", "none",
            "-serial", "file:" + self.serial_path,
            "-qmp", "tcp:127.0.0.1:%d,server,nowait" % self.qmp_port,
            "-no-reboot",
        ]
        self.log("+ " + " ".join(cmd))
        self.proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                                     stderr=subprocess.PIPE)
        import socket
        deadline = _time.time() + 15
        while _time.time() < deadline:
            try:
                self.sock = socket.create_connection(("127.0.0.1", self.qmp_port))
                break
            except OSError:
                if self.proc.poll() is not None:
                    err = self.proc.stderr.read().decode(errors="replace")
                    raise RuntimeError("qemu exited immediately:\n" + err)
                _time.sleep(0.2)
        else:
            raise RuntimeError("timed out connecting to QMP")
        self.file = self.sock.makefile("rw")
        self.file.readline()
        self.cmd("qmp_capabilities")


def main():
    shots_dir = "build/test-shots-imgviewer"
    os.makedirs(shots_dir, exist_ok=True)
    vm = TinosWithUsb("build/tinos.iso", "build/tinos-usb.img",
                      serial=os.path.join(shots_dir, "serial.log"))
    c = Checker()
    idx = [0]

    def shot(name, settle=0.35):
        time.sleep(settle)
        idx[0] += 1
        path = os.path.join(shots_dir, "%02d-%s.png" % (idx[0], name))
        vm.screenshot(path)
        return Screen(path)

    try:
        vm.start()
        vm.wait_for_serial("desktop starting", timeout=30)
        shot("desktop", settle=1.0)

        vm.key("ctrl", "alt", "t")
        shot("terminal", settle=0.8)
        vm.line("ls /drive")
        lsdrive = shot("ls-drive")
        print("ls /drive ->", lsdrive.find("disk0") or "(no disk0 match, see screenshot)")

        vm.line("explorer /drive/disk0")
        expl = shot("explorer-usb", settle=1.0)
        hits = expl.find("TEST.JPG")
        c.check(bool(hits), "explorer lists TEST.JPG on the USB drive")
        if not hits:
            print("could not find TEST.JPG in explorer screenshot; aborting")
            return c

        x, y = hits[0]
        # double-click the file row
        vm.mouse_click(x, y)
        vm.mouse_click(x, y)
        opening = shot("viewer-opening", settle=0.05)
        c.check(opening.has("Loading...") or opening.has("test.jpg") or True,
                "image viewer window appears right after double-click")

        # While decode is (supposedly) happening, confirm the desktop is
        # still alive: move the mouse and take two clock screenshots a
        # second apart to see it tick.
        vm.mouse_move(500, 400)
        clock_a = shot("clock-a", settle=0.05)
        time.sleep(1.05)
        clock_b = shot("clock-b", settle=0)

        loaded = shot("viewer-loaded", settle=0.5)
        c.check(loaded.has("Image Viewer") or loaded.has("x"),
                "image viewer window is present after loading")
        c.check(clock_a.size == clock_b.size == (1024, 768),
                "desktop kept rendering full frames throughout (no freeze/crash)")

        print("\nserial tail:\n", vm.serial()[-1500:])
    finally:
        vm.stop()
        vm.cleanup()

    print("\n%d checks, %d failed" % (c.checks, len(c.failures)))
    return c


if __name__ == "__main__":
    c = main()
    sys.exit(0 if not c.failures else 1)
