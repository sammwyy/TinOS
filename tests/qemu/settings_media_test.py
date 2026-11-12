#!/usr/bin/env python3
"""Exercise settings actions and a two-file userspace PCM playlist."""

import argparse

from actions_test import entry_matches, launch, shot, wait_text
from qemu_test import Tinos


def click_text(vm, text, region):
    matches = shot(vm).find(text, region=region, tolerance=10)
    assert matches, f"missing {text!r}"
    x, y = matches[0]
    vm.mouse_click(x + 5, y + 6)


def settings(vm):
    panel = launch(vm, "control_panel", "Control Panel")
    assert wait_text(vm, "Network"), "settings category missing"
    click_text(vm, "Sound", (48, 62, 250, 525))
    click_text(vm, "Output", (48, 62, 250, 525))
    assert wait_text(vm, "Output volume"), "sound controls are missing"
    click_text(vm, "Mute", (260, 130, 740, 465))
    assert wait_text(vm, "Unmute"), "mute did not change the audio device"
    click_text(vm, "Unmute", (260, 130, 740, 465))
    assert wait_text(vm, "Mute"), "unmute did not restore the audio device"
    before = shot(vm).image.crop((285, 263, 675, 280)).tobytes()
    vm.type("+")
    after = shot(vm).image.crop((285, 263, 675, 280)).tobytes()
    assert before != after, "volume control did not update"

    click_text(vm, "Display", (48, 62, 250, 525))
    click_text(vm, "Appearance", (48, 62, 250, 525))
    click_text(vm, "Solid", (260, 130, 740, 465))
    solid = shot(vm).image.getpixel((920, 330))
    click_text(vm, "Gradient", (260, 130, 740, 465))
    gradient = shot(vm).image.getpixel((920, 330))
    assert solid != gradient, "wallpaper controls did not update the desktop"
    click_text(vm, "Default", (260, 130, 740, 465))
    click_text(vm, "Resolution", (48, 62, 250, 525))
    click_text(vm, "800 x 600", (260, 130, 740, 465))
    assert shot(vm).size == (800, 600), "display mode did not change"
    click_text(vm, "1024 x 768", (260, 130, 740, 525))
    assert shot(vm).size == (1024, 768), "display mode was not restored"

    click_text(vm, "Network", (48, 62, 250, 525))
    click_text(vm, "IP Configuration", (48, 62, 250, 525))
    assert wait_text(vm, "IP address"), "IPv4 settings are not editable"
    vm.mouse_click(392, 190)
    vm.type("256.0.0.1")
    vm.mouse_click(330, 355)
    assert wait_text(vm, "Invalid IPv4 address"), \
        "invalid IPv4 input did not get feedback"
    vm.key("alt", "f4")
    assert vm.wait_for_serial(f"closed window {panel} 'Control Panel'",
                              timeout=15), vm.serial()


def playlist(vm):
    terminal = launch(vm, "terminal", "Terminal")
    vm.line("run /apps/actionsprobe.elf --playlist")
    assert wait_text(vm, "playlist ready"), "test WAV copies were not created"
    vm.key("alt", "f4")
    assert vm.wait_for_serial(f"closed window {terminal} 'Terminal'",
                              timeout=15), vm.serial()

    explorer = launch(vm, "explorer", "File Explorer")
    matches = entry_matches(shot(vm), "one.wav", 16)
    assert matches, "copied WAV is not visible in Explorer"
    x, y = matches[0]
    vm.mouse_click(x + 5, y + 6)
    vm.key("ret")
    assert vm.wait_for_serial("'Media Player'", timeout=15), vm.serial()
    assert wait_text(vm, "Playlist"), "playlist HUD is missing"
    assert wait_text(vm, "one.wav"), "first WAV not selected"
    assert wait_text(vm, "two.wav"), "second WAV not listed"
    vm.key("r")
    vm.key("spc")
    assert wait_text(vm, "Paused"), "transport did not pause PCM"
    vm.type("]")
    assert wait_text(vm, "two.wav"), "next track did not update metadata"
    vm.key("spc")
    assert wait_text(vm, "Paused"), "new track could not pause"
    vm.mouse_click(376, 326)
    progress = shot(vm).image
    assert progress.getpixel((320, 326)) != \
           progress.getpixel((440, 326)), \
           "timeline seek did not change position"
    vm.key("alt", "f4")
    vm.key("alt", "f4")
    assert vm.wait_for_serial(f"closed window {explorer} 'File Explorer'",
                              timeout=15), vm.serial()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--iso", default="build/tinos.iso")
    args = parser.parse_args()
    vm = Tinos(args.iso, mem=16, verbose=False)
    try:
        vm.start()
        assert vm.wait_for_serial("desktop starting", timeout=30), vm.serial()
        settings(vm)
        playlist(vm)
        assert "KERNEL PANIC" not in vm.serial(), vm.serial()
        print("16 MiB userspace settings and PCM playlist: OK")
    finally:
        vm.stop()
        vm.cleanup()


if __name__ == "__main__":
    main()
