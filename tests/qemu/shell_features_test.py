#!/usr/bin/env python3
"""Exercise the desktop shell: shortcuts, desktop icons, context menus,
the tray calendar and Task Manager's process list and End task."""

import argparse
import os
import re
import time

from qemu_test import Screen, Tinos


def shot(vm, name="shell.png", settle=0.4):
    time.sleep(settle)
    path = os.path.join(vm.tmp, name)
    vm.screenshot(path)
    return Screen(path)


def wait_window(vm, title, count=1, timeout=15):
    pattern = f"opened window \\d+ '{re.escape(title)}'"
    deadline = time.time() + timeout
    while len(re.findall(pattern, vm.serial())) < count:
        assert time.time() < deadline, vm.serial()
        time.sleep(0.1)
    time.sleep(0.4)


def wait_text(vm, text, region=None, present=True, tries=12):
    for _ in range(tries):
        if shot(vm, settle=0.3).has(text, region=region) == present:
            return True
    return False


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--iso", default="build/tinos.iso")
    parser.add_argument("--mem", type=int, default=16)
    args = parser.parse_args()
    vm = Tinos(args.iso, mem=args.mem, verbose=False)
    try:
        vm.start()
        assert vm.wait_for_serial("desktop starting", timeout=30), vm.serial()
        desk = shot(vm, settle=1.0)
        width, height = desk.size
        for label in ("Files", "Terminal", "Settings"):
            assert desk.has(label), "missing desktop shortcut " + label

        # Desktop context menu.
        vm.mouse_click(width - 300, height // 2, "right")
        assert wait_text(vm, "Show desktop"), "desktop context menu missing"
        vm.key("esc")
        assert wait_text(vm, "Show desktop", present=False), "menu stayed open"

        # Double-click a desktop shortcut.
        hits = desk.find("Terminal", region=(0, 0, 120, height))
        assert hits, "terminal shortcut not found"
        x, y = hits[0]
        vm.mouse_move(x + 20, y - 20)
        vm.mouse_button(True)
        vm.mouse_button(False)
        time.sleep(0.05)
        vm.mouse_button(True)
        vm.mouse_button(False)
        wait_window(vm, "Terminal")

        # Task Manager is single instance.
        vm.key("ctrl", "shift", "esc")
        wait_window(vm, "Task Manager")
        assert wait_text(vm, "process slots in use"), "task manager header"
        vm.key("ctrl", "shift", "esc")
        time.sleep(1.0)
        assert len(re.findall(r"opened window \d+ 'Task Manager'",
                              vm.serial())) == 1, "task manager opened twice"
        screen = shot(vm, settle=1.2)
        assert screen.has("running") and screen.has("waiting"), \
            "process list missing"

        # Task Manager starts on its own row; the terminal (pid 1) is above.
        vm.key("up")
        vm.key("delete")
        assert vm.wait_for_serial("[proc] killed pid 1", timeout=10), vm.serial()
        assert vm.wait_for_serial("closed window 1 'Terminal'", timeout=10), \
            vm.serial()

        # Win+T opens a new terminal; Win+D hides and restores windows.
        vm.key("meta_l", "t")
        wait_window(vm, "Terminal", count=2)
        assert wait_text(vm, "tinos:/"), "terminal prompt missing"
        vm.key("meta_l", "d")
        assert wait_text(vm, "tinos:/", present=False), \
            "Win+D did not show the desktop"
        vm.key("meta_l", "d")
        assert wait_text(vm, "tinos:/"), "Win+D did not restore the windows"

        # The clock opens a calendar with the weekday header.
        vm.mouse_click(width - 30, height - 14)
        days = ("Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
                "Saturday", "Sunday")
        calendar = shot(vm, settle=0.6)
        assert any(calendar.has(d) for d in days) and calendar.has("Mo"), \
            "calendar popup missing"
        vm.key("esc")
        assert "KERNEL PANIC" not in vm.serial()
        print(f"{args.mem} MiB desktop shell, shortcuts and Task Manager: OK")
    finally:
        vm.stop()
        vm.cleanup()


if __name__ == "__main__":
    main()
