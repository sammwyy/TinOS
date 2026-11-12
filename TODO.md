# TinOS TODO

Technical backlog for turning TinOS from a single-address-space ring-0 system
into a kernel with a userspace, a text-mode base system and an optional
desktop process. The target design is described in
[ARCHITECTURE.md](ARCHITECTURE.md).

Phases are ordered by dependency. Phase 0 can be done on the current code
base without architectural change. Items marked **(decision)** need a choice
before the dependent work starts.

---

## Deferred file moves

The mechanical moves are done: drivers are in `kernel/drivers/<class>/`,
threads and sync primitives in `kernel/sched/`, the ELF loader in
`kernel/proc/`, the self-test in `tests/kernel/`, the QEMU test scripts in
`tests/qemu/`.

The moves below are **not** done because the code cannot work at its
destination yet. Each one is unblocked by the listed prerequisite; moving it
earlier would only rename a directory while the code stays linked into the
kernel.

| Move | To | Do it after |
|------|----|-------------|
| `kernel/sh/{shell,parse,stream,regex}.c` | `userland/sh/` | Phase 2: processes, syscalls, libc, pipes |
| `kernel/sh/commands.c` (split per command) | `userland/bin/` | Same as above, plus `spawn`/`wait` |
| `kernel/include/tinos/{sh,regex}.h` | `userland/sh/` | With `kernel/sh` |
| `apps/lib/shim.[ch]` (replaced, not moved) | `libraries/libc/` | After all nine native apps use the minimal syscall library (now created) |
| `kernel/lib/{string,printf}.c` (userspace copy) | `libraries/libc/` | Phase 2: libc build and sysroot exist; kernel keeps its own copy |
| `kernel/fs/path.c` (userspace copy) | `libraries/libc/` | Same as above |
| `kernel/gfx/gfx.c`, `font8x16.c` | `libraries/gfx/` | Phase 2 console done (kernel has its own text renderer) and Phase 3 framebuffer device exists |
| `kernel/include/tinos/gfx.h` | `libraries/gfx/include/tinos/` | With `kernel/gfx` |
| `desktop/widgets.c`, widget and `text_input_*` part of `desktop/include/tinos/ui.h` | `libraries/ui/` | Phase 3: client protocol defined, `libraries/gfx` draws into arbitrary buffers |
| `parse_manifest()`, `load_icon()`, `load_native_apps()` in `kernel/main.c` | `desktop/` | Phase 3: desktop is a process and can read `/apps` through the VFS |
| `kernel/core/abi.c`, `kernel/include/tinos/{abi,native_app}.h` | deleted | Phase 3: every app is a process |
| `libraries/image`, `libraries/audio` (leave the kernel link; no path change) | - | Phase 3: apps link them directly |
| `resources/icons` generated atlas (`icons_generated.[ch]`) | image files in the root filesystem | Phase 3: desktop loads icons at runtime |
| `kernel/drivers/video/vga_text.c` console logic | `kernel/tty/` | Phase 2: TTY core and console driver interface exist |
| Bitmap allocator part of `kernel/arch/x86/pmm.c` | `kernel/mm/` | Phase 1: PMM split (needs code change, not a plain move) |
| Driver-private headers (`e1000.h`, `ata.h`, `uhci.h`, `virtio_blk.h`, `usb_msc.h`, `vbe.h`) | next to each driver in `kernel/drivers/<class>/` | Phase 1: initcall table (so `kernel/main.c` stops including them) |
| Public error codes and shared structs (new files) | `kernel/include/uapi/tinos/` | Once the existing syscall numbers need structured results |
| README sections (build, shell, filesystem, testing) | `docs/` | Any time; pure documentation |

Placeholder directories that stay empty until their phase: `kernel/ipc`,
`userland/{init,sh}`, `libraries/{gfx,ui}`, `ports`, `toolchain`, `docs`.

---

## Phase 0 - Fixes on the current code base

### Unexplained memory corruption

`kernel/main.c` carries two workarounds for corruption whose root cause was
never found. Both were found by bisection, not diagnosis.

- [ ] Root-cause the `__attribute__((optimize("O0")))` on `parse_manifest()`
      (`kernel/main.c`). The comment attributes it to a GCC miscompile; a
      compiler bug is the least likely explanation. Symptom: hang on the
      first `launch()` call after switching start-menu categories.
- [ ] Root-cause why `app_main()` has to take its fields by value instead of
      a pointer to an `app_entry_t` (`app_main_fn` comment in `kernel/main.c`).
- [ ] Ruled out so far: slot overflow. All nine app ELFs are 12-23 KiB against
      a 128 KiB slot.
- [ ] Candidates not yet checked:
  - `parse_manifest()` stores `kstrdup()` results in `entry`, but the
    defaults (`dir->name`, string literals) have different lifetimes; check
    who frees or retains `name`/`summary`/`category` in `desktop/registry.c`.
  - `app_entry_t` / `image_t` struct layout or calling-convention mismatch
    between kernel and app builds: the kernel uses `-mgeneral-regs-only`,
    `apps/Makefile` does not, and `libraries/` use `-msse2 -mstackrealign`.
  - Stack alignment or stack depth when app code is entered from the boot
    stack versus from a 16 KiB thread stack.
  - Heap corruption from `kernel/fs/tar.c` or `image_decode()` during
    `load_native_apps()`; run the same path under the host ASan harness.
- [ ] Add heap canaries / redzones and a `heap_check()` callable from the
      self-test and from the panic path (`kernel/mm/heap.c`).
- [ ] Add stack guard words to thread stacks and check them on context switch
      (`kernel/sched/thread.c`).

### App loader and ABI

- [x] Validate each app's ELF against its own slot, not the entire arena.
- [x] Reject overflowing ELF program-header and segment bounds before writing
      any segments. Host tests exercise invalid headers and out-of-slot loads.
- [x] Reject two apps linked to the same slot at load time.
- [ ] Slot addresses are assigned by hand in `apps/Makefile`
      (`NATIVE_APP_SLOT_*`) and `NATIVE_APP_MAX_COUNT` is 12. Generate the
      assignment, or build apps as position-independent and relocate at load.
