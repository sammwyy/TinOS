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

The kernel, text shell, desktop and codecs still share ring 0. No application
ELF executes there: the former native GUI apps are disabled, their ABI is
not linked and the initrd contains only `userland/bin/` executables.
They run in ring 3 through `run <path>` or the desktop menu. Up to four jobs can be
resident on the boot processor; PIT preemption lets the console run even
when a user program loops. They have console I/O and filesystem access
through private integer descriptors; writable files are limited to ramfs.
User programs can create child processes with `spawn` and wait for their
exit without blocking the boot processor.

| Property            | Today                                                        |
|---------------------|--------------------------------------------------------------|
| CPU target          | i386 (`-m32`), protected mode                                |
| Boot                | TinOS El Torito loader, BIOS only, VGA text console at handoff |
| Privilege           | Kernel/shell/desktop ring 0; executable apps ring 3          |
| Paging              | Supervisor identity map (PSE); 4 KiB user pages per process  |
| Memory isolation    | Ring-3 programs cannot access the kernel or each other       |
| Processes           | Four slots, distinct address spaces and kernel stacks; BSP-only scheduling |
| Threads             | Kernel threads, `THREAD_MAX` = 32, 16 KiB stacks             |
| SMP                 | APs brought up via ACPI MADT + LAPIC INIT/SIPI               |
| Interrupts          | 8259 PIC, PIT timer                                          |
| Syscalls            | `int 0x80`: I/O, ramfs, process lifecycle, GUI handles/events, sysinfo, keyboard ioctl, PCM audio |
| Filesystem          | In-memory node tree; FAT16/FAT32 imported read-only          |
| Network             | Polling Ethernet/ARP/IPv4/ICMP/UDP/TCP/DHCP/DNS in the kernel |
| Graphics            | Bochs/QEMU DISPI only, software framebuffer in the kernel    |

### 1.2 Boot flow

```
TinOS BIOS loader (boot/x86/loader.asm, El Torito boot image)
  -> BIOS E820 memory map, optional initrd, ACPI RSDP -> boot_info_t v1
  -> load kernel at 1 MiB, enter 32-bit protected mode
  -> kernel/arch/x86/boot.S     _start, 64 KiB boot stack
  -> kernel/main.c kmain()
       serial, VGA text console
       GDT, IDT, PIC, FPU/SSE, identity paging, PIT, RTC
       PMM (reserves the initrd), heap
       async pool, event bus, threads, timers, driver model
       SMP bring-up
       PS/2 keyboard + mouse, ATA PIO, tmpfs root, FAT mounts
       import_userland(): expose ring-3 ELF files under /apps without executing them
       PCI scan, driver binding (e1000, virtio-blk, AC'97), UHCI, network + DHCP
       if tinos.mode=text: tty_start(); kernel_loop_run(tty_step)
       desktop command resumes kmain
       try gfx_init(1024x768, 800x600, 640x480)
       if no supported mode or not enough RAM: tty_start(); kernel_loop_run(tty_step)
       otherwise: VGA text disabled; desktop_start(); kernel_loop_run(desktop_step)
```

The text console runs the existing in-kernel shell without the graphics
device. `desktop` leaves the console and enters the desktop; it cannot
return to the console. Neither shell nor desktop is a userspace process yet.
From the VGA text console, `run` loads a validated ELF32 into a fresh
address space, copies arguments onto the user stack, assigns a PID and
returns to the prompt. Each process has a 16 KiB kernel stack for its
BSP TSS transitions. Its mapped pages and open-file snapshots are freed on
exit; `jobs` can inspect a root job's status until `reap` collects it or a
full table reuses an exited slot. Child exit statuses stay available to
their parent until `wait` collects them. A waiting parent is BLOCKED and
wakes when the child exits or is killed. Parent exit orphans its children
to the kernel job table. User faults terminate only the affected process.
`int 0x80` validates mapped user
buffers, copies pathnames into the kernel and reads file data through a
kernel bounce buffer. A keyboard read parks the calling process in BLOCKED
state and saves its syscall frame. A key wakes the process and completes
the read without blocking the kernel. The TTY forwards input to the reader
until it exits or Ctrl+C cancels it.
Floating-point instructions trap until per-process FPU state exists.

### 1.3 Kernel subsystems

