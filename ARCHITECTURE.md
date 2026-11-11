# TinOS Architecture

This document describes two things:

1. **Current architecture** - what the code in this repository does today.
2. **Target architecture** - where the system is heading, and which directory
   each piece belongs in.

The work needed to get from (1) to (2) is tracked in [TODO.md](TODO.md).
Anything under "Target" is a design proposal, not a description of existing
code.

---

## 1. Current architecture

### 1.1 Summary

TinOS today is a single-address-space, ring-0 system. The kernel, the shell,
the window manager, the codecs and every application share one identity-mapped
address space with no privilege separation.

| Property            | Today                                                        |
|---------------------|--------------------------------------------------------------|
| CPU target          | i386 (`-m32`), protected mode                                |
| Boot                | GRUB2, Multiboot2, BIOS only, EGA text console at handoff    |
| Privilege           | Everything in ring 0; flat GDT, no TSS, no ring 3            |
| Paging              | Identity map of the full 4 GiB with 4 MiB PSE pages          |
| Memory isolation    | None                                                         |
| Processes           | None                                                         |
| Threads             | Kernel threads, `THREAD_MAX` = 32, 16 KiB stacks             |
| SMP                 | APs brought up via ACPI MADT + LAPIC INIT/SIPI               |
| Interrupts          | 8259 PIC, PIT timer                                          |
| Syscalls            | None; apps receive a table of kernel function pointers       |
| Filesystem          | In-memory node tree; FAT16/FAT32 imported read-only          |
| Network             | Polling Ethernet/ARP/IPv4/ICMP/UDP/TCP/DHCP/DNS in the kernel |
| Graphics            | Bochs/QEMU DISPI only, software framebuffer in the kernel    |

### 1.2 Boot flow

```
GRUB2 (multiboot2 /boot/tinos.elf + module2 /boot/ramfs.tar)
  -> kernel/arch/x86/boot.S     _start, 64 KiB boot stack
  -> kernel/main.c kmain()
       serial, VGA text console
       GDT, IDT, PIC, FPU/SSE, identity paging, PIT, RTC
       PMM (reserves ramfs module + native app arena), heap
       async pool, event bus, threads, timers, driver model
       SMP bring-up
       PS/2 keyboard + mouse, ATA PIO, tmpfs root, FAT mounts
       load_native_apps(): untar ramfs into /apps, ELF-load and call each app_main()
       PCI scan, driver binding (e1000, virtio-blk, AC'97), UHCI, network + DHCP
       gfx_init(1024x768), VGA text disabled
       desktop_run()   <- never returns
```

`kmain()` unconditionally ends in `desktop_run()`. There is no text-mode
operating mode after boot: the VGA text console is only a boot log.

### 1.3 Kernel subsystems

| Area        | Location                              | Notes |
|-------------|---------------------------------------|-------|
| Arch        | `kernel/arch/x86/`                    | boot stub, GDT, IDT, ISR stubs, PIC, PIT, RTC, CPUID, paging, PMM, ACPI, LAPIC, SMP trampoline, context switch |
| Core        | `kernel/core/`                        | spinlocks, async state machines, timers, event bus, driver model, logging, panic, app ABI table |
| Scheduling  | `kernel/sched/`                       | kernel threads, mutexes/events |
| Loader      | `kernel/proc/`                        | ELF loader |
| Memory      | `kernel/mm/`                          | first-fit coalescing heap, DMA buffers |
| Drivers     | `kernel/drivers/<class>/`             | serial, VGA text, PCI, DISPI, PS/2 keyboard/mouse, ATA PIO, virtio-blk, e1000, UHCI, USB mass storage, AC'97, PC speaker |
| Filesystem  | `kernel/fs/`                          | ramfs node tree, path helpers, block layer, MBR, FAT16, FAT32, ustar reader, async whole-file reads |
| Network     | `kernel/net/`                         | Ethernet through TCP, DHCP, DNS, async jobs |
| Graphics    | `kernel/gfx/`                         | back buffer, dirty-region present, primitives, clipping, 8x16 font |
| Shell       | `kernel/sh/`                          | streams, parser, execution engine, built-in commands, regex |
| Lib         | `kernel/lib/`                         | freestanding `string` and `printf` |

### 1.4 Scheduling

Kernel threads have private stacks and are preemptible from the PIT, but they
are not driven by an independent scheduler loop:

- On the boot processor, threads run when the **desktop event loop** calls
  `thread_run(8)` (`desktop/desktop.c`).
