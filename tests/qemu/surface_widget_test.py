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
             "explorer": "File Explorer",
             "control_panel": "Control Panel"}[name]
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
        assert any(300 <= x <= 334 and 72 <= y <= 110
                   for x, y in screen.find("42")), "calculator display is wrong"
        assert screen.has("BS") and screen.has("C")
        assert sum(screen.image.getpixel((66, 82))) + 150 < \
               sum(screen.image.getpixel((66, 128))), \
               "calculator lost its dark display and raised keypad"
        vm.mouse_click(92, 135)
        screen = capture(vm, path)
        assert any(300 <= x <= 334 and 72 <= y <= 110
                   for x, y in screen.find("0")), "C key did not clear display"
        vm.type("10+7")
        vm.key("ret")
        screen = capture(vm, path)
        assert any(300 <= x <= 334 and 72 <= y <= 110
                   for x, y in screen.find("17")), "calculator lost keyboard input"
        vm.mouse_drag(340, 332, 410, 375)
        screen = capture(vm, path)
        assert screen.has("17"), \
            "layout lost data on resize"
        vm.key("alt", "f4")
        assert vm.wait_for_serial("closed window 3 'Calculator'", timeout=15)

        launch(vm, path, "about", 4)
        screen = capture(vm, path)
        assert screen.has("TinOS") and screen.has("desktop")
        assert screen.image.getpixel((63, 75)) != \
               screen.image.getpixel((63, 125)), "old gradient is missing"
        vm.key("alt", "f4")
        assert vm.wait_for_serial("closed window 4 'About TinOS'",
                                  timeout=15), vm.serial()

        launch(vm, path, "explorer", 5)
        screen = capture(vm, path)
        assert screen.has("Home Folder") and screen.has("Location:") \
               and screen.has("/users/sammy"), \
               "original explorer HUD is missing"
        assert screen.image.getpixel((62, 210)) != \
               screen.image.getpixel((240, 210)), \
               "explorer lost its sidebar and file pane"
        vm.key("backspace")
        vm.key("backspace")
        assert capture(vm, path).has("README.txt"), "cannot reach filesystem root"
        vm.key("ret")
        assert capture(vm, path).has("/README.txt"), "file preview did not open"
        vm.key("esc")
        assert capture(vm, path).has("README.txt"), "preview did not close"
        vm.key("down")
        vm.key("ret")
        screen = capture(vm, path)
        assert screen.has("/apps"), "directory navigation failed"
        if args.mem >= 16:
            vm.mouse_drag(740, 464, 785, 494)
        else:
            vm.mouse_drag(520, 347, 568, 367)
        assert capture(vm, path).has("/apps"), "directory lost after resize"
        vm.key("alt", "f4")
        assert vm.wait_for_serial("closed window 5 'File Explorer'",
                                  timeout=15), vm.serial()
        launch(vm, path, "control_panel", 6)
        vm.type("2")
        screen = capture(vm, path)
        assert screen.has("Keyboard") and screen.has("Layout") and \
               screen.has("Spanish"), "settings sidebar collapsed at low memory"
        number = 7 if "opened window 7 'Control Panel'" in vm.serial() else 6
        vm.key("alt", "f4")
        assert vm.wait_for_serial(f"closed window {number} 'Control Panel'",
                                  timeout=15), vm.serial()
        assert "KERNEL PANIC" not in vm.serial()
        print(f"{args.mem} MiB ring-3 surfaces, widgets and resize: OK")
    finally:
        vm.stop()
        vm.cleanup()


if __name__ == "__main__":
    main()