| Area        | Location                              | Notes |
|-------------|---------------------------------------|-------|
| Arch        | `kernel/arch/x86/`                    | boot stub, GDT, IDT, ISR stubs, PIC, PIT, RTC, CPUID, paging, PMM, ACPI, LAPIC, SMP trampoline, context switch |
| Core        | `kernel/core/`                        | spinlocks, async state machines, timers, event bus, driver model, logging, panic, app ABI table |
| Scheduling  | `kernel/sched/`                       | kernel loop, threads, mutexes/events |
| Loader      | `kernel/proc/`                        | native and ring-3 ELF loaders, asynchronous process lifecycle |
| Syscalls    | `kernel/syscall/`, `kernel/include/uapi/tinos/` | `int 0x80` ABI, user pointer validation |
| Userspace   | `libraries/libc/`, `userland/bin/`   | freestanding crt0, syscall stubs and ELF32 command-line programs |
| Memory      | `kernel/mm/`                          | first-fit coalescing heap, DMA buffers |
| Drivers     | `kernel/drivers/<class>/`             | serial, VGA text, PCI, DISPI, PS/2 keyboard/mouse, ATA PIO, virtio-blk, e1000, UHCI, USB mass storage, AC'97, PC speaker |
| Filesystem  | `kernel/fs/`                          | ramfs node tree, path helpers, block layer, MBR, FAT16, FAT32, ustar reader, async whole-file reads |
| Network     | `kernel/net/`                         | Ethernet through TCP, DHCP, DNS, async jobs |
| Graphics    | `kernel/gfx/`                         | back buffer, dirty-region present, primitives, clipping, 8x16 font |
| Shell       | `kernel/sh/`                          | streams, parser, execution engine, built-in commands, regex |
| Lib         | `kernel/lib/`                         | freestanding `string` and `printf` |

### 1.4 Scheduling

Kernel threads have private stacks and are preemptible from the PIT. On the
boot processor, `kernel/sched/loop.c` owns the idle loop and pumps bounded
audio, bus, async, thread, process, filesystem and timer work before calling
an optional UI tick. `tty_step()` and `desktop_step()` handle only their
respective input and display state; the same loop advances workers when no
UI callback is installed. The boot flow changes from text to graphics by
returning from the text loop and starting the desktop loop.

- The loop calls `process_schedule()` for one READY ring-3 job at a time
  for at most 5 ms. PIT interrupts save the user register frame and resume
  the kernel loop; a syscall that reaches the deadline also yields before
  returning to ring 3. Each job retains its address space and kernel stack
  across slices. Jobs cannot run on application processors.
- On application processors, `smp.c:ap_entry()` loops on `thread_run(4)`.
- The BSP can preempt ring-3 jobs while an AP runs kernel workers; APs do not
  enter ring 3 until they have their own TSS and privilege-transition stack.
- PIT preemption can transfer control to another worker before `thread_run()`
  resumes; its pending state is committed for the worker that actually
  returned to the scheduler.
- `thread_spawn_pinned()` restricts a thread to the boot processor because the
  filesystem and network stack have no SMP-safe locking.

The loop still uses budgeted pull scheduling on the BSP, not per-CPU run
queues. Trusted kernel UI callbacks remain ring 0: one that never returns
can still stall this loop.

### 1.5 Desktop, libraries and applications

- `desktop/` (window manager, widgets, app registry, taskbar, start menu,
  event loop) is compiled and linked **into the kernel image**.
- `libraries/image` and `libraries/audio` (stb_image, simplewebp, stb_vorbis
  wrappers) are also linked into the kernel, with per-directory SSE2 flags.
- `userland/bin/` produces freestanding ELF32 programs against
  `libraries/libc/`; `userland/lib/` produces freestanding ELF32 shared
  objects with the reusable BMP/WAV decoders. The boot image packages
  these binaries and sample BMP/WAV resources. The older ring-0 native
  app ABI is never packed.