- On application processors, `smp.c:ap_entry()` loops on `thread_run(4)`.
- `thread_spawn_pinned()` restricts a thread to the boot processor because the
  filesystem and network stack have no SMP-safe locking.

Consequence: the desktop is currently part of the scheduler. Removing or
making the desktop optional requires a real idle/scheduler loop in the kernel
first.

### 1.5 Desktop, libraries and applications

- `desktop/` (window manager, widgets, app registry, taskbar, start menu,
  event loop) is compiled and linked **into the kernel image**.
- `libraries/image` and `libraries/audio` (stb_image, simplewebp, stb_vorbis
  wrappers) are also linked into the kernel, with per-directory SSE2 flags.
- `apps/<name>/` each build to a separate `ET_EXEC` ELF32 linked at a fixed
  1 MiB slot starting at `0x01000000` (`apps/Makefile`,
  `kernel/include/tinos/native_app.h`), at most 12 slots.
- The apps are packed into `ramfs.tar`, unpacked into `/apps` at boot, copied
  to their link address by `kernel/proc/elf.c`, and their `app_main()` is
  called once in kernel context. It registers an `app_entry_t` with a
  `launch` callback; from then on the desktop calls into app code directly.
- Apps call the kernel through `tinos_abi_t` (`kernel/include/tinos/abi.h`),
  a struct of roughly 130 function pointers. `apps/lib/shim.c` re-exports
  each one under the kernel function's own name.
- Apps include kernel-internal headers and compile several kernel source
  files (`string.c`, `printf.c`, `path.c`, `spinlock.c`, `stream.c`) directly
  into their own binary.

### 1.6 Dependency direction today

```
apps  ->  kernel headers + desktop headers + library headers  (via ABI table)
kernel/core/abi.c  ->  desktop, libraries/image, libraries/audio, kernel/sh
kernel/main.c      ->  desktop_run(), image_decode(), app manifest parsing
desktop            ->  kernel internals (gfx, fs, net, threads, input, shell)
```

The kernel depends on the desktop and the codecs, so none of them can be
removed or replaced independently.

### 1.7 Build and test

- GNU Make, host `gcc -m32`, host `ld`; `grub2-mkrescue` + `xorriso` for the ISO.
- Byte-reproducible kernel and ISO (`make verify-reproducible`).
- Host unit tests with ASan/UBSan for `kernel/fs`, `kernel/sh`, audio codecs
  (`make test`).
- In-kernel self-test (`tests/kernel/`) selected by the `selftest` kernel command-line flag.
- Scripted QEMU session with screenshot OCR (`make check`, `tests/qemu/qemu_test.py`).

---

## 2. Target architecture

### 2.1 Goals

- A kernel that boots to a usable **text-mode system** by itself: console,
  TTY, shell, core utilities.
- The **desktop is optional**: a normal userspace program that can be started
  at boot or later from a TTY, and can crash or exit without taking the
  system down.
- A stable **syscall ABI** and a libc, so third-party software can be ported.
- Everything stays in one repository and builds into one image.

### 2.2 Target platforms

| Target | CPU | Memory model | Status |
|--------|-----|--------------|--------|
| PC, 32-bit | i386 | MMU, paging | Existing, kept |
| PC, 64-bit | x86_64 | MMU, 4-level paging | Deferred |
| Nintendo DSi | ARM946E-S (ARMv5TE) + ARM7TDMI coprocessor | **MPU only, no MMU**, 16 MiB RAM | Planned |

The DSi has no MMU, so the kernel has two memory configurations:

- **MMU targets**: per-process page tables, higher-half kernel, as described
  in 2.4.
- **No-MMU targets**: one shared address space, position-independent or
  load-relocated executables, `spawn` instead of copy-on-write `fork`, fixed
  stack and heap sizes, MPU regions protecting the kernel from user code.

The DSi runs the full system (processes, shell, desktop), not a reduced
profile. Whether its process isolation uses the plain no-MMU model or a
software-emulated MMU is an open decision (TODO.md).

The desktop is the same on every target: the display driver reports the
mode (resolution, pixel format, screen count) and the desktop lays itself
out for it, from 1024x768 on a PC down to 256x192 on the DSi.

