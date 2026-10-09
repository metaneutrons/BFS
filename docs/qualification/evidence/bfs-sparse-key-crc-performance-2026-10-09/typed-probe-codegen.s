#NO_APP
.comm _TimerBase,4
	.text
	.align	2
_baseline_adapter:
	jmp _bfs_crc32
	.align	2
_sparse_adapter:
	jmp _bfs_crc32_sparse
	.data
	.align	2
_call_baseline:
	.long	_baseline_adapter
	.align	2
_call_sparse:
	.long	_sparse_adapter
.lcomm _timing_sink,4
	.text
.LC0:
	.ascii "zero\0"
.LC1:
	.ascii "dense\0"
.LC2:
	.ascii "block32_224\0"
.LC3:
	.ascii "dirkey_like_264\0"
.LC4:
	.ascii "mixed128_384\0"
	.data
	.align	2
_pattern_names:
	.long	.LC0
	.long	.LC1
	.long	.LC2
	.long	.LC3
	.long	.LC4
	.text
	.align	2
_seeds:
	.long	0
	.long	-1
	.long	305419896
	.align	2
_short_lengths:
	.long	0
	.long	1
	.long	15
	.long	16
	.long	17
	.long	31
	.long	32
	.long	63
	.long	64
	.long	65
	.long	66
	.long	127
	.long	128
	.long	129
	.align	2
_perturb_positions:
	.long	0
	.long	63
	.long	64
	.long	65
	.long	255
	.long	256
	.long	2047
	.long	4095
	.data
	.align	2
_counts:
	.skip 72
	.long	-2128831035
	.skip 32
.lcomm _storage,4112
.lcomm _baseline_ticks,240
.lcomm _sparse_ticks,240
	.text
.LC5:
	.ascii "dense4096\0"
.LC6:
	.ascii "zero4096\0"
.LC7:
	.ascii "sparse4096-dirkey264\0"
.LC8:
	.ascii "short64-dense\0"
.LC9:
	.ascii "mixed128-384\0"
	.align	2
_timing_cases:
	.long	.LC5
	.long	4096
	.long	64
	.long	1
	.long	.LC6
	.long	4096
	.long	64
	.long	0
	.long	.LC7
	.long	4096
	.long	64
	.long	3
	.long	.LC8
	.long	64
	.long	512
	.long	1
	.long	.LC9
	.long	512
	.long	64
	.long	4
	.align	2
_fill_pattern:
	movem.l d7/d6/d5/d4/d3/d2,-(sp)
	move.l (32,sp),d1
	move.l (36,sp),d2
	tst.l d1
	jeq .L3
	move.l (28,sp),d3
	moveq #91,d4
	clr.l d0
	subq.l #1,d1
.L15:
	moveq #1,d5
	cmp.l d2,d5
	jeq .L6
	jge .L25
	moveq #2,d5
	cmp.l d2,d5
	jeq .L9
	moveq #3,d5
	cmp.l d2,d5
	jne .L5
	move.l d0,d6
	mulu.l #1041204193,d5:d6
	move.l d5,d6
	lsr.l #6,d6
	move.l d6,d5
	lsl.l #5,d5
	add.l d6,d5
	lsl.l #3,d5
	move.l d0,d6
	sub.l d5,d6
	clr.b d5
	moveq #12,d7
	cmp.l d6,d7
	jcs .L14
	move.l d0,d5
	lsr.l #4,d5
	move.b d5,d6
	lsl.b #3,d6
	add.b d5,d6
	add.b d6,d6
.L21:
	add.b d6,d5
	add.b d4,d5
	or.b #1,d5
.L14:
	move.l d3,a0
	move.b d5,(a0)
	addq.l #1,d3
	add.b #73,d4
	addq.l #1,d0
	dbra d1,.L15
	clr.w d1
	subq.l #1,d1
	jcc .L15
.L3:
	movem.l (sp)+,d2/d3/d4/d5/d6/d7
	rts
.L25:
	tst.l d2
	jne .L5
	move.l d3,a0
	move.b d2,(a0)
	addq.l #1,d3
	add.b #73,d4
	addq.l #1,d0
	dbra d1,.L15
	clr.w d1
	subq.l #1,d1
	jcc .L15
	jra .L3
.L5:
	clr.b d5
	move.l #511,d6
	and.l d0,d6
	moveq #127,d7
	cmp.l d6,d7
	jcs .L14
	move.l d0,d5
	lsr.l #4,d5
	move.b d5,d6
	lsl.b #3,d6
	add.b d5,d6
	add.b d6,d6
	jra .L21
.L6:
	move.l d0,d6
	lsr.l #4,d6
	move.b d6,d5
	lsl.b #3,d5
	add.b d6,d5
	add.b d5,d5
	add.b d6,d5
	add.b d4,d5
	or.b #1,d5
	jra .L14
.L9:
	clr.b d5
	cmp.b #31,d0
	jhi .L14
	move.l d0,d5
	lsr.l #4,d5
	move.b d5,d6
	lsl.b #3,d6
	add.b d5,d6
	add.b d6,d6
	jra .L21
.LC10:
	.ascii "abi\0"
.LC11:
	.ascii "oracle\0"
	.align	2
_check_one:
	subq.l #4,sp
	movem.l a5/d7/d6/d5/d4/d3/d2,-(sp)
	lea (36,sp),a0
	move.l (a0)+,d0
	move.l (a0)+,d5
	move.l (a0)+,d7
	move.l (a0)+,d2
	move.l (a0)+,d1
	move.l (a0)+,d3
	move.l (a0)+,d6
	move.l (a0),d4
	lea (32,sp),a0
	clr.l -(a0)
	move.l a0,-(sp)
	move.l d3,-(sp)
	move.l d1,-(sp)
	move.l d2,-(sp)
	move.l d0,-(sp)
	jsr _bfs_crc32_abi_probe
	lea _counts,a0
	addq.l #1,(12,a0)
	lea (20,sp),sp
	tst.l d5
	jeq .L27
	addq.l #1,(4,a0)
.L28:
	tst.l d0
	jeq .L30
	addq.l #1,(20,a0)
	addq.l #1,(64,a0)
	tst.l (76,a0)
	jeq .L37
.L30:
	move.l (28,sp),d0
	cmp.l d0,d4
	jeq .L26
	addq.l #1,(16,a0)
	addq.l #1,(64,a0)
	tst.l (76,a0)
	jeq .L38
.L26:
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a5
	addq.l #4,sp
	rts
.L27:
	addq.l #1,(8,a0)
	jra .L28
.L38:
	lea (76,a0),a1
	move.l #.LC11,(a1)+
	move.l d7,(a1)+
	move.l d3,(a1)+
	move.l d6,(a1)
	move.l d2,(96,a0)
	move.l d4,(100,a0)
	move.l d0,(104,a0)
	clr.l (92,a0)
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a5
	addq.l #4,sp
	rts
.L37:
	lea (76,a0),a1
	move.l #.LC10,(a1)+
	move.l d7,(a1)+
	move.l d3,(a1)+
	move.l d6,(a1)
	move.l d2,(96,a0)
	clr.l (100,a0)
	clr.l (104,a0)
	move.l d0,(92,a0)
	jra .L30
	.align	2
_check:
	movem.l a6/a5/a4/a3/a2/d7/d6/d5/d4/d3/d2,-(sp)
	lea (48,sp),a0
	move.l (a0)+,a2
	move.l (a0)+,d5
	move.l (a0)+,d4
	move.l (a0)+,d7
	move.l (a0),a6
	move.l a6,d0
	not.l d0
	tst.l d4
	jeq .L40
	move.l d5,a3
	move.l d5,d6
	move.l #-306674912,a0
	move.l d5,a1
	add.l d4,a1
	not.l d6
	add.l a1,d6
.L43:
	clr.l d1
	move.b (a3)+,d1
	eor.l d1,d0
	moveq #7,d1
.L42:
	move.l d0,d3
	lsr.l #1,d3
	clr.l d2
	btst #0,d0
	jeq .L41
	move.l a0,d2
.L41:
	move.l d3,d0
	eor.l d2,d0
	dbra d1,.L42
	dbra d6,.L43
	clr.w d6
	subq.l #1,d6
	jcc .L43
.L40:
	move.l d0,d2
	not.l d2
	move.l _call_baseline,a0
	lea _counts,a1
	move.l _call_sparse,a3
	addq.l #1,(a1)
	move.l (72,a1),d6
	clr.l d3
	moveq #3,d1
.L44:
	move.l d4,d0
	lsr.l d3,d0
	and.l #255,d0
	eor.l d0,d6
	muls.l #16777619,d6
	addq.l #8,d3
	dbra d1,.L44
	clr.l d1
	moveq #3,d0
.L45:
	move.l d7,d3
	lsr.l d1,d3
	and.l #255,d3
	eor.l d3,d6
	muls.l #16777619,d6
	addq.l #8,d1
	dbra d0,.L45
	clr.l d3
	moveq #3,d0
.L46:
	move.l a6,d1
	lsr.l d3,d1
	and.l #255,d1
	eor.l d1,d6
	muls.l #16777619,d6
	addq.l #8,d3
	dbra d0,.L46
	move.l d6,(72,a1)
	tst.l d4
	jeq .L47
	move.l d5,a4
	move.l d5,d1
	not.l d1
	move.l d5,d0
	add.l d4,d0
	add.l d0,d1
.L48:
	clr.l d0
	move.b (a4)+,d0
	eor.l d0,d6
	muls.l #16777619,d6
	move.l d6,(72,a1)
	dbra d1,.L48
	clr.w d1
	subq.l #1,d1
	jcc .L48
.L47:
	clr.l d3
	moveq #3,d1
.L49:
	move.l d2,d0
	lsr.l d3,d0
	and.l #255,d0
	eor.l d0,d6
	muls.l #16777619,d6
	addq.l #8,d3
	dbra d1,.L49
	move.l d6,(72,a1)
	move.l d2,-(sp)
	move.l d7,-(sp)
	move.l d4,-(sp)
	move.l d5,-(sp)
	move.l a6,-(sp)
	move.l a2,-(sp)
	pea 1.w
	move.l a0,-(sp)
	lea _check_one,a4
	jsr (a4)
	lea (28,sp),sp
	move.l d2,(sp)
	move.l d7,-(sp)
	move.l d4,-(sp)
	move.l d5,-(sp)
	move.l a6,-(sp)
	move.l a2,-(sp)
	clr.l -(sp)
	move.l a3,-(sp)
	jsr (a4)
	lea (32,sp),sp
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6
	rts
.LC12:
	.ascii "clobber_a2\0"
.LC13:
	.ascii "clobber_d2\0"
.LC14:
	.ascii "timer\0"
.LC15:
	.ascii "\11sparse-first\11\0"
