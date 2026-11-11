; BIOS El Torito loader. The ISO boot information table supplies the boot
; image LBA; the remaining sectors contain the kernel, initrd and AP code.
bits 16
org 0x7c00

%define BOOT_INFO       0x5000
%define BOOT_VERSION    (BOOT_INFO + 4)
%define BOOT_COUNT      (BOOT_INFO + 8)
%define BOOT_INITRD     (BOOT_INFO + 12)
%define BOOT_RSDP       (BOOT_INFO + 20)
%define BOOT_CMDLINE    (BOOT_INFO + 24)
%define BOOT_MEMORY     (BOOT_INFO + 152)
%define BOOT_MAX_MAP    128
%define BOUNCE_SEGMENT  0x1000
%define BOUNCE_ADDRESS  0x10000
%define TINOS_MAGIC     0x54494e4f

    jmp 0:entry
    times 8-($-$$) db 0
    dd 0                        ; El Torito primary volume descriptor LBA
boot_image_lba:
    dd 0                        ; patched by xorriso -boot-info-table
    dd 0                        ; boot image byte size
    dd 0                        ; boot image checksum
    times 64-($-$$) db 0

entry:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    sti
    cld
    mov [drive], dl
    mov si, message
    call print
    in al, 0x92
    or al, 2
    and al, 0xfe
    out 0x92, al

    mov di, BOOT_INFO
    mov cx, (152 + 24 * BOOT_MAX_MAP) / 2
    xor ax, ax
    rep stosw
    mov dword [BOOT_INFO], 152 + 24 * BOOT_MAX_MAP
    mov dword [BOOT_VERSION], 1

    xor ebx, ebx
.e820:
    mov di, BOOT_MEMORY
    mov ax, [BOOT_COUNT]
    mov cx, 24
    mul cx
    add di, ax
    mov eax, 0xe820
    mov edx, 0x534d4150
    mov ecx, 24
    int 0x15
    jc disk_error
    cmp eax, 0x534d4150
    jne disk_error
    cmp dword [es:di + 16], 1
    jne .not_usable
    cmp dword [es:di + 4], 0
    jne .not_usable
    cmp dword [es:di + 12], 0
    jne .not_usable
    mov eax, [es:di]
    add eax, [es:di + 8]
    jc .not_usable
    cmp eax, [usable_limit]
    jbe .not_usable
    mov [usable_limit], eax
.not_usable:
    inc dword [BOOT_COUNT]
    cmp dword [BOOT_COUNT], BOOT_MAX_MAP
    jae .map_done
    test ebx, ebx
    jnz .e820
.map_done:
    call find_rsdp
    call choose_mode

    mov eax, 0x100000
    mov edx, INITRD_ADDR
    call range_available
    jc disk_error
    mov eax, [boot_image_lba]
    inc eax
    mov [lba], eax
    mov dword [destination], 0x100000
    mov word [remaining], KERNEL_SECTORS
    call load_sectors

    mov eax, [usable_limit]
    ; The bundled application slots begin at 6 MiB. On a 4 MiB machine
    ; the initrd cannot supply executable apps, so retain RAM for the TTY.
    cmp eax, 0x780000
    jb .skip_initrd
    mov eax, INITRD_ADDR
    mov edx, INITRD_ADDR + INITRD_SIZE + 0x80000
    call range_available
    jc .skip_initrd
    mov dword [BOOT_INITRD], INITRD_ADDR
    mov dword [BOOT_INITRD + 4], INITRD_ADDR + INITRD_SIZE
    mov dword [destination], INITRD_ADDR
    mov word [remaining], INITRD_SECTORS
    call load_sectors
    jmp .trampoline
.skip_initrd:
    add dword [lba], INITRD_SECTORS
.trampoline:
    mov si, dap
    mov word [si + 2], 1
    mov dword [si + 8], 0
    mov eax, [lba]
    mov [si + 8], eax
    mov ax, BOUNCE_SEGMENT
    mov [si + 6], ax
    xor ax, ax
    mov [si + 4], ax
    call read_disk

    cli
    lgdt [gdt_pointer]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_entry

load_sectors:
    cmp word [remaining], 0
    je .done
.next:
    mov ax, [remaining]
    cmp ax, 16
    jbe .count_ready
    mov ax, 16
.count_ready:
    mov [read_count], ax
    mov [dap + 2], ax
    mov dword [dap + 8], 0
    mov eax, [lba]
    mov [dap + 8], eax
    mov word [dap + 4], 0
    mov word [dap + 6], BOUNCE_SEGMENT
    call read_disk

    ; FS retains a flat 4 GiB limit when returning to real mode.
    cli
    lgdt [gdt_pointer]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    mov ax, 0x10
    mov fs, ax
    mov eax, cr0
    and eax, 0xfffffffe
    mov cr0, eax
    sti

    mov ax, BOUNCE_SEGMENT
    mov ds, ax
    xor si, si
    mov edi, [cs:destination]
    movzx ecx, word [cs:read_count]
    shl ecx, 11
.copy:
    mov al, [si]
    mov [fs:edi], al
    inc si
    inc edi
    dec ecx
    jnz .copy
    xor ax, ax
    mov ds, ax
    mov [destination], edi
    movzx eax, word [read_count]
    add [lba], eax
    sub [remaining], ax
    jnz .next