On the DSi the two screens have fixed roles. The top screen is the system
display. The bottom touch screen is an **input panel** owned by the
platform layer: an on-screen keyboard, switchable by an on-panel button to
a touchpad that moves the pointer on the top screen; the D-pad also moves
the pointer and the face buttons click. The panel emits the same key and
pointer events a PC keyboard and mouse produce, so the TTY, the desktop and
apps contain no DSi-specific input code.

```
 top screen (256x192)        console / desktop      <- display driver
 bottom screen (touch)       keyboard | touchpad    <- input panel
 D-pad, A/B/X/Y, L/R         pointer, clicks, mods  <- input panel
                                   |
                     key + pointer events (same as PS/2 on PC)
```

Code above the syscall boundary must not assume `fork`, file-backed `mmap`
or a fixed link address, so the same userspace builds for both. Platform
differences (PCI + ACPI on PC, a static device table and an ARM7 companion
processor on the DSi) stay inside `kernel/arch/<arch>/` and the per-arch
driver list.

### 2.3 Layering

```
+---------------------------------------------------------------+
| apps/            GUI applications (clients of the desktop)    |
+------------------------------+--------------------------------+
| desktop/                     | userland/sh, userland/bin      |
| compositor + shell UI        | text shell, core utilities     |
| (optional process)           |                                |
+------------------------------+--------------------------------+
| libraries/  libc, gfx, ui, image, audio                       |
+---------------------------------------------------------------+
| userland/init   PID 1: mounts, services, getty / desktop      |
+======================= syscall boundary ======================+
| kernel/  syscall, proc, sched, mm, fs (VFS), net, ipc, tty    |
|          drivers/, arch/                                      |
+---------------------------------------------------------------+
```

Dependency rule: arrows only point down. The kernel must build and boot with
`desktop/`, `apps/` and `libraries/` deleted from the tree.

### 2.4 Kernel

- **Privilege and memory** (MMU targets; see 2.2 for no-MMU): unprivileged
  mode for all non-kernel code, a TSS per CPU on x86, per-process page
  tables, kernel mapped in the higher half, copy-in/copy-out
  validation for every user pointer.
- **Processes** (`kernel/proc/`): address space, file descriptor table,
  credentials, working directory, parent/child, exit status. ELF loading by
  `exec` into a fresh address space; no fixed link slots.
- **Scheduler** (`kernel/sched/`): per-CPU run queues and an idle thread, with
  no dependency on any UI loop.
- **Syscalls** (`kernel/syscall/`, `kernel/include/uapi/`): numbered entry
  points, integer handles/file descriptors only, no kernel pointers or kernel
  struct layouts exposed. `uapi/` is the only kernel header tree userspace
  may include.
- **VFS** (`kernel/fs/`): vnode/inode operations, mount table, open file
  objects with offsets, page-granular reads instead of whole-file buffers,
  writable on-disk filesystem, `devfs` for device nodes.
- **TTY** (`kernel/tty/`): line discipline, virtual terminals, a console that
  renders on VGA text or on the framebuffer, pseudo-terminals for terminal
  emulators.
- **IPC** (`kernel/ipc/`): pipes, local sockets or message channels, shared
  memory, and a wait primitive (`poll`-like) over all of them.
- **Drivers** (`kernel/drivers/<class>/`): grouped by device class, not by
  CPU architecture. `kernel/arch/<arch>/` keeps only CPU and platform
  bring-up, and each architecture selects the drivers it builds in
  `kernel/arch/<arch>/drivers.mk`. Drivers reach hardware through
  arch-neutral accessors (MMIO, BAR access, IRQ registration), never through
  x86 port I/O directly.
- **Network** (`kernel/net/`): socket layer exposed through file descriptors.

### 2.5 Userspace base system

- `libraries/libc/`: syscall wrappers, `crt0`, stdio, malloc, string, POSIX
  subset. This replaces `apps/lib/shim.c` and the kernel sources currently
  compiled into each app.
- `userland/init/`: PID 1. Reads the boot mode and starts either a getty on
  the console or the desktop.
- `userland/sh/`: the current `kernel/sh` parser and execution engine, moved
  out of the kernel.
- `userland/bin/`: the current shell built-ins that are really programs
  (`ls`, `cat`, `cp`, `ping`, ...), one binary each.

### 2.6 Desktop as an optional process

`desktop/` stays a top-level directory and becomes a standalone binary
(`/bin/desktop`). It owns the display only while it runs.