.LC16:
	.ascii "\11baseline-first\11\0"
.LC17:
	.ascii "STATUS\11PASS\12\0"
.LC18:
	.ascii "STATUS\11FAIL\12\0"
.LC19:
	.ascii "abi_negative_control\0"
.LC20:
	.ascii "zero_run_boundary\0"
.LC21:
	.ascii "mixed128_384_control\0"
.LC22:
	.ascii "single_byte_perturbation\0"
.LC23:
	.ascii "oracle_selfcheck\0"
.LC24:
	.ascii "123456789\0"
.LC25:
	.ascii "known_vector\0"
.LC26:
	.ascii "timer.device\0"
.LC27:
	.ascii "timing_checksum\0"
.LC28:
	.ascii "EClock\0"
.LC29:
	.ascii "SPARSE_CRC_PROBE\11"
	.ascii "2\12\0"
.LC30:
	.ascii "META\11oracle\11independent-bitwise-reflected-IEEE\12\0"
.LC31:
	.ascii "META\11api\11typed size_t adapters; explicit uint32_t conversion; 32-bit lengths\12\0"
.LC32:
	.ascii "FIXTURE\11name\11description\12\0"
.LC33:
	.ascii "FIXTURE\11small\11zero,dense,32-nonzero+224-zero per 256; lengths 0,1,15,16,17,31,32,63,64,65,66,127,128,129; offsets 0..7\12\0"
.LC34:
	.ascii "FIXTURE\11zero_run_boundary\11prefix byte; 63,64,65 supplied zero bytes; following byte included separately\12\0"
.LC35:
	.ascii "FIXTURE\11large\11"
	.ascii "4096-byte zero,dense,block256,dirkey264; seeds 00000000,FFFFFFFF,12345678; offsets 0..7\12\0"
.LC36:
	.ascii "FIXTURE\11dirkey_like_264\11synthetic packed 264-byte key: parent4,hash4,nameLen1,four-char name; 251 zero tail bytes\12\0"
.LC37:
	.ascii "FIXTURE\11single_byte_perturbation\11"
	.ascii "4096 zero bytes with one A5 at offsets 0,63,64,65,255,256,2047,4095\12\0"
.LC38:
	.ascii "FIXTURE\11mixed128_384\11density control; 128 nonzero and 384 zero bytes per 512-byte group; not an inode fixture\12\0"
.LC39:
	.ascii "COUNT\11\0"
.LC40:
	.ascii "vector_cases\0"
.LC41:
	.ascii "baseline_calls\0"
.LC42:
	.ascii "sparse_calls\0"
.LC43:
	.ascii "abi_positive_checks\0"
.LC44:
	.ascii "abi_negative_control_cases\0"
.LC45:
	.ascii "abi_negative_control_passes\0"
.LC46:
	.ascii "abi_negative_control_errors\0"
.LC47:
	.ascii "oracle_errors\0"
.LC48:
	.ascii "abi_errors\0"
.LC49:
	.ascii "timing_errors\0"
.LC50:
	.ascii "errors\0"
.LC51:
	.ascii "timer_available\0"
.LC52:
	.ascii "CLOCK_HZ\11\0"
.LC53:
	.ascii "oracle_digest_fnv1a32\0"
.LC54:
	.ascii "timing_cases\0"
.LC55:
	.ascii "timing_samples\0"
.LC56:
	.ascii "baseline_timing_calls\0"
.LC57:
	.ascii "sparse_timing_calls\0"
.LC58:
	.ascii "timing_order_baseline_first\0"
.LC59:
	.ascii "timing_order_sparse_first\0"
.LC60:
	.ascii "TIMING\11case\11length\11repeats\11sample\11order\11baseline_ticks\11sparse_ticks\12\0"
.LC61:
	.ascii "TIMING\11\0"
.LC62:
	.ascii "FIRST_ERROR\11\0"
.LC63:
	.ascii "SYS:Results/crc32-sparse-probe.tsv\0"
.LC64:
	.ascii "SYS:Results/crc32-sparse-probe.done\0"
	.section .text
	.align	2
	.globl	_main
_main:
	lea (-8432,sp),sp
	movem.l a6/a5/a4/a3/a2/d7/d6/d5/d4/d3/d2,-(sp)
	sub.l a1,a1
	move.l _SysBase,a6
#APP
| 446 "tests/amiga/crc32_sparse_probe.c" 1
	jsr a6@(-0x126:W)
| 0 "" 2
#NO_APP
	move.l d0,(228,sp)
	move.l d0,a0
	move.l (184,a0),(232,sp)
	moveq #-1,d0
	move.l d0,(184,a0)
	lea _controls.2751,a2
	moveq #1,d2
	clr.l d4
	moveq #4,d3
	lea _bfs_crc32_abi_probe,a3
	move.l #.LC13,d5
	move.l #.LC12,d6
	move.l #305419896,d7
	clr.l (284,sp)
	pea (284,sp)
	clr.l -(sp)
	clr.l -(sp)
	move.l #305419896,-(sp)
	move.l (a2)+,-(sp)
	jsr (a3)
	lea _counts,a0
	addq.l #1,(24,a0)
	lea (20,sp),sp
	cmp.l d0,d3
	jeq .L406
.L69:
	lea _counts,a0
	addq.l #1,(32,a0)
	move.l d0,d1
	tst.l d4
	jeq .L72
.L410:
	lea _counts,a0
	addq.l #1,(64,a0)
	tst.l (76,a0)
	jeq .L407
.L71:
	moveq #2,d0
	cmp.l d2,d0
	jne .L255
	move.l #4+_small_patterns.2692,(204,sp)
	move.w #3,(88,sp)
	sub.l a3,a3
	lea _fill_pattern,a5
	move.l #4+_seeds,d6
	lea _check,a4
	lea 4+_short_lengths,a6
	move.w #14,(48,sp)
	clr.l d4
.L80:
	moveq #8,d5
	clr.l d2
.L77:
	move.l d2,d7
	add.l #8+_storage,d7
	move.l a3,-(sp)
	move.l d4,-(sp)
	move.l d7,-(sp)
	jsr (a5)
	lea (12,sp),sp
	move.l d6,a2
	moveq #3,d3
	clr.l -(sp)
	move.l d2,-(sp)
	move.l d4,-(sp)
	move.l d7,-(sp)
	move.l (_pattern_names,a3.l*4),-(sp)
	jsr (a4)
	subq.w #1,d3
	lea (20,sp),sp
	jeq .L75
.L408:
	move.l (a2)+,-(sp)
	move.l d2,-(sp)
	move.l d4,-(sp)
	move.l d7,-(sp)
	move.l (_pattern_names,a3.l*4),-(sp)
	jsr (a4)
	subq.w #1,d3
	lea (20,sp),sp
	jne .L408
.L75:
	addq.l #1,d2
	subq.w #1,d5
	jne .L77
	subq.w #1,(48,sp)
	jeq .L78
	move.l (a6)+,d4
	moveq #8,d5
	clr.l d2
	jra .L77
.L78:
	subq.w #1,(88,sp)
	jeq .L79
	move.l (204,sp),a0
	move.l (a0)+,a3
	move.l a0,(204,sp)
	lea 4+_short_lengths,a6
	move.w #14,(48,sp)
	clr.l d4
	jra .L80
.L255:
	move.w #1,a0
	move.l (_expected_masks.2752,a0.l*4),d3
	addq.l #1,d4
	addq.l #1,d2
.L412:
	clr.l (284,sp)
	pea (284,sp)
	clr.l -(sp)
	clr.l -(sp)
	move.l #305419896,-(sp)
	move.l (a2)+,-(sp)
	jsr (a3)
	lea _counts,a0
	addq.l #1,(24,a0)
	lea (20,sp),sp
	cmp.l d0,d3
	jne .L69
.L406:
	move.l (284,sp),d1
	cmp.l #305419896,d1
	jeq .L409
	lea _counts,a0
	addq.l #1,(32,a0)
	move.l d7,d3
	tst.l d4
	jne .L410
.L72:
	lea _counts,a0
	addq.l #1,(64,a0)
	tst.l (76,a0)
	jeq .L411
	move.l (_expected_masks.2752,d2.l*4),d3
	addq.l #1,d4
	addq.l #1,d2
	jra .L412
.L79:
	lea 8+_storage,a2
	move.w #8,(88,sp)
	clr.l d3
.L86:
	lea (64,a2),a0
	moveq #3,d5
	moveq #63,d0
	move.l d0,d4
	move.l a0,(48,sp)
.L81:
	move.l a2,a0
	moveq #66,d0
.L82:
	clr.b (a0)+
	dbra d0,.L82
	move.b #-91,(a2)
	move.l d4,d7
	move.l (48,sp),a0
	addq.l #1,d7
	move.b #90,(a0)+
	move.l a0,(48,sp)
	addq.l #2,d4
	move.l d6,a3
	sub.l a6,a6
	moveq #3,d2
	move.l a6,-(sp)
	move.l d3,-(sp)
	move.l d7,-(sp)
	move.l a2,-(sp)
	pea .LC20
	jsr (a4)
	move.l a6,-(sp)
	move.l d3,-(sp)
	move.l d4,-(sp)
	move.l a2,-(sp)
	pea .LC20
	jsr (a4)
	subq.w #1,d2
	lea (40,sp),sp
	jeq .L83
.L413:
	move.l (a3)+,a6
	move.l a6,-(sp)
	move.l d3,-(sp)
	move.l d7,-(sp)
	move.l a2,-(sp)
	pea .LC20
	jsr (a4)
	move.l a6,-(sp)
	move.l d3,-(sp)
	move.l d4,-(sp)
	move.l a2,-(sp)
	pea .LC20
	jsr (a4)
	subq.w #1,d2
	lea (40,sp),sp
	jne .L413
.L83:
	subq.w #1,d5
	move.l d7,d4
	tst.w d5
	jne .L81
	addq.l #1,d3
	addq.l #1,a2
	subq.w #1,(88,sp)
	jne .L86
	lea 4+_large_patterns.2693,a6
	moveq #4,d5
	sub.l a3,a3
	moveq #8,d4
	clr.l d2
.L90:
	move.l d2,d7
	add.l #8+_storage,d7
	move.l a3,-(sp)
	pea 4096.w
	move.l d7,-(sp)
	jsr (a5)
	lea (12,sp),sp
	move.l d6,a2
	moveq #3,d3
	clr.l -(sp)
	move.l d2,-(sp)
	pea 4096.w
	move.l d7,-(sp)
	move.l (_pattern_names,a3.l*4),-(sp)
	jsr (a4)
	subq.w #1,d3
	lea (20,sp),sp
	jeq .L88
