# TinOS agent notes

## Build and verify
- `make -j16` builds kernel, libc, userland and `build/tinos.iso`.
- `make test` runs host unit tests; `make check` runs host tests plus every
  QEMU scenario sequentially (about 10 minutes). `make check` stops at the
  first failure, so when iterating run the individual
  `tests/qemu/*_test.py` lines from the Makefile's `check` recipe.
- QEMU tests use QMP on port 4455 by default: do not run two at once.
- Target devices have 4-16 MiB of RAM: keep static buffers small, prefer
  allocating per-window/per-process state on demand, and check `size
  build/tinos.elf` after kernel changes. Test GUI changes at `--mem 8`.

## Icons
- Icons are vector-drawn in `tools/draw_icons.py` and committed as
  `resources/icons/<name>[@16|@48].bmp` (32-bit BMP with alpha). After editing
  run `python3 tools/draw_icons.py resources/icons`.
- `tools/gen_icons.py` builds the palettised kernel atlas at build time.
  Userland gets icons through `TINOS_SYS_ICON` (`tinos_canvas_icon`), never a
  linked copy.

## Apps
- GUI programs are `userland/bin/<app>.c`, listed in `GUI_APPS` in both
  `Makefile` and `userland/bin/Makefile`, with a start-menu manifest in
  `userland/manifests/<app>.app` (name, category, icon, summary, desktop).
- libc objects depend on the uapi headers; if a uapi constant changes and an
  app still behaves the old way, rebuild libc.

## QEMU test helpers
- `tests/qemu/qemu_test.py`: `menu_launch(vm, "<app>")` opens an app via the
  start menu using `MENU_APPS`; keep it in sync with the manifests.
- `screen_ocr.find_text` returns light-on-dark matches before dark-on-light
  ones; pass `region=` when the same word can appear in both (desktop icon
  labels vs. menus/windows).
