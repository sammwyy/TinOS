#!/usr/bin/env python3
"""Exercise ring-3 file actions, editor history and copied GUI input."""

import argparse
import os
import re
import time

from qemu_test import Screen, Tinos


def shot(vm):
    time.sleep(0.35)
    path = os.path.join(vm.tmp, "actions.png")
    vm.screenshot(path)
    return Screen(path)

def wait_text(vm, text, present=True):
    for _ in range(10):
        if shot(vm).has(text, tolerance=12) == present:
            return True
    return False

def entry_matches(screen, name, mem):
    region = (200, 112, 745, 446) if mem >= 16 else (170, 112, 405, 249)
    return screen.find(name, tolerance=12, region=region)


def wait_entry(vm, name, mem, present=True):
    for _ in range(10):
        if bool(entry_matches(shot(vm), name, mem)) == present:
            return True
    return False

def wait_delete_prompt(vm, window_height):
    status_y = 40 + window_height - 24
    for _ in range(12):
        image = shot(vm).image
        red = 0
        for y in range(status_y, status_y + 22):
            for x in range(54, 402):
                r, g, b = image.getpixel((x, y))
                if r > g + 60 and r > b + 60:
                    red += 1
        if red > 30:
            return True
    return False


def launch(vm, name, title):
    pattern = rf"opened window (\d+) '{re.escape(title)}'"
    before = len(re.findall(pattern, vm.serial()))
    vm.key("ctrl", "esc")
    matches = shot(vm).find(name + ".elf")
    assert matches, "missing %s in desktop menu" % name
    vm.mouse_click(matches[0][0] + 20, matches[0][1] + 6)
    deadline = time.time() + 15
    while len(re.findall(pattern, vm.serial())) <= before:
        assert time.time() < deadline, vm.serial()
        time.sleep(0.1)
    time.sleep(0.4)
    return int(re.findall(pattern, vm.serial())[-1])


def gui(iso, mem):
    vm = Tinos(iso, mem=mem, verbose=False)
    try:
        vm.start()
        assert vm.wait_for_serial("desktop starting", timeout=30), vm.serial()
        explorer_id = launch(vm, "explorer", "File Explorer")
        dimensions = re.findall(
            rf"opened window {explorer_id} 'File Explorer' \((\d+)x(\d+)\)",
            vm.serial())
        assert dimensions, vm.serial()
        explorer_height = int(dimensions[-1][1])
        vm.key("ctrl", "n")
        vm.type("Actions")
        vm.key("ret")
        assert wait_text(vm, "Actions"), "new folder was not created"
        vm.key("ctrl", "t")
        vm.type("memo.txt")
        vm.key("ret")
        screen = shot(vm)
        if not entry_matches(screen, "memo.txt", mem):
            assert wait_entry(vm, "memo.txt", mem), "new file was not created"
            screen = shot(vm)
        if mem >= 16:
            matches = entry_matches(screen, "memo.txt", mem)
            assert matches, "memo file disappeared before editor launch"
            x, y = matches[0]
            vm.mouse_click(x + 5, y + 6)
            before = len(re.findall(r"opened window \d+ 'Text Editor'",
                                    vm.serial()))
            vm.key("e")
            deadline = time.time() + 15
            while len(re.findall(r"opened window \d+ 'Text Editor'",
                                 vm.serial())) <= before:
                assert time.time() < deadline, vm.serial()
                time.sleep(0.1)
            editor_id = int(re.findall(r"opened window (\d+) 'Text Editor'",
                                       vm.serial())[-1])
            assert shot(vm).has("memo.txt"), "editor was not opened from explorer"
            vm.key("alt", "f4")
            assert vm.wait_for_serial(f"closed window {editor_id} 'Text Editor'",
                                      timeout=15), vm.serial()
        screen = shot(vm)
        if mem < 16:
            matches = entry_matches(screen, "memo.txt", mem)
            assert matches, "memo file disappeared before copy"
            x, y = matches[0]
            vm.mouse_click(x + 5, y + 6)
        else:
            assert screen.has("File Explorer"), "explorer did not resume"
        vm.key("ctrl", "c")
        vm.key("ctrl", "v")
        assert wait_entry(vm, "memo.txt-copy", mem), \
            "file copy did not create a sibling"
        screen = shot(vm)
        x, y = entry_matches(screen, "memo.txt-copy", mem)[0]
        vm.mouse_click(x + 5, y + 6)
        vm.key("f2")
        vm.key("ctrl", "a")
        vm.type("renamed.txt")
        vm.key("ret")
        vm.key("up")
        assert wait_entry(vm, "renamed.txt", mem), \
            "file rename did not update the list"
        vm.key("down")
        vm.key("delete")
        assert wait_delete_prompt(vm, explorer_height), \
            "deletion requires confirmation"
        vm.key("esc")
        vm.key("up")
        assert wait_entry(vm, "renamed.txt", mem), \
            "cancelled deletion removed the file"
        vm.key("down")
        vm.key("delete")
        vm.key("ret")
        assert wait_entry(vm, "renamed.txt", mem, False), \
            "confirmed deletion failed"
        vm.key("alt", "f4")
        assert vm.wait_for_serial(f"closed window {explorer_id} 'File Explorer'",
                                  timeout=15), vm.serial()
        launch(vm, "editor", "Text Editor")
        vm.type("hello TinOS")
        vm.key("ctrl", "s")
        assert wait_text(vm, "hello TinOS"), "editor did not insert text"
        vm.key("ctrl", "a")
        vm.key("ctrl", "c")
        vm.key("ctrl", "n")
        vm.key("ctrl", "n")
        vm.key("ctrl", "v")
        assert wait_text(vm, "hello TinOS"), "clipboard paste did not restore text"
        vm.key("ctrl", "z")
        assert wait_text(vm, "hello TinOS", False), "undo did not remove paste"
        vm.key("ctrl", "y")
        assert wait_text(vm, "hello TinOS"), "redo did not restore paste"
        assert "KERNEL PANIC" not in vm.serial(), vm.serial()
        print(f"{mem} MiB ring-3 GUI actions: OK")
    finally:
        vm.stop()
        vm.cleanup()


def probe(iso):
    vm = Tinos(iso, mem=16, verbose=False)
    try:
        vm.start()
        vm.key("down")
        vm.key("down")
        vm.key("ret")
        assert vm.wait_for_serial("[tty] text console ready", timeout=30)
        vm.line("run /apps/actionsprobe.elf")
        assert vm.wait_for_serial("ring3: actions isolated", timeout=15), vm.serial()
        assert "KERNEL PANIC" not in vm.serial()
        print("Validated copy/move/seek/clipboard syscalls: OK")
    finally:
        vm.stop()
        vm.cleanup()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--iso", default="build/tinos.iso")
    parser.add_argument("--mem", type=int, default=8)
    parser.add_argument("--probe", action="store_true")
    args = parser.parse_args()
    if args.probe:
        probe(args.iso)
    else:
        gui(args.iso, args.mem)
