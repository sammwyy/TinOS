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
| `make runvnc` | launch QEMU with the display served over VNC |
| `make usb-image` | create or reuse a sparse 4 GiB FAT32 USB development image |
| `make test` | build and run the host-side unit tests |
| `make resources` | compile `resources/icons/*.bmp` into the generated icon atlas |

The desktop icons live in `resources/icons/` and are always compiled into the
shell atlas.  The legacy `icon_t` values remain as compatibility handles, but
`widget_icon()` resolves them exclusively to those named bitmap resources;
custom code can use `widget_icon_name(x, y, size, "name")` directly.

Image files with `.jpg`, `.jpeg`, `.png`, or `.webp` extensions open in the
Image Viewer from the File Explorer.  The viewer uses the file contents to
select a registered decoder, renders RGBA pixels with transparency, and fits
the image proportionally to its window.  PNG and JPEG decoding is provided by
the vendored `stb_image` library; WebP decoding is provided by the vendored
`simplewebp` library.  Both libraries use the TinOS heap and do not depend on
host filesystem or libc services.

For temporary artwork, create a numbered placeholder with:

```sh
python3 tools/make_icon_placeholder.py my-icon 21
python3 tools/make_icon_placeholder.py next-icon
```

Without a number, the script assigns the next number after counting the BMPs
already present in `resources/icons/`.
| `make check` | host tests **and** the scripted QEMU boot/GUI test |
| `make verify-reproducible` | build twice from clean and compare the kernel and ISO |
| `make clean` | delete `build/` |

The exact QEMU invocation (also in `tools/run.sh`):

```sh
qemu-system-i386 -cdrom build/tinos.iso -m 256 -vga std \
                 -machine pc,pcspk-audiodev=tinos-audio \
                 -audiodev driver=pipewire,id=tinos-audio \
                 -device AC97,audiodev=tinos-audio \
                 -boot order=d -rtc base=localtime \
                 -drive file=build/tinos-usb.img,if=none,id=tinos-usb-disk,format=raw \
                 -device piix3-usb-uhci,id=tinos-usb \
                 -device usb-storage,bus=tinos-usb.0,drive=tinos-usb-disk \
                 -serial file:build/tinos-serial.log
```

The run targets also attach `build/tinos-usb.img` through the emulated UHCI
controller. The image is sparse, formatted as FAT32 on first use, and reused
afterwards. Files placed in `build/usb-root/` are copied to its root:

```sh
mkdir -p build/usb-root
cp test-data.bin build/usb-root/
make run-serial
```

Override the image and source directory with `USB_IMAGE` and `USB_SOURCE`.

`-vga std` matters: the graphics driver programs the Bochs/QEMU **DISPI**
registers of the standard VGA adapter. `-m 256` is a comfortable default;
TinOS runs in far less.

The default QEMU run uses an AC'97 device with the PipeWire audio backend, so
Media Player audio and `beep` are audible through the host sound server. Set `AUDIODEV`,
for example to `driver=pa,id=tinos-audio` or `driver=none,id=tinos-audio`, when
another backend is required. Automated headless tests select `none` explicitly.
The `doremi` shell command remains a blocking AC'97 hardware test; music
playback and controls are provided by Media Player.

The GRUB menu has two entries:

* **TinOS** - the normal desktop.
* **TinOS (kernel self-test on serial)** - runs the in-kernel assertions over
  the heap, path handling, filesystem and shell before starting the desktop
  and prints the results to the serial port.

## Controls

### Desktop

| Key | Action |
|-----|--------|
| `Win`, `Ctrl+Esc` or `F12` | open / close the start menu |
| `Up` `Down` | move through the focused categories or apps |
| `Enter` | expand / collapse a category, launch the selected app |
| `Right` / `Left` | focus the category's apps / return to the categories |
| `Esc` | close the start menu |