.done:
    ret

; CF is clear only when a single usable E820 region contains [EAX, EDX).
range_available:
    mov ecx, [BOOT_COUNT]
    mov esi, BOOT_MEMORY
.entry:
    cmp dword [esi + 16], 1
    jne .next
    cmp dword [esi + 4], 0
    jne .next
    cmp dword [esi + 12], 0
    jne .next
    cmp eax, [esi]
    jb .next
    mov ebx, [esi]
    add ebx, [esi + 8]
    jc .next
    cmp edx, ebx
    ja .next
    clc
    ret
.next:
    add esi, 24
    loop .entry
    stc
    ret

read_disk:
    mov cx, 3
.retry:
    mov ah, 0x42
    mov dl, [drive]
    mov si, dap
    int 0x13
    jnc .ok
    push cx
    xor ah, ah
    mov dl, [drive]
    int 0x13
    pop cx
    loop .retry
    jmp disk_error
.ok:
    ret

choose_mode:
    ; A three-second menu retains the existing normal, self-test and TTY
    ; boot paths. Arrow keys move selection and Enter boots immediately.
    call show_selection
    mov eax, [0x46c]
    add eax, 54
    mov [deadline], eax
.wait:
    mov ah, 1
    int 0x16
    jnz .key
    mov eax, [0x46c]
    cmp eax, [deadline]
    jb .wait
    jmp .selected
.key:
    xor ah, ah
    int 0x16
    cmp ah, 0x50
    jne .not_down
    cmp byte [selection], 2
    jae .wait
    inc byte [selection]
    call show_selection
    jmp .wait
.not_down:
    cmp ah, 0x48
    jne .not_up
    cmp byte [selection], 0
    je .wait
    dec byte [selection]
    call show_selection
    jmp .wait
.not_up:
    cmp ah, 0x1c
    jne .wait
.selected:
    cmp byte [selection], 1
    jne .not_selftest
    mov si, selftest_command
    jmp .copy_command
.not_selftest:
    cmp byte [selection], 2
    jne .done
    mov si, text_command
.copy_command:
    mov di, BOOT_CMDLINE
.copy_string:
    lodsb
    stosb
    test al, al
    jnz .copy_string
.done:
    ret

show_selection:
    mov si, mode_desktop
    cmp byte [selection], 1
    jne .not_selftest
    mov si, mode_selftest
.not_selftest:
    cmp byte [selection], 2
    jne .print
    mov si, mode_text
.print:
    call print
    ret

find_rsdp:
    ; ACPI 1.0 RSDP may reside in the EBDA or BIOS ROM area.
    mov ax, [0x40e]
    mov es, ax
    xor bx, bx
    mov cx, 64
    call scan_rsdp
    jc .found
    mov ax, 0xe000
    mov es, ax
    xor bx, bx
    mov cx, 8192
    call scan_rsdp
.found:
    xor ax, ax
    mov es, ax
    ret

scan_rsdp:
    mov eax, [es:bx]
    cmp eax, 0x20445352    ; "RSD "
    jne .next
    cmp dword [es:bx + 4], 0x20525450 ; "PTR "
    jne .next
    mov ax, es
    movzx eax, ax
    shl eax, 4
    add eax, ebx
    mov [BOOT_RSDP], eax
    stc
    ret
.next:
    add bx, 16
    jnc .no_wrap
    mov ax, es
    add ax, 0x1000
    mov es, ax
.no_wrap:
    loop scan_rsdp
    clc
    ret

print:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0e
    xor bx, bx
    int 0x10
    jmp print
.done:
    ret

disk_error:
    mov si, error_message
    call print
.halt:
    cli
    hlt
    jmp .halt

align 4
gdt:
    dq 0
    dq 0x00cf9a000000ffff
    dq 0x00cf92000000ffff
gdt_pointer:
    dw gdt_pointer - gdt - 1
    dd gdt

align 4
dap:
    db 16, 0
    dw 0
    dw 0
    dw BOUNCE_SEGMENT
    dq 0
destination: dd 0
lba: dd 0
deadline: dd 0
usable_limit: dd 0
drive: db 0
selection: db 0
read_count: dw 0
remaining: dw 0
message: db 13, 10, 'TinOS boot: Enter=boot, Down=select (0 OS, 1 tests, 2 text)', 13, 10, 0
mode_desktop: db '  > Desktop', 13, 10, 0
mode_selftest: db '  > Kernel self-test', 13, 10, 0
mode_text: db '  > Text console', 13, 10, 0
error_message: db 13, 10, 'TinOS boot: BIOS disk/memory error', 13, 10, 0
text_command: db 'tinos.mode=text', 0
selftest_command: db 'selftest', 0

bits 32
protected_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov esp, 0x90000
    mov edi, BSS_START
    mov ecx, BSS_END - BSS_START
    xor eax, eax
    rep stosb
    mov eax, TINOS_MAGIC
    mov ebx, BOOT_INFO
    mov edx, KERNEL_ENTRY
    jmp edx

    times 2048-($-$$) db 0