.L414:
	move.l (a2)+,-(sp)
	move.l d2,-(sp)
	pea 4096.w
	move.l d7,-(sp)
	move.l (_pattern_names,a3.l*4),-(sp)
	jsr (a4)
	subq.w #1,d3
	lea (20,sp),sp
	jne .L414
.L88:
	addq.l #1,d2
	subq.w #1,d4
	jne .L90
	subq.w #1,d5
	jeq .L256
	move.l (a6)+,a3
	moveq #8,d4
	clr.l d2
	jra .L90
.L256:
	move.l #8+_storage,d5
	move.w #8,a3
	clr.l d7
.L91:
	move.l d5,a0
	moveq #91,d2
	clr.l d3
	move.w #511,d4
.L93:
	clr.b d0
	moveq #127,d1
	cmp.l d3,d1
	jcs .L92
	move.l d3,d0
	lsr.l #4,d0
	move.b d0,d1
	lsl.b #3,d1
	add.b d0,d1
	add.b d1,d1
	add.b d1,d0
	add.b d2,d0
	or.b #1,d0
.L92:
	move.b d0,(a0)+
	addq.l #1,d3
	add.b #73,d2
	dbra d4,.L93
	move.l d6,a2
	moveq #3,d2
	clr.l -(sp)
	move.l d7,-(sp)
	pea 512.w
	move.l d5,-(sp)
	pea .LC21
	jsr (a4)
	subq.w #1,d2
	lea (20,sp),sp
	jeq .L94
.L415:
	move.l (a2)+,-(sp)
	move.l d7,-(sp)
	pea 512.w
	move.l d5,-(sp)
	pea .LC21
	jsr (a4)
	subq.w #1,d2
	lea (20,sp),sp
	jne .L415
.L94:
	addq.l #1,d7
	subq.w #1,a3
	addq.l #1,d5
	move.w a3,d0
	jne .L91
	lea 4+_perturb_positions,a3
	moveq #8,d2
	sub.l a1,a1
	lea 8+_storage,a0
	move.w #4095,d0
.L97:
	clr.b (a0)+
	dbra d0,.L97
	move.l #8+_storage,d0
	move.b #-91,(a1,d0.l)
	move.l d6,a2
	moveq #3,d3
	clr.l -(sp)
	clr.l -(sp)
	pea 4096.w
	pea 8+_storage
	pea .LC22
	jsr (a4)
	subq.w #1,d3
	lea (20,sp),sp
	jeq .L98
.L416:
	move.l (a2)+,-(sp)
	clr.l -(sp)
	pea 4096.w
	pea 8+_storage
	pea .LC22
	jsr (a4)
	subq.w #1,d3
	lea (20,sp),sp
	jne .L416
.L98:
	subq.w #1,d2
	jeq .L417
	move.l (a3)+,a1
	lea 8+_storage,a0
	move.w #4095,d0
	jra .L97
.L417:
	lea 1+_known.2746,a1
	moveq #49,d1
	moveq #-1,d0
	move.l #-306674912,a0
	moveq #8,d4
	and.l #255,d1
	eor.l d1,d0
	moveq #7,d1
.L103:
	move.l d0,d3
	lsr.l #1,d3
	clr.l d2
	btst #0,d0
	jeq .L102
	move.l a0,d2
.L102:
	move.l d3,d0
	eor.l d2,d0
	dbra d1,.L103
	dbra d4,.L392
	not.l d0
	cmp.l #-873187034,d0
	jeq .L106
	lea _counts,a0
	addq.l #1,(64,a0)
	tst.l (76,a0)
	jeq .L418
.L106:
	clr.l -(sp)
	clr.l -(sp)
	pea 9.w
	pea _known.2746
	pea .LC25
	jsr (a4)
	lea _SysBase,a2
	move.l (a2),a6
#APP
| 266 "tests/amiga/crc32_sparse_probe.c" 1
	jsr a6@(-0x29a:W)
| 0 "" 2
#NO_APP
	move.l d0,(68,sp)
	clr.l (304,sp)
	clr.l (308,sp)
	lea (20,sp),sp
	tst.l d0
	jeq .L108
	move.l (a2),a6
	move.l d0,a0
	moveq #40,d0
#APP
| 271 "tests/amiga/crc32_sparse_probe.c" 1
	jsr a6@(-0x28e:W)
| 0 "" 2
#NO_APP
	move.l d0,a3
	move.l (a2),a6
	tst.l d0
	jeq .L401
	lea .LC26,a0
	moveq #2,d0
	clr.l d1
	move.l a3,a1
#APP
| 273 "tests/amiga/crc32_sparse_probe.c" 1
	jsr a6@(-0x1bc:W)
| 0 "" 2
#NO_APP
	tst.b d0
	jne .L419
	move.l (20,a3),a6
	move.l a6,_TimerBase
	lea (284,sp),a0
#APP
| 278 "tests/amiga/crc32_sparse_probe.c" 1
	jsr a6@(-0x3c:W)
| 0 "" 2
#NO_APP
	lea _counts,a0
	move.l d0,d2
	move.l d0,(68,a0)
	jeq .L420
	lea _timing_cases,a2
	move.l #_baseline_ticks,(224,sp)
	move.l #_sparse_ticks,(204,sp)
	move.w #5,(88,sp)
	lea (_time_batch.constprop.0),a6
.L119:
	move.l (4,a2),a4
	move.l (12,a2),-(sp)
	move.l a4,-(sp)
	pea 11+_storage
	jsr (a5)
	lea _counts,a0
	addq.l #1,(36,a0)
	lea (12,sp),sp
	move.l (204,sp),d5
	move.l (224,sp),d6
	moveq #6,d2
	clr.l d4
	moveq #1,d3
	and.l d4,d3
	jne .L112
.L422:
	move.l (8,a2),d1
	move.l d6,-(sp)
	pea 1.w
	move.l d1,-(sp)
	move.l a4,-(sp)
	pea _call_baseline
	move.l d1,(64,sp)
	jsr (a6)
	move.l d0,d7
	move.l d5,-(sp)
	move.l d3,-(sp)
	move.l (72,sp),-(sp)
	move.l a4,-(sp)
	pea _call_sparse
	jsr (a6)
	lea _counts,a0
	move.l d0,d3
	addq.l #1,(56,a0)
	lea (40,sp),sp
.L113:
	cmp.l d7,d3
	jeq .L115
	lea _counts,a1
	addq.l #1,(52,a1)
	move.l (4,a2),d1
	move.l (a2),d0
	addq.l #1,(64,a1)
	tst.l (76,a1)
	jeq .L421
.L115:
	lea _counts,a0
	addq.l #1,(40,a0)
	addq.l #1,d4
	addq.l #8,d6
	addq.l #8,d5
	subq.w #1,d2
	jeq .L117
.L474:
	moveq #1,d3
	move.l (4,a2),a4
	and.l d4,d3
	jeq .L422
.L112:
	move.l (8,a2),d7
	move.l d5,-(sp)
	clr.l -(sp)
	move.l d7,-(sp)
	move.l a4,-(sp)
	pea _call_sparse
	jsr (a6)
	move.l d0,d3
	move.l d6,-(sp)
	pea 1.w
	move.l d7,-(sp)
	move.l a4,-(sp)
	pea _call_baseline
	jsr (a6)
	lea _counts,a0
	move.l d0,d7
	addq.l #1,(60,a0)
	lea (40,sp),sp
	jra .L113
.L392:
	clr.l d1
	move.b (a1)+,d1
	eor.l d1,d0
	moveq #7,d1
	jra .L103
.L419:
	move.l a3,a0
	move.l _SysBase,a6
#APP
| 274 "tests/amiga/crc32_sparse_probe.c" 1
	jsr a6@(-0x294:W)
| 0 "" 2
#NO_APP
	move.l _SysBase,a6
.L401:
	move.l (48,sp),a0
#APP
| 274 "tests/amiga/crc32_sparse_probe.c" 1
	jsr a6@(-0x2a0:W)
| 0 "" 2
#NO_APP
.L108:
	lea _counts,a0
	move.l (64,a0),d0
	addq.l #1,d0
	move.l d0,(204,sp)
	move.l d0,(64,a0)
	move.l (76,a0),(88,sp)
	clr.b (253,sp)
	moveq #48,d6
	clr.w (224,sp)
	tst.l (88,sp)
	jeq .L423
.L120:
	lea (284,sp),a1
	lea 1+.LC29,a0
	moveq #83,d0
	clr.l d3
.L121:
	addq.l #1,d3
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L121
	lea (284,sp,d3.l),a1
	lea 1+.LC30,a0
	moveq #77,d0
.L122:
	addq.l #1,d3
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L122
	lea (284,sp,d3.l),a1
	lea 1+.LC31,a0
	moveq #77,d0
.L123:
	addq.l #1,d3
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L123
	lea (284,sp,d3.l),a1
	lea 1+.LC32,a0
	moveq #70,d0
.L124:
	addq.l #1,d3
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L124
	lea (284,sp,d3.l),a1
	lea 1+.LC33,a0
	moveq #70,d0
.L125:
	addq.l #1,d3
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L125
	lea (284,sp,d3.l),a1
	lea 1+.LC34,a0
	moveq #70,d0
.L126:
	addq.l #1,d3
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L126
	lea (284,sp,d3.l),a1
	lea 1+.LC35,a0
	moveq #70,d0
.L127:
	addq.l #1,d3
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L127
	lea (284,sp,d3.l),a1
	lea 1+.LC36,a0
	moveq #70,d0
.L128:
	addq.l #1,d3
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L128
	lea (284,sp,d3.l),a1
	lea 1+.LC37,a0
	moveq #70,d0
.L129:
	addq.l #1,d3
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L129
	lea (284,sp,d3.l),a1
	lea 1+.LC38,a0
	moveq #70,d0
.L130:
	addq.l #1,d3
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L130
	lea 1+.LC39,a5
	lea (284,sp,d3.l),a1
	move.l a5,a0
	moveq #67,d0
.L131:
	addq.l #1,d3
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L131
	lea (284,sp,d3.l),a2
	lea 1+.LC40,a1
	move.l d3,a0
	moveq #118,d0
	addq.l #1,a0
	move.b d0,(a2)+
	move.b (a1)+,d0
	jeq .L424
.L259:
	move.l a0,d3
	move.l d3,a0
	addq.l #1,a0
	move.b d0,(a2)+
	move.b (a1)+,d0
	jne .L259
.L424:
	move.b #9,(284,sp,a0.l)
	move.l _counts,(60,sp)
	clr.l (56,sp)
	lea (263,sp),a4
	clr.l d4
	lea ___umoddi3,a2
	lea ___udivdi3,a3
	move.l d3,d7
	move.l d4,d5
	move.l (56,sp),d2
	move.l (60,sp),d3
	addq.l #1,d5
	pea 10.w
	clr.l -(sp)
	move.l d3,-(sp)
	move.l d2,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d3,-(sp)
	move.l d2,-(sp)
	jsr (a3)
	move.l d0,d2
	move.l d1,d3
	lea (16,sp),sp
	move.l d2,d0
	or.l d3,d0
	jeq .L425