- [ ] `tinos_abi_t` has no version or size field. Add `abi_version` and
      `abi_size` as the first members and reject mismatching apps.
- [ ] Freeze field order in `tinos_abi_t`: append only, never insert.
- [x] Correct the native app count in `native_app.h`.

### Low-memory i386 boot

The target is a usable text or graphical session on i386 PCs with 4-16 MiB
of physical RAM. The current prototype still needs SSE2 and Bochs DISPI for
graphics; this is not yet a claim of support for arbitrary PCs.

- [x] Keep app slots inside physical RAM: check each slot against the
      boot information memory map before writing executable pages, and reject a
      slot overlapping the initrd module.
- [x] Relink nine bundled apps to 128 KiB slots at 6 MiB (previously 1 MiB
      slots starting at 16 MiB); enforce slot size at link time. This is a
      transitional fixed-address scheme pending per-process relocation.
- [x] Place the initrd deterministically immediately after the kernel; the
      BIOS loader does not drop the module under memory pressure.
- [x] Grow heap in 256 KiB minimum arenas rather than reserving 4 MiB at
      initialization; allow a 16 MiB machine to boot the full desktop.
- [x] Replace GRUB/Multiboot2 with a TinOS El Torito BIOS loader. At 4 MiB
      it omits the native-app initrd, enters the kernel in protected mode
      and boots the TTY. The previous GRUB2 relocator failed before handoff.
- [ ] Implement a BIOS disk boot path (raw USB/HDD image). The current boot
      image uses El Torito CD-ROM BIOS services and is not a USB bootloader.
- [ ] Add an alternative firmware loader for UEFI PCs. Keep both loaders
      behind the same `boot_info_t`; there is no universal BIOS/UEFI/DSi
      executable.
- [ ] Harden the BIOS loader for real hardware: E820 failure handling,
      verify ACPI RSDP before use, A20 activation fallback, EDD/ATAPI
      compatibility, disk read errors and a memory bound for every
      kernel/initrd write. QEMU exercises only one BIOS implementation.
- [ ] Avoid loading and unpacking graphical apps during text-only boot; a
      later `desktop` command should load them on demand if RAM permits.
- [x] Exercise the 640x480 fallback at 8 MiB with the desktop and a terminal
      window clamped to the available workspace.
- [ ] Set a minimum usable GUI resolution and memory budget per app. The
      desktop back buffer needs 3 MiB at 1024x768x32 or 1.2 MiB at
      640x480x32, before any window cache or decoded image.
- [x] Detect graphics allocation or driver failure and remain in text
      mode instead of panicking; text mode works without DISPI.
- [x] Try 1024x768, 800x600, then 640x480 when memory permits, preserving
      at least 1 MiB of free frames for other work. Buffer allocation happens
      before changing the video mode.
- [ ] Choose display modes from adapter capabilities and use a tested
      small-screen desktop layout, rather than fixed fallback dimensions.
- [x] Bind files in the ramfs to the reserved initrd instead of
      copying every ELF/icon/manifest into a second heap buffer. On write,
      allocate a private copy. An 8 MiB boot with nine apps now has about
      2672 KiB free before graphics and starts a 640x480 desktop.
- [ ] Continue reducing the compositor and per-window surface memory
      footprint: every opened window currently allocates a cached
      32-bit surface in addition to the desktop back buffer.
- [ ] Remove the SSE2 boot requirement or provide a scalar codec build for
      older i386 processors. Test on representative pre-SSE2 hardware.
- [ ] Ensure FAT/ramfs and the app loader degrade gracefully under OOM;
      track kernel, ramfs, app slots, heap and framebuffer usage separately.
- [x] Add QEMU tests for 8 and 16 MiB, and for a non-DISPI VGA adapter.
- [x] Add an 8 MiB graphical boot/window-layout acceptance test.
- [x] Add a 4 MiB text console boot test under QEMU.
- [ ] Extend
      low-memory testing to file editing, image decode and multiple windows.

### Build

- [ ] Kernel and app `CFLAGS` are duplicated in `Makefile` and
      `apps/Makefile` and already differ (`-mgeneral-regs-only`,
      `-Wno-address-of-packed-member`). Move shared flags to one included
      `.mk` file.
- [ ] `apps/Makefile` has no header dependency tracking (`-MMD -MP`); a
      header change does not rebuild apps.
- [ ] `apps/Makefile` treats every directory under `apps/` except `lib` as an
      app. Make the app list explicit or require `app.conf` to be present.
- [ ] `HOST_BIN` is rebuilt as one compile of all sources; split into objects.
- [ ] `distclean` is an alias of `clean`.
- [ ] Generate `compile_commands.json` instead of hand-maintaining `.clangd`.

### Repository hygiene

- [ ] Add a `LICENSE` file at the repository root.
- [ ] Add CI: `make test`, `make check`, `make verify-reproducible`.
- [ ] `README.md` contradicts the code:
  - "all five applications"; nine exist.
  - References `REPORT.md`, which is git-ignored.
- [ ] Split `README.md` (40 KB) into `docs/`: build, shell reference,
      filesystem, testing. Keep README to overview and quick start.
- [ ] `TINOS_DEFAULT_USER "sammy"` is hard-coded in
      `kernel/include/tinos/fs.h`.
- [ ] `.gitignore` is missing a trailing newline; `tools/__pycache__` exists
      in the tree on disk.

---

## Phase 1 - Kernel foundations

### Target and toolchain

