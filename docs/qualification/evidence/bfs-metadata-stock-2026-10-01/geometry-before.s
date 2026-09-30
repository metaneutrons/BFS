_metadata_block_protected:
	subq.l #8,sp
	movem.l a5/a4/a3/a2/d7/d6/d4/d3/d2,-(sp)
	move.l (48,sp),a2
	move.l (a2)+,a0
	move.l (52,sp),d7
	move.l (4,a0),d6
	move.l #4095,d0
	add.l d6,d0
	divu.l d6,d0
	moveq #1,d4
	cmp.l d7,d0
	jhi .L44
	cmp.l (8,a0),d7
	jcc .L44
	cmp.l (8,a2),d7
	jeq .L44
	move.l (620,a2),a3
	move.l d7,-(sp)
	move.l a3,-(sp)
	jsr _seal_block_is_sb_root
	addq.l #8,sp
	tst.l d0
	jne .L44
	move.l (616,a2),a0
	cmp.l (28,a0),d7
	jeq .L44
	move.l d7,d3
	clr.l d2
	move.l d6,(40,sp)
	clr.l (36,sp)
	lea ___udivdi3,a5
	move.l (40,sp),-(sp)
	move.l (40,sp),-(sp)
	move.l (100,a0),-(sp)
	move.l (96,a0),-(sp)
	jsr (a5)
	lea (16,sp),sp
	sub.l d3,d1
	subx.l d2,d0
	jeq .L44
	move.l (40,sp),-(sp)
	move.l (40,sp),-(sp)
	move.l (100,a3),-(sp)
	move.l (96,a3),-(sp)
	jsr (a5)
	lea (16,sp),sp
	sub.l d3,d1
	subx.l d2,d0
	jeq .L44
	move.l (608,a2),a0
	move.l a0,d0
	jeq .L53
	move.l d7,-(sp)
	move.l a0,-(sp)
	jsr _seal_block_is_current_root
	addq.l #8,sp
	tst.l d0
	sne d0
	extb.l d0
	move.l d0,d4
	neg.l d4
.L44:
	move.l d4,d0
	movem.l (sp)+,d2/d3/d4/d6/d7/a2/a3/a4/a5
	addq.l #8,sp
	rts
.L53:
	move.l a0,d1
	move.l d1,d0
	movem.l (sp)+,d2/d3/d4/d6/d7/a2/a3/a4/a5
	addq.l #8,sp
	rts
	.align	2
_validate_metadata_stock:
	movem.l a5/a4/a3/a2/d4/d3/d2,-(sp)
	move.l (32,sp),a3
	move.l (584,a3),a0
	cmp.w #128,a0
	jhi .L62
	move.l (620,a3),a1
	moveq #32,d0
	cmp.l (232,a1),d0
	jcs .L62
	move.l a0,d0
	jeq .L61
	lea (72,a0.l*4),a0
	move.l a0,d4
	lea (72,a3),a2
	add.l a3,d4
	clr.l d3
	lea _metadata_block_protected,a4
	lea _validate_free_stock_range,a5
.L58:
	move.l (a2)+,d2
	move.l d2,-(sp)
	move.l a3,-(sp)
	jsr (a4)
	addq.l #8,sp
	tst.l d0
	jne .L62
	move.l d3,-(sp)
	pea 1.w
	move.l d2,-(sp)
	move.l a3,-(sp)
	jsr (a5)
	lea (16,sp),sp
	tst.l d0
	jne .L56
	addq.l #1,d3
	cmp.l a2,d4
	jne .L58
.L56:
	movem.l (sp)+,d2/d3/d4/a2/a3/a4/a5
	rts
.L62:
	moveq #-3,d0
	movem.l (sp)+,d2/d3/d4/a2/a3/a4/a5
	rts
.L61:
	move.l a0,d0
	movem.l (sp)+,d2/d3/d4/a2/a3/a4/a5
	rts
