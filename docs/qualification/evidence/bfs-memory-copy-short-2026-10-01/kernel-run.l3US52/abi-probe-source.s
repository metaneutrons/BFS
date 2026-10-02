| SPDX-License-Identifier: MPL-2.0
| Amiga 68k ABI witness for CRC32 function-pointer calls.
|
| ULONG bfs_crc32_abi_probe(CrcFunction function, uint32_t seed,
|                           const void *data, size_t length,
|                           uint32_t *result)
|
| Return mask:
|   bit 0: target returned with an unbalanced caller argument stack
|   bit 1: local stack guard was modified
|   bits 2-7: D2-D7 sentinel changed
|   bits 8-12: A2-A6 sentinel changed
|
| The wrapper saves/restores its caller's callee-saved registers, installs
| sentinels, calls the target with the public stack ABI, records D0, checks
| stack/register/guard behavior, then returns the error mask in D0. Its
| wrapper frame and target arguments are four-byte aligned and balanced.

        .text
        .even
        .globl  _bfs_crc32_abi_probe

_bfs_crc32_abi_probe:
        movem.l d2-d7/a2-a6,-(sp)       | Preserve wrapper caller's ABI state
        lea     -12(sp),sp              | local: expected SP, guard, result ptr

        | Original arguments are now at 60,64,68,72,76(sp).
        move.l  76(sp),d0
        move.l  d0,8(sp)                | Retain result pointer in wrapper frame
        move.l  #0xC0DEF00D,4(sp)       | Stack guard
        move.l  60(sp),a0               | Target function pointer

        | Push C arguments right-to-left. At each step the next source is +72.
        move.l  72(sp),d0               | length
        move.l  d0,-(sp)
        move.l  72(sp),d0               | data pointer
        move.l  d0,-(sp)
        move.l  72(sp),d0               | seed
        move.l  d0,-(sp)
        move.l  sp,d0
        move.l  d0,12(sp)               | Expected SP immediately after target RTS

        move.l  #0xD2D2D2D2,d2
        move.l  #0xD3D3D3D3,d3
        move.l  #0xD4D4D4D4,d4
        move.l  #0xD5D5D5D5,d5
        move.l  #0xD6D6D6D6,d6
        move.l  #0xD7D7D7D7,d7
        move.l  #0xA2A2A2A2,a2
        move.l  #0xA3A3A3A3,a3
        move.l  #0xA4A4A4A4,a4
        move.l  #0xA5A5A5A5,a5
        move.l  #0xA6A6A6A6,a6
        jsr     (a0)

        | Read the return value before using D0 as the error mask.
        move.l  20(sp),a0               | Wrapper-local result pointer
        move.l  d0,(a0)
        moveq   #0,d0
        move.l  sp,d1
        cmp.l   12(sp),d1               | Did the target leave SP balanced?
        beq.s   .Lcrc_abi_sp_ok
        ori.l   #0x00000001,d0
.Lcrc_abi_sp_ok:
        lea     12(sp),sp               | Caller removes target arguments

        cmpi.l  #0xC0DEF00D,4(sp)
        beq.s   .Lcrc_abi_guard_ok
        ori.l   #0x00000002,d0
.Lcrc_abi_guard_ok:

        cmpi.l  #0xD2D2D2D2,d2
        beq.s   .Lcrc_abi_d2_ok
        ori.l   #0x00000004,d0
.Lcrc_abi_d2_ok:
        cmpi.l  #0xD3D3D3D3,d3
        beq.s   .Lcrc_abi_d3_ok
        ori.l   #0x00000008,d0
.Lcrc_abi_d3_ok:
        cmpi.l  #0xD4D4D4D4,d4
        beq.s   .Lcrc_abi_d4_ok
        ori.l   #0x00000010,d0
.Lcrc_abi_d4_ok:
        cmpi.l  #0xD5D5D5D5,d5
        beq.s   .Lcrc_abi_d5_ok
        ori.l   #0x00000020,d0
.Lcrc_abi_d5_ok:
        cmpi.l  #0xD6D6D6D6,d6
        beq.s   .Lcrc_abi_d6_ok
        ori.l   #0x00000040,d0
.Lcrc_abi_d6_ok:
        cmpi.l  #0xD7D7D7D7,d7
        beq.s   .Lcrc_abi_d7_ok
        ori.l   #0x00000080,d0
.Lcrc_abi_d7_ok:
        move.l  a2,d1
        cmpi.l  #0xA2A2A2A2,d1
        beq.s   .Lcrc_abi_a2_ok
        ori.l   #0x00000100,d0
.Lcrc_abi_a2_ok:
        move.l  a3,d1
        cmpi.l  #0xA3A3A3A3,d1
        beq.s   .Lcrc_abi_a3_ok
        ori.l   #0x00000200,d0
.Lcrc_abi_a3_ok:
        move.l  a4,d1
        cmpi.l  #0xA4A4A4A4,d1
        beq.s   .Lcrc_abi_a4_ok
        ori.l   #0x00000400,d0
.Lcrc_abi_a4_ok:
        move.l  a5,d1
        cmpi.l  #0xA5A5A5A5,d1
        beq.s   .Lcrc_abi_a5_ok
        ori.l   #0x00000800,d0
.Lcrc_abi_a5_ok:
        move.l  a6,d1
        cmpi.l  #0xA6A6A6A6,d1
        beq.s   .Lcrc_abi_a6_ok
        ori.l   #0x00001000,d0
.Lcrc_abi_a6_ok:
        lea     12(sp),sp              | Release wrapper locals
        movem.l (sp)+,d2-d7/a2-a6      | Restore wrapper caller's registers
        rts

| Diagnostic-only bad callees. They deliberately violate exactly one
| callee-saved register while keeping the stack and return value valid; the C
| probe requires the witness to return exactly the corresponding mask bit.
        .even
        .globl  _bfs_crc32_abi_clobber_d2
        .globl  _bfs_crc32_abi_clobber_a2

_bfs_crc32_abi_clobber_d2:
        move.l  #0xBAD2C0DE,d2
        move.l  4(sp),d0
        rts

_bfs_crc32_abi_clobber_a2:
        move.l  #0xBAA2C0DE,a2
        move.l  4(sp),d0
        rts
