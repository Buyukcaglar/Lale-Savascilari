/*
 * hook.S - input-script driver injected into DOSBox-X (tools/verify/dbx_replay.py).
 *
 * Real-mode code + data block that dbx_replay.py writes into a free UMB
 * (segment HOOK_SEG, default D800h) at the main() breakpoint, then hooks
 * INT 16h, INT 21h and INT 33h to it.  It implements, inside the emulated
 * machine, exactly the rule the port's script mode uses (sdl3_main.c,
 * "input scripts"):
 *
 *   At every input read of the game - INT 21h 0Bh (kbhit), 01h/07h/08h
 *   (getch, also every wake-up while it blocks), INT 16h 00h/01h/10h/11h
 *   (bioskey, idem), INT 33h AX=3 (mouse poll) - all script events whose
 *   time is <= the game's ms clock (DGROUP:E74A, NKONTROL) are applied in
 *   order:
 *     type 1 key     a = BIOS key word, stuffed into the BDA ring (dropped
 *                    when full, like the BIOS)
 *     type 2 mouse   a, b = x, y (driver coordinates), c = buttons: the
 *                    position is set through the driver (AX=4, clamped by
 *                    it) right before this AX=3 read, buttons are reported
 *                    from now on.  AX=3 reports only positions set by the
 *                    game or the script (host pointer ignored, see int33)
 *     type 4 buttons a = buttons
 *     type 5 sync    nothing; due only when also DGROUP word a == b
 *     type 6 syncne  the same for a != b
 *     type 7 srand   DGROUP:94C4 (RTL random state) := b:a
 *     type 8 poke    DGROUP:a := b, c = 1 (byte) or 2 (word); a script
 *                    `poke addr bytes...` is split into such events
 *   Type bit 80h: t is a read count (+n): due n counted reads after the
 *   read that applied the previous event, or at a wake-up of a blocking
 *   read.  Counted reads: the entry of each function above, not the
 *   wake-ups; INT 16h calls made by DOS inside our INT 21h call pass.
 *     type 3 marker  VGA registers + DAC are captured to VGABUF and the
 *                    code passes MARKER_BP, where the Python side has a
 *                    breakpoint (dumps, quit)
 * The work happens only inside the emulated PC, so the debugger link (50 ms
 * per command) is used only at markers.
 *
 * Build: python tools/verify/build_hook.py  (clang/ld.lld of llvm-mingw).
 */
    .code16
    .text
    .globl _start
_start:
    .ascii "LALEHOOK"
    .org 0x08
old16:      .long 0
old21:      .long 0
old33:      .long 0
    .org 0x14
ev_idx:     .word 0
ev_count:   .word 0
dgseg:      .word 0x1F04
mpend:      .word 0
mx:         .word 0
my:         .word 0
mbtn:       .word 0
marker_ev:  .word 0xFFFF
clock_at:   .long 0
nchecks:    .long 0
reads:      .long 0             /* counted input reads (read-count sync) */
    .org 0x30
    .word int16, int21, int33, marker_bp   /* entry offsets for the installer */
    .org 0x38
last_read:  .long 0             /* reads at the last applied event */
blocked:    .byte 0             /* this check is a wake-up of a blocking read */
count_it:   .byte 0
in_dos:     .byte 0             /* inside our own call of the DOS/BIOS handler */
mvalid:     .byte 0             /* mxr/myr hold the position the game/script set */
mxr:        .word 0
myr:        .word 0
    .org 0x100
vgabuf:     .space 829          /* misc, seq 0-4, gc 0-8, crtc 0-24, attr 0-20, dac 768 */

    .org 0x500
/* ---- check: apply due events; preserves everything --------------------- */
/* check_read: one counted read of the game; check_wake: a wake-up while a
   blocking read waits (not counted; takes the next +n event at once) */
check_read:
    movb $0, %cs:blocked
    movb $1, %cs:count_it
    jmp check
check_wake:
    movb $1, %cs:blocked
    movb $0, %cs:count_it
check:
    pushf
    cli
    push %ax
    push %bx
    push %cx
    push %dx
    push %si
    push %di
    push %ds
    push %es
    mov %cs, %ax
    mov %ax, %ds
    addw $1, nchecks
    adcw $0, nchecks+2
    cmpb $0, count_it
    je next_ev
    addw $1, reads
    adcw $0, reads+2