- [x] Targets decided: **i386** stays the PC target, **Nintendo DSi** (ARM9)
      is the second target. **x86_64 is deferred** (see "Port: x86_64
      (deferred)").
- [ ] Build with cross-compilers (`i686-elf-gcc`,
      `arm-none-eabi-gcc`) instead of host `gcc -m32`; scripts go in
      `toolchain/`. `CC`/`LD` are hard-coded to `gcc`/`ld` in both Makefiles.
- [x] Use TinOS's own BIOS El Torito loader and a versionable `boot_info_t`
      handoff. An independent UEFI boot path remains future work.

### Scheduler independent of the desktop

- [x] The text console runs without starting graphics; kernel services no
      longer depend on its input loop (`kernel/sched/loop.c`).
- [x] Commit the state of the worker that actually returns to the scheduler
      after PIT preemption, not the worker originally dispatched. The
      boot-menu kernel self-test exercises this transition.
- [x] Add a kernel-owned scheduler/idle loop on the boot processor, with
      bounded service polling and an optional TTY/desktop tick. The boot-menu
      self-test runs a worker through the loop without either UI.
- [ ] Move blocking legacy ring-0 UI/native app work out of the loop.
- [x] Move `kernel/core/thread.c` and `sync.c` to `kernel/sched/`.
- [ ] Replace the `thread_run(budget)` pull model with per-CPU run queues and
      preemptive switching on every CPU.
- [ ] Remove `THREAD_MAX` = 32; allocate thread control blocks dynamically.
- [ ] Remove `thread_spawn_pinned()` once `kernel/fs` and `kernel/net` have
      real locking (below).

### Virtual memory

- [ ] Replace the 4 GiB identity map with a kernel mapped in the higher half
      and unmapped low memory, so null and wild pointers fault.
- [ ] Virtual memory manager: address-space object, map/unmap/protect,
      page-fault handler, kernel virtual allocator for MMIO and stacks.
- [ ] Guard pages under kernel stacks.
- [ ] Split `kernel/arch/x86/pmm.c`: the bitmap frame allocator is
      architecture-independent and belongs in `kernel/mm/`; only memory-map
      discovery is arch-specific.
- [ ] `kernel/mm/dma.c`: stop assuming physical == virtual.
- [ ] Framebuffer, LAPIC, PCI BARs: map through the VMM instead of relying on
      the identity map.

### Interrupts and time

- [ ] IOAPIC support; the kernel routes device IRQs through the 8259 PIC
      while running multiple CPUs.
- [ ] Per-CPU LAPIC timer for scheduling ticks instead of the PIT.
- [ ] HPET or TSC-based monotonic clock.

### Portability to other architectures

Targets: **i386** (existing, kept) and **Nintendo DSi** (ARM9). x86_64 is
deferred. This section is the arch-neutral groundwork the DSi port needs;
the target-specific work is in "Port: Nintendo DSi" below.

State today: only `kernel/arch/x86` exists. `kernel/fs`, `kernel/net`
protocols, `kernel/mm/heap.c`, `kernel/sh` and `kernel/lib` are portable C.
The items below are what any second architecture trips over.

- [x] Replace `sti; hlt` in the desktop, TTY, network waits and timer sleep
      with `arch_idle()`, implemented by `kernel/arch/x86/cpu.c`.
- [ ] `kernel/include/tinos/arch.h` is the x86 interface, not an arch-neutral
      one: it exposes `regs_t` with x86 registers, `pic_*`, `irq_install()`
      with 8259 IRQ numbers and CPUID helpers, and is included by generic
      code (`kernel/core`, `kernel/sched/thread.c`, `kernel/net`,
      `kernel/gfx/gfx.c`, `desktop/desktop.c`). Define a neutral contract
      (`arch_irq_save/restore`, `arch_idle`, `arch_context_switch`,
      `arch_irq_register`, `arch_cpu_count`, `arch_reboot`) and move the x86
      details to `kernel/arch/x86/include/`.
- [ ] `kernel/include/tinos/io.h` (x86 port I/O) is in the shared include
      tree. Move it under `kernel/arch/x86/include/` and add an MMIO accessor
      API (`mmio_read32` etc.) for portable drivers.
- [ ] 11 of the 13 drivers call `inb`/`outb` directly; only `net/e1000.c` and
      `usb/usb_msc.c` do not.
  - PC-only by nature, stay x86-only: `char/serial.c` (COM1 ports),
    `video/vga_text.c`, `input/keyboard.c`, `input/mouse.c` (i8042),
    `audio/speaker.c`, `block/ata.c` (legacy ISA ports).
  - Portable devices with x86-only access code, to fix: `bus/pci.c`
    (config space via ports 0xCF8/0xCFC; add ECAM), `block/virtio_blk.c`
    (legacy I/O-port transport; add virtio-mmio / modern PCI),
    `usb/uhci.c` and `audio/audio.c` (I/O BARs; go through a BAR accessor
    that handles both I/O and memory BARs), `video/vbe.c` (DISPI ports; add
    a generic bootloader framebuffer driver).
- [ ] `kernel/sched/thread.c` includes `arch.h` and manipulates `regs_t`
      frames and FXSAVE areas; isolate behind `arch_thread_init_frame()` and
      `arch_fpu_save/restore()`.
- [ ] Interrupt controller abstraction (8259/IOAPIC on x86, GIC on ARM, PLIC
      on RISC-V) and a timer abstraction (PIT/LAPIC, generic timer, CLINT).
- [x] `kmain()`, PMM and SMP consume size- and version-checked `boot_info_t`
      (memory map, command line, initrd and ACPI RSDP) instead of
      Multiboot2 tags.
- [ ] Add optional framebuffer/devicetree fields and compatibility rules
      for extending the versioned handoff on future platforms.
- [ ] Device discovery without PCI/ACPI: devicetree parser and a platform bus.
- [ ] `kernel/main.c` calls x86 functions by name (`gdt_init`, `idt_init`,
      `paging_init`, `rtc_init`, `vga_text_*`, `keyboard_init`, `mouse_init`,
      `ata_pio_init`). Replace with `arch_early_init()` plus the initcall
      table.
- [ ] Early console abstraction so boot logging does not call `vga_text_*`
      and `serial_write` directly.
- [ ] Word-size and endianness audit: `uint32_t` used for addresses and
      sizes (`pmm`, `elf.c` is ELF32/EM_386 only, DMA, framebuffer BARs).
- [ ] Per-arch pieces outside the kernel: `boot/linker.ld`, `boot/x86/`,
      `apps/link.ld`, `-m32`/`elf_i386` flags in both Makefiles, the SSE
      `CODEC_CFLAGS`, `tools/run.sh` (QEMU binary and machine).
- [ ] Userspace later: per-arch syscall entry, `crt0` and syscall stubs in
      `libraries/libc/arch/<ARCH>/`.

### Port: x86_64 (deferred)

Not scheduled. Kept as a reference list for when it is picked up; i386
remains supported alongside it. Both current targets are 32-bit, so the
syscall ABI and libc will be designed 32-bit first - avoid baking
`sizeof(long) == 4` or 32-bit pointers into `uapi` structs (use fixed-width
types) so a 64-bit port stays possible.

- [ ] Directory split: `kernel/arch/x86/` shared code (PIC, PIT, RTC, LAPIC,
      ACPI, CPUID) versus per-mode code (`boot.S`, `context.S`, `isr.S`,
      `trampoline.S`, `gdt.c`, `idt.c`, `paging.c`). Either
      `arch/x86/{32,64}/` or `arch/i386` + `arch/x86_64` + `arch/x86-common`.
- [ ] Boot: the TinOS BIOS loader enters in 32-bit protected mode; add the long-mode
      transition in the boot stub (enable PAE, set `EFER.LME`, load a 64-bit
      GDT, far jump), or switch to a bootloader that enters in long mode
      (Limine) **(decision)**.
- [ ] 4-level paging; replace the 4 MiB PSE identity map
      (`kernel/arch/x86/paging.c`).
- [ ] 64-bit GDT, TSS with IST stacks, 16-byte IDT gates.
- [ ] Rewrite `isr.S` and `context.S` for the 64-bit register set and
      interrupt frame; rewrite `regs_t`.
- [ ] AP trampoline (`trampoline.S`): add the long-mode step.
- [ ] Compiler flags: `-m64 -mcmodel=kernel -mno-red-zone`, `elf_x86_64`;
      SSE is baseline on x86_64, so revisit `-mgeneral-regs-only` and the
      per-directory `CODEC_CFLAGS`.
- [ ] ELF64 / `EM_X86_64` support in `kernel/proc/elf.c`.
- [ ] `uint32_t` addresses and sizes: `pmm.c`, `dma.c`, PCI BARs (64-bit
      BARs), framebuffer address, `mb_tag_module_t` handling, memory above
      4 GiB in the PMM.
- [ ] `apps/link.ld`, `apps/Makefile`: 64-bit link and flags.
- [ ] `tools/run.sh` and `tests/qemu/qemu_test.py`: `qemu-system-x86_64`.
- [ ] Syscall entry via `syscall`/`sysret` (Phase 2).

### Port: Nintendo DSi (ARM9)

Hardware facts that shape the port (verify register-level details against
GBATEK before implementing):

| Property | DSi | Consequence |
|----------|-----|-------------|
| Main CPU | ARM946E-S, ARMv5TE, 32-bit, up to 133 MHz | New `kernel/arch/arm/`, ARM + Thumb code, no FPU, no SIMD |
| Memory protection | **MPU (8 regions, CP15), no MMU** | No virtual memory, no per-process address spaces, no demand paging |
| RAM | 16 MiB main RAM | Kernel + userspace must fit; no 3 MiB back buffer |
| Second CPU | ARM7TDMI with its own program | Sound, touchscreen, power, RTC, Wi-Fi sit behind it; needs a separate ARM7 binary and an IPC FIFO protocol |
| Display | Two 256x192 screens, 15/16-bit colour | Desktop designed for 1024x768x32 does not fit as is |
| Input | Buttons, resistive touchscreen, no keyboard | On-screen keyboard; touch as pointer |
| Storage | SD/MMC host controller, internal NAND | New block driver; FAT is already supported |
| Network | Wi-Fi only (no Ethernet) | Wireless driver + WPA supplicant before any networking |
| Buses | No PCI, no ACPI, no devicetree | Static platform device table |
| Emulation | melonDS, no$gba (not QEMU) | `tests/qemu` harness (QMP + screenshots) does not apply |

The missing MMU conflicts with parts of the target architecture in
ARCHITECTURE.md (higher-half kernel, per-process page tables, `mmap` of
files, copy-on-write `fork`). The kernel has to support a **no-MMU
configuration** alongside the MMU one:

- [x] Scope decided: **full OS with processes** on the DSi, not a reduced
      profile. Processes run without an MMU.
- [ ] **(decision)** How process isolation works without an MMU:
  - *No-MMU model* (default assumption): shared address space, relocated
    executables, MPU protects the kernel and the non-running processes'
    memory as far as 8 regions allow.
  - *Software-emulated MMU*: run user code through an interpreter or
    translated/instrumented code that checks every memory access. Gives
    real per-process address spaces but costs a large constant factor on a
    133 MHz CPU; evaluate with a prototype before committing.
- [ ] `kernel/mm`: abstract "address space" so it is page tables on x86 and
      a flat physical allocation set on no-MMU targets.
- [ ] Process model without an MMU: no `fork` with copy-on-write. Use
      `spawn`/`vfork`+`exec` as the portable primitive. This settles the
      `fork` vs `spawn` open decision in favour of `spawn`.
- [ ] Executable format for no-MMU: all processes share one address space,
      so binaries must be position-independent or relocated at load (PIE
      with load-time relocation, or FDPIC/bFLT-style). The current
      fixed-link-address scheme does not scale to this either.
- [ ] `kernel/proc/elf.c`: `EM_ARM`, relocation processing.
- [ ] Fixed-size user stacks and heaps (no growth by page fault);
      per-process memory limit declared in the executable or manifest.
- [ ] MPU use: protect kernel memory from user code and switch the user
      region(s) on context switch. With 8 regions isolation between
      processes is coarse; document what is and is not guaranteed.
- [ ] Syscall entry via `svc`; user/system mode split still applies.
- [ ] `mmap`: anonymous and shared memory only; file mappings become reads.
- [ ] Memory footprint budget: kernel image, heap, thread stacks
      (`THREAD_STACK_SIZE` 16 KiB x N), file cache. `kernel/fs/ramfs.c`
      keeps whole files in RAM, which is not viable in 16 MiB.
- [ ] Build-time kernel configuration to drop subsystems (network stack,
      codecs, SMP, USB, PCI) - required here, not optional.

Arch and platform bring-up:

- [ ] Toolchain: `arm-none-eabi-gcc` (`-mcpu=arm946e-s -mthumb-interwork`),
      added to `toolchain/`.
- [ ] `kernel/arch/arm/`: startup code, exception vectors (reset, undefined,
      SVC, prefetch/data abort, IRQ, FIQ), CP15 setup (caches, MPU regions),
      context switch, `arch_irq_save/restore`, `arch_idle`.
- [ ] Platform layer `kernel/arch/arm/dsi/` (or `kernel/platform/dsi/`):
      memory map, interrupt controller (IE/IF/IME registers), hardware
      timers, static device table. Keep CPU code separate from the board so
      another ARM board can reuse it.
- [ ] Boot format: produce a `.nds`/DSi-mode image with ARM9 and ARM7
      binaries and header, loadable by a homebrew launcher; build script in
      `tools/`. No GRUB, no Multiboot2 - depends on the `boot_info`
      abstraction above.
- [ ] Initrd/ramfs delivery without a bootloader module: embed in the image
      or load from SD.
- [ ] ARM7 companion program (`kernel/arch/arm/dsi/arm7/` or a top-level
      `firmware/`): owns sound, touchscreen, RTC, power management, Wi-Fi
      hardware; define the FIFO message protocol with the ARM9 kernel.
- [ ] No SMP on this target: the ARM7 is a peripheral processor, not a
      second scheduler CPU. `smp_init()` and spinlocks must compile out.
- [ ] No hardware divide and no FPU: libgcc/soft-float routines; audit
      `float`/`double` use in `libraries/audio` (stb_vorbis) and
      `libraries/image` for performance.

Drivers (`kernel/drivers/`):

- [ ] `video/`: DS 2D engine in bitmap/framebuffer mode for both screens.
- [ ] `input/`: button matrix; touchscreen via the ARM7.
- [ ] `block/`: DSi SD/MMC host controller; later internal NAND (read-only
      first; writing NAND can brick a console).
- [ ] `audio/`: sound channels via the ARM7.
- [ ] `char/`: debug output channel (emulator debug port; no UART).
- [ ] RTC and power (sleep on lid close, backlight, shutdown) via the ARM7.
- [ ] `net/`: DSi Wi-Fi (SDIO Atheros-based) - large; needs an 802.11 layer
      and WPA2. Lowest priority.
- [ ] `kernel/arch/arm/drivers.mk` listing the above.

Console, desktop and apps on a 256x192 dual-screen device:

- [ ] Console: 8x16 font gives 32x12 characters on the top screen; add a
      smaller font (e.g. 4x6 or 6x8). The console uses the top screen only.
- [ ] `gfx`: support 16-bit pixel formats (currently 32-bit `color_t`
      framebuffer) and non-fixed resolution.
- [x] Decided: one desktop for every target. The display driver reports the
      mode and the desktop always adapts to it; no separate small-screen UI.
- [ ] Display driver interface reports width, height, pixel format and
      screen count; nothing above the driver assumes a resolution. Remove
      `SCREEN_WIDTH`/`SCREEN_HEIGHT` (1024x768) from `kernel/main.c`.
- [ ] Desktop layout computed from the reported mode: taskbar height, start
      menu, window default sizes, title bars and fonts scale down to
      256x192; windows open maximized below a width threshold.
- [x] Dual-screen mapping decided: the **top screen is the OS display**
      (console or desktop); the **bottom touch screen is an input panel**
      and never shows OS content. See "DSi input panel" below.
- [ ] Apps: window sizes and widget metrics are pixel constants
      (`apps/*`, `desktop/widgets.c`); replace with layout relative to the
      client rect and font metrics.
- [ ] Add a small-resolution QEMU test run (e.g. 320x240) on i386 so layout
      regressions show up without DSi hardware.

DSi input panel (bottom screen):

The panel turns DSi hardware input into ordinary keyboard and pointer
events, so the TTY, the desktop and apps see a normal keyboard and mouse and
need no DSi-specific code. It has to work in text mode before any desktop
exists, so it lives below the desktop: in the DSi platform layer
(`kernel/arch/arm/dsi/`) at first, feeding the same input event queues the
PS/2 drivers feed on PC.

- [ ] Panel owns the bottom screen framebuffer; the display driver exposes
      only the top screen (256x192) as the system display.
- [ ] **Keyboard mode**: on-screen keyboard; a touch on a key emits a key
      press/release event. Layout pages for letters, digits/symbols, and
      modifiers (Shift, Ctrl, Alt as sticky keys), plus Esc, Tab, Enter,
      Backspace and arrow keys.
- [ ] **Touchpad mode**: the bottom screen acts as a relative touchpad;
      dragging moves the pointer on the top screen, tap is a left click.
- [ ] Mode toggle button always visible on the panel, switching between
      keyboard and touchpad.
- [ ] D-pad moves the mouse pointer in both modes (with acceleration while
      held).
- [ ] Physical button mapping (proposal, adjust after testing): A = left
      click, B = right click, X/Y = scroll up/down, L/R = hold for
      Shift/Ctrl, Start = Enter, Select = toggle panel mode.
- [ ] In text mode with no pointer consumer, the D-pad emits arrow keys
      instead of pointer motion.
- [ ] Touch input pipeline: raw samples from the ARM7, calibration (read
      the firmware calibration data), debouncing, pressure threshold.
- [ ] Key repeat for held on-screen keys; visual feedback on press.
- [ ] Keyboard layout of the panel follows the system layout setting.
- [ ] Later, when userspace exists: consider moving the panel to a
      userspace input service; keep the event interface identical.
- [ ] Generalise as a "virtual input panel" usable on any touch-only
      target, and runnable on i386/QEMU in a second window or region for
      testing.

Testing:

- [ ] Abstract `tests/qemu/qemu_test.py` into an emulator-neutral harness
      (boot, inject input, capture screen, read log) with QEMU and melonDS
      backends.
- [ ] Test on real hardware regularly; emulators differ from the console in
      timing and SD behaviour.

### Locking

- [ ] `kernel/fs`: the node tree is protected only by `irq_save()`. Add
      per-node or per-mount locks.
- [ ] `kernel/net`: same; add locking to the stack and the e1000 driver.
- [ ] `kernel/gfx` and the desktop state are unlocked and boot-processor only.
- [ ] Add a lock-order document and debug-build lock-order checking.

### Driver model

- [x] Move drivers from `kernel/dev/x86/` to `kernel/drivers/<class>/`.
- [x] Per-architecture driver selection: `kernel/arch/<ARCH>/drivers.mk`
      lists the files under `kernel/drivers/` that the target builds.
- [ ] `kmain()` calls each driver's register and init function by name
      (`e1000_driver_register()`, `ata_pio_init()`, `uhci_usb_init()`, ...).
      Replace with a linker-section initcall table.
- [ ] Remove `DRIVER_MAX` = 32 and `DEVICE_MAX` = 64 static limits.
- [ ] `device_bus_t` only knows PCI; add USB and platform buses.
- [ ] Driver-private headers (`e1000.h`, `ata.h`, `uhci.h`, `virtio_blk.h`,
      `usb_msc.h`, `vbe.h`) sit in the shared `kernel/include/tinos/`; move
      them next to their drivers.
- [ ] USB: only UHCI exists; add EHCI/xHCI and a HID class driver.
- [ ] Graphics: only Bochs DISPI is supported; accept a bootloader-provided
      linear framebuffer as a generic fallback.
- [ ] Storage: AHCI and NVMe; ATA is PIO only.

---

## Phase 2 - Userspace

### Ring 3 and processes

- [x] Add i386 ring-3 code/data segments and a BSP TSS with a kernel stack.
- [ ] Add one TSS per CPU before allowing APs to execute user processes.
- [x] `run <ELF32 file>` allocates a separate page directory with supervisor
      kernel mappings, 4 KiB user pages and a user stack; it validates the ELF,
      enters ring 3 and frees the address space on return. Initial programs
      live in `userland/bin/`; the nine native desktop apps still run in ring 0.
- [x] Preempt i386 user execution with the PIT and resume its saved register
      frame in a later slice. Up to four processes have separate address
      spaces and kernel stacks on the BSP; `run` returns a PID immediately.
      `jobs`, `kill` and `reap` control them from the text console. QEMU
      covers concurrent user jobs with 8/16 MiB, and kernel AP startup
      alongside BSP user jobs with two CPUs and 64 MiB.
- [x] Park a process in BLOCKED state on empty keyboard input and wake it
      from the TTY when a key arrives. The kernel never waits inside the
      syscall or burns a time slice polling for the next character.
- [ ] Replace the single stdin owner with a TTY line discipline and
      process-group/foreground-job policy, including backspace and raw mode.
- [x] Move scheduling and idle handling out of the TTY/desktop event loops
      into `kernel/sched/loop.c`, so both interfaces can be replaced without
      replacing the scheduler.
- [ ] Add kernel-owned run/wait queues and schedule user jobs on APs after
      per-CPU privilege-transition stacks and locking are in place.
- [x] Add parent-child relationships and `getpid`, `spawn`, `wait` syscalls.
      Parents wait without blocking the kernel, child statuses are retained
      until wait, and exiting parents orphan their children to the kernel.
- [ ] Add userspace `kill`/signal handling and process groups. Job control
      (`jobs`, `kill`, `reap`) is currently exposed only in the kernel shell;
      root jobs may be evicted if all four slots are occupied.
- [ ] Route process standard streams through terminal/TTY file descriptors
      so `run` works from the graphical terminal too. It currently starts
      only from VGA text; jobs that persist into desktop mode write to serial.
- [x] Processes have separate address spaces, kernel stacks, PIDs, exit
      status and 16-slot read-only descriptor tables with independent offsets.
      Mutable ramfs files are copied on open so unlinking a file cannot
      leave a process descriptor pointing to freed memory.
- [ ] Expand process state with threads and working directory.
- [x] `exec` loads an ELF into a fresh address space with a user stack,
      `argc`/`argv` and an entry point; the booting shell owns the initial
      arguments.
- [ ] Supply an environment, relocatable executables and dynamic linking.
- [x] Use `spawn` plus `exit` and `wait` as the initial process lifecycle,
      rather than requiring `fork`/copy-on-write. The current `spawn` accepts
      only memory-backed ELF32 files from the root filesystem.
- [x] Validate mapped user pages in console `read`/`write` and reject bad
      pointers without a kernel fault.
- [x] Copy validated user pathnames into the kernel; transfer file reads
      through a kernel bounce buffer rather than handing device code a
      virtual user pointer.
- [ ] General copy-in/copy-out helpers with fault recovery for future VFS,
      IPC and ioctl syscalls.
- [x] A fault in ring 3 terminates the process instead of calling `panic()`.
- [x] Raise #NM instead of exposing the kernel's FPU/SSE registers to ring 3;
      restore the kernel's FPU availability after process exit.
- [ ] Add per-process FPU/SSE state so userspace can use floating point
      without sharing the kernel's state.
- [ ] Prevent a process's user mapping at 1 GiB from masking physical
      identity-mapped kernel RAM on machines with more than 1 GiB, by
      separating kernel virtual mappings from physical addresses.
- [x] Reject overlapping/W+X segments and protect executable pages
      read-only after loading. A userspace write to text now faults.
- [ ] Add an NX-capable backend where supported; page tables without PAE
      cannot mark writable stack/data pages non-executable.
- [ ] Reserve a low-memory base userspace program separately from the native
      app archive: on 4 MiB the loader intentionally omits the entire initrd.
- [ ] Signals or an equivalent asynchronous notification mechanism **(decision)**.

### Syscall interface

- [x] `int 0x80` ring-3 syscall gate with `exit`, process-blocking keyboard `read`,
      console `write`, read-only `open`/`read`/`close` for memory-backed VFS
      files, `getpid`, `spawn` and `wait`.
- [ ] Add a fast syscall entry if useful after the ABI stabilizes.
- [x] Place the current syscall numbers under `kernel/include/uapi/tinos/`;
      the sample userspace binaries include only this public header.
- [x] Build a separate `libraries/libc/` with i386 syscall stubs and `crt0`;
      `userland/bin/cat.c` uses no kernel-private headers.
- [ ] Define complete public error codes and versioned structs before adding
      richer filesystem or graphical APIs; `-1` is currently the only public
      error return.
- [ ] No kernel pointers across the boundary. Today's ABI exposes
      `fs_node_t *`, `window_t *`, `kernel_thread_t *`, `shell_t *`,
      `audio_decoder_t *`; replace with integer handles / file descriptors.
- [ ] No callbacks from kernel into app code. Today's ABI takes
      `bus_listener_fn`, `fs_io_complete_fn`, `thread_entry_fn` and
      `app_entry_t.launch`; replace with events the process reads.
- [ ] **(decision)** POSIX-like syscall surface (eases porting) or a native
      one with a POSIX layer in libc.
- [ ] Initial set: `open close read write lseek stat fstat readdir mkdir
      unlink rename`, `mmap munmap`, `spawn/exec exit wait getpid`,
      `pipe dup2`, `poll`, `clock_gettime nanosleep`, `ioctl`.
- [ ] Delete `kernel/core/abi.c`, `kernel/include/tinos/abi.h`,
      `native_app.h` and the fixed-slot arena once apps are processes.

### VFS

- [ ] Add write-capable file descriptors and real shared open-file objects.
      Today each process holds immutable file contents, snapshotting writable
      ramfs files on open; disk-backed files are not opened by userspace.
      Add filesystem locking and refcounts before exposing disk backends.
- [ ] `fs_node_t` is simultaneously the directory entry, the inode and the
      file contents buffer. Split into vnode operations, a mount table, and
      open-file objects with an offset.
- [ ] Replace the existing per-process read-only snapshot descriptors with
      general VFS descriptor objects and per-process offset/flags tables.
- [ ] `fs_read_all_async()` reads whole files into memory; add ranged reads
      and a page/buffer cache.
- [ ] Writable persistent filesystem. FAT volumes are read-only and the base
      tree is not written back. Options: writable FAT32, ext2, or own format
      **(decision)**.
- [ ] `devfs` exposing devices as nodes.
- [ ] Permissions and ownership fields, even if a single user at first.
- [ ] Remove `FS_NAME_MAX` = 64 from the in-memory node (fixed-size name per node).
- [ ] Root filesystem from an initrd, then pivot to disk. Replace the
      "ramfs.tar unpacked into /apps" special case in `kernel/main.c`.

### IPC

- [ ] Pipes.
- [ ] Shared memory objects mappable into several processes.
- [ ] Local sockets or message channels with descriptor passing.
- [ ] `poll`-style wait over descriptors, timers and child exit.

### TTY and console

- [ ] `kernel/tty/`: TTY core with a line discipline (canonical and raw
      modes, echo, control characters).
- [ ] Console driver with two backends: VGA text and framebuffer text
      rendering. The framebuffer backend keeps a minimal font renderer in the
      kernel; the rest of `kernel/gfx` leaves.
- [ ] Virtual terminals and switching; the display is handed to and reclaimed
      from a graphical process.
- [ ] Pseudo-terminals for the graphical terminal emulator.
- [ ] `klog` to a ring buffer readable from userspace.
- [ ] Panic screen: stop depending on `gfx_*` drawing primitives
      (`kernel/core/panic.c`); render through the console.

### libc and SDK

- [ ] `libraries/libc/`: `crt0`, syscall stubs, `errno`, string, stdio,
      `malloc`, `stdlib`, time, a POSIX subset.
      **(decision)** write it, or port newlib / musl.
- [ ] Replace `apps/lib/shim.c` (678 lines of forwarding stubs).
- [ ] Apps stop compiling kernel sources into themselves
      (`kernel/lib/string.c`, `printf.c`, `kernel/fs/path.c`,
      `kernel/core/spinlock.c`, `kernel/sh/stream.c` in `apps/Makefile`).
- [ ] Apps stop including `kernel/include`; they build against an installed
      sysroot (`build/sysroot/usr/include`, `usr/lib`).
- [ ] OS-specific toolchain target (`*-tinos`) so `configure` scripts work.
- [ ] Dynamic linking: later, after static binaries work.

### Base system

- [ ] `userland/init/`: PID 1, mounts filesystems, reads boot mode, spawns
      getty or the desktop, reaps children.
- [ ] `userland/sh/`: move `kernel/sh/{shell,parse,stream,regex}.c` out of
      the kernel. Run commands as child processes with real pipes.
- [ ] `userland/bin/`: split `kernel/sh/commands.c` (1320 lines, 32
      built-ins) into individual programs; keep only true built-ins (`cd`,
      `exit`, `export`) in the shell.
- [ ] Commands that reach into kernel internals (`crash`, network
      configuration, desktop control) become syscalls, device ioctls, or
      desktop IPC requests.
- [x] `tinos.mode=text` boots an interactive VGA shell, and its `desktop`
      command starts the existing graphical session (still ring 0).
- [ ] `kmain()` ends by starting `init`, rather than entering the desktop's
      kernel loop directly.
- [ ] Complete kernel command line parsing for `tinos.mode=text|desktop`,
      `init=`, `root=`; current text mode checks for a substring.

---

## Phase 3 - Desktop as an optional process

Depends on Phase 2 (processes, IPC, shared memory, VT handoff).

### Decouple the kernel from the desktop

- [ ] Remove `#include <tinos/desktop.h>`, `<tinos/ui.h>`, `<tinos/image.h>`
      and `<tinos/codec.h>` from `kernel/main.c` and `kernel/core/abi.c`.
- [ ] Move `parse_manifest()`, `load_icon()` and `load_native_apps()` out of
      `kernel/main.c`; the desktop scans `/apps/*/app.conf` itself.
- [ ] `audio_codecs_init()` and `image_codecs_init()` leave `kmain()`.
- [ ] `desktop_shutdown()` becomes a `reboot`/`poweroff` syscall.
- [ ] Remove `desktop` and `libraries` from the kernel's `SRCS_C` and
      `INCLUDES` in the `Makefile`. Acceptance test: the kernel links with
      `desktop/`, `apps/` and `libraries/` absent.
- [ ] Drop the per-directory `CODEC_CFLAGS` override; SSE use in userspace
      needs no special handling.
- [ ] `build/generated/icons_generated.[ch]` is linked into the kernel; ship
      icons as files in the image instead.

### Kernel interfaces the desktop needs

- [ ] Framebuffer device: mode query/set and `mmap`.
- [ ] Input event devices for keyboard and mouse. Today drivers push into
      ring buffers that the desktop loop drains directly.
- [ ] Keyboard layout handling moves out of the PS/2 driver into userspace
      (driver emits key codes).
- [ ] Audio device node with a PCM write interface; the mixer moves to
      userspace.
- [ ] Socket syscalls for the network stack; `net_ping`, `net_dns_lookup`,
      DHCP control become userspace programs or a service.

### Compositor and client protocol

- [ ] `desktop/` builds to a standalone binary (`/bin/desktop`).
- [ ] Define the client protocol: connect, create/destroy window, attach
      shared-memory buffer, damage, title, input and focus events, close
      request.
- [ ] `window_t` currently holds an `app_class_t` vtable the desktop calls
      into (`draw`, key, mouse, tick). Replace with protocol messages; the
      desktop never executes client code.
- [ ] Each client renders into its own shared buffer; the compositor only
      composites. A hung client no longer freezes the desktop.
- [ ] `libraries/gfx/`: move drawing primitives, clipping and font rendering
      from `kernel/gfx/` to a library that draws into any buffer (today it
      draws into one global back buffer).
- [ ] `libraries/ui/`: move `desktop/widgets.c` and the text-input widget
      here; add an event loop and window wrapper over the protocol.
- [ ] `desktop/registry.c`: app registry built from manifests; apps are
      launched with `spawn`, not a `launch` function pointer.
- [ ] Notifications, wallpaper and other `desktop_*` calls become protocol
      requests.
- [ ] Starting the desktop from a TTY and exiting back to it; restart after a
      crash without rebooting.

### Applications

- [ ] Port each app in `apps/` to a process with `main()`, linked against
      libc, `libraries/ui` and `libraries/gfx`.
- [ ] Remove the `apps/<name>/main.c` registration stubs and `apps/link.ld`.
- [ ] Terminal app: use a pseudo-terminal and run `userland/sh` as a child
      instead of calling `shell_execute()` through the ABI.
- [ ] Image viewer and media player link `libraries/image` and
      `libraries/audio` directly.
- [ ] Extend `app.conf`: executable path, file associations, version.
- [ ] Untrusted file parsing (stb_image, simplewebp, stb_vorbis) leaves ring 0
      as a result of this phase.

---

## Phase 4 - Porting and ecosystem

- [ ] `toolchain/`: reproducible cross-toolchain build with pinned binutils
      and GCC versions and TinOS target patches.
- [ ] `ports/`: recipe format (source URL, checksum, patches, build steps)
      and a driver script.
- [ ] First ports, in increasing difficulty: a small C program, `zlib`, a
      text editor, a scripting language interpreter.
- [ ] Package format and install location for third-party apps.
- [ ] Users, groups and permission checks.
- [ ] Network userland: sockets API in libc, DNS resolver, DHCP client as a
      service.
- [ ] Unicode: UTF-8 in the console and UI, a font format beyond the 8x16
      bitmap, keyboard layouts that can produce non-ASCII input.
- [ ] Second architecture to validate the `arch/` split.

---

## Cross-cutting

### Testing

- [x] Move `kernel/core/selftest.c` to `tests/kernel/`.
- [x] Move `tools/qemu_test.py`, `tools/screen_ocr.py` and
      `tools/test_image_viewer.py` to `tests/qemu/`.
- [ ] `tests/kernel/selftest.c` is always linked into the kernel image; make
      it a build option.
- [ ] Host tests cover `kernel/fs`, `kernel/sh` and audio only; add
      `kernel/proc/elf.c`, `kernel/fs/tar.c`, FAT parsers and `kernel/net`
      packet parsing.
- [ ] Fuzz the parsers that take untrusted bytes: ELF, tar, FAT, MBR, DHCP,
      DNS, TCP.
- [ ] Syscall-level test suite run inside QEMU once userspace exists.
- [ ] Text-mode boot test in CI (no desktop) once Phase 2 lands.

### Kernel hygiene

- [ ] Split `kernel/include/tinos/` (50 flat headers) by subsystem.
- [ ] Kernel configuration mechanism to enable or disable drivers and
      subsystems at build time.
- [ ] Stack traces with symbol names in the panic output.
- [ ] Remove artificial boot delays (`BOOT_STEP_DELAY_MS`, `BOOT_HOLD_MS` in
      `kernel/main.c`) or put them behind a command-line flag.
- [ ] Hard-coded `SCREEN_WIDTH`/`SCREEN_HEIGHT` 1024x768 in `kernel/main.c`.
- [ ] Coding style document and a `clang-format` configuration.

### Decisions taken

| Decision | Outcome |
|----------|---------|
| PC target | i386 is kept; x86_64 deferred |
| Second target | Nintendo DSi (ARM9) |
| DSi scope | Full OS with processes, without a hardware MMU |
| UI on small screens | Single desktop that always adapts to the mode the display driver reports |
| DSi screens | Top screen = OS display; bottom touch screen = input panel (on-screen keyboard / touchpad, toggle button), D-pad moves the pointer |
| Desktop packaging | Optional separate process in `desktop/`; text mode is the base system |

### Open decisions

| Decision | Blocks |
|----------|--------|
| DSi process isolation: plain no-MMU model vs software-emulated MMU | Process model, executable format, VM design |
| BIOS only vs UEFI | Console backend, framebuffer discovery |
| `fork`+`exec` vs `spawn` (the DSi has no MMU, which favours `spawn`) | Process model, VM (copy-on-write) |
| POSIX-like vs native syscalls | uapi design, libc, porting effort |
| Own libc vs newlib/musl | Phase 2 SDK, licensing |
| Persistent filesystem format | VFS write path |
| Signals vs event-based notification | TTY job control, libc |