.L260:
	move.l d5,d4
	move.l d4,d5
	addq.l #1,d5
	pea 10.w
	clr.l -(sp)
	move.l d3,-(sp)
	move.l d2,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d3,-(sp)
	move.l d2,-(sp)
	jsr (a3)
	move.l d0,d2
	move.l d1,d3
	lea (16,sp),sp
	move.l d2,d0
	or.l d3,d0
	jne .L260
.L425:
	move.l d7,d3
	move.l d5,d2
	addq.l #2,d7
	lea (263,sp,d5.l),a1
	lea (284,sp,d7.l),a0
	move.l d5,d3
.L134:
	subq.l #1,d3
	move.b -(a1),(a0)+
	dbra d4,.L134
	clr.w d4
	subq.l #1,d4
	jcc .L134
	add.l d7,d2
	move.b #10,(284,sp,d2.l)
	addq.l #1,d2
	lea (284,sp,d2.l),a1
	move.l a5,a0
	moveq #67,d0
.L135:
	addq.l #1,d2
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L135
	lea (284,sp,d2.l),a4
	lea 1+.LC41,a1
	move.l d2,a0
	moveq #98,d0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jeq .L426
.L261:
	move.l a0,d2
	move.l d2,a0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jne .L261
.L426:
	move.b #9,(284,sp,a0.l)
	move.l 4+_counts,(68,sp)
	clr.l (64,sp)
	lea (263,sp),a4
	move.l d3,d7
	move.l (64,sp),d4
	move.l (68,sp),d5
	addq.l #1,d7
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a3)
	move.l d0,d4
	move.l d1,d5
	lea (16,sp),sp
	move.l d4,d1
	or.l d5,d1
	jeq .L427
.L262:
	move.l d7,d3
	move.l d3,d7
	addq.l #1,d7
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a3)
	move.l d0,d4
	move.l d1,d5
	lea (16,sp),sp
	move.l d4,d1
	or.l d5,d1
	jne .L262
.L427:
	move.l d7,d1
	move.l d2,d0
	addq.l #2,d0
	lea (263,sp,d7.l),a1
	lea (284,sp,d0.l),a0
	move.l d7,d2
.L138:
	subq.l #1,d2
	move.b -(a1),(a0)+
	dbra d3,.L138
	clr.w d3
	subq.l #1,d3
	jcc .L138
	add.l d1,d0
	move.b #10,(284,sp,d0.l)
	move.l d0,d3
	addq.l #1,d3
	lea (284,sp,d3.l),a1
	move.l a5,a0
	moveq #67,d0
.L139:
	addq.l #1,d3
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L139
	lea (284,sp,d3.l),a4
	lea 1+.LC42,a1
	move.l d3,a0
	moveq #115,d0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jeq .L428
.L263:
	move.l a0,d3
	move.l d3,a0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jne .L263
.L428:
	move.b #9,(284,sp,a0.l)
	move.l 8+_counts,(76,sp)
	clr.l (72,sp)
	lea (263,sp),a4
	move.l d2,d7
	move.l (72,sp),d4
	move.l (76,sp),d5
	addq.l #1,d7
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a3)
	move.l d0,d4
	move.l d1,d5
	lea (16,sp),sp
	move.l d4,d0
	or.l d5,d0
	jeq .L429
.L264:
	move.l d7,d2
	move.l d2,d7
	addq.l #1,d7
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a3)
	move.l d0,d4
	move.l d1,d5
	lea (16,sp),sp
	move.l d4,d0
	or.l d5,d0
	jne .L264
.L429:
	move.l d7,d1
	move.l d3,d0
	addq.l #2,d0
	lea (263,sp,d7.l),a1
	lea (284,sp,d0.l),a0
	move.l d7,d3
.L142:
	subq.l #1,d3
	move.b -(a1),(a0)+
	dbra d2,.L142
	clr.w d2
	subq.l #1,d2
	jcc .L142
	add.l d1,d0
	move.b #10,(284,sp,d0.l)
	move.l d0,d2
	addq.l #1,d2
	lea (284,sp,d2.l),a1
	move.l a5,a0
	moveq #67,d0
.L143:
	addq.l #1,d2
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L143
	lea (284,sp,d2.l),a4
	lea 1+.LC43,a1
	move.l d2,a0
	moveq #97,d0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jeq .L430
.L265:
	move.l a0,d2
	move.l d2,a0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jne .L265
.L430:
	move.b #9,(284,sp,a0.l)
	move.l 12+_counts,(84,sp)
	clr.l (80,sp)
	lea (263,sp),a4
	move.l d3,d7
	move.l (80,sp),d4
	move.l (84,sp),d5
	addq.l #1,d7
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a3)
	move.l d0,d4
	move.l d1,d5
	lea (16,sp),sp
	move.l d4,d1
	or.l d5,d1
	jeq .L431
.L266:
	move.l d7,d3
	move.l d3,d7
	addq.l #1,d7
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a3)
	move.l d0,d4
	move.l d1,d5
	lea (16,sp),sp
	move.l d4,d1
	or.l d5,d1
	jne .L266
.L431:
	move.l d7,d1
	move.l d2,d0
	addq.l #2,d0
	lea (263,sp,d7.l),a1
	lea (284,sp,d0.l),a0
	move.l d7,d2
.L146:
	subq.l #1,d2
	move.b -(a1),(a0)+
	dbra d3,.L146
	clr.w d3
	subq.l #1,d3
	jcc .L146
	add.l d1,d0
	move.b #10,(284,sp,d0.l)
	move.l d0,d3
	addq.l #1,d3
	lea (284,sp,d3.l),a1
	move.l a5,a0
	moveq #67,d0
.L147:
	addq.l #1,d3
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L147
	lea (284,sp,d3.l),a4
	lea 1+.LC44,a1
	move.l d3,a0
	moveq #97,d0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jeq .L432
.L267:
	move.l a0,d3
	move.l d3,a0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jne .L267
.L432:
	move.b #9,(284,sp,a0.l)
	move.l 24+_counts,(96,sp)
	clr.l (92,sp)
	lea (263,sp),a4
	move.l d2,d7
	move.l (92,sp),d4
	move.l (96,sp),d5
	addq.l #1,d7
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a3)
	move.l d0,d4
	move.l d1,d5
	lea (16,sp),sp
	move.l d4,d0
	or.l d5,d0
	jeq .L433
.L268:
	move.l d7,d2
	move.l d2,d7
	addq.l #1,d7
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a3)
	move.l d0,d4
	move.l d1,d5
	lea (16,sp),sp
	move.l d4,d0
	or.l d5,d0
	jne .L268
.L433:
	move.l d7,d1
	move.l d3,d0
	addq.l #2,d0
	lea (263,sp,d7.l),a1
	lea (284,sp,d0.l),a0
	move.l d7,d3
.L150:
	subq.l #1,d3
	move.b -(a1),(a0)+
	dbra d2,.L150
	clr.w d2
	subq.l #1,d2
	jcc .L150
	add.l d1,d0
	move.b #10,(284,sp,d0.l)
	move.l d0,d2
	addq.l #1,d2
	lea (284,sp,d2.l),a1
	move.l a5,a0
	moveq #67,d0
.L151:
	addq.l #1,d2
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L151
	lea (284,sp,d2.l),a4
	lea 1+.LC45,a1
	move.l d2,a0
	moveq #97,d0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jeq .L434
.L269:
	move.l a0,d2
	move.l d2,a0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jne .L269
.L434:
	move.b #9,(284,sp,a0.l)
	move.l 28+_counts,(104,sp)
	clr.l (100,sp)
	lea (263,sp),a4
	move.l d3,d7
	move.l (100,sp),d4
	move.l (104,sp),d5
	addq.l #1,d7
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a3)
	move.l d0,d4
	move.l d1,d5
	lea (16,sp),sp
	move.l d4,d1
	or.l d5,d1
	jeq .L435
.L270:
	move.l d7,d3
	move.l d3,d7
	addq.l #1,d7
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a3)
	move.l d0,d4
	move.l d1,d5
	lea (16,sp),sp
	move.l d4,d1
	or.l d5,d1
	jne .L270
.L435:
	move.l d7,d1
	move.l d2,d0
	addq.l #2,d0
	lea (263,sp,d7.l),a1
	lea (284,sp,d0.l),a0
	move.l d7,d2
.L154:
	subq.l #1,d2
	move.b -(a1),(a0)+
	dbra d3,.L154
	clr.w d3
	subq.l #1,d3
	jcc .L154
	add.l d1,d0
	move.b #10,(284,sp,d0.l)
	move.l d0,d3
	addq.l #1,d3
	lea (284,sp,d3.l),a1
	move.l a5,a0
	moveq #67,d0
.L155:
	addq.l #1,d3
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L155
	lea (284,sp,d3.l),a4
	lea 1+.LC46,a1
	move.l d3,a0
	moveq #97,d0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jeq .L436
.L271:
	move.l a0,d3
	move.l d3,a0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jne .L271
.L436:
	move.b #9,(284,sp,a0.l)
	move.l 32+_counts,(112,sp)
	clr.l (108,sp)
	lea (263,sp),a4
	move.l d2,d7
	move.l (108,sp),d4
	move.l (112,sp),d5
	addq.l #1,d7
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a3)
	move.l d0,d4
	move.l d1,d5
	lea (16,sp),sp
	move.l d4,d0
	or.l d5,d0
	jeq .L437
.L272:
	move.l d7,d2
	move.l d2,d7
	addq.l #1,d7
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a3)
	move.l d0,d4
	move.l d1,d5
	lea (16,sp),sp
	move.l d4,d0
	or.l d5,d0
	jne .L272
.L437:
	move.l d7,d1
	move.l d3,d0
	addq.l #2,d0
	lea (263,sp,d7.l),a1
	lea (284,sp,d0.l),a0
	move.l d7,d3
.L158:
	subq.l #1,d3
	move.b -(a1),(a0)+
	dbra d2,.L158
	clr.w d2
	subq.l #1,d2
	jcc .L158
	add.l d1,d0
	move.b #10,(284,sp,d0.l)
	move.l d0,d2
	addq.l #1,d2
	lea (284,sp,d2.l),a1
	move.l a5,a0
	moveq #67,d0
.L159:
	addq.l #1,d2
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L159
	lea (284,sp,d2.l),a4
	lea 1+.LC47,a1
	move.l d2,a0
	moveq #111,d0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jeq .L438
