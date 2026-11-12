#!/usr/bin/env python3
"""Automated TinOS boot/GUI test driver.

Boots the ISO in a headless QEMU, drives it through the QMP monitor
(keystrokes, mouse, screenshots) and checks the serial log, so the desktop
can be exercised without a human at the keyboard.

    tests/qemu/qemu_test.py --iso build/tinos.iso            run the full scenario
    tests/qemu/qemu_test.py --legacy                         run the retired
                                                    ring-0 app scenario
    tests/qemu/qemu_test.py --shots-dir /tmp/shots          keep the screenshots
    tests/qemu/qemu_test.py --interactive 20                boot and idle, taking
                                                       one screenshot per second
"""

import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import screen_ocr  # noqa: E402  (same directory)

# --- character -> QEMU qcode ----------------------------------------------
BASE_KEYS = {
    " ": "spc", "\n": "ret", "\t": "tab",
    "-": "minus", "=": "equal", "[": "bracket_left", "]": "bracket_right",
    "\\": "backslash", ";": "semicolon", "'": "apostrophe",
    "`": "grave_accent", ",": "comma", ".": "dot", "/": "slash",
}
SHIFTED = {
    "!": "1", "@": "2", "#": "3", "$": "4", "%": "5", "^": "6", "&": "7",
    "*": "8", "(": "9", ")": "0", "_": "minus", "+": "equal",
    "{": "bracket_left", "}": "bracket_right", "|": "backslash",
    ":": "semicolon", '"': "apostrophe", "~": "grave_accent",
    "<": "comma", ">": "dot", "?": "slash",
}


def char_to_keys(ch):
    """Return the list of qcodes needed to type `ch`."""
    if ch.isalpha():
        return (["shift"] if ch.isupper() else []) + [ch.lower()]
    if ch.isdigit():
        return [ch]
    if ch in BASE_KEYS:
        return [BASE_KEYS[ch]]
    if ch in SHIFTED:
        return ["shift", SHIFTED[ch]]
    raise ValueError("cannot type %r" % ch)