The start menu groups the applications into collapsible categories
(*Accessories*, *Multimedia*, *System* and *Config*): opening a category with `Enter` or a click unfolds
its apps underneath, and the power actions sit at the bottom.
| `Alt+Tab` | switch to the next window |
| `Alt+F4` | close the focused window |
| `Ctrl+Alt+T` / `Ctrl+Alt+E` | new terminal / new file explorer |

The mouse (if present - QEMU provides a PS/2 mouse by default) can click the
Start button and menu entries, focus windows, drag them by the title bar,
resize them by dragging the grip in the bottom-right corner, press the
minimise and close buttons in the title bar, and click taskbar buttons to
raise or minimise a window.

### Calculator

The Calculator is available from Start > Accessories. It supports integer
arithmetic with `+`, `-`, `*`, `/`, `%` and parentheses. Type expressions on
the keyboard, press `Enter` or `=`, use `Backspace` to edit and `C` to clear.

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

The mouse wheel scrolls the scrollback (3 lines per notch).

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

Every action also has a toolbar button. Double-clicking a row opens it;
clicking and dragging a row onto a folder row moves it there (in the same
window or a different one). The mouse wheel scrolls the list. In a rename box
the old name starts out selected, so typing replaces it; `Esc` cancels any
prompt.

### Text editor

Ordinary typing, arrow keys, `Home`/`End`, `PageUp`/`PageDown`, `Tab`,
`Enter`, `Backspace`, `Delete`, and `Ctrl+S` to save. The title shows `*`
while there are unsaved changes. Click and drag to select text (shown
highlighted); `Ctrl+C`/`Ctrl+X`/`Ctrl+V` copy, cut and paste it, and typing
over a selection replaces it. The clipboard is shared with every other
editor window.

## Shell

The shell operates on the in-memory filesystem shared by every application:
a file created in the terminal appears in the file explorer, and vice versa.

### Built-in commands

```
about     beep      cat       cd        clear     cp        crash     date
edit      exit      explorer  free      grep      head      help      history
ls        mkdir     mv        pwd       reboot    rm        shutdown  sort
stat      sysinfo   tail      touch     tree      uname     uptime    wc
```

`help` lists them with a one-line summary; `help <command>` shows the usage
line. The required set behaves as expected:

| Command | Notes |
|---------|-------|
| `cd [dir\|-]` | no argument goes to the current user's home, `-` returns to the previous directory |
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
| `beep [frequency] [milliseconds]` | test the PC speaker (defaults to 880 Hz for 120 ms) |
| `doremi` | test AC'97 PCM output with a short melody |

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
`~` expands to the current user's directory (the built-in default user is
`sammy`, whose home is `/users/sammy`). Error messages go to the terminal even inside a
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

TinOS uses one hierarchical namespace. The canonical top-level layout is:

```
/
├── apps/                         # installed applications
│   └── <app-name>/
│       ├── <app-name>.elf        # main executable
│       ├── assets/               # icons, fonts and other app data
│       └── ...
├── config/                       # system-wide configuration
│   ├── <file>                    # configuration used by the system
│   └── <app-name>/               # configuration owned by one app
│       └── ...
├── dev/                          # device nodes exposed by drivers
├── drive/                        # mounted storage volumes
│   └── <device-name>/            # for example, an attached disk or volume
├── run/                          # volatile runtime state
│   └── ...
├── system/                       # immutable OS files
│   ├── bin/                      # system utilities and core executables
│   ├── lib/                      # trusted system libraries
│   ├── drivers/                  # driver modules and metadata (future)
│   └── ...
├── tmp/                          # temporary, disposable files
├── var/                          # mutable system-managed state
│   ├── cache/                    # regenerable global cache
│   ├── lib/                      # persistent app/service state
│   └── log/                      # system and application logs
├── users/                        # user homes and user-owned data
│   └── <username>/
│       ├── .apps/                # per-user app overrides and packages
│       ├── .data/                # per-user mutable app state
│       │   └── <app-name>/
│       │       ├── config/
│       │       ├── cache/
│       │       ├── logs/
│       │       ├── tmp/
│       │       └── data/
│       ├── documents/
│       └── downloads/
├── shared/                       # explicitly shared user data
└── README.txt
```

