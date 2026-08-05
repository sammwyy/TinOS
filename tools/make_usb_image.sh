#!/usr/bin/env bash
# Create a sparse FAT32 USB development image and optionally copy a directory
# tree into its root. Existing images are reused so make run remains fast.

set -euo pipefail

image=${USB_IMAGE:-build/tinos-usb.img}
source_dir=${USB_SOURCE:-build/usb-root}
size=${USB_SIZE:-4G}

mkdir -p "$(dirname "$image")"
if [[ ! -f $image ]]; then
    qemu-img create -f raw "$image" "$size" >/dev/null
    mkfs.fat -F 32 -n TINOSUSB "$image" >/dev/null
fi

if [[ -d $source_dir ]] && find "$source_dir" -mindepth 1 -print -quit | grep -q .; then
    mcopy -i "$image" -s -o "$source_dir"/* ::/ >/dev/null
fi

echo "  usb   : $image (source: $source_dir)"