- `kernel/proc/dynlink.c` follows bounded ELF32 `DT_NEEDED` chains,
  applies `R_386_RELATIVE`, `R_386_32`, `R_386_GLOB_DAT` and
  `R_386_JUMP_SLOT` only to private writable pages, and offers
  `dlopen`/`dlsym`. Cyclic dependencies and invalid relocation targets fail
  loading. At most eight 64 KiB ET_DYN objects occupy distinct user regions.
  `kernel/arch/x86/paging.c` maps their cached read-only physical frames
  into each process and gives each process private writable pages. Code
  cache frames remain pinned until reboot; user processes cannot modify
  `/apps/lib` or write to the shared code pages. The loader validates
  `DT_INIT`/`DT_INIT_ARRAY` and queues at most 32 user entry points per
  process. `crt0` and the libc `dlopen` wrapper consume that queue in ring 3
  after relocation and dependency initialization. TLS, destructors,
  symbol versions, lazy binding and unload remain unsupported.
- The initrd is imported as read-only backing for files in `/apps`. A
  write detaches the file into private heap storage. The module stays
  reserved while borrowed data exists.
- The desktop registry stores copied paths under `/apps/gui/`, never
  callbacks from application code. Menu selections call `process_spawn()`
  after resolving the file; ELF validation and page mappings are shared with
  the text console's `run` command.
- Former fixed-slot `apps/` sources and the function-pointer ABI remain in
  the repository as porting references, but are excluded from the boot
  image and kernel link. No fixed-slot physical arena is reserved.
- The trusted compositor offers one window handle per process, text-cell
  frame submission (bounded copy-in) and blocking keyboard/mouse/resize/close
  events. The process table wakes only the blocked owner of a window.
  Terminal, Explorer, Editor, Image Viewer, Media Player, Calculator,
  System Information, Control Panel and About are ring-3 programs using
  this ABI. Image Viewer
  decodes BMP in its own address space and submits a validated, copied pixel
  surface (at most 320x192); the compositor owns the resulting buffer.
  Media Player decodes standard PCM WAV in userspace and sends at most 256
  stereo frames per syscall to a single-owner PCM audio stream. GUI timer
  events pace its playback without busy-waiting in the kernel. The audio
  stream is stopped when its process exits, even on a fault or kill.
  Other clients can open the same raw PCM device with a fixed-width format
  (U8/S16, mono/stereo, 8–48 kHz). Each syscall validates and copies up to
  256 frames; the trusted audio boundary converts them to stereo S16
  before AC'97 DMA. `tinos_pcm_wait()` blocks only the owner process until
  a chosen queue threshold is reached, with wakeups from the kernel loop.
  There is no audio capture or virtual audio-device filesystem node yet.
  The terminal handles simple commands itself and bridges its children's
  fd 0/1/2 to a bounded parent-owned byte stream and input queue. Full
  streams block child writers; the compositor wakes them when the terminal
  drains data. Child exit and window events wake only their owner. Programs
  without a terminal parent still write stdout to serial; pipes and a
  general pseudo-terminal device are not implemented.

### 1.6 Dependency direction today

```
userland/bin       ->  libraries/libc + userland/lib/include + kernel/include/uapi/tinos
kernel/main.c      ->  desktop_start()/kernel_loop_run(), codec registration
desktop/registry.c ->  filesystem executable paths (not app code)
desktop            ->  kernel internals (gfx, fs, net, threads, input, shell)
```

The kernel depends on the desktop and the codecs, so none of them can be
removed or replaced independently.

### 1.7 Build and test

- GNU Make, host `gcc -m32`, host `ld`; NASM and `xorriso` for the ISO.
- Byte-reproducible kernel and ISO (`make verify-reproducible`).
- Host unit tests with ASan/UBSan for `kernel/fs`, `kernel/sh`, audio codecs
  (`make test`).
- In-kernel self-test (`tests/kernel/`) selected by the `selftest` kernel command-line flag.
- Scripted QEMU session with screenshot OCR (`make check`, `tests/qemu/qemu_test.py`).
- Current memory acceptance in QEMU: 4 MiB boots the text console without
  the executable archive; 8 MiB starts the desktop with ring-3 menu
  programs (resolution selected from available RAM). With an unsupported display
  adapter, boot continues into the VGA text console.

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
| `desktop/registry.c` scans `/apps/*.elf` | userspace desktop scans versioned manifests itself |
| retired `apps/*` sources | ported userspace clients of the desktop protocol |

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
boot/                      BIOS El Torito loader and kernel linker script
kernel/
  arch/x86/                CPU and platform bring-up
  core/                    logging, panic, locks, timers, driver model
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
apps/                      retired GUI sources, awaiting userspace ports
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
