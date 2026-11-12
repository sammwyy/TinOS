#!/usr/bin/env python3
"""Exercise raw PCM formats and blocking drain from a ring-3 process."""

import argparse

from qemu_test import Tinos


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--iso", default="build/tinos.iso")
    parser.add_argument("--mem", type=int, default=8)
    args = parser.parse_args()

    vm = Tinos(args.iso, mem=args.mem, verbose=False)
    try:
        vm.start()
        vm.key("down")
        vm.key("down")
        vm.key("ret")
        assert vm.wait_for_serial("[tty] text console ready", timeout=30), vm.serial()
        vm.line("run /apps/pcm_demo.elf")
        assert vm.wait_for_serial("ring3: PCM U8/S16 mono/stereo drained",
                                  timeout=30), vm.serial()
        assert vm.serial().count("waiting for PCM queue") >= 4, vm.serial()
        assert vm.serial().count("opened stereo PCM at 22050 Hz") == 4, vm.serial()
        assert "[proc] ring 3 exit status 0" in vm.serial()
        vm.line("run /apps/hello.elf")
        assert vm.wait_for_serial("ring3: hello from an isolated ELF",
                                  timeout=15), vm.serial()
        assert "KERNEL PANIC" not in vm.serial()
        print(f"{args.mem} MiB raw U8/S16 mono/stereo PCM and "
              "blocking drain: OK")
    finally:
        vm.stop()
        vm.cleanup()


if __name__ == "__main__":
    main()