```
            +-----------+   IPC channel + shared-memory buffers   +---------+
            |  desktop  | <-------------------------------------> |  app    |
            | compositor|   (create window, damage, input events) | (client)|
            +-----+-----+                                         +----+----+
                  |                                                    |
   /dev/fb0 (mmap)|  /dev/input/*  VT switch                 libraries/ui
                  v                                          libraries/gfx
            +-------------------- kernel --------------------+
```

What the kernel provides to it:

| Need                         | Kernel interface                               |
|------------------------------|------------------------------------------------|
| Pixels                       | framebuffer device, mappable into the process  |
| Keyboard and mouse           | input event devices                            |
| Talking to client apps       | IPC channel + shared memory                    |
| Taking and releasing display | virtual terminal switch                        |
| Audio                        | audio device node                              |

What moves where:

| Today                                   | Target                                   |
|-----------------------------------------|------------------------------------------|
| `desktop/window.c`, `desktop/desktop.c` | `desktop/` (compositor process)          |
| `desktop/widgets.c`, text input widget  | `libraries/ui/` (linked into each app)   |
| `kernel/gfx/` drawing primitives, font  | `libraries/gfx/` (kernel keeps a minimal console renderer) |
| `desktop/registry.c` + `app.conf` parsing in `kernel/main.c` | `desktop/` scans `/apps/*/app.conf` itself |
| `apps/*` `launch` callbacks in kernel context | separate processes spawned by the desktop |

Widgets belong in a library rather than in `desktop/` because apps link
against them; the compositor only needs windows, buffers and input routing.

Boot modes:

- `tinos.mode=text` (kernel command line, or init configuration): init starts
  a getty and the shell on the console. No graphics code runs.
- `tinos.mode=desktop`: init starts `/bin/desktop`.
- From a text session, running `desktop` starts it on demand; exiting it
  returns to the TTY.

### 2.7 Third-party software

- `toolchain/`: scripts that build the cross binutils/GCC and assemble a
  sysroot (libc headers and libraries) for the TinOS target.
- `ports/`: one directory per ported package with its fetch, patch and build
  recipe, built against the sysroot.
- `third_party/`: vendored single-file libraries used by `libraries/`.

---

## 3. Repository layout

Directories marked `(placeholder)` are empty today. The "<-" column names the
code that will move there; the prerequisite for each move is listed in the
"Deferred file moves" table in [TODO.md](TODO.md).

```
boot/                      GRUB config, kernel linker script
kernel/
  arch/x86/                CPU and platform bring-up
  core/                    logging, panic, locks, timers, driver model, app ABI table
  mm/                      heap, DMA                         <- allocator part of arch/x86/pmm.c
  sched/                   threads, mutexes, events
  proc/                    ELF loader; later processes and exec
  syscall/   (placeholder) syscall table and entry
  ipc/       (placeholder) pipes, channels, shared memory
  tty/       (placeholder) console, line discipline, VTs     <- console part of drivers/video/vga_text.c
  fs/                      filesystem tree, FAT, tar, block layer
  net/                     network stack
  gfx/                     framebuffer engine, font          -> libraries/gfx/
  sh/                      shell and built-ins               -> userland/sh/, userland/bin/
  lib/                     freestanding string, printf
  drivers/
    bus/                   PCI
    block/                 ATA PIO, virtio-blk
    net/                   e1000
    usb/                   UHCI, mass storage
    input/                 PS/2 keyboard, mouse
    video/                 VGA text, Bochs DISPI
    audio/                 AC'97, PC speaker
    char/                  serial
  include/tinos/           kernel-internal headers
  include/uapi/tinos/ (placeholder) headers shared with userspace
userland/
  init/      (placeholder) PID 1
  sh/        (placeholder) <- kernel/sh/
  bin/       (placeholder) <- kernel/sh/commands.c, one program per command
libraries/
  libc/      (placeholder) <- apps/lib/shim.c, kernel/lib/
  gfx/       (placeholder) <- kernel/gfx/
  ui/        (placeholder) <- desktop/widgets.c
  image/, audio/           codecs (still linked into the kernel)
desktop/                   window manager, widgets, taskbar (still linked into the kernel)
apps/                      GUI applications
ports/       (placeholder) third-party package recipes
toolchain/   (placeholder) cross-compiler and sysroot scripts
third_party/               vendored libraries
resources/                 icons, wallpaper
tests/
  host/                    host unit tests
  kernel/                  in-kernel self-test
  qemu/                    scripted QEMU session, screen OCR
tools/                     build and run helpers
docs/        (placeholder) long-form documentation split out of README.md
```
