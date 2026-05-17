| SPDX-License-Identifier: MPL-2.0
| BFS — Fast memset in 68020+ assembly
|
| void *memset(void *dst, int c, size_t len)
|
| Uses movem.l for large fills (44 bytes/iter = 11 registers).
| Safe to override libc memset — no overlap concerns.

        .text
        .even
        .globl  _memset

| void *memset(void *dst, int c, size_t len)
|   4(sp)=dst  8(sp)=c  12(sp)=len
|   Returns dst in d0.

_memset:
        movem.l d2-d7/a2-a6,-(sp)      | save 11 regs (+44)
        move.l  48(sp),a0               | dst
        move.l  52(sp),d1               | c (byte)
        move.l  56(sp),d0               | len
        move.l  a0,a1                   | save dst for return

        | Expand byte to long
        andi.l  #0xFF,d1
        move.l  d1,d2
        lsl.l   #8,d2
        or.l    d2,d1
        move.l  d1,d2
        swap    d2
        or.l    d2,d1                   | d1 = c|c|c|c

        | Fill registers for movem store
        move.l  d1,d2
        move.l  d1,d3
        move.l  d1,d4
        move.l  d1,d5
        move.l  d1,d6
        move.l  d1,d7
        move.l  d1,a2
        move.l  d1,a3
        move.l  d1,a4
        move.l  d1,a5
        move.l  d1,a6

        | If len < 44, skip to long/byte fill
        cmp.l   #44,d0
        bcs.s   .Lms_longs

        | Main loop: 44 bytes per iteration
        move.l  d0,-(sp)               | save len
        move.l  d0,d0
        divu.w  #44,d0
        ext.l   d0
        subq.l  #1,d0

.Lms_44:
        movem.l d1-d7/a2-a6,(a0)
        lea     44(a0),a0
        dbra    d0,.Lms_44

        | Calculate remainder
        move.l  (sp)+,d0               | restore len
        divu.w  #44,d0
        swap    d0
        ext.l   d0

.Lms_longs:
        move.l  d0,d0
        lsr.l   #2,d0
        beq.s   .Lms_bytes
        subq.l  #1,d0
.Lms_l4:
        move.l  d1,(a0)+
        dbra    d0,.Lms_l4

.Lms_bytes:
        move.l  56(sp),d0
        andi.l  #3,d0
        beq.s   .Lms_done
        subq.l  #1,d0
.Lms_b1:
        move.b  d1,(a0)+
        dbra    d0,.Lms_b1

.Lms_done:
        move.l  a1,d0                   | return dst
        movem.l (sp)+,d2-d7/a2-a6
        rts