.L273:
	move.l a0,d2
	move.l d2,a0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jne .L273
.L438:
	move.b #9,(284,sp,a0.l)
	move.l 16+_counts,(120,sp)
	clr.l (116,sp)
	lea (263,sp),a4
	move.l d3,d7
	move.l (116,sp),d4
	move.l (120,sp),d5
	addq.l #1,d7
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a3)
	move.l d0,d4
	move.l d1,d5
	lea (16,sp),sp
	move.l d4,d1
	or.l d5,d1
	jeq .L439
.L274:
	move.l d7,d3
	move.l d3,d7
	addq.l #1,d7
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a3)
	move.l d0,d4
	move.l d1,d5
	lea (16,sp),sp
	move.l d4,d1
	or.l d5,d1
	jne .L274
.L439:
	move.l d7,d1
	move.l d2,d0
	addq.l #2,d0
	lea (263,sp,d7.l),a1
	lea (284,sp,d0.l),a0
	move.l d7,d2
.L162:
	subq.l #1,d2
	move.b -(a1),(a0)+
	dbra d3,.L162
	clr.w d3
	subq.l #1,d3
	jcc .L162
	add.l d1,d0
	move.b #10,(284,sp,d0.l)
	move.l d0,d3
	addq.l #1,d3
	lea (284,sp,d3.l),a1
	move.l a5,a0
	moveq #67,d0
.L163:
	addq.l #1,d3
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L163
	lea (284,sp,d3.l),a4
	lea 1+.LC48,a1
	move.l d3,a0
	moveq #97,d0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jeq .L440
.L275:
	move.l a0,d3
	move.l d3,a0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jne .L275
.L440:
	move.b #9,(284,sp,a0.l)
	move.l 20+_counts,(128,sp)
	clr.l (124,sp)
	lea (263,sp),a4
	move.l d2,d7
	move.l (124,sp),d4
	move.l (128,sp),d5
	addq.l #1,d7
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a3)
	move.l d0,d4
	move.l d1,d5
	lea (16,sp),sp
	move.l d4,d0
	or.l d5,d0
	jeq .L441
.L276:
	move.l d7,d2
	move.l d2,d7
	addq.l #1,d7
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a3)
	move.l d0,d4
	move.l d1,d5
	lea (16,sp),sp
	move.l d4,d0
	or.l d5,d0
	jne .L276
.L441:
	move.l d7,d1
	move.l d3,d0
	addq.l #2,d0
	lea (263,sp,d7.l),a1
	lea (284,sp,d0.l),a0
	move.l d7,d3
.L166:
	subq.l #1,d3
	move.b -(a1),(a0)+
	dbra d2,.L166
	clr.w d2
	subq.l #1,d2
	jcc .L166
	add.l d1,d0
	move.b #10,(284,sp,d0.l)
	move.l d0,d2
	addq.l #1,d2
	lea (284,sp,d2.l),a1
	move.l a5,a0
	moveq #67,d0
.L167:
	addq.l #1,d2
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L167
	lea (284,sp,d2.l),a4
	lea 1+.LC49,a1
	move.l d2,a0
	moveq #116,d0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jeq .L442
.L277:
	move.l a0,d2
	move.l d2,a0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jne .L277
.L442:
	move.b #9,(284,sp,a0.l)
	move.l 52+_counts,(136,sp)
	clr.l (132,sp)
	lea (263,sp),a4
	move.l d3,d7
	move.l (132,sp),d4
	move.l (136,sp),d5
	addq.l #1,d7
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a3)
	move.l d0,d4
	move.l d1,d5
	lea (16,sp),sp
	move.l d4,d1
	or.l d5,d1
	jeq .L443
.L278:
	move.l d7,d3
	move.l d3,d7
	addq.l #1,d7
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d5,-(sp)
	move.l d4,-(sp)
	jsr (a3)
	move.l d0,d4
	move.l d1,d5
	lea (16,sp),sp
	move.l d4,d1
	or.l d5,d1
	jne .L278
.L443:
	move.l d7,d4
	move.l d2,d0
	addq.l #2,d0
	lea (263,sp,d7.l),a1
	lea (284,sp,d0.l),a0
	move.l d7,d2
.L170:
	subq.l #1,d2
	move.b -(a1),(a0)+
	dbra d3,.L170
	clr.w d3
	subq.l #1,d3
	jcc .L170
	add.l d0,d4
	move.b #10,(284,sp,d4.l)
	addq.l #1,d4
	lea (284,sp,d4.l),a1
	move.l a5,a0
	moveq #67,d0
.L171:
	addq.l #1,d4
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L171
	lea (284,sp,d4.l),a4
	lea 1+.LC50,a1
	move.l d4,a0
	moveq #101,d0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jeq .L444
.L279:
	move.l a0,d4
	move.l d4,a0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jne .L279
.L444:
	move.b #9,(284,sp,a0.l)
	move.l (204,sp),(144,sp)
	clr.l (140,sp)
	lea (263,sp),a4
	move.l d2,d5
	move.l d5,d7
	move.l (140,sp),d2
	move.l (144,sp),d3
	addq.l #1,d7
	pea 10.w
	clr.l -(sp)
	move.l d3,-(sp)
	move.l d2,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d3,-(sp)
	move.l d2,-(sp)
	jsr (a3)
	move.l d0,d2
	move.l d1,d3
	lea (16,sp),sp
	move.l d2,d0
	or.l d3,d0
	jeq .L445
.L280:
	move.l d7,d5
	move.l d5,d7
	addq.l #1,d7
	pea 10.w
	clr.l -(sp)
	move.l d3,-(sp)
	move.l d2,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d3,-(sp)
	move.l d2,-(sp)
	jsr (a3)
	move.l d0,d2
	move.l d1,d3
	lea (16,sp),sp
	move.l d2,d0
	or.l d3,d0
	jne .L280
.L445:
	move.l d5,d0
	move.l d7,d1
	addq.l #2,d4
	lea (263,sp,d7.l),a1
	lea (284,sp,d4.l),a0
	move.l d7,d5
.L174:
	subq.l #1,d5
	move.b -(a1),(a0)+
	dbra d0,.L174
	clr.w d0
	subq.l #1,d0
	jcc .L174
	move.l d4,d2
	add.l d1,d2
	move.b #10,(284,sp,d2.l)
	addq.l #1,d2
	lea (284,sp,d2.l),a1
	move.l a5,a0
	moveq #67,d0
.L175:
	addq.l #1,d2
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L175
	lea (284,sp,d2.l),a4
	lea 1+.LC51,a1
	move.l d2,a0
	moveq #116,d0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jeq .L446
.L281:
	move.l a0,d2
	move.l d2,a0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jne .L281
.L446:
	move.b #9,(284,sp,a0.l)
	move.b d6,(286,sp,d2.l)
	move.b #10,(287,sp,d2.l)
	addq.l #4,d2
	lea (284,sp,d2.l),a1
	lea 1+.LC52,a0
	moveq #67,d0
.L177:
	addq.l #1,d2
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L177
	move.l 68+_counts,(240,sp)
	clr.l (236,sp)
	lea (263,sp),a4
	move.l d5,d4
	move.l (236,sp),d6
	move.l (240,sp),d7
	addq.l #1,d4
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a3)
	move.l d0,d6
	move.l d1,d7
	lea (16,sp),sp
	move.l d6,d1
	or.l d7,d1
	jeq .L447
.L282:
	move.l d4,d5
	move.l d5,d4
	addq.l #1,d4
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a3)
	move.l d0,d6
	move.l d1,d7
	lea (16,sp),sp
	move.l d6,d1
	or.l d7,d1
	jne .L282
.L447:
	lea (263,sp,d4.l),a1
	lea (284,sp,d2.l),a0
	move.l d4,d3
.L179:
	subq.l #1,d3
	move.b -(a1),(a0)+
	dbra d5,.L179
	clr.w d5
	subq.l #1,d5
	jcc .L179
	add.l d4,d2
	move.b #10,(284,sp,d2.l)
	addq.l #1,d2
	lea (284,sp,d2.l),a1
	move.l a5,a0
	moveq #67,d0
.L180:
	addq.l #1,d2
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L180
	lea (284,sp,d2.l),a4
	lea 1+.LC53,a1
	move.l d2,a0
	moveq #111,d0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jeq .L448
.L283:
	move.l a0,d2
	move.l d2,a0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jne .L283
.L448:
	move.b #9,(284,sp,a0.l)
	move.l 72+_counts,(152,sp)
	clr.l (148,sp)
	lea (263,sp),a4
	move.l d3,d4
	move.l (148,sp),d6
	move.l (152,sp),d7
	addq.l #1,d4
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a3)
	move.l d0,d6
	move.l d1,d7
	lea (16,sp),sp
	move.l d6,d0
	or.l d7,d0
	jeq .L449
.L284:
	move.l d4,d3
	move.l d3,d4
	addq.l #1,d4
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a3)
	move.l d0,d6
	move.l d1,d7
	lea (16,sp),sp
	move.l d6,d0
	or.l d7,d0
	jne .L284
.L449:
	move.l d2,d0
	addq.l #2,d0
	lea (263,sp,d4.l),a1
	lea (284,sp,d0.l),a0
	move.l d4,d2
.L183:
	subq.l #1,d2
	move.b -(a1),(a0)+
	dbra d3,.L183
	clr.w d3
	subq.l #1,d3
	jcc .L183
	add.l d4,d0
	move.b #10,(284,sp,d0.l)
	move.l d0,d3
	addq.l #1,d3
	lea (284,sp,d3.l),a1
	move.l a5,a0
	moveq #67,d0
.L184:
	addq.l #1,d3
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L184
	lea (284,sp,d3.l),a4
	lea 1+.LC54,a1
	move.l d3,a0
	moveq #116,d0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jeq .L450
.L285:
	move.l a0,d3
	move.l d3,a0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jne .L285
.L450:
	move.b #9,(284,sp,a0.l)
	move.l 36+_counts,(160,sp)
	clr.l (156,sp)
	lea (263,sp),a4
	move.l d2,d4
	move.l (156,sp),d6
	move.l (160,sp),d7
	addq.l #1,d4
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a3)
	move.l d0,d6
	move.l d1,d7
	lea (16,sp),sp
	move.l d6,d1
	or.l d7,d1
	jeq .L451
.L286:
	move.l d4,d2
	move.l d2,d4
	addq.l #1,d4
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a3)
	move.l d0,d6
	move.l d1,d7
	lea (16,sp),sp
	move.l d6,d1
	or.l d7,d1
	jne .L286
.L451:
	move.l d3,d0
	addq.l #2,d0
	lea (263,sp,d4.l),a1
	lea (284,sp,d0.l),a0
	move.l d4,d3