### Path conventions

* `/apps/<app-name>/` is the installed application package. The executable
  that launches an app is conventionally
  `/apps/<app-name>/<app-name>.elf`; assets and private libraries stay beside
  it in directories such as `assets/` and `lib/`. This keeps installation,
  discovery and removal simple and avoids a separate global `/bin`.
* `/users/<username>/` is the home directory of that user. User documents,
  downloads and mutable personal data belong there. A shell's `~` should
  resolve to the logged-in user's directory, not to a global shared home.
* `/config/` contains system-wide configuration that must survive
  independently of an application executable. Application defaults live
  below `/config/<app-name>/`; user-specific overrides live below
  `~/.data/<app-name>/config/`.
* `/system/` contains files supplied by the operating system. It is a
  reserved, immutable tree at runtime: applications may read it but should
  not modify or remove it. `/system/bin` and `/system/drivers` are reserved
  for system utilities and future driver support.
* `/dev/` is the device namespace. Entries represent devices or kernel
  interfaces, while `/drive/` contains mounted filesystems backed by storage
  devices. A device name under `/drive` is not required to match its `/dev`
  node.
* `/tmp/` is volatile scratch space. Programs must tolerate its contents
  being removed between boots, and future cleanup may remove old entries
  while the system is running.
* `/run/` is reserved for live runtime state such as service metadata,
  sockets and lock files. It is volatile and must not be used for documents
  or configuration.
* `/var/lib/` stores persistent mutable state that an application or service
  needs to operate, but that is not itself user documents or configuration.
  Examples include indexes, databases, queues and generated metadata.
* `/var/cache/` stores regenerable global data. Deleting it must not make the
  system or an application unusable.
* `/var/log/` stores system-wide and application logs. Logs do not belong in
  `/apps/<app-name>/`, because an app installation should remain replaceable
  and easy to remove.
* `/shared/` stores data deliberately exposed to multiple users. It is not a
  general-purpose second home directory.

User-specific application state is grouped below the user's home instead of
adding a separate global root for every kind of data:

* `~/.apps/<app-name>/` contains user-installed packages, overrides, plugins
  and optional user resources. It must not silently replace system
  executables or trusted libraries.
* `~/.data/<app-name>/config/` contains per-user configuration and overrides.
* `~/.data/<app-name>/cache/` contains per-user regenerable cache.
* `~/.data/<app-name>/logs/` contains logs visible only to that user.
* `~/.data/<app-name>/tmp/` contains user-scoped temporary files.
* `~/.data/<app-name>/data/` contains persistent user-owned application data.

There is deliberately no `/libraries` or `~/.libraries`. System libraries
belong in `/system/lib/`, while private libraries and assets belong inside
`/apps/<app-name>/` (for example `lib/` and `assets/`). User overrides may
provide resources through `~/.apps/<app-name>/`, but executable and library
search paths must be explicit rather than implicitly searching every user
directory.

The intended precedence is user overrides from `~/.apps/<app-name>/` and
`~/.data/<app-name>/config/`, then installed app files from
`/apps/<app-name>/`, then system defaults from `/config/<app-name>/` and
`/system/`. Application data, configuration, cache, logs and temporary files
remain separate concerns inside one per-app data root. If a future feature
needs a new top-level directory, it should be reserved here first instead of
inventing another application-specific root.

Names are lowercase, use ASCII letters, digits, `-` and `_`, and application
names are stable identifiers rather than display titles. Paths always use `/`,
are case-sensitive, collapse repeated separators and `.`/`..`, and never
allow `..` to escape the root. Applications should construct paths from the
well-known directories above instead of hard-coding another private root.

The current prototype creates the conventional directory tree in memory and
initializes the default user as `sammy`. It still has no user database or
permission enforcement. ATA disks with a supported filesystem are imported
into `/drive`; unsupported or unformatted disks are ignored.

