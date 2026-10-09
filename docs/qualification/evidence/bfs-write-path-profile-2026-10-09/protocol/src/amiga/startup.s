/* SPDX-License-Identifier: MPL-2.0 */
/* Switch stacks before C creates its frame. Exec and the C ABI preserve a2/a3. */

	.globl _EntryPoint
	.globl _EntryPointNoStack
	.globl start

start:
	movem.l d2-d7/a2-a6,-(sp)
	lea -12(sp),sp
	move.l sp,a2
	move.l 4.w,a6
	move.l #131072,d0
	moveq #1,d1
	jsr -198(a6)
	tst.l d0
	beq .no_stack
	move.l d0,a3
	move.l d0,(a2)
	add.l #131072,d0
	move.l d0,4(a2)
	move.l d0,8(a2)
	move.l a2,a0
	jsr -732(a6)
	bsr _EntryPoint
	move.l 4.w,a6
	move.l a2,a0
	jsr -732(a6)
	move.l a3,a1
	move.l #131072,d0
	jsr -210(a6)
	bra .done
.no_stack:
	bsr _EntryPointNoStack
.done:
	lea 12(sp),sp
	movem.l (sp)+,d2-d7/a2-a6
	moveq #0,d0
	rts