.L187:
	subq.l #1,d3
	move.b -(a1),(a0)+
	dbra d2,.L187
	clr.w d2
	subq.l #1,d2
	jcc .L187
	add.l d4,d0
	move.b #10,(284,sp,d0.l)
	move.l d0,d2
	addq.l #1,d2
	lea (284,sp,d2.l),a1
	move.l a5,a0
	moveq #67,d0
.L188:
	addq.l #1,d2
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L188
	lea (284,sp,d2.l),a4
	lea 1+.LC55,a1
	move.l d2,a0
	moveq #116,d0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jeq .L452
.L287:
	move.l a0,d2
	move.l d2,a0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jne .L287
.L452:
	move.b #9,(284,sp,a0.l)
	move.l 40+_counts,(168,sp)
	clr.l (164,sp)
	lea (263,sp),a4
	move.l d3,d4
	move.l (164,sp),d6
	move.l (168,sp),d7
	addq.l #1,d4
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a3)
	move.l d0,d6
	move.l d1,d7
	lea (16,sp),sp
	move.l d6,d0
	or.l d7,d0
	jeq .L453
.L288:
	move.l d4,d3
	move.l d3,d4
	addq.l #1,d4
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a3)
	move.l d0,d6
	move.l d1,d7
	lea (16,sp),sp
	move.l d6,d0
	or.l d7,d0
	jne .L288
.L453:
	move.l d2,d0
	addq.l #2,d0
	lea (263,sp,d4.l),a1
	lea (284,sp,d0.l),a0
	move.l d4,d2
.L191:
	subq.l #1,d2
	move.b -(a1),(a0)+
	dbra d3,.L191
	clr.w d3
	subq.l #1,d3
	jcc .L191
	add.l d4,d0
	move.b #10,(284,sp,d0.l)
	move.l d0,d3
	addq.l #1,d3
	lea (284,sp,d3.l),a1
	move.l a5,a0
	moveq #67,d0
.L192:
	addq.l #1,d3
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L192
	lea (284,sp,d3.l),a4
	lea 1+.LC56,a1
	move.l d3,a0
	moveq #98,d0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jeq .L454
.L289:
	move.l a0,d3
	move.l d3,a0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jne .L289
.L454:
	move.b #9,(284,sp,a0.l)
	move.l 48+_counts,(176,sp)
	clr.l (172,sp)
	lea (263,sp),a4
	move.l d2,d4
	move.l (172,sp),d6
	move.l (176,sp),d7
	addq.l #1,d4
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a3)
	move.l d0,d6
	move.l d1,d7
	lea (16,sp),sp
	move.l d6,d1
	or.l d7,d1
	jeq .L455
.L290:
	move.l d4,d2
	move.l d2,d4
	addq.l #1,d4
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a3)
	move.l d0,d6
	move.l d1,d7
	lea (16,sp),sp
	move.l d6,d1
	or.l d7,d1
	jne .L290
.L455:
	move.l d3,d0
	addq.l #2,d0
	lea (263,sp,d4.l),a1
	lea (284,sp,d0.l),a0
	move.l d4,d3
.L195:
	subq.l #1,d3
	move.b -(a1),(a0)+
	dbra d2,.L195
	clr.w d2
	subq.l #1,d2
	jcc .L195
	add.l d4,d0
	move.b #10,(284,sp,d0.l)
	move.l d0,d2
	addq.l #1,d2
	lea (284,sp,d2.l),a1
	move.l a5,a0
	moveq #67,d0
.L196:
	addq.l #1,d2
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L196
	lea (284,sp,d2.l),a4
	lea 1+.LC57,a1
	move.l d2,a0
	moveq #115,d0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jeq .L456
.L291:
	move.l a0,d2
	move.l d2,a0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jne .L291
.L456:
	move.b #9,(284,sp,a0.l)
	move.l 44+_counts,(184,sp)
	clr.l (180,sp)
	lea (263,sp),a4
	move.l d3,d4
	move.l (180,sp),d6
	move.l (184,sp),d7
	addq.l #1,d4
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a3)
	move.l d0,d6
	move.l d1,d7
	lea (16,sp),sp
	move.l d6,d0
	or.l d7,d0
	jeq .L457
.L292:
	move.l d4,d3
	move.l d3,d4
	addq.l #1,d4
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a3)
	move.l d0,d6
	move.l d1,d7
	lea (16,sp),sp
	move.l d6,d0
	or.l d7,d0
	jne .L292
.L457:
	move.l d2,d0
	addq.l #2,d0
	lea (263,sp,d4.l),a1
	lea (284,sp,d0.l),a0
	move.l d4,d2
.L199:
	subq.l #1,d2
	move.b -(a1),(a0)+
	dbra d3,.L199
	clr.w d3
	subq.l #1,d3
	jcc .L199
	add.l d0,d4
	move.b #10,(284,sp,d4.l)
	addq.l #1,d4
	lea (284,sp,d4.l),a1
	move.l a5,a0
	moveq #67,d0
.L200:
	addq.l #1,d4
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L200
	lea (284,sp,d4.l),a4
	lea 1+.LC58,a1
	move.l d4,a0
	moveq #116,d0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jeq .L458
.L293:
	move.l a0,d4
	move.l d4,a0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jne .L293
.L458:
	move.b #9,(284,sp,a0.l)
	move.l 56+_counts,(192,sp)
	clr.l (188,sp)
	lea (263,sp),a4
	move.l d2,d3
	move.l (188,sp),d6
	move.l (192,sp),d7
	addq.l #1,d3
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a3)
	move.l d0,d6
	move.l d1,d7
	lea (16,sp),sp
	move.l d6,d1
	or.l d7,d1
	jeq .L459
.L294:
	move.l d3,d2
	move.l d2,d3
	addq.l #1,d3
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a3)
	move.l d0,d6
	move.l d1,d7
	lea (16,sp),sp
	move.l d6,d1
	or.l d7,d1
	jne .L294
.L459:
	move.l d4,d0
	addq.l #2,d0
	lea (263,sp,d3.l),a1
	lea (284,sp,d0.l),a0
	move.l d3,d4
.L203:
	subq.l #1,d4
	move.b -(a1),(a0)+
	dbra d2,.L203
	clr.w d2
	subq.l #1,d2
	jcc .L203
	add.l d0,d3
	move.b #10,(284,sp,d3.l)
	addq.l #1,d3
	lea (284,sp,d3.l),a0
	moveq #67,d0
.L204:
	addq.l #1,d3
	move.b d0,(a0)+
	move.b (a5)+,d0
	jne .L204
	lea (284,sp,d3.l),a4
	lea 1+.LC59,a1
	move.l d3,a0
	moveq #116,d0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jeq .L460
.L295:
	move.l a0,d3
	move.l d3,a0
	addq.l #1,a0
	move.b d0,(a4)+
	move.b (a1)+,d0
	jne .L295
.L460:
	move.b #9,(284,sp,a0.l)
	move.l 60+_counts,(200,sp)
	clr.l (196,sp)
	lea (263,sp),a4
	move.l d4,d2
	move.l (196,sp),d6
	move.l (200,sp),d7
	addq.l #1,d2
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a3)
	move.l d0,d6
	move.l d1,d7
	lea (16,sp),sp
	move.l d6,d0
	or.l d7,d0
	jeq .L461
.L296:
	move.l d2,d4
	move.l d4,d2
	addq.l #1,d2
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a3)
	move.l d0,d6
	move.l d1,d7
	lea (16,sp),sp
	move.l d6,d0
	or.l d7,d0
	jne .L296
.L461:
	addq.l #2,d3
	lea (263,sp,d2.l),a1
	lea (284,sp,d3.l),a0
	move.l d2,a6
	move.l d2,d0
.L207:
	subq.l #1,d0
	move.b -(a1),(a0)+
	dbra d4,.L207
	clr.w d4
	subq.l #1,d4
	jcc .L207
	add.l d2,d3
	move.l d0,a5
	move.b #10,(284,sp,d3.l)
	move.l d3,d4
	addq.l #1,d4
	lea (284,sp,d4.l),a1
	lea 1+.LC60,a0
	moveq #84,d0
.L208:
	addq.l #1,d4
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L208
	tst.w (224,sp)
	jeq .L210
	move.l #_timing_cases,(100,sp)
	move.l #_sparse_ticks,(124,sp)
	move.l #_baseline_ticks,(116,sp)
	move.w #5,(108,sp)
.L226:
	move.l (100,sp),a0
	move.l (a0)+,(92,sp)
	move.l (a0)+,(258,sp)
	clr.l (254,sp)
	move.l (a0),(220,sp)
	clr.l (216,sp)
	move.l (116,sp),(64,sp)
	move.l (124,sp),(72,sp)
	clr.l (48,sp)
	clr.l (52,sp)
	move.w #6,(56,sp)
	move.l a5,(132,sp)
	move.l (254,sp),a4
	move.l (258,sp),a5
.L227:
	move.l (52,sp),(80,sp)
	lea (284,sp,d4.l),a1
	lea 1+.LC61,a0
	moveq #84,d0
.L211:
	addq.l #1,d4
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L211
	move.l (92,sp),a0
	lea (284,sp,d4.l),a1
	move.b (a0)+,d0
	jeq .L462
.L213:
	addq.l #1,d4
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L213
.L462:
	move.b #9,(284,sp,d4.l)
	lea (263,sp),a6
	clr.l d7
	move.l d7,d5
	move.l a4,d2
	move.l a5,d3
	addq.l #1,d5
	pea 10.w
	clr.l -(sp)
	move.l d3,-(sp)
	move.l d2,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a6)+
	pea 10.w
	clr.l -(sp)
	move.l d3,-(sp)
	move.l d2,-(sp)
	jsr (a3)
	move.l d0,d2
	move.l d1,d3
	lea (16,sp),sp
	move.l d2,d1
	or.l d3,d1
	jeq .L463
.L297:
	move.l d5,d7
	move.l d7,d5
	addq.l #1,d5
	pea 10.w
	clr.l -(sp)
	move.l d3,-(sp)
	move.l d2,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a6)+
	pea 10.w
	clr.l -(sp)
	move.l d3,-(sp)
	move.l d2,-(sp)
	jsr (a3)
	move.l d0,d2
	move.l d1,d3
	lea (16,sp),sp
	move.l d2,d1
	or.l d3,d1
	jne .L297
.L463:
	addq.l #1,d4
	lea (263,sp,d5.l),a0
	lea (284,sp,d4.l),a1
	move.l d5,d6
	move.l d7,d0
