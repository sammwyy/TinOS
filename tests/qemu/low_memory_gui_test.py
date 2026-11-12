#!/usr/bin/env python3
"""Exercise the desktop and a ring-3 menu program with 8 MiB of RAM."""

import argparse
import os
import time

from qemu_test import Screen, Tinos


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--iso", default="build/tinos.iso")
    args = parser.parse_args()

    vm = Tinos(args.iso, mem=8, verbose=False)
    try:
        vm.start()
        assert vm.wait_for_serial("desktop starting", timeout=30), vm.serial()
        assert "userland: imported " in vm.serial(), vm.serial()
        assert "native app(s)" not in vm.serial(), vm.serial()
        vm.key("ctrl", "esc")
        time.sleep(0.4)
        path = os.path.join(vm.tmp, "programs.png")
        vm.screenshot(path)
        screen = Screen(path)
        assert screen.size in ((640, 480), (800, 600), (1024, 768)), screen.size
        assert screen.has("Programs"), "start menu not visible"
        matches = screen.find("terminal.elf")
        assert matches, "ring-3 program not visible"
        x, y = matches[0]
        vm.mouse_click(x + 20, y + 6)
        assert vm.wait_for_serial("opened window 1 'Terminal'",
                                  timeout=15), vm.serial()
        vm.line("ls /")
        time.sleep(0.4)
        vm.screenshot(path)
        assert Screen(path).has("README.txt"), "terminal cannot read directory"
        vm.line("run /apps/echo.elf")
        time.sleep(0.4)
        vm.screenshot(path)
        assert Screen(path).has("ring3: type a line"), "child output not visible"
        vm.line("eight megabytes")
        time.sleep(0.4)
        vm.screenshot(path)
        assert Screen(path).has("ring3 echo: eight megabytes"), "child input failed"
        vm.key("alt", "f4")
        assert vm.wait_for_serial("closed window 1 'Terminal'", timeout=15)
        vm.key("ctrl", "esc")
        time.sleep(0.4)
        vm.screenshot(path)
        matches = Screen(path).find("image_viewer.elf")
        assert matches, "image viewer missing at 8 MiB"
        x, y = matches[0]
        vm.mouse_click(x + 20, y + 6)
        assert vm.wait_for_serial("opened window 2 'Image Viewer'",
                                  timeout=15), vm.serial()
        time.sleep(0.4)
        vm.screenshot(path)
        assert Screen(path).has("BMP 24/32-bit"), "image viewer failed at 8 MiB"
        vm.key("alt", "f4")
        assert vm.wait_for_serial("closed window 2 'Image Viewer'", timeout=15)
        vm.key("ctrl", "esc")
        time.sleep(0.4)
        vm.screenshot(path)
        matches = Screen(path).find("media_player.elf")
        assert matches, "media player missing at 8 MiB"
        x, y = matches[0]
        vm.mouse_click(x + 20, y + 6)
        assert vm.wait_for_serial("opened window 3 'Media Player'",
                                  timeout=15), vm.serial()
        assert vm.wait_for_serial("opened stereo PCM at 22050 Hz",
                                  timeout=15), vm.serial()
        assert "KERNEL PANIC" not in vm.serial()
        print("8 MiB desktop and ring-3 menu launch: OK")
    finally:
        vm.stop()
        vm.cleanup()


if __name__ == "__main__":
    main()
