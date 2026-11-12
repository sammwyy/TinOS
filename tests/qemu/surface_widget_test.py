#!/usr/bin/env python3
"""Exercise bounded graphics, reactive widgets and resize in ring 3."""

import argparse
import os
import time

from qemu_test import Screen, Tinos


def capture(vm, path):
    time.sleep(0.3)
    vm.screenshot(path)
    return Screen(path)


def launch(vm, path, name, number):
    vm.key("ctrl", "esc")
    menu = capture(vm, path)
    matches = menu.find(name + ".elf")
    assert matches, vm.serial()
    x, y = matches[0]
    vm.mouse_click(x + 20, y + 6)
    title = {"calculator": "Calculator", "about": "About TinOS",
             "explorer": "File Explorer"}[name]
    assert vm.wait_for_serial(f"opened window {number} '{title}'",
                              timeout=15), vm.serial()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--iso", default="build/tinos.iso")
    parser.add_argument("--mem", type=int, default=8)
    args = parser.parse_args()
    vm = Tinos(args.iso, mem=args.mem, verbose=False)
    try:
        vm.start()
        assert vm.wait_for_serial("desktop starting", timeout=30), vm.serial()
        path = os.path.join(vm.tmp, "surface.png")
        vm.key("ctrl", "esc")
        menu = capture(vm, path)
        matches = menu.find("terminal.elf")
        assert matches
        x, y = matches[0]
        vm.mouse_click(x + 20, y + 6)
        assert vm.wait_for_serial("opened window 1 'Terminal'", timeout=15)
        vm.line("run /apps/surfaceprobe.elf")
        assert vm.wait_for_serial("opened window 2 'Surface test'",
                                  timeout=15), vm.serial()
        assert vm.wait_for_serial("closed window 2 'Surface test'",
                                  timeout=15), vm.serial()
        assert capture(vm, path).has("surface bounds and theme isolated")
        vm.key("alt", "f4")
        assert vm.wait_for_serial("closed window 1 'Terminal'", timeout=15)

        launch(vm, path, "calculator", 3)
        vm.type("6*7")
        vm.key("ret")
        screen = capture(vm, path)
        assert screen.has("Result") and screen.has("42")
        clear = screen.find("Clear")
        assert clear
        vm.mouse_click(clear[0][0] + 10, clear[0][1] + 5)
        assert capture(vm, path).has("Result: 0"), "button did not update signal"
        vm.mouse_click(100, 180)
        vm.type("10+7")
        vm.key("ret")
        assert capture(vm, path).has("17")
        vm.mouse_drag(440, 285, 500, 318)
        screen = capture(vm, path)
        assert screen.has("Result") and screen.has("17"), \
            "layout lost data on resize"
        vm.key("alt", "f4")
        assert vm.wait_for_serial("closed window 3 'Calculator'", timeout=15)

        launch(vm, path, "about", 4)
        screen = capture(vm, path)
        assert screen.has("SYSTEM") and screen.has("ring 3")
        close = screen.find("Close")
        assert close
        vm.mouse_click(close[0][0] + 8, close[0][1] + 5)
        assert vm.wait_for_serial("closed window 4 'About TinOS'",
                                  timeout=15), vm.serial()

        launch(vm, path, "explorer", 5)
        screen = capture(vm, path)
        assert screen.has("FILES") and screen.has("README.txt")
        banner = screen.image.getpixel((63, 75))
        panel = screen.image.getpixel((63, 110))
        assert sum(panel) > sum(banner) + 200, \
            "themed pixels were not copied into the client surface"
        open_button = screen.find("Open")
        assert open_button
        vm.mouse_click(open_button[0][0] + 10, open_button[0][1] + 5)
        assert capture(vm, path).has("/README.txt"), "file preview did not open"
        vm.key("esc")
        assert capture(vm, path).has("README.txt"), "preview did not close"
        vm.key("down")
        vm.key("ret")
        screen = capture(vm, path)
        assert screen.has("/apps"), "directory navigation failed"
        vm.mouse_drag(520, 347, 568, 367)
        assert capture(vm, path).has("/apps"), "directory lost after resize"
        vm.key("alt", "f4")
        assert vm.wait_for_serial("closed window 5 'File Explorer'",
                                  timeout=15), vm.serial()
        assert "KERNEL PANIC" not in vm.serial()
        print(f"{args.mem} MiB ring-3 surfaces, widgets and resize: OK")
    finally:
        vm.stop()
        vm.cleanup()


if __name__ == "__main__":
    main()