.L215:
	subq.l #1,d6
	move.b -(a0),(a1)+
	dbra d0,.L215
	clr.w d0
	subq.l #1,d0
	jcc .L215
	add.l d5,d4
	move.b #9,(284,sp,d4.l)
	addq.l #1,d4
	lea (263,sp),a6
	move.l d6,d5
	move.l (216,sp),d2
	move.l (220,sp),d3
	addq.l #1,d5
	pea 10.w
	clr.l -(sp)
	move.l d3,-(sp)
	move.l d2,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a6)+
	pea 10.w
	clr.l -(sp)
	move.l d3,-(sp)
	move.l d2,-(sp)
	jsr (a3)
	move.l d0,d2
	move.l d1,d3
	lea (16,sp),sp
	move.l d2,d0
	or.l d3,d0
	jeq .L464
.L298:
	move.l d5,d6
	move.l d6,d5
	addq.l #1,d5
	pea 10.w
	clr.l -(sp)
	move.l d3,-(sp)
	move.l d2,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a6)+
	pea 10.w
	clr.l -(sp)
	move.l d3,-(sp)
	move.l d2,-(sp)
	jsr (a3)
	move.l d0,d2
	move.l d1,d3
	lea (16,sp),sp
	move.l d2,d0
	or.l d3,d0
	jne .L298
.L464:
	lea (263,sp,d5.l),a0
	lea (284,sp,d4.l),a1
	move.l d5,d7
	move.l d6,d0
.L217:
	subq.l #1,d7
	move.b -(a0),(a1)+
	dbra d0,.L217
	clr.w d0
	subq.l #1,d0
	jcc .L217
	add.l d5,d4
	move.b #9,(284,sp,d4.l)
	move.l d4,d5
	addq.l #2,d5
	move.b (55,sp),d0
	add.b #48,d0
	move.b d0,(285,sp,d4.l)
	btst #0,(83,sp)
	jne .L299
	lea .LC16,a0
	lea (284,sp,d5.l),a1
.L219:
	move.b (a0)+,d0
	jeq .L465
.L220:
	addq.l #1,d5
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L220
.L465:
	move.l (64,sp),a0
	move.l (a0)+,d2
	move.l (a0)+,d3
	move.l a0,(64,sp)
	move.l d7,d4
	lea (263,sp),a6
	addq.l #1,d4
	pea 10.w
	clr.l -(sp)
	move.l d3,-(sp)
	move.l d2,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a6)+
	pea 10.w
	clr.l -(sp)
	move.l d3,-(sp)
	move.l d2,-(sp)
	jsr (a3)
	move.l d0,d2
	move.l d1,d3
	lea (16,sp),sp
	move.l d2,d0
	or.l d3,d0
	jeq .L466
.L300:
	move.l d4,d7
	move.l d7,d4
	addq.l #1,d4
	pea 10.w
	clr.l -(sp)
	move.l d3,-(sp)
	move.l d2,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a6)+
	pea 10.w
	clr.l -(sp)
	move.l d3,-(sp)
	move.l d2,-(sp)
	jsr (a3)
	move.l d0,d2
	move.l d1,d3
	lea (16,sp),sp
	move.l d2,d0
	or.l d3,d0
	jne .L300
.L466:
	lea (263,sp,d4.l),a0
	lea (284,sp,d5.l),a1
	move.l d4,d6
	move.l d7,d0
.L222:
	subq.l #1,d6
	move.b -(a0),(a1)+
	dbra d0,.L222
	clr.w d0
	subq.l #1,d0
	jcc .L222
	add.l d5,d4
	move.b #9,(284,sp,d4.l)
	move.l (72,sp),a0
	move.l (a0)+,d2
	move.l (a0)+,d3
	move.l a0,(72,sp)
	addq.l #1,d4
	move.l d6,d5
	lea (263,sp),a6
	addq.l #1,d5
	pea 10.w
	clr.l -(sp)
	move.l d3,-(sp)
	move.l d2,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a6)+
	pea 10.w
	clr.l -(sp)
	move.l d3,-(sp)
	move.l d2,-(sp)
	jsr (a3)
	move.l d0,d2
	move.l d1,d3
	lea (16,sp),sp
	move.l d2,d0
	or.l d3,d0
	jeq .L467
.L301:
	move.l d5,d6
	move.l d6,d5
	addq.l #1,d5
	pea 10.w
	clr.l -(sp)
	move.l d3,-(sp)
	move.l d2,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a6)+
	pea 10.w
	clr.l -(sp)
	move.l d3,-(sp)
	move.l d2,-(sp)
	jsr (a3)
	move.l d0,d2
	move.l d1,d3
	lea (16,sp),sp
	move.l d2,d0
	or.l d3,d0
	jne .L301
.L467:
	lea (263,sp,d5.l),a0
	lea (284,sp,d4.l),a1
	move.l d6,d0
.L224:
	move.b -(a0),(a1)+
	dbra d0,.L224
	clr.w d0
	subq.l #1,d0
	jcc .L224
	add.l d5,d4
	move.b #10,(284,sp,d4.l)
	addq.l #1,d4
	moveq #0,d1
	moveq #1,d2
	add.l d2,(52,sp)
	move.l (48,sp),d0
	addx.l d1,d0
	move.l d0,(48,sp)
	subq.w #1,(56,sp)
	jne .L227
	moveq #16,d0
	move.l (132,sp),a5
	add.l d0,(100,sp)
	moveq #48,d0
	add.l d0,(124,sp)
	add.l d0,(116,sp)
	subq.w #1,(108,sp)
	jne .L226
.L210:
	tst.l (88,sp)
	jeq .L228
	lea (284,sp,d4.l),a1
	lea 1+.LC62,a0
	moveq #70,d0
.L229:
	addq.l #1,d4
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L229
	move.l (88,sp),a1
	lea (284,sp,d4.l),a0
	move.b (a1)+,d0
	jeq .L468
.L231:
	addq.l #1,d4
	move.b d0,(a0)+
	move.b (a1)+,d0
	jne .L231
.L468:
	move.b #9,(284,sp,d4.l)
	lea _counts,a0
	addq.l #1,d4
	move.l (80,a0),a1
	lea (284,sp,d4.l),a0
	move.b (a1)+,d0
	jeq .L469
.L233:
	addq.l #1,d4
	move.b d0,(a0)+
	move.b (a1)+,d0
	jne .L233
.L469:
	move.b #9,(284,sp,d4.l)
	move.l 84+_counts,(212,sp)
	clr.l (208,sp)
	lea (263,sp),a4
	move.l a5,d2
	move.l (208,sp),d6
	move.l (212,sp),d7
	addq.l #1,d2
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a3)
	move.l d0,d6
	move.l d1,d7
	lea (16,sp),sp
	move.l d6,d0
	or.l d7,d0
	jeq .L470
.L302:
	move.l d2,a5
	move.l a5,d2
	addq.l #1,d2
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a3)
	move.l d0,d6
	move.l d1,d7
	lea (16,sp),sp
	move.l d6,d0
	or.l d7,d0
	jne .L302
.L470:
	move.l d4,d0
	addq.l #1,d0
	lea (263,sp,d2.l),a1
	lea (284,sp,d0.l),a0
	move.l d2,d4
.L235:
	subq.l #1,d4
	move.b -(a1),(a0)+
	subq.l #1,a5
	cmp.l #-1,a5
	jne .L235
	add.l d2,d0
	move.b #9,(284,sp,d0.l)
	move.l d0,d3
	addq.l #1,d3
	move.l 88+_counts,(248,sp)
	clr.l (244,sp)
	lea (263,sp),a4
	move.l d4,d2
	move.l (244,sp),d6
	move.l (248,sp),d7
	addq.l #1,d2
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a3)
	move.l d0,d6
	move.l d1,d7
	lea (16,sp),sp
	move.l d6,d1
	or.l d7,d1
	jeq .L471
.L303:
	move.l d2,d4
	move.l d4,d2
	addq.l #1,d2
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a2)
	add.b #48,d1
	lea (16,sp),sp
	move.b d1,(a4)+
	pea 10.w
	clr.l -(sp)
	move.l d7,-(sp)
	move.l d6,-(sp)
	jsr (a3)
	move.l d0,d6
	move.l d1,d7
	lea (16,sp),sp
	move.l d6,d1
	or.l d7,d1
	jne .L303
.L471:
	lea (263,sp,d2.l),a0
	lea (284,sp,d3.l),a1
.L237:
	move.b -(a0),(a1)+
	dbra d4,.L237
	clr.w d4
	subq.l #1,d4
	jcc .L237
	add.l d2,d3
	move.l d3,a0
	move.b #9,(284,sp,d3.l)
	lea _counts,a1
	move.l (96,a1),d3
	lea (285,sp,a0.l),a1
	moveq #28,d2
	moveq #7,d1
.L238:
	move.l d3,d0
	lsr.l d2,d0
	moveq #15,d4
	and.l d4,d0
	move.l d0,a2
	move.b (_hex.2963,a2),(a1)+
	subq.l #4,d2
	dbra d1,.L238
	move.b #9,(293,sp,a0.l)
	lea _counts,a1
	move.l (100,a1),d3
	lea (294,sp,a0.l),a1
	moveq #28,d1
	moveq #7,d0
.L239:
	move.l d3,d2
	lsr.l d1,d2
	moveq #15,d4
	and.l d4,d2
	move.l d2,a2
	move.b (_hex.2963,a2),(a1)+
	subq.l #4,d1
	dbra d0,.L239
	move.b #9,(302,sp,a0.l)
	lea _counts,a1
	move.l (104,a1),d3
	lea (303,sp,a0.l),a1
	moveq #28,d1
	moveq #7,d0
.L240:
	move.l d3,d2
	lsr.l d1,d2
	moveq #15,d4
	and.l d4,d2
	move.l d2,a2
	move.b (_hex.2963,a2),(a1)+
	subq.l #4,d1
	dbra d0,.L240
	move.b #9,(311,sp,a0.l)
	lea _counts,a1
	move.l (92,a1),d3
	lea (312,sp,a0.l),a1
	moveq #28,d1
	moveq #7,d0
.L241:
	move.l d3,d2
	lsr.l d1,d2
	moveq #15,d4
	and.l d4,d2
	move.l d2,a2
	move.b (_hex.2963,a2),(a1)+
	subq.l #4,d1
	dbra d0,.L241
	move.b #10,(320,sp,a0.l)
	moveq #37,d4
	add.l a0,d4
.L228:
	tst.l (204,sp)
	jne .L305
	tst.w (224,sp)
	jeq .L305
	lea .LC17,a0
	lea (284,sp,d4.l),a1
.L243:
	move.b (a0)+,d0
	jeq .L472
.L244:
	addq.l #1,d4
	move.b d0,(a1)+
	move.b (a0)+,d0
	jne .L244
.L472:
	move.l _DOSBase,a6
	move.l #.LC63,d1
	move.l #1006,d2