class Tinos:
    def __init__(self, iso, mem=256, qmp_port=4455, serial=None, verbose=True,
                 vga="std", cpus=1):
        self.iso = iso
        self.mem = mem
        self.qmp_port = qmp_port
        self.tmp = tempfile.mkdtemp(prefix="tinostest-")
        self.serial_path = serial or os.path.join(self.tmp, "serial.log")
        self.proc = None
        self.sock = None
        self.file = None
        self.verbose = verbose
        self.cursor = None
        self.vga = vga
        self.cpus = cpus

    # -- lifecycle ---------------------------------------------------------
    def start(self):
        cmd = [
            "qemu-system-i386",
            "-cdrom", self.iso,
            "-m", str(self.mem),
            "-smp", str(self.cpus),
            "-vga", self.vga,
            "-machine", "pc,pcspk-audiodev=tinos-audio",
            "-audiodev", "driver=none,id=tinos-audio",
            "-device", "AC97,audiodev=tinos-audio",
            "-display", "none",
            "-serial", "file:" + self.serial_path,
            "-qmp", "tcp:127.0.0.1:%d,server,nowait" % self.qmp_port,
            "-no-reboot",
        ]
        self.log("+ " + " ".join(cmd))
        self.proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                                     stderr=subprocess.PIPE)
        deadline = time.time() + 15
        while time.time() < deadline:
            try:
                self.sock = socket.create_connection(("127.0.0.1", self.qmp_port))
                break
            except OSError:
                if self.proc.poll() is not None:
                    err = self.proc.stderr.read().decode(errors="replace")
                    raise RuntimeError("qemu exited immediately:\n" + err)
                time.sleep(0.2)
        else:
            raise RuntimeError("timed out connecting to QMP")
        self.file = self.sock.makefile("rw")
        self.file.readline()                      # greeting
        self.cmd("qmp_capabilities")

    def stop(self):
        try:
            if self.file:
                self.cmd("quit")
        except Exception:
            pass
        if self.proc:
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()

    def cleanup(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def log(self, msg):
        if self.verbose:
            print(msg, flush=True)

    # -- QMP ---------------------------------------------------------------
    def cmd(self, execute, **arguments):
        req = {"execute": execute}
        if arguments:
            req["arguments"] = arguments
        self.file.write(json.dumps(req) + "\n")
        self.file.flush()
        while True:
            line = self.file.readline()
            if not line:
                raise RuntimeError("QMP connection closed")
            msg = json.loads(line)
            if "event" in msg:
                continue
            if "error" in msg:
                raise RuntimeError("QMP error for %s: %s" % (execute, msg["error"]))
            return msg.get("return")

    # -- input -------------------------------------------------------------
    def key(self, *keys, hold=12):
        # QEMU delivers key events through a queue paced by hold-time, so a
        # large hold time throttles typing to a few characters per second.
        self.cmd("send-key",
                 keys=[{"type": "qcode", "data": k} for k in keys],
                 **{"hold-time": hold})
        time.sleep(0.02)

    def type(self, text, delay=0.01):
        for ch in text:
            self.key(*char_to_keys(ch))
            time.sleep(delay)

    def line(self, text, delay=0.01):
        """Type a command and press Enter."""
        self.type(text, delay)
        self.key("ret")
        time.sleep(0.25)

    # TinOS drives a relative PS/2 mouse, so absolute events are not
    # delivered: moves are sent as bounded relative steps and the harness
    # keeps track of where the guest cursor ended up.
    def _rel(self, dx, dy):
        events = []
        if dx:
            events.append({"type": "rel", "data": {"axis": "x", "value": dx}})
        if dy:
            events.append({"type": "rel", "data": {"axis": "y", "value": dy}})
        if events:
            self.cmd("input-send-event", events=events)
            time.sleep(0.02)

    def mouse_home(self):
        """Push the cursor into the top left corner, where it clamps."""
        for _ in range(8):
            self._rel(-200, -200)
        self.cursor = [0, 0]

    def mouse_move(self, x, y):
        if self.cursor is None:
            self.mouse_home()
        while self.cursor != [x, y]:
            dx = max(-200, min(200, x - self.cursor[0]))
            dy = max(-200, min(200, y - self.cursor[1]))
            self._rel(dx, dy)
            self.cursor[0] += dx
            self.cursor[1] += dy
        time.sleep(0.05)

    def mouse_button(self, down, button="left"):
        self.cmd("input-send-event", events=[
            {"type": "btn", "data": {"down": down, "button": button}}])
        time.sleep(0.08)

    def mouse_click(self, x, y, button="left"):
        self.mouse_move(x, y)
        self.mouse_button(True, button)
        self.mouse_button(False, button)
        time.sleep(0.2)

    def mouse_drag(self, x0, y0, x1, y1):
        self.mouse_move(x0, y0)
        self.mouse_button(True)
        steps = 6
        for i in range(1, steps + 1):
            self.mouse_move(x0 + (x1 - x0) * i // steps,
                            y0 + (y1 - y0) * i // steps)
        self.mouse_button(False)
        time.sleep(0.2)

    # -- output ------------------------------------------------------------
    def screenshot(self, path):
        """Capture the screen; converts to PNG when Pillow is available."""
        ppm = os.path.join(self.tmp, "shot.ppm")
        self.cmd("screendump", filename=ppm)
        for _ in range(50):
            if os.path.exists(ppm) and os.path.getsize(ppm) > 1000:
                break
            time.sleep(0.05)
        if path.endswith(".png"):
            try:
                from PIL import Image
                Image.open(ppm).save(path)
            except ImportError:
                shutil.copy(ppm, path[:-4] + ".ppm")
                return path[:-4] + ".ppm"
        else:
            shutil.copy(ppm, path)
        os.remove(ppm)
        return path

    def serial(self):
        try:
            with open(self.serial_path, "rb") as f:
                return f.read().decode("utf-8", errors="replace")
        except FileNotFoundError:
            return ""

    def wait_for_serial(self, needle, timeout=30):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if needle in self.serial():
                return True
            time.sleep(0.25)
        return False

# --------------------------------------------------------------------------
# Acceptance scenario
# --------------------------------------------------------------------------

class Checker:
    def __init__(self):
        self.failures = []
        self.checks = 0

    def check(self, condition, description, detail=""):
        self.checks += 1
        print("  [%s] %s" % ("PASS" if condition else "FAIL", description),
              flush=True)
        if not condition:
            if detail:
                print("         " + detail.replace("\n", "\n         "),
                      flush=True)
            self.failures.append(description)
        return bool(condition)


class Screen:
    """A captured frame that can be asked what text it shows."""

    def __init__(self, path):
        from PIL import Image
        self.path = path
        self.image = Image.open(path).convert("RGB")

    @property
    def size(self):
        return self.image.size

    def find(self, text, **kwargs):
        return screen_ocr.find_text(self.image, text, **kwargs)

    def has(self, text, **kwargs):
        return len(self.find(text, **kwargs)) > 0

    def missing(self, text, **kwargs):
        return len(self.find(text, **kwargs)) == 0

    def crop_text(self, box):
        return self.image.crop(box)


def run_legacy_scenario(iso, shots_dir):
    os.makedirs(shots_dir, exist_ok=True)
    vm = Tinos(iso, serial=os.path.join(shots_dir, "serial.log"), verbose=False)
    c = Checker()
    shot_index = [0]

    def shot(name, settle=0.35):
        time.sleep(settle)
        shot_index[0] += 1
        path = os.path.join(shots_dir, "%02d-%s.png" % (shot_index[0], name))
        vm.screenshot(path)
        return Screen(path)

    try:
        vm.start()

        # ---------------------------------------------------------------
        print("\n== text mode boot ==")
        c.check(vm.wait_for_serial("boot: TinOS BIOS loader", timeout=30),
                "kernel starts through the TinOS BIOS loader")
        time.sleep(1.2)
        boot = shot("boot-text", settle=0)
        c.check(boot.size == (720, 400),
                "boot stage runs in 80x25 VGA text mode (%dx%d)" % boot.size)

        for needle, what in [
            ("gdt: flat 32-bit segments loaded", "GDT installed"),
            ("idt: vectors installed", "IDT and PIC ready"),
            ("timer: PIT", "timer and RTC start"),
            ("mm: heap and physical allocator online", "memory manager comes up"),
            ("i8042: keyboard on irq1", "keyboard driver attaches"),
            ("i8042: mouse on irq12", "mouse driver attaches"),
            ("vfs: tmpfs mounted on /", "filesystem mounts"),
        ]:
            c.check(needle in vm.serial(), "boot log reports: " + what)

        c.check(vm.wait_for_serial("graphics mode active", timeout=30),
                "kernel switches into graphics mode", vm.serial()[-600:])
        c.check("mode set to 1024x768x32 linear" in vm.serial(),
                "VGA driver programs a 1024x768x32 linear framebuffer")

        # ---------------------------------------------------------------
        print("\n== desktop ==")
        vm.wait_for_serial("desktop starting", timeout=20)
        desk = shot("desktop", settle=1.0)
        c.check(desk.size == (1024, 768),
                "desktop renders at 1024x768 (%dx%d)" % desk.size)
        c.check(desk.has("TinOS 1.0"), "wallpaper shows the system name")
        c.check(desk.has("Start"), "taskbar shows the start button")

        # ---------------------------------------------------------------
        print("\n== start menu ==")
        vm.key("ctrl", "esc")
        menu = shot("start-menu")
        for entry in ["System", "Accessories", "Multimedia",
                      "Restart", "Shut Down"]:
            c.check(menu.has(entry), "start menu lists '%s'" % entry)

        # System is selected by default, so its apps are already in the
        # right-hand submenu; the submenu only ever shows one category's
        # apps at a time.
        for entry in ["About TinOS", "Control Panel", "System Information",
                      "Terminal"]:
            c.check(menu.has(entry),
                    "start menu shows System's apps by default: '%s'" % entry)
        c.check(menu.missing("File Explorer"),
                "apps outside the selected category stay out of the submenu")

        # Down selects the next category; the submenu swaps live, no
        # expand/collapse step involved.
        vm.key("down")
        accessories = shot("start-menu-accessories")
        for entry in ["Calculator", "Text Editor", "File Explorer"]:
            c.check(accessories.has(entry),
                    "selecting Accessories reveals '%s'" % entry)
        c.check(accessories.missing("Terminal"),
                "selecting a category swaps the previous one's apps out")

        vm.key("esc")
        vm.key("ctrl", "alt", "t")

        # ---------------------------------------------------------------
        print("\n== terminal ==")
        term = shot("terminal", settle=0.8)
        c.check(term.has("tinos:/users/sammy$"), "terminal shows a prompt with the cwd")
        c.check(term.has("Terminal - /users/sammy"), "terminal window has a title")

        for command in ["cd /users/sammy", "mkdir projects", "cd projects",
                        "touch notes.txt", "echo kernel test > notes.txt",
                        "cat notes.txt", "cp notes.txt backup.txt",
                        "mv backup.txt archive.txt", "ls"]:
            vm.line(command)
        cmds = shot("terminal-commands")
        c.check(cmds.has("tinos:/users/sammy/projects$"),
                "cd changed the working directory shown in the prompt")
        c.check(cmds.has("kernel test"),
                "echo > file then cat shows the file contents")
        c.check(cmds.has("archive.txt"),
                "cp and mv produced archive.txt, listed by ls")

        vm.line("cat notes.txt | grep kernel")
        vm.line("cat notes.txt | grep nothing-here")
        vm.line("echo piped | wc -w")
        pipes = shot("terminal-pipes")
        c.check(len(pipes.find("kernel test")) >= 2,
                "pipeline 'cat | grep' prints the matching line")
        c.check(pipes.has("tinos:/users/sammy/projects$"),
                "terminal is still responsive after a pipeline")

        vm.line("cat missing.txt")
        vm.line("cd /nowhere")
        vm.line("rm")
        errs = shot("terminal-errors")
        c.check(errs.has("no such file or directory"),
                "missing paths produce a readable error")
        c.check(errs.has("missing operand"),
                "commands without arguments explain themselves")

        vm.line("clear")
        vm.line("help")
        helped = shot("terminal-help")
        c.check(helped.has("Operators:") and helped.has("pipe a"),
                "help documents the commands and the shell operators")
        c.check(helped.missing("archive.txt"),
                "clear wiped the previous terminal output")

        vm.line("clear")
        vm.line("echo wheel-test-top-marker")
        for i in range(28):
            vm.line("echo filler %d" % i)
        vm.line("echo wheel-test-bottom-marker")
        bottom = shot("terminal-scrolled-bottom")
        c.check(bottom.has("bottom-marker", tolerance=0) and
                bottom.missing("top-marker", tolerance=0),
                "scrollback shows the most recent line before scrolling")
        vm.mouse_move(400, 300)
        for _ in range(20):  # clamps at the top; more than enough to get there
            vm.mouse_button(True, "wheel-up")
            vm.mouse_button(False, "wheel-up")
        scrolled = shot("terminal-scrolled-up")
        c.check(scrolled.has("top-marker", tolerance=0) and
                scrolled.missing("bottom-marker", tolerance=0),
                "the mouse wheel scrolls the terminal scrollback")

        # ---------------------------------------------------------------
        print("\n== file explorer (opened with the mouse) ==")
        vm.mouse_click(48, 750)                       # the Start button
        menu2 = shot("start-menu-mouse")
        cat_hits = menu2.find("Accessories")
        c.check(bool(cat_hits), "start menu opens from a mouse click")
        hits = []
        if cat_hits:
            cx, cy = cat_hits[0]
            vm.mouse_click(cx + 10, cy + 6)          # select the category
            menu2 = shot("start-menu-mouse-accessories")
            hits = menu2.find("File Explorer")
        if hits:
            x, y = hits[0]
            vm.mouse_click(x + 30, y + 6)
        files = shot("explorer", settle=0.8)
        c.check(files.has("Location:") and files.has("/users/sammy"),
                "explorer opens showing the user home path bar")
        c.check(files.has("projects"),
                "explorer shows the directory created from the terminal")

        vm.key("ctrl", "n")
        vm.type("from-explorer")
        vm.key("ret")
        made = shot("explorer-newdir")
        c.check(made.has("from-explorer"),
                "Ctrl+N creates a folder from the explorer")

        # ... and the terminal sees it immediately
        vm.key("alt", "tab")
        time.sleep(0.3)
        vm.line("ls /users/sammy")
        shared = shot("terminal-sees-explorer")
        c.check(shared.has("from-explorer"),
                "the terminal sees the folder the explorer created")

        vm.key("alt", "tab")
        time.sleep(0.3)
        vm.key("f5")
        vm.key("down")
        copied_before = shot("explorer-select")
        vm.key("ctrl", "c")
        vm.key("ctrl", "v")
        pasted = shot("explorer-paste")
        c.check(pasted.has("(copy)"),
                "copy and paste duplicates a real filesystem entry")

        vm.key("f2")
        vm.type("renamed-entry")       # typing replaces the pre-filled name
        vm.key("ret")
        renamed = shot("explorer-rename")
        c.check(renamed.has("renamed-entry"), "F2 renames an entry")

        vm.key("delete")
        confirm = shot("explorer-delete-confirm")
        c.check(confirm.has("Enter = yes"), "delete asks for confirmation")
        vm.key("ret")
        deleted = shot("explorer-deleted")
        c.check(deleted.has("Deleted"),
                "confirming the prompt performs the delete")
        time.sleep(3.0)                     # let the notification expire
        gone = shot("explorer-after-delete")
        c.check(gone.missing("renamed-entry"),
                "the deleted entry is gone from the listing")

        # Drag-and-drop: create two folders and drag one onto the other.
        vm.key("ctrl", "n")
        vm.type("dnd-src")
        vm.key("ret")
        vm.key("ctrl", "n")
        vm.type("dnd-dst")
        vm.key("ret")
        listed = shot("explorer-dnd-setup")
        src_hits = listed.find("dnd-src")
        dst_hits = listed.find("dnd-dst")
        docs_hits = listed.find("documents")
        c.check(bool(src_hits) and bool(dst_hits) and bool(docs_hits),
                "two folders created for the drag-and-drop test")
        if src_hits and dst_hits and docs_hits:
            sx, sy = src_hits[0]
            dx, dy = dst_hits[0]
            qx, qy = docs_hits[0]
            # dnd-dst is already selected right after being created (it was
            # the last folder made); click a third, unrelated row first so
            # the click on dnd-src below is a genuine "select" and not
            # "select and open" (clicking an already-selected row opens it).
            vm.mouse_click(qx + 10, qy + 6)
            time.sleep(0.15)
            vm.mouse_click(sx + 10, sy + 6)
            time.sleep(0.15)
            vm.mouse_drag(sx + 10, sy + 6, dx + 10, dy + 6)
        dropped = shot("explorer-dnd-dropped")
        c.check(dropped.has("Moved into dnd-dst") and dropped.missing("dnd-src"),
                "dragging a row onto a folder moves it there")

        vm.key("alt", "tab")
        time.sleep(0.3)
        vm.line("ls /users/sammy/dnd-dst")
        moved = shot("terminal-sees-dnd")
        c.check(moved.has("dnd-src"),
                "the terminal sees the file the drag moved")
        vm.key("alt", "tab")
        time.sleep(0.3)

        # ---------------------------------------------------------------
        print("\n== text editor ==")
        vm.key("ctrl", "esc")
        editor_menu = shot("start-menu-editor")
        cat_hits = editor_menu.find("Accessories")
        if cat_hits:
            cx, cy = cat_hits[0]
            vm.mouse_click(cx + 10, cy + 6)          # select the category
            editor_menu = shot("start-menu-editor-accessories")
        editor_hits = editor_menu.find("Text Editor")
        c.check(bool(editor_hits), "start menu exposes Text Editor")
        if editor_hits:
            ex, ey = editor_hits[0]
            vm.mouse_click(ex + 30, ey + 6)
        editor = shot("editor", settle=0.8)
        c.check(editor.has("in-memory filesystem"),
                "editor opens /README.txt and shows its contents")
        vm.type("EDITED. ")
        vm.key("ctrl", "s")
        saved = shot("editor-saved")
        c.check(saved.has("Saved /README.txt"),
                "Ctrl+S writes the file back and reports it")

        # Mouse-drag text selection, then copy/paste it.
        vm.type("\nSELECTME here")
        marked = shot("editor-selection-source")
        hits = marked.find("SELECTME")
        c.check(bool(hits), "marker text is on screen to select")
        if hits:
            hx, hy = hits[0]
            vm.mouse_click(hx, hy + 6)
            vm.mouse_drag(hx, hy + 6, hx + 8 * len("SELECTME"), hy + 6)
            vm.key("ctrl", "c")
            vm.key("end")
            vm.type("\npasted: ")
            vm.key("ctrl", "v")
        pasted = shot("editor-selection-pasted")
        # Higher tolerance: the source occurrence sits right where the
        # editor's line-wrap glyphs are packed tightly against each other.
        c.check(pasted.has("pasted: SELECTME"),
                "dragging selects text and Ctrl+C/Ctrl+V copies it")

        vm.key("alt", "f4")
        time.sleep(0.3)
        vm.key("alt", "tab")
        time.sleep(0.3)
        vm.line("cat /README.txt | head -n 1")
        edited = shot("terminal-reads-edit")
        c.check(edited.has("EDITED."),
                "the terminal reads back what the editor saved")

        # ---------------------------------------------------------------
        print("\n== window management ==")
        before = shot("wm-before-drag")
        title = before.find("Terminal - ")
        if title:
            tx, ty = title[0]
            vm.mouse_drag(tx + 40, ty + 6, tx + 190, ty + 96)
        after = shot("wm-after-drag")
        moved = after.find("Terminal - ")
        c.check(bool(title) and bool(moved) and moved[0] != title[0],
                "a window can be dragged by its title bar")

        if moved:
            mx, my = moved[0]
            # The title is centered, so recover the frame origin from the
            # known terminal title width rather than treating text origin as
            # the window origin.
            title_width = len("Terminal - /users/sammy") * 8
            frame_x = mx - (748 - title_width) // 2
            frame_y = my - 10
            grip_x, grip_y = frame_x + 748 - 7, frame_y + 452 - 7
            before_size = shot("wm-before-resize")
            vm.mouse_drag(grip_x, grip_y, grip_x + 120, grip_y + 100)
            after_size = shot("wm-after-resize")
            # A taller window has more scrollback rows visible below the
            # prompt; the prompt line itself moves down as a result.
            before_prompt = before_size.find("tinos:")
            after_prompt = after_size.find("tinos:")
            c.check(bool(before_prompt) and bool(after_prompt) and
                    after_prompt[-1][1] > before_prompt[-1][1],
                    "dragging the resize grip grows the window")
            vm.line("echo after-resize")
            resized_output = shot("wm-resize-typed")
            c.check(resized_output.has("after-resize"),
                    "the terminal keeps working after being resized")

        vm.key("alt", "f4")
        closed = shot("wm-closed")
        c.check(closed.missing("tinos:/users/sammy/projects$"),
                "Alt+F4 closes the focused window")

        # ---------------------------------------------------------------
        print("\n== taskbar clock ==")
        first = shot("clock-a", settle=0.2)
        time.sleep(2.5)
        second = shot("clock-b", settle=0.2)
        clock_box = (900, 734, 1020, 764)
        from PIL import ImageChops
        diff = ImageChops.difference(first.crop_text(clock_box),
                                     second.crop_text(clock_box))
        c.check(diff.getbbox() is not None,
                "the taskbar clock updates without user input")

        # ---------------------------------------------------------------
        print("\n== stability ==")
        vm.key("ctrl", "alt", "t")
        time.sleep(0.8)
        for junk in ["", "   ", "|", "&&", ">", "cat /", "cd ../../..",
                     "rm -rf /", "echo 'unterminated", "ls | | ls",
                     "mv", "cp", "grep", "zzz --nope"]:
            vm.line(junk)
        alive = shot("stability")
        c.check(alive.has("tinos:"),
                "the shell survives malformed and hostile input")
        vm.line("echo still alive")
        alive2 = shot("stability-after")
        c.check(alive2.has("still alive"),
                "commands still run after the abuse")

        log = vm.serial()
        c.check("KERNEL PANIC" not in log, "no kernel panic during the run",
                log[-1200:] if "KERNEL PANIC" in log else "")

    finally:
        try:
            vm.stop()
        finally:
            vm.cleanup()

    print("\n%d checks, %d failures" % (c.checks, len(c.failures)))
    for failure in c.failures:
        print("  FAILED: %s" % failure)
    print("screenshots in %s" % shots_dir)
    return 0 if not c.failures else 1


def run_scenario(iso, shots_dir):
    os.makedirs(shots_dir, exist_ok=True)
    vm = Tinos(iso, serial=os.path.join(shots_dir, "serial.log"), verbose=False)
    c = Checker()

    def shot(name):
        time.sleep(0.4)
        path = os.path.join(shots_dir, name + ".png")
        vm.screenshot(path)
        return Screen(path)

    try:
        vm.start()
        c.check(vm.wait_for_serial("userland: imported ",
                                    timeout=30), "boot imports only ring-3 programs")
        c.check(vm.wait_for_serial("desktop starting", timeout=30),
                "desktop starts without native apps")
        log = vm.serial()
        c.check("native app(s)" not in log, "no native app loader runs")
        desk = shot("desktop")
        c.check(desk.size == (1024, 768), "desktop renders at 1024x768")
        c.check(desk.has("Start"), "taskbar remains available")

        vm.key("ctrl", "esc")
        menu = shot("programs")
        for label in ("Programs", "terminal.elf", "explorer.elf",
                      "editor.elf", "about.elf", "calculator.elf",
                      "sysinfo.elf", "control_panel.elf", "Restart", "Shut Down"):
            c.check(menu.has(label), "start menu shows " + label)

        hits = menu.find("terminal.elf")
        if c.check(bool(hits), "ring-3 executable is selectable"):
            x, y = hits[0]
            vm.mouse_click(x + 20, y + 6)
            c.check(vm.wait_for_serial("opened window 1 'Terminal'", timeout=15),
                    "terminal opens from an isolated process")
            terminal = shot("terminal")
            c.check(terminal.has("tinos:/"), "terminal draws its own prompt")
            vm.line("ls /")
            c.check(shot("terminal-ls").has("README.txt"), "directory enumeration")
            vm.line("echo orange > /users/sammy/note.txt")
            vm.line("cat /users/sammy/note.txt")
            c.check(shot("terminal-cat").has("orange"), "writable file descriptor")
            vm.line("run /apps/gui_security.elf")
            c.check(shot("gui-security").has("GUI handles and files isolated"),
                    "GUI handles and child controls are process-scoped")
            vm.line("run /apps/echo.elf")
            c.check(shot("child-prompt").has("ring3: type a line"),
                    "child stdout reaches the GUI terminal")
            vm.line("guest input")
            c.check(shot("child-output").has("ring3 echo: guest input"),
                    "child stdin and stdout work through the terminal")
            vm.line("run /apps/stream.elf")
            c.check(shot("stream-output").has("stream complete"),
                    "backpressure lets large child output finish")
            vm.line("run /apps/spin.elf")
            vm.key("ctrl", "c")
            c.check(vm.wait_for_serial("[proc] killed pid", timeout=15),
                    "Ctrl+C kills a spinning child")
            vm.line("echo still responsive")
            c.check(shot("after-spinning-child").has("still responsive"),
                    "desktop terminal survives a CPU-bound child")
            vm.line("edit /users/sammy/note.txt")
            c.check(vm.wait_for_serial("opened window 2 'Text Editor'",
                                       timeout=15), "editor is another process")
            c.check(shot("editor").has("orange"), "editor reads the file")
            vm.type("blue")
            vm.key("ctrl", "s")
            c.check(shot("editor-save").has("Saved"), "editor saves through libc")
            vm.key("alt", "f4")
            c.check(vm.wait_for_serial("closed window 2 'Text Editor'",
                                       timeout=15), "window close reaches the process")
            vm.line("cat /users/sammy/note.txt")
            c.check(shot("terminal-readback").has("blue"), "saved data is visible")
            vm.key("alt", "f4")
            c.check(vm.wait_for_serial("closed window 1 'Terminal'",
                                       timeout=15), "terminal exits after close")

        vm.key("ctrl", "esc")
        menu = shot("menu-explorer")
        hits = menu.find("explorer.elf")
        if c.check(bool(hits), "file explorer listed in the menu"):
            x, y = hits[0]
            vm.mouse_click(x + 20, y + 6)
            c.check(vm.wait_for_serial("opened window 3 'File Explorer'",
                                       timeout=15), "explorer opens in ring 3")
            c.check(shot("explorer").has("README.txt"), "explorer lists files")
            vm.key("alt", "f4")
        vm.key("ctrl", "esc")
        menu = shot("menu-calculator")
        hits = menu.find("calculator.elf")
        if c.check(bool(hits), "calculator listed in the menu"):
            x, y = hits[0]
            vm.mouse_click(x + 20, y + 6)
            c.check(vm.wait_for_serial("opened window 4 'Calculator'", timeout=15),
                    "calculator opens in ring 3")
            vm.type("2+3*4")
            vm.key("ret")
            calc = shot("calculator")
            c.check(calc.has("Result") and calc.has("14"),
                    "calculator evaluates arithmetic in userspace")
            vm.key("alt", "f4")
        vm.key("ctrl", "esc")
        menu = shot("menu-sysinfo")
        hits = menu.find("sysinfo.elf")
        if c.check(bool(hits), "system information listed in the menu"):
            x, y = hits[0]
            vm.mouse_click(x + 20, y + 6)
            c.check(vm.wait_for_serial("opened window 5 'System Information'",
                                       timeout=15), "sysinfo opens in ring 3")
            info = shot("sysinfo")
            c.check(info.has("RAM total") and info.has("CPU"),
                    "system information comes through a validated syscall")
            vm.key("alt", "f4")
        vm.key("ctrl", "esc")
        menu = shot("menu-settings")
        hits = menu.find("control_panel.elf")
        if c.check(bool(hits), "control panel listed in the menu"):
            x, y = hits[0]
            vm.mouse_click(x + 20, y + 6)
            c.check(vm.wait_for_serial("opened window 6 'Control Panel'",
                                       timeout=15), "settings app opens in ring 3")
            vm.type("2")
            c.check(shot("keyboard-es").has("Spanish"),
                    "userspace can configure keyboard through ioctl")
            vm.type("1")
            c.check(shot("keyboard-us").has("English"),
                    "keyboard setting can be restored")
            vm.key("alt", "f4")
        vm.key("ctrl", "esc")
        after = shot("desktop-after-process")
        c.check(after.has("Programs"), "desktop accepts input after client exits")
        c.check("KERNEL PANIC" not in vm.serial(), "no kernel panic")
    finally:
        vm.stop()
        vm.cleanup()

    print("\n%d checks, %d failures" % (c.checks, len(c.failures)))
    return 0 if not c.failures else 1


def run_interactive(iso, seconds, shots_dir):
    os.makedirs(shots_dir, exist_ok=True)
    vm = Tinos(iso, serial=os.path.join(shots_dir, "serial.log"))
    try:
        vm.start()
        for i in range(seconds):
            time.sleep(1)
            vm.screenshot(os.path.join(shots_dir, "t%02d.png" % i))
        print(vm.serial())
    finally:
        vm.stop()
        vm.cleanup()
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", default="build/tinos.iso")
    ap.add_argument("--shots-dir", default="build/test-shots")
    ap.add_argument("--interactive", type=int, default=0,
                    help="boot and screenshot once per second for N seconds")
    ap.add_argument("--legacy", action="store_true",
                    help="run the retired ring-0 GUI app scenario")
    args = ap.parse_args()

    if not os.path.exists(args.iso):
        print("%s not found; run make first" % args.iso, file=sys.stderr)
        return 2
    if args.interactive:
        return run_interactive(args.iso, args.interactive, args.shots_dir)
    if args.legacy:
        return run_legacy_scenario(args.iso, args.shots_dir)
    return run_scenario(args.iso, args.shots_dir)


if __name__ == "__main__":
    sys.exit(main())