Directories, regular files with real byte content, absolute and relative
paths, `.` and `..`, creation, reading, writing, appending, copying (deep for
directories), moving, renaming and deletion are supported. Files grow
geometrically on the kernel heap. The base tree lives for one boot session;
the current imported FAT16/FAT32 volumes are read-only from the VFS
perspective; the in-memory filesystem remains writable for the session.

### Storage

TinOS currently supports several block transports. ATA PIO is the simplest
legacy path and uses polling with 28-bit LBA over 512-byte sectors. VirtIO-blk
is available for virtual machines, and USB Mass Storage uses a polling UHCI
host controller plus BOT/SCSI. All of them feed the same partition and
filesystem layers and mount supported volumes below `/drive/`.

For example, a QEMU disk can be prepared and attached with:

```sh
qemu-img create -f raw build/disk.img 64M
mkfs.fat -F 16 build/disk.img
qemu-system-i386 -cdrom build/tinos.iso \
                 -drive file=build/disk.img,format=raw,if=ide,index=1
```

The storage stack is layered: block devices provide sectors, partition
readers describe ranges of sectors, filesystems interpret those ranges, and
the VFS exposes mounted trees. MBR partition discovery, FAT16 and FAT32 are
supported. NTFS, USB host controllers other than UHCI, and filesystem writes
are future work.

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
| Devices | `kernel/dev/` | COM1 serial log, VGA text console, PCI enumeration, Bochs DISPI mode setting, PS/2 keyboard, PS/2 mouse with IntelliMouse wheel support |
| Graphics | `kernel/gfx/` | double-buffered framebuffer with multi-region dirty upload, rectangles, lines, gradients, alpha blending, clipping stack, 8x16 bitmap font |
| Filesystem | `kernel/fs/` | node tree, path resolution, textual path helpers |
| Shell | `kernel/sh/` | byte streams, tokeniser/parser, execution engine, 32 built-ins |
| UI | `kernel/ui/` | window manager, widgets and icons, desktop/taskbar/start menu/event loop |
| Applications | `kernel/apps/` | terminal, file explorer, text editor, system information, about |
| Networking | `kernel/net/` | Ethernet, ARP, IPv4, ICMP, UDP, DHCP, DNS, TCP/HTTP and async jobs |

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
`gfx_present()` copies only the changed regions to the adapter. Windows keep a
cached surface of their last rendered frame, so moving one blits cached pixels
instead of calling the application's draw routine again. Closing or minimising
one repaints only
the rectangle it uncovered (the wallpaper there, plus any window now exposed
by it), not the whole desktop. A full repaint is reserved for opening a
window or a z-order change (focus, Alt-Tab), where recomputing what is
visible everywhere is simplest. The mouse cursor is painted on top of the
finished frame and lifted off again, so moving the mouse uploads a small region
rather than three megabytes.

**Streams, not terminals, in the shell.** Commands read and write
`stream_t` byte buffers. That is what makes pipelines and `>` redirection
fall out naturally - and it is why the whole shell and filesystem can be
compiled natively and unit-tested on the host.

**Cooperative, single-threaded.** There are no processes or threads: the
desktop event loop drains the input queues, ticks the applications, redraws
what is dirty and then `hlt`s until the next interrupt. Interrupt handlers
only enqueue events. Applications are C structs with a vtable
(`app_class_t`), not separate address spaces. Nothing shipped needs more
than this - there is no user-defined iteration for a command to hang in, and
no callback in `kernel/apps/` runs longer than a screen redraw - but it is
worth being explicit that a genuinely misbehaving callback (an infinite
loop, a slow computation) would stall the whole desktop, since one C
function call is the entire scheduling unit.

**Asynchronous workers and kernel threads.** `kernel/core/async.c` provides a
stackless state-machine API for short operations, while `kernel/core/thread.c`
and `kernel/arch/x86/context.S` provide real kernel threads with private
stacks. Async tasks and threads are both run outside interrupt handlers, and
the desktop gives each subsystem a bounded scheduling budget. Threads can
call `thread_yield()`, `thread_sleep()` and `thread_exit()`; networking can
therefore use a blocking-looking worker API without blocking the GUI. The PIT
can preempt a running worker, while explicit yields and blocking calls
remain available for predictable hand-off points.