#APP
| 461 "tests/amiga/crc32_sparse_probe.c" 1
	jsr a6@(-0x1e:W)
| 0 "" 2
#NO_APP
	move.l d0,d5
	jne .L473
.L250:
	move.l (228,sp),a0
	move.l (232,sp),(184,a0)
	moveq #20,d0
.L67:
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6
	lea (8432,sp),sp
	rts
.L299:
	lea .LC15,a0
	lea (284,sp,d5.l),a1
	jra .L219
.L305:
	lea .LC18,a0
	lea (284,sp,d4.l),a1
	jra .L243
.L473:
	move.l _DOSBase,a6
	move.l d0,d1
	move.l sp,d2
	add.l #284,d2
	move.l d4,d3
#APP
| 463 "tests/amiga/crc32_sparse_probe.c" 1
	jsr a6@(-0x30:W)
| 0 "" 2
#NO_APP
	move.l d0,d2
	move.l d5,d1
	move.l _DOSBase,a6
#APP
| 464 "tests/amiga/crc32_sparse_probe.c" 1
	jsr a6@(-0x24:W)
| 0 "" 2
#NO_APP
	tst.l d0
	jeq .L247
	cmp.l d4,d2
	seq d0
	ext.w d0
	neg.w d0
	clr.l d5
	lea _counts,a0
	tst.l (64,a0)
	jeq .L253
.L248:
	tst.w d0
	jeq .L250
	lea _DOSBase,a2
	move.l (a2),a6
	move.l #.LC64,d1
	move.l #1006,d2
#APP
| 467 "tests/amiga/crc32_sparse_probe.c" 1
	jsr a6@(-0x1e:W)
| 0 "" 2
#NO_APP
	move.l d0,d4
	jeq .L250
	move.l (a2),a6
	move.l d0,d1
	move.l #_complete.3012,d2
	moveq #26,d3
#APP
| 470 "tests/amiga/crc32_sparse_probe.c" 1
	jsr a6@(-0x30:W)
| 0 "" 2
#NO_APP
	move.l d0,d2
	move.l d4,d1
	move.l (a2),a6
#APP
| 472 "tests/amiga/crc32_sparse_probe.c" 1
	jsr a6@(-0x24:W)
| 0 "" 2
#NO_APP
	tst.l d0
	jeq .L250
	move.l (228,sp),a0
	move.l (232,sp),(184,a0)
	moveq #20,d0
	cmp.l d2,d3
	jne .L67
	tst.l d5
	jeq .L67
	clr.l d0
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6
	lea (8432,sp),sp
	rts
.L421:
	move.w #76,a0
	add.l a1,a0
	move.l #.LC27,(a0)+
	move.l d0,(a0)+
	move.l d1,(a0)+
	moveq #3,d0
	move.l d0,(a0)
	move.l #305419896,(96,a1)
	move.l d7,(100,a1)
	move.l d3,(104,a1)
	clr.l (92,a1)
	lea _counts,a0
	addq.l #1,(40,a0)
	addq.l #1,d4
	addq.l #8,d6
	addq.l #8,d5
	subq.w #1,d2
	jne .L474
.L117:
	moveq #48,d0
	lea (16,a2),a2
	add.l d0,(224,sp)
	add.l d0,(204,sp)
	subq.w #1,(88,sp)
	jne .L119
	lea _SysBase,a2
	move.l a3,a1
	move.l (a2),a6
#APP
| 289 "tests/amiga/crc32_sparse_probe.c" 1
	jsr a6@(-0x1c2:W)
| 0 "" 2
#NO_APP
	move.l a3,a0
	move.l (a2),a6
#APP
| 290 "tests/amiga/crc32_sparse_probe.c" 1
	jsr a6@(-0x294:W)
| 0 "" 2
#NO_APP
	move.l (48,sp),a0
	move.l (a2),a6
#APP
| 290 "tests/amiga/crc32_sparse_probe.c" 1
	jsr a6@(-0x2a0:W)
| 0 "" 2
#NO_APP
	clr.l _TimerBase
	lea _counts,a0
	move.l (64,a0),(204,sp)
	move.l (76,a0),(88,sp)
	move.b #1,(253,sp)
	moveq #49,d6
	move.w #1,(224,sp)
	lea (284,sp),a1
	lea 1+.LC29,a0
	moveq #83,d0
	clr.l d3
	jra .L121
.L407:
	move.w #76,a0
	add.l #_counts,a0
	move.l #.LC19,(a0)+
	move.l d6,(a0)+
	clr.l (a0)+
	clr.l (a0)
	lea _counts,a0
	move.l #305419896,(96,a0)
	move.l d3,(100,a0)
	move.l d1,(104,a0)
	move.l d0,(92,a0)
	jra .L71
.L418:
	move.w #76,a0
	add.l #_counts,a0
	move.l #.LC23,(a0)+
	move.l #.LC24,(a0)+
	moveq #9,d1
	move.l d1,(a0)+
	clr.l (a0)
	lea _counts,a0
	clr.l (96,a0)
	move.l #-873187034,(100,a0)
	move.l d0,(104,a0)
	clr.l (92,a0)
	jra .L106
.L409:
	addq.l #1,(28,a0)
	jra .L71
.L247:
	lea _counts,a0
	tst.l (64,a0)
	jne .L250
.L253:
	clr.l d5
	move.b (253,sp),d5
	jra .L248
.L423:
	move.w #76,a0
	add.l #_counts,a0
	move.l #.LC14,(a0)+
	move.l #.LC28,(a0)+
	move.l (88,sp),(a0)+
	move.l (88,sp),(a0)
	lea _counts,a0
	move.l (88,sp),(96,a0)
	move.l (88,sp),(100,a0)
	move.l (88,sp),(104,a0)
	move.l (88,sp),(92,a0)
	move.l #.LC14,(88,sp)
	lea (284,sp),a1
	lea 1+.LC29,a0
	moveq #83,d0
	clr.l d3
	jra .L121
.L420:
	lea _SysBase,a2
	move.l a3,a1
	move.l (a2),a6
#APP
| 280 "tests/amiga/crc32_sparse_probe.c" 1
	jsr a6@(-0x1c2:W)
| 0 "" 2
#NO_APP
	move.l a3,a0
	move.l (a2),a6
#APP
| 281 "tests/amiga/crc32_sparse_probe.c" 1
	jsr a6@(-0x294:W)
| 0 "" 2
#NO_APP
	move.l (48,sp),a0
	move.l (a2),a6
#APP
| 281 "tests/amiga/crc32_sparse_probe.c" 1
	jsr a6@(-0x2a0:W)
| 0 "" 2
#NO_APP
	move.l d2,_TimerBase
	lea _counts,a0
	move.l (64,a0),d0
	addq.l #1,d0
	move.l d0,(204,sp)
	move.l d0,(64,a0)
	move.l (76,a0),(88,sp)
	clr.b (253,sp)
	moveq #48,d6
	clr.w (224,sp)
	tst.l (88,sp)
	jne .L120
	jra .L423
.L411:
	move.w #76,a0
	add.l #_counts,a0
	move.l #.LC19,(a0)+
	move.l d5,(a0)+
	clr.l (a0)+
	clr.l (a0)
	lea _counts,a0
	move.l #305419896,(96,a0)
	move.l d3,(100,a0)
	move.l d1,(104,a0)
	move.l d0,(92,a0)
	jra .L71
	.text
_complete.3012:
	.ascii "SPARSE_CRC_PROBE_COMPLETE\12\0"
	.data
	.align	2
_controls.2751:
	.long	_bfs_crc32_abi_clobber_d2
	.long	_bfs_crc32_abi_clobber_a2
	.text
	.align	2
_expected_masks.2752:
	.long	4
	.long	256
	.align	2
_small_patterns.2692:
	.long	0
	.long	1
	.long	2
	.align	2
_large_patterns.2693:
	.long	0
	.long	1
	.long	2
	.long	3
_known.2746:
	.byte	49
	.byte	50
	.byte	51
	.byte	52
	.byte	53
	.byte	54
	.byte	55
	.byte	56
	.byte	57
_hex.2963:
	.ascii "0123456789ABCDEF\0"
	.align	2
_time_batch.constprop.0:
	lea (-16,sp),sp
	movem.l a6/a4/a3/a2/d6/d5/d4/d3/d2,-(sp)
	lea (56,sp),a0
	move.l (a0)+,a3
	move.l (a0)+,d4
	move.l (a0)+,d5
	move.l (a0)+,a4
	move.l (a0),a2
	clr.l (36,sp)
	clr.l (40,sp)
	clr.l (44,sp)
	clr.l (48,sp)
	lea (36,sp),a0
	move.l _TimerBase,a6
#APP
| 306 "tests/amiga/crc32_sparse_probe.c" 1
	jsr a6@(-0x3c:W)
| 0 "" 2
#NO_APP
	tst.l d5
	jeq .L478
	move.l d5,d6
	muls.l #-1640531527,d6
	clr.l d3
	move.l #1831565813,d2
.L477:
	move.l (a3),a0
	move.l d4,-(sp)
	pea 11+_storage
	move.l #305419896,-(sp)
	jsr (a0)
	rol.l #5,d2
	eor.l d0,d2
	eor.l d3,d2
	add.l #-1640531527,d3
	lea (12,sp),sp
	cmp.l d3,d6
	jne .L477
	lea (44,sp),a0
	move.l _TimerBase,a6
#APP
| 312 "tests/amiga/crc32_sparse_probe.c" 1
	jsr a6@(-0x3c:W)
| 0 "" 2
#NO_APP
	move.l (44,sp),d0
	move.l (48,sp),d1
	move.l (36,sp),d3
	sub.l (40,sp),d1
	subx.l d3,d0
	move.l d0,(a2)
	move.l d1,(4,a2)
	move.l d2,_timing_sink
	add.l d5,(44+_counts,a4.l*4)
	move.l d2,d0
	movem.l (sp)+,d2/d3/d4/d5/d6/a2/a3/a4/a6
	lea (16,sp),sp
	rts
.L478:
	move.l #1831565813,d2
	lea (44,sp),a0
	move.l _TimerBase,a6
#APP
| 312 "tests/amiga/crc32_sparse_probe.c" 1
	jsr a6@(-0x3c:W)
| 0 "" 2
#NO_APP
	move.l (44,sp),d0
	move.l (48,sp),d1
	move.l (36,sp),d3
	sub.l (40,sp),d1
	subx.l d3,d0
	move.l d0,(a2)
	move.l d1,(4,a2)
	move.l d2,_timing_sink
	add.l d5,(44+_counts,a4.l*4)
	move.l d2,d0
	movem.l (sp)+,d2/d3/d4/d5/d6/a2/a3/a4/a6
	lea (16,sp),sp
	rts
