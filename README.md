# TinOS

A small graphical operating system for QEMU, written from scratch: its own
32-bit kernel, VGA graphics driver, window manager, in-memory filesystem and
shell. It boots through GRUB, prints a text-mode start-up log, switches the
display into a 1024x768x32 linear framebuffer, and hands over to a desktop
with a taskbar, a start menu, a terminal, a file explorer and a text editor.

It is not based on Linux and contains no Linux userspace: after GRUB hands
over control, every instruction that runs is from this repository (plus
libgcc's 64-bit arithmetic helpers).

```
   +-----------------------------------------------------------+
   |  GRUB2  --multiboot2-->  kernel/main.c  (text mode)        |
   |                              |                             |
   |            gdt -> idt/pic -> pit/rtc -> pmm/heap ->        |
   |            keyboard/mouse -> ramfs -> pci -> vbe           |
   |                              |                             |
   |                     kernel/ui/desktop.c                    |
   |          window manager, taskbar, start menu, cursor       |
   |            /            |             |          \         |
   |      terminal      explorer        editor      sysinfo     |
   |            \            |             |          /         |
   |                     kernel/fs (one shared tree)            |
   +-----------------------------------------------------------+
```

## Quick start

```sh
sudo dnf install gcc binutils make grub2-tools-extra xorriso qemu-system-x86 \
                 libgcc.i686           # 32-bit libgcc for -m32 code
make                                    # builds build/tinos.iso
make run                                # boots it in QEMU
```

The first screen is the GRUB menu (3 seconds), then the text-mode boot log
for about three seconds, then the desktop.

## Requirements

| Tool | Why | Fedora package |
|------|-----|----------------|
| `gcc` with `-m32` | compiles the freestanding 32-bit kernel | `gcc`, `libgcc.i686` |
| `ld` | links the kernel with a custom linker script | `binutils` |
| `grub2-mkrescue` | builds the bootable ISO | `grub2-tools-extra` |
| `xorriso` | ISO filesystem writer used by grub2-mkrescue | `xorriso` |
| `qemu-system-i386` | runs the image | `qemu-system-x86` |
| `python3` + Pillow + numpy | optional: automated GUI tests | `python3-pillow`, `python3-numpy` |

Everything above is already present on a stock Fedora workstation except
possibly `libgcc.i686` and `grub2-tools-extra`.

## Build and run

| Command | Effect |
|---------|--------|
| `make` | build `build/tinos.elf` and `build/tinos.iso` |
| `make run` | build and launch QEMU in a window (serial log to `build/tinos-serial.log`) |
| `make run-serial` | launch with the kernel log on your terminal |
| `make run-headless` | launch with no display (for the test driver) |
| `make test` | build and run the host-side unit tests |
| `make check` | host tests **and** the scripted QEMU boot/GUI test |
| `make verify-reproducible` | build twice from clean and compare the kernel and ISO |
| `make clean` | delete `build/` |

The exact QEMU invocation (also in `tools/run.sh`):

```sh
qemu-system-i386 -cdrom build/tinos.iso -m 256 -vga std -machine pc \
                 -rtc base=localtime -serial file:build/tinos-serial.log
```

`-vga std` matters: the graphics driver programs the Bochs/QEMU **DISPI**
registers of the standard VGA adapter. `-m 256` is a comfortable default;
TinOS runs in far less.

The GRUB menu has two entries:

* **TinOS** - the normal desktop.
* **TinOS (kernel self-test on serial)** - runs 173 in-kernel assertions over
  the heap, path handling, filesystem and shell before starting the desktop
  and prints the results to the serial port.

## Controls

### Desktop

| Key | Action |
|-----|--------|
| `Ctrl+Esc` or `F12` | open / close the start menu |
| `Up` `Down` `Enter` `Esc` | navigate the start menu |
| `Alt+Tab` | switch to the next window |
| `Alt+F4` | close the focused window |
| `Ctrl+Alt+T` / `Ctrl+Alt+E` | new terminal / new file explorer |

The mouse (if present - QEMU provides a PS/2 mouse by default) can click the
Start button and menu entries, focus windows, drag them by the title bar,
press the minimise and close buttons in the title bar, and click taskbar
buttons to raise or minimise a window.

### Terminal

| Key | Action |
|-----|--------|
| `Enter` | run the command |
| `Up` / `Down` | command history |
| `Tab` | complete a command name or file name |
| `Left` `Right` `Home` `End` `Backspace` `Delete` | line editing |
| `PageUp` / `PageDown` | scroll the output |
| `Ctrl+L` | clear the screen |
| `Ctrl+U` | clear the input line |
| `Ctrl+C` | abandon the current line |

### File Explorer

| Key | Action |
|-----|--------|
| `Up` / `Down` / `Home` / `End` / `PageUp` / `PageDown` | move the selection |
| `Enter` | open (directory: enter it, file: open in the editor) |
| `Backspace` | go to the parent directory |
| `F5` | refresh, `F2` rename |
| `Delete` | delete (asks for confirmation) |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | copy / cut / paste |
| `Ctrl+N` / `Ctrl+T` | new folder / new file |

Every action also has a toolbar button. Clicking an already-selected row
opens it. In a rename box the old name starts out selected, so typing
replaces it; `Esc` cancels any prompt.

### Text editor

Ordinary typing, arrow keys, `Home`/`End`, `PageUp`/`PageDown`, `Tab`,
`Enter`, `Backspace`, `Delete`, and `Ctrl+S` to save. The title shows `*`
while there are unsaved changes.

## Shell

The shell operates on the in-memory filesystem shared by every application:
a file created in the terminal appears in the file explorer, and vice versa.

### Built-in commands

```
about     cat       cd        clear     cp        crash     date      echo
edit      exit      explorer  free      grep      head      help      history
ls        mkdir     mv        pwd       reboot    rm        shutdown  sort
stat      sysinfo   tail      touch     tree      uname     uptime    wc
```

`help` lists them with a one-line summary; `help <command>` shows the usage
line. The required set behaves as expected:

| Command | Notes |
|---------|-------|
| `cd [dir\|-]` | no argument goes to `/home`, `-` returns to the previous directory |
| `ls [-l] [-a] [-1] [path...]` | columns by default, `-l` adds size and modification time |
| `mkdir [-p] dir...` | |
| `touch file...` | creates or updates the timestamp |
| `echo [-n] text...` | |
| `cat [file...]` | with no argument it copies its input, which is what makes `... \| cat` work |
| `cp [-r] src... dst` | `-r` for directories, multiple sources need a directory target |
| `mv src... dst` | rename or move; refuses to move a directory into itself |
| `rm [-r] [-f] path...` | `-r` for directories, `-f` ignores missing files |
| `grep [-i] [-n] [-v] [-c] pattern [file...]` | regex search (`.`, `*`, `+`, `?`, `^`, `$`, `[classes]`), reads its input when no file is given |
| `pwd` | |
| `clear` | |
| `help [command]` | |

Extras: `tree`, `wc`, `head`, `tail`, `sort`, `stat`, `free`, `date`,
`uptime`, `uname -a`, `sysinfo`, `history`, `edit <file>` and
`explorer [dir]` (open a graphical window), `about`, `reboot`, `shutdown`,
`exit`, and `crash` (deliberately faults the kernel to demonstrate the panic
screen - see *Error handling*).

### Operators

| Syntax | Meaning |
|--------|---------|
| `cmd > file` | write the output to a file (created or truncated) |
| `cmd >> file` | append |
| `cmd < file` | read the input from a file |
| `a \| b` | pipe a's output into b (up to 8 stages) |
| `a & b`, `a ; b` | run a, then b - **sequential**, there is no background execution |
| `a && b` | run b only if a succeeded |
| `a \|\| b` | run b only if a failed |

Words honour `'single'` and `"double"` quotes and `\` escapes, and a leading
`~` expands to `/home`. Error messages go to the terminal even inside a
pipeline, so a mistake in the middle of a pipe is still visible.

The example session from the project brief works verbatim:

```sh
echo hello
echo hello > greeting.txt
cat greeting.txt
cat greeting.txt | grep hell
mkdir projects
cd projects
touch notes.txt
echo kernel test > notes.txt
cp notes.txt backup.txt
mv backup.txt archive.txt
ls
rm archive.txt
```

## Filesystem

A single tree of nodes in RAM, created at boot:

```
/
├── home/
│   └── documents/notes.txt
├── system/
│   ├── help.txt
│   └── motd.txt
├── tmp/
└── README.txt
```

Directories, regular files with real byte content, absolute and relative
paths, `.` and `..`, creation, reading, writing, appending, copying (deep for
directories), moving, renaming and deletion. Files grow geometrically on the
kernel heap. Nothing is written to disk - the tree lives for one boot
session, which is what the brief asks for.

Every application holds *paths*, not node pointers, and re-resolves them, so
deleting a directory from one window cannot leave another window pointing at
freed memory: a terminal standing in a deleted directory quietly falls back
to the nearest surviving ancestor.

## Architecture

| Area | Files | What it does |
|------|-------|--------------|
| Boot | `boot/boot.S`, `boot/linker.ld`, `boot/grub.cfg` | Multiboot2 header, stack, entry; kernel linked at 1 MiB |
| Arch | `kernel/arch/x86/` | GDT, IDT + 48 interrupt stubs, 8259A PIC, PIT, CMOS RTC, CPUID, reboot/power-off |
| Memory | `kernel/mm/` | bitmap page frame allocator over the Multiboot2 memory map; first-fit heap with coalescing that grows by taking new arenas from the frame allocator |
| Devices | `kernel/dev/` | COM1 serial log, VGA text console, PCI enumeration, Bochs DISPI mode setting, PS/2 keyboard, PS/2 mouse |
| Graphics | `kernel/gfx/` | double-buffered framebuffer with dirty-rectangle upload, rectangles, lines, gradients, alpha blending, clipping stack, 8x16 bitmap font |
| Filesystem | `kernel/fs/` | node tree, path resolution, textual path helpers |
| Shell | `kernel/sh/` | byte streams, tokeniser/parser, execution engine, 32 built-ins |
| UI | `kernel/ui/` | window manager, widgets and icons, desktop/taskbar/start menu/event loop |
| Applications | `kernel/apps/` | terminal, file explorer, text editor, system information, about |

### Decisions worth knowing

**i386, protected mode, no paging.** The kernel runs in ring 0 with a flat
GDT, so physical and virtual addresses are the same. Paging would buy
isolation the brief explicitly does not require, and its absence keeps the
framebuffer, the heap and the DMA-free device model simple. (One consequence:
a wild pointer write does not fault, which is why `crash gpf` uses an invalid
segment selector rather than a bad address.)

**Text mode first, then our own mode switch.** The Multiboot2 header asks
GRUB for an EGA text console and deliberately does *not* include a framebuffer
tag - with one, GRUB picks a graphics mode itself and the text-mode boot log
would never be visible. The kernel then switches to graphics on its own by
programming the DISPI registers (ports 0x1CE/0x1CF) and reading the
framebuffer address out of BAR0 of the PCI display device, rather than
hardcoding QEMU's 0xFD000000 or dropping to real mode for a BIOS call.

**Damage-based compositing.** Drawing goes into a system-memory back buffer;
`gfx_present()` copies only the changed rows to the adapter. Windows repaint
when their content changes, the whole screen only when the geometry changes.
The mouse cursor is painted on top of the finished frame and lifted off
again, so moving the mouse uploads a few hundred pixels rather than three
megabytes. A frame takes 1-3 ms in QEMU.

**Streams, not terminals, in the shell.** Commands read and write
`stream_t` byte buffers. That is what makes pipelines and `>` redirection
fall out naturally - and it is why the whole shell and filesystem can be
compiled natively and unit-tested on the host.

**Cooperative, single-threaded.** There are no processes or threads: the
desktop event loop drains the input queues, ticks the applications, redraws
what is dirty and then `hlt`s until the next interrupt. Interrupt handlers
only enqueue events. Applications are C structs with a vtable
(`app_class_t`), not separate address spaces.

## Error handling

Every CPU exception is fatal and reported: the handler paints a panic screen
with the exception name, error code, faulting `EIP`, `CR2` and a full
register dump, writes the same text to the serial port, and halts. The panic
path also works before the graphics stack is up (it falls back to the VGA
text console) and refuses to recurse.

You can see it on purpose:

```sh
crash divide   # division by zero      (exception 0)
crash opcode   # invalid instruction   (exception 6)
crash gpf      # bad segment selector  (exception 13)
crash panic    # an explicit kernel panic
```

Ordinary mistakes are *not* fatal: invalid paths, missing arguments, bad
options, unterminated quotes, deleting a non-empty directory, moving a
directory into itself and so on all produce an error message and a non-zero
exit status.

## Testing

Three layers, all runnable:

```sh
make test      # 267 host assertions (path, filesystem, parser, commands)
make check     # the above plus a 51-check scripted QEMU session
```

* **Host unit tests** (`tests/host/`) compile the portable filesystem and
  shell sources natively - with AddressSanitizer when the runtime is
  installed - and cover path normalisation, filesystem semantics and error
  codes, the parser (including malformed input), every required command,
  redirection, pipelines, sequencing, two shells sharing one tree, and a
  hostile-input fuzz list.

* **In-kernel self-test** (`kernel/core/selftest.c`, second GRUB entry) runs
  173 assertions inside the real kernel against the real heap and prints the
  result to the serial port.

* **Scripted QEMU session** (`tools/qemu_test.py`) boots the ISO headless,
  drives it through the QMP monitor (keystrokes, mouse moves, clicks, drags),
  captures screenshots and *reads the text off them*: `tools/screen_ocr.py`
  template-matches the screen against the kernel's own 8x16 font, so a check
  like "the terminal shows `kernel test`" is verified against real pixels.

## Known limitations

* No windows resizing, no overlap-aware partial repaint of the wallpaper
  (moving a window repaints the whole desktop), no drag-and-drop.
* The terminal wraps output at the width the window had when it opened; the
  window cannot be resized, so this never changes at run time.
* Text is ASCII only (codes 0x20-0x7E plus a few UI symbols). No Unicode.
* The scrollback holds 600 lines and the editor 256 KiB per file; both
  truncate rather than fail.
* No networking, sound, USB, disk persistence, user accounts, SMP or
  userspace processes - all explicitly out of scope.
* The RTC is read directly, so the taskbar clock follows the host clock via
  `-rtc base=localtime`; without that flag QEMU reports UTC.
* Only the Bochs/QEMU DISPI graphics interface is supported. On hardware or
  a VM without it the kernel stops with a diagnostic panic instead of
  guessing.

## What is original and what is reused

Written for this project: the kernel and all of its subsystems (boot code,
descriptor tables, interrupt handling, timer/RTC, memory management, PCI
scan, VGA/DISPI driver, graphics engine, keyboard and mouse drivers,
filesystem, shell and parser, window manager, widgets, and all five
applications), the build system, the test harnesses and this documentation.

Reused: GRUB2 as the bootloader (an explicitly allowed foundational
component); `libgcc` for 64-bit division helpers; the glyph shapes in
`kernel/gfx/font8x16.c`, which `tools/mkfont.py` rasterised once from
Liberation Mono on the host - the font *renderer* is ours, only the bitmaps
are derived. No operating system code was copied from another project.

## Repository layout

```
boot/            multiboot2 entry, linker script, GRUB config
kernel/
  arch/x86/      GDT, IDT, interrupt stubs, PIC, PIT, RTC, CPU, reset
  core/          logging, panic, clock glue, system info, self-tests
  dev/           serial, VGA text, PCI, VBE/DISPI, keyboard, mouse
  gfx/           framebuffer engine and the generated font
  fs/            in-memory filesystem and path helpers
  sh/            streams, parser, execution engine, built-in commands
  ui/            window manager, widgets, desktop/taskbar/start menu
  apps/          terminal, explorer, editor, system information, about
  lib/           freestanding string and printf routines
  mm/            page frame allocator and kernel heap
  include/tinos/  public headers for all of the above
tests/host/      native unit tests and the kernel-service shims
tools/           run script, QEMU test driver, screen OCR, font generator
```

`REPORT.md` records what was verified, how, and what remains incomplete.
