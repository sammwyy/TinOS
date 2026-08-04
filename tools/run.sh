#!/usr/bin/env bash
# Launch TinOS in QEMU.
#
#   tools/run.sh                 graphical window, serial log to tinos-serial.log
#   tools/run.sh --serial-stdio  serial log multiplexed onto the terminal
#   tools/run.sh --headless      no display (used by the automated tests)
#   tools/run.sh --vnc           no local window, serve the display over VNC
#
# Extra arguments are passed straight through to QEMU.

set -euo pipefail

cd "$(dirname "$0")/.."

ISO=${ISO:-build/tinos.iso}
MEM=${MEM:-256}
QEMU=${QEMU:-qemu-system-i386}

if [[ ! -f $ISO ]]; then
    echo "$ISO not found - run 'make' first" >&2
    exit 1
fi

# -vga std selects the Bochs-compatible adapter whose DISPI registers the
# TinOS graphics driver programs.  -rtc base=localtime makes the taskbar
# clock agree with the host.
args=(
    -cdrom "$ISO"
    -m "$MEM"
    -vga std
    -machine pc
    -rtc base=localtime
    -name "TinOS"
)

display="default"
serial="file:build/tinos-serial.log"

for arg in "$@"; do
    case "$arg" in
        --serial-stdio) serial="stdio"; shift ;;
        --headless)     display="none"; shift ;;
        --vnc)          display="vnc"; shift ;;
        *) break ;;
    esac
done

if [[ $display == none ]]; then
    args+=(-display none)
elif [[ $display == vnc ]]; then
    VNC=${VNC:-0.0.0.0:100}
    args+=(-display none -vnc "$VNC")
fi
args+=(-serial "$serial")

mkdir -p build
echo "+ $QEMU ${args[*]} $*"
exec "$QEMU" "${args[@]}" "$@"