next_ev:
    mov ev_idx, %si
    cmp ev_count, %si
    jae done
    mov %si, %ax
    mov $12, %bx
    mul %bx
    mov %ax, %bx
    add $0x1000, %bx
    mov 4(%bx), %al               /* sync (5): DGROUP word a == b; syncne (6): != */
    and $0x7F, %al
    cmp $5, %al
    je 4f
    cmp $6, %al
    jne 5f
    mov dgseg, %es
    mov 6(%bx), %di
    mov %es:(%di), %ax
    cmp 8(%bx), %ax
    je done
    jmp 5f
4:  mov dgseg, %es
    mov 6(%bx), %di
    mov %es:(%di), %ax
    cmp 8(%bx), %ax
    jne done
5:  testb $0x80, 4(%bx)
    jz clock_test
    cmpb $0, blocked
    jne due
    mov reads, %ax                /* +n: reads - last_read >= n */
    mov reads+2, %dx
    sub last_read, %ax
    sbb last_read+2, %dx
    cmp 2(%bx), %dx
    ja due
    jb done
    cmp (%bx), %ax
    jb done
    jmp due
clock_test:
    mov dgseg, %es
    mov %es:0xE74A, %ax
    mov %es:0xE74C, %dx
    cmp 2(%bx), %dx
    ja due
    jb done
    cmp (%bx), %ax
    jb done
due:
    incw ev_idx
    mov reads, %ax
    mov %ax, last_read
    mov reads+2, %ax
    mov %ax, last_read+2
    mov 4(%bx), %ax
    and $0x7F, %ax
    cmp $1, %ax
    je ev_key
    cmp $2, %ax
    je ev_mouse
    cmp $3, %ax
    je ev_marker
    cmp $4, %ax
    je ev_btn
    cmp $7, %ax
    je ev_srand
    cmp $8, %ax
    je ev_poke
    jmp next_ev                   /* 5/6 sync: only an anchor */
ev_poke:                          /* DGROUP:a := b (c = 1: byte, 2: word) */
    mov dgseg, %es
    mov 6(%bx), %di
    mov 8(%bx), %ax
    cmpw $1, 10(%bx)
    jne 1f
    mov %al, %es:(%di)
    jmp next_ev
1:  mov %ax, %es:(%di)
    jmp next_ev
ev_srand:
    mov dgseg, %es
    mov 6(%bx), %ax
    mov %ax, %es:0x94C4
    mov 8(%bx), %ax
    mov %ax, %es:0x94C6
    jmp next_ev
ev_key:
    mov $0x40, %ax
    mov %ax, %es
    mov %es:0x1C, %di
    mov %di, %cx
    add $2, %cx
    cmp %es:0x82, %cx
    jb 1f
    mov %es:0x80, %cx
1:  cmp %es:0x1A, %cx
    je next_ev                    /* buffer full: dropped */
    mov 6(%bx), %ax
    mov %ax, %es:(%di)
    mov %cx, %es:0x1C
    jmp next_ev
ev_mouse:
    mov 6(%bx), %ax
    mov %ax, mx
    mov 8(%bx), %ax
    mov %ax, my
    mov 10(%bx), %ax
    mov %ax, mbtn
    movw $1, mpend
    jmp next_ev
ev_btn:
    mov 6(%bx), %ax
    mov %ax, mbtn
    jmp next_ev
ev_marker:
    mov %si, marker_ev
    mov dgseg, %es
    mov %es:0xE74A, %ax
    mov %ax, clock_at
    mov %es:0xE74C, %ax
    mov %ax, clock_at+2
    call capture_vga
    call marker_bp
    jmp next_ev
done:
    pop %es
    pop %ds
    pop %di
    pop %si
    pop %dx
    pop %cx
    pop %bx
    pop %ax
    popf
    ret

marker_bp:
    nop
    ret

/* ---- VGA registers + DAC -> vgabuf (ds = cs) ----------------------------- */
capture_vga:
    push %ds
    pop %es
    mov $vgabuf, %di
    cld
    mov $0x3CC, %dx
    in %dx, %al
    stosb
    mov $0x3C4, %dx
    mov $5, %cx
    call idx_regs
    mov $0x3CE, %dx
    mov $9, %cx
    call idx_regs
    mov $0x3D4, %dx
    mov $25, %cx
    call idx_regs
    /* attribute controller */
    mov $0x3DA, %dx
    in %dx, %al
    mov $0x3C0, %dx
    in %dx, %al
    mov %al, %bl                  /* index incl. bit 5 */
    xor %bh, %bh
