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
USB_IMAGE=${USB_IMAGE:-build/tinos-usb.img}
USB_SOURCE=${USB_SOURCE:-build/usb-root}

if [[ ! -f $ISO ]]; then
    echo "$ISO not found - run 'make' first" >&2
    exit 1
fi

USB_IMAGE="$USB_IMAGE" USB_SOURCE="$USB_SOURCE" tools/make_usb_image.sh

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
    -netdev user,id=tinosnet
    -device e1000,netdev=tinosnet
    -drive "file=$USB_IMAGE,if=none,id=tinos-usb-disk,format=raw"
    -device piix3-usb-uhci,id=tinos-usb
    -device usb-storage,bus=tinos-usb.0,drive=tinos-usb-disk
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