`kernel/core/sync.c` supplies scheduler-aware mutexes and auto-reset events.
`kmutex_lock()` and `kevent_wait()` put the current worker in a blocked state
instead of spinning; an unlock or signal wakes it. Interrupt handlers should
only publish data and wake a worker, never acquire a mutex or execute a
blocking operation.

**Devices and drivers.** PCI enumeration publishes immutable bus records into
the small driver model (`kernel/core/driver.c`). Drivers register a match and
probe callback, and the model binds each device to at most one matching
driver. PCI helpers expose command/status, IRQ routing, BARs and bus-master
enablement. `kernel/mm/dma.c` exposes physically contiguous DMA buffers behind
an API that can later absorb virtual-to-physical mappings. The e1000 driver
backs the current Ethernet stack; networking is intentionally a small
polling-oriented kernel service rather than a userspace socket subsystem.

Preemption is deliberately still a separate step: the PIT must save and
restore a thread context instead of only marking events pending, and the
kernel needs a run queue, locks and a decision about what "task" even means
here (each window? each app tick?). That touches the PIT ISR
(`kernel/arch/x86/pit.c`), the GDT/TSS setup, and the desktop event loop
all at once, and every one of the 32 shell built-ins and 5 applications
would need to be re-audited for what they assume about not being
interrupted mid-callback. Given the theoretical (not observed) nature of
the risk, that rewrite is out of proportion to attempt as an incremental
change - it is a separate project, not a fix to fold into this one.

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
make test      # host assertions (path, filesystem, parser and commands)
make check     # the above plus the scripted QEMU boot/GUI session
```

* **Host unit tests** (`tests/host/`) compile the portable filesystem and
  shell sources natively - with AddressSanitizer when the runtime is
  installed - and cover path normalisation, filesystem semantics and error
  codes, the parser (including malformed input), every required command,
  redirection, pipelines, sequencing, two shells sharing one tree, and a
  hostile-input fuzz list.

* **In-kernel self-test** (`kernel/core/selftest.c`, second GRUB entry) runs
  the kernel assertions against the real heap and prints the result to serial.

* **Scripted QEMU session** (`tools/qemu_test.py`) boots the ISO headless,
  drives it through the QMP monitor (keystrokes, mouse moves, clicks, drags),
  captures screenshots and *reads the text off them*: `tools/screen_ocr.py`
  template-matches the screen against the kernel's own 8x16 font, so a check
  like "the terminal shows `kernel test`" is verified against real pixels.

## Known limitations

* Resizing only grows or shrinks from the bottom-right corner (no dragging
  the other three edges or corners), and toolbar buttons in the file
  explorer aren't responsive - shrink the window enough and they clip.
* The font covers ASCII, a few hand-drawn UI symbols, and the Latin-1
  Supplement (0xA0-0xFF: accented Western European letters). No Unicode
  beyond that, and there is no keyboard layout that can type those bytes -
  they only render if a file already contains them.
* The filesystem is session-oriented: imported FAT volumes are read-only and
  the writable base tree is not persisted back to disk.
* There are no user accounts, permissions, userspace processes, SMP, sound,
  NTFS support, or USB host controllers beyond the current UHCI path.
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
  net/           Ethernet and layered IPv4 networking protocols
  sh/            streams, parser, execution engine, built-in commands
  ui/            window manager, widgets, desktop/taskbar/start menu
  apps/          terminal, explorer, editor, system information, about
  lib/           freestanding string and printf routines
  mm/            page frame allocator and kernel heap
  include/tinos/  public headers for all of the above
tests/host/      native unit tests and the kernel-service shims
tools/           run script, USB image helper, QEMU test driver, screen OCR
                 and font generator
```

`REPORT.md` records what was verified, how, and what remains incomplete.