2:  mov $0x3DA, %dx
    in %dx, %al
    mov %bh, %al
    or $0x20, %al
    mov $0x3C0, %dx
    out %al, %dx
    mov $0x3C1, %dx
    in %dx, %al
    stosb
    inc %bh
    cmp $21, %bh
    jb 2b
    mov $0x3DA, %dx
    in %dx, %al
    mov %bl, %al
    mov $0x3C0, %dx
    out %al, %dx
    mov $0x3DA, %dx
    in %dx, %al
    /* DAC */
    xor %al, %al
    mov $0x3C7, %dx
    out %al, %dx
    mov $0x3C9, %dx
    mov $768, %cx
    rep insb
    ret

/* dx = index port, cx = count; index register restored */
idx_regs:
    in %dx, %al
    mov %al, %bl
    xor %bh, %bh
3:  mov %bh, %al
    out %al, %dx
    inc %dx
    in %dx, %al
    dec %dx
    stosb
    inc %bh
    loop 3b
    mov %bl, %al
    out %al, %dx
    ret

/* ---- INT 16h ---------------------------------------------------------------- */
int16:
    cmpb $0, %cs:in_dos           /* DOS console reads come through here */
    je 1f
    ljmp *%cs:old16
1:  cmp $0x00, %ah
    je i16blk
    cmp $0x10, %ah
    je i16blk
    cmp $0x01, %ah
    je i16chk
    cmp $0x11, %ah
    je i16chk
    ljmp *%cs:old16
i16chk:
    call check_read
    ljmp *%cs:old16
i16blk:
    push %ax
    call check_read
    jmp i16try
i16loop:
    call check_wake
i16try:
    pop %ax
    push %ax
    or $1, %ah
    pushf
    lcall *%cs:old16
    jnz i16go
    sti
    hlt
    cli
    jmp i16loop
i16go:
    pop %ax
    ljmp *%cs:old16

/* ---- INT 21h ---------------------------------------------------------------- */
int21:
    cmp $0x0B, %ah
    je i21chk
    cmp $0x07, %ah
    je i21blk
    cmp $0x08, %ah
    je i21blk
    cmp $0x01, %ah
    je i21blk
    ljmp *%cs:old21
i21chk:
    call check_read
    jmp i21dos
i21blk:
    push %ax
    call check_read
    jmp i21try
i21loop:
    call check_wake
i21try:
    mov $0x0B, %ah
    movb $1, %cs:in_dos
    pushf
    lcall *%cs:old21
    movb $0, %cs:in_dos
    cmp $0, %al
    jne i21go
    sti
    hlt
    cli
    jmp i21loop
i21go:
    pop %ax
i21dos:                           /* call DOS with INT 16h passing through */
    movb $1, %cs:in_dos
    pushf
    lcall *%cs:old21
    movb $0, %cs:in_dos
    iret                          /* 01h/07h/08h/0Bh return in AL only */

/* ---- INT 33h ---------------------------------------------------------------- */
int33:
    /* The mouse position is only what the game (AX=0 reset, AX=4, window AX=7/8) or the
       script set, latched as the driver clamped it: the host pointer over
       the DOSBox-X window (mouse integration) cannot move it.  The port has
       no host mouse in script mode either. */
    cmp $3, %ax
    je m3
    cmp $0, %ax                   /* reset: the driver centres the pointer */
    je mset
    cmp $4, %ax
    je mset
    cmp $7, %ax
    je mset
    cmp $8, %ax
    je mset
    ljmp *%cs:old33
mset:
    pushf
    lcall *%cs:old33
    call mlatch
    iret
mlatch:                           /* driver position -> mxr/myr; preserves all */
    push %ax
    push %bx
    push %cx
    push %dx
    mov $3, %ax
    pushf
    lcall *%cs:old33
    mov %cx, %cs:mxr
    mov %dx, %cs:myr
    movb $1, %cs:mvalid
    pop %dx
    pop %cx
    pop %bx
    pop %ax
    ret
m3:
    call check_read
    cmpw $0, %cs:mpend
    je m3r
    movw $0, %cs:mpend
    push %cx
    push %dx
    mov %cs:mx, %cx
    mov %cs:my, %dx
    mov $4, %ax
    pushf
    lcall *%cs:old33
    call mlatch
    pop %dx
    pop %cx
    mov $3, %ax
m3r:
    pushf
    lcall *%cs:old33
    cmpb $0, %cs:mvalid
    je 1f
    mov %cs:mxr, %cx
    mov %cs:myr, %dx
1:  mov %cs:mbtn, %bx
    iret

    .org 0x1000
events:
