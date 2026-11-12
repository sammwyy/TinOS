# TinOS

A small operating system for QEMU, written from scratch: its own 32-bit
kernel, VGA graphics driver, window manager, in-memory filesystem and text
shell. It boots through its own BIOS El Torito loader and can start an
optional desktop with a taskbar and a start menu for ring-3 programs.

It is not based on Linux and contains no Linux userspace: the bootloader
uses BIOS services for disk and memory discovery, then enters the kernel,
whose code is from this repository (plus libgcc's arithmetic helpers).
TinOS also has a small, independently linked ELF32 userspace.

```
   +-----------------------------------------------------------+
   |  TinOS BIOS loader  -->  kernel/main.c  (text mode)        |
   |                              |                             |
   |            gdt -> idt/pic -> pit/rtc -> pmm/heap ->        |
   |            keyboard/mouse -> ramfs -> pci -> vbe           |
   |                              |                             |
   |                     desktop/desktop.c                      |
   |          window manager, taskbar, start menu, cursor       |
   |            /            |             |          \         |
   |      terminal      explorer        editor      sysinfo     |
   |            \            |             |          /         |
   |                     kernel/fs (one shared tree)            |
   +-----------------------------------------------------------+
```

## Quick start

```sh
sudo dnf install gcc binutils make nasm xorriso qemu-system-x86 \
                 libgcc.i686           # 32-bit libgcc for -m32 code
make                                    # builds build/tinos.iso
make run                                # boots it in QEMU
```

The first screen is the TinOS boot menu (3 seconds), then the text-mode boot log
for about three seconds, then the desktop.

Press Down twice and Enter in the boot menu to keep the VGA console and run
the interactive shell. Type `desktop` to enter the graphical session. The text
console currently runs in the kernel address space; the desktop is not yet a
separate process and cannot return to the console after it starts.

From the VGA text console, the `run` shell command starts a separate
ring-3 ELF32 executable. For example, `run /apps/hello.elf` prints a
message and exits; `run /apps/echo.elf` reads a line from the keyboard.
`run /apps/cat.elf /README.txt` reads a file through a read-only descriptor
and prints it using the new userspace libc. `run /apps/fault.elf` demonstrates
that a user page fault terminates only that program. Programs receive
arguments on their own user stack and call `int 0x80`; they cannot access
kernel memory. `run /apps/spawn.elf /apps/hello.elf` demonstrates a userspace
parent spawning and waiting for a child. `run` returns to the shell with a PID
immediately; `jobs`
lists processes, `kill <pid>` stops a running process, and `reap <pid>` collects
its exit status. The PIT preempts ring-3 CPU loops every 5 ms, so even
`run /apps/spin.elf` does not block the console. A process reading from the
keyboard sleeps until a key arrives without busy-polling or blocking the
kernel; Ctrl+C stops that reader. The desktop and text shell remain kernel
services, but application ELF files can execute only in ring 3.

At 8 MiB of QEMU RAM the graphical desktop can launch Terminal, File
Explorer, Text Editor, Calculator, System Information, Control Panel and
About as separate ring-3 ELF programs with their own windows. Terminal
connects its child processes to a bounded stdin/stdout channel, so `run`
shows their output and forwards keyboard input; `Ctrl+C` stops a running
child. Programs without a Terminal parent still write stdout to serial.
This is not a full POSIX pseudo-terminal or pipe API yet.
At 4 MiB the loader skips the executable archive and boots into the text
console. Graphics also falls back to
the console on unsupported video hardware. This BIOS boot image is currently
for optical ISO media, not UEFI or a raw USB disk.

## Requirements

| Tool | Why | Fedora package |
|------|-----|----------------|
| `gcc` with `-m32` | compiles the freestanding 32-bit kernel | `gcc`, `libgcc.i686` |
| `ld` | links the kernel with a custom linker script | `binutils` |
| `nasm` | assembles the 16-bit BIOS loader | `nasm` |
| `xorriso` | writes the El Torito ISO | `xorriso` |
| `qemu-system-i386` | runs the image | `qemu-system-x86` |
| `python3` + Pillow + numpy | optional: automated GUI tests | `python3-pillow`, `python3-numpy` |

Everything above is already present on a stock Fedora workstation except
possibly `libgcc.i686` and `nasm`.

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

Every target above builds for `ARCH=x86` (`make ARCH=x86 ...`), the only
target implemented today. The build picks its sources from
`kernel/arch/$(ARCH)/`, so a future port (e.g.
`ARCH=arm`) is a sibling directory tree, not a rewrite of this one; passing
an `ARCH` with no matching directory fails the build immediately with a
clear error instead of a wall of missing-symbol linker errors.

The desktop icons live in `resources/icons/` and are always compiled into the
shell atlas.  The legacy `icon_t` values remain as compatibility handles, but
`widget_icon()` resolves them exclusively to those named bitmap resources;
custom code can use `widget_icon_name(x, y, size, "name")` directly.

The current File Explorer browses directories and previews text through
userspace filesystem calls. The former Image Viewer still needs a safe
pixel-buffer protocol and a userspace decoder; its source remains under
`apps/` as a porting reference.

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
`beep` is audible through the host sound server. Set `AUDIODEV`,
for example to `driver=pa,id=tinos-audio` or `driver=none,id=tinos-audio`, when
another backend is required. Automated headless tests select `none` explicitly.
The `doremi` shell command remains a blocking AC'97 hardware test. The
former graphical Media Player is not included until its userspace port.

The TinOS boot menu has three selections (Down/Up, then Enter):

* **TinOS** - the normal desktop.
* **TinOS (kernel self-test on serial)** - runs the in-kernel assertions over
  the heap, path handling, filesystem and shell before starting the desktop
  and prints the results to the serial port.
* **TinOS (text console)** - stays on the VGA console until `desktop` is
  requested.

## Controls

### Desktop

| Key | Action |
|-----|--------|
| `Win`, `Ctrl+Esc` or `F12` | open / close the start menu |
| `Up` `Down` | move through the focused categories or apps |
| `Enter` | expand / collapse a category, launch the selected app |
| `Right` / `Left` | focus the category's apps / return to the categories |
| `Esc` | close the start menu |
| `Alt+Tab` | switch to the next window |
| `Alt+F4` | close the focused window |

The start menu lists `/apps/gui/*.elf` under *Programs* and launches each
through the process loader. Applications use `int 0x80` to open a window,
submit bounded text-cell frames and block while awaiting keyboard/mouse
events. Terminal supports `ls`, `cd`, `pwd`, `cat`, `echo`, `touch`, `mkdir`,
`rm`, `edit`, `run`, `clear` and `exit`. Explorer browses directories,
previews files and opens them in the editor with `E`; the editor saves
ramfs files with `Ctrl+S`. These are new userspace implementations, not
the old kernel-callback GUI apps; their advanced controls are not yet ported.
`run /apps/echo.elf` demonstrates interactive child I/O in the GUI;
`run /apps/cat.elf /README.txt` displays child output, and Ctrl+C cancels
`run /apps/spin.elf`. The child stdout queue blocks writers when full and
wakes them when Terminal reads it, without busy-waiting in the kernel.
Calculator handles bounded integer arithmetic; System Information reads
system status through a validated syscall. Control Panel changes the
keyboard between US and ES through an `ioctl` on the console descriptor.

The mouse (if present - QEMU provides a PS/2 mouse by default) can click the
Start button and menu entries, focus windows, drag them by the title bar,
resize them by dragging the grip in the bottom-right corner, press the
minimise and close buttons in the title bar, and click taskbar buttons to
raise or minimise a window.

### Historical Graphical App Features

The sections below describe the former ring-0 GUI implementations, kept as
porting references under `apps/`. The new userspace programs do not yet
implement all these features.

### Calculator

The historical Calculator appeared under Start > Accessories. It supported
integer arithmetic with `+`, `-`, `*`, `/`, `%` and parentheses. Type expressions on
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

The VGA text shell operates on the in-memory filesystem shared with ring-3
programs.

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
| Boot | `boot/x86/`, `boot/linker.ld`, `kernel/arch/x86/boot.S` | El Torito BIOS loader, shared boot information, protected-mode entry; kernel linked at 1 MiB |
| Arch | `kernel/arch/x86/` | GDT, IDT + 48 interrupt stubs, 8259A PIC, PIT, CMOS RTC, CPUID, reboot/power-off, page tables - everything specific to the target picked by `make ARCH=...` (only `x86` exists today) |
| SMP | `kernel/arch/x86/{acpi,lapic,smp,trampoline.S}` | ACPI MADT parsing, local APIC, AP bring-up (see below) |
| Memory | `kernel/mm/`, `kernel/arch/x86/{pmm,paging}.c` | bitmap page frame allocator over the boot information memory map and the x86 page tables (arch-specific, under `kernel/arch/x86/`); first-fit heap with coalescing that grows by taking new arenas from the frame allocator (portable, under `kernel/mm/`) |
| Devices | `kernel/drivers/<class>/` | drivers grouped by device class (`bus`, `block`, `net`, `usb`, `input`, `video`, `audio`, `char`): COM1 serial log, VGA text console, PCI enumeration, Bochs DISPI mode setting, PS/2 keyboard, PS/2 mouse with IntelliMouse wheel support |
| Graphics | `kernel/gfx/` | double-buffered framebuffer with multi-region dirty upload, rectangles, lines, gradients, alpha blending, clipping stack, 8x16 bitmap font |
| Filesystem | `kernel/fs/` | node tree, path resolution, textual path helpers |
| Shell | `kernel/sh/` | byte streams, tokeniser/parser, execution engine, 32 built-ins |
| Networking | `kernel/net/` | Ethernet, ARP, IPv4, ICMP, UDP, DHCP, DNS, TCP/HTTP and async jobs |
| Libraries | `libraries/image/` | PNG, JPEG and WebP decoding on top of stb_image/simplewebp; own kernel thread, real SSE2 |
| Libraries | `libraries/audio/` | WAV and Ogg Vorbis decoding, codec dispatch; own kernel thread, real SSE2 |
| Desktop | `desktop/` | window manager, widgets, icons, application registry, taskbar/start menu/event loop |
| Applications | `userland/bin/`, `kernel/proc/`, `kernel/fs/tar.c` | ELF32 programs packed into the initrd and started in ring 3; `apps/` is an unbuilt porting reference |

Each self-contained unit owns its own public headers under its own
`include/tinos/` tree - `kernel/include/tinos/` for the kernel core (shared
by everything, since `dev`/`fs`/`net`/`core` are tightly coupled),
`desktop/include/tinos/` for the window manager API, and
`libraries/image/include/tinos/` / `libraries/audio/include/tinos/` for the
format codecs. `#include <tinos/...>` never changes when a file moves within
a unit; only that unit's `-I` flag would need to go if it were ever pulled
out.

### Decisions worth knowing

**i386, protected mode, separate user mappings.** The kernel uses
supervisor-only 4 MiB identity-mapped pages (`CR4.PSE`); its physical and
virtual addresses are equal. A `run` process gets its own page directory,
with 4 KiB user pages at `0x40000000` for its ELF and a separate stack. It
cannot access the kernel or another process's pages. Kernel-linked apps
still share the kernel address space with no isolation.

**Text mode first, then our own mode switch.** The BIOS loader leaves the
machine in VGA text mode, and the kernel switches to graphics on its own by
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

**The desktop and text shell remain kernel services.** A kernel-owned loop
advances workers and preemptible ring-3 programs before calling an optional
TTY or desktop tick. A CPU-bound userspace program cannot monopolize the
console, and a keyboard read parks only its caller. Kernel UI callbacks
can still block the loop if they never return; per-CPU run
queues and userspace services remain future work.

**Asynchronous workers and kernel threads.** `kernel/core/async.c` provides a
stackless state-machine API for short operations, while `kernel/sched/thread.c`
and `kernel/arch/x86/context.S` provide real kernel threads with private
stacks. Async tasks and threads are both run outside interrupt handlers, and
the kernel loop gives each subsystem a bounded scheduling budget. Threads can
call `thread_yield()`, `thread_sleep()` and `thread_exit()`; networking can
therefore use a blocking-looking worker API without blocking the GUI. The PIT
can preempt a running worker, while explicit yields and blocking calls
remain available for predictable hand-off points.

`kernel/sched/sync.c` supplies scheduler-aware mutexes and auto-reset events.
`kmutex_lock()` and `kevent_wait()` put the current worker in a blocked state
instead of spinning; an unlock or signal wakes it. Interrupt handlers should
only publish data and wake a worker, never acquire a mutex or execute a
blocking operation.

**FPU/SSE is opt-in per translation unit.** Interrupt handlers and most of
the kernel are compiled `-mgeneral-regs-only -mno-sse -mno-sse2`, and their
FPU/SSE register contents are never saved - a wild use would silently
corrupt whatever another thread left there. `libraries/image/` and
`libraries/audio/` are the one exception: they run only on their own kernel
thread (never inside an interrupt handler), get real `-msse2` codegen and
auto-vectorization, and `kernel/sched/thread.c` FXSAVEs/FXRSTORs a 512-byte
area per thread on every context switch (cooperative or PIT-preempted) so
two such threads - say a WAV decode and a JPEG decode - can freely
interleave. `arch_fpu_init()` (`kernel/arch/x86/cpu.c`) enables CR0/CR4 and
resets the FPU once at boot, before any thread runs.

**SMP: real cores, sharing the thread pool.** `kernel/arch/x86/acpi.c` reads
the RSDP found by the BIOS loader and walks RSDT/XSDT to
the MADT to find every CPU's APIC ID and the local APIC's address;
`lapic.c` then sends each one the INIT-SIPI-SIPI sequence. Every
application processor boots through `trampoline.S` - 16-bit real mode code
linked at a fixed low
physical address so its SIPI vector is known at build time - which loads
the boot processor's own flat GDT, enters 32-bit protected mode, and calls
into `smp.c:ap_entry()`, which activates the shared identity-mapped page
directory, enables its own FPU/SSE, loads the shared IDT, brings up its
own local APIC, and then runs `thread_run()` in a loop forever, pulling
work from the same `threads[]` pool the boot processor's kernel loop
does. `kernel/core/spinlock.c` gives the scheduler (and the heap and the
physical frame allocator, both former `irq_save()`-only critical sections
that a second core running concurrently would have raced) real cross-core
locks - `irq_save()`/`irq_restore()` alone only ever stopped the *calling*
core's own interrupts.

Not everything is safe to hand to another core yet, since the filesystem
tree and the network stack still have no locking beyond `irq_save()`.
`thread_spawn_pinned()` keeps threads that touch either of those (file
reads, storage/network workers, and audio *loading*/*decoding*, which
streams from the filesystem a chunk at a time) on the boot processor;
plain `thread_spawn()` is for self-contained, in-memory compute that any
core can run - today, image decode and audio *playback*. The AC'97 driver
(`kernel/drivers/audio/audio.c`) and each media player's decode->playback ring
buffer got their own spinlocks specifically so playback, which only reads
already-decoded samples and pushes them at the device, could move off the
boot processor safely. Verified with `tests/qemu/qemu_test.py` and a stress
scenario that opens several images and an audio track at once under
`-smp 4`.

Getting here surfaced a real, pre-existing bug: `thread_run()` published
`current` before setting `scheduler_switching`, leaving a window where a
timer tick could preempt a thread before its stack switch had actually
happened, capturing an interrupt frame that pointed at the *scheduler's*
stack instead of the thread's own. It was latent on a single core (nothing
ran long enough between yields to hit the window) and became reachable
once a genuinely long-running, non-yielding thread - image decode - gave
the timer something to land on mid-instruction.

**Devices and drivers.** PCI enumeration publishes immutable bus records into
the small driver model (`kernel/core/driver.c`). Drivers register a match and
probe callback, and the model binds each device to at most one matching
driver. PCI helpers expose command/status, IRQ routing, BARs and bus-master
enablement. `kernel/mm/dma.c` exposes physically contiguous DMA buffers behind
an API that can later absorb virtual-to-physical mappings. The e1000 driver
backs the current Ethernet stack; networking is intentionally a small
polling-oriented kernel service rather than a userspace socket subsystem.

**Application isolation.** Only the ELF32 files from `userland/bin/` are
packed into the initrd. `kernel/proc/process.c` checks each executable,
allocates a private address space and enters ring 3; the desktop start menu
stores paths, not function pointers, and starts programs through that same
process loader. The old fixed-address loader and function-pointer ABI are
not linked into the kernel, and the fixed-slot arena is no longer reserved.
The former GUI app sources remain under `apps/` for porting, but are
neither installed nor executable in this build. `make native-apps` fails
intentionally. The current GUI extension uses process-scoped integer
window handles, copied text cells and blocking event delivery; filesystem
calls use file descriptors and path-based metadata. Writes are limited to
64 KiB per file, the installed `/apps` tree is read-only to userspace,
and persistent disk writes, pipes and PTYs are not yet implemented.

## Error handling

Kernel CPU exceptions are fatal and reported: the handler paints a panic screen
with the exception name, error code, faulting `EIP`, `CR2` and a full
register dump, writes the same text to the serial port, and halts. The panic
path also works before the graphics stack is up (it falls back to the VGA
text console) and refuses to recurse. User-originated exceptions instead
terminate only the affected process.

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

* **In-kernel self-test** (`tests/kernel/selftest.c`, second boot selection) runs
  the kernel assertions against the real heap and prints the result to serial.

* **Scripted QEMU session** (`tests/qemu/qemu_test.py`) boots the ISO headless,
  drives it through the QMP monitor (keystrokes, mouse moves, clicks, drags),
  captures screenshots and *reads the text off them*: `tests/qemu/screen_ocr.py`
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
* There are no user accounts or permissions, userspace signals or kill syscall,
  per-CPU run queues,
  NTFS support, or USB host controllers beyond the current UHCI path.
  The ring-3 syscall ABI has console I/O, read-only access to memory-backed
  files, `getpid`, `spawn`, `wait`, and `exit`; it has no writable files or
  graphical APIs. Up to four processes share the boot processor while its
  kernel-owned loop runs. Ring-3 floating-point instructions currently trap;
  an independent FPU context is still pending.
* The RTC is read directly, so the taskbar clock follows the host clock via
  `-rtc base=localtime`; without that flag QEMU reports UTC.
* Only the Bochs/QEMU DISPI graphics interface is supported. On hardware or
  a VM without it the kernel keeps the VGA text console.

## What is original and what is reused

Written for this project: the kernel and all of its subsystems (boot code,
descriptor tables, interrupt handling, timer/RTC, memory management, PCI
scan, VGA/DISPI driver, graphics engine, keyboard and mouse drivers,
filesystem, shell and parser, window manager, widgets and userspace
programs), the build system, the test harnesses and this documentation.

Reused: `libgcc` for 64-bit division helpers; the glyph shapes in
`kernel/gfx/font8x16.c`, which `tools/mkfont.py` rasterised once from
Liberation Mono on the host - the font *renderer* is ours, only the bitmaps
are derived. No operating system code was copied from another project.

## Repository layout

```
boot/            kernel linker script and BIOS El Torito loader under x86/
kernel/
  arch/x86/      GDT, IDT, interrupt stubs, PIC, PIT, RTC, CPU, reset,
                 page tables, physical memory allocator, boot.S - every
                 target's own arch/<ARCH> tree looks like this one
  core/          logging, panic, clock glue, system info, locks, timers
  sched/         kernel threads, mutexes and events
  proc/          the ELF loader
  drivers/       drivers by device class: bus (PCI), block (ATA,
                 virtio-blk), net (e1000), usb (UHCI, mass storage),
                 input (PS/2 keyboard, mouse), video (VGA text, DISPI),
                 audio (AC'97, PC speaker), char (serial)
  gfx/           framebuffer engine and the generated font
  fs/            in-memory filesystem, path helpers, the ustar reader
  net/           Ethernet and layered IPv4 networking protocols
  sh/            streams, parser, execution engine, built-in commands
  lib/           freestanding string and printf routines
  mm/            portable kernel heap (arch-specific page allocation and
                 paging live under arch/<ARCH>/)
  include/tinos/  public headers for all of the above
desktop/         window manager, widgets, icons, application registry,
                 taskbar/start menu/event loop
userland/bin/    programs built against libc for ring 3
apps/            retired ring-0 GUI sources; not built or installed
libraries/       image (PNG/JPEG/WebP) and audio (WAV/Vorbis) codecs
tests/host/      native unit tests and the kernel-service shims
tests/kernel/    in-kernel self-test (linked into the kernel image)
tests/qemu/      QEMU test driver and screen OCR
tools/           run script, USB image helper, icon and font generators
```

Empty placeholder directories (`userland/{init,sh}`, `libraries/{gfx,ui}`,
`kernel/ipc`, `ports/`, `toolchain/`, `docs/`) belong to the
target layout described in [ARCHITECTURE.md](ARCHITECTURE.md).
```

`REPORT.md` records what was verified, how, and what remains incomplete.
