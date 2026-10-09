| GNU C99 (GCC) version 6.5.0b 260303094121^ (m68k-amigaos)
|	compiled by GNU C version 12.5.0, GMP version 6.3.0, MPFR version 4.2.2, MPC version 1.4.1, isl version 0.15
| GGC heuristics: --param ggc-min-expand=100 --param ggc-min-heapsize=131072
| options passed:  -I src/amiga -I include -I tests
| -I /opt/homebrew/opt/amiga-gcc/m68k-amigaos/ndk-include
| -imultilib libm020 -iprefix /opt/homebrew/lib/gcc/m68k-amigaos/6.5.0b/
| -isystem m68k-amigaos/../include -isystem m68k-amigaos/libnix/include
| -Dlibnix -D__libnix__ -D__libnix -D BFS_AMIGA=1 src/core/btree.c
| -mcpu=68020
| -auxbase-strip build/perf-2026-10-09/exact-fastpath/asm-review/btree-current.s
| -O2 -Wall -Wextra -Werror -Wno-pointer-sign -Wframe-larger-than=16384
| -std=c99 -fomit-frame-pointer -fverbose-asm
| options enabled:  -faggressive-loop-optimizations -falign-functions
| -falign-jumps -falign-labels -falign-loops -fauto-inc-dec
| -fbranch-count-reg -fcaller-saves -fchkp-check-incomplete-type
| -fchkp-check-read -fchkp-check-write -fchkp-instrument-calls
| -fchkp-narrow-bounds -fchkp-optimize -fchkp-store-bounds
| -fchkp-use-static-bounds -fchkp-use-static-const-bounds
| -fchkp-use-wrappers -fcombine-stack-adjustments -fcommon -fcompare-elim
| -fcprop-registers -fcrossjumping -fcse-follow-jumps -fdefer-pop
| -fdelete-null-pointer-checks -fdevirtualize -fdevirtualize-speculatively
| -fearly-inlining -feliminate-unused-debug-types -fexpensive-optimizations
| -fforward-propagate -ffunction-cse -fgcse -fgcse-lm -fgnu-runtime
| -fgnu-unique -fguess-branch-probability -fhoist-adjacent-loads -fident
| -fif-conversion -fif-conversion2 -findirect-inlining -finline
| -finline-atomics -finline-functions-called-once -finline-small-functions
| -fipa-cp -fipa-cp-alignment -fipa-icf -fipa-icf-functions
| -fipa-icf-variables -fipa-profile -fipa-pure-const -fipa-ra
| -fipa-reference -fipa-sra -fira-hoist-pressure -fira-share-save-slots
| -fira-share-spill-slots -fisolate-erroneous-paths-dereference -fivopts
| -fkeep-static-consts -fleading-underscore -flifetime-dse
| -floop-size-optimize -flra-remat -flto-odr-type-merging -fmath-errno
| -fmerge-constants -fmerge-debug-strings -fmove-loop-invariants
| -fomit-frame-pointer -foptimize-sibling-calls -foptimize-strlen
| -fpartial-inlining -fpcc-struct-return -fpeephole -fpeephole2 -fplt
| -fprefetch-loop-arrays -freorder-blocks -freorder-functions
| -frerun-cse-after-loop -fsched-critical-path-heuristic
| -fsched-dep-count-heuristic -fsched-group-heuristic -fsched-interblock
| -fsched-last-insn-heuristic -fsched-rank-heuristic -fsched-spec
| -fsched-spec-insn-heuristic -fsched-stalled-insns-dep -fschedule-fusion
| -fsemantic-interposition -fshow-column -fshrink-wrap -fsigned-zeros
| -fsplit-ivs-in-unroller -fsplit-wide-types -fssa-backprop -fssa-phiopt
| -fstdarg-opt -fstrict-aliasing -fstrict-overflow
| -fstrict-volatile-bitfields -fsync-libcalls -fthread-jumps
| -ftrapping-math -ftree-bit-ccp -ftree-builtin-call-dce -ftree-ccp
| -ftree-ch -ftree-coalesce-vars -ftree-copy-prop -ftree-dce
| -ftree-dominator-opts -ftree-dse -ftree-forwprop -ftree-fre
| -ftree-loop-if-convert -ftree-loop-im -ftree-loop-ivcanon
| -ftree-loop-optimize -ftree-parallelize-loops= -ftree-phiprop -ftree-pre
| -ftree-pta -ftree-reassoc -ftree-scev-cprop -ftree-sink -ftree-slsr
| -ftree-sra -ftree-switch-conversion -ftree-tail-merge -ftree-ter
| -ftree-vrp -funit-at-a-time -fverbose-asm -fzero-initialized-in-bss
| -mbitfield -mstrict-align

	.text
	.align	2
_get_child:
	subq.l #4,sp	|,
	move.l d2,-(sp)	|,
	move.l (12,sp),a0	| tree, tree
	move.l (8,a0),a1	| tree_2(D)->ops, tree_2(D)->ops
	move.l (4,a1),d2	| _9->key_size, _10
	moveq #-32,d0	|, tmp50
	move.l (a0),a0	| MEM[(struct bfs_bio_t * *)tree_2(D)], MEM[(struct bfs_bio_t * *)tree_2(D)]
	add.l (4,a0),d0	| _6->block_size, tmp50
	move.l d2,d1	| _10, tmp51
	addq.l #4,d1	|, tmp51
	divu.l d1,d0	| tmp51, tmp53
	move.l (20,sp),a0	| i, i
	lea (28,a0.l*4),a0	|, tmp57
	muls.l d2,d0	| _10, tmp55
	add.l a0,d0	| tmp57, tmp58
	add.l (16,sp),d0	| buf, tmp58
	lea (4,sp),a1	|,, tmp61
	move.l d0,a0	| tmp58, tmp60
	move.l (a0)+,(a1)+	|,
	move.l (4,sp),d0	| v,
	move.l (sp)+,d2	|,
	addq.l #4,sp	|,
	rts
	.align	2
_set_child:
	subq.l #4,sp	|,
	movem.l a5/a2/d3/d2,-(sp)	|,
	lea (24,sp),a0	|,, tmp61
	move.l (a0)+,a1	| tree, tree
	move.l (a0)+,d1	| buf, buf
	move.l (8,a1),a2	| tree_2(D)->ops, tree_2(D)->ops
	move.l (a0)+,a5	| i, i
	move.l (4,a2),d3	| _9->key_size, _10
	moveq #-32,d0	|, tmp49
	move.l (a1),a1	| MEM[(struct bfs_bio_t * *)tree_2(D)], MEM[(struct bfs_bio_t * *)tree_2(D)]
	add.l (4,a1),d0	| _5->block_size, tmp49
	move.l d3,d2	| _10, tmp50
	addq.l #4,d2	|, tmp50
	divu.l d2,d0	| tmp50, tmp52
	lea (28,a5.l*4),a1	|, tmp56
	muls.l d3,d0	| _10, tmp54
	lea (20,sp),a5	|,, tmp58
	add.l a1,d0	| tmp56, tmp57
	move.l (a0),-(a5)	| blk, v
	add.l d0,d1	| tmp57, buf
	move.l d1,a0	| buf, _17
	move.l (a5)+,(a0)+	|,
	movem.l (sp)+,d2/d3/a2/a5	|,
	addq.l #4,sp	|,
	rts
	.align	2
_node_structure_ok:
	subq.l #4,sp	|,
	movem.l a4/a3/a2/d6/d5/d4/d3/d2,-(sp)	|,
	move.l (40,sp),a3	| tree, tree
	move.l (44,sp),a2	| buf, buf
	move.w (20,a2),d4	|, _13
	jne .L4	|
	move.l (8,a3),a0	| tree_17(D)->ops, _39
	moveq #-28,d0	|, tmp101
	move.l (a3),a1	| MEM[(struct bfs_bio_t * *)tree_17(D)], MEM[(struct bfs_bio_t * *)tree_17(D)]
	add.l (4,a1),d0	| _43->block_size, tmp101
	move.l (8,a0),d1	| _23->val_size, tmp102
	add.l (4,a0),d1	| _23->key_size, tmp102
	divu.l d1,d0	| tmp102, _16
	move.l (16,a2),d3	|, _15
	jeq .L8	|
.L38:
	cmp.l d3,d0	| _15, _16
	jcs .L8	|
	tst.w (22,a2)	|
	jne .L8	|
	move.l (24,a2),d0	|, _22
	jne .L9	|
	moveq #1,d2	|, i
	cmp.l d3,d2	| _15, i
	jeq .L15	|
.L14:
	move.l (4,a0),d1	| _26->key_size, _58
	move.l d2,d0	| i, tmp112
	muls.l d1,d0	| _58, tmp112
	pea (28,a2,d0.l)	|
	move.l d2,d0	| i, tmp115
	subq.l #1,d0	|, tmp115
	muls.l d1,d0	| _58, tmp116
	pea (28,a2,d0.l)	|
	move.l (a0),a0	| _26->key_compare, _26->key_compare
	jsr (a0)	| _26->key_compare
	addq.l #8,sp	|,
	tst.l d0	|
	jge .L8	|
	addq.l #1,d2	|, i
	move.l (8,a3),a0	| tree_17(D)->ops, _39
	cmp.l d3,d2	| _15, i
	jcs .L14	|
.L15:
	tst.w d4	| _13
	jeq .L12	|
	move.l (a3),a3	| MEM[(struct bfs_bio_t * *)tree_17(D)], pretmp_131
	move.l (4,a0),d2	| pretmp_136->key_size, pretmp_138
	moveq #-32,d0	|, tmp121
	add.l (4,a3),d0	| pretmp_131->block_size, tmp121
	move.l d2,d1	| pretmp_138, tmp122
	addq.l #4,d1	|, tmp122
	divu.l d1,d0	| tmp122, tmp124
	muls.l d2,d0	| pretmp_138, tmp126
	lea (28,a2,d0.l),a2	|,
	clr.l d0	|
.L16:
	lea (32,sp),a0	|,, tmp130
	move.l a2,a1	|, ivtmp.329
	move.l (a1)+,(a0)+	|,
	move.l (32,sp),d1	| v, v.0_89
	jeq .L8	|
	cmp.l (8,a3),d1	| pretmp_131->block_count, v.0_89
	jcc .L8	|
	addq.l #1,d0	|,
	move.l a1,a2	| ivtmp.329,
	cmp.l d3,d0	| _15,
	jls .L16	|
.L17:
	moveq #1,d0	|, <retval>
	movem.l (sp)+,d2/d3/d4/d5/d6/a2/a3/a4	|,
	addq.l #4,sp	|,
	rts
.L4:
	move.l (a3),a0	| MEM[(struct bfs_bio_t * *)tree_17(D)], MEM[(struct bfs_bio_t * *)tree_17(D)]
	move.l (4,a0),a1	| _14->block_size, _48
	move.l (8,a3),a0	| tree_17(D)->ops, _39
	move.l (4,a0),d1	| _50->key_size, _51
	cmp.w #31,d4	|, _13
	jls .L6	|
.L8:
	clr.l d0	| <retval>
	movem.l (sp)+,d2/d3/d4/d5/d6/a2/a3/a4	|,
	addq.l #4,sp	|,
	rts
.L6:
	moveq #-32,d0	|, tmp107
	add.l a1,d0	| _48, tmp107
	addq.l #4,d1	|, tmp108
	divu.l d1,d0	| tmp108, _16
	move.l (16,a2),d3	|, _15
	jne .L38	|
	jra .L8	|
.L9:
	tst.w d4	| _13
	jne .L8	|
	move.l (a3),a1	| tree_17(D)->bio, tree_17(D)->bio
	cmp.l (8,a1),d0	| _24->block_count, _22
	jcc .L8	|
	moveq #1,d2	|, i
	cmp.l d3,d2	| _15, i
	jne .L14	|
.L12:
	move.l (16,a0),a1	| _39->entry_ok, _40
	move.l a1,d0	|,
	jeq .L17	|
	clr.l d4	|
.L18:
	move.l (4,a0),d0	| prephitmp_128->key_size,
	move.l (8,a0),d2	| prephitmp_128->val_size,
	moveq #-28,d1	|, tmp132
	move.l (a3),a0	| MEM[(struct bfs_bio_t * *)tree_17(D)], MEM[(struct bfs_bio_t * *)tree_17(D)]
	add.l (4,a0),d1	| _65->block_size, tmp132
	move.l d0,d6	|, tmp133
	add.l d2,d6	|, tmp133
	divu.l d6,d1	| tmp133, tmp135
	muls.l d0,d1	|, tmp137
	move.l d1,a4	| tmp137,
	muls.l d4,d2	|,
	lea (28,a4,d2.l),a0	|, tmp140
	pea (a2,a0.l)	|
	muls.l d4,d0	|,
	pea (28,a2,d0.l)	|
	jsr (a1)	| _40
	addq.l #8,sp	|,
	tst.l d0	|
	jeq .L8	|
	addq.l #1,d4	|,
	cmp.l d3,d4	| _15,
	jeq .L17	|
	move.l (8,a3),a0	| tree_17(D)->ops, _39
	move.l (16,a0),a1	| pretmp_127->entry_ok, _40
	jra .L18	|
	.align	2
_node_read:
	link.w a5,#-32	|,
	movem.l a6/a4/a3/a2/d6/d5/d4/d3/d2,-(sp)	|,
	move.l (8,a5),a2	| tree, tree
	move.l (12,a5),d2	| blk, blk
	move.l (16,a5),a3	| buf, buf
	jeq .L56	|
	move.l (a2),a0	| tree_5(D)->bio, _6
	cmp.l (8,a0),d2	| _6->block_count, blk
	jcc .L56	|
	move.l (a0),a1	| _6->ops, _11
	moveq #-8,d3	|, <retval>
	move.l a1,d0	|,
	jeq .L39	|
	move.l (a1),a1	| _11->read_block, _10
	move.l a1,d0	|,
	jeq .L39	|
	move.l a3,d0	|,
	jeq .L39	|
	move.l a3,-(sp)	| buf,
	move.l d2,-(sp)	| blk,
	move.l a0,-(sp)	| _6,
	jsr (a1)	| _10
	move.l d0,d3	|, <retval>
	lea (12,sp),sp	|,
	jne .L39	|
	move.l (a2),a4	| MEM[(struct bfs_bio_t * *)tree_5(D)], _17
	move.l (8,a2),a1	| MEM[(const struct bfs_btree_ops_t * *)tree_5(D) + 8B], _18
	move.l (4,a1),d5	| _18->key_size, _20
	move.l (8,a1),d4	| _18->val_size, _21
	move.l (4,a4),d1	| _17->block_size, _22
	lea (-20,a5),a0	|,, tmp109
	move.l (8,a4),d0	| _17->block_count, _23
	move.l (a1)+,(a0)+	| _18->key_compare, MEM[(struct bfs_node_validation *)&validation]
	move.l d5,(a0)+	| _20, MEM[(struct bfs_node_validation *)&validation + 4B]
	move.l d4,(a0)+	| _21, MEM[(struct bfs_node_validation *)&validation + 8B]
	move.l d1,(a0)+	| _22, MEM[(struct bfs_node_validation *)&validation + 12B]
	move.l d0,(a0)	| _23, MEM[(struct bfs_node_validation *)&validation + 16B]
	tst.l (8,a1)	| _18->cache_key_order
	jeq .L44	|
	move.l (a4),a0	| _17->ops, _25
	move.l a0,d0	|,
	jeq .L44	|
	move.l (28,a0),a0	| _25->node_structure_valid, _26
	move.l a0,d0	|,
	jeq .L44	|
	pea (-20,a5)	|
	move.l d2,-(sp)	| blk,
	move.l a4,-(sp)	| _17,
	jsr (a0)	| _26
	lea (12,sp),sp	|,
	tst.l d0	|
	jne .L39	|
.L44:
	cmp.l #1112821316,(a3)	|, MEM[(struct bfs_btnode_hdr_t *)buf_9(D)]
	jne .L56	|
	move.l (a2),a0	| tree_5(D)->bio, _30
	move.l a0,d0	|,
	jeq .L46	|
	move.l (a0),a1	| _30->ops, _31
	move.l a1,d0	|,
	jeq .L46	|
	move.l (20,a1),a1	| _31->node_crc_valid, _32
	move.l a1,d0	|,
	jeq .L46	|
	move.l d2,-(sp)	| blk,
	move.l a0,-(sp)	| _30,
	jsr (a1)	| _32
	addq.l #8,sp	|,
	tst.l d0	|
	jeq .L46	|
.L49:
	move.l a3,-(sp)	| buf,
	move.l a2,-(sp)	| tree,
	jsr _node_structure_ok	|
	addq.l #8,sp	|,
	tst.l d0	|
	jeq .L56	|
	move.l (a2),a0	| tree_5(D)->bio, _43
	move.l a0,d0	|,
	jeq .L39	|
	move.l (a0),a1	| _38->ops, prephitmp_34
	move.l a1,d0	|,
	jeq .L39	|
	move.l (24,a1),a3	| _39->mark_node_crc_valid, _40
	move.l a3,d0	|,
	jeq .L51	|
	move.l d2,-(sp)	| blk,
	move.l a0,-(sp)	| _43,
	jsr (a3)	| _40
	move.l (8,a2),a0	| tree_5(D)->ops, tree_5(D)->ops
	addq.l #8,sp	|,
	tst.l (12,a0)	| _41->cache_key_order
	jeq .L39	|
	move.l (a2),a0	| tree_5(D)->bio, _43
	move.l a0,d0	|,
	jeq .L39	|
	move.l (a0),a1	| _43->ops, prephitmp_34
	move.l a1,d0	|,
	jeq .L39	|
.L54:
	move.l (32,a1),a1	| prephitmp_34->mark_node_structure_valid, _45
	move.l a1,d0	|,
	jeq .L39	|
	pea (-20,a5)	|
	move.l d2,-(sp)	| blk,
	move.l a0,-(sp)	| _43,
	jsr (a1)	| _45
.L39:
	move.l d3,d0	| <retval>,
	movem.l (-68,a5),d2/d3/d4/d5/d6/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L46:
	move.l (4,a3),d4	|, _35
	pea (-32,a5)	|
	move.l a3,-(sp)	| buf,
	move.l a2,-(sp)	| tree,
	jsr _node_ranges	|
	lea (12,sp),sp	|,
	tst.l d0	| _47
	jeq .L108	|
	move.l (4,a3),d6	|, saved_crc
	clr.l (4,a3)	| MEM[(struct bfs_btnode_hdr_t *)buf_9(D)].crc32
	move.l (-28,a5),d5	| r.values_start, values_end
	add.l (-24,a5),d5	| r.values_length, values_end
	move.l (-32,a5),-(sp)	| r.prefix_end,
	move.l a3,-(sp)	| buf,
	clr.l -(sp)	|
	lea _bfs_crc32,a6	|, tmp93
	jsr (a6)	| tmp93
	move.l (-32,a5),a0	| r.prefix_end, _59
	move.l (-28,a5),d1	| r.values_start,
	sub.l a0,d1	| _59,
	move.l d1,-(sp)	|,
	pea (a3,a0.l)	|
	move.l d0,-(sp)	|,
	lea _crc_range,a4	|, tmp96
	jsr (a4)	| tmp96
	move.l (-24,a5),-(sp)	| r.values_length,
	move.l a3,d1	| buf,
	add.l (-28,a5),d1	| r.values_start,
	move.l d1,-(sp)	|,
	move.l d0,-(sp)	|,
	jsr (a6)	| tmp93
	move.l (a2),a0	| tree_5(D)->bio, tree_5(D)->bio
	move.l (4,a0),d1	| _67->block_size,
	sub.l d5,d1	| values_end,
	lea (32,sp),sp	|,
	move.l d1,(sp)	|,
	pea (a3,d5.l)	|
	move.l d0,-(sp)	|,
	jsr (a4)	| tmp96
	move.l d6,(4,a3)	| saved_crc, MEM[(struct bfs_btnode_hdr_t *)buf_9(D)].crc32
	lea (12,sp),sp	|,
	cmp.l d4,d0	| _35, crc
	jeq .L49	|
.L56:
	moveq #-3,d1	|,
	move.l d1,d0	|,
	movem.l (-68,a5),d2/d3/d4/d5/d6/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L108:
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_5(D)], _48
	move.l (4,a3),d5	|, saved_crc
	move.l d0,(4,a3)	| _47, MEM[(struct bfs_btnode_hdr_t *)buf_9(D)].crc32
	move.l (4,a0),-(sp)	| _48->block_size,
	move.l a3,-(sp)	| buf,
	move.l d0,-(sp)	| _47,
	jsr _bfs_crc32	|
	move.l d5,(4,a3)	| saved_crc, MEM[(struct bfs_btnode_hdr_t *)buf_9(D)].crc32
	lea (12,sp),sp	|,
	cmp.l d4,d0	| _35, crc
	jeq .L49	|
	jra .L56	|
.L51:
	move.l (8,a2),a2	| tree_5(D)->ops, tree_5(D)->ops
	tst.l (12,a2)	| _133->cache_key_order
	jne .L54	|
	move.l d3,d0	| <retval>,
	movem.l (-68,a5),d2/d3/d4/d5/d6/a2/a3/a4/a6	|,
	unlk a5	|
	rts
	.align	2
_node_ranges:
	movem.l a4/a3/a2/d4/d3/d2,-(sp)	|,
	move.l (28,sp),a1	| tree, tree
	move.l (32,sp),a0	| buf, buf
	move.l (36,sp),a2	| out, out
	move.l (16,a0),d2	|, _15
	move.w (20,a0),d1	|, _16
	jne .L110	|
	move.l (a1)+,a0	| MEM[(struct bfs_bio_t * *)tree_10(D)], MEM[(struct bfs_bio_t * *)tree_10(D)]
	move.l (4,a0),a3	| _13->block_size, _12
	move.l (4,a1),a0	| tree_10(D)->ops, _9
	move.l (4,a0),d1	| _9->key_size, _8
	move.l (8,a0),d3	| _9->val_size, _32
	move.l d1,d0	| _8, tmp62
	lea (-28,a3),a0	|, _12, tmp61
	add.l d3,d0	| _32, tmp62
	move.l a0,d4	| tmp61, tmp63
	divu.l d0,d4	| tmp62, tmp63
	clr.l d0	| <retval>
	cmp.l d2,d4	| _15, tmp63
	jcc .L129	|
.L109:
	movem.l (sp)+,d2/d3/d4/a2/a3/a4	|,
	rts
.L110:
	clr.l d0	| <retval>
	cmp.w #31,d1	|, _16
	jhi .L109	|
	move.l (a1)+,a0	| MEM[(struct bfs_bio_t * *)tree_10(D)], MEM[(struct bfs_bio_t * *)tree_10(D)]
	move.l (4,a0),a3	| _35->block_size, _12
	move.l (4,a1),a0	| tree_10(D)->ops, tree_10(D)->ops
	move.l (4,a0),d1	| _38->key_size, _39
	moveq #-32,d3	|, tmp74
	add.l a3,d3	| _12, tmp74
	move.l d1,d4	| _39, tmp75
	addq.l #4,d4	|, tmp75
	divu.l d4,d3	| tmp75, tmp76
	cmp.l d2,d3	| _15, tmp76
	jcs .L109	|
	move.l d2,d0	| _15,
	move.w #28,a1	|, tmp69
	muls.l d1,d0	| _39,
	add.l d0,a1	|, tmp69
	move.w #28,a0	|, values_start
	muls.l d3,d1	| tmp76, tmp70
	lea (4,d2.l*4),a4	|,
	add.l d1,a0	| tmp70, values_start
	move.l a4,d2	|, iftmp.9_4
.L114:
	clr.l d0	| <retval>
	cmp.l a1,a0	| _80, values_start
	jcs .L109	|
	cmp.l a3,a0	| _12, values_start
	jhi .L109	|
	sub.l a0,a3	| values_start, tmp65
	cmp.l d2,a3	| iftmp.9_4, tmp65
	jcs .L109	|
	move.l a1,(a2)+	| _80, out_27(D)->prefix_end
	move.l a0,(a2)+	| values_start, out_27(D)->values_start
	move.l d2,(a2)	| iftmp.9_4, out_27(D)->values_length
	moveq #1,d0	|, <retval>
	movem.l (sp)+,d2/d3/d4/a2/a3/a4	|,
	rts
.L129:
	move.l d1,d0	| _8,
	move.w #28,a1	|, tmp67
	muls.l d2,d0	| _15,
	add.l d0,a1	|, tmp67
	move.w #28,a0	|, values_start
	muls.l d4,d1	| tmp63, tmp68
	add.l d1,a0	| tmp68, values_start
	muls.l d3,d2	| _32, iftmp.9_4
	jra .L114	|
	.align	2
_crc_range:
	movem.l a2/d3/d2,-(sp)	|,
	move.l (16,sp),d3	| crc, crc
	move.l (20,sp),d2	| p, p
	move.l (24,sp),a2	| length, length
	moveq #15,d0	|,
	move.l a2,a1	| length, length
	move.l d2,a0	| p, p
	cmp.l a2,d0	| length,
	jcc .L131	|
	moveq #-16,d1	|, tmp51
	add.l a2,d1	| length, tmp51
	lsr.l #4,d1	|, tmp50
.L133:
	move.l (a0),d0	| MEM[(const bfs_any_word_t * {ref-all})p_34], tmp45
	or.l (4,a0),d0	| MEM[(const bfs_any_word_t * {ref-all})p_34 + 4B], tmp45
	or.l (8,a0),d0	| MEM[(const bfs_any_word_t * {ref-all})p_34 + 8B], tmp46
	or.l (12,a0),d0	| MEM[(const bfs_any_word_t * {ref-all})p_34 + 12B], tmp47
	jne .L132	|
	lea (16,a0),a0	|, p
	lea (-16,a1),a1	|, length
	dbra d1,.L133	| tmp50,
	clr.w d1	| tmp50
	subq.l #1,d1	| tmp50
	jcc .L133	|
.L131:
	move.l a1,d0	|,
	jeq .L134	|
	subq.l #1,a1	|,
.L135:
	tst.b (a0)+	| MEM[base: p_31, offset: 0B]
	jne .L132	|
	subq.l #1,a1	|
	cmp.l #-1,a1	|
	jne .L135	|
.L134:
	move.l a2,(20,sp)	| length,
	move.l d3,(16,sp)	| crc,
	movem.l (sp)+,d2/d3/a2	|,
	jmp _bfs_crc32_zeros	|
.L132:
	move.l a2,(24,sp)	| length,
	move.l d2,(20,sp)	| p,
	move.l d3,(16,sp)	| crc,
	movem.l (sp)+,d2/d3/a2	|,
	jmp _bfs_crc32	|
	.align	2
_node_compute_write_crc:
	link.w a5,#-12	|,
	movem.l a6/a4/a3/a2/d3/d2,-(sp)	|,
	move.l (8,a5),a4	| tree, tree
	move.l (12,a5),a2	| buf, buf
	pea (-12,a5)	|
	move.l a2,-(sp)	| buf,
	move.l a4,-(sp)	| tree,
	jsr _node_ranges	|
	lea (12,sp),sp	|,
	tst.l d0	|
	jeq .L146	|
	move.l (-8,a5),d2	| r.values_start, _8
	move.l d2,d3	| _8, gap
	move.l (-12,a5),a3	| r.prefix_end, _9
	sub.l a3,d3	| _9, gap
	move.l (a4),a0	| tree_4(D)->bio, tree_4(D)->bio
	move.l (4,a0),a4	| _13->block_size, tail
	add.l (-4,a5),d2	| r.values_length, values_end
	sub.l d2,a4	| values_end, tail
	move.l d3,-(sp)	| gap,
	clr.l -(sp)	|
	pea (a2,a3.l)	|
	lea _memset,a6	|, tmp60
	jsr (a6)	| tmp60
	move.l a4,-(sp)	| tail,
	clr.l -(sp)	|
	pea (a2,d2.l)	|
	jsr (a6)	| tmp60
	move.l a3,-(sp)	| _9,
	move.l a2,-(sp)	| buf,
	clr.l -(sp)	|
	lea _bfs_crc32,a6	|, tmp68
	jsr (a6)	| tmp68
	lea (32,sp),sp	|,
	move.l d3,(sp)	| gap,
	move.l d0,-(sp)	|,
	lea _bfs_crc32_zeros,a3	|, tmp69
	jsr (a3)	| tmp69
	move.l (-4,a5),-(sp)	| r.values_length,
	add.l (-8,a5),a2	| r.values_start,
	move.l a2,-(sp)	|,
	move.l d0,-(sp)	|,
	jsr (a6)	| tmp68
	move.l a4,-(sp)	| tail,
	move.l d0,-(sp)	|,
	jsr (a3)	| tmp69
	move.l (16,a5),a0	| crc_out, crc_out
	move.l d0,(a0)	|, *crc_out_31(D)
	clr.l d0	| <retval>
	movem.l (-36,a5),d2/d3/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L146:
	moveq #-3,d0	|, <retval>
	movem.l (-36,a5),d2/d3/a2/a3/a4/a6	|,
	unlk a5	|
	rts
	.align	2
_node_finalize_deferred:
	link.w a5,#-104	|,
	move.l a2,-(sp)	|,
	move.l (16,a5),a2	| buf, buf
	lea (-100,a5),a0	|,, tmp36
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	move.l (12,a5),(-96,a5)	| block_size, geometry.block_size
	lea (-88,a5),a0	|,, tmp37
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	lea (-100,a5),a0	|,,
	move.l a0,(-88,a5)	|, tree.bio
	move.l (8,a5),(-80,a5)	| layout, tree.ops
	clr.l (4,a2)	| MEM[(struct bfs_btnode_hdr_t *)buf_11(D)].crc32
	pea (-104,a5)	|
	move.l a2,-(sp)	| buf,
	pea (-88,a5)	|
	jsr _node_compute_write_crc	|
	tst.l d0	| <retval>
	jne .L149	|
	move.l (-104,a5),(4,a2)	| crc, MEM[(struct bfs_btnode_hdr_t *)buf_11(D)].crc32
.L149:
	move.l (-108,a5),a2	|,
	unlk a5	|
	rts
	.align	2
_node_write_image:
	link.w a5,#-20	|,
	movem.l a6/a4/a3/a2/d3/d2,-(sp)	|,
	lea (8,a5),a0	|,, tmp88
	move.l (a0)+,a2	| tree, tree
	move.l (a0)+,d2	| blk, blk
	move.l (a0)+,a4	| image, image
	move.l (a0),d3	| adopt, adopt
	tst.l d2	| blk
	jeq .L181	|
	move.l (a2),a0	| tree_11(D)->bio, _12
	cmp.l (8,a0),d2	| _12->block_count, blk
	jcc .L181	|
	move.l (a4),a3	| *image_14(D), buf
	addq.l #1,(60,a2)	|, tree_11(D)->generation
	move.l #1112821316,(a3)	|, MEM[(struct bfs_btnode_hdr_t *)buf_15].magic
	move.l (20,a2),a1	| MEM[(const uint64_t * *)tree_11(D) + 20B], _51
	move.l a1,d0	|,
	jeq .L155	|
	move.l (a1),d0	| *_51, iftmp.35_54
	move.l (4,a1),d1	| *_51,
.L156:
	move.l d0,(8,a3)	| iftmp.35_54, MEM[(struct bfs_btnode_hdr_t *)buf_15].txn_id
	move.l d1,(12,a3)	|, MEM[(struct bfs_btnode_hdr_t *)buf_15].txn_id
	clr.l (4,a3)	| MEM[(struct bfs_btnode_hdr_t *)buf_15].crc32
	move.l (a0),a0	| MEM[(const struct bfs_bio_t *)_12].ops, _45
	move.l a0,d0	|,
	jeq .L159	|
	tst.l (44,a0)	| _45->defer_node_block
	jeq .L159	|
	tst.l (48,a0)	| _45->flush_deferred
	jeq .L159	|
	tst.l (52,a0)	| _45->discard_deferred
	jeq .L159	|
	move.l d2,-(sp)	| blk,
	move.l a2,-(sp)	| tree,
	jsr _owned_contains	|
	addq.l #8,sp	|,
	tst.l d0	|
	jeq .L159	|
	move.l (8,a2),d0	| tree_11(D)->ops,
	move.l (a2),a0	| tree_11(D)->bio, _30
	tst.l d3	| adopt
	jne .L224	|
.L162:
	move.l a0,d1	|,
	jne .L164	|
.L159:
	pea (-20,a5)	|
	move.l a3,-(sp)	| buf,
	move.l a2,-(sp)	| tree,
	jsr _node_compute_write_crc	|
	lea (12,sp),sp	|,
	tst.l d0	| <retval>
	jeq .L225	|
.L153:
	movem.l (-44,a5),d2/d3/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L225:
	move.l (-20,a5),(4,a3)	| crc, MEM[(struct bfs_btnode_hdr_t *)buf_15].crc32
	move.l (a2),a0	| tree_11(D)->bio, _47
	move.l a0,d0	|,
	jeq .L183	|
	move.l (a0),a1	| _47->ops, _94
	move.l a1,d0	|,
	jeq .L183	|
	cmp.l (8,a0),d2	| _47->block_count, blk
	jcc .L183	|
	move.l (16,a1),a2	| _94->write_node_block, _96
	move.l a2,d0	|,
	jeq .L175	|
	move.l a3,-(sp)	| buf,
	move.l d2,-(sp)	| blk,
	move.l a0,-(sp)	| _47,
	jsr (a2)	| _96
	movem.l (-44,a5),d2/d3/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L175:
	move.l (4,a1),a1	| _94->write_block, _98
	move.l a1,d0	|,
	jeq .L183	|
	move.l a3,-(sp)	| buf,
	move.l d2,-(sp)	| blk,
	move.l a0,-(sp)	| _47,
	jsr (a1)	| _98
	movem.l (-44,a5),d2/d3/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L224:
	move.l a0,d1	|,
	jeq .L159	|
	move.l (a0),a1	| MEM[(const struct bfs_bio_t *)_30].ops, pretmp_76
	move.l a1,d1	|,
	jeq .L164	|
	tst.l (44,a1)	| _71->defer_node_block
	jeq .L164	|
	tst.l (48,a1)	| _71->flush_deferred
	jeq .L164	|
	tst.l (52,a1)	| _71->discard_deferred
	jeq .L164	|
	move.l (56,a1),a6	| _71->adopt_node_block, _77
	move.l a6,d1	|,
	jeq .L166	|
	tst.l (36,a1)	| _71->alloc_buffer
	jeq .L166	|
	tst.l (40,a1)	| _71->free_buffer
	jeq .L166	|
	tst.l (a4)	| MEM[(void * *)image_14(D)]
	jeq .L183	|
	cmp.l (8,a0),d2	| _30->block_count, blk
	jcc .L183	|
	move.l d0,-(sp)	|,
	pea _node_finalize_deferred	|
	move.l a4,-(sp)	| image,
	move.l d2,-(sp)	| blk,
	move.l a0,-(sp)	| _30,
	jsr (a6)	| _77
	lea (20,sp),sp	|,
	moveq #-10,d1	|,
	cmp.l d0,d1	| <retval>,
	jeq .L226	|
	tst.l d0	| <retval>
	jne .L153	|
.L179:
	move.l (8,a2),a0	| tree_11(D)->ops, tree_11(D)->ops
	tst.l (12,a0)	| _37->cache_key_order
	jne .L227	|
.L172:
	clr.l d0	| <retval>
.L228:
	movem.l (-44,a5),d2/d3/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L155:
	move.l (24,a2),d0	| MEM[(const uint64_t *)tree_11(D) + 24B], iftmp.35_54
	move.l (28,a2),d1	| MEM[(const uint64_t *)tree_11(D) + 24B],
	jra .L156	|
.L181:
	moveq #-3,d0	|, <retval>
	movem.l (-44,a5),d2/d3/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L227:
	move.l a3,-(sp)	| buf,
	move.l a2,-(sp)	| tree,
	jsr _node_structure_ok	|
	addq.l #8,sp	|,
	tst.l d0	|
	jeq .L172	|
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_11(D)], _52
	move.l (8,a2),a2	| MEM[(const struct bfs_btree_ops_t * *)tree_11(D) + 8B], _53
	move.l (4,a2),a3	| _53->key_size, _60
	move.l (8,a2),d3	| _53->val_size, _61
	move.l (4,a1),d1	| _52->block_size, _62
	lea (-20,a5),a0	|,, tmp89
	move.l (8,a1),d0	| _52->block_count, _63
	move.l (a2),(a0)+	| _53->key_compare, MEM[(struct bfs_node_validation *)&validation]
	move.l a3,(a0)+	| _60, MEM[(struct bfs_node_validation *)&validation + 4B]
	move.l d3,(a0)+	| _61, MEM[(struct bfs_node_validation *)&validation + 8B]
	move.l d1,(a0)+	| _62, MEM[(struct bfs_node_validation *)&validation + 12B]
	move.l d0,(a0)	| _63, MEM[(struct bfs_node_validation *)&validation + 16B]
	move.l (a1),a0	| _52->ops, _57
	move.l a0,d0	|,
	jeq .L172	|
	move.l (32,a0),a0	| _57->mark_node_structure_valid, _58
	move.l a0,d0	|,
	jeq .L172	|
	pea (-20,a5)	|
	move.l d2,-(sp)	| blk,
	move.l a1,-(sp)	| _52,
	jsr (a0)	| _58
	clr.l d0	| <retval>
	jra .L228	|
.L164:
	move.l (a0),a1	| MEM[(const struct bfs_bio_t *)prephitmp_93].ops, pretmp_76
	move.l a1,d1	|,
	jeq .L159	|
.L166:
	move.l (44,a1),a4	| prephitmp_102->defer_node_block, _85
	move.l a4,d1	|,
	jeq .L159	|
	tst.l (48,a1)	| prephitmp_102->flush_deferred
	jeq .L159	|
	tst.l (52,a1)	| prephitmp_102->discard_deferred
	jeq .L159	|
	cmp.l (8,a0),d2	| _132->block_count, blk
	jcc .L183	|
	move.l d0,-(sp)	|,
	pea _node_finalize_deferred	|
	move.l a3,-(sp)	| buf,
	move.l d2,-(sp)	| blk,
	move.l a0,-(sp)	| _30,
	jsr (a4)	| _85
	lea (20,sp),sp	|,
	tst.l d0	| <retval>
	jeq .L179	|
	moveq #-10,d1	|,
	cmp.l d0,d1	| <retval>,
	jne .L153	|
	pea (-20,a5)	|
	move.l a3,-(sp)	| buf,
	move.l a2,-(sp)	| tree,
	jsr _node_compute_write_crc	|
	lea (12,sp),sp	|,
	tst.l d0	| <retval>
	jne .L153	|
	jra .L225	|
.L183:
	moveq #-8,d0	|, <retval>
	movem.l (-44,a5),d2/d3/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L226:
	move.l (8,a2),d0	| tree_11(D)->ops,
	move.l (a2),a0	| tree_11(D)->bio, _30
	jra .L162	|
	.align	2
_node_view:
	link.w a5,#-20	|,
	movem.l a6/a4/a3/a2/d6/d5/d4/d3/d2,-(sp)	|,
	lea (8,a5),a0	|,, tmp110
	move.l (a0)+,a4	| tree, tree
	move.l (a0)+,d3	| blk, blk
	move.l (a0)+,a6	| buf, buf
	move.l (a0)+,d0	| level, level
	move.l (a0)+,a3	| bounds, bounds
	move.l (a0),d6	| node, node
	move.l d6,a0	| node,
	move.w d0,d2	| level, level
	move.l a6,(a0)	| buf, *node_7(D)
	tst.l d3	| blk
	jeq .L230	|
	move.l (a4),a2	| tree_11(D)->bio, _12
	move.l (8,a2),d0	| _12->block_count, _13
	cmp.l d3,d0	| blk, _13
	jls .L230	|
	move.l (8,a4),a1	| tree_11(D)->ops, _14
	tst.l (12,a1)	| _14->cache_key_order
	jeq .L232	|
	move.l (4,a1),d5	| _14->key_size, _42
	move.l (8,a1),d4	| _14->val_size, _43
	lea (-20,a5),a0	|,, tmp111
	move.l (4,a2),d1	| _12->block_size, _44
	move.l (a1),(a0)+	| _14->key_compare, MEM[(struct bfs_node_validation *)&validation]
	move.l d5,(a0)+	| _42, MEM[(struct bfs_node_validation *)&validation + 4B]
	move.l d4,(a0)+	| _43, MEM[(struct bfs_node_validation *)&validation + 8B]
	move.l d1,(a0)+	| _44, MEM[(struct bfs_node_validation *)&validation + 12B]
	move.l d0,(a0)	| _13, MEM[(struct bfs_node_validation *)&validation + 16B]
	move.l (a2),a0	| _12->ops, _38
	move.l a0,d0	|,
	jeq .L232	|
	move.l (68,a0),a0	| _38->peek_valid_node, _36
	move.l a0,d0	|,
	jeq .L232	|
	pea (-20,a5)	|
	move.l d3,-(sp)	| blk,
	move.l a2,-(sp)	| _12,
	jsr (a0)	| _36
	move.l d0,a0	|, iftmp.2_28
	lea (12,sp),sp	|,
	tst.l d0	| iftmp.2_28
	jeq .L232	|
	move.l d6,a1	| node,
	move.l d0,(a1)	| iftmp.2_28, *node_7(D)
	cmp.w (20,a0),d2	|, level
	jne .L230	|
	move.l a3,d0	|,
	jeq .L240	|
	tst.l (1024,a3)	| bounds_17(D)->have_lower
	jne .L241	|
	tst.l (1028,a3)	| bounds_17(D)->have_upper
	jne .L270	|
.L240:
	clr.l d0	| <retval>
.L229:
	movem.l (-56,a5),d2/d3/d4/d5/d6/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L270:
	move.l d6,a2	| node,
	move.l (8,a4),a0	| tree_11(D)->ops, _32
	move.l (a2),a1	| *node_7(D), _35
	pea (512,a3)	|
	move.l (16,a1),d0	|, tmp104
	subq.l #1,d0	|, tmp104
	muls.l (4,a0),d0	| _32->key_size, tmp105
	pea (28,a1,d0.l)	|
	move.l (a0),a0	|* _32, _32->key_compare
	jsr (a0)	| _32->key_compare
	tst.l d0	|
	jlt .L240	|
.L230:
	moveq #-3,d0	|, <retval>
	movem.l (-56,a5),d2/d3/d4/d5/d6/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L232:
	move.l a3,d0	|,
	jeq .L236	|
	move.l a6,-(sp)	| buf,
	move.l d3,-(sp)	| blk,
	move.l a4,-(sp)	| tree,
	jsr _node_read	|
	lea (12,sp),sp	|,
	tst.l d0	| <retval>
	jne .L229	|
	cmp.w (20,a6),d2	|, level
	jne .L230	|
	tst.l (1024,a3)	| bounds_17(D)->have_lower
	jne .L271	|
	tst.l (1028,a3)	| bounds_17(D)->have_upper
	jeq .L240	|
.L239:
	move.l (8,a4),a0	| tree_11(D)->ops, _73
	pea (512,a3)	|
	move.l (16,a6),d0	|, tmp94
	subq.l #1,d0	|, tmp94
	muls.l (4,a0),d0	| _73->key_size, tmp95
	pea (28,a6,d0.l)	|
	move.l (a0),a0	|* _73, _73->key_compare
	jsr (a0)	| _73->key_compare
	tst.l d0	|
	jge .L230	|
	clr.l d0	| <retval>
	jra .L229	|
.L241:
	move.l (8,a4),a1	| tree_11(D)->ops, tree_11(D)->ops
	move.l a3,-(sp)	| bounds,
	pea (28,a0)	|
	move.l (a1),a0	| _25->key_compare, _25->key_compare
	jsr (a0)	| _25->key_compare
	addq.l #8,sp	|,
	tst.l d0	|
	jlt .L230	|
	tst.l (1028,a3)	| bounds_17(D)->have_upper
	jeq .L240	|
	jra .L270	|
.L271:
	move.l (8,a4),a0	| tree_11(D)->ops, tree_11(D)->ops
	move.l a3,-(sp)	| bounds,
	pea (28,a6)	|
	move.l (a0),a0	| _67->key_compare, _67->key_compare
	jsr (a0)	| _67->key_compare
	addq.l #8,sp	|,
	tst.l d0	|
	jlt .L230	|
	tst.l (1028,a3)	| bounds_17(D)->have_upper
	jeq .L240	|
	jra .L239	|
.L236:
	move.l a6,-(sp)	| buf,
	move.l d3,-(sp)	| blk,
	move.l a4,-(sp)	| tree,
	jsr _node_read	|
	tst.l d0	| <retval>
	jne .L229	|
	cmp.w (20,a6),d2	|, level
	jne .L230	|
	clr.l d0	| <retval>
	jra .L229	|
	.align	2
	.globl	_bfs_btree_key_compare_be32
_bfs_btree_key_compare_be32:
	subq.l #4,sp	|,
	move.l (8,sp),a1	| a, a
	lea (0,sp),a0	|,, tmp39
	move.l (a1)+,(a0)+	|,
	move.l (sp),d1	| v, v.0_6
	move.l (12,sp),a1	| b, b
	lea (0,sp),a0	|,, tmp42
	move.l (a1)+,(a0)+	|,
	move.l (sp),d0	| v, v.0_7
	cmp.l d1,d0	| v.0_6, v.0_7
	jhi .L274	|
	scs d0	| tmp44
	extb.l d0	| tmp43
	neg.l d0	| <retval>
	addq.l #4,sp	|,
	rts
.L274:
	moveq #-1,d0	|, <retval>
	addq.l #4,sp	|,
	rts
	.align	2
	.globl	_bfs_btree_key_hint_cache_reset
_bfs_btree_key_hint_cache_reset:
	move.l (4,sp),a1	| cache, cache
	move.l a1,d0	|,
	jeq .L277	|
	move.l a1,a0	| cache, cache
	moveq #39,d0	|, tmp47
.L279:
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	dbra d0,.L279	| tmp47,
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	lea (4,a1),a0	|, cache, ivtmp.384
	moveq #63,d0	|, tmp49
.L280:
	clr.l (a0)+	| MEM[base: _27, offset: 0B]
	lea (24,a0),a0	|, ivtmp.384
	dbra d0,.L280	| tmp49,
	lea (1800,a1),a0	|, cache, ivtmp.376
	moveq #63,d0	|, tmp48
.L281:
	clr.l (a0)+	| MEM[base: _15, offset: 0B]
	addq.l #8,a0	|, ivtmp.376
	dbra d0,.L281	| tmp48,
	clr.l (2572,a1)	| cache_6(D)->leaf_root
.L277:
	rts
	.align	2
	.globl	_bfs_btree_init
_bfs_btree_init:
	link.w a5,#-24	|,
	movem.l a6/a4/a3/a2/d7/d6/d5/d4/d3/d2,-(sp)	|,
	lea (8,a5),a0	|,, tmp96
	move.l (a0)+,a2	| tree, tree
	move.l (a0)+,a3	| bio, bio
	move.l (a0)+,a6	| alloc, alloc
	move.l (a0)+,a1	| ops, ops
	move.l (a0)+,d2	| root, root
	move.l (a0),d4	| txn_id, txn_id
	move.l (4,a0),d5	| txn_id,
	move.l a2,d0	|,
	jeq .L293	|
	move.l a3,d0	|,
	jeq .L293	|
	move.l (a3),(-24,a5)	| bio_6(D)->ops, %sfp
	jeq .L293	|
	move.l (-24,a5),a0	| %sfp,
	tst.l (a0)	| _7->read_block
	jeq .L293	|
	move.l a6,d0	|,
	jeq .L293	|
	tst.l (a6)	| alloc_9(D)->alloc
	jeq .L293	|
	tst.l (4,a6)	| alloc_9(D)->dealloc
	jeq .L293	|
	move.l a1,d0	|,
	jeq .L293	|
	move.l (a1),d7	| ops_12(D)->key_compare, _13
	jeq .L293	|
	move.l (4,a1),d1	| ops_12(D)->key_size, _14
	move.l d1,d0	| _14, tmp71
	subq.l #1,d0	|, tmp71
	cmp.l #511,d0	|, tmp71
	jhi .L293	|
	move.l (8,a1),d3	| ops_12(D)->val_size, _15
	jeq .L293	|
	move.l (4,a3),a4	| bio_6(D)->block_size, _16
	lea (-1024,a4),a0	|, _16, tmp73
	cmp.l #64512,a0	|, tmp73
	jhi .L293	|
	move.l a4,d0	| _16, tmp75
	subq.l #1,d0	|, tmp75
	move.l a4,d6	| _16,
	and.l d6,d0	|, tmp76
	jne .L293	|
	move.l (8,a3),d6	| bio_6(D)->block_count, _17
	jeq .L293	|
	tst.l d2	| root
	jeq .L295	|
	cmp.l d6,d2	| _17, root
	jcc .L293	|
.L295:
	move.l a3,(a2)	| bio, tree_4(D)->bio
	move.l a6,(4,a2)	| alloc, tree_4(D)->alloc
	move.l a1,(8,a2)	| ops, tree_4(D)->ops
	move.l d2,(12,a2)	| root, tree_4(D)->root
	clr.l (16,a2)	| tree_4(D)->height
	clr.l (20,a2)	| tree_4(D)->txn_id_ptr
	move.l d4,(24,a2)	| txn_id, tree_4(D)->txn_id_fallback
	move.l d5,(28,a2)	|, tree_4(D)->txn_id_fallback
	lea (32,a2),a0	|, tree, tmp77
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (56,a2)	| tree_4(D)->free_sink_err
	clr.l (60,a2)	| tree_4(D)->generation
	clr.l (64,a2)	| tree_4(D)->hint_leaf
	clr.l (68,a2)	| tree_4(D)->hint_root
	clr.l (72,a2)	| tree_4(D)->hint_generation
	clr.l (76,a2)	| tree_4(D)->hint_mutation_epoch
	clr.l (80,a2)	| tree_4(D)->hint_mutation_epoch
	clr.l (84,a2)	| tree_4(D)->key_hint_cache
	move.l d1,d4	| _14, tmp79
	add.l d3,d4	| _15, tmp79
	moveq #-28,d0	|, tmp78
	add.l a4,d0	| _16, tmp78
	divu.l d4,d0	| tmp79, tmp81
	moveq #2,d4	|,
	cmp.l d0,d4	| tmp81,
	jcc .L293	|
	move.l d1,d4	| _14, tmp84
	addq.l #4,d4	|, tmp84
	moveq #-32,d0	|, tmp83
	add.l a4,d0	| _16, tmp83
	divu.l d4,d0	| tmp84, tmp86
	moveq #2,d4	|,
	cmp.l d0,d4	| tmp86,
	jcc .L293	|
	clr.l d4	| <retval>
	tst.l d2	| root
	jeq .L290	|
	tst.l (12,a1)	| ops_12(D)->cache_key_order
	jeq .L296	|
	lea (-20,a5),a0	|,, tmp97
	move.l d7,(a0)+	| _13, MEM[(struct bfs_node_validation *)&validation]
	move.l d1,(a0)+	| _14, MEM[(struct bfs_node_validation *)&validation + 4B]
	move.l d3,(a0)+	| _15, MEM[(struct bfs_node_validation *)&validation + 8B]
	move.l a4,(a0)+	| _16, MEM[(struct bfs_node_validation *)&validation + 12B]
	move.l d6,(a0)	| _17, MEM[(struct bfs_node_validation *)&validation + 16B]
	move.l (-24,a5),a1	| %sfp,
	move.l (68,a1),a0	| _7->peek_valid_node, _61
	move.l a0,d0	|,
	jeq .L296	|
	pea (-20,a5)	|
	move.l d2,-(sp)	| root,
	move.l a3,-(sp)	| bio,
	jsr (a0)	| _61
	lea (12,sp),sp	|,
	tst.l d0	| iftmp.2_62
	jeq .L335	|
	move.l d0,a0	| iftmp.2_62,
	clr.l d0	| tmp89
	move.w (20,a0),d0	|, tmp89
	addq.l #1,d0	|, tmp89
	move.l d0,(16,a2)	| tmp89, tree_4(D)->height
.L290:
	move.l d4,d0	| <retval>,
	movem.l (-64,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L293:
	moveq #-8,d1	|,
	move.l d1,d0	|,
	movem.l (-64,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L335:
	move.l (a2),a3	| MEM[(struct bfs_bio_t * *)tree_4(D)], bio
	move.l (4,a3),a4	| pretmp_97->block_size, _16
.L296:
	move.l a4,-(sp)	| _16,
	move.l a3,-(sp)	| bio,
	jsr _bfs_bio_alloc_buffer	|
	move.l d0,a3	|, _66
	addq.l #8,sp	|,
	tst.l d0	| _66
	jeq .L302	|
	move.l d0,-(sp)	| _66,
	move.l d2,-(sp)	| root,
	move.l a2,-(sp)	| tree,
	jsr _node_read	|
	move.l d0,d2	|,
	lea (12,sp),sp	|,
	jne .L332	|
	clr.l d0	| tmp93
	move.w (20,a3),d0	|, tmp93
	addq.l #1,d0	|, tmp93
	move.l d0,(16,a2)	| tmp93, tree_4(D)->height
.L332:
	move.l a3,-(sp)	| _66,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_4(D)],
	jsr _bfs_bio_free_buffer	|
	move.l d2,d0	|,
	movem.l (-64,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L302:
	moveq #-2,d4	|, <retval>
	jra .L290	|
	.align	2
	.globl	_bfs_btree_lower_bound
_bfs_btree_lower_bound:
	link.w a5,#-1048	|,
	movem.l a6/a4/a3/a2/d7/d6/d5/d4/d3/d2,-(sp)	|,
	move.l (8,a5),a2	| tree, tree
	move.l a2,d0	|,
	jeq .L353	|
	move.l (a2),a0	| tree_10(D)->bio, _12
	move.l a0,d0	|,
	jeq .L353	|
	tst.l (8,a2)	| tree_10(D)->ops
	jeq .L353	|
	tst.l (12,a5)	| key
	jeq .L353	|
	tst.l (16,a5)	| key_out
	jeq .L353	|
	move.l (12,a2),d0	| tree_10(D)->root, _16
	jeq .L354	|
	moveq #-3,d6	|,
	cmp.l (8,a0),d0	| _12->block_count, _16
	jcc .L336	|
	move.l (16,a2),d0	| MEM[(const struct bfs_btree_t *)tree_10(D)].height, tmp93
	subq.l #1,d0	|, tmp93
	moveq #31,d1	|,
	cmp.l d0,d1	| tmp93,
	jcs .L336	|
	move.l (4,a0),-(sp)	| _12->block_size,
	move.l a0,-(sp)	| _12,
	jsr _bfs_bio_alloc_buffer	|
	move.l d0,d5	|, _55
	addq.l #8,sp	|,
	jeq .L363	|
	move.l (12,a2),d0	| tree_10(D)->root, blk
	move.w (18,a2),d2	| tree_10(D)->height, expected_level
	subq.w #1,d2	|, expected_level
	move.l a5,d4	|, tmp143
	add.l #-1032,d4	|, tmp143
	move.l d4,a0	| tmp143, tmp95
	moveq #15,d1	|, tmp96
.L339:
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	dbra d1,.L339	| tmp96,
	clr.l (a0)+	|
	clr.l (a0)+	|
	moveq #32,d3	|, ivtmp_89
	lea _node_view,a4	|, tmp141
	lea (8,a2),a3	|, tree, _49
.L347:
	pea (-1044,a5)	|
	move.l d4,-(sp)	| tmp143,
	move.w d2,-(sp)	| expected_level,
	clr.w -(sp)	|
	move.l d5,-(sp)	| _55,
	move.l d0,-(sp)	| blk,
	move.l a2,-(sp)	| tree,
	jsr (a4)	| tmp141
	move.l d0,d6	|,
	lea (24,sp),sp	|,
	jne .L364	|
	pea (-1040,a5)	|
	move.l (12,a5),-(sp)	| key,
	move.l (-1044,a5),-(sp)	| node,
	move.l a3,-(sp)	| _49,
	jsr _node_search.isra.12	|
	move.l (-1044,a5),a0	| node, node.14_29
	lea (16,sp),sp	|,
	tst.w (20,a0)	|
	jeq .L365	|
	tst.w d2	| expected_level
	jeq .L366	|
	tst.l (-1040,a5)	| found
	jeq .L346	|
	addq.l #1,d0	|, idx
.L346:
	move.l d4,-(sp)	| tmp143,
	move.l d0,-(sp)	| idx,
	move.l a0,-(sp)	| node.14_29,
	move.l a3,-(sp)	| _49,
	move.l d0,(-1048,a5)	|,
	jsr _child_bounds.isra.13	|
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_10(D)], _63
	move.l (8,a2),a1	| MEM[(const struct bfs_btree_t *)tree_10(D)].ops, MEM[(const struct bfs_btree_t *)tree_10(D)].ops
	move.l (4,a1),d0	| _66->key_size,
	moveq #-32,d1	|, tmp126
	add.l (4,a0),d1	| _63->block_size, tmp126
	move.l d0,d6	|, tmp127
	addq.l #4,d6	|, tmp127
	divu.l d6,d1	| tmp127, tmp129
	muls.l d0,d1	|, tmp131
	move.l (-1048,a5),d0	|,
	move.l d1,a6	| tmp131,
	lsl.l #2,d0	|, tmp133
	lea (28,a6,d0.l),a1	|, tmp134
	add.l (-1044,a5),a1	| node, tmp136
	move.l a5,d0	|, tmp137
	add.l #-1036,d0	|, tmp137
	move.l d0,a6	| tmp137,
	move.l (a1)+,(a6)+	|,
	move.l (-1036,a5),d0	| v, blk
	subq.w #1,d2	|, expected_level
	subq.w #1,d3	|, ivtmp_89
	lea (16,sp),sp	|,
	jne .L347	|
	moveq #-3,d6	|,
	move.l d5,-(sp)	| _55,
	move.l a0,-(sp)	| _63,
	jsr _bfs_bio_free_buffer	|
.L336:
	move.l d6,d0	|,
	movem.l (-1088,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L364:
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_10(D)], _63
	move.l d5,-(sp)	| _55,
	move.l a0,-(sp)	| _63,
	jsr _bfs_bio_free_buffer	|
	jra .L336	|
.L365:
	cmp.l (16,a0),d0	|, idx
	jcs .L367	|
	tst.l (-4,a5)	| bounds.have_upper
	jne .L344	|
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_10(D)], _63
	moveq #-5,d6	|,
	move.l d5,-(sp)	| _55,
	move.l a0,-(sp)	| _63,
	jsr _bfs_bio_free_buffer	|
	jra .L336	|
.L366:
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_10(D)], _63
	moveq #-3,d6	|,
	move.l d5,-(sp)	| _55,
	move.l a0,-(sp)	| _63,
	jsr _bfs_bio_free_buffer	|
	jra .L336	|
.L367:
	move.l (8,a2),a1	| tree_10(D)->ops, tree_10(D)->ops
	move.l (4,a1),d1	| _43->key_size, _44
	muls.l d1,d0	| _44, tmp104
	move.l d1,-(sp)	| _44,
	pea (28,a0,d0.l)	|
	move.l (16,a5),-(sp)	| key_out,
	jsr _memcpy	|
	lea (12,sp),sp	|,
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_10(D)], _63
	move.l d5,-(sp)	| _55,
	move.l a0,-(sp)	| _63,
	jsr _bfs_bio_free_buffer	|
	jra .L336	|
.L344:
	move.l (8,a2),a0	| tree_10(D)->ops, tree_10(D)->ops
	move.l (4,a0),-(sp)	| _40->key_size,
	pea (-520,a5)	|
	move.l (16,a5),-(sp)	| key_out,
	jsr _memcpy	|
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_10(D)], _63
	lea (12,sp),sp	|,
	moveq #-9,d6	|,
	move.l d5,-(sp)	| _55,
	move.l a0,-(sp)	| _63,
	jsr _bfs_bio_free_buffer	|
	jra .L336	|
.L354:
	moveq #-5,d1	|,
	move.l d1,d0	|,
	movem.l (-1088,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L353:
	moveq #-8,d1	|,
	move.l d1,d0	|,
	movem.l (-1088,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L363:
	moveq #-2,d1	|,
	move.l d1,d0	|,
	movem.l (-1088,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
	.align	2
_owned_leaf_in_place:
	link.w a5,#-28	|,
	movem.l a6/a4/a3/a2/d7/d6/d5/d4/d3/d2,-(sp)	|,
	move.l (8,a5),a4	| tree, tree
	move.l (12,a5),d4	| key, key
	move.l (16,a5),d3	| bounds, bounds
	move.l (8,a4),a3	| tree_9(D)->ops, _10
	tst.l (12,a3)	| _10->cache_key_order
	jeq .L369	|
	move.l (a4),a0	| tree_9(D)->bio, _77
	move.l a0,d0	|,
	jeq .L369	|
	move.l (a0),a2	| MEM[(const struct bfs_bio_t *)_12].ops, _69
	move.l a2,d0	|,
	jeq .L369	|
	tst.l (44,a2)	| _69->defer_node_block
	jeq .L369	|
	tst.l (48,a2)	| _69->flush_deferred
	jeq .L369	|
	tst.l (52,a2)	| _69->discard_deferred
	jeq .L369	|
	tst.l (72,a2)	| _69->modify_dirty_node
	jeq .L369	|
	move.l (16,a4),d2	| tree_9(D)->height, _15
	move.l d2,d0	| _15, tmp93
	subq.l #1,d0	|, tmp93
	moveq #31,d1	|,
	cmp.l d0,d1	| tmp93,
	jcs .L369	|
	move.l (4,a3),d6	| _10->key_size, _45
	move.l (8,a3),d5	| _10->val_size, _42
	move.l (4,a0),d1	| _12->block_size, _13
	lea (-20,a5),a1	|,, tmp126
	move.l (8,a0),d0	| _12->block_count, _52
	move.l (a3),(a1)+	| _10->key_compare, MEM[(struct bfs_node_validation *)&validation]
	move.l d6,(a1)+	| _45, MEM[(struct bfs_node_validation *)&validation + 4B]
	move.l d5,(a1)+	| _42, MEM[(struct bfs_node_validation *)&validation + 8B]
	move.l d1,(a1)+	| _13, MEM[(struct bfs_node_validation *)&validation + 12B]
	move.l d0,(a1)	| _52, MEM[(struct bfs_node_validation *)&validation + 16B]
	move.l (12,a4),d5	| tree_9(D)->root,
	subq.w #1,d2	|, level
	jeq .L372	|
	tst.l d5	|
	jeq .L369	|
	cmp.l d5,d0	|, _52
	jls .L369	|
	moveq #-20,d6	|, tmp119
	move.l (68,a2),a1	| _69->peek_valid_node, _19
	add.l a5,d6	|, tmp119
	lea (8,a4),a3	|, tree, _50
	move.l a1,d0	|,
	jeq .L369	|
.L411:
	move.l d6,-(sp)	| tmp119,
	move.l d5,-(sp)	|,
	move.l a0,-(sp)	| _77,
	jsr (a1)	| _19
	move.l d0,a2	|, iftmp.2_55
	lea (12,sp),sp	|,
	tst.l d0	| iftmp.2_55
	jeq .L369	|
	cmp.w (20,a2),d2	|, level
	jne .L369	|
	move.l d3,-(sp)	| bounds,
	move.l d0,-(sp)	| iftmp.2_55,
	move.l a3,-(sp)	| _50,
	jsr (_node_within_bounds.isra.6)	|
	lea (12,sp),sp	|,
	tst.l d0	|
	jeq .L369	|
	pea (-28,a5)	|
	move.l d4,-(sp)	| key,
	move.l a2,-(sp)	| iftmp.2_55,
	move.l a3,-(sp)	| _50,
	jsr _node_search.isra.12	|
	move.l d0,d7	|, idx
	lea (16,sp),sp	|,
	tst.l (-28,a5)	| found
	jeq .L375	|
	addq.l #1,d7	|, idx
.L375:
	move.l d3,-(sp)	| bounds,
	move.l d7,-(sp)	| idx,
	move.l a2,-(sp)	| iftmp.2_55,
	move.l a3,-(sp)	| _50,
	jsr _child_bounds.isra.13	|
	move.l (a4),a0	| MEM[(struct bfs_bio_t * *)tree_9(D)], _77
	move.l (8,a4),a1	| MEM[(const struct bfs_btree_t *)tree_9(D)].ops, MEM[(const struct bfs_btree_t *)tree_9(D)].ops
	move.l (4,a1),d0	| _80->key_size, _81
	moveq #-32,d1	|, tmp101
	add.l (4,a0),d1	| _77->block_size, tmp101
	move.l d0,d5	| _81, tmp102
	addq.l #4,d5	|, tmp102
	divu.l d5,d1	| tmp102, tmp104
	muls.l d0,d1	| _81, tmp106
	move.l d1,a6	| tmp106,
	lsl.l #2,d7	|, tmp108
	lea (28,a6,d7.l),a1	|, tmp109
	add.l a1,a2	| tmp109, tmp111
	lea (-24,a5),a1	|,, tmp112
	move.l (a2)+,(a1)+	|,
	move.l (-24,a5),d5	| v,
	subq.w #1,d2	|, level
	lea (16,sp),sp	|,
	jeq .L372	|
	tst.l d5	|
	jeq .L369	|
	cmp.l (8,a0),d5	| _77->block_count,
	jcc .L369	|
	move.l (a0),a1	| _77->ops, _53
	move.l a1,d0	|,
	jeq .L369	|
	move.l (68,a1),a1	| _53->peek_valid_node, _19
	move.l a1,d0	|,
	jne .L411	|
.L369:
	clr.l d0	| <retval>
	movem.l (-68,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L372:
	tst.l d5	|
	jeq .L369	|
	move.l (a4),a0	| tree_9(D)->bio, tree_9(D)->bio
	cmp.l (8,a0),d5	| _35->block_count,
	jcc .L369	|
	move.l d5,-(sp)	|,
	move.l a4,-(sp)	| tree,
	jsr _owned_contains	|
	addq.l #8,sp	|,
	tst.l d0	|
	jeq .L369	|
	move.l (a4),a1	| tree_9(D)->bio, _39
	move.l a1,d0	|,
	jeq .L369	|
	move.l (a1),a0	| MEM[(const struct bfs_bio_t *)_39].ops, _95
	move.l a0,d0	|,
	jeq .L369	|
	tst.l (44,a0)	| _95->defer_node_block
	jeq .L369	|
	tst.l (48,a0)	| _95->flush_deferred
	jeq .L369	|
	tst.l (52,a0)	| _95->discard_deferred
	jeq .L369	|
	move.l (72,a0),a0	| _95->modify_dirty_node, _101
	move.l a0,d0	|,
	jeq .L369	|
	pea (-20,a5)	|
	move.l d5,-(sp)	|,
	move.l a1,-(sp)	| _39,
	jsr (a0)	| _101
	move.l d0,a2	|, iftmp.56_93
	lea (12,sp),sp	|,
	tst.l d0	| iftmp.56_93
	jeq .L369	|
	tst.w (20,a2)	|
	jne .L369	|
	move.l d3,-(sp)	| bounds,
	move.l d0,-(sp)	| iftmp.56_93,
	pea (8,a4)	|
	jsr (_node_within_bounds.isra.6)	|
	tst.l d0	|
	jeq .L369	|
	move.l (20,a4),a0	| MEM[(const uint64_t * *)tree_9(D) + 20B], _49
	move.l (8,a2),d2	|, _46
	move.l (12,a2),d3	|,
	move.l a0,d0	|,
	jeq .L380	|
	move.l (a0),d0	| *_49, iftmp.35_59
	move.l (4,a0),d1	| *_49,
.L381:
	move.l d2,d4	| _46,
	move.l d3,d5	|,
	sub.l d1,d5	| iftmp.35_59,
	subx.l d0,d4	| iftmp.35_59,
	jne .L369	|
	move.l a2,d0	| iftmp.56_93, <retval>
	movem.l (-68,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L380:
	move.l (24,a4),d0	| MEM[(const uint64_t *)tree_9(D) + 24B], iftmp.35_59
	move.l (28,a4),d1	| MEM[(const uint64_t *)tree_9(D) + 24B],
	jra .L381	|
	.align	2
_key_hint_remember:
	subq.l #4,sp	|,
	movem.l a3/a2/d6/d5/d4/d3/d2,-(sp)	|,
	lea (36,sp),a0	|,, tmp90
	move.l (a0)+,a1	| tree, tree
	move.l (a0)+,d0	| key, key
	move.l (a0)+,d5	| leaf, leaf
	move.l (a0)+,d4	| index, index
	move.l (a0),d2	| mutation_epoch, mutation_epoch
	move.l (4,a0),d3	| mutation_epoch,
	move.l (84,a1),a0	| MEM[(const struct bfs_btree_t *)tree_3(D)].key_hint_cache, _6
	move.l a0,d1	|,
	jeq .L428	|
	move.l (8,a1),a2	| MEM[(const struct bfs_btree_t *)tree_3(D)].ops, _4
	tst.l (12,a2)	| _4->cache_key_order
	jeq .L428	|
	cmp.l #_bfs_btree_key_compare_be32,(a2)	|, _4->key_compare
	jeq .L436	|
.L428:
	movem.l (sp)+,d2/d3/d4/d5/d6/a2/a3	|,
	addq.l #4,sp	|,
	rts
.L436:
	moveq #4,d1	|,
	cmp.l (4,a2),d1	| _4->key_size,
	jne .L428	|
	move.l (a1),a2	| MEM[(const struct bfs_btree_t *)tree_3(D)].bio, MEM[(const struct bfs_btree_t *)tree_3(D)].bio
	move.l (a2),a2	| _21->ops, _21->ops
	tst.l (68,a2)	| _22->peek_valid_node
	jeq .L428	|
	lea (28,sp),a3	|,, tmp54
	move.l d0,a2	| key, key
	move.l (a2)+,(a3)+	|,
	move.l (28,sp),d6	| v, v.0_27
	move.l d6,d1	| v.0_27, tmp55
	muls.l #-1640531535,d1	|, tmp55
	moveq #26,d0	|,
	lsr.l d0,d1	|, _26
	move.l d1,d0	| _26, tmp57
	lsl.l #3,d0	|, tmp57
	sub.l d1,d0	| _26, tmp58
	lsl.l #2,d0	|, tmp59
	move.l d6,(a0,d0.l)	| v.0_27, MEM[(struct bfs_btree_key_hint_t *)_6].slots[_26].key
	add.l d0,a0	| tmp59, tmp64
	move.l d5,(4,a0)	| leaf, MEM[(struct bfs_btree_key_hint_t *)_6].slots[_26].leaf
	move.l d4,(8,a0)	| index, MEM[(struct bfs_btree_key_hint_t *)_6].slots[_26].index
	move.l (60,a1),(12,a0)	| tree_3(D)->generation, MEM[(struct bfs_btree_key_hint_t *)_6].slots[_26].generation
	move.l (12,a1),(16,a0)	| tree_3(D)->root, MEM[(struct bfs_btree_key_hint_t *)_6].slots[_26].root
	move.l d2,(20,a0)	| mutation_epoch, MEM[(struct bfs_btree_key_hint_t *)_6].slots[_26].mutation_epoch
	move.l d3,(24,a0)	|, MEM[(struct bfs_btree_key_hint_t *)_6].slots[_26].mutation_epoch
	movem.l (sp)+,d2/d3/d4/d5/d6/a2/a3	|,
	addq.l #4,sp	|,
	rts
	.align	2
_leaf_hint_remember:
	subq.l #4,sp	|,
	movem.l a5/a4/a3/a2/d6/d5/d4/d3/d2,-(sp)	|,
	lea (44,sp),a0	|,, tmp167
	move.l (a0)+,a1	| tree, tree
	move.l (a0)+,d0	| blk, blk
	move.l (a0)+,a3	| leaf, leaf
	move.l (a0),d2	| mutation_epoch, mutation_epoch
	move.l (4,a0),d3	| mutation_epoch,
	move.l (84,a1),a0	| MEM[(const struct bfs_btree_t *)tree_9(D)].key_hint_cache, _43
	move.l a0,d1	|,
	jeq .L437	|
	move.l (8,a1),a2	| MEM[(const struct bfs_btree_t *)tree_9(D)].ops, _34
	tst.l (12,a2)	| _34->cache_key_order
	jeq .L437	|
	cmp.l #_bfs_btree_key_compare_be32,(a2)	|, _34->key_compare
	jeq .L482	|
.L437:
	movem.l (sp)+,d2/d3/d4/d5/d6/a2/a3/a4/a5	|,
	addq.l #4,sp	|,
	rts
.L482:
	moveq #4,d1	|,
	cmp.l (4,a2),d1	| _34->key_size,
	jne .L437	|
	move.l (a1),a4	| MEM[(const struct bfs_btree_t *)tree_9(D)].bio, _26
	move.l (a4),a5	| _26->ops, _26->ops
	tst.l (68,a5)	| _10->peek_valid_node
	jeq .L437	|
	cmp.l (2560,a0),a1	| _43->leaf_owner, tree
	jne .L437	|
	move.l (2572,a0),d1	| _43->leaf_root,
	cmp.l (12,a1),d1	| tree_9(D)->root,
	jne .L437	|
	move.l (2564,a0),a5	| _43->leaf_bio, _14
	cmp.l a5,a4	| _14, _26
	jne .L437	|
	cmp.l (2568,a0),a2	| _43->leaf_ops, _34
	jne .L437	|
	move.l (2576,a0),d1	| _43->leaf_generation,
	cmp.l (60,a1),d1	| tree_9(D)->generation,
	jne .L437	|
	move.l (2580,a0),d4	| _43->leaf_mutation_epoch,
	move.l (2584,a0),d5	| _43->leaf_mutation_epoch,
	sub.l d3,d5	| mutation_epoch,
	subx.l d2,d4	| mutation_epoch,
	jne .L437	|
	move.w (2588,a0),d4	| _43->leaf_count, _22
	cmp.w #64,d4	|, _22
	jhi .L437	|
	cmp.l (8,a5),d0	| MEM[(struct bfs_bio_t *)_14].block_count, blk
	jcc .L437	|
	tst.w (20,a3)	|
	jne .L437	|
	move.l (16,a3),a2	|, _58
	move.l a2,d1	|,
	jeq .L437	|
	lea (36,sp),a1	|,, tmp104
	lea (28,a3),a4	|, leaf, tmp103
	move.l (a4)+,(a1)+	|,
	lea (24,a2.l*4),a2	|, tmp108
	move.l (36,sp),a1	| v, v.0_64
	add.l a2,a3	| tmp108, tmp110
	lea (36,sp),a2	|,, tmp111
	move.l (a3)+,(a2)+	|,
	move.l (36,sp),a2	| v, v.0_59
	cmp.l a2,a1	| v.0_59, v.0_64
	jhi .L437	|
	clr.l d2	| i
	move.w d4,d2	| _22, i
	move.l d2,d5	| i, hi
	clr.l d1	| pos
.L442:
	cmp.l d1,d5	| pos, hi
	jls .L445	|
.L446:
	move.l d5,d3	| hi, tmp112
	sub.l d1,d3	| pos, tmp112
	lsr.l #1,d3	|, tmp113
	add.l d1,d3	| pos, mid
	move.l d3,d6	| mid, tmp115
	add.l d3,d6	| mid, tmp115
	add.l d3,d6	| mid, tmp115
	cmp.l (1792,a0,d6.l*4),a1	| MEM[(const struct bfs_btree_key_hint_cache_t *)_43].leaf_slots[mid_72].first_key, v.0_64
	jhi .L483	|
	move.l d3,d5	| mid, hi
	cmp.l d3,d1	| hi, pos
	jcs .L446	|
.L445:
	cmp.l d2,d1	| i, pos
	jcc .L447	|
	move.l d1,d3	| pos, tmp162
	add.l d1,d3	| pos, tmp162
	add.l d1,d3	| pos, tmp161
	move.l d3,d5	| tmp161, tmp123
	lsl.l #2,d5	|, tmp123
	cmp.l (1792,a0,d5.l),a1	| _43->leaf_slots[lo_75].first_key, v.0_64
	jeq .L484	|
	cmp.w #64,d4	|, _22
	jeq .L458	|
.L459:
	move.l d2,d5	| i, tmp150
	add.l d2,d5	| i, tmp150
	add.l d2,d5	| i, tmp151
	lea (1780,d5.l*4),a3	|, tmp153
	move.l d1,d5	| pos, tmp164
	not.l d5	| tmp164
	add.l a0,a3	| _43, ivtmp.415
	add.l d2,d5	| i, tmp163
	subq.l #1,d2	|, tmp165
	cmp.l d1,d2	| pos, tmp165
	jcs .L471	|
	addq.l #1,d1	|, pos
	jeq .L471	|
.L456:
	move.l a3,a5	| ivtmp.415, _78
	lea (12,a3),a4	|, ivtmp.415, tmp155
	move.l (a5)+,(a4)+	|,
	move.l (a5)+,(a4)+	|,
	move.l (a5)+,(a4)+	|,
	lea (-12,a3),a3	|, ivtmp.415
	dbra d5,.L456	| tmp163,
	clr.w d5	| tmp163
	subq.l #1,d5	| tmp163
	jcc .L456	|
.L455:
	lea (1792,d3.l*4),a3	|, tmp160
	add.l a0,a3	| _43, _28
	move.l a1,(a3)+	| v.0_64, MEM[(struct  *)_28]
	move.l a2,(a3)+	| v.0_59, MEM[(struct  *)_28 + 4B]
	move.l d0,(a3)	| blk, MEM[(struct  *)_28 + 8B]
	addq.w #1,d4	|, _22
	move.w d4,(2588,a0)	| _22, _43->leaf_count
	movem.l (sp)+,d2/d3/d4/d5/d6/a2/a3/a4/a5	|,
	addq.l #4,sp	|,
	rts
.L471:
	clr.l d5	| tmp163
	jra .L456	|
.L458:
	move.w (2590,a0),d3	| _43->leaf_next_victim, _36
	moveq #63,d1	|, victim
	and.l d3,d1	| _36, victim
	move.l d1,d2	| victim, tmp133
	add.l d1,d2	| victim, tmp133
	add.l d1,d2	| victim, tmp134
	lea (1792,d2.l*4),a3	|, tmp136
	moveq #63,d2	|,
	sub.l d1,d2	| victim,
	add.l a0,a3	| _43, ivtmp.426
	move.l d2,d1	|, tmp166
.L449:
	lea (12,a3),a3	|, ivtmp.426
	dbra d1,.L450	| tmp166,
	clr.w d1	| tmp166
	subq.l #1,d1	| tmp166
	jcc .L450	|
	move.w #63,(2588,a0)	|, _43->leaf_count
	moveq #63,d4	|, hi
	clr.l d1	| pos
.L454:
	move.l d4,d2	| hi, tmp139
	sub.l d1,d2	| pos, tmp139
	lsr.l #1,d2	|, tmp140
	add.l d1,d2	| pos, mid
	move.l d2,d5	| mid, tmp142
	add.l d2,d5	| mid, tmp142
	add.l d2,d5	| mid, tmp143
	cmp.l (1792,a0,d5.l*4),a1	| MEM[(const struct bfs_btree_key_hint_cache_t *)_43].leaf_slots[mid_82].first_key, v.0_64
	jhi .L485	|
	move.l d2,d4	| mid, hi
	cmp.l d2,d1	| hi, pos
	jcs .L454	|
.L453:
	addq.w #1,d3	|, tmp148
	and.w #63,d3	|, tmp148
	move.w d3,(2590,a0)	| tmp148, _43->leaf_next_victim
	moveq #62,d2	|,
	cmp.l d1,d2	| pos,
	jcs .L460	|
	moveq #63,d2	|, i
	move.w d2,d4	| i, _22
	moveq #3,d3	|, tmp161
	muls.l d1,d3	| pos, tmp161
	jra .L459	|
.L450:
	move.l a3,a5	| ivtmp.426, _131
	lea (-12,a3),a4	|, ivtmp.426, tmp138
	move.l (a5)+,(a4)+	|,
	move.l (a5)+,(a4)+	|,
	move.l (a5)+,(a4)+	|,
	jra .L449	|
.L484:
	lea (1792,a0,d5.l),a0	|, _87
	move.l a1,(a0)+	| v.0_64, MEM[(struct  *)_87]
	move.l a2,(a0)+	| v.0_59, MEM[(struct  *)_87 + 4B]
	move.l d0,(a0)	| blk, MEM[(struct  *)_87 + 8B]
	movem.l (sp)+,d2/d3/d4/d5/d6/a2/a3/a4/a5	|,
	addq.l #4,sp	|,
	rts
.L447:
	cmp.w #64,d4	|, _22
	jeq .L458	|
.L478:
	moveq #3,d3	|, tmp161
	muls.l d1,d3	| pos, tmp161
	jra .L455	|
.L483:
	move.l d3,d1	| mid, pos
	addq.l #1,d1	|, pos
	jra .L442	|
.L485:
	move.l d2,d1	| mid, pos
	addq.l #1,d1	|, pos
	cmp.l d1,d4	| pos, hi
	jhi .L454	|
	jra .L453	|
.L460:
	moveq #63,d4	|, _22
	jra .L478	|
	.align	2
	.globl	_bfs_btree_search
_bfs_btree_search:
	lea (-1068,sp),sp	|,
	movem.l a6/a5/a4/a3/a2/d7/d6/d5/d4/d3/d2,-(sp)	|,
	move.l (1116,sp),a2	| tree, tree
	move.l a2,d0	|,
	jeq .L573	|
	move.l (a2),a0	| tree_18(D)->bio, _20
	move.l a0,d0	|,
	jeq .L573	|
	tst.l (8,a2)	| tree_18(D)->ops
	jeq .L573	|
	tst.l (1120,sp)	| key
	jeq .L573	|
	tst.l (1124,sp)	| val_out
	jeq .L573	|
	move.l (12,a2),d0	| tree_18(D)->root, _24
	jeq .L574	|
	moveq #-3,d2	|,
	cmp.l (8,a0),d0	| _20->block_count, _24
	jcc .L486	|
	move.l (16,a2),d0	| MEM[(const struct bfs_btree_t *)tree_18(D)].height, tmp300
	subq.l #1,d0	|, tmp300
	moveq #31,d1	|,
	cmp.l d0,d1	| tmp300,
	jcs .L486	|
	move.l (a0),a1	| _20->ops, _97
	move.l a1,d0	|,
	jeq .L492	|
	move.l (76,a1),a1	| _97->mutation_epoch, _98
	move.l a1,d0	|,
	jeq .L492	|
	move.l a0,-(sp)	| _20,
	jsr (a1)	| _98
	move.l d0,(60,sp)	|, %sfp
	move.l d1,(64,sp)	|, %sfp
	addq.l #4,sp	|,
	moveq #-1,d2	|,
	moveq #-1,d3	|,
	sub.l d3,d1	|,
	subx.l d2,d0	|,
	jeq .L492	|
	move.l (84,a2),a1	| MEM[(const struct bfs_btree_t *)tree_18(D)].key_hint_cache, prephitmp_193
	move.l a1,d0	|,
	jeq .L567	|
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_t *)tree_18(D)].ops, _137
	tst.l (12,a0)	| _137->cache_key_order
	jeq .L500	|
	cmp.l #_bfs_btree_key_compare_be32,(a0)	|, _137->key_compare
	jeq .L650	|
.L500:
	move.l a1,d0	|,
	jne .L651	|
.L567:
	move.l (a2),a0	|* tree, prephitmp_524
.L560:
	move.l (64,a2),d4	| MEM[(const struct bfs_btree_t *)tree_18(D)].hint_leaf, _309
	jeq .L637	|
	move.l (72,a2),d0	| MEM[(const struct bfs_btree_t *)tree_18(D)].hint_generation,
	cmp.l (60,a2),d0	| MEM[(const struct bfs_btree_t *)tree_18(D)].generation,
	jne .L637	|
	move.l (68,a2),d0	| MEM[(const struct bfs_btree_t *)tree_18(D)].hint_root,
	cmp.l (12,a2),d0	| MEM[(const struct bfs_btree_t *)tree_18(D)].root,
	jne .L637	|
	move.l (76,a2),d2	| MEM[(const struct bfs_btree_t *)tree_18(D)].hint_mutation_epoch, _314
	move.l (80,a2),d3	| MEM[(const struct bfs_btree_t *)tree_18(D)].hint_mutation_epoch,
	move.l (56,sp),d5	| %sfp,
	move.l (60,sp),d6	| %sfp,
	sub.l d3,d6	| _314,
	subx.l d2,d5	| _314,
	jne .L637	|
	move.l (8,a2),a3	| MEM[(const struct bfs_btree_t *)tree_18(D)].ops, _315
	tst.l (12,a3)	| _315->cache_key_order
	jeq .L637	|
	move.l (4,a3),d6	| _315->key_size, _319
	move.l (8,a3),d5	| _315->val_size, _320
	move.l (4,a0),d1	| prephitmp_478->block_size, prephitmp_479
	lea (80,sp),a1	|,, tmp567
	move.l (8,a0),d0	| prephitmp_478->block_count, _322
	move.l (a3),(a1)+	| _315->key_compare, MEM[(struct bfs_node_validation *)&validation]
	move.l d6,(a1)+	| _319, MEM[(struct bfs_node_validation *)&validation + 4B]
	move.l d5,(a1)+	| _320, MEM[(struct bfs_node_validation *)&validation + 8B]
	move.l d1,(a1)+	| prephitmp_479, MEM[(struct bfs_node_validation *)&validation + 12B]
	move.l d0,(a1)	| _322, MEM[(struct bfs_node_validation *)&validation + 16B]
	move.l (a0),a1	| prephitmp_478->ops, _323
	move.l a1,d0	|,
	jeq .L634	|
	move.l (68,a1),a1	| _323->peek_valid_node, _324
	move.l a1,d0	|,
	jeq .L634	|
	moveq #80,d5	|, tmp554
	add.l sp,d5	|, tmp554
	move.l d5,-(sp)	| tmp554,
	move.l d4,-(sp)	| _309,
	move.l a0,-(sp)	| prephitmp_524,
	jsr (a1)	| _324
	move.l d0,a3	|, iftmp.2_325
	lea (12,sp),sp	|,
	tst.l d0	| iftmp.2_325
	jeq .L628	|
	tst.w (20,a3)	|
	jne .L628	|
	move.l (16,a3),d4	|, _328
	jeq .L628	|
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_t *)tree_18(D)].ops, MEM[(const struct bfs_btree_t *)tree_18(D)].ops
	move.l (1120,sp),-(sp)	| key,
	pea (28,a3)	|
	move.l (a0),a0	| _329->key_compare, _329->key_compare
	jsr (a0)	| _329->key_compare
	addq.l #8,sp	|,
	tst.l d0	|
	jgt .L628	|
	subq.l #1,d4	|, tmp464
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_t *)tree_18(D)].ops, _333
	muls.l (4,a0),d4	| _333->key_size, tmp465
	pea (28,a3,d4.l)	|
	move.l (1124,sp),-(sp)	| key,
	move.l (a0),a0	| _333->key_compare, _333->key_compare
	jsr (a0)	| _333->key_compare
	addq.l #8,sp	|,
	tst.l d0	|
	jgt .L628	|
	move.l d5,-(sp)	| tmp554,
	move.l (1124,sp),-(sp)	| key,
	move.l a3,-(sp)	| iftmp.2_325,
	pea (8,a2)	|
	jsr _node_search.isra.12	|
	move.l d0,d4	|, idx
	lea (16,sp),sp	|,
	tst.l (80,sp)	| found
	jne .L652	|
.L628:
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_18(D)], prephitmp_524
.L637:
	move.l (4,a0),d1	|, prephitmp_479
.L634:
	moveq #1,d0	|,
	move.l d0,(64,sp)	|, %sfp
.L506:
	move.l d1,-(sp)	| prephitmp_479,
	move.l a0,-(sp)	| prephitmp_524,
	jsr _bfs_bio_alloc_buffer	|
	move.l d0,(56,sp)	|, %sfp
	addq.l #8,sp	|,
	jeq .L580	|
	move.l (12,a2),d6	| tree_18(D)->root, blk
	move.w (18,a2),d2	| tree_18(D)->height, expected_level
	subq.w #1,d2	|, expected_level
	moveq #80,d5	|, tmp554
	add.l sp,d5	|, tmp554
	move.l d5,a0	| tmp554, tmp490
	moveq #15,d0	|, tmp491
.L542:
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	dbra d0,.L542	| tmp491,
	clr.l (a0)+	|
	clr.l (a0)+	|
	moveq #32,d3	|, ivtmp_578
	lea _node_view,a4	|, tmp556
	lea (8,a2),a3	|, tree, _87
	lea _node_search.isra.12,a5	|, tmp561
	move.l (1120,sp),d7	| key, key
.L543:
	pea (68,sp)	|
	move.l d5,-(sp)	| tmp554,
	move.w d2,-(sp)	| expected_level,
	clr.w -(sp)	|
	move.l (60,sp),-(sp)	| %sfp,
	move.l d6,-(sp)	| blk,
	move.l a2,-(sp)	| tree,
	jsr (a4)	| tmp556
	move.l d0,d4	|, <retval>
	lea (24,sp),sp	|,
	jne .L549	|
	pea (72,sp)	|
	move.l d7,-(sp)	| key,
	move.l (76,sp),-(sp)	| node,
	move.l a3,-(sp)	| _87,
	jsr (a5)	| tmp561
	move.l (84,sp),a1	| node, node.25_60
	lea (16,sp),sp	|,
	tst.w (20,a1)	|
	jeq .L653	|
	tst.w d2	| expected_level
	jeq .L654	|
	tst.l (72,sp)	| found
	jeq .L551	|
	addq.l #1,d0	|, idx
.L551:
	move.l d5,-(sp)	| tmp554,
	move.l d0,-(sp)	| idx,
	move.l a1,-(sp)	| node.25_60,
	move.l a3,-(sp)	| _87,
	move.l d0,(60,sp)	|,
	jsr _child_bounds.isra.13	|
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_18(D)], _342
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_t *)tree_18(D)].ops, MEM[(const struct bfs_btree_t *)tree_18(D)].ops
	move.l (4,a0),d0	| _345->key_size,
	moveq #-32,d1	|, tmp526
	add.l (4,a1),d1	| _342->block_size, tmp526
	move.l d0,d6	|, tmp527
	addq.l #4,d6	|, tmp527
	divu.l d6,d1	| tmp527, tmp529
	muls.l d0,d1	|, tmp531
	move.l (60,sp),d0	|,
	move.l d1,a6	| tmp531,
	lsl.l #2,d0	|, tmp533
	lea (28,a6,d0.l),a0	|, tmp534
	moveq #92,d0	|, tmp537
	add.l (84,sp),a0	| node, tmp536
	lea (sp,d0.l),a6	|, tmp537,
	move.l (a0)+,(a6)+	|,
	move.l (92,sp),d6	| v, blk
	subq.w #1,d2	|, expected_level
	subq.w #1,d3	|, ivtmp_578
	lea (16,sp),sp	|,
	jne .L543	|
	move.l (48,sp),-(sp)	| %sfp,
	move.l a1,-(sp)	| _342,
	jsr _bfs_bio_free_buffer	|
	addq.l #8,sp	|,
	moveq #-3,d2	|,
.L486:
	move.l d2,d0	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (1068,sp),sp	|,
	rts
.L651:
	move.l (8,a2),a3	| MEM[(const struct bfs_btree_t *)tree_18(D)].ops, _195
	moveq #1,d2	|, _162
	tst.l (12,a3)	| _195->cache_key_order
	jeq .L491	|
	cmp.l #_bfs_btree_key_compare_be32,(a3)	|, _195->key_compare
	jeq .L655	|
.L491:
	tst.l (2560,a1)	| prephitmp_193->leaf_owner
	jeq .L656	|
.L502:
	move.l a1,a0	| prephitmp_193, D.10139
	moveq #27,d0	|, tmp373
.L504:
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	dbra d0,.L504	| tmp373,
.L503:
	clr.l (2588,a1)	|
	clr.l (2560,a1)	| prephitmp_193->leaf_owner
	clr.l (2564,a1)	| prephitmp_193->leaf_bio
	clr.l (2568,a1)	| prephitmp_193->leaf_ops
	clr.l (2572,a1)	| prephitmp_193->leaf_root
	clr.l (2576,a1)	| prephitmp_193->leaf_generation
	clr.l (2580,a1)	| prephitmp_193->leaf_mutation_epoch
	clr.l (2584,a1)	| prephitmp_193->leaf_mutation_epoch
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_18(D)], prephitmp_524
	tst.l d2	| _162
	jne .L505	|
	move.l (4,a0),d1	| pretmp_526->block_size, prephitmp_479
	move.l d2,(64,sp)	| _162, %sfp
	jra .L506	|
.L492:
	lea (64,a2),a0	|, tree, tmp564
	clr.l (a0)+	| tree_18(D)->hint_leaf
	clr.l (a0)+	| tree_18(D)->hint_root
	clr.l (a0)+	| tree_18(D)->hint_generation
	clr.l (a0)+	| tree_18(D)->hint_mutation_epoch
	clr.l (a0)+	| tree_18(D)->hint_mutation_epoch
	move.l (a0),a1	| tree_18(D)->key_hint_cache, prephitmp_193
	move.l a1,d0	|,
	jeq .L490	|
	clr.l (56,sp)	| %sfp
	clr.l (60,sp)	| %sfp
	clr.l d2	| _162
	tst.l (2560,a1)	| prephitmp_193->leaf_owner
	jne .L502	|
.L656:
	tst.l (2564,a1)	| prephitmp_193->leaf_bio
	jne .L502	|
	tst.l (2568,a1)	| prephitmp_193->leaf_ops
	jeq .L503	|
	move.l a1,a0	| prephitmp_193, D.10139
	moveq #27,d0	|, tmp373
	jra .L504	|
.L548:
	move.l (8,a2),a0	| tree_18(D)->ops,
	move.l (8,a0),d1	| _77->val_size, _78
	move.l (4,a0),d2	| _77->key_size, _118
	moveq #-28,d0	|, tmp502
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_18(D)],
	add.l (4,a0),d0	| _115->block_size, tmp502
	move.l d1,d3	| _78, tmp503
	add.l d2,d3	| _118, tmp503
	divu.l d3,d0	| tmp503, tmp505
	muls.l d2,d0	| _118, tmp507
	move.l d5,d2	|, tmp509
	move.l d0,a0	| tmp507,
	muls.l d1,d2	| _78, tmp509
	lea (28,a0,d2.l),a3	|, tmp510
	move.l d1,-(sp)	| _78,
	pea (a1,a3.l)	|
	move.l (1132,sp),-(sp)	| val_out,
	jsr _memcpy	|
	lea (12,sp),sp	|,
	tst.l (64,sp)	| %sfp
	jeq .L549	|
	move.l (60,sp),-(sp)	| %sfp,
	move.l (60,sp),-(sp)	| %sfp,
	move.l d5,-(sp)	|,
	move.l d6,-(sp)	| blk,
	move.l (1136,sp),-(sp)	| key,
	move.l a2,-(sp)	| tree,
	jsr _key_hint_remember	|
	move.l (84,sp),-(sp)	| %sfp,
	move.l (84,sp),-(sp)	| %sfp,
	move.l (100,sp),-(sp)	| node,
	move.l d6,-(sp)	| blk,
	move.l a2,-(sp)	| tree,
	jsr _leaf_hint_remember	|
	lea (44,sp),sp	|,
.L549:
	move.l (48,sp),-(sp)	| %sfp,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_18(D)],
	jsr _bfs_bio_free_buffer	|
	move.l d4,d0	| <retval>,
	addq.l #8,sp	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (1068,sp),sp	|,
	rts
.L653:
	move.l d0,d5	| idx,
	tst.l (64,sp)	| %sfp
	jeq .L547	|
	move.l d6,(64,a2)	| blk, tree_18(D)->hint_leaf
	move.l (12,a2),(68,a2)	| tree_18(D)->root, tree_18(D)->hint_root
	move.l (60,a2),(72,a2)	| tree_18(D)->generation, tree_18(D)->hint_generation
	move.l (56,sp),(76,a2)	| %sfp, tree_18(D)->hint_mutation_epoch
	move.l (60,sp),(80,a2)	| %sfp, tree_18(D)->hint_mutation_epoch
.L547:
	tst.l (72,sp)	| found
	jne .L548	|
	move.l (48,sp),-(sp)	| %sfp,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_18(D)],
	jsr _bfs_bio_free_buffer	|
	moveq #-5,d1	|,
	move.l d1,d0	|,
	addq.l #8,sp	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (1068,sp),sp	|,
	rts
.L654:
	move.l (48,sp),-(sp)	| %sfp,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_18(D)],
	jsr _bfs_bio_free_buffer	|
	moveq #-3,d1	|,
	move.l d1,d0	|,
	addq.l #8,sp	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (1068,sp),sp	|,
	rts
.L490:
	move.l (a2),a0	| MEM[(const struct bfs_btree_t *)tree_18(D)].bio, prephitmp_524
	move.l (4,a0),d1	| pretmp_548->block_size, prephitmp_479
	clr.l (56,sp)	| %sfp
	clr.l (60,sp)	| %sfp
	move.l a1,(64,sp)	| prephitmp_193, %sfp
	jra .L506	|
.L650:
	moveq #4,d0	|,
	cmp.l (4,a0),d0	| _137->key_size,
	jne .L500	|
	move.l (a2),a4	| MEM[(const struct bfs_btree_t *)tree_18(D)].bio, _141
	move.l (a4),a3	| _141->ops, _141->ops
	move.l (68,a3),d0	| _142->peek_valid_node, _143
	jeq .L500	|
	cmp.l (2560,a1),a2	| MEM[(const struct bfs_btree_key_hint_cache_t *)_136].leaf_owner, tree
	jne .L500	|
	cmp.l (2564,a1),a4	| MEM[(const struct bfs_btree_key_hint_cache_t *)_136].leaf_bio, _141
	jne .L500	|
	cmp.l (2568,a1),a0	| MEM[(const struct bfs_btree_key_hint_cache_t *)_136].leaf_ops, _137
	jne .L500	|
	moveq #80,d5	|, tmp554
	move.l (1120,sp),a5	| key, key
	lea (sp,d5.l),a3	|, tmp554, tmp305
	move.l (a5)+,(a3)+	|,
	move.l (80,sp),d2	| v, v.0_150
	move.l d2,d3	| v.0_150, tmp306
	muls.l #-1640531535,d3	|, tmp306
	moveq #26,d1	|,
	lsr.l d1,d3	|, _152
	move.l d3,d1	| _152, tmp558
	lsl.l #3,d1	|, tmp558
	sub.l d3,d1	| _152, tmp558
	move.l d1,d3	| tmp558, tmp557
	move.l (4,a1,d1.l*4),d1	| MEM[(const struct bfs_btree_key_hint_t *)_136].slots[_152].leaf, _153
	jeq .L500	|
	lsl.l #2,d3	|, tmp557
	move.l d3,a3	| tmp557, tmp316
	cmp.l (a3,a1.l),d2	| MEM[(const struct bfs_btree_key_hint_t *)_136].slots[_152].key, v.0_150
	jne .L500	|
	add.l a1,a3	| prephitmp_193, tmp321
	move.l (16,a3),d2	| MEM[(const struct bfs_btree_key_hint_t *)_136].slots[_152].root,
	cmp.l (12,a2),d2	| MEM[(const struct bfs_btree_t *)tree_18(D)].root,
	jne .L500	|
	move.l (12,a3),d2	| MEM[(const struct bfs_btree_key_hint_t *)_136].slots[_152].generation,
	cmp.l (60,a2),d2	| MEM[(const struct bfs_btree_t *)tree_18(D)].generation,
	jne .L500	|
	move.l (56,sp),d2	| %sfp,
	move.l (60,sp),d3	| %sfp,
	move.l (20,a3),d6	| MEM[(const struct bfs_btree_key_hint_t *)_136].slots[_152].mutation_epoch,
	move.l (24,a3),d7	| MEM[(const struct bfs_btree_key_hint_t *)_136].slots[_152].mutation_epoch,
	sub.l d7,d3	|,
	subx.l d6,d2	|,
	jne .L500	|
	move.l (8,a4),d2	| _141->block_count, _160
	cmp.l d1,d2	| _153, _160
	jls .L500	|
	move.l (8,a0),d3	| _137->val_size, _163
	lea (sp,d5.l),a0	|, tmp554, tmp565
	move.l (4,a4),a1	| _141->block_size, _164
	move.l #_bfs_btree_key_compare_be32,(a0)+	|, MEM[(struct bfs_node_validation *)&validation]
	moveq #4,d4	|,
	move.l d4,(a0)+	|, MEM[(struct bfs_node_validation *)&validation + 4B]
	move.l d3,(a0)+	| _163, MEM[(struct bfs_node_validation *)&validation + 8B]
	move.l a1,(a0)+	| _164, MEM[(struct bfs_node_validation *)&validation + 12B]
	move.l d2,(a0)	| _160, MEM[(struct bfs_node_validation *)&validation + 16B]
	pea (sp,d5.l)	|
	move.l d1,-(sp)	| _153,
	move.l a4,-(sp)	| _141,
	move.l d0,a0	| _143,
	jsr (a0)	|
	move.l d0,a4	|, iftmp.2_167
	lea (12,sp),sp	|,
	tst.l d0	| iftmp.2_167
	jeq .L636	|
	tst.w (20,a4)	|
	jne .L636	|
	addq.l #8,a3	|, tmp341
	move.l (a3),d0	| MEM[(const struct bfs_btree_key_hint_t *)_136].slots[_152].index, _170
	cmp.l (16,a4),d0	|, _170
	jcs .L657	|
.L636:
	move.l (84,a2),a1	| tree_18(D)->key_hint_cache, prephitmp_193
	move.l a1,d0	|,
	jeq .L567	|
	jra .L651	|
.L655:
	moveq #4,d0	|,
	cmp.l (4,a3),d0	| _195->key_size,
	jne .L491	|
	move.l (a2),a0	| MEM[(const struct bfs_btree_t *)tree_18(D)].bio, prephitmp_524
	move.l (a0),a4	| _199->ops, _199->ops
	tst.l (68,a4)	| _200->peek_valid_node
	jeq .L491	|
	move.l (2560,a1),a4	| prephitmp_687->leaf_owner, _207
	cmp.l a2,a4	| tree, _207
	jeq .L658	|
	move.l a1,a0	| prephitmp_193, D.10149
	moveq #27,d0	|, tmp544
.L561:
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	dbra d0,.L561	| tmp544,
	move.l (a2),a0	| tree_18(D)->bio, prephitmp_524
	move.l (8,a2),d1	| tree_18(D)->ops, prephitmp_543
	move.l (12,a2),d0	| tree_18(D)->root, pretmp_476
	move.l (84,a2),a3	| MEM[(const struct bfs_btree_t *)tree_18(D)].key_hint_cache, prephitmp_514
.L511:
	clr.l (2588,a1)	|
	move.l a2,(2560,a1)	| tree, prephitmp_687->leaf_owner
	move.l a0,(2564,a1)	| prephitmp_524, prephitmp_687->leaf_bio
	move.l d1,(2568,a1)	| prephitmp_543, prephitmp_687->leaf_ops
	move.l d0,(2572,a1)	| pretmp_476, prephitmp_687->leaf_root
	move.l (60,a2),(2576,a1)	| tree_18(D)->generation, prephitmp_687->leaf_generation
	move.l (56,sp),(2580,a1)	| %sfp, prephitmp_687->leaf_mutation_epoch
	move.l (60,sp),(2584,a1)	| %sfp, prephitmp_687->leaf_mutation_epoch
.L507:
	move.l a3,d0	|,
	jeq .L560	|
	move.l (8,a2),a1	| MEM[(const struct bfs_btree_t *)tree_18(D)].ops, _228
	tst.l (12,a1)	| _228->cache_key_order
	jeq .L560	|
	cmp.l #_bfs_btree_key_compare_be32,(a1)	|, _228->key_compare
	jne .L560	|
	moveq #4,d0	|,
	cmp.l (4,a1),d0	| _228->key_size,
	jne .L560	|
	move.l (a0),a4	| prephitmp_100->ops, prephitmp_100->ops
	move.l (68,a4),a4	| _233->peek_valid_node, _234
	move.l a4,d0	|,
	jeq .L560	|
	cmp.l (2560,a3),a2	| prephitmp_514->leaf_owner, tree
	jne .L560	|
	move.l (2572,a3),d0	| prephitmp_514->leaf_root,
	cmp.l (12,a2),d0	| tree_18(D)->root,
	jne .L560	|
	cmp.l (2564,a3),a0	| prephitmp_514->leaf_bio, prephitmp_524
	jne .L560	|
	cmp.l (2568,a3),a1	| prephitmp_514->leaf_ops, _228
	jne .L560	|
	move.l (2576,a3),d0	| prephitmp_514->leaf_generation,
	cmp.l (60,a2),d0	| tree_18(D)->generation,
	jne .L560	|
	move.l (2580,a3),(48,sp)	| prephitmp_514->leaf_mutation_epoch, %sfp
	move.l (2584,a3),(52,sp)	| prephitmp_514->leaf_mutation_epoch, %sfp
	move.l (48,sp),d1	| %sfp,
	move.l (52,sp),d2	| %sfp,
	move.l (56,sp),d3	| %sfp,
	move.l (60,sp),d4	| %sfp,
	sub.l d4,d2	|,
	subx.l d3,d1	|,
	jne .L560	|
	move.w (2588,a3),d3	| prephitmp_514->leaf_count, _246
	move.w d3,d0	| _246, tmp377
	subq.w #1,d0	|, tmp377
	cmp.w #63,d0	|, tmp377
	jhi .L560	|
	moveq #80,d5	|, tmp554
	add.l sp,d5	|, tmp554
	move.l d5,a5	| tmp554, tmp380
	move.l (1120,sp),a6	| key, key
	move.l (a6)+,(a5)+	|,
	move.l (80,sp),d2	| v, v.0_247
	and.l #65535,d3	|, hi
	clr.l d1	| lo
.L518:
	move.l d3,d0	| hi, tmp388
.L633:
	sub.l d1,d0	| lo, tmp388
	lsr.l #1,d0	|, tmp389
	add.l d1,d0	| lo, mid
	move.l d0,d4	| mid, tmp391
	add.l d0,d4	| mid, tmp391
	add.l d0,d4	| mid, tmp392
	cmp.l (1792,a3,d4.l*4),d2	|, v.0_247
	jcc .L659	|
	move.l d0,d3	| mid, hi
	cmp.l d0,d1	| mid, lo
	jcs .L633	|
	tst.l d1	| lo
	jeq .L560	|
.L523:
	subq.l #1,d1	|, _256
	move.l d1,d6	| _256, tmp555
	add.l d1,d6	| _256, tmp555
	add.l d1,d6	| _256, tmp551
	move.l d6,d0	| tmp551, tmp399
	lsl.l #2,d0	|, tmp399
	lea (a3,d0.l),a5	| prephitmp_514, tmp399, tmp400
	move.l (1800,a5),d0	| MEM[(const struct bfs_btree_leaf_hint_t *)prephitmp_514].leaf_slots[_256].leaf, _257
	jeq .L560	|
	lea (1796,a5),a6	|, tmp400, tmp407
	cmp.l (a6),d2	| MEM[(const struct bfs_btree_leaf_hint_t *)prephitmp_514].leaf_slots[_256].last_key, v.0_247
	jhi .L560	|
	move.l (8,a0),d1	| prephitmp_100->block_count, _259
	cmp.l d0,d1	| _257, _259
	jls .L560	|
	move.l (8,a1),d3	| _228->val_size, _262
	lea (80,sp),a1	|,, tmp566
	move.l (4,a0),d4	| prephitmp_100->block_size, _263
	move.l #_bfs_btree_key_compare_be32,(a1)+	|, MEM[(struct bfs_node_validation *)&validation]
	moveq #4,d7	|,
	move.l d7,(a1)+	|, MEM[(struct bfs_node_validation *)&validation + 4B]
	move.l d3,(a1)+	| _262, MEM[(struct bfs_node_validation *)&validation + 8B]
	move.l d4,(a1)+	| _263, MEM[(struct bfs_node_validation *)&validation + 12B]
	move.l d1,(a1)	| _259, MEM[(struct bfs_node_validation *)&validation + 16B]
	move.l d5,-(sp)	| tmp554,
	move.l d0,-(sp)	| _257,
	move.l a0,-(sp)	| prephitmp_524,
	jsr (a4)	| _234
	move.l d0,a4	|, iftmp.2_266
	lea (12,sp),sp	|,
	tst.l d0	| iftmp.2_266
	jeq .L567	|
	tst.w (20,a4)	|
	jne .L567	|
	move.l (16,a4),d0	|, _269
	jeq .L567	|
	move.l (8,a2),d4	| MEM[(const struct bfs_btree_ops_t * *)tree_18(D) + 8B], _270
	move.l d4,a0	| _270,
	move.l (4,a0),d7	| _270->key_size, _271
	moveq #76,d3	|, tmp553
	add.l sp,d3	|, tmp553
	move.l d3,a0	| tmp553, tmp412
	lea (28,a4),a1	|, iftmp.2_266, tmp411
	move.l (a1)+,(a0)+	|,
	move.l (76,sp),d1	| v, v.0_273
	move.l d0,d5	| _269, tmp414
	subq.l #1,d5	|, tmp414
	muls.l d7,d5	| _271, tmp415
	move.l d3,a0	| tmp553, tmp419
	lea (28,a4,d5.l),a1	|, tmp418
	move.l (a1)+,(a0)+	|,
	move.l (76,sp),a0	| v, v.0_278
	cmp.l (1792,a5),d1	| MEM[(const struct bfs_btree_leaf_hint_t *)prephitmp_514].leaf_slots[_256].first_key, v.0_273
	jne .L567	|
	cmp.l (a6),a0	| MEM[(const struct bfs_btree_leaf_hint_t *)prephitmp_514].leaf_slots[_256].last_key, v.0_278
	jne .L567	|
	cmp.l d2,d1	| v.0_247, v.0_273
	jhi .L567	|
	cmp.l d2,a0	| v.0_247, v.0_278
	jcs .L567	|
	sub.l d1,d2	| v.0_273, index
	cmp.l d0,d2	| _269, index
	jcs .L660	|
.L529:
	clr.l (76,sp)	| found
	move.l d3,-(sp)	| tmp553,
	move.l (1124,sp),-(sp)	| key,
	move.l a4,-(sp)	| iftmp.2_266,
	pea (8,a2)	|
	jsr _node_search.isra.12	|
	move.l d0,d2	|, index
	lea (16,sp),sp	|,
	tst.l (76,sp)	| found
	jeq .L567	|
.L559:
	move.l (8,a2),a0	| tree_18(D)->ops, _291
	move.l (8,a0),d1	| _291->val_size, _292
	move.l (4,a0),a0	| _291->key_size, _296
	moveq #-28,d0	|, tmp437
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_18(D)], MEM[(struct bfs_bio_t * *)tree_18(D)]
	add.l (4,a1),d0	| _293->block_size, tmp437
	move.l d1,d3	| _292, tmp438
	add.l a0,d3	| _296, tmp438
	divu.l d3,d0	| tmp438, tmp440
	move.l a0,d3	| _296,
	muls.l d0,d3	| tmp440,
	move.l d1,d0	| _292,
	muls.l d2,d0	| index,
	move.l d3,a0	|, tmp442
	lea (28,a0,d0.l),a0	|, tmp445
	move.l d1,-(sp)	| _292,
	pea (a4,a0.l)	|
	move.l (1132,sp),-(sp)	| val_out,
	jsr _memcpy	|
	move.l (64,sp),-(sp)	| %sfp,
	move.l (64,sp),-(sp)	| %sfp,
	move.l d2,-(sp)	| index,
	move.l (1800,a3,d6.l*4),-(sp)	| MEM[(const struct bfs_btree_leaf_hint_t *)prephitmp_514].leaf_slots[_256].leaf,
	move.l (1148,sp),-(sp)	| key,
	move.l a2,-(sp)	| tree,
	jsr _key_hint_remember	|
	clr.l d0	|
	lea (36,sp),sp	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (1068,sp),sp	|,
	rts
.L652:
	move.l (8,a2),a0	| tree_18(D)->ops, _39
	move.l (8,a0),d1	| _39->val_size, _40
	move.l (4,a0),d6	| _39->key_size, _104
	moveq #-28,d0	|, tmp470
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_18(D)], MEM[(struct bfs_bio_t * *)tree_18(D)]
	add.l (4,a0),d0	| _101->block_size, tmp470
	move.l d1,d5	| _40, tmp471
	add.l d6,d5	| _104, tmp471
	divu.l d5,d0	| tmp471, tmp473
	muls.l d6,d0	| _104, tmp475
	move.l d4,d5	| idx, tmp477
	move.l d0,a1	| tmp475,
	muls.l d1,d5	| _40, tmp477
	lea (28,a1,d5.l),a0	|, tmp478
	move.l d1,-(sp)	| _40,
	pea (a3,a0.l)	|
	move.l (1132,sp),-(sp)	| val_out,
	jsr _memcpy	|
	move.l d3,-(sp)	|,
	move.l d2,-(sp)	|,
	move.l d4,-(sp)	| idx,
	move.l (64,a2),-(sp)	| tree_18(D)->hint_leaf,
	move.l (1148,sp),-(sp)	| key,
	move.l a2,-(sp)	| tree,
	jsr _key_hint_remember	|
	lea (32,sp),sp	|,
	move.l d3,(sp)	|,
	move.l d2,-(sp)	|,
	move.l a3,-(sp)	| iftmp.2_325,
	move.l (64,a2),-(sp)	| tree_18(D)->hint_leaf,
	move.l a2,-(sp)	| tree,
	jsr _leaf_hint_remember	|
	clr.l d0	|
	lea (20,sp),sp	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (1068,sp),sp	|,
	rts
.L658:
	move.l (2564,a1),d2	| prephitmp_687->leaf_bio, _208
	cmp.l a0,d2	| prephitmp_524, _208
	jeq .L661	|
.L508:
	move.l a1,a0	| prephitmp_193, D.10150
	moveq #27,d0	|, tmp547
.L562:
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	dbra d0,.L562	| tmp547,
	move.l (12,a4),d0	| MEM[(struct bfs_btree_t *)_207].root, pretmp_476
	cmp.l (2572,a1),d0	| prephitmp_687->leaf_root, pretmp_476
	jeq .L563	|
	move.l (a4)+,a0	| MEM[(struct bfs_btree_t *)_207].bio, prephitmp_524
	move.l (4,a4),d1	| MEM[(struct bfs_btree_t *)_207].ops, prephitmp_543
	move.l (80,a4),a3	| MEM[(const struct bfs_btree_t *)_207].key_hint_cache, prephitmp_514
	clr.l (2588,a1)	|
	move.l a2,(2560,a1)	| tree, prephitmp_687->leaf_owner
	move.l a0,(2564,a1)	| prephitmp_524, prephitmp_687->leaf_bio
	move.l d1,(2568,a1)	| prephitmp_543, prephitmp_687->leaf_ops
	move.l d0,(2572,a1)	| pretmp_476, prephitmp_687->leaf_root
	move.l (60,a2),(2576,a1)	| tree_18(D)->generation, prephitmp_687->leaf_generation
	move.l (56,sp),(2580,a1)	| %sfp, prephitmp_687->leaf_mutation_epoch
	move.l (60,sp),(2584,a1)	| %sfp, prephitmp_687->leaf_mutation_epoch
	jra .L507	|
.L661:
	move.l (2568,a1),d1	| prephitmp_687->leaf_ops, prephitmp_543
	cmp.l a3,d1	| _195, prephitmp_543
	jne .L508	|
	move.l (12,a2),d0	| MEM[(struct bfs_btree_t *)_207].root, pretmp_476
	move.l a1,a3	| prephitmp_193, prephitmp_514
	cmp.l (2572,a1),d0	| prephitmp_687->leaf_root, pretmp_476
	jne .L511	|
.L512:
	move.l (2576,a1),d0	| prephitmp_687->leaf_generation,
	cmp.l (60,a4),d0	| MEM[(struct bfs_btree_t *)_207].generation,
	jeq .L662	|
.L632:
	move.l (12,a4),d0	| MEM[(struct bfs_btree_t *)_207].root, pretmp_476
	move.l d2,a0	| _208, prephitmp_524
	clr.l (2588,a1)	|
	move.l a2,(2560,a1)	| tree, prephitmp_687->leaf_owner
	move.l a0,(2564,a1)	| prephitmp_524, prephitmp_687->leaf_bio
	move.l d1,(2568,a1)	| prephitmp_543, prephitmp_687->leaf_ops
	move.l d0,(2572,a1)	| pretmp_476, prephitmp_687->leaf_root
	move.l (60,a2),(2576,a1)	| tree_18(D)->generation, prephitmp_687->leaf_generation
	move.l (56,sp),(2580,a1)	| %sfp, prephitmp_687->leaf_mutation_epoch
	move.l (60,sp),(2584,a1)	| %sfp, prephitmp_687->leaf_mutation_epoch
	jra .L507	|
.L505:
	move.l (84,a2),a3	| MEM[(const struct bfs_btree_t *)tree_18(D)].key_hint_cache, prephitmp_514
	jra .L507	|
.L574:
	moveq #-5,d1	|,
	move.l d1,d0	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (1068,sp),sp	|,
	rts
.L573:
	moveq #-8,d1	|,
	move.l d1,d0	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (1068,sp),sp	|,
	rts
.L563:
	move.l (a4),a0	| MEM[(struct bfs_btree_t *)_207].bio, prephitmp_524
	move.l (8,a4),d1	| MEM[(struct bfs_btree_t *)_207].ops, prephitmp_543
	move.l (84,a4),a3	| MEM[(const struct bfs_btree_t *)_207].key_hint_cache, prephitmp_514
	cmp.l d2,a0	| _208, prephitmp_524
	jne .L511	|
	cmp.l (2568,a1),d1	| prephitmp_687->leaf_ops, prephitmp_543
	jeq .L512	|
	clr.l (2588,a1)	|
	move.l a2,(2560,a1)	| tree, prephitmp_687->leaf_owner
	move.l a0,(2564,a1)	| prephitmp_524, prephitmp_687->leaf_bio
	move.l d1,(2568,a1)	| prephitmp_543, prephitmp_687->leaf_ops
	move.l d0,(2572,a1)	| pretmp_476, prephitmp_687->leaf_root
	move.l (60,a2),(2576,a1)	| tree_18(D)->generation, prephitmp_687->leaf_generation
	move.l (56,sp),(2580,a1)	| %sfp, prephitmp_687->leaf_mutation_epoch
	move.l (60,sp),(2584,a1)	| %sfp, prephitmp_687->leaf_mutation_epoch
	jra .L507	|
.L580:
	moveq #-2,d1	|,
	move.l d1,d0	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (1068,sp),sp	|,
	rts
.L657:
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_t *)tree_18(D)].ops, _172
	move.l (1120,sp),-(sp)	| key,
	muls.l (4,a0),d0	| _172->key_size, tmp342
	pea (28,a4,d0.l)	|
	move.l (a0),d0	| _172->key_compare, _172->key_compare
	move.l d0,a1	| _172->key_compare,
	jsr (a1)	|
	move.l d0,d3	|,
	addq.l #8,sp	|,
	jne .L636	|
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_t *)tree_18(D)].ops, _179
	move.l (8,a0),d1	| _179->val_size, _180
	move.l (4,a0),a0	| _179->key_size, _185
	moveq #-28,d0	|, tmp347
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_18(D)], MEM[(struct bfs_bio_t * *)tree_18(D)]
	add.l (4,a1),d0	| _182->block_size, tmp347
	move.l d1,d2	| _180, tmp348
	add.l a0,d2	| _185, tmp348
	divu.l d2,d0	| tmp348, tmp350
	move.l a0,d2	| _185,
	muls.l d0,d2	| tmp350,
	move.l d1,d0	| _180,
	muls.l (a3),d0	| MEM[(const struct bfs_btree_key_hint_t *)_136].slots[_152].index,
	move.l d2,a0	|, tmp352
	lea (28,a0,d0.l),a0	|, tmp361
	move.l d1,-(sp)	| _180,
	pea (a4,a0.l)	|
	move.l (1132,sp),-(sp)	| val_out,
	jsr _memcpy	|
	move.l d3,d0	|,
	lea (12,sp),sp	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (1068,sp),sp	|,
	rts
.L662:
	move.l (56,sp),d6	| %sfp,
	move.l (60,sp),d7	| %sfp,
	move.l (2580,a1),d3	| prephitmp_687->leaf_mutation_epoch,
	move.l (2584,a1),d4	| prephitmp_687->leaf_mutation_epoch,
	sub.l d4,d7	|,
	subx.l d3,d6	|,
	jne .L632	|
	cmp.w #64,(2588,a1)	|, prephitmp_687->leaf_count
	jhi .L632	|
	move.l d2,a0	| _208, prephitmp_524
	jra .L507	|
.L659:
	move.l d0,d1	| mid, lo
	addq.l #1,d1	|, lo
	cmp.l d3,d1	| hi, lo
	jcs .L518	|
	jra .L523	|
.L660:
	move.l (1120,sp),-(sp)	| key,
	muls.l d2,d7	| index, tmp432
	pea (28,a4,d7.l)	|
	move.l d4,a0	| _270,
	move.l (a0),d0	| _270->key_compare, _270->key_compare
	move.l d0,a6	| _270->key_compare,
	jsr (a6)	|
	addq.l #8,sp	|,
	tst.l d0	|
	jne .L529	|
	moveq #1,d0	|,
	move.l d0,(76,sp)	|, found
	jra .L559	|
	.align	2
	.globl	_bfs_btree_owned_reset
_bfs_btree_owned_reset:
	move.l a2,-(sp)	|,
	move.l (8,sp),a2	| owned, owned
	move.l a2,d0	|,
	jeq .L663	|
	move.l (a2),d1	| owned_3(D)->slots, _5
	jeq .L665	|
	move.l (4,a2),d0	| owned_3(D)->capacity, owned_3(D)->capacity
	lsl.l #2,d0	|, tmp35
	move.l d0,-(sp)	| tmp35,
	clr.l -(sp)	|
	move.l d1,-(sp)	| _5,
	jsr _memset	|
	lea (12,sp),sp	|,
.L665:
	clr.l (8,a2)	| owned_3(D)->used
	clr.l (12,a2)	| owned_3(D)->txn_id
	clr.l (16,a2)	| owned_3(D)->txn_id
.L663:
	move.l (sp)+,a2	|,
	rts
	.align	2
	.globl	_bfs_btree_owned_destroy
_bfs_btree_owned_destroy:
	move.l a6,-(sp)	|,
	move.l a2,-(sp)	|,
	move.l (12,sp),a2	| owned, owned
	move.l a2,d0	|,
	jeq .L674	|
	move.l (a2),a1	| owned_2(D)->slots, _4
	move.l a1,d0	|,
	jeq .L676	|
	move.l _SysBase,a6	| SysBase, _FreeVec_bn
#APP
| 21 "src/amiga/stdlib.h" 1
	jsr a6@(-0x2b2:W)
| 0 "" 2
#NO_APP
.L676:
	clr.l (a2)	| owned_2(D)->slots
	clr.l (4,a2)	| owned_2(D)->capacity
	clr.l (8,a2)	| owned_2(D)->used
	clr.l (12,a2)	| owned_2(D)->txn_id
	clr.l (16,a2)	| owned_2(D)->txn_id
.L674:
	move.l (sp)+,a2	|,
	move.l (sp)+,a6	|,
	rts
	.align	2
_owned_for:
	movem.l a2/d3/d2,-(sp)	|,
	move.l (16,sp),a0	| tree, tree
	move.l (52,a0),a2	| tree_4(D)->free_sink.owned, <retval>
	move.l a2,d0	|,
	jeq .L685	|
	tst.l (20,a2)	| owned_5->disabled
	jne .L689	|
	move.l (20,a0),a1	| MEM[(const uint64_t * *)tree_4(D) + 20B], _10
	move.l a1,d0	|,
	jeq .L687	|
	move.l (a1),d2	| *_10, iftmp.35_11
	move.l (4,a1),d3	| *_10,
.L688:
	move.l (12,a2),d0	| owned_5->txn_id,
	move.l (16,a2),d1	| owned_5->txn_id,
	sub.l d3,d1	| iftmp.35_11,
	subx.l d2,d0	| iftmp.35_11,
	jeq .L685	|
	move.l a2,-(sp)	| <retval>,
	jsr _bfs_btree_owned_reset	|
	move.l d2,(12,a2)	| iftmp.35_11, owned_5->txn_id
	move.l d3,(16,a2)	|, owned_5->txn_id
	addq.l #4,sp	|,
.L685:
	move.l a2,d0	| <retval>,
	movem.l (sp)+,d2/d3/a2	|,
	rts
.L687:
	move.l (24,a0),d2	| MEM[(const uint64_t *)tree_4(D) + 24B], iftmp.35_11
	move.l (28,a0),d3	| MEM[(const uint64_t *)tree_4(D) + 24B],
	jra .L688	|
.L689:
	sub.l a0,a0	|
	move.l a0,d0	|,
	movem.l (sp)+,d2/d3/a2	|,
	rts
	.align	2
_owned_contains:
	movem.l d4/d3/d2,-(sp)	|,
	move.l (20,sp),d3	| blk, blk
	move.l (16,sp),-(sp)	| tree,
	jsr _owned_for	|
	move.l d0,a0	|, owned
	addq.l #4,sp	|,
	tst.l d0	| owned
	jeq .L705	|
	move.l (4,a0),d2	| owned_7->capacity, _8
	clr.l d0	| <retval>
	tst.l d2	| _8
	jeq .L695	|
	move.l d3,d1	| blk, tmp47
	subq.l #1,d1	|, tmp47
	moveq #-3,d4	|,
	cmp.l d1,d4	| tmp47,
	jcs .L695	|
	move.l d2,d4	| _8, _18
	subq.l #1,d4	|, _18
	move.l d3,d1	| blk, tmp48
	muls.l #-1640531535,d1	|, tmp48
	and.l d4,d1	| _18, i
	move.l (a0),a0	| owned_7->slots, pretmp_21
	move.l d4,d2	| _18, tmp50
.L697:
	move.l (a0,d1.l*4),d0	| *_13, _14
	cmp.l d3,d0	| blk, _14
	jeq .L701	|
	tst.l d0	| _14
	jeq .L695	|
	addq.l #1,d1	|, _15
	and.l d4,d1	| _18, i
	dbra d2,.L697	| tmp50,
	clr.w d2	| tmp50
	subq.l #1,d2	| tmp50
	jcc .L697	|
.L705:
	clr.l d0	| <retval>
.L695:
	movem.l (sp)+,d2/d3/d4	|,
	rts
.L701:
	moveq #1,d0	|, <retval>
	movem.l (sp)+,d2/d3/d4	|,
	rts
	.align	2
_owned_place:
	movem.l a2/d5/d4/d3/d2,-(sp)	|,
	move.l (24,sp),a2	| owned, owned
	move.l (28,sp),d2	| blk, blk
	move.l (4,a2),d4	| owned_5(D)->capacity, _20
	subq.l #1,d4	|, _20
	move.l d2,d0	| blk, tmp45
	muls.l #-1640531535,d0	|, tmp45
	and.l d4,d0	| _20, i
	move.l (a2),d3	| owned_5(D)->slots, pretmp_28
	move.l d0,d1	| i, tmp46
	lsl.l #2,d1	|, tmp46
	add.l d3,d1	| pretmp_28, tmp46
	move.l d1,a0	| tmp46, _11
	move.l (a0),d1	| *_11, _12
	move.l d1,a1	| _12, tmp47
	subq.l #1,a1	|, tmp47
	moveq #-3,d5	|,
	cmp.l a1,d5	| tmp47,
	jcs .L710	|
.L716:
	cmp.l d2,d1	| blk, _12
	jeq .L708	|
	addq.l #1,d0	|, _14
	and.l d4,d0	| _20, i
	move.l d0,d1	| i, tmp46
	lsl.l #2,d1	|, tmp46
	add.l d3,d1	| pretmp_28, tmp46
	move.l d1,a0	| tmp46, _11
	move.l (a0),d1	| *_11, _12
	move.l d1,a1	| _12, tmp47
	subq.l #1,a1	|, tmp47
	moveq #-3,d5	|,
	cmp.l a1,d5	| tmp47,
	jcc .L716	|
.L710:
	cmp.l d2,d1	| blk, _12
	jeq .L708	|
	tst.l d1	| _12
	jne .L713	|
	addq.l #1,(8,a2)	|, owned_5(D)->used
.L713:
	move.l d2,(a0)	| blk, *_11
.L708:
	movem.l (sp)+,d2/d3/d4/d5/a2	|,
	rts
	.align	2
_node_dealloc:
	movem.l a2/d4/d3/d2,-(sp)	|,
	move.l (20,sp),a2	| tree, tree
	move.l (24,sp),d3	| blk, blk
	move.l a2,-(sp)	| tree,
	jsr _owned_for	|
	addq.l #4,sp	|,
	tst.l d0	| owned
	jeq .L718	|
	move.l d0,a0	| owned,
	move.l (4,a0),d0	| owned_18->capacity,
	jeq .L718	|
	move.l d3,d1	| blk, tmp54
	subq.l #1,d1	|, tmp54
	moveq #-3,d2	|,
	cmp.l d1,d2	| tmp54,
	jcs .L718	|
	move.l d0,d4	|, _21
	subq.l #1,d4	|, _21
	move.l d3,d1	| blk, tmp55
	muls.l #-1640531535,d1	|, tmp55
	and.l d4,d1	| _21, i
	move.l (a0),a1	| owned_18->slots, pretmp_43
	move.l d4,d2	| _21, tmp58
.L720:
	move.l d1,d0	| i, tmp56
	lsl.l #2,d0	|, tmp56
	lea (a1,d0.l),a0	| pretmp_43, tmp56, _26
	move.l (a0),d0	| *_26, _27
	cmp.l d3,d0	| blk, _27
	jeq .L741	|
	tst.l d0	| _27
	jeq .L718	|
	addq.l #1,d1	|, _28
	and.l d4,d1	| _21, i
	dbra d2,.L720	| tmp58,
	clr.w d2	| tmp58
	subq.l #1,d2	| tmp58
	jcc .L720	|
.L718:
	move.l (a2)+,a1	| tree_2(D)->bio, _5
	move.l a1,d0	|,
	jeq .L721	|
	move.l (a1),a0	| MEM[(const struct bfs_bio_t *)_5].ops, _11
	move.l a0,d0	|,
	jeq .L721	|
	tst.l (44,a0)	| _11->defer_node_block
	jeq .L721	|
	tst.l (48,a0)	| _11->flush_deferred
	jeq .L721	|
	move.l (52,a0),a0	| _11->discard_deferred, _14
	move.l a0,d0	|,
	jeq .L721	|
	move.l d3,-(sp)	| blk,
	move.l a1,-(sp)	| _5,
	jsr (a0)	| _14
	addq.l #8,sp	|,
.L721:
	move.l (a2),a0	| tree_2(D)->alloc, _7
	move.l d3,(24,sp)	| blk,
	move.l a0,(20,sp)	| _7,
	move.l (4,a0),a1	| _7->dealloc,
	movem.l (sp)+,d2/d3/d4/a2	|,
	jmp (a1)	|
.L741:
	moveq #-1,d0	|,
	move.l d0,(a0)	|, *_26
	jra .L718	|
	.align	2
_mutation_alloc:
	lea (-28,sp),sp	|,
	movem.l a6/a4/a3/a2/d4/d3/d2,-(sp)	|,
	move.l (60,sp),a2	| tree, tree
	move.l (4,a2),a0	| tree_3(D)->alloc, _4
	move.l (64,sp),a3	| mutation, mutation
	move.l a0,-(sp)	| _4,
	move.l (a0),a0	| _4->alloc, _4->alloc
	jsr (a0)	| _4->alloc
	move.l d0,d2	|, <retval>
	addq.l #4,sp	|,
	jeq .L743	|
	move.l (544,a3),d0	| mutation_9(D)->new_count, _10
	cmp.l #135,d0	|, _10
	jhi .L777	|
	move.l d0,d1	| _10,
	addq.l #1,d1	|,
	move.l d1,(544,a3)	|, mutation_9(D)->new_count
	move.l d2,(a3,d0.l*4)	| <retval>, mutation_9(D)->new_blocks
	move.l a2,-(sp)	| tree,
	jsr _owned_for	|
	move.l d0,a2	|, owned
	addq.l #4,sp	|,
	tst.l d0	| owned
	jeq .L742	|
	moveq #-1,d0	|,
	cmp.l d2,d0	| <retval>,
	jeq .L742	|
	move.l (4,a2),d0	| owned_26->capacity, _27
	jeq .L755	|
	move.l (8,a2),d1	| owned_26->used, tmp68
	addq.l #1,d1	|, tmp68
	add.l d1,d1	| tmp68, tmp69
	cmp.l d0,d1	| _27, tmp69
	jls .L778	|
	move.l d0,d3	| _27, iftmp.33_31
	add.l d0,d3	| _27, iftmp.33_31
	cmp.l d0,d3	| _27, iftmp.33_31
	jcc .L779	|
.L742:
	move.l d2,d0	| <retval>,
	movem.l (sp)+,d2/d3/d4/a2/a3/a4/a6	|,
	lea (28,sp),sp	|,
	rts
.L777:
	move.l (4,a2),a0	| tree_3(D)->alloc, _11
	move.l d2,-(sp)	| <retval>,
	move.l a0,-(sp)	| _11,
	move.l (4,a0),a0	| _11->dealloc, _11->dealloc
	jsr (a0)	| _11->dealloc
	addq.l #8,sp	|,
	tst.l d0	| _14
	jeq .L745	|
	tst.l (56,a2)	| MEM[(bfs_err_t *)tree_3(D) + 56B]
	jne .L743	|
	move.l d0,(56,a2)	| _14, MEM[(bfs_err_t *)tree_3(D) + 56B]
.L743:
	clr.l d0	|
	movem.l (sp)+,d2/d3/d4/a2/a3/a4/a6	|,
	lea (28,sp),sp	|,
	rts
.L745:
	tst.l (56,a2)	| MEM[(bfs_err_t *)tree_3(D) + 56B]
	jne .L743	|
	moveq #-4,d1	|,
	move.l d1,(56,a2)	|, MEM[(bfs_err_t *)tree_3(D) + 56B]
	move.l d0,d1	| _14,
	move.l d1,d0	|,
	movem.l (sp)+,d2/d3/d4/a2/a3/a4/a6	|,
	lea (28,sp),sp	|,
	rts
.L755:
	moveq #64,d3	|, iftmp.33_31
.L747:
	move.l d3,d0	| iftmp.33_31, _n1
	move.l _SysBase,a6	| SysBase, _AllocVec_bn
	lsl.l #2,d0	|, _n1
	moveq #1,d1	|, _n2
	swap d1	| _n2
#APP
| 25 "src/amiga/stdlib.h" 1
	jsr a6@(-0x2ac:W)
| 0 "" 2
#NO_APP
	tst.l d0	| _AllocVec_re2.37_37
	jeq .L742	|
	lea (28,sp),a4	|,, tmp82
	move.l a4,a0	| tmp82, tmp74
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	move.l d0,(28,sp)	| _AllocVec_re2.37_37, grown.slots
	move.l d3,(32,sp)	| iftmp.33_31, grown.capacity
	move.l (12,a2),d0	| owned_26->txn_id,
	move.l (16,a2),d1	| owned_26->txn_id,
	move.l d0,(40,sp)	|, grown.txn_id
	move.l d1,(44,sp)	|, grown.txn_id
	tst.l (4,a2)	| owned_26->capacity
	jeq .L780	|
	move.l (a2),a1	| owned_26->slots, prephitmp_86
	clr.l d3	| i
	lea _owned_place,a3	|, tmp83
.L753:
	move.l (a1,d3.l*4),d0	| *_43, entry
	move.l d0,d1	| entry, tmp75
	subq.l #1,d1	|, tmp75
	moveq #-3,d4	|,
	cmp.l d1,d4	| tmp75,
	jcc .L781	|
	addq.l #1,d3	|, i
	cmp.l (4,a2),d3	| owned_26->capacity, i
	jcs .L753	|
.L751:
	move.l a1,d0	|,
	jeq .L754	|
	move.l _SysBase,a6	| SysBase, _FreeVec_bn
#APP
| 21 "src/amiga/stdlib.h" 1
	jsr a6@(-0x2b2:W)
| 0 "" 2
#NO_APP
.L754:
	move.l a2,a0	| owned, owned
	lea (28,sp),a1	|,, tmp78
	move.l (a1)+,(a0)+	|,
	move.l (a1)+,(a0)+	|,
	move.l (a1)+,(a0)+	|,
	move.l (a1)+,(a0)+	|,
	move.l (a1)+,(a0)+	|,
	move.l (a1)+,(a0)+	|,
	move.l (a1)+,(a0)+	|,
	move.l d2,-(sp)	| <retval>,
	move.l a2,-(sp)	| owned,
	jsr (a3)	| tmp83
	addq.l #8,sp	|,
.L782:
	move.l d2,d0	| <retval>,
	movem.l (sp)+,d2/d3/d4/a2/a3/a4/a6	|,
	lea (28,sp),sp	|,
	rts
.L781:
	move.l d0,-(sp)	| entry,
	move.l a4,-(sp)	| tmp82,
	jsr (a3)	| tmp83
	move.l (a2),a1	| owned_26->slots, prephitmp_86
	addq.l #8,sp	|,
	addq.l #1,d3	|, i
	cmp.l (4,a2),d3	| owned_26->capacity, i
	jcs .L753	|
	jra .L751	|
.L778:
	lea _owned_place,a3	|, tmp83
	move.l d2,-(sp)	| <retval>,
	move.l a2,-(sp)	| owned,
	jsr (a3)	| tmp83
	addq.l #8,sp	|,
	jra .L782	|
.L779:
	tst.l d3	| iftmp.33_31
	jlt .L742	|
	moveq #-1,d0	|, tmp72
	divu.l d3,d0	| iftmp.33_31, tmp72
	moveq #3,d1	|,
	cmp.l d0,d1	| tmp72,
	jcs .L747	|
	move.l d2,d0	| <retval>,
	movem.l (sp)+,d2/d3/d4/a2/a3/a4/a6	|,
	lea (28,sp),sp	|,
	rts
.L780:
	move.l (a2),a1	| owned_26->slots, prephitmp_86
	lea _owned_place,a3	|, tmp83
	jra .L751	|
	.align	2
_mutation_abort:
	movem.l a5/a4/a3/a2/d2,-(sp)	|,
	move.l (24,sp),a2	| tree, tree
	move.l (28,sp),a3	| mutation, mutation
	tst.l (3272,a3)	| mutation_5(D)->staged_count
	jeq .L788	|
	lea (2728,a3),a4	|, mutation, ivtmp.479
	clr.l d2	| i
	lea _bfs_bio_free_buffer,a5	|, tmp51
.L787:
	move.l (a4)+,-(sp)	| MEM[base: _34, offset: 0B],
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_4(D)],
	jsr (a5)	| tmp51
	addq.l #1,d2	|, i
	addq.l #8,sp	|,
	cmp.l (3272,a3),d2	| mutation_5(D)->staged_count, i
	jcs .L787	|
.L788:
	clr.l (3272,a3)	| mutation_5(D)->staged_count
	tst.l (544,a3)	| mutation_5(D)->new_count
	jeq .L783	|
	move.l a3,a4	| mutation, ivtmp.473
	clr.l d2	| i
	lea _node_dealloc,a5	|, tmp52
.L790:
	move.l (a4)+,-(sp)	| MEM[base: _1, offset: 0B],
	move.l a2,-(sp)	| tree,
	jsr (a5)	| tmp52
	addq.l #8,sp	|,
	tst.l d0	| _9
	jeq .L789	|
	tst.l (56,a2)	| MEM[(bfs_err_t *)tree_4(D) + 56B]
	jne .L789	|
	move.l d0,(56,a2)	| _9, MEM[(bfs_err_t *)tree_4(D) + 56B]
.L789:
	addq.l #1,d2	|, i
	cmp.l (544,a3),d2	| mutation_5(D)->new_count, i
	jcs .L790	|
.L783:
	movem.l (sp)+,d2/a2/a3/a4/a5	|,
	rts
	.align	2
_mutation_commit:
	movem.l a6/a5/a4/a3/a2/d7/d6/d5/d4/d2,-(sp)	|,
	move.l (44,sp),a2	| tree, tree
	move.l (48,sp),a3	| mutation, mutation
	tst.l (3272,a3)	| mutation_9(D)->staged_count
	jeq .L812	|
	lea (2184,a3),a1	|, mutation, ivtmp.509
	clr.l d2	| i
	lea _node_write_image,a5	|, tmp90
.L813:
	move.l a1,a4	| ivtmp.509, ivtmp.509
	move.l (a4)+,d1	| MEM[base: _91, offset: 0B], blk
	move.l (2180,a3),d0	| MEM[(const struct btree_mutation_t *)mutation_9(D)].retired_count, _62
	jeq .L802	|
	cmp.l (548,a3),d1	| MEM[(const struct btree_mutation_t *)mutation_9(D)].retired_blocks, blk
	jeq .L803	|
	lea (552,a3),a0	|, mutation, ivtmp.503
	subq.l #1,d0	|, tmp91
.L805:
	dbra d0,.L806	| tmp91,
	clr.w d0	| tmp91
	subq.l #1,d0	| tmp91
	jcc .L806	|
.L802:
	pea 1.w	|
	pea (544,a1)	|
	move.l d1,-(sp)	| blk,
	move.l a2,-(sp)	| tree,
	jsr (a5)	| tmp90
	lea (16,sp),sp	|,
	tst.l d0	| err
	jeq .L803	|
	tst.l (56,a2)	| MEM[(bfs_err_t *)tree_15(D) + 56B]
	jeq .L848	|
.L807:
	move.l (52,a2),a0	| tree_15(D)->free_sink.owned, owned
	move.l a0,d1	|,
	jeq .L843	|
	move.l (24,a0),a0	| owned_18->recovery_state, _19
	move.l a0,d1	|,
	jeq .L843	|
	tst.l (a0)	| *_19
	jne .L843	|
	move.l d0,(a0)	| err, *_19
.L843:
	move.l (3272,a3),d0	| mutation_9(D)->staged_count, prephitmp_2
.L809:
	tst.l d0	| prephitmp_2
	jeq .L812	|
	lea (2728,a3),a4	|, mutation, ivtmp.497
	clr.l d2	| i
	lea _bfs_bio_free_buffer,a5	|, tmp85
.L814:
	move.l (a4)+,-(sp)	| MEM[base: _81, offset: 0B],
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_15(D)],
	jsr (a5)	| tmp85
	addq.l #1,d2	|, i
	addq.l #8,sp	|,
	cmp.l (3272,a3),d2	| mutation_9(D)->staged_count, i
	jcs .L814	|
.L812:
	clr.l (3272,a3)	| mutation_9(D)->staged_count
	tst.l (2180,a3)	| mutation_9(D)->retired_count
	jeq .L798	|
	lea (548,a3),a5	|, mutation, ivtmp.487
	lea (1092,a3),a4	|, mutation, ivtmp.490
	clr.l d2	| i
	lea _node_dealloc,a6	|, tmp89
.L821:
	move.l (a5)+,a1	| MEM[base: _72, offset: 0B], blk
	move.l (20,a2),a0	| MEM[(const uint64_t * *)tree_15(D) + 20B], _33
	move.l (a4)+,d0	| MEM[base: _60, offset: 0B], _24
	move.l (a4)+,d1	| MEM[base: _60, offset: 0B],
	move.l a0,d4	|,
	jeq .L815	|
	move.l (a0),d4	| *_33, iftmp.35_43
	move.l (4,a0),d5	| *_33,
.L816:
	move.l d0,d6	| _24,
	move.l d1,d7	|,
	sub.l d5,d7	| iftmp.35_43,
	subx.l d4,d6	| iftmp.35_43,
	jcc .L849	|
	move.l (36,a2),a0	| tree_15(D)->free_sink.defer, _28
	move.l a0,d0	|,
	jeq .L819	|
	move.l a1,-(sp)	| blk,
	move.l (32,a2),-(sp)	| tree_15(D)->free_sink.ctx,
	jsr (a0)	| _28
	addq.l #8,sp	|,
	tst.l d0	| _31
	jeq .L819	|
	tst.l (56,a2)	| MEM[(bfs_err_t *)tree_15(D) + 56B]
	jne .L819	|
	move.l d0,(56,a2)	| _31, MEM[(bfs_err_t *)tree_15(D) + 56B]
.L819:
	addq.l #1,d2	|, i
	cmp.l (2180,a3),d2	| mutation_9(D)->retired_count, i
	jcs .L821	|
.L798:
	movem.l (sp)+,d2/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	rts
.L806:
	cmp.l (a0)+,d1	| MEM[base: _86, offset: 0B], blk
	jne .L805	|
.L803:
	addq.l #1,d2	|, i
	move.l (3272,a3),d0	| mutation_9(D)->staged_count, prephitmp_2
	move.l a4,a1	| ivtmp.509, ivtmp.509
	cmp.l d0,d2	| prephitmp_2, i
	jcs .L813	|
	jra .L809	|
.L849:
	move.l a1,-(sp)	| blk,
	move.l a2,-(sp)	| tree,
	jsr (a6)	| tmp89
	addq.l #8,sp	|,
	tst.l d0	| _27
	jeq .L819	|
	tst.l (56,a2)	| MEM[(bfs_err_t *)tree_15(D) + 56B]
	jne .L819	|
	move.l d0,(56,a2)	| _31, MEM[(bfs_err_t *)tree_15(D) + 56B]
	jra .L819	|
.L815:
	move.l (24,a2),d4	| MEM[(const uint64_t *)tree_15(D) + 24B], iftmp.35_43
	move.l (28,a2),d5	| MEM[(const uint64_t *)tree_15(D) + 24B],
	jra .L816	|
.L848:
	move.l d0,(56,a2)	| err, MEM[(bfs_err_t *)tree_15(D) + 56B]
	jra .L807	|
	.align	2
_cow_node:
	subq.l #4,sp	|,
	movem.l a6/a5/a4/a3/a2/d7/d6/d5/d4/d3/d2,-(sp)	|,
	lea (52,sp),a0	|,, tmp80
	move.l (a0)+,a2	| tree, tree
	move.l (a0)+,a4	| mutation, mutation
	move.l (a0)+,d2	| old_blk, old_blk
	move.l (a0)+,a3	| buf, buf
	move.l (a0),a6	| out_blk, out_blk
	move.l (8,a3),d4	|, _11
	move.l (12,a3),d5	|,
	tst.l d2	| old_blk
	jeq .L851	|
	move.l (20,a2),a0	| MEM[(const uint64_t * *)tree_14(D) + 20B], _35
	move.l a0,d0	|,
	jeq .L852	|
	move.l (a0),d0	| *_35, iftmp.35_12
	move.l (4,a0),d1	| *_35,
	move.l d4,d6	| _11,
	move.l d5,d7	|,
	sub.l d1,d7	| iftmp.35_12,
	subx.l d0,d6	| iftmp.35_12,
	jeq .L889	|
.L851:
	move.l a4,-(sp)	| mutation,
	move.l a2,-(sp)	| tree,
	jsr _mutation_alloc	|
	move.l d0,d3	|, new_blk
	addq.l #8,sp	|,
	jne .L861	|
	move.l (4,a2),a1	| MEM[(struct bfs_allocator_t * *)tree_14(D) + 4B], _36
	move.l (8,a1),a0	| _36->error, _42
	move.l a0,d0	|,
	jeq .L863	|
	move.l a1,-(sp)	| _36,
	jsr (a0)	| _42
	move.l d0,a2	|, <retval>
	addq.l #4,sp	|,
	tst.l d0	| <retval>
	jeq .L863	|
.L850:
	move.l a2,d0	| <retval>,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	addq.l #4,sp	|,
	rts
.L861:
	lea (48,sp),a0	|,,
	move.l a3,-(a0)	| buf, buf
	clr.l -(sp)	|
	move.l a0,-(sp)	|,
	move.l d0,-(sp)	| new_blk,
	move.l a2,-(sp)	| tree,
	jsr _node_write_image	|
	move.l d0,a2	|, <retval>
	lea (16,sp),sp	|,
	tst.l d0	| <retval>
	jne .L850	|
	tst.l d2	| old_blk
	jne .L864	|
.L865:
	move.l d3,(a6)	| new_blk, *out_blk_30(D)
	move.l a2,d0	| <retval>,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	addq.l #4,sp	|,
	rts
.L889:
	move.l d2,-(sp)	| old_blk,
	move.l a2,-(sp)	| tree,
	jsr _owned_contains	|
	addq.l #8,sp	|,
	tst.l d0	|
	jeq .L851	|
	move.l (3272,a4),d0	| mutation_17(D)->staged_count, _88
	jeq .L866	|
	cmp.l (2184,a4),d2	| mutation_17(D)->staged_blocks, old_blk
	jeq .L867	|
	lea (2188,a4),a0	|, mutation, ivtmp.518
	clr.l d3	| slot
	subq.l #1,d0	|, tmp78
.L857:
	addq.l #1,d3	|, slot
	dbra d0,.L879	| tmp78,
	clr.w d0	| tmp78
	subq.l #1,d0	| tmp78
	jcc .L879	|
	cmp.l #136,d3	|, slot
	jeq .L851	|
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_14(D)], _37
	move.l (4,a0),-(sp)	| _37->block_size,
	move.l a0,-(sp)	| _37,
	jsr _bfs_bio_alloc_buffer	|
	move.l d0,a5	|, iftmp.59_2
	addq.l #8,sp	|,
.L858:
	move.l a5,d0	|,
	jeq .L851	|
	move.l (a2),a0	| tree_14(D)->bio, tree_14(D)->bio
	move.l (4,a0),-(sp)	| _22->block_size,
	move.l a3,-(sp)	| buf,
	move.l a5,-(sp)	| iftmp.59_2,
	jsr _memcpy	|
	lea (12,sp),sp	|,
	cmp.l (3272,a4),d3	| mutation_17(D)->staged_count, slot
	jeq .L890	|
	move.l d2,(a6)	| old_blk, *out_blk_30(D)
	sub.l a0,a0	|
.L891:
	move.l a0,d0	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	addq.l #4,sp	|,
	rts
.L852:
	move.l (24,a2),d0	| MEM[(const uint64_t *)tree_14(D) + 24B], iftmp.35_12
	move.l (28,a2),d1	| MEM[(const uint64_t *)tree_14(D) + 24B],
	move.l d4,d6	| _11,
	move.l d5,d7	|,
	sub.l d1,d7	| iftmp.35_12,
	subx.l d0,d6	| iftmp.35_12,
	jne .L851	|
	jra .L889	|
.L864:
	move.l d5,-(sp)	|,
	move.l d4,-(sp)	|,
	move.l d2,-(sp)	| old_blk,
	move.l a4,-(sp)	| mutation,
	jsr (_mutation_retire_txn.part.9)	|
	lea (16,sp),sp	|,
	tst.l d0	| _45
	jeq .L865	|
	move.l d0,a0	| _45,
	move.l a0,d0	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	addq.l #4,sp	|,
	rts
.L863:
	move.w #-4,a0	|,
	move.l a0,d0	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	addq.l #4,sp	|,
	rts
.L890:
	move.l d3,d0	| slot, tmp72
	lsl.l #2,d0	|, tmp72
	lea (a4,d0.l),a0	| mutation, tmp72, _90
	move.l d2,(2184,a0)	| old_blk, MEM[(struct btree_mutation_t *)_90 + 2184B]
	move.l a5,(2728,a0)	| iftmp.59_2, MEM[(struct btree_mutation_t *)_90 + 2728B]
	addq.l #1,d3	|, slot
	move.l d3,(3272,a4)	| slot, mutation_17(D)->staged_count
	move.l d2,(a6)	| old_blk, *out_blk_30(D)
	sub.l a0,a0	|
	jra .L891	|
.L879:
	cmp.l (a0)+,d2	| MEM[base: _92, offset: 0B], old_blk
	jne .L857	|
	move.l (2728,a4,d3.l*4),a5	| mutation_17(D)->staged_images, iftmp.59_2
	jra .L858	|
.L866:
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_14(D)], _37
	move.l d0,d3	| _88, slot
	move.l (4,a0),-(sp)	| _37->block_size,
	move.l a0,-(sp)	| _37,
	jsr _bfs_bio_alloc_buffer	|
	move.l d0,a5	|, iftmp.59_2
	addq.l #8,sp	|,
	jra .L858	|
.L867:
	clr.l d3	| slot
	move.l (2728,a4,d3.l*4),a5	| mutation_17(D)->staged_images, iftmp.59_2
	jra .L858	|
	.align	2
	.globl	_bfs_btree_leaf_capacity
_bfs_btree_leaf_capacity:
	movem.l d4/d3/d2,-(sp)	|,
	move.l (16,sp),a0	| tree, tree
	move.l a0,d0	|,
	jeq .L898	|
	move.l (a0),a1	| tree_2(D)->bio, _4
	move.l a1,d0	|,
	jeq .L898	|
	move.l (8,a0),a0	| tree_2(D)->ops, _5
	move.l a0,d0	|,
	jeq .L898	|
	tst.l (a0)	| _5->key_compare
	jeq .L898	|
	move.l (4,a0),d1	| _5->key_size, _7
	move.l d1,d2	| _7, tmp45
	subq.l #1,d2	|, tmp45
	clr.l d0	| <retval>
	cmp.l #511,d2	|, tmp45
	jhi .L892	|
	move.l (8,a0),d2	| _5->val_size, _8
	jeq .L892	|
	move.l (4,a1),a0	| _4->block_size, _9
	lea (-1024,a0),a1	|, _9, tmp47
	cmp.l #64512,a1	|, tmp47
	jhi .L892	|
	move.l a0,d3	| _9, tmp49
	subq.l #1,d3	|, tmp49
	move.l a0,d4	| _9,
	and.l d4,d3	|, tmp50
	jne .L892	|
	moveq #-28,d0	|, tmp52
	add.l a0,d0	| _9, tmp52
	add.l d2,d1	| _8, tmp53
	divu.l d1,d0	| tmp53, <retval>
.L892:
	movem.l (sp)+,d2/d3/d4	|,
	rts
.L898:
	clr.l d0	| <retval>
	movem.l (sp)+,d2/d3/d4	|,
	rts
	.align	2
_validate_leaf_entries:
	movem.l a3/a2/d3/d2,-(sp)	|,
	move.l (20,sp),a3	| tree, tree
	move.l (24,sp),a2	| keys, keys
	move.l (28,sp),d3	| count, count
	jeq .L908	|
	move.l (8,a3),a0	| tree_8(D)->ops, _24
	move.l (4,a0),d1	| _24->key_size, _25
	moveq #-28,d0	|, tmp52
	move.l (a3),a1	| MEM[(struct bfs_bio_t * *)tree_8(D)], MEM[(struct bfs_bio_t * *)tree_8(D)]
	add.l (4,a1),d0	| _9->block_size, tmp52
	move.l d1,d2	| _25, tmp53
	add.l (8,a0),d2	| _24->val_size, tmp53
	divu.l d2,d0	| tmp53, tmp55
	cmp.l d3,d0	| count, tmp55
	jcs .L908	|
	moveq #1,d0	|,
	cmp.l d3,d0	| count,
	jeq .L909	|
	move.l d0,d2	|, i
	move.l d2,d0	| i, tmp57
	muls.l d1,d0	| _25, tmp57
	pea (a2,d0.l)	|
	move.l d2,d0	| i, tmp59
	subq.l #1,d0	|, tmp59
	muls.l d1,d0	| _25, tmp60
	pea (a2,d0.l)	|
	move.l (a0),a0	| prephitmp_32->key_compare, prephitmp_32->key_compare
	jsr (a0)	| prephitmp_32->key_compare
	addq.l #8,sp	|,
	tst.l d0	|
	jge .L908	|
.L916:
	addq.l #1,d2	|, i
	cmp.l d3,d2	| count, i
	jeq .L909	|
	move.l (8,a3),a0	| tree_8(D)->ops, _24
	move.l (4,a0),d1	| pretmp_33->key_size, _25
	move.l d2,d0	| i, tmp57
	muls.l d1,d0	| _25, tmp57
	pea (a2,d0.l)	|
	move.l d2,d0	| i, tmp59
	subq.l #1,d0	|, tmp59
	muls.l d1,d0	| _25, tmp60
	pea (a2,d0.l)	|
	move.l (a0),a0	| prephitmp_32->key_compare, prephitmp_32->key_compare
	jsr (a0)	| prephitmp_32->key_compare
	addq.l #8,sp	|,
	tst.l d0	|
	jlt .L916	|
.L908:
	moveq #-8,d0	|, <retval>
	movem.l (sp)+,d2/d3/a2/a3	|,
	rts
.L909:
	clr.l d0	| <retval>
	movem.l (sp)+,d2/d3/a2/a3	|,
	rts
	.align	2
_build_leaf:
	movem.l a5/a4/a3/a2/d7/d6/d5/d4/d3/d2,-(sp)	|,
	lea (44,sp),a0	|,, tmp95
	move.l (a0)+,a2	| tree, tree
	move.l (a0)+,a3	| new_buf, new_buf
	move.l (a0)+,d2	| keys, keys
	move.l (a0)+,a4	| vals, vals
	move.l (a0),d3	| count, count
	move.l (a2),a0	| MEM[(const struct bfs_btree_t *)tree_4(D)].bio, MEM[(const struct bfs_btree_t *)tree_4(D)].bio
	move.l (4,a0),-(sp)	| _21->block_size,
	clr.l -(sp)	|
	move.l a3,-(sp)	| new_buf,
	jsr _memset	|
	move.l #1112821316,(a3)	|, MEM[(struct bfs_btnode_hdr_t *)new_buf_5(D)].magic
	clr.w (20,a3)	| MEM[(struct bfs_btnode_hdr_t *)new_buf_5(D)].level
	clr.l (24,a3)	| MEM[(struct bfs_btnode_hdr_t *)new_buf_5(D)].right_sibling
	move.l d3,(16,a3)	| count, MEM[(struct bfs_btnode_hdr_t *)new_buf_5(D)].num_keys
	lea (12,sp),sp	|,
	clr.l d1	| i
	tst.l d3	| count
	jeq .L917	|
	subq.l #1,d3	|, tmp89
.L928:
	move.l (8,a2),a5	| tree_4(D)->ops, pretmp_93
	move.l (4,a5),d0	| _12->key_size, pretmp_95
	move.l d0,d4	| pretmp_95,
	muls.l d1,d4	| i,
	move.l d4,a0	|, _14
	lea (28,a0),a1	|, _14, _40
	tst.l d0	| pretmp_95
	jeq .L919	|
	add.l a3,a1	| new_buf, ivtmp.544
	add.l d2,a0	| keys, ivtmp.541
	subq.l #1,d0	|, tmp93
.L920:
	move.b (a0)+,(a1)+	| MEM[base: _68, offset: 0B], MEM[base: _67, offset: 0B]
	dbra d0,.L920	| tmp93,
	clr.w d0	| tmp93
	subq.l #1,d0	| tmp93
	jcc .L920	|
	move.l (8,a2),a5	| tree_4(D)->ops, pretmp_93
	move.l (4,a5),d0	| pretmp_93->key_size, pretmp_95
.L919:
	move.l (8,a5),d4	| prephitmp_94->val_size, _18
	move.l d4,d6	| _18, _19
	muls.l d1,d6	| i, _19
	moveq #-28,d5	|, tmp81
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_4(D)], MEM[(struct bfs_bio_t * *)tree_4(D)]
	add.l (4,a0),d5	| _26->block_size, tmp81
	move.l d4,d7	| _18, tmp82
	add.l d0,d7	| pretmp_95, tmp82
	divu.l d7,d5	| tmp82, tmp84
	move.l d6,a1	| _19,
	muls.l d5,d0	| tmp84, tmp86
	lea (28,a1,d0.l),a0	|, _34
	tst.l d4	| _18
	jeq .L925	|
	lea (a4,d6.l),a1	| vals, _19, ivtmp.535
	add.l d6,d4	| _19, tmp88
	add.l a3,a0	| new_buf, ivtmp.537
	add.l a4,d4	| vals, _78
	move.l a1,d0	| ivtmp.535, tmp92
	not.l d0	| tmp92
	add.l d4,d0	| _78, tmp91
.L924:
	move.b (a1)+,(a0)+	| MEM[base: _82, offset: 0B], MEM[base: _81, offset: 0B]
	dbra d0,.L924	| tmp91,
	clr.w d0	| tmp91
	subq.l #1,d0	| tmp91
	jcc .L924	|
.L925:
	addq.l #1,d1	|, i
	dbra d3,.L928	| tmp89,
	clr.w d3	| tmp89
	subq.l #1,d3	| tmp89
	jcc .L928	|
.L917:
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5	|,
	rts
	.align	2
	.globl	_bfs_btree_create_root_leaf
_bfs_btree_create_root_leaf:
	link.w a5,#-3284	|,
	movem.l a2/d5/d4/d3/d2,-(sp)	|,
	lea (8,a5),a0	|,, tmp68
	move.l (a0)+,a2	| tree, tree
	move.l (a0)+,d2	| keys, keys
	move.l (a0)+,d3	| vals, vals
	move.l (a0),d4	| count, count
	move.l a2,d0	|,
	jeq .L947	|
	tst.l (a2)	| tree_3(D)->bio
	jeq .L947	|
	tst.l (4,a2)	| tree_3(D)->alloc
	jeq .L947	|
	tst.l d2	| keys
	jeq .L947	|
	tst.l d3	| vals
	jeq .L947	|
	move.l a2,-(sp)	| tree,
	jsr _bfs_btree_leaf_capacity	|
	addq.l #4,sp	|,
	tst.l d0	|
	jeq .L947	|
	tst.l (12,a2)	| tree_3(D)->root
	jne .L948	|
	move.l d4,-(sp)	| count,
	move.l d2,-(sp)	| keys,
	move.l a2,-(sp)	| tree,
	jsr _validate_leaf_entries	|
	lea (12,sp),sp	|,
	tst.l d0	| <retval>
	jeq .L953	|
.L935:
	movem.l (-3304,a5),d2/d3/d4/d5/a2	|,
	unlk a5	|
	rts
.L953:
	move.l d0,(56,a2)	| <retval>, tree_3(D)->free_sink_err
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_3(D)], _29
	move.l (4,a0),-(sp)	| _29->block_size,
	move.l a0,-(sp)	| _29,
	jsr _bfs_bio_alloc_buffer	|
	move.l d0,d5	|, _33
	addq.l #8,sp	|,
	jeq .L949	|
	move.l d4,-(sp)	| count,
	move.l d3,-(sp)	| vals,
	move.l d2,-(sp)	| keys,
	move.l d0,-(sp)	| _33,
	move.l a2,-(sp)	| tree,
	jsr _build_leaf	|
	lea (-3276,a5),a0	|,, tmp54
	moveq #50,d0	|, tmp55
.L937:
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	dbra d0,.L937	| tmp55,
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	jsr _mutation_alloc	|
	move.l d0,d2	|, blk
	lea (28,sp),sp	|,
	jne .L938	|
	move.l d5,-(sp)	| _33,
	move.l (a2)+,-(sp)	| MEM[(struct bfs_bio_t * *)tree_3(D)],
	jsr _bfs_bio_free_buffer	|
	move.l (a2),a1	| MEM[(struct bfs_allocator_t * *)tree_3(D) + 4B], _28
	move.l (8,a1),a0	| _28->error, _34
	addq.l #8,sp	|,
	move.l a0,d0	|,
	jeq .L940	|
	move.l a1,-(sp)	| _28,
	jsr (a0)	| _34
	tst.l d0	| <retval>
	jne .L935	|
.L940:
	moveq #-4,d0	|, <retval>
	movem.l (-3304,a5),d2/d3/d4/d5/a2	|,
	unlk a5	|
	rts
.L938:
	move.l d5,(-3280,a5)	| _33, buf
	clr.l -(sp)	|
	pea (-3280,a5)	|
	move.l d0,-(sp)	| blk,
	move.l a2,-(sp)	| tree,
	jsr _node_write_image	|
	move.l d5,-(sp)	| _33,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_3(D)],
	move.l d0,(-3284,a5)	|,
	jsr _bfs_bio_free_buffer	|
	lea (24,sp),sp	|,
	move.l (-3284,a5),d0	|,
	jne .L954	|
	move.l d2,(12,a2)	| blk, tree_3(D)->root
	addq.l #1,(60,a2)	|, tree_3(D)->generation
	moveq #1,d0	|,
	move.l d0,(16,a2)	|, tree_3(D)->height
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	jsr _mutation_commit	|
	move.l (56,a2),d0	| tree_3(D)->free_sink_err, <retval>
	movem.l (-3304,a5),d2/d3/d4/d5/a2	|,
	unlk a5	|
	rts
.L948:
	moveq #-6,d0	|, <retval>
	movem.l (-3304,a5),d2/d3/d4/d5/a2	|,
	unlk a5	|
	rts
.L947:
	moveq #-8,d0	|, <retval>
	movem.l (-3304,a5),d2/d3/d4/d5/a2	|,
	unlk a5	|
	rts
.L954:
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	jsr _mutation_abort	|
	move.l (-3284,a5),d0	|,
	movem.l (-3304,a5),d2/d3/d4/d5/a2	|,
	unlk a5	|
	rts
.L949:
	moveq #-2,d0	|, <retval>
	movem.l (-3304,a5),d2/d3/d4/d5/a2	|,
	unlk a5	|
	rts
	.align	2
_replace_root_leaf:
	movem.l a3/a2/d7/d6/d5/d4/d3/d2,-(sp)	|,
	lea (36,sp),a0	|,, tmp52
	move.l (a0)+,a2	| tree, tree
	move.l (a0)+,d6	| expected_root, expected_root
	move.l (a0)+,d2	| keys, keys
	move.l (a0)+,d4	| vals, vals
	move.l (a0)+,d5	| count, count
	move.l (a0)+,d3	| transfer_old_root, transfer_old_root
	move.l (a0)+,d7	| allow_in_place, allow_in_place
	move.l (a0),a3	| published, published
	clr.l (a3)	| *published_5(D)
	move.l a2,d0	|,
	jeq .L967	|
	tst.l (4,a2)	| tree_7(D)->alloc
	jeq .L967	|
	tst.l d2	| keys
	jeq .L967	|
	tst.l d4	| vals
	jeq .L967	|
	move.l a2,-(sp)	| tree,
	jsr _bfs_btree_leaf_capacity	|
	addq.l #4,sp	|,
	tst.l d0	|
	jeq .L967	|
	moveq #1,d0	|,
	cmp.l (16,a2),d0	| tree_7(D)->height,
	jne .L958	|
	move.l (12,a2),d0	| tree_7(D)->root, _28
	jeq .L958	|
	move.l (a2),a0	| MEM[(const struct bfs_btree_t *)tree_7(D)].bio, _29
	move.l a0,d1	|,
	jeq .L968	|
	cmp.l (8,a0),d0	| _29->block_count, _28
	jcc .L968	|
	move.l d5,-(sp)	| count,
	move.l d2,-(sp)	| keys,
	move.l a2,-(sp)	| tree,
	jsr _validate_leaf_entries	|
	lea (12,sp),sp	|,
	tst.l d0	| <retval>
	jne .L955	|
	tst.l d3	| transfer_old_root
	jeq .L960	|
	tst.l d6	| expected_root
	jeq .L958	|
	cmp.l (12,a2),d6	| tree_7(D)->root, expected_root
	jne .L958	|
.L961:
	move.l a3,(60,sp)	| published,
	move.l d7,(56,sp)	| allow_in_place,
	move.l d3,(52,sp)	| transfer_old_root,
	move.l d5,(48,sp)	| count,
	move.l d4,(44,sp)	| vals,
	move.l d2,(40,sp)	| keys,
	move.l a2,(36,sp)	| tree,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3	|,
	jmp _replace_root_leaf.part.22	|
.L968:
	moveq #-3,d0	|, <retval>
.L955:
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3	|,
	rts
.L960:
	move.l d3,(56,a2)	| transfer_old_root, tree_7(D)->free_sink_err
	tst.l (36,a2)	| MEM[(const struct bfs_btree_t *)tree_7(D)].free_sink.defer
	jeq .L961	|
	pea 1.w	|
	move.l a2,-(sp)	| tree,
	jsr (_mutation_headroom.part.3)	|
	addq.l #8,sp	|,
	tst.l d0	| <retval>
	jeq .L961	|
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3	|,
	rts
.L958:
	moveq #-10,d0	|, <retval>
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3	|,
	rts
.L967:
	moveq #-8,d0	|, <retval>
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3	|,
	rts
	.align	2
	.globl	_bfs_btree_replace_root_leaf
_bfs_btree_replace_root_leaf:
	link.w a5,#-4	|,
	lea (8,a5),a0	|,, tmp39
	move.l (a0)+,d0	| tree, tree
	move.l (a0)+,d1	| keys, keys
	move.l (a0)+,a1	| vals, vals
	pea (-4,a5)	|
	clr.l -(sp)	|
	clr.l -(sp)	|
	move.l (a0),-(sp)	| count,
	move.l a1,-(sp)	| vals,
	move.l d1,-(sp)	| keys,
	clr.l -(sp)	|
	move.l d0,-(sp)	| tree,
	jsr _replace_root_leaf	|
	unlk a5	|
	rts
	.align	2
	.globl	_bfs_btree_rewrite_root_leaf
_bfs_btree_rewrite_root_leaf:
	subq.l #4,sp	|,
	move.l d3,-(sp)	|,
	move.l d2,-(sp)	|,
	lea (16,sp),a0	|,, tmp39
	move.l (a0)+,d1	| tree, tree
	move.l (a0)+,a1	| keys, keys
	move.l (a0)+,d2	| vals, vals
	move.l (a0)+,d3	| count, count
	move.l (a0),d0	| published, published
	jeq .L988	|
	move.l d0,-(sp)	| published,
	pea 1.w	|
	clr.l -(sp)	|
	move.l d3,-(sp)	| count,
	move.l d2,-(sp)	| vals,
	move.l a1,-(sp)	| keys,
	clr.l -(sp)	|
	move.l d1,-(sp)	| tree,
	jsr _replace_root_leaf	|
	lea (32,sp),sp	|,
	move.l (sp)+,d2	|,
	move.l (sp)+,d3	|,
	addq.l #4,sp	|,
	rts
.L988:
	move.l sp,d0	|, published
	addq.l #8,d0	|, published
	move.l d0,-(sp)	| published,
	pea 1.w	|
	clr.l -(sp)	|
	move.l d3,-(sp)	| count,
	move.l d2,-(sp)	| vals,
	move.l a1,-(sp)	| keys,
	clr.l -(sp)	|
	move.l d1,-(sp)	| tree,
	jsr _replace_root_leaf	|
	lea (32,sp),sp	|,
	move.l (sp)+,d2	|,
	move.l (sp)+,d3	|,
	addq.l #4,sp	|,
	rts
	.align	2
	.globl	_bfs_btree_root_leaf_txn_id
_bfs_btree_root_leaf_txn_id:
	subq.l #4,sp	|,
	movem.l a4/a3/a2/d2,-(sp)	|,
	move.l (24,sp),a2	| tree, tree
	move.l (28,sp),a4	| txn_id_out, txn_id_out
	move.l a4,d0	|,
	jeq .L996	|
	move.l a2,-(sp)	| tree,
	jsr _bfs_btree_leaf_capacity	|
	addq.l #4,sp	|,
	tst.l d0	|
	jeq .L996	|
	move.l (12,a2),d1	| tree_6(D)->root, _8
	jeq .L998	|
	moveq #1,d0	|,
	cmp.l (16,a2),d0	| tree_6(D)->height,
	jne .L998	|
	move.l (a2),a0	| MEM[(const struct bfs_btree_t *)tree_6(D)].bio, _12
	moveq #-3,d0	|, <retval>
	move.l a0,d2	|,
	jeq .L989	|
	cmp.l (8,a0),d1	| _12->block_count, _8
	jcc .L989	|
	move.l (4,a0),-(sp)	| _12->block_size,
	move.l a0,-(sp)	| _12,
	jsr _bfs_bio_alloc_buffer	|
	move.l d0,a3	|, _19
	addq.l #8,sp	|,
	tst.l d0	| _19
	jeq .L1006	|
	move.l a3,-(sp)	| _19,
	move.l (12,a2),-(sp)	| tree_6(D)->root,
	move.l a2,-(sp)	| tree,
	jsr _node_read	|
	lea (12,sp),sp	|,
	tst.l d0	| <retval>
	jeq .L1007	|
	move.l a3,-(sp)	| _19,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_6(D)],
	move.l d0,(24,sp)	|,
	jsr _bfs_bio_free_buffer	|
	addq.l #8,sp	|,
	move.l (16,sp),d0	|,
.L989:
	movem.l (sp)+,d2/a2/a3/a4	|,
	addq.l #4,sp	|,
	rts
.L1007:
	tst.w (20,a3)	|
	jne .L1008	|
	move.l (8,a3),d1	|,
	move.l (12,a3),d2	|,
	move.l d1,(a4)	|, *txn_id_out_4(D)
	move.l d2,(4,a4)	|, *txn_id_out_4(D)
	move.l a3,-(sp)	| _19,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_6(D)],
	move.l d0,(24,sp)	|,
	jsr _bfs_bio_free_buffer	|
	addq.l #8,sp	|,
	move.l (16,sp),d0	|,
	jra .L989	|
.L1008:
	moveq #-3,d0	|, <retval>
	move.l a3,-(sp)	| _19,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_6(D)],
	move.l d0,(24,sp)	|,
	jsr _bfs_bio_free_buffer	|
	addq.l #8,sp	|,
	move.l (16,sp),d0	|,
	jra .L989	|
.L998:
	moveq #-10,d0	|, <retval>
	movem.l (sp)+,d2/a2/a3/a4	|,
	addq.l #4,sp	|,
	rts
.L996:
	moveq #-8,d0	|, <retval>
	movem.l (sp)+,d2/a2/a3/a4	|,
	addq.l #4,sp	|,
	rts
.L1006:
	moveq #-2,d0	|, <retval>
	movem.l (sp)+,d2/a2/a3/a4	|,
	addq.l #4,sp	|,
	rts
	.align	2
	.globl	_bfs_btree_replace_owned_root_leaf
_bfs_btree_replace_owned_root_leaf:
	link.w a5,#-4	|,
	move.l d2,-(sp)	|,
	lea (8,a5),a0	|,, tmp40
	move.l (a0)+,d0	| tree, tree
	move.l (a0)+,d1	| expected_root, expected_root
	move.l (a0)+,a1	| keys, keys
	move.l (a0)+,d2	| vals, vals
	pea (-4,a5)	|
	clr.l -(sp)	|
	pea 1.w	|
	move.l (a0),-(sp)	| count,
	move.l d2,-(sp)	| vals,
	move.l a1,-(sp)	| keys,
	move.l d1,-(sp)	| expected_root,
	move.l d0,-(sp)	| tree,
	jsr _replace_root_leaf	|
	move.l (-8,a5),d2	|,
	unlk a5	|
	rts
	.align	2
_leaf_insert_at:
	subq.l #4,sp	|,
	movem.l a6/a5/a4/a3/a2/d7/d6/d5/d4/d3/d2,-(sp)	|,
	lea (52,sp),a0	|,, tmp143
	move.l (a0)+,a5	| tree, tree
	move.l (a0)+,a4	| buf, buf
	move.l (a0)+,d3	| idx, idx
	move.l (a0)+,a3	| key, key
	move.l (a0),a6	| val, val
	move.l (16,a4),(44,sp)	|, %sfp
	move.l (8,a5),a0	| tree_5(D)->ops, _6
	move.l (4,a0),d4	| _6->key_size, ks
	move.l (8,a0),d5	| _6->val_size, vs
	cmp.l (44,sp),d3	| %sfp, idx
	jcc .L1014	|
	move.l (44,sp),d2	| %sfp, i
	subq.l #1,d2	|, i
	move.l d2,d0	| i, _42
	muls.l d4,d0	|, _42
	move.l d0,a1	| _42,
	lea _memcpy,a2	|, tmp142
	lea (28,a1,d4.l),a0	|, tmp85
	move.l d4,-(sp)	| ks,
	pea (28,a4,d0.l)	|
	pea (a4,a0.l)	|
	jsr (a2)	| tmp142
	move.l (8,a5),a0	| tree_5(D)->ops,
	move.l (4,a0),d7	| _28->key_size, _29
	move.l (8,a0),d1	| _28->val_size, _30
	moveq #-28,d0	|, tmp96
	move.l (a5),a0	| MEM[(struct bfs_bio_t * *)tree_5(D)],
	add.l (4,a0),d0	| _25->block_size, tmp96
	move.l d7,d6	| _29, tmp97
	add.l d1,d6	| _30, tmp97
	divu.l d6,d0	| tmp97, tmp99
	move.w #28,a0	|, keys_end
	muls.l d7,d0	| _29, tmp101
	add.l d0,a0	| tmp101, keys_end
	move.l d2,d0	| i, _35
	muls.l d1,d0	| _30, _35
	add.l d0,d1	| _35, tmp102
	add.l a0,d1	| keys_end, tmp103
	add.l a0,d0	| keys_end, tmp105
	move.l d5,-(sp)	| vs,
	pea (a4,d0.l)	|
	pea (a4,d1.l)	|
	jsr (a2)	| tmp142
	lea (24,sp),sp	|,
	move.l (8,a5),a0	| MEM[(const struct bfs_btree_ops_t * *)tree_5(D) + 8B], MEM[(const struct bfs_btree_ops_t * *)tree_5(D) + 8B]
	cmp.l d3,d2	| idx, i
	jeq .L1012	|
.L1017:
	move.l (4,a0),d1	| pretmp_79->key_size, pretmp_81
	subq.l #1,d2	|, i
	move.l d2,d0	| i, _42
	muls.l d1,d0	| pretmp_81, _42
	move.l d0,a1	| _42,
	lea (28,a1,d1.l),a0	|, tmp85
	move.l d4,-(sp)	| ks,
	pea (28,a4,d0.l)	|
	pea (a4,a0.l)	|
	jsr (a2)	| tmp142
	move.l (8,a5),a0	| tree_5(D)->ops,
	move.l (4,a0),d7	| _28->key_size, _29
	move.l (8,a0),d1	| _28->val_size, _30
	moveq #-28,d0	|, tmp96
	move.l (a5),a0	| MEM[(struct bfs_bio_t * *)tree_5(D)],
	add.l (4,a0),d0	| _25->block_size, tmp96
	move.l d7,d6	| _29, tmp97
	add.l d1,d6	| _30, tmp97
	divu.l d6,d0	| tmp97, tmp99
	move.w #28,a0	|, keys_end
	muls.l d7,d0	| _29, tmp101
	add.l d0,a0	| tmp101, keys_end
	move.l d2,d0	| i, _35
	muls.l d1,d0	| _30, _35
	add.l d0,d1	| _35, tmp102
	add.l a0,d1	| keys_end, tmp103
	add.l a0,d0	| keys_end, tmp105
	move.l d5,-(sp)	| vs,
	pea (a4,d0.l)	|
	pea (a4,d1.l)	|
	jsr (a2)	| tmp142
	lea (24,sp),sp	|,
	move.l (8,a5),a0	| MEM[(const struct bfs_btree_ops_t * *)tree_5(D) + 8B], MEM[(const struct bfs_btree_ops_t * *)tree_5(D) + 8B]
	cmp.l d3,d2	| idx, i
	jne .L1017	|
.L1012:
	move.l (4,a0),d0	| pretmp_83->key_size, pretmp_85
	muls.l d3,d0	| idx, tmp115
	move.l d4,-(sp)	| ks,
	move.l a3,-(sp)	| key,
	pea (28,a4,d0.l)	|
	jsr (a2)	| tmp142
	move.l (8,a5),a0	| tree_5(D)->ops, _48
	move.l (4,a0),d4	| _48->key_size, _49
	move.l (8,a0),d2	| _48->val_size, _50
	moveq #-28,d0	|, tmp125
	move.l (a5),a0	| MEM[(struct bfs_bio_t * *)tree_5(D)], MEM[(struct bfs_bio_t * *)tree_5(D)]
	add.l (4,a0),d0	| _45->block_size, tmp125
	move.l d4,d1	| _49, tmp126
	add.l d2,d1	| _50, tmp126
	divu.l d1,d0	| tmp126, tmp128
	muls.l d4,d0	| _49, tmp130
	move.l d0,a1	| tmp130,
	muls.l d2,d3	| _50, tmp131
	lea (28,a1,d3.l),a0	|, tmp133
	move.l d5,-(sp)	| vs,
	move.l a6,-(sp)	| val,
	pea (a4,a0.l)	|
	jsr (a2)	| tmp142
	move.l (68,sp),d0	| %sfp,
	addq.l #1,d0	|,
	move.l d0,(16,a4)	|, MEM[(struct bfs_btnode_hdr_t *)buf_4(D)].num_keys
	lea (24,sp),sp	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	addq.l #4,sp	|,
	rts
.L1014:
	move.l d4,d0	| ks, pretmp_85
	lea _memcpy,a2	|, tmp142
	muls.l d3,d0	| idx, tmp115
	move.l d4,-(sp)	| ks,
	move.l a3,-(sp)	| key,
	pea (28,a4,d0.l)	|
	jsr (a2)	| tmp142
	move.l (8,a5),a0	| tree_5(D)->ops, _48
	move.l (4,a0),d4	| _48->key_size, _49
	move.l (8,a0),d2	| _48->val_size, _50
	moveq #-28,d0	|, tmp125
	move.l (a5),a0	| MEM[(struct bfs_bio_t * *)tree_5(D)], MEM[(struct bfs_bio_t * *)tree_5(D)]
	add.l (4,a0),d0	| _45->block_size, tmp125
	move.l d4,d1	| _49, tmp126
	add.l d2,d1	| _50, tmp126
	divu.l d1,d0	| tmp126, tmp128
	muls.l d4,d0	| _49, tmp130
	move.l d0,a1	| tmp130,
	muls.l d2,d3	| _50, tmp131
	lea (28,a1,d3.l),a0	|, tmp133
	move.l d5,-(sp)	| vs,
	move.l a6,-(sp)	| val,
	pea (a4,a0.l)	|
	jsr (a2)	| tmp142
	move.l (68,sp),d0	| %sfp,
	addq.l #1,d0	|,
	move.l d0,(16,a4)	|, MEM[(struct bfs_btnode_hdr_t *)buf_4(D)].num_keys
	lea (24,sp),sp	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	addq.l #4,sp	|,
	rts
	.align	2
_internal_insert_at:
	subq.l #8,sp	|,
	movem.l a5/a4/a3/a2/d7/d6/d5/d4/d3/d2,-(sp)	|,
	lea (52,sp),a0	|,, tmp146
	move.l (a0)+,a3	| tree, tree
	move.l (a0)+,a2	| buf, buf
	move.l (a0)+,d4	| idx, idx
	move.l (a0)+,d6	| key, key
	move.l (a0),a5	| right_child, right_child
	move.l (8,a3),a0	| tree_8(D)->ops, tree_8(D)->ops
	move.l (16,a2),d3	|,
	move.l (4,a0),d5	| _9->key_size, ks
	cmp.l d4,d3	| idx,
	jls .L1025	|
	move.l d3,d2	|, i
	subq.l #1,d2	|, i
	move.l d2,d0	| i, _31
	muls.l d5,d0	|, _31
	move.l d0,a1	| _31,
	lea _memcpy,a4	|, tmp141
	lea (28,a1,d5.l),a0	|, tmp86
	move.l d5,-(sp)	| ks,
	pea (28,a2,d0.l)	|
	pea (a2,a0.l)	|
	jsr (a4)	| tmp141
	lea (12,sp),sp	|,
	move.l (8,a3),a0	| MEM[(const struct bfs_btree_ops_t * *)tree_8(D) + 8B], MEM[(const struct bfs_btree_ops_t * *)tree_8(D) + 8B]
	cmp.l d4,d2	| idx, i
	jeq .L1020	|
.L1032:
	move.l (4,a0),d1	| pretmp_115->key_size, pretmp_117
	subq.l #1,d2	|, i
	move.l d2,d0	| i, _31
	muls.l d1,d0	| pretmp_117, _31
	move.l d0,a1	| _31,
	lea (28,a1,d1.l),a0	|, tmp86
	move.l d5,-(sp)	| ks,
	pea (28,a2,d0.l)	|
	pea (a2,a0.l)	|
	jsr (a4)	| tmp141
	lea (12,sp),sp	|,
	move.l (8,a3),a0	| MEM[(const struct bfs_btree_ops_t * *)tree_8(D) + 8B], MEM[(const struct bfs_btree_ops_t * *)tree_8(D) + 8B]
	cmp.l d4,d2	| idx, i
	jne .L1032	|
.L1020:
	move.l (4,a0),d0	| pretmp_119->key_size, pretmp_121
.L1019:
	muls.l d4,d0	| idx, tmp98
	move.l d5,-(sp)	| ks,
	move.l d6,-(sp)	| key,
	pea (28,a2,d0.l)	|
	jsr (a4)	| tmp141
	move.l d3,d6	|, i
	addq.l #1,d6	|, i
	addq.l #1,d4	|, _45
	lea (12,sp),sp	|,
	cmp.l d6,d4	| i, _45
	jcc .L1033	|
	move.l d3,d1	|, ivtmp.576
	lsl.l #2,d1	|, ivtmp.576
	moveq #44,d5	|, tmp140
	add.l sp,d5	|, tmp140
	move.l d4,d2	| _45, tmp144
	not.l d2	| tmp144
	add.l d6,d2	| i, tmp143
	cmp.l d4,d3	| _45,
	jcs .L1034	|
.L1023:
	move.l (8,a3),a0	| tree_8(D)->ops, tree_8(D)->ops
	move.l (4,a0),d3	| _53->key_size, _54
	moveq #-32,d0	|, tmp123
	move.l (a3),a0	| MEM[(struct bfs_bio_t * *)tree_8(D)],
	add.l (4,a0),d0	| _50->block_size, tmp123
	move.l d3,d7	| _54, tmp124
	addq.l #4,d7	|, tmp124
	divu.l d7,d0	| tmp124, tmp126
	muls.l d3,d0	| _54, tmp126
	move.l d0,a0	| tmp126, _57
	lea (28,a0,d1.l),a1	|, tmp130
	move.l d5,a4	| tmp140, tmp133
	add.l a2,a1	| buf, tmp132
	move.l (a1)+,(a4)+	|,
	move.l (44,sp),(40,sp)	| v, v
	lea (32,a0,d1.l),a0	|, tmp135
	add.l a2,a0	| buf, tmp139
	lea (40,sp),a1	|,, tmp138
	move.l (a1)+,(a0)+	|,
	subq.l #4,d1	|, ivtmp.576
	dbra d2,.L1023	| tmp143,
	clr.w d2	| tmp143
	subq.l #1,d2	| tmp143
	jcc .L1023	|
.L1024:
	move.l (8,a3),a0	| tree_8(D)->ops, tree_8(D)->ops
	move.l (4,a0),d2	| _66->key_size, _67
	moveq #-32,d0	|, tmp109
	move.l (a3),a0	| MEM[(struct bfs_bio_t * *)tree_8(D)], MEM[(struct bfs_bio_t * *)tree_8(D)]
	add.l (4,a0),d0	| _63->block_size, tmp109
	move.l d2,d1	| _67, tmp110
	addq.l #4,d1	|, tmp110
	divu.l d1,d0	| tmp110, tmp112
	lea (28,d4.l*4),a0	|, tmp116
	muls.l d2,d0	| _67, tmp114
	add.l a0,d0	| tmp116, tmp117
	move.l a5,(44,sp)	| right_child, v
	lea (a2,d0.l),a0	| buf, tmp117, _74
	move.l d5,a1	| tmp140, tmp119
	move.l (a1)+,(a0)+	|,
	move.l d6,(16,a2)	| i, MEM[(struct bfs_btnode_hdr_t *)buf_6(D)].num_keys
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5	|,
	addq.l #8,sp	|,
	rts
.L1025:
	move.l d5,d0	| ks, pretmp_121
	lea _memcpy,a4	|, tmp141
	jra .L1019	|
.L1033:
	moveq #44,d3	|,
	move.l (8,a3),a0	| tree_8(D)->ops, tree_8(D)->ops
	add.l sp,d3	|,
	move.l (4,a0),d2	| _66->key_size, _67
	moveq #-32,d0	|, tmp109
	move.l (a3),a0	| MEM[(struct bfs_bio_t * *)tree_8(D)], MEM[(struct bfs_bio_t * *)tree_8(D)]
	add.l (4,a0),d0	| _63->block_size, tmp109
	move.l d2,d1	| _67, tmp110
	addq.l #4,d1	|, tmp110
	divu.l d1,d0	| tmp110, tmp112
	lea (28,d4.l*4),a0	|, tmp116
	muls.l d2,d0	| _67, tmp114
	add.l a0,d0	| tmp116, tmp117
	move.l a5,(44,sp)	| right_child, v
	lea (a2,d0.l),a0	| buf, tmp117, _74
	move.l d3,a1	|, tmp119
	move.l (a1)+,(a0)+	|,
	move.l d6,(16,a2)	| i, MEM[(struct bfs_btnode_hdr_t *)buf_6(D)].num_keys
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5	|,
	addq.l #8,sp	|,
	rts
.L1034:
	move.l (8,a3),a0	| tree_8(D)->ops, tree_8(D)->ops
	clr.l d2	| tmp143
	move.l (4,a0),d3	| _53->key_size, _54
	moveq #-32,d0	|, tmp123
	move.l (a3),a0	| MEM[(struct bfs_bio_t * *)tree_8(D)],
	add.l (4,a0),d0	| _50->block_size, tmp123
	move.l d3,d7	| _54, tmp124
	addq.l #4,d7	|, tmp124
	divu.l d7,d0	| tmp124, tmp126
	muls.l d3,d0	| _54, tmp126
	move.l d0,a0	| tmp126, _57
	lea (28,a0,d1.l),a1	|, tmp130
	move.l d5,a4	| tmp140, tmp133
	add.l a2,a1	| buf, tmp132
	move.l (a1)+,(a4)+	|,
	move.l (44,sp),(40,sp)	| v, v
	lea (32,a0,d1.l),a0	|, tmp135
	add.l a2,a0	| buf, tmp139
	lea (40,sp),a1	|,, tmp138
	move.l (a1)+,(a0)+	|,
	subq.l #4,d1	|, ivtmp.576
	dbra d2,.L1023	| tmp143,
	clr.w d2	| tmp143
	subq.l #1,d2	| tmp143
	jcc .L1023	|
	jra .L1024	|
	.align	2
	.globl	_bfs_btree_insert
_bfs_btree_insert:
	link.w a5,#-5688	|,
	movem.l a6/a4/a3/a2/d7/d6/d5/d4/d3/d2,-(sp)	|,
	move.l (8,a5),a2	| tree, tree
	move.l (12,a5),d6	| key, key
	move.l a2,d0	|,
	jeq .L1115	|
	move.l (a2),a1	| tree_29(D)->bio, _31
	move.l a1,d0	|,
	jeq .L1115	|
	tst.l (4,a2)	| tree_29(D)->alloc
	jeq .L1115	|
	tst.l d6	| key
	jeq .L1115	|
	tst.l (16,a5)	| val
	jeq .L1115	|
	clr.l (56,a2)	| tree_29(D)->free_sink_err
	lea (-3276,a5),a0	|,, tmp291
	moveq #50,d0	|, tmp292
.L1037:
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	dbra d0,.L1037	| tmp292,
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	move.l (12,a2),d3	| tree_29(D)->root, _37
	jeq .L1151	|
	moveq #-3,d2	|, <retval>
	cmp.l (8,a1),d3	| _31->block_count, _37
	jcc .L1035	|
	move.l (16,a2),d0	| MEM[(const struct bfs_btree_t *)tree_29(D)].height, tmp311
	subq.l #1,d0	|, tmp311
	moveq #31,d1	|,
	cmp.l d0,d1	| tmp311,
	jcs .L1035	|
	lea (-4308,a5),a0	|,, tmp572
	moveq #15,d0	|, tmp573
.L1106:
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	dbra d0,.L1106	| tmp573,
	clr.l (a0)+	|
	clr.l (a0)+	|
	pea (-4308,a5)	|
	move.l d6,-(sp)	| key,
	move.l a2,-(sp)	| tree,
	jsr _owned_leaf_in_place	|
	move.l d0,a3	|, owned_leaf
	lea (12,sp),sp	|,
	tst.l d0	| owned_leaf
	jeq .L1049	|
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_ops_t * *)tree_29(D) + 8B], MEM[(const struct bfs_btree_ops_t * *)tree_29(D) + 8B]
	move.l (16,a0),a0	| _192->entry_ok, _214
	move.l a0,d0	|,
	jeq .L1045	|
	move.l (16,a5),-(sp)	| val,
	move.l d6,-(sp)	| key,
	jsr (a0)	| _214
	addq.l #8,sp	|,
	tst.l d0	|
	jeq .L1049	|
.L1045:
	pea (-4828,a5)	|
	move.l d6,-(sp)	| key,
	move.l a3,-(sp)	| owned_leaf,
	pea (8,a2)	|
	jsr _node_search.isra.12	|
	lea (16,sp),sp	|,
	moveq #-6,d2	|, <retval>
	tst.l (-4828,a5)	| found
	jne .L1035	|
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_t *)tree_29(D)].ops, _220
	moveq #-28,d1	|, tmp314
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_29(D)], MEM[(struct bfs_bio_t * *)tree_29(D)]
	add.l (4,a1),d1	| _217->block_size, tmp314
	move.l (4,a0),d2	| _220->key_size, tmp315
	add.l (8,a0),d2	| _220->val_size, tmp315
	divu.l d2,d1	| tmp315, tmp317
	cmp.l (16,a3),d1	|, tmp317
	jhi .L1152	|
.L1049:
	move.l (16,a2),d0	| tree_29(D)->height, _62
	tst.l (36,a2)	| MEM[(const struct bfs_btree_t *)tree_29(D)].free_sink.defer
	jeq .L1050	|
	tst.l d0	| _62
	jne .L1153	|
.L1050:
	move.l (a2),a0	| tree_29(D)->bio, _63
	move.l (4,a0),(-5680,a5)	| _63->block_size, %sfp
	muls.l (-5680,a5),d0	| %sfp, _62
	move.l d0,-(sp)	| _62,
	move.l a0,-(sp)	| _63,
	jsr _bfs_bio_alloc_buffer	|
	move.l d0,(-5672,a5)	|, %sfp
	addq.l #8,sp	|,
	jeq .L1051	|
	move.l (12,a2),d2	| tree_29(D)->root,
	move.l a5,d1	|, ivtmp.624
	add.l #-5604,d1	|, ivtmp.624
	moveq #32,d5	|, ivtmp_595
	clr.l d4	| depth
	lea (8,a2),a0	|, tree,
	lea _node_read,a3	|, tmp603
	move.l a0,(-5676,a5)	|, %sfp
	lea _node_search.isra.12,a4	|, tmp606
	move.l d0,d7	| ivtmp.625, ivtmp.625
	move.l d2,a6	|, blk
	move.l d1,(-5684,a5)	| ivtmp.624, %sfp
.L1057:
	move.l (16,a2),d2	| tree_29(D)->height, _70
	cmp.l d2,d4	| _70, depth
	jcc .L1142	|
	move.l d7,-(sp)	| ivtmp.625,
	move.l a6,-(sp)	| blk,
	move.l a2,-(sp)	| tree,
	jsr (a3)	| tmp603
	lea (12,sp),sp	|,
	tst.l d0	| <retval>
	jeq .L1054	|
	move.l d0,d2	| <retval>, <retval>
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_29(D)], prephitmp_570
	lea _bfs_bio_free_buffer,a4	|, tmp584
.L1058:
	move.l (-5672,a5),-(sp)	| %sfp,
	move.l a1,-(sp)	| prephitmp_570,
	jsr (a4)	| tmp584
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	jsr _mutation_abort	|
.L1035:
	move.l d2,d0	| <retval>,
	movem.l (-5728,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L1054:
	subq.w #1,d2	|, tmp324
	sub.w d4,d2	| depth, expected_level
	move.l d7,a0	| ivtmp.625,
	cmp.w (20,a0),d2	|, expected_level
	jeq .L1055	|
.L1142:
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_29(D)], prephitmp_570
	moveq #-3,d2	|, <retval>
	lea _bfs_bio_free_buffer,a4	|, tmp584
	move.l (-5672,a5),-(sp)	| %sfp,
	move.l a1,-(sp)	| prephitmp_570,
	jsr (a4)	| tmp584
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	jsr _mutation_abort	|
	jra .L1035	|
.L1055:
	move.l (-5684,a5),a0	| %sfp,
	move.l a6,(a0)	| blk, MEM[base: _559, offset: 0B]
	tst.w d2	| expected_level
	jeq .L1154	|
	pea (-5348,a5)	|
	move.l d6,-(sp)	| key,
	move.l d7,-(sp)	| ivtmp.625,
	move.l (-5676,a5),-(sp)	| %sfp,
	jsr (a4)	| tmp606
	lea (16,sp),sp	|,
	tst.l (-5348,a5)	| found
	jeq .L1056	|
	addq.l #1,d0	|, idx
.L1056:
	move.l (-5684,a5),a0	| %sfp,
	move.l d0,(4,a0)	| idx, MEM[base: _559, offset: 4B]
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_29(D)], prephitmp_570
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_t *)tree_29(D)].ops, MEM[(const struct bfs_btree_t *)tree_29(D)].ops
	move.l (4,a0),d2	| _307->key_size, _308
	moveq #-32,d1	|, tmp330
	add.l (4,a1),d1	| _304->block_size, tmp330
	move.l d2,d3	| _308, tmp331
	addq.l #4,d3	|, tmp331
	divu.l d3,d1	| tmp331, tmp333
	muls.l d2,d1	| _308, tmp335
	move.l d1,a6	| tmp335,
	lsl.l #2,d0	|, tmp337
	lea (28,a6,d0.l),a0	|, tmp338
	lea (-4828,a5),a6	|,, tmp341
	add.l d7,a0	| ivtmp.625, tmp340
	move.l (a0)+,(a6)+	|,
	move.l (-4828,a5),a6	| v, blk
	addq.l #1,d4	|, depth
	addq.l #8,(-5684,a5)	|, %sfp
	add.l (-5680,a5),d7	| %sfp, ivtmp.625
	subq.w #1,d5	|, ivtmp_595
	jne .L1057	|
	moveq #-3,d2	|, <retval>
	lea _bfs_bio_free_buffer,a4	|, tmp584
	jra .L1058	|
.L1151:
	move.l (4,a1),-(sp)	| _31->block_size,
	move.l a1,-(sp)	| _31,
	jsr _bfs_bio_alloc_buffer	|
	move.l d0,a3	|, _167
	addq.l #8,sp	|,
	tst.l d0	| _167
	jeq .L1051	|
	move.l (a2),a0	| MEM[(const struct bfs_btree_t *)tree_29(D)].bio, MEM[(const struct bfs_btree_t *)tree_29(D)].bio
	move.l (4,a0),-(sp)	| _161->block_size,
	move.l d3,-(sp)	| _37,
	move.l d0,-(sp)	| _167,
	jsr _memset	|
	move.l #1112821316,(a3)	|, MEM[(struct bfs_btnode_hdr_t *)_167].magic
	move.w d3,(20,a3)	| _37, MEM[(struct bfs_btnode_hdr_t *)_167].level
	move.l d3,(16,a3)	| _37, MEM[(struct bfs_btnode_hdr_t *)_167].num_keys
	move.l d3,(24,a3)	| _37, MEM[(struct bfs_btnode_hdr_t *)_167].right_sibling
	move.l (16,a5),-(sp)	| val,
	move.l d6,-(sp)	| key,
	move.l d3,-(sp)	| _37,
	move.l a3,-(sp)	| _167,
	move.l a2,-(sp)	| tree,
	jsr _leaf_insert_at	|
	lea (32,sp),sp	|,
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	jsr _mutation_alloc	|
	move.l d0,d4	|, blk
	addq.l #8,sp	|,
	jne .L1040	|
	move.l a3,-(sp)	| _167,
	move.l (a2)+,-(sp)	| MEM[(struct bfs_bio_t * *)tree_29(D)],
	jsr _bfs_bio_free_buffer	|
	move.l (a2),a1	| MEM[(struct bfs_allocator_t * *)tree_29(D) + 4B], _193
	move.l (8,a1),a0	| _193->error, _94
	addq.l #8,sp	|,
	move.l a0,d0	|,
	jeq .L1042	|
	move.l a1,-(sp)	| _193,
	jsr (a0)	| _94
	move.l d0,d2	|, <retval>
	jne .L1035	|
.L1042:
	moveq #-4,d1	|,
	move.l d1,d0	|,
	movem.l (-5728,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L1040:
	move.l a3,(-4308,a5)	| _167, buf
	move.l d3,-(sp)	| _37,
	pea (-4308,a5)	|
	move.l d0,-(sp)	| blk,
	move.l a2,-(sp)	| tree,
	jsr _node_write_image	|
	move.l d0,d2	|, <retval>
	move.l a3,-(sp)	| _167,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_29(D)],
	jsr _bfs_bio_free_buffer	|
	lea (24,sp),sp	|,
	tst.l d2	| <retval>
	jeq .L1043	|
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	jsr _mutation_abort	|
	move.l d2,d0	| <retval>,
	movem.l (-5728,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L1153:
	move.l d0,-(sp)	| _62,
	move.l a2,-(sp)	| tree,
	jsr (_mutation_headroom.part.3)	|
	move.l d0,d2	|, <retval>
	addq.l #8,sp	|,
	jne .L1035	|
	move.l (16,a2),d0	| tree_29(D)->height, _62
	jra .L1050	|
.L1043:
	move.l d4,(12,a2)	| blk, tree_29(D)->root
	addq.l #1,(60,a2)	|, tree_29(D)->generation
	moveq #1,d0	|,
	move.l d0,(16,a2)	|, tree_29(D)->height
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	jsr _mutation_commit	|
	move.l (56,a2),d1	| tree_29(D)->free_sink_err,
	move.l d1,d0	|,
	movem.l (-5728,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L1152:
	move.l (16,a5),-(sp)	| val,
	move.l d6,-(sp)	| key,
	move.l d0,-(sp)	| idx,
	move.l a3,-(sp)	| owned_leaf,
	move.l a2,-(sp)	| tree,
	jsr _leaf_insert_at	|
	addq.l #1,(60,a2)	|, tree_29(D)->generation
	clr.l d0	|
	movem.l (-5728,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L1154:
	move.l a6,d3	| blk, blk
	lea (8,a2),a0	|, tree,
	move.l d7,a6	| ivtmp.625, ivtmp.625
	move.l a0,(-5664,a5)	|, %sfp
	pea (-5640,a5)	|
	move.l d6,-(sp)	| key,
	move.l d7,-(sp)	| ivtmp.625,
	move.l a0,-(sp)	|,
	jsr _node_search.isra.12	|
	lea (16,sp),sp	|,
	tst.l (-5640,a5)	| found
	jne .L1059	|
	lea (-5348,a5),a0	|,,
	move.l a0,(-5676,a5)	|, %sfp
	moveq #7,d1	|, tmp345
.L1060:
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	dbra d1,.L1060	| tmp345,
	clr.l (a0)+	|
	clr.l (a0)+	|
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_29(D)], _233
	move.l (16,a6),d5	|, _241
	move.l (8,a2),a4	| MEM[(const struct bfs_btree_t *)tree_29(D)].ops, _236
	move.l (4,a1),a0	| _233->block_size, _234
	move.l (4,a4),(-5668,a5)	| _236->key_size, %sfp
	move.l (8,a4),d7	| _236->val_size, _238
	moveq #-28,d1	|, tmp346
	add.l a0,d1	| _234, tmp346
	move.l (-5668,a5),d2	| %sfp, tmp347
	add.l d7,d2	| _238, tmp347
	divu.l d2,d1	| tmp347, tmp349
	cmp.l d1,d5	| tmp349, _241
	jcs .L1061	|
	move.l d5,d0	| _241,
	lsr.l #1,d0	|,
	move.l d0,(-5660,a5)	|, %sfp
	move.l a0,-(sp)	| _234,
	move.l a1,-(sp)	| _233,
	jsr _bfs_bio_alloc_buffer	|
	move.l d0,a4	|, _324
	addq.l #8,sp	|,
	tst.l d0	| _324
	jeq .L1155	|
	move.l (a2),a0	| MEM[(const struct bfs_btree_t *)tree_29(D)].bio, MEM[(const struct bfs_btree_t *)tree_29(D)].bio
	move.l (4,a0),-(sp)	| _325->block_size,
	clr.l -(sp)	|
	move.l d0,-(sp)	| _324,
	jsr _memset	|
	move.l #1112821316,(a4)	|, MEM[(struct bfs_btnode_hdr_t *)_324].magic
	clr.w (20,a4)	| MEM[(struct bfs_btnode_hdr_t *)_324].level
	clr.l (16,a4)	| MEM[(struct bfs_btnode_hdr_t *)_324].num_keys
	clr.l (24,a4)	| MEM[(struct bfs_btnode_hdr_t *)_324].right_sibling
	sub.l (-5660,a5),d5	| %sfp, _241
	move.l d5,(-5656,a5)	| _241, %sfp
	lea (12,sp),sp	|,
	jeq .L1067	|
	clr.l d5	| i
	move.l #_memcpy,d0	|,
	move.l d4,(-5652,a5)	| depth, %sfp
	move.l d7,d4	| _238, _238
	move.l (-5656,a5),d7	| %sfp, right_count
	move.l d3,(-5648,a5)	| blk, %sfp
	move.l d6,(-5644,a5)	| key, %sfp
	move.l (-5660,a5),d6	| %sfp, mid
	move.l d0,(-5684,a5)	|, %sfp
.L1066:
	move.l d6,d2	| mid, _329
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_ops_t * *)tree_29(D) + 8B], MEM[(const struct bfs_btree_ops_t * *)tree_29(D) + 8B]
	add.l d5,d2	| i, _329
	move.l (4,a0),d0	| _330->key_size, _331
	move.l d0,d1	| _331, tmp361
	muls.l d5,d1	| i, tmp361
	muls.l d2,d0	| _329, tmp364
	move.l (-5668,a5),-(sp)	| %sfp,
	pea (28,a6,d0.l)	|
	pea (28,a4,d1.l)	|
	move.l (-5684,a5),a0	| %sfp,
	jsr (a0)	|
	move.l (8,a2),a1	| MEM[(const struct bfs_btree_t *)tree_29(D)].ops, _341
	move.l (4,a1),a0	| _341->key_size, _342
	move.l (8,a1),d1	| _341->val_size, _343
	moveq #-28,d0	|, tmp374
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_29(D)], MEM[(struct bfs_bio_t * *)tree_29(D)]
	add.l (4,a1),d0	| _338->block_size, tmp374
	move.l a0,d3	| _342, tmp375
	add.l d1,d3	| _343, tmp375
	divu.l d3,d0	| tmp375, tmp377
	move.l a0,d3	| _342,
	move.w #28,a0	|, keys_end
	muls.l d3,d0	|, tmp379
	add.l d0,a0	| tmp379, keys_end
	move.l d1,d0	| _343, tmp380
	muls.l d5,d0	| i, tmp380
	add.l a0,d0	| keys_end, tmp381
	muls.l d1,d2	| _343, tmp383
	add.l a0,d2	| keys_end, tmp384
	move.l d4,-(sp)	| _238,
	pea (a6,d2.l)	|
	pea (a4,d0.l)	|
	move.l (-5684,a5),a0	| %sfp,
	jsr (a0)	|
	addq.l #1,d5	|, i
	lea (24,sp),sp	|,
	cmp.l d7,d5	| right_count, i
	jne .L1066	|
	move.l (-5652,a5),d4	| %sfp, depth
	move.l (-5648,a5),d3	| %sfp, blk
	move.l (-5644,a5),d6	| %sfp, key
.L1067:
	move.l (-5656,a5),(16,a4)	| %sfp, MEM[(struct bfs_btnode_hdr_t *)_324].num_keys
	move.l (24,a6),(24,a4)	|, MEM[(struct bfs_btnode_hdr_t *)_324].right_sibling
	move.l (-5660,a5),(16,a6)	| %sfp, MEM[(struct bfs_btnode_hdr_t *)_77].num_keys
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	jsr _mutation_alloc	|
	move.l d0,d5	|, right_blk
	addq.l #8,sp	|,
	jne .L1156	|
	move.l a4,-(sp)	| _324,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_29(D)],
	lea _bfs_bio_free_buffer,a4	|, tmp584
	jsr (a4)	| tmp584
	move.l (4,a2),a1	| MEM[(struct bfs_allocator_t * *)tree_29(D) + 4B], _358
	move.l (8,a1),a0	| _358->error, _359
	addq.l #8,sp	|,
	move.l a0,d0	|,
	jeq .L1144	|
	move.l a1,-(sp)	| _358,
	jsr (a0)	| _359
	move.l d0,d2	|, <retval>
	addq.l #4,sp	|,
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_29(D)], prephitmp_570
	jne .L1058	|
.L1143:
	moveq #-4,d2	|, <retval>
	jra .L1058	|
.L1115:
	moveq #-8,d1	|,
	move.l d1,d0	|,
	movem.l (-5728,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L1059:
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_29(D)], prephitmp_570
	moveq #-6,d2	|, <retval>
	lea _bfs_bio_free_buffer,a4	|, tmp584
	jra .L1058	|
.L1061:
	move.l (16,a5),-(sp)	| val,
	move.l d6,-(sp)	| key,
	move.l d0,-(sp)	| idx,
	move.l a6,-(sp)	| ivtmp.625,
	move.l a2,-(sp)	| tree,
	jsr _leaf_insert_at	|
	lea (20,sp),sp	|,
	lea _bfs_bio_free_buffer,a4	|, tmp584
.L1075:
	pea (-5636,a5)	|
	move.l a6,-(sp)	| ivtmp.625,
	move.l d3,-(sp)	| blk,
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	jsr _cow_node	|
	move.l d0,d2	|, <retval>
	lea (20,sp),sp	|,
	jne .L1141	|
	move.l d4,d5	| depth, d
	subq.l #1,d5	|, d
	moveq #-1,d0	|,
	cmp.l d5,d0	| d,
	jeq .L1100	|
	lea (-8,d4.l*8),a0	|, tmp429
	lea (-5604,a5,a0.l),a6	|, ivtmp.609
	move.l (-5680,a5),d0	| %sfp, tmp430
	muls.l d5,d0	| d, tmp430
	add.l (-5672,a5),d0	| %sfp,
	move.l d0,(-5656,a5)	|, %sfp
	move.l a4,(-5668,a5)	| tmp584, %sfp
.L1099:
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_t *)tree_29(D)].ops, MEM[(const struct bfs_btree_t *)tree_29(D)].ops
	move.l (4,a6),d1	| MEM[base: _578, offset: 4B], ci
	move.l (4,a0),d3	| _370->key_size, _371
	moveq #-32,d0	|, tmp433
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_29(D)], MEM[(struct bfs_bio_t * *)tree_29(D)]
	add.l (4,a0),d0	| _367->block_size, tmp433
	move.l d3,d2	| _371, tmp434
	addq.l #4,d2	|, tmp434
	divu.l d2,d0	| tmp434, tmp436
	muls.l d3,d0	| _371, tmp438
	move.l d1,d2	| ci, tmp440
	move.l d0,a1	| tmp438,
	lsl.l #2,d2	|, tmp440
	lea (28,a1,d2.l),a0	|, tmp441
	move.l (-5636,a5),(-5620,a5)	| new_blk, v
	lea (-5620,a5),a1	|,, tmp443
	add.l (-5656,a5),a0	| %sfp, _378
	move.l (a1)+,(a0)+	|,
	tst.l (-5348,a5)	| split.did_split
	jeq .L1080	|
	move.l (-5656,a5),a0	| %sfp,
	move.l (16,a0),d3	|, _252
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_29(D)], _245
	move.l (8,a2),a4	| MEM[(const struct bfs_btree_t *)tree_29(D)].ops, MEM[(const struct bfs_btree_t *)tree_29(D)].ops
	move.l (4,a0),a1	| _245->block_size, _246
	move.l (4,a4),d4	| _248->key_size, _249
	moveq #-32,d0	|, tmp446
	add.l a1,d0	| _246, tmp446
	move.l d4,d2	| _249, tmp447
	addq.l #4,d2	|, tmp447
	divu.l d2,d0	| tmp447, tmp449
	cmp.l d0,d3	| tmp449, _252
	jcs .L1081	|
	move.l d3,d0	| _252,
	lsr.l #1,d0	|,
	move.l d0,(-5684,a5)	|, %sfp
	move.l a1,-(sp)	| _246,
	move.l a0,-(sp)	| _245,
	jsr _bfs_bio_alloc_buffer	|
	move.l d0,d6	|, _385
	addq.l #8,sp	|,
	jeq .L1118	|
	move.l (-5656,a5),a0	| %sfp,
	move.w (20,a0),d2	|, _386
	move.l (a2),a0	| MEM[(const struct bfs_btree_t *)tree_29(D)].bio, MEM[(const struct bfs_btree_t *)tree_29(D)].bio
	move.l (4,a0),-(sp)	| _387->block_size,
	clr.l -(sp)	|
	move.l d0,-(sp)	| _385,
	jsr _memset	|
	move.l d6,a0	| _385,
	move.l #1112821316,(a0)+	|, MEM[(struct bfs_btnode_hdr_t *)_385].magic
	move.w d2,(16,a0)	| _386, MEM[(struct bfs_btnode_hdr_t *)_385].level
	clr.l (12,a0)	| MEM[(struct bfs_btnode_hdr_t *)_385].num_keys
	clr.l (20,a0)	| MEM[(struct bfs_btnode_hdr_t *)_385].right_sibling
	move.l (-5684,a5),d0	| %sfp, tmp460
	move.l (8,a2),a1	| MEM[(const struct bfs_btree_ops_t * *)tree_29(D) + 8B], prephitmp_125
	muls.l (4,a1),d0	| _389->key_size, tmp460
	move.l d4,-(sp)	| _249,
	move.l (-5656,a5),a0	| %sfp,
	pea (28,a0,d0.l)	|
	pea (-4824,a5)	|
	lea _memcpy,a4	|, tmp589
	move.l a1,(-5688,a5)	|,
	jsr (a4)	| tmp589
	subq.l #1,d3	|, tmp469
	sub.l (-5684,a5),d3	| %sfp, right_count
	lea (24,sp),sp	|,
	move.l (-5688,a5),a1	|,
	move.l (-5684,a5),d2	| %sfp, prephitmp_564
	addq.l #1,d2	|, prephitmp_564
	tst.l d3	| right_count
	jeq .L1084	|
	clr.l d7	| i
	move.l d5,(-5660,a5)	| d, %sfp
	move.l a4,d0	| tmp589, tmp589
	move.l d6,a4	| _385, _385
	move.l a6,d6	| ivtmp.609, ivtmp.609
	move.l a2,a6	| tree, tree
	move.l d0,a2	| tmp589, tmp589
.L1086:
	move.l (4,a1),d1	| prephitmp_565->key_size, _401
	move.l d1,d5	| _401, tmp470
	muls.l d7,d5	| i, tmp470
	move.l d2,d0	| prephitmp_564, tmp473
	add.l d7,d0	| i, tmp473
	muls.l d1,d0	| _401, tmp474
	move.l d4,-(sp)	| _249,
	move.l (-5656,a5),a0	| %sfp,
	pea (28,a0,d0.l)	|
	pea (28,a4,d5.l)	|
	jsr (a2)	| tmp589
	addq.l #1,d7	|, i
	lea (12,sp),sp	|,
	cmp.l d3,d7	| right_count, i
	jeq .L1085	|
	move.l (8,a6),a1	| MEM[(const struct bfs_btree_t *)tree_29(D)].ops, prephitmp_125
	jra .L1086	|
.L1093:
	pea (-5608,a5)	|
	move.l d4,-(sp)	| tmp512,
	move.l (-5656,a5),-(sp)	| %sfp,
	move.l (-5664,a5),-(sp)	| %sfp,
	jsr _node_search.isra.12	|
	move.l (-4832,a5),-(sp)	| split.new_right,
	move.l d4,-(sp)	| tmp512,
	move.l d0,-(sp)	|,
	move.l (-5656,a5),-(sp)	| %sfp,
	move.l a2,-(sp)	| tree,
	jsr _internal_insert_at	|
	lea (36,sp),sp	|,
.L1096:
	lea (-4828,a5),a1	|,, tmp534
	lea (-5348,a5),a0	|,,
	move.l a0,(-5676,a5)	|, %sfp
	moveq #7,d0	|, tmp536
	move.l (-5656,a5),d1	| %sfp, ivtmp.611
.L1097:
	move.l (a1)+,(a0)+	|,
	move.l (a1)+,(a0)+	|,
	move.l (a1)+,(a0)+	|,
	move.l (a1)+,(a0)+	|,
	move.l (a1)+,(a0)+	|,
	move.l (a1)+,(a0)+	|,
	move.l (a1)+,(a0)+	|,
	move.l (a1)+,(a0)+	|,
	move.l (a1)+,(a0)+	|,
	move.l (a1)+,(a0)+	|,
	move.l (a1)+,(a0)+	|,
	move.l (a1)+,(a0)+	|,
	move.l (a1)+,(a0)+	|,
	move.l (a1)+,(a0)+	|,
	move.l (a1)+,(a0)+	|,
	move.l (a1)+,(a0)+	|,
	dbra d0,.L1097	| tmp536,
	move.l d1,(-5656,a5)	| ivtmp.611, %sfp
	move.l (a1)+,(a0)+	|,
	move.l (a1)+,(a0)+	|,
.L1080:
	pea (-5636,a5)	|
	move.l (-5656,a5),-(sp)	| %sfp,
	move.l (a6),-(sp)	| MEM[base: _578, offset: 0B],
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	jsr _cow_node	|
	move.l d0,d2	|, <retval>
	lea (20,sp),sp	|,
	jne .L1146	|
	move.l (-5680,a5),d0	| %sfp,
	subq.l #8,a6	|, ivtmp.609
	sub.l d0,(-5656,a5)	|, %sfp
	dbra d5,.L1099	| d,
	clr.w d5	| d
	subq.l #1,d5	| d
	jcc .L1099	|
	move.l (-5668,a5),a4	| %sfp, tmp584
.L1100:
	tst.l (-5348,a5)	| split.did_split
	jne .L1078	|
	move.l (-5636,a5),(12,a2)	| new_blk, tree_29(D)->root
	addq.l #1,(60,a2)	|, tree_29(D)->generation
	move.l (-5672,a5),-(sp)	| %sfp,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_29(D)],
	jsr (a4)	| tmp584
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	jsr _mutation_commit	|
	move.l (56,a2),d1	| tree_29(D)->free_sink_err,
.L1161:
	move.l d1,d0	|,
	movem.l (-5728,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L1081:
	move.l (-4832,a5),-(sp)	| split.new_right,
	move.l (-5676,a5),a0	| %sfp,
	pea (4,a0)	|
	move.l d1,-(sp)	| ci,
	move.l (-5656,a5),-(sp)	| %sfp,
	move.l a2,-(sp)	| tree,
	jsr _internal_insert_at	|
	clr.l (-5348,a5)	| split.did_split
	lea (20,sp),sp	|,
	jra .L1080	|
.L1085:
	move.l (-5660,a5),d5	| %sfp, d
	move.l a6,a2	| tree, tree
	move.l d6,a6	| ivtmp.609, ivtmp.609
	move.l a4,d6	| _385, _385
	move.l (8,a2),a1	| MEM[(const struct bfs_btree_t *)tree_29(D)].ops, prephitmp_125
.L1084:
	move.l d6,a0	| _385,
	move.l d3,(16,a0)	| right_count, MEM[(struct bfs_btnode_hdr_t *)_385].num_keys
	lsl.l #2,d2	|, ivtmp.593
	clr.l d1	| i
.L1088:
	move.l (4,a1),d7	| prephitmp_125->key_size, _439
	moveq #-32,d0	|, tmp484
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_29(D)], MEM[(struct bfs_bio_t * *)tree_29(D)]
	add.l (4,a0),d0	| _435->block_size, tmp484
	move.l d7,d4	| _439, tmp485
	addq.l #4,d4	|, tmp485
	divu.l d4,d0	| tmp485, tmp487
	move.w #28,a0	|, keys_end
	muls.l d7,d0	| _439, tmp489
	add.l d0,a0	| tmp489, keys_end
	move.l a0,d0	| keys_end, tmp491
	add.l d2,d0	| ivtmp.593, tmp491
	add.l (-5656,a5),d0	| %sfp, tmp491
	lea (-5608,a5),a4	|,, tmp494
	move.l d0,a1	| tmp491, tmp493
	move.l (a1)+,(a4)+	|,
	move.l (-5608,a5),(-5612,a5)	| v, v
	move.l d1,d0	| i, tmp495
	lsl.l #2,d0	|, tmp495
	add.l a0,d0	| keys_end, tmp496
	add.l d6,d0	| _385, tmp496
	move.l d0,a0	| tmp496, tmp500
	lea (-5612,a5),a1	|,, tmp499
	move.l (a1)+,(a0)+	|,
	addq.l #1,d1	|, i
	addq.l #4,d2	|, ivtmp.593
	cmp.l d3,d1	| right_count, i
	jhi .L1087	|
	move.l (8,a2),a1	| MEM[(const struct bfs_btree_t *)tree_29(D)].ops, prephitmp_125
	jra .L1088	|
.L1087:
	move.l (-5656,a5),a0	| %sfp,
	move.l (-5684,a5),(16,a0)	| %sfp, MEM[(struct bfs_btnode_hdr_t *)node_122].num_keys
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	jsr _mutation_alloc	|
	move.l d0,d3	|, right_blk
	addq.l #8,sp	|,
	jne .L1089	|
	move.l (-5668,a5),a4	| %sfp, tmp584
	move.l d6,-(sp)	| _385,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_29(D)],
	jsr (a4)	| tmp584
	move.l (4,a2),a1	| MEM[(struct bfs_allocator_t * *)tree_29(D) + 4B], _416
	move.l (8,a1),a0	| _416->error, _417
	addq.l #8,sp	|,
	move.l a0,d0	|,
	jeq .L1091	|
	move.l a1,-(sp)	| _416,
	jsr (a0)	| _417
	move.l d0,d2	|, <retval>
	addq.l #4,sp	|,
	jeq .L1091	|
.L1141:
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_29(D)], prephitmp_570
	jra .L1058	|
.L1091:
	moveq #-4,d2	|, <retval>
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_29(D)], prephitmp_570
	jra .L1058	|
.L1089:
	move.l d6,(-5616,a5)	| _385, buf
	clr.l -(sp)	|
	pea (-5616,a5)	|
	move.l d0,-(sp)	| right_blk,
	move.l a2,-(sp)	| tree,
	lea _node_write_image,a4	|, tmp505
	jsr (a4)	| tmp505
	move.l d0,d2	|, <retval>
	lea (16,sp),sp	|,
	jne .L1157	|
	move.l d3,(-4312,a5)	| right_blk, parent_split.new_right
	moveq #1,d0	|,
	move.l d0,(-4828,a5)	|, parent_split.did_split
	move.l d6,-(sp)	| _385,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_29(D)],
	move.l (-5668,a5),a0	| %sfp,
	jsr (a0)	|
	move.l (8,a2),a0	| tree_29(D)->ops, tree_29(D)->ops
	pea (-4824,a5)	|
	move.l (-5676,a5),d4	| %sfp, tmp512
	addq.l #4,d4	|, tmp512
	move.l d4,-(sp)	| tmp512,
	move.l (a0),a0	| _137->key_compare, _137->key_compare
	jsr (a0)	| _137->key_compare
	lea (16,sp),sp	|,
	tst.l d0	|
	jlt .L1093	|
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_29(D)], _199
	move.l (4,a0),-(sp)	| _199->block_size,
	move.l a0,-(sp)	| _199,
	jsr _bfs_bio_alloc_buffer	|
	move.l d0,d3	|, _254
	addq.l #8,sp	|,
	jeq .L1118	|
	move.l d0,-(sp)	| _254,
	move.l (-4312,a5),-(sp)	| parent_split.new_right,
	move.l a2,-(sp)	| tree,
	jsr (a3)	| tmp603
	move.l d0,d2	|, <retval>
	lea (12,sp),sp	|,
	jne .L1158	|
	pea (-5608,a5)	|
	move.l d4,-(sp)	| tmp512,
	move.l d3,-(sp)	| _254,
	move.l (-5664,a5),-(sp)	| %sfp,
	jsr _node_search.isra.12	|
	move.l (-4832,a5),-(sp)	| split.new_right,
	move.l d4,-(sp)	| tmp512,
	move.l d0,-(sp)	|,
	move.l d3,-(sp)	| _254,
	move.l a2,-(sp)	| tree,
	jsr _internal_insert_at	|
	move.l d3,(-5628,a5)	| _254, buf
	lea (32,sp),sp	|,
	move.l d2,(sp)	| <retval>,
	pea (-5628,a5)	|
	move.l (-4312,a5),-(sp)	| parent_split.new_right,
	move.l a2,-(sp)	| tree,
	jsr (a4)	| tmp505
	move.l d0,d2	|, <retval>
	move.l d3,-(sp)	| _254,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_29(D)],
	move.l (-5668,a5),a1	| %sfp,
	jsr (a1)	|
	lea (24,sp),sp	|,
	tst.l d2	| <retval>
	jeq .L1096	|
.L1146:
	move.l (-5668,a5),a4	| %sfp, tmp584
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_29(D)], prephitmp_570
	jra .L1058	|
.L1157:
	move.l (-5668,a5),a4	| %sfp, tmp584
	move.l d6,-(sp)	| _385,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_29(D)],
	jsr (a4)	| tmp584
	addq.l #8,sp	|,
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_29(D)], prephitmp_570
	jra .L1058	|
.L1051:
	moveq #-2,d1	|,
	move.l d1,d0	|,
	movem.l (-5728,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L1156:
	move.l d0,(24,a6)	| right_blk, MEM[(struct bfs_btnode_hdr_t *)_77].right_sibling
	move.l a4,(-5624,a5)	| _324, buf
	clr.l -(sp)	|
	pea (-5624,a5)	|
	move.l d0,-(sp)	| right_blk,
	move.l a2,-(sp)	| tree,
	move.l #_node_write_image,d7	|, tmp394
	move.l d7,a1	| tmp394,
	jsr (a1)	|
	move.l d0,d2	|, <retval>
	lea (16,sp),sp	|,
	jne .L1159	|
	move.l (-5668,a5),-(sp)	| %sfp,
	pea (28,a4)	|
	move.l (-5676,a5),d2	| %sfp, tmp397
	addq.l #4,d2	|, tmp397
	move.l d2,-(sp)	| tmp397,
	jsr _memcpy	|
	move.l d5,(-4832,a5)	| right_blk, split.new_right
	moveq #1,d0	|,
	move.l d0,(-5348,a5)	|, split.did_split
	move.l a4,-(sp)	| _324,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_29(D)],
	lea _bfs_bio_free_buffer,a4	|, tmp584
	jsr (a4)	| tmp584
	move.l (8,a2),a0	| tree_29(D)->ops, tree_29(D)->ops
	move.l d2,-(sp)	| tmp397,
	move.l d6,-(sp)	| key,
	move.l (a0),d0	| _97->key_compare, _97->key_compare
	move.l d0,a0	| _97->key_compare,
	jsr (a0)	|
	lea (28,sp),sp	|,
	tst.l d0	|
	jlt .L1071	|
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_29(D)], _198
	move.l (4,a0),-(sp)	| _198->block_size,
	move.l a0,-(sp)	| _198,
	jsr _bfs_bio_alloc_buffer	|
	move.l d0,d5	|, _243
	addq.l #8,sp	|,
	jeq .L1145	|
	move.l d0,-(sp)	| _243,
	move.l (-4832,a5),-(sp)	| split.new_right,
	move.l a2,-(sp)	| tree,
	jsr (a3)	| tmp603
	move.l d0,d2	|, <retval>
	lea (12,sp),sp	|,
	jeq .L1073	|
	move.l d5,-(sp)	| _243,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_29(D)],
	jsr (a4)	| tmp584
	addq.l #8,sp	|,
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_29(D)], prephitmp_570
	jra .L1058	|
.L1159:
	move.l a4,-(sp)	| _324,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_29(D)],
	lea _bfs_bio_free_buffer,a4	|, tmp584
	jsr (a4)	| tmp584
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_29(D)], prephitmp_570
	addq.l #8,sp	|,
	jra .L1058	|
.L1118:
	move.l (-5668,a5),a4	| %sfp, tmp584
.L1145:
	moveq #-2,d2	|, <retval>
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_29(D)], prephitmp_570
	jra .L1058	|
.L1073:
	pea (-4828,a5)	|
	move.l d6,-(sp)	| key,
	move.l d5,-(sp)	| _243,
	move.l (-5664,a5),-(sp)	| %sfp,
	jsr _node_search.isra.12	|
	lea (16,sp),sp	|,
	tst.l (-4828,a5)	| f2
	jeq .L1074	|
	move.l d5,-(sp)	| _243,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_29(D)],
	jsr (a4)	| tmp584
	addq.l #8,sp	|,
	moveq #-6,d2	|, <retval>
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_29(D)], prephitmp_570
	jra .L1058	|
.L1071:
	pea (-4828,a5)	|
	move.l d6,-(sp)	| key,
	move.l a6,-(sp)	| ivtmp.625,
	move.l (-5664,a5),-(sp)	| %sfp,
	jsr _node_search.isra.12	|
	move.l (16,a5),-(sp)	| val,
	move.l d6,-(sp)	| key,
	move.l d0,-(sp)	|,
	move.l a6,-(sp)	| ivtmp.625,
	move.l a2,-(sp)	| tree,
	jsr _leaf_insert_at	|
	lea (36,sp),sp	|,
	jra .L1075	|
.L1078:
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_29(D)], _200
	move.l (4,a0),-(sp)	| _200->block_size,
	move.l a0,-(sp)	| _200,
	jsr _bfs_bio_alloc_buffer	|
	move.l d0,a3	|, _257
	addq.l #8,sp	|,
	tst.l d0	| _257
	jeq .L1160	|
	move.l (-5672,a5),a0	| %sfp,
	move.w (20,a0),d2	|, _162
	move.l (a2),a0	| MEM[(const struct bfs_btree_t *)tree_29(D)].bio, MEM[(const struct bfs_btree_t *)tree_29(D)].bio
	addq.w #1,d2	|, _162
	move.l (4,a0),-(sp)	| _259->block_size,
	clr.l -(sp)	|
	move.l d0,-(sp)	| _257,
	jsr _memset	|
	move.l #1112821316,(a3)	|, MEM[(struct bfs_btnode_hdr_t *)_257].magic
	move.w d2,(20,a3)	| _162, MEM[(struct bfs_btnode_hdr_t *)_257].level
	clr.l (16,a3)	| MEM[(struct bfs_btnode_hdr_t *)_257].num_keys
	clr.l (24,a3)	| MEM[(struct bfs_btnode_hdr_t *)_257].right_sibling
	move.l (-5636,a5),-(sp)	| new_blk,
	clr.l -(sp)	|
	move.l a3,-(sp)	| _257,
	move.l a2,-(sp)	| tree,
	lea _set_child,a6	|, tmp550
	jsr (a6)	| tmp550
	move.l (8,a2),a0	| tree_29(D)->ops, tree_29(D)->ops
	move.l (4,a0),-(sp)	| _165->key_size,
	move.l (-5676,a5),a0	| %sfp,
	pea (4,a0)	|
	pea (28,a3)	|
	jsr _memcpy	|
	lea (36,sp),sp	|,
	move.l (-4832,a5),(sp)	| split.new_right,
	pea 1.w	|
	move.l a3,-(sp)	| _257,
	move.l a2,-(sp)	| tree,
	jsr (a6)	| tmp550
	moveq #1,d0	|,
	move.l d0,(16,a3)	|, MEM[(struct bfs_btnode_hdr_t *)_257].num_keys
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	jsr _mutation_alloc	|
	move.l d0,d3	|, root_blk
	lea (24,sp),sp	|,
	jne .L1102	|
	move.l a3,-(sp)	| _257,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_29(D)],
	jsr (a4)	| tmp584
	move.l (4,a2),a0	| MEM[(struct bfs_allocator_t * *)tree_29(D) + 4B], _194
	move.l (8,a0),d0	| _194->error, _262
	addq.l #8,sp	|,
	jeq .L1144	|
	move.l a0,-(sp)	| _194,
	move.l d0,a0	| _262,
	jsr (a0)	|
	move.l d0,d2	|, <retval>
	addq.l #4,sp	|,
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_29(D)], prephitmp_570
	jne .L1058	|
	jra .L1143	|
.L1074:
	move.l (16,a5),-(sp)	| val,
	move.l d6,-(sp)	| key,
	move.l d0,-(sp)	| idx2,
	move.l d5,-(sp)	| _243,
	move.l a2,-(sp)	| tree,
	jsr _leaf_insert_at	|
	move.l d5,(-5632,a5)	| _243, buf
	move.l d2,-(sp)	| <retval>,
	pea (-5632,a5)	|
	move.l (-4832,a5),-(sp)	| split.new_right,
	move.l a2,-(sp)	| tree,
	move.l d7,a1	| tmp394,
	jsr (a1)	|
	lea (32,sp),sp	|,
	move.l d0,d2	|, <retval>
	move.l d5,(sp)	| _243,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_29(D)],
	jsr (a4)	| tmp584
	addq.l #8,sp	|,
	tst.l d2	| <retval>
	jne .L1141	|
	jra .L1075	|
.L1158:
	move.l (-5668,a5),a4	| %sfp, tmp584
	move.l d3,-(sp)	| _254,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_29(D)],
	jsr (a4)	| tmp584
	addq.l #8,sp	|,
	jra .L1141	|
.L1155:
	move.l (a2),a1	| MEM[(const struct bfs_btree_t *)tree_29(D)].bio, prephitmp_570
	moveq #-2,d2	|, <retval>
	lea _bfs_bio_free_buffer,a4	|, tmp584
	jra .L1058	|
.L1102:
	move.l a3,(-4828,a5)	| _257, buf
	clr.l -(sp)	|
	pea (-4828,a5)	|
	move.l d0,-(sp)	| root_blk,
	move.l a2,-(sp)	| tree,
	jsr _node_write_image	|
	move.l d0,d2	|, <retval>
	move.l a3,-(sp)	| _257,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_29(D)],
	jsr (a4)	| tmp584
	lea (24,sp),sp	|,
	tst.l d2	| <retval>
	jne .L1141	|
	addq.l #1,(16,a2)	|, tree_29(D)->height
	move.l d3,(12,a2)	| root_blk, tree_29(D)->root
	addq.l #1,(60,a2)	|, tree_29(D)->generation
	move.l (-5672,a5),-(sp)	| %sfp,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_29(D)],
	jsr (a4)	| tmp584
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	jsr _mutation_commit	|
	move.l (56,a2),d1	| tree_29(D)->free_sink_err,
	jra .L1161	|
.L1160:
	move.l (a2),a1	| MEM[(const struct bfs_btree_t *)tree_29(D)].bio, prephitmp_570
	moveq #-2,d2	|, <retval>
	jra .L1058	|
.L1144:
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_29(D)], prephitmp_570
	moveq #-4,d2	|, <retval>
	jra .L1058	|
	.align	2
	.globl	_bfs_btree_update
_bfs_btree_update:
	link.w a5,#-4584	|,
	movem.l a6/a4/a3/a2/d7/d6/d5/d4/d3/d2,-(sp)	|,
	move.l (8,a5),a2	| tree, tree
	move.l a2,d0	|,
	jeq .L1194	|
	move.l (a2),a1	| tree_19(D)->bio,
	move.l a1,d0	|,
	jeq .L1194	|
	tst.l (4,a2)	| tree_19(D)->alloc
	jeq .L1194	|
	tst.l (12,a5)	| key
	jeq .L1194	|
	tst.l (16,a5)	| new_val
	jeq .L1194	|
	clr.l (56,a2)	| tree_19(D)->free_sink_err
	lea (-3276,a5),a0	|,, tmp157
	moveq #50,d0	|, tmp158
.L1164:
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	dbra d0,.L1164	| tmp158,
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	move.l (12,a2),d0	| tree_19(D)->root, _27
	jeq .L1195	|
	moveq #-3,d4	|,
	cmp.l (8,a1),d0	| _21->block_count, _27
	jcc .L1162	|
	move.l (16,a2),d0	| MEM[(const struct bfs_btree_t *)tree_19(D)].height, tmp159
	subq.l #1,d0	|, tmp159
	moveq #31,d1	|,
	cmp.l d0,d1	| tmp159,
	jcs .L1162	|
	lea (-4308,a5),a0	|,, tmp254
	moveq #15,d0	|, tmp255
.L1185:
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	dbra d0,.L1185	| tmp255,
	clr.l (a0)+	|
	clr.l (a0)+	|
	pea (-4308,a5)	|
	move.l (12,a5),-(sp)	| key,
	move.l a2,-(sp)	| tree,
	jsr _owned_leaf_in_place	|
	move.l d0,d2	|, owned_leaf
	lea (12,sp),sp	|,
	jeq .L1167	|
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_ops_t * *)tree_19(D) + 8B], MEM[(const struct bfs_btree_ops_t * *)tree_19(D) + 8B]
	move.l (16,a0),a0	| _102->entry_ok, _114
	move.l a0,d0	|,
	jeq .L1166	|
	move.l (16,a5),-(sp)	| new_val,
	move.l (12,a5),-(sp)	| key,
	jsr (a0)	| _114
	addq.l #8,sp	|,
	tst.l d0	|
	jne .L1166	|
.L1167:
	move.l (16,a2),d0	| tree_19(D)->height, _41
	tst.l (36,a2)	| MEM[(const struct bfs_btree_t *)tree_19(D)].free_sink.defer
	jeq .L1169	|
	tst.l d0	| _41
	jne .L1216	|
	move.l (a2),a0	| tree_19(D)->bio, _186
	move.l (4,a0),(-4580,a5)	| _186->block_size, %sfp
	moveq #2,d0	|, _41
.L1171:
	muls.l (-4580,a5),d0	| %sfp, _41
	move.l d0,-(sp)	| _41,
	move.l a0,-(sp)	| _186,
	jsr _bfs_bio_alloc_buffer	|
	move.l d0,d6	|, node_bufs
	addq.l #8,sp	|,
	jeq .L1198	|
	move.l a5,d1	|, ivtmp.647
	move.l (12,a2),a1	| tree_19(D)->root,
	add.l #-4564,d1	|, ivtmp.647
	moveq #32,d5	|, ivtmp_2
	clr.l d4	| d
	lea _node_read,a4	|, tmp271
	lea (8,a2),a0	|, tree, tmp273
	move.l a1,d2	|, blk
	move.l d0,d3	| ivtmp.648, ivtmp.648
	move.l d1,a3	| ivtmp.647, ivtmp.647
	move.l a0,(-4584,a5)	| tmp273, %sfp
.L1172:
	move.l (16,a2),a6	| tree_19(D)->height, _49
	cmp.l a6,d4	| _49, d
	jcc .L1217	|
	move.l d3,d7	| ivtmp.648, _53
	sub.l d6,d7	| node_bufs, _53
	move.l d3,-(sp)	| ivtmp.648,
	move.l d2,-(sp)	| blk,
	move.l a2,-(sp)	| tree,
	jsr (a4)	| tmp271
	lea (12,sp),sp	|,
	tst.l d0	| <retval>
	jne .L1209	|
	move.w a6,d1	|, tmp185
	subq.w #1,d1	|, tmp185
	sub.w d4,d1	| d, _54
	move.l d3,a0	| ivtmp.648,
	cmp.w (20,a0),d1	|, _54
	jeq .L1176	|
	moveq #-3,d4	|,
	move.l d6,-(sp)	| node_bufs,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_19(D)],
	jsr _bfs_bio_free_buffer	|
.L1162:
	move.l d4,d0	|,
	movem.l (-4624,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L1166:
	pea (-4564,a5)	|
	move.l (12,a5),-(sp)	| key,
	move.l d2,-(sp)	| owned_leaf,
	pea (8,a2)	|
	jsr _node_search.isra.12	|
	lea (16,sp),sp	|,
	moveq #-5,d4	|,
	tst.l (-4564,a5)	| found
	jeq .L1162	|
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_t *)tree_19(D)].ops, _120
	move.l (4,a0),d5	| _120->key_size, _121
	move.l (8,a0),d3	| _120->val_size, _122
	moveq #-28,d1	|, tmp162
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_19(D)], MEM[(struct bfs_bio_t * *)tree_19(D)]
	add.l (4,a0),d1	| _117->block_size, tmp162
	move.l d5,d4	| _121, tmp163
	add.l d3,d4	| _122, tmp163
	divu.l d4,d1	| tmp163, tmp165
	muls.l d5,d1	| _121, tmp167
	move.l d1,a1	| tmp167,
	muls.l d3,d0	| _122, tmp169
	lea (28,a1,d0.l),a0	|, tmp170
	add.l a0,d2	| tmp170, _129
	move.l d3,-(sp)	| _122,
	move.l (16,a5),-(sp)	| new_val,
	move.l d2,-(sp)	| _129,
	jsr _memcmp	|
	lea (12,sp),sp	|,
	clr.l d4	|
	tst.l d0	|
	jeq .L1162	|
	move.l d3,-(sp)	| _122,
	move.l (16,a5),-(sp)	| new_val,
	move.l d2,-(sp)	| _129,
	jsr _memcpy	|
	addq.l #1,(60,a2)	|, tree_19(D)->generation
	move.l d4,d0	|,
	movem.l (-4624,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L1176:
	move.l d2,(a3)	| blk, MEM[base: _83, offset: 0B]
	tst.w d1	| _54
	jeq .L1218	|
	pea (-4572,a5)	|
	move.l (12,a5),-(sp)	| key,
	move.l d3,-(sp)	| ivtmp.648,
	move.l (-4584,a5),-(sp)	| %sfp,
	jsr _node_search.isra.12	|
	lea (16,sp),sp	|,
	tst.l (-4572,a5)	| found
	jeq .L1177	|
	addq.l #1,d0	|, idx
.L1177:
	move.l d0,(4,a3)	| idx, MEM[base: _83, offset: 4B]
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_19(D)], _159
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_t *)tree_19(D)].ops, MEM[(const struct bfs_btree_t *)tree_19(D)].ops
	move.l (4,a0),d7	| _162->key_size, _163
	moveq #-32,d1	|, tmp191
	add.l (4,a1),d1	| _159->block_size, tmp191
	move.l d7,d2	| _163, tmp192
	addq.l #4,d2	|, tmp192
	divu.l d2,d1	| tmp192, tmp194
	muls.l d7,d1	| _163, tmp196
	move.l d1,a6	| tmp196,
	lsl.l #2,d0	|, tmp198
	lea (28,a6,d0.l),a0	|, tmp199
	lea (-4568,a5),a6	|,, tmp202
	add.l d3,a0	| ivtmp.648, tmp201
	move.l (a0)+,(a6)+	|,
	move.l (-4568,a5),d2	| v, blk
	addq.l #1,d4	|, d
	addq.l #8,a3	|, ivtmp.647
	add.l (-4580,a5),d3	| %sfp, ivtmp.648
	subq.w #1,d5	|, ivtmp_2
	jne .L1172	|
	move.l d6,-(sp)	| node_bufs,
	move.l a1,-(sp)	| _159,
	jsr _bfs_bio_free_buffer	|
	moveq #-3,d1	|,
	move.l d1,d0	|,
	movem.l (-4624,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L1209:
	move.l d0,d4	| <retval>,
	move.l d6,-(sp)	| node_bufs,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_19(D)],
	jsr _bfs_bio_free_buffer	|
	jra .L1162	|
.L1216:
	move.l d0,-(sp)	| _41,
	move.l a2,-(sp)	| tree,
	jsr (_mutation_headroom.part.3)	|
	move.l d0,d4	|,
	addq.l #8,sp	|,
	jne .L1162	|
	move.l (16,a2),d0	| tree_19(D)->height, _41
.L1169:
	move.l (a2),a0	| tree_19(D)->bio, _186
	move.l (4,a0),(-4580,a5)	| _42->block_size, %sfp
	tst.l d0	| _41
	jne .L1171	|
	moveq #2,d0	|, _41
	jra .L1171	|
.L1217:
	move.l d6,-(sp)	| node_bufs,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_19(D)],
	jsr _bfs_bio_free_buffer	|
	moveq #-3,d1	|,
	move.l d1,d0	|,
	movem.l (-4624,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L1218:
	move.l d2,a3	| blk, blk
	move.l d7,(-4584,a5)	| _53, %sfp
	move.l d3,a6	| ivtmp.648, ivtmp.648
	move.l d0,d7	| <retval>, <retval>
	pea (-4576,a5)	|
	move.l (12,a5),-(sp)	| key,
	move.l d3,-(sp)	| ivtmp.648,
	pea (8,a2)	|
	jsr _node_search.isra.12	|
	lea (16,sp),sp	|,
	tst.l (-4576,a5)	| found
	jeq .L1219	|
	move.l (8,a2),a0	| tree_19(D)->ops, _71
	move.l (8,a0),d5	| _71->val_size, _72
	move.l (a2),a4	| MEM[(struct bfs_bio_t * *)tree_19(D)], _137
	move.l (4,a0),d1	| _71->key_size, _140
	moveq #-28,d3	|, tmp207
	move.l d5,a0	| _72, tmp208
	add.l d1,a0	| _140, tmp208
	add.l (4,a4),d3	| _137->block_size, tmp207
	move.l a0,d2	| tmp208,
	divu.l d2,d3	|, tmp210
	move.l (-4584,a5),a1	| %sfp,
	muls.l d1,d3	| _140, tmp212
	lea (28,a1,d3.l),a0	|, tmp214
	muls.l d5,d0	| _72, tmp215
	move.l a0,d2	| tmp214, tmp216
	add.l d0,d2	| tmp215, tmp216
	add.l d6,d2	| node_bufs, _147
	move.l d5,-(sp)	| _72,
	move.l (16,a5),-(sp)	| new_val,
	move.l d2,-(sp)	| _147,
	jsr _memcmp	|
	lea (12,sp),sp	|,
	tst.l d0	|
	jeq .L1220	|
	move.l d5,-(sp)	| _72,
	move.l (16,a5),-(sp)	| new_val,
	move.l d2,-(sp)	| _147,
	jsr _memcpy	|
	move.l a5,d3	|, tmp268
	add.l #-4572,d3	|, tmp268
	move.l d3,-(sp)	| tmp268,
	move.l a6,-(sp)	| ivtmp.648,
	move.l a3,-(sp)	| blk,
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	lea _cow_node,a4	|, tmp266
	jsr (a4)	| tmp266
	move.l d0,d2	|,
	lea (32,sp),sp	|,
	jne .L1181	|
	move.l d4,d5	| d, i
	subq.l #1,d5	|, i
	moveq #-1,d0	|,
	cmp.l d5,d0	| i,
	jeq .L1184	|
	lea (-8,d4.l*8),a6	|, tmp234
	lea (-4564,a5,a6.l),a6	|, ivtmp.635
	move.l (-4580,a5),d4	| %sfp, tmp235
	muls.l d5,d4	| i, tmp235
	add.l d6,d4	| node_bufs, ivtmp.637
	move.l (-4580,a5),d2	| %sfp, bs
	move.l d6,a3	| node_bufs, node_bufs
.L1183:
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_t *)tree_19(D)].ops, MEM[(const struct bfs_btree_t *)tree_19(D)].ops
	move.l (4,a6),d1	| MEM[base: _146, offset: 4B], _79
	move.l (4,a0),d6	| _175->key_size, _176
	moveq #-32,d0	|, tmp238
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_19(D)],
	add.l (4,a0),d0	| _172->block_size, tmp238
	move.l d6,d7	| _176, tmp239
	addq.l #4,d7	|, tmp239
	divu.l d7,d0	| tmp239, tmp241
	muls.l d6,d0	| _176, tmp243
	move.l d0,a1	| tmp243,
	lsl.l #2,d1	|, tmp245
	lea (28,a1,d1.l),a0	|, tmp246
	move.l (-4572,a5),(-4568,a5)	| new_blk, v
	add.l d4,a0	| ivtmp.637, _183
	lea (-4568,a5),a1	|,, tmp248
	move.l (a1)+,(a0)+	|,
	move.l d3,-(sp)	| tmp268,
	move.l d4,-(sp)	| ivtmp.637,
	move.l (a6),-(sp)	| MEM[base: _146, offset: 0B],
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	jsr (a4)	| tmp266
	lea (20,sp),sp	|,
	tst.l d0	| <retval>
	jne .L1210	|
	subq.l #8,a6	|, ivtmp.635
	sub.l d2,d4	| bs, ivtmp.637
	dbra d5,.L1183	| i,
	clr.w d5	| i
	subq.l #1,d5	| i
	jcc .L1183	|
	move.l a3,d6	| node_bufs, node_bufs
.L1184:
	move.l (-4572,a5),(12,a2)	| new_blk, tree_19(D)->root
	addq.l #1,(60,a2)	|, tree_19(D)->generation
	move.l d6,-(sp)	| node_bufs,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_19(D)],
	jsr _bfs_bio_free_buffer	|
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	jsr _mutation_commit	|
	move.l (56,a2),d1	| tree_19(D)->free_sink_err,
	move.l d1,d0	|,
	movem.l (-4624,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L1210:
	move.l a3,d6	| node_bufs, node_bufs
	move.l d0,d2	| <retval>,
.L1181:
	move.l d6,-(sp)	| node_bufs,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_19(D)],
	jsr _bfs_bio_free_buffer	|
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	jsr _mutation_abort	|
	move.l d2,d0	|,
	movem.l (-4624,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L1195:
	moveq #-5,d1	|,
	move.l d1,d0	|,
	movem.l (-4624,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L1194:
	moveq #-8,d1	|,
	move.l d1,d0	|,
	movem.l (-4624,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L1220:
	move.l d6,-(sp)	| node_bufs,
	move.l a4,-(sp)	| _137,
	jsr _bfs_bio_free_buffer	|
	move.l d7,d0	| <retval>,
	movem.l (-4624,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L1198:
	moveq #-2,d1	|,
	move.l d1,d0	|,
	movem.l (-4624,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L1219:
	move.l d6,-(sp)	| node_bufs,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_19(D)],
	jsr _bfs_bio_free_buffer	|
	moveq #-5,d1	|,
	move.l d1,d0	|,
	movem.l (-4624,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
	.align	2
_rekey_commit_path:
	subq.l #8,sp	|,
	movem.l a6/a5/a4/a3/a2/d7/d6/d5/d4/d3/d2,-(sp)	|,
	lea (56,sp),a0	|,, tmp99
	move.l (a0)+,a4	| tree, tree
	move.l (a0)+,d4	| mutation, mutation
	move.l (a0)+,a3	| path, path
	move.l (a0)+,a5	| node_bufs, node_bufs
	move.l (a0),d6	| depth, depth
	move.l (a4),a0	| tree_6(D)->bio, tree_6(D)->bio
	move.l (4,a0),d3	| _7->block_size, block_size
	move.l d3,d2	| block_size, _11
	muls.l d6,d2	| depth, _11
	move.l d6,d5	| depth,
	lsl.l #3,d5	|,
	pea (44,sp)	|
	pea (a5,d2.l)	|
	move.l (a3,d5.l),-(sp)	| _16->blk,
	move.l d4,-(sp)	| mutation,
	move.l a4,-(sp)	| tree,
	lea _cow_node,a2	|, tmp98
	jsr (a2)	| tmp98
	move.l d0,a6	|, <retval>
	lea (20,sp),sp	|,
	tst.l d0	| <retval>
	jne .L1221	|
	subq.l #1,d6	|, level
	jmi .L1225	|
	sub.l d3,d2	| block_size, tmp78
	lea (-8,a3,d5.l),a3	|, ivtmp.659
	add.l a5,d2	| node_bufs, ivtmp.661
.L1224:
	move.l (8,a4),a0	| MEM[(const struct bfs_btree_t *)tree_6(D)].ops, MEM[(const struct bfs_btree_t *)tree_6(D)].ops
	move.l (4,a3),d1	| MEM[base: _73, offset: 4B], _28
	move.l (4,a0),d5	| _43->key_size, _44
	moveq #-32,d0	|, tmp81
	move.l (a4),a0	| MEM[(struct bfs_bio_t * *)tree_6(D)],
	add.l (4,a0),d0	| _40->block_size, tmp81
	move.l d5,d7	| _44, tmp82
	addq.l #4,d7	|, tmp82
	divu.l d7,d0	| tmp82, tmp84
	muls.l d5,d0	| _44, tmp86
	move.l d0,a1	| tmp86,
	lsl.l #2,d1	|, tmp88
	lea (28,a1,d1.l),a0	|, tmp89
	move.l (44,sp),(48,sp)	| replacement, v
	add.l d2,a0	| ivtmp.661, _51
	lea (48,sp),a1	|,, tmp91
	move.l (a1)+,(a0)+	|,
	pea (44,sp)	|
	move.l d2,-(sp)	| ivtmp.661,
	move.l (a3),-(sp)	| MEM[base: _73, offset: 0B],
	move.l d4,-(sp)	| mutation,
	move.l a4,-(sp)	| tree,
	jsr (a2)	| tmp98
	lea (20,sp),sp	|,
	tst.l d0	| err
	jne .L1226	|
	subq.l #8,a3	|, ivtmp.659
	sub.l d3,d2	| block_size, ivtmp.661
	dbra d6,.L1224	| level,
	clr.w d6	| level
	subq.l #1,d6	| level
	jcc .L1224	|
.L1225:
	move.l (44,sp),(12,a4)	| replacement, tree_6(D)->root
	addq.l #1,(60,a4)	|, tree_6(D)->generation
.L1221:
	move.l a6,d0	| <retval>,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	addq.l #8,sp	|,
	rts
.L1226:
	move.l d0,a0	| err,
	move.l a0,d0	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	addq.l #8,sp	|,
	rts
	.align	2
	.globl	_bfs_btree_rekey_equal
_bfs_btree_rekey_equal:
	link.w a5,#-3544	|,
	movem.l a2/d5/d4/d3/d2,-(sp)	|,
	move.l (8,a5),a2	| tree, tree
	move.l (12,a5),d3	| old_key, old_key
	move.l (16,a5),d2	| new_key, new_key
	move.l a2,d0	|,
	jeq .L1233	|
	tst.l (a2)	| tree_6(D)->bio
	jeq .L1233	|
	tst.l (4,a2)	| tree_6(D)->alloc
	jeq .L1233	|
	tst.l d3	| old_key
	jeq .L1233	|
	tst.l d2	| new_key
	jeq .L1233	|
	move.l (8,a2),a0	| tree_6(D)->ops, _12
	move.l a0,d0	|,
	jeq .L1233	|
	move.l d2,-(sp)	| new_key,
	move.l d3,-(sp)	| old_key,
	move.l (a0),a0	| _12->key_compare, _12->key_compare
	jsr (a0)	| _12->key_compare
	addq.l #8,sp	|,
	tst.l d0	| _15
	jne .L1233	|
	move.l d0,(56,a2)	| _15, tree_6(D)->free_sink_err
	lea (-3276,a5),a0	|,, tmp72
	moveq #50,d0	|, tmp73
.L1234:
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	dbra d0,.L1234	| tmp73,
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	move.l (12,a2),d1	| tree_6(D)->root, _18
	jeq .L1244	|
	move.l (a2),a0	| MEM[(const struct bfs_btree_t *)tree_6(D)].bio, _39
	moveq #-3,d0	|, <retval>
	move.l a0,d4	|,
	jeq .L1230	|
	cmp.l (8,a0),d1	| _39->block_count, _18
	jcc .L1230	|
	move.l (16,a2),d1	| MEM[(const struct bfs_btree_t *)tree_6(D)].height, _53
	move.l d1,d4	| _53, tmp74
	subq.l #1,d4	|, tmp74
	moveq #31,d5	|,
	cmp.l d4,d5	| tmp74,
	jcs .L1230	|
	tst.l (36,a2)	| MEM[(const struct bfs_btree_t *)tree_6(D)].free_sink.defer
	jeq .L1242	|
	move.l d1,-(sp)	| _53,
	move.l a2,-(sp)	| tree,
	jsr (_mutation_headroom.part.3)	|
	addq.l #8,sp	|,
	tst.l d0	| <retval>
	jeq .L1265	|
.L1230:
	movem.l (-3564,a5),d2/d3/d4/d5/a2	|,
	unlk a5	|
	rts
.L1265:
	move.l (a2),a0	| tree_6(D)->bio, _39
	move.l (16,a2),d1	| tree_6(D)->height, _53
.L1242:
	move.l (4,a0),d4	| prephitmp_54->block_size, block_size
	muls.l d4,d1	| block_size, _53
	move.l d1,-(sp)	| _53,
	move.l a0,-(sp)	| _39,
	jsr _bfs_bio_alloc_buffer	|
	move.l d0,d5	|, node_bufs
	addq.l #8,sp	|,
	jeq .L1247	|
	clr.l (-3540,a5)	| depth
	clr.l -(sp)	|
	pea (-3540,a5)	|
	pea (-3532,a5)	|
	move.l d0,-(sp)	| node_bufs,
	move.l d3,-(sp)	| old_key,
	move.l a2,-(sp)	| tree,
	jsr _rekey_descend_to_leaf.constprop.24	|
	lea (24,sp),sp	|,
	tst.l d0	| <retval>
	jeq .L1266	|
	move.l d5,-(sp)	| node_bufs,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_6(D)],
	move.l d0,(-3544,a5)	|,
	jsr _bfs_bio_free_buffer	|
	addq.l #8,sp	|,
	move.l (-3544,a5),d0	|,
.L1239:
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	move.l d0,(-3544,a5)	|,
	jsr _mutation_abort	|
	move.l (-3544,a5),d0	|,
	movem.l (-3564,a5),d2/d3/d4/d5/a2	|,
	unlk a5	|
	rts
.L1266:
	muls.l (-3540,a5),d4	| depth, tmp81
	add.l d5,d4	| node_bufs, leaf
	pea (-3536,a5)	|
	move.l d3,-(sp)	| old_key,
	move.l d4,-(sp)	| leaf,
	pea (8,a2)	|
	jsr _node_search.isra.12	|
	lea (16,sp),sp	|,
	tst.l (-3536,a5)	| found
	jeq .L1248	|
	move.l (8,a2),a0	| tree_6(D)->ops, tree_6(D)->ops
	move.l (4,a0),d1	| _37->key_size, _38
	cmp.l #512,d1	|, _38
	jhi .L1249	|
	move.w #28,a0	|, _64
	muls.l d1,d0	| _38, tmp87
	add.l d0,a0	| tmp87, _64
	tst.l d1	| _38
	jeq .L1241	|
	move.l d2,a1	| new_key, ivtmp.671
	add.l d4,a0	| leaf, ivtmp.674
	subq.l #1,d1	|, tmp100
.L1240:
	move.b (a1)+,(a0)+	| MEM[base: _89, offset: 0B], MEM[base: _21, offset: 0B]
	dbra d1,.L1240	| tmp100,
	clr.w d1	| tmp100
	subq.l #1,d1	| tmp100
	jcc .L1240	|
.L1241:
	move.l (-3540,a5),-(sp)	| depth,
	move.l d5,-(sp)	| node_bufs,
	pea (-3532,a5)	|
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	jsr _rekey_commit_path	|
	move.l d5,-(sp)	| node_bufs,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_6(D)],
	move.l d0,(-3544,a5)	|,
	jsr _bfs_bio_free_buffer	|
	lea (28,sp),sp	|,
	move.l (-3544,a5),d0	|,
	jne .L1239	|
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	jsr _mutation_commit	|
	move.l (56,a2),d0	| tree_6(D)->free_sink_err, <retval>
	movem.l (-3564,a5),d2/d3/d4/d5/a2	|,
	unlk a5	|
	rts
.L1233:
	moveq #-8,d0	|, <retval>
	movem.l (-3564,a5),d2/d3/d4/d5/a2	|,
	unlk a5	|
	rts
.L1248:
	moveq #-5,d0	|, <retval>
	move.l d5,-(sp)	| node_bufs,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_6(D)],
	move.l d0,(-3544,a5)	|,
	jsr _bfs_bio_free_buffer	|
	addq.l #8,sp	|,
	move.l (-3544,a5),d0	|,
	jra .L1239	|
.L1244:
	moveq #-5,d0	|, <retval>
	movem.l (-3564,a5),d2/d3/d4/d5/a2	|,
	unlk a5	|
	rts
.L1249:
	moveq #-3,d0	|, <retval>
	move.l d5,-(sp)	| node_bufs,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_6(D)],
	move.l d0,(-3544,a5)	|,
	jsr _bfs_bio_free_buffer	|
	addq.l #8,sp	|,
	move.l (-3544,a5),d0	|,
	jra .L1239	|
.L1247:
	moveq #-2,d0	|, <retval>
	movem.l (-3564,a5),d2/d3/d4/d5/a2	|,
	unlk a5	|
	rts
	.align	2
	.globl	_bfs_btree_update_key
_bfs_btree_update_key:
	link.w a5,#-4572	|,
	movem.l a4/a3/a2/d6/d5/d4/d3/d2,-(sp)	|,
	lea (8,a5),a0	|,, tmp202
	move.l (a0)+,a2	| tree, tree
	move.l (a0)+,d5	| old_key, old_key
	move.l (a0)+,d2	| new_key, new_key
	move.l (a0),d3	| new_val, new_val
	move.l a2,d0	|,
	jeq .L1299	|
	move.l (a2),a1	| tree_9(D)->bio, _11
	move.l a1,d0	|,
	jeq .L1299	|
	tst.l (4,a2)	| tree_9(D)->alloc
	jeq .L1299	|
	tst.l d5	| old_key
	jeq .L1299	|
	tst.l d2	| new_key
	jeq .L1299	|
	tst.l d3	| new_val
	jeq .L1299	|
	tst.l (8,a2)	| tree_9(D)->ops
	jeq .L1299	|
	clr.l (56,a2)	| tree_9(D)->free_sink_err
	lea (-3276,a5),a0	|,, tmp132
	moveq #50,d0	|, tmp133
.L1269:
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	dbra d0,.L1269	| tmp133,
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	move.l (12,a2),d0	| tree_9(D)->root, _19
	jeq .L1300	|
	moveq #-3,d4	|, <retval>
	cmp.l (8,a1),d0	| _11->block_count, _19
	jcc .L1267	|
	move.l (16,a2),d0	| MEM[(const struct bfs_btree_t *)tree_9(D)].height, tmp134
	subq.l #1,d0	|, tmp134
	moveq #31,d1	|,
	cmp.l d0,d1	| tmp134,
	jcs .L1267	|
	lea (-4308,a5),a0	|,, tmp183
	moveq #15,d0	|, tmp184
.L1290:
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	dbra d0,.L1290	| tmp184,
	clr.l (a0)+	|
	clr.l (a0)+	|
	pea (-4308,a5)	|
	move.l d5,-(sp)	| old_key,
	move.l a2,-(sp)	| tree,
	jsr _owned_leaf_in_place	|
	move.l d0,d6	|, owned_leaf
	lea (12,sp),sp	|,
	jeq .L1272	|
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_ops_t * *)tree_9(D) + 8B], MEM[(const struct bfs_btree_ops_t * *)tree_9(D) + 8B]
	move.l (16,a0),a0	| _74->entry_ok, _82
	move.l a0,d0	|,
	jeq .L1271	|
	move.l d3,-(sp)	| new_val,
	move.l d2,-(sp)	| new_key,
	jsr (a0)	| _82
	addq.l #8,sp	|,
	tst.l d0	|
	jeq .L1272	|
.L1271:
	lea (8,a2),a3	|, tree, _75
	pea (-4564,a5)	|
	move.l d5,-(sp)	| old_key,
	move.l d6,-(sp)	| owned_leaf,
	move.l a3,-(sp)	| _75,
	jsr _node_search.isra.12	|
	move.l d0,d5	|, index
	lea (16,sp),sp	|,
	moveq #-5,d4	|, <retval>
	tst.l (-4564,a5)	| found
	jne .L1328	|
.L1267:
	move.l d4,d0	| <retval>,
	movem.l (-4604,a5),d2/d3/d4/d5/d6/a2/a3/a4	|,
	unlk a5	|
	rts
.L1272:
	lea (-4308,a5),a0	|,, tmp149
	moveq #15,d0	|, tmp150
.L1279:
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	dbra d0,.L1279	| tmp150,
	clr.l (a0)+	|
	clr.l (a0)+	|
	move.l (16,a2),d0	| tree_9(D)->height, _40
	tst.l (36,a2)	| MEM[(const struct bfs_btree_t *)tree_9(D)].free_sink.defer
	jeq .L1280	|
	tst.l d0	| _40
	jne .L1329	|
.L1280:
	move.l (a2),a0	| tree_9(D)->bio, _41
	move.l (4,a0),d6	| _41->block_size, block_size
	muls.l d6,d0	| block_size, _40
	move.l d0,-(sp)	| _40,
	move.l a0,-(sp)	| _41,
	jsr _bfs_bio_alloc_buffer	|
	move.l d0,a3	|, node_bufs
	addq.l #8,sp	|,
	tst.l d0	| node_bufs
	jeq .L1303	|
	clr.l (-4572,a5)	| depth
	pea (-4308,a5)	|
	pea (-4572,a5)	|
	pea (-4564,a5)	|
	move.l d0,-(sp)	| node_bufs,
	move.l d5,-(sp)	| old_key,
	move.l a2,-(sp)	| tree,
	jsr _rekey_descend_to_leaf.constprop.24	|
	move.l d0,d4	|, <retval>
	lea (24,sp),sp	|,
	jeq .L1330	|
.L1281:
	move.l a3,-(sp)	| node_bufs,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_9(D)],
	jsr _bfs_bio_free_buffer	|
	addq.l #8,sp	|,
.L1288:
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	jsr _mutation_abort	|
	move.l d4,d0	| <retval>,
	movem.l (-4604,a5),d2/d3/d4/d5/d6/a2/a3/a4	|,
	unlk a5	|
	rts
.L1328:
	move.l d2,-(sp)	| new_key,
	pea (-4308,a5)	|
	move.l d5,-(sp)	| index,
	move.l d6,-(sp)	| owned_leaf,
	move.l a3,-(sp)	| _75,
	jsr _key_fits_at.isra.10	|
	moveq #-10,d4	|, <retval>
	tst.l d0	|
	jeq .L1267	|
	move.l (8,a2),a1	| tree_9(D)->ops, pretmp_216
	move.l (4,a1),d0	| _31->key_size, pretmp_218
	move.l d5,d1	| index,
	move.w #28,a0	|, tmp138
	muls.l d0,d1	| pretmp_218,
	add.l d1,a0	|, tmp138
	tst.l d0	| pretmp_218
	jeq .L1274	|
	move.l d2,a1	| new_key, ivtmp.686
	add.l d6,a0	| owned_leaf, ivtmp.689
	subq.l #1,d0	|, tmp196
.L1275:
	move.b (a1)+,(a0)+	| MEM[base: _193, offset: 0B], MEM[base: _189, offset: 0B]
	dbra d0,.L1275	| tmp196,
	clr.w d0	| tmp196
	subq.l #1,d0	| tmp196
	jcc .L1275	|
	move.l (8,a2),a1	| tree_9(D)->ops, pretmp_216
	move.l (4,a1),d0	| pretmp_216->key_size, pretmp_218
.L1274:
	move.l (8,a1),d1	| prephitmp_217->val_size, _34
	moveq #-28,d2	|, tmp140
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_9(D)], MEM[(struct bfs_bio_t * *)tree_9(D)]
	add.l (4,a0),d2	| _90->block_size, tmp140
	move.l d1,d4	| _34, tmp141
	add.l d0,d4	| pretmp_218, tmp141
	divu.l d4,d2	| tmp141, tmp143
	muls.l d2,d0	| tmp143, tmp145
	move.l d0,a1	| tmp145,
	muls.l d1,d5	| _34, tmp146
	lea (28,a1,d5.l),a0	|, _99
	tst.l d1	| _34
	jeq .L1278	|
	move.l d3,a1	| new_val, ivtmp.680
	move.l d3,d0	| ivtmp.680, _202
	add.l d6,a0	| owned_leaf, ivtmp.682
	add.l d1,d0	| _34, _202
	move.l d3,d1	| ivtmp.680, tmp195
	not.l d1	| tmp195
	add.l d0,d1	| _202, tmp194
.L1277:
	move.b (a1)+,(a0)+	| MEM[base: _205, offset: 0B], MEM[base: _204, offset: 0B]
	dbra d1,.L1277	| tmp194,
	clr.w d1	| tmp194
	subq.l #1,d1	| tmp194
	jcc .L1277	|
.L1278:
	addq.l #1,(60,a2)	|, tree_9(D)->generation
	clr.l d0	|
	movem.l (-4604,a5),d2/d3/d4/d5/d6/a2/a3/a4	|,
	unlk a5	|
	rts
.L1329:
	move.l d0,-(sp)	| _40,
	move.l a2,-(sp)	| tree,
	jsr (_mutation_headroom.part.3)	|
	move.l d0,d4	|, <retval>
	addq.l #8,sp	|,
	jne .L1267	|
	move.l (16,a2),d0	| tree_9(D)->height, _40
	jra .L1280	|
.L1330:
	muls.l (-4572,a5),d6	| depth, tmp158
	add.l a3,d6	| node_bufs, leaf
	lea (8,a2),a4	|, tree, _76
	pea (-4568,a5)	|
	move.l d5,-(sp)	| old_key,
	move.l d6,-(sp)	| leaf,
	move.l a4,-(sp)	| _76,
	jsr _node_search.isra.12	|
	move.l d0,d5	|, index
	lea (16,sp),sp	|,
	moveq #-5,d4	|, <retval>
	tst.l (-4568,a5)	| found
	jeq .L1281	|
	move.l d2,-(sp)	| new_key,
	pea (-4308,a5)	|
	move.l d0,-(sp)	| index,
	move.l d6,-(sp)	| leaf,
	move.l a4,-(sp)	| _76,
	jsr _key_fits_at.isra.10	|
	lea (20,sp),sp	|,
	tst.l d0	|
	jeq .L1305	|
	move.l (8,a2),a1	| tree_9(D)->ops, _61
	move.l (4,a1),d0	| _59->key_size, _60
	move.l d5,d1	| index, tmp164
	move.w #28,a0	|, _131
	muls.l d0,d1	| _60, tmp164
	add.l d1,a0	| tmp164, _131
	tst.l d0	| _60
	jeq .L1286	|
	move.l d2,a1	| new_key, ivtmp.699
	add.l d6,a0	| leaf, ivtmp.702
	subq.l #1,d0	|, tmp200
.L1285:
	move.b (a1)+,(a0)+	| MEM[base: _157, offset: 0B], MEM[base: _156, offset: 0B]
	dbra d0,.L1285	| tmp200,
	clr.w d0	| tmp200
	subq.l #1,d0	| tmp200
	jcc .L1285	|
	move.l (8,a2),a1	| tree_9(D)->ops, _61
.L1286:
	move.l (8,a1),d1	| _61->val_size, _62
	move.l (4,a1),d4	| _61->key_size, _118
	moveq #-28,d0	|, tmp166
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_9(D)], MEM[(struct bfs_bio_t * *)tree_9(D)]
	add.l (4,a0),d0	| _115->block_size, tmp166
	move.l d1,d2	| _62, tmp167
	add.l d4,d2	| _118, tmp167
	divu.l d2,d0	| tmp167, tmp169
	muls.l d4,d0	| _118, tmp171
	move.l d0,a1	| tmp171,
	muls.l d1,d5	| _62, tmp173
	lea (28,a1,d5.l),a0	|, _124
	tst.l d1	| _62
	jeq .L1284	|
	move.l d3,a1	| new_val, ivtmp.693
	add.l d6,a0	| leaf, ivtmp.695
	subq.l #1,d1	|, tmp198
.L1289:
	move.b (a1)+,(a0)+	| MEM[base: _171, offset: 0B], MEM[base: _170, offset: 0B]
	dbra d1,.L1289	| tmp198,
	clr.w d1	| tmp198
	subq.l #1,d1	| tmp198
	jcc .L1289	|
.L1284:
	move.l (-4572,a5),-(sp)	| depth,
	move.l a3,-(sp)	| node_bufs,
	pea (-4564,a5)	|
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	jsr _rekey_commit_path	|
	move.l d0,d4	|, <retval>
	move.l a3,-(sp)	| node_bufs,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_9(D)],
	jsr _bfs_bio_free_buffer	|
	lea (28,sp),sp	|,
	tst.l d4	| <retval>
	jne .L1288	|
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	jsr _mutation_commit	|
	move.l (56,a2),d1	| tree_9(D)->free_sink_err,
	move.l d1,d0	|,
	movem.l (-4604,a5),d2/d3/d4/d5/d6/a2/a3/a4	|,
	unlk a5	|
	rts
.L1299:
	moveq #-8,d1	|,
	move.l d1,d0	|,
	movem.l (-4604,a5),d2/d3/d4/d5/d6/a2/a3/a4	|,
	unlk a5	|
	rts
.L1300:
	moveq #-5,d1	|,
	move.l d1,d0	|,
	movem.l (-4604,a5),d2/d3/d4/d5/d6/a2/a3/a4	|,
	unlk a5	|
	rts
.L1305:
	moveq #-10,d4	|, <retval>
	move.l a3,-(sp)	| node_bufs,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_9(D)],
	jsr _bfs_bio_free_buffer	|
	addq.l #8,sp	|,
	jra .L1288	|
.L1303:
	moveq #-2,d1	|,
	move.l d1,d0	|,
	movem.l (-4604,a5),d2/d3/d4/d5/d6/a2/a3/a4	|,
	unlk a5	|
	rts
	.align	2
_scan_descend:
	lea (-1040,sp),sp	|,
	movem.l a6/a5/a4/a3/a2/d7/d6/d5/d4/d3/d2,-(sp)	|,
	lea (1088,sp),a0	|,, tmp105
	move.l (a0)+,a3	| tree, tree
	move.l (a0)+,d4	| key, key
	move.l (a0)+,(1096,sp)	| buf, buf
	move.l (a0)+,a2	| path, path
	move.l (a0),(1104,sp)	| leaf, leaf
	move.l (12,a3),d3	| tree_10(D)->root, blk
	move.w (18,a3),d2	| tree_10(D)->height, level
	subq.w #1,d2	|, level
	moveq #52,d5	|, tmp97
	add.l sp,d5	|, tmp97
	move.l d5,a0	| tmp97, tmp70
	moveq #15,d0	|, tmp71
.L1332:
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	dbra d0,.L1332	| tmp71,
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (256,a2)	| path_16(D)->depth
	lea _node_view,a5	|, tmp98
	moveq #48,d7	|, tmp101
	lea (8,a3),a4	|, tree, prephitmp_38
	add.l sp,d7	|, tmp101
	lea _child_bounds.isra.13,a6	|, tmp104
	pea (44,sp)	|
	move.l d5,-(sp)	| tmp97,
	move.w d2,-(sp)	| level,
	clr.w -(sp)	|
	move.l (1108,sp),-(sp)	| buf,
	move.l d3,-(sp)	| blk,
	move.l a3,-(sp)	| tree,
	jsr (a5)	| tmp98
	lea (24,sp),sp	|,
	tst.l d0	| <retval>
	jne .L1331	|
.L1346:
	tst.w d2	| level
	jeq .L1344	|
	move.l (256,a2),d0	| path_16(D)->depth, prephitmp_64
	moveq #31,d1	|,
	cmp.l d0,d1	| prephitmp_64,
	jcs .L1339	|
	tst.l d4	| key
	jeq .L1345	|
	move.l d7,-(sp)	| tmp101,
	move.l d4,-(sp)	| key,
	move.l (52,sp),-(sp)	| node,
	move.l a4,-(sp)	| prephitmp_38,
	jsr _node_search.isra.12	|
	move.l d0,a1	|, idx
	lea (16,sp),sp	|,
	tst.l (48,sp)	| found
	jeq .L1337	|
	addq.l #1,a1	|, idx
.L1337:
	move.l (256,a2),d0	| path_16(D)->depth, prephitmp_64
	move.l a1,d6	| idx, _39
	lsl.l #2,d6	|, _39
	move.l d0,d1	| prephitmp_64, tmp78
	lsl.l #2,d1	|, tmp78
	lea (a2,d1.l),a0	| path, tmp78, _48
	move.l d3,(a0)+	| blk, MEM[(struct scan_path_t *)_48]
	move.l a1,(124,a0)	| idx, MEM[(struct scan_path_t *)_48 + 128B]
	addq.l #1,d0	|, prephitmp_64
	move.l d0,(256,a2)	| prephitmp_64, path_16(D)->depth
	move.l d5,-(sp)	| tmp97,
	move.l a1,-(sp)	| idx,
	move.l (52,sp),-(sp)	| node,
	move.l a4,-(sp)	| prephitmp_38,
	jsr (a6)	| tmp104
	move.l (8,a3),a0	| tree_10(D)->ops, tree_10(D)->ops
	move.l (4,a0),d3	| _52->key_size, _53
	moveq #-32,d0	|, tmp84
	move.l (a3),a0	| MEM[(struct bfs_bio_t * *)tree_10(D)], MEM[(struct bfs_bio_t * *)tree_10(D)]
	add.l (4,a0),d0	| _49->block_size, tmp84
	move.l d3,d1	| _53, tmp85
	addq.l #4,d1	|, tmp85
	divu.l d1,d0	| tmp85, tmp87
	move.l d6,a1	| _39,
	muls.l d3,d0	| _53, tmp89
	lea (28,a1,d0.l),a0	|, tmp91
	move.l d7,a1	| tmp101, tmp94
	add.l (60,sp),a0	| node, tmp93
	move.l (a0)+,(a1)+	|,
	move.l (64,sp),d3	| v, blk
	subq.w #1,d2	|, level
	lea (16,sp),sp	|,
.L1347:
	pea (44,sp)	|
	move.l d5,-(sp)	| tmp97,
	move.w d2,-(sp)	| level,
	clr.w -(sp)	|
	move.l (1108,sp),-(sp)	| buf,
	move.l d3,-(sp)	| blk,
	move.l a3,-(sp)	| tree,
	jsr (a5)	| tmp98
	lea (24,sp),sp	|,
	tst.l d0	| <retval>
	jeq .L1346	|
.L1331:
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (1040,sp),sp	|,
	rts
.L1345:
	move.l d0,d1	| prephitmp_64, tmp78
	lsl.l #2,d1	|, tmp78
	lea (a2,d1.l),a0	| path, tmp78, _48
	move.l d3,(a0)+	| blk, MEM[(struct scan_path_t *)_48]
	move.l d4,(124,a0)	|, MEM[(struct scan_path_t *)_48 + 128B]
	addq.l #1,d0	|, prephitmp_64
	move.l d0,(256,a2)	| prephitmp_64, path_16(D)->depth
	move.l d5,-(sp)	| tmp97,
	move.l d4,-(sp)	|,
	move.l (52,sp),-(sp)	| node,
	move.l a4,-(sp)	| prephitmp_38,
	jsr (a6)	| tmp104
	move.l (8,a3),a0	| tree_10(D)->ops, tree_10(D)->ops
	move.l (4,a0),d3	| _52->key_size, _53
	moveq #-32,d0	|, tmp84
	move.l (a3),a0	| MEM[(struct bfs_bio_t * *)tree_10(D)], MEM[(struct bfs_bio_t * *)tree_10(D)]
	add.l (4,a0),d0	| _49->block_size, tmp84
	move.l d3,d1	| _53, tmp85
	addq.l #4,d1	|, tmp85
	divu.l d1,d0	| tmp85, tmp87
	move.l d4,a1	|,
	muls.l d3,d0	| _53, tmp89
	lea (28,a1,d0.l),a0	|, tmp91
	move.l d7,a1	| tmp101, tmp94
	add.l (60,sp),a0	| node, tmp93
	move.l (a0)+,(a1)+	|,
	move.l (64,sp),d3	| v, blk
	subq.w #1,d2	|, level
	lea (16,sp),sp	|,
	jra .L1347	|
.L1344:
	move.l (1104,sp),a0	| leaf,
	move.l (44,sp),(a0)	| node, *leaf_43(D)
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (1040,sp),sp	|,
	rts
.L1339:
	moveq #-3,d0	|, <retval>
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (1040,sp),sp	|,
	rts
	.align	2
	.globl	_bfs_btree_cursor_init
_bfs_btree_cursor_init:
	move.l (4,sp),a0	| cursor, cursor
	move.l a0,d0	|,
	jeq .L1348	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
.L1348:
	rts
	.align	2
	.globl	_bfs_btree_cursor_release
_bfs_btree_cursor_release:
	move.l a6,-(sp)	|,
	move.l a2,-(sp)	|,
	move.l (12,sp),a2	| cursor, cursor
	move.l a2,d0	|,
	jeq .L1355	|
	move.l (4,a2),a1	| cursor_2(D)->leaf, _4
	move.l a1,d0	|,
	jeq .L1357	|
	move.l _SysBase,a6	| SysBase, _FreeVec_bn
#APP
| 21 "src/amiga/stdlib.h" 1
	jsr a6@(-0x2b2:W)
| 0 "" 2
#NO_APP
.L1357:
	clr.l (a2)+	|
	clr.l (a2)+	|
	clr.l (a2)+	|
	clr.l (a2)+	|
	clr.l (a2)+	|
	clr.l (a2)+	|
	clr.l (a2)+	|
	clr.l (a2)+	|
	clr.l (a2)+	|
	clr.l (a2)+	|
.L1355:
	move.l (sp)+,a2	|,
	move.l (sp)+,a6	|,
	rts
	.align	2
_cursor_current:
	movem.l a3/a2/d3/d2,-(sp)	|,
	move.l (20,sp),a1	| tree, tree
	move.l (a1),a0	| tree_4(D)->bio, _5
	move.l (24,sp),a2	| cursor, cursor
	move.l a0,d0	|,
	jeq .L1369	|
	move.l (a0),a3	| _5->ops, _6
	move.l a3,d0	|,
	jeq .L1369	|
	tst.l (4,a2)	| cursor_7(D)->leaf
	jeq .L1369	|
	tst.l (36,a2)	| cursor_7(D)->valid
	jeq .L1369	|
	cmp.l (a2),a1	| cursor_7(D)->tree, tree
	jeq .L1381	|
.L1369:
	clr.l d0	| <retval>
	movem.l (sp)+,d2/d3/a2/a3	|,
	rts
.L1381:
	move.l (12,a2),d0	| cursor_7(D)->root,
	cmp.l (12,a1),d0	| tree_4(D)->root,
	jne .L1369	|
	move.l (8,a2),d0	| cursor_7(D)->generation,
	cmp.l (60,a1),d0	| tree_4(D)->generation,
	jne .L1369	|
	move.l (76,a3),a1	| _6->mutation_epoch, _15
	move.l a1,d0	|,
	jeq .L1371	|
	tst.l (28,a2)	| cursor_7(D)->mutation_epoch_valid
	jeq .L1369	|
	move.l a0,-(sp)	| _5,
	jsr (a1)	| _15
	addq.l #4,sp	|,
	moveq #-1,d2	|,
	moveq #-1,d3	|,
	sub.l d1,d3	| epoch,
	subx.l d0,d2	| epoch,
	jeq .L1369	|
	cmp.l (20,a2),d0	| cursor_7(D)->mutation_epoch, epoch
	jne .L1382	|
	cmp.l (24,a2),d1	| cursor_7(D)->mutation_epoch,
.L1382:
	seq d0	| tmp50
	extb.l d0	| tmp49
	neg.l d0	| <retval>
	movem.l (sp)+,d2/d3/a2/a3	|,
	rts
.L1371:
	moveq #1,d0	|, <retval>
	movem.l (sp)+,d2/d3/a2/a3	|,
	rts
	.align	2
	.globl	_bfs_btree_cursor_stop_key
_bfs_btree_cursor_stop_key:
	movem.l a4/a3/a2/d3/d2,-(sp)	|,
	move.l (24,sp),a3	| tree, tree
	move.l (28,sp),a2	| cursor, cursor
	move.l a3,d0	|,
	jeq .L1386	|
	move.l (a3),a0	| tree_5(D)->bio, _13
	move.l a0,d0	|,
	jeq .L1386	|
	tst.l (a0)	| _13->ops
	jeq .L1386	|
	move.l (8,a3),a1	| tree_5(D)->ops, _15
	move.l a1,d0	|,
	jeq .L1386	|
	tst.l (a1)	| _15->key_compare
	jeq .L1386	|
	move.l (4,a0),d0	| _13->block_size, _17
	move.l #-1024,d1	|, tmp70
	add.l d0,d1	| _17, tmp70
	cmp.l #64512,d1	|, tmp70
	jhi .L1386	|
	move.l d0,d1	| _17, tmp72
	subq.l #1,d1	|, tmp72
	and.l d0,d1	| _17, tmp73
	jne .L1386	|
	move.l (4,a1),d1	| _15->key_size, _22
	move.l d1,d2	| _22, tmp94
	subq.l #1,d2	|, tmp94
	cmp.l #511,d2	|, tmp94
	jhi .L1386	|
	move.l (8,a1),d2	| _15->val_size, _23
	jeq .L1386	|
	move.l d2,d3	| _23, tmp74
	not.l d3	| tmp74
	cmp.l d1,d3	| _22, tmp74
	jcs .L1386	|
	add.l d1,d2	| _22, leaf_stride
	jeq .L1386	|
	moveq #-28,d3	|, data_size
	add.l d0,d3	| _17, data_size
	divu.l d2,d3	| leaf_stride, tmp77
	moveq #2,d2	|,
	cmp.l d3,d2	| tmp77,
	jcc .L1386	|
	moveq #-32,d2	|,
	add.l d2,d0	|, tmp79
	addq.l #4,d1	|, internal_stride
	divu.l d1,d0	| internal_stride, tmp82
	moveq #2,d1	|,
	cmp.l d0,d1	| tmp82,
	jcc .L1386	|
	move.l a2,d0	|,
	jeq .L1386	|
	tst.l (4,a2)	| cursor_7(D)->leaf
	jeq .L1386	|
	tst.l (32,a2)	| cursor_7(D)->stopped
	jeq .L1386	|
	move.l a2,-(sp)	| cursor,
	move.l a3,-(sp)	| tree,
	jsr _cursor_current	|
	addq.l #8,sp	|,
	tst.l d0	|
	jeq .L1386	|
	move.l (4,a2),a0	| cursor_7(D)->leaf, _32
	tst.w (20,a0)	|
	jne .L1386	|
	move.l (16,a0),a1	|, _34
	move.l a1,d0	|,
	jeq .L1386	|
	move.l (8,a3),a4	| tree_5(D)->ops, _38
	move.l (4,a4),d1	| _38->key_size, _39
	moveq #-28,d0	|, tmp86
	move.l (a3),a3	| MEM[(struct bfs_bio_t * *)tree_5(D)], MEM[(struct bfs_bio_t * *)tree_5(D)]
	add.l (4,a3),d0	| _35->block_size, tmp86
	move.l d1,d2	| _39, tmp87
	add.l (8,a4),d2	| _38->val_size, tmp87
	divu.l d2,d0	| tmp87, tmp89
	cmp.l a1,d0	| _34, tmp89
	jcs .L1386	|
	move.l (16,a2),d0	| cursor_7(D)->stop_index, _43
	cmp.l a1,d0	| _34, _43
	jcc .L1386	|
	muls.l d0,d1	| _43, tmp91
	lea (28,a0,d1.l),a0	|,
	move.l a0,d0	|, <retval>
	movem.l (sp)+,d2/d3/a2/a3/a4	|,
	rts
.L1386:
	clr.l d0	| <retval>
	movem.l (sp)+,d2/d3/a2/a3/a4	|,
	rts
	.align	2
	.globl	_bfs_btree_scan
_bfs_btree_scan:
	lea (4,sp),a0	|,, tmp38
	move.l (a0)+,d0	| tree, tree
	move.l (a0)+,d1	| start_key, start_key
	move.l (a0)+,a1	| cb, cb
	move.l (a0),-(sp)	| ctx,
	move.l a1,-(sp)	| cb,
	clr.l -(sp)	|
	move.l d1,-(sp)	| start_key,
	clr.l -(sp)	|
	move.l d0,-(sp)	| tree,
	jsr _btree_scan_cursor_impl	|
	lea (24,sp),sp	|,
	rts
	.align	2
_btree_scan_cursor_impl:
	link.w a5,#-1332	|,
	movem.l a6/a4/a3/a2/d7/d6/d5/d4/d3/d2,-(sp)	|,
	lea (8,a5),a0	|,, tmp392
	move.l (a0)+,a2	| tree, tree
	move.l (a0)+,a4	| cursor, cursor
	move.l (a0)+,d3	| start_key, start_key
	move.l (a0)+,d4	| resume_exclusive, resume_exclusive
	move.l (a0)+,d7	| cb, cb
	move.l (a0),d5	| ctx, ctx
	move.l a2,d0	|,
	jeq .L1481	|
	move.l (a2),a0	| tree_41(D)->bio, _43
	move.l a0,d0	|,
	jeq .L1481	|
	tst.l (a0)	| _43->ops
	jeq .L1481	|
	tst.l (8,a2)	| tree_41(D)->ops
	jeq .L1481	|
	tst.l d7	| cb
	jeq .L1481	|
	move.l (12,a2),d1	| tree_41(D)->root, _48
	clr.l d0	| <retval>
	tst.l d1	| _48
	jeq .L1414	|
	moveq #-3,d0	|, <retval>
	cmp.l (8,a0),d1	| _43->block_count, _48
	jcc .L1414	|
	move.l (16,a2),d1	| MEM[(const struct bfs_btree_t *)tree_41(D)].height, tmp218
	subq.l #1,d1	|, tmp218
	moveq #31,d2	|,
	cmp.l d1,d2	| tmp218,
	jcs .L1414	|
	move.l a4,d0	|,
	jeq .L1417	|
	tst.l (4,a4)	| cursor_51(D)->leaf
	jeq .L1567	|
.L1417:
	move.l (4,a0),-(sp)	| prephitmp_499->block_size,
	move.l a0,-(sp)	| _43,
	jsr _bfs_bio_alloc_buffer	|
	move.l d0,(-1320,a5)	|, %sfp
	addq.l #8,sp	|,
	jeq .L1484	|
	clr.l (-1304,a5)	| leaf
	tst.l d4	| resume_exclusive
	jeq .L1419	|
	move.l (4,a4),(-1304,a5)	| cursor_1->leaf, leaf
	move.l (16,a4),d2	| cursor_1->stop_index, i
	addq.l #1,d2	|, i
	moveq #1,d1	|, positioned
	clr.l d0	| <retval>
	move.l d0,(-1308,a5)	| <retval>, %sfp
	clr.l (32,a4)	| cursor_1->stopped
.L1476:
	tst.l d0	| <retval>
	jne .L1427	|
	tst.l d3	| start_key
	jeq .L1428	|
	tst.l d1	| positioned
	jeq .L1568	|
.L1428:
	move.l (-1304,a5),a6	| leaf, prephitmp_479
	clr.l (-1324,a5)	| %sfp
.L1429:
	move.l d7,(-1332,a5)	| cb, %sfp
.L1469:
	move.l a6,d0	|,
	jeq .L1569	|
	move.l (16,a6),d3	|, reached
	tst.l (-1324,a5)	| %sfp
	jeq .L1430	|
	tst.l d2	| i
	jne .L1430	|
	tst.l d3	| reached
	jne .L1570	|
.L1431:
	tst.l (-1308,a5)	| %sfp
	jne .L1571	|
.L1456:
	pea (-1304,a5)	|
	pea (-1292,a5)	|
	move.l (-1320,a5),-(sp)	| %sfp,
	move.l (-1324,a5),-(sp)	| %sfp,
	move.l a2,-(sp)	| tree,
	jsr _scan_descend	|
	lea (20,sp),sp	|,
	tst.l d0	| err
	jeq .L1572	|
.L1554:
	move.l d0,d2	| err, err
	tst.l (-1324,a5)	| %sfp
	jeq .L1573	|
.L1432:
	move.l (-1324,a5),a1	| %sfp, _n1
	move.l _SysBase,a6	| SysBase, _FreeVec_bn
#APP
| 21 "src/amiga/stdlib.h" 1
	jsr a6@(-0x2b2:W)
| 0 "" 2
#NO_APP
	move.l d2,d0	| err, <retval>
.L1427:
	move.l (-1320,a5),-(sp)	| %sfp,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_41(D)],
	move.l d0,(-1328,a5)	|,
	jsr _bfs_bio_free_buffer	|
	move.l (-1328,a5),d0	|,
.L1414:
	movem.l (-1372,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L1419:
	move.l a4,d0	|,
	jeq .L1421	|
	tst.l d3	| start_key
	jeq .L1422	|
	tst.l (32,a4)	| MEM[(const struct bfs_btree_cursor_t *)cursor_1].stopped
	jne .L1547	|
	lea _cursor_current,a3	|, tmp372
.L1423:
	move.l a4,-(sp)	| cursor,
	move.l a2,-(sp)	| tree,
	jsr (a3)	| tmp372
	addq.l #8,sp	|,
	tst.l d0	|
	jeq .L1425	|
	move.l (4,a4),a0	| MEM[(const struct bfs_btree_cursor_t *)cursor_1].leaf, _209
	move.l (16,a0),d2	|, _210
	jne .L1574	|
.L1425:
	pea (-1304,a5)	|
	pea (-1292,a5)	|
	move.l (-1320,a5),-(sp)	| %sfp,
	move.l d3,-(sp)	| start_key,
	move.l a2,-(sp)	| tree,
	jsr _scan_descend	|
	lea (20,sp),sp	|,
	clr.l d1	| positioned
	moveq #1,d2	|,
	move.l d2,(-1308,a5)	|, %sfp
	move.l d1,d2	| positioned, i
	clr.l (32,a4)	| cursor_1->stopped
	jra .L1476	|
.L1572:
	pea (-1032,a5)	|
	move.l (-1324,a5),-(sp)	| %sfp,
	move.l (-1304,a5),-(sp)	| leaf,
	pea (8,a2)	|
	jsr _node_search.isra.12	|
	move.l d0,d2	|, i
	lea (16,sp),sp	|,
	tst.l (-1032,a5)	| found
	jeq .L1458	|
	addq.l #1,d2	|, i
.L1458:
	moveq #1,d0	|,
	move.l (-1304,a5),a6	| leaf, prephitmp_479
	move.l d0,(-1308,a5)	|, %sfp
	jra .L1469	|
.L1571:
	clr.l (-1304,a5)	| leaf
	move.l (-1036,a5),a6	| path.depth, _53
	move.l a6,d0	|,
	jeq .L1490	|
	lea _node_view,a3	|, tmp376
	move.l (-1320,a5),d4	| %sfp, _147
	move.l (-1332,a5),d6	| %sfp,
.L1463:
	move.w (18,a2),d3	| MEM[(const struct bfs_btree_t *)tree_41(D)].height, _262
	lea (-1,a6),a0	|, _53, d
	sub.w a0,d3	| d, _262
	pea (-1300,a5)	|
	clr.l -(sp)	|
	move.w d3,d0	| _262, level
	subq.w #1,d0	|, level
	move.w d0,-(sp)	| level,
	clr.w -(sp)	|
	move.l d4,-(sp)	| _147,
	move.l (-1292,a5,a0.l*4),-(sp)	| path.blk,
	move.l a2,-(sp)	| tree,
	jsr (a3)	| tmp376
	lea (24,sp),sp	|,
	tst.l d0	| err
	jne .L1554	|
	lea (31,a6),a6	|, tmp315
	move.l (-1292,a5,a6.l*4),d2	| path.idx, next
	addq.l #1,d2	|, next
	move.l (-1300,a5),a1	| node, node.105_269
	cmp.l (16,a1),d2	|, next
	jls .L1462	|
	move.l (-1036,a5),a6	| path.depth, _53
	subq.l #1,a6	|, _53
	move.l a6,(-1036,a5)	| _53, path.depth
	move.l a6,d0	|,
	jne .L1463	|
	move.l (-1304,a5),a6	| leaf, prephitmp_479
	clr.l d2	| i
.L1583:
	move.l d6,(-1332,a5)	|, %sfp
	jra .L1469	|
.L1570:
	move.l (8,a2),a0	| tree_41(D)->ops, tree_41(D)->ops
	move.l (-1324,a5),-(sp)	| %sfp,
	pea (28,a6)	|
	move.l (a0),a0	| _130->key_compare, _130->key_compare
	jsr (a0)	| _130->key_compare
	addq.l #8,sp	|,
	tst.l d0	|
	jle .L1575	|
.L1433:
	move.l a4,d0	|,
	jeq .L1486	|
	move.l (4,a4),a3	| cursor_1->leaf, prephitmp_481
.L1435:
	move.l (a2),a0	| tree_41(D)->bio, _84
	move.l a0,d0	|,
	jeq .L1436	|
	move.l (a0),a1	| _84->ops, _150
	move.l a1,d0	|,
	jeq .L1436	|
	move.l (76,a1),a1	| _150->mutation_epoch, _151
	move.l a1,d0	|,
	jeq .L1436	|
	move.l a0,-(sp)	| _84,
	jsr (a1)	| _151
	move.l d0,(-1316,a5)	|, %sfp
	move.l d1,(-1312,a5)	|, %sfp
	addq.l #4,sp	|,
	moveq #1,d4	|, _153
	moveq #-1,d6	|,
	moveq #-1,d7	|,
	sub.l d7,d1	|,
	subx.l d6,d0	|,
	jeq .L1436	|
.L1437:
	move.l (-1304,a5),a6	| leaf, leaf.91_85
	cmp.l a3,a6	| prephitmp_481, leaf.91_85
	jeq .L1439	|
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_41(D)], MEM[(struct bfs_bio_t * *)tree_41(D)]
	move.l (16,a6),d7	|, _225
	move.l (8,a2),a1	| MEM[(const struct bfs_btree_t *)tree_41(D)].ops, _229
	move.l (4,a0),a0	| _226->block_size, _227
	move.l (4,a1),d6	| _229->key_size, _230
	moveq #-28,d0	|, tmp243
	add.l a0,d0	| _227, tmp243
	move.l d6,d1	| _230, tmp244
	add.l (8,a1),d1	| _229->val_size, tmp244
	divu.l d1,d0	| tmp244, tmp246
	cmp.l d7,d0	| _225, tmp246
	jcs .L1576	|
	muls.l d7,d6	| _225, tmp254
	move.l d6,a0	| tmp254,
	pea (28,a0)	|
	move.l a6,-(sp)	| leaf.91_85,
	move.l a3,-(sp)	| prephitmp_481,
	jsr _memcpy	|
	lea (12,sp),sp	|,
	tst.l d7	| _225
	jne .L1577	|
.L1439:
	move.l a3,(-1304,a5)	| prephitmp_481, leaf
	move.l a4,d0	|,
	jeq .L1578	|
	move.l a2,(a4)	| tree, cursor_1->tree
	move.l (12,a2),(12,a4)	| tree_41(D)->root, cursor_1->root
	move.l (60,a2),d6	| tree_41(D)->generation, _91
	move.l d6,(8,a4)	| _91, cursor_1->generation
	move.l (-1316,a5),(20,a4)	| %sfp, cursor_1->mutation_epoch
	move.l (-1312,a5),(24,a4)	| %sfp, cursor_1->mutation_epoch
	move.l d4,(28,a4)	| _153, cursor_1->mutation_epoch_valid
	moveq #1,d0	|,
	move.l d0,(36,a4)	|, cursor_1->valid
	move.l d4,a6	| _153, _153
	move.l d5,d7	| ctx, ctx
.L1473:
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_t *)tree_41(D)].ops, _160
	move.l (4,a0),d1	| _160->key_size, _161
	move.l (8,a0),a0	| _160->val_size, _162
	move.l d7,-(sp)	| ctx,
	moveq #-28,d0	|, tmp279
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_41(D)], MEM[(struct bfs_bio_t * *)tree_41(D)]
	add.l (4,a1),d0	| _157->block_size, tmp279
	move.l d1,d4	| _161, tmp280
	add.l a0,d4	| _162, tmp280
	divu.l d4,d0	| tmp280, tmp282
	muls.l d1,d0	| _161, tmp284
	move.l a0,d4	| _162,
	muls.l d2,d4	| i,
	move.l d4,a0	|, tmp286
	lea (28,a0,d0.l),a0	|, tmp287
	pea (a3,a0.l)	|
	muls.l d2,d1	| i, tmp289
	pea (28,a3,d1.l)	|
	move.l (-1332,a5),a0	| %sfp,
	jsr (a0)	|
	lea (12,sp),sp	|,
	tst.l d0	| _102
	jeq .L1579	|
.L1444:
	cmp.l (60,a2),d6	| tree_41(D)->generation, _91
	jne .L1555	|
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_41(D)], _137
	move.l (a1),a0	| _137->ops, _250
	move.l a0,d0	|,
	jeq .L1452	|
	move.l (76,a0),a0	| _250->mutation_epoch, _251
	move.l a0,d0	|,
	jeq .L1452	|
	move.l a6,d0	|,
	jeq .L1452	|
	move.l a1,-(sp)	| _137,
	jsr (a0)	| _251
	addq.l #4,sp	|,
	moveq #-1,d4	|,
	moveq #-1,d5	|,
	sub.l d1,d5	| epoch,
	subx.l d0,d4	| epoch,
	jeq .L1555	|
	move.l (-1316,a5),d4	| %sfp,
	move.l (-1312,a5),d5	| %sfp,
	sub.l d1,d5	| epoch,
	subx.l d0,d4	| epoch,
	jeq .L1452	|
.L1555:
	move.l d7,d5	| ctx, ctx
	tst.l d3	| reached
	jne .L1447	|
.L1448:
	move.l a4,d0	|,
	jeq .L1456	|
	clr.l (36,a4)	| cursor_1->valid
	pea (-1304,a5)	|
	pea (-1292,a5)	|
	move.l (-1320,a5),-(sp)	| %sfp,
	move.l (-1324,a5),-(sp)	| %sfp,
	move.l a2,-(sp)	| tree,
	jsr _scan_descend	|
	lea (20,sp),sp	|,
	tst.l d0	| err
	jne .L1554	|
	jra .L1572	|
.L1447:
	move.l d2,d3	| i, reached
	addq.l #1,d3	|, reached
	moveq #1,d2	|, changed
	tst.l (-1324,a5)	| %sfp
	jeq .L1580	|
.L1455:
	move.l (8,a2),a0	| tree_41(D)->ops, tree_41(D)->ops
	move.l (4,a0),d0	| _112->key_size, _113
	subq.l #1,d3	|, tmp294
	move.w #28,a0	|, tmp296
	muls.l d0,d3	| _113, tmp295
	add.l d3,a0	| tmp295, tmp296
	move.l d0,-(sp)	| _113,
	move.l (-1304,a5),d0	| leaf,
	add.l a0,d0	| tmp296,
	move.l d0,-(sp)	|,
	move.l (-1324,a5),-(sp)	| %sfp,
	jsr _memcpy	|
	lea (12,sp),sp	|,
	tst.l d2	| changed
	jne .L1448	|
.L1565:
	tst.l (-1308,a5)	| %sfp
	jeq .L1456	|
	jra .L1571	|
.L1452:
	addq.l #1,d2	|, i
	cmp.l d2,d3	| i, reached
	jls .L1581	|
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_t *)tree_41(D)].ops, _160
	move.l (-1304,a5),a3	| leaf, prephitmp_481
	move.l (4,a0),d1	| _160->key_size, _161
	move.l (8,a0),a0	| _160->val_size, _162
	move.l d7,-(sp)	| ctx,
	moveq #-28,d0	|, tmp279
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_41(D)], MEM[(struct bfs_bio_t * *)tree_41(D)]
	add.l (4,a1),d0	| _157->block_size, tmp279
	move.l d1,d4	| _161, tmp280
	add.l a0,d4	| _162, tmp280
	divu.l d4,d0	| tmp280, tmp282
	muls.l d1,d0	| _161, tmp284
	move.l a0,d4	| _162,
	muls.l d2,d4	| i,
	move.l d4,a0	|, tmp286
	lea (28,a0,d0.l),a0	|, tmp287
	pea (a3,a0.l)	|
	muls.l d2,d1	| i, tmp289
	pea (28,a3,d1.l)	|
	move.l (-1332,a5),a0	| %sfp,
	jsr (a0)	|
	lea (12,sp),sp	|,
	tst.l d0	| _102
	jne .L1444	|
.L1579:
	move.l a4,d1	|,
	jeq .L1488	|
	move.l d2,(16,a4)	| i, cursor_1->stop_index
	moveq #1,d1	|,
	move.l d1,(32,a4)	|, cursor_1->stopped
	move.l d0,d2	| _102, err
	tst.l (-1324,a5)	| %sfp
	jne .L1432	|
	jra .L1573	|
.L1577:
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_t *)tree_41(D)].ops, _236
	move.l (8,a0),d6	| _236->val_size, _237
	move.l (4,a0),a0	| _236->key_size, _242
	moveq #-28,d0	|, tmp263
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_41(D)], MEM[(struct bfs_bio_t * *)tree_41(D)]
	add.l (4,a1),d0	| _239->block_size, tmp263
	move.l d6,d1	| _237, tmp264
	add.l a0,d1	| _242, tmp264
	divu.l d1,d0	| tmp264, tmp266
	move.l a0,d1	| _242,
	move.w #28,a0	|, keys_end
	muls.l d1,d0	|, tmp268
	muls.l d6,d7	| _237, _225
	add.l d0,a0	| tmp268, keys_end
	move.l d7,-(sp)	| _225,
	pea (a6,a0.l)	|
	pea (a3,a0.l)	|
	jsr _memcpy	|
	lea (12,sp),sp	|,
	jra .L1439	|
.L1567:
	move.l _SysBase,a6	| SysBase, _AllocVec_bn
	move.l (4,a0),d0	| _43->block_size, _n1
	moveq #1,d1	|, _n2
	swap d1	| _n2
#APP
| 18 "src/amiga/stdlib.h" 1
	jsr a6@(-0x2ac:W)
| 0 "" 2
#NO_APP
	move.l d0,(4,a4)	| _AllocVec_re2.100_145, cursor_51(D)->leaf
	clr.l (36,a4)	| cursor_51(D)->valid
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_41(D)], _43
	tst.l d0	| _AllocVec_re2.100_145
	jne .L1417	|
	move.l d0,a4	| _AllocVec_re2.100_145, cursor
	jra .L1417	|
.L1568:
	pea (-1032,a5)	|
	move.l d3,-(sp)	| start_key,
	move.l (-1304,a5),-(sp)	| leaf,
	pea (8,a2)	|
	jsr _node_search.isra.12	|
	move.l d0,d2	|, i
	lea (16,sp),sp	|,
	move.l (-1304,a5),a6	| leaf, prephitmp_479
	clr.l (-1324,a5)	| %sfp
	jra .L1429	|
.L1580:
	move.l (8,a2),a0	| tree_41(D)->ops, tree_41(D)->ops
	move.l _SysBase,a6	| SysBase, _AllocVec_bn
	move.l (4,a0),d0	| _110->key_size, _n1
	moveq #1,d1	|, _n2
	swap d1	| _n2
#APP
| 18 "src/amiga/stdlib.h" 1
	jsr a6@(-0x2ac:W)
| 0 "" 2
#NO_APP
	tst.l d0	| _AllocVec_re2.100_171
	jeq .L1489	|
	move.l d0,(-1324,a5)	| _AllocVec_re2.100_171, %sfp
	move.l (8,a2),a0	| tree_41(D)->ops, tree_41(D)->ops
	move.l (4,a0),d0	| _112->key_size, _113
	subq.l #1,d3	|, tmp294
	move.w #28,a0	|, tmp296
	muls.l d0,d3	| _113, tmp295
	add.l d3,a0	| tmp295, tmp296
	move.l d0,-(sp)	| _113,
	move.l (-1304,a5),d0	| leaf,
	add.l a0,d0	| tmp296,
	move.l d0,-(sp)	|,
	move.l (-1324,a5),-(sp)	| %sfp,
	jsr _memcpy	|
	lea (12,sp),sp	|,
	tst.l d2	| changed
	jne .L1448	|
	jra .L1565	|
.L1436:
	clr.l (-1316,a5)	| %sfp
	clr.l (-1312,a5)	| %sfp
	clr.l d4	| _153
	jra .L1437	|
.L1430:
	cmp.l d3,d2	| reached, i
	jcs .L1433	|
	clr.l d2	| changed
	tst.l d3	| reached
	jeq .L1431	|
	tst.l (-1324,a5)	| %sfp
	jne .L1455	|
	jra .L1580	|
.L1462:
	move.l d2,(-1292,a5,a6.l*4)	| next, path.idx
	lea (-1032,a5),a0	|,, tmp317
	moveq #15,d0	|, tmp318
.L1464:
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	dbra d0,.L1464	| tmp318,
	move.l d6,(-1332,a5)	|, %sfp
	clr.l (a0)+	|
	clr.l (a0)+	|
	pea (-1032,a5)	|
	move.l d2,-(sp)	| next,
	move.l a1,-(sp)	| node.105_269,
	pea (8,a2)	|
	jsr _child_bounds.isra.13	|
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_t *)tree_41(D)].ops, MEM[(const struct bfs_btree_t *)tree_41(D)].ops
	move.l (4,a0),d4	| _294->key_size, _295
	moveq #-32,d0	|, tmp325
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_41(D)], MEM[(struct bfs_bio_t * *)tree_41(D)]
	add.l (4,a0),d0	| _291->block_size, tmp325
	move.l d4,d1	| _295, tmp326
	addq.l #4,d1	|, tmp326
	divu.l d1,d0	| tmp326, tmp328
	muls.l d4,d0	| _295, tmp330
	move.l d0,a1	| tmp330,
	lsl.l #2,d2	|, tmp332
	lea (28,a1,d2.l),a0	|, tmp333
	add.l (-1300,a5),a0	| node, tmp335
	lea (-1296,a5),a1	|,, tmp336
	move.l (a0)+,(a1)+	|,
	move.l (-1296,a5),d2	| v, blk
	subq.w #2,d3	|, level
	lea (16,sp),sp	|,
	move.l (-1320,a5),d4	| %sfp, _147
	move.l a4,d6	| cursor, cursor
.L1468:
	pea (-1300,a5)	|
	pea (-1032,a5)	|
	move.w d3,-(sp)	| level,
	clr.w -(sp)	|
	move.l d4,-(sp)	| _147,
	move.l d2,-(sp)	| blk,
	move.l a2,-(sp)	| tree,
	jsr (a3)	| tmp376
	lea (24,sp),sp	|,
	tst.l d0	| err
	jne .L1554	|
	tst.w d3	| level
	jeq .L1582	|
	move.l (-1036,a5),d1	| path.depth, _282
	moveq #31,d7	|,
	cmp.l d1,d7	| _282,
	jcs .L1491	|
	move.l d2,(-1292,a5,d1.l*4)	| blk, path.blk
	move.l d0,(-1164,a5,d1.l*4)	| err, path.idx
	addq.l #1,d1	|, _282
	move.l d1,(-1036,a5)	| _282, path.depth
	move.l (-1300,a5),a6	| node, node.105_284
	tst.l (16,a6)	|
	jne .L1466	|
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_ops_t * const *)tree_41(D) + 8B],
	move.l (4,a0),d2	| prephitmp_496->key_size, _314
	moveq #-32,d0	|, tmp353
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_41(D)], MEM[(struct bfs_bio_t * *)tree_41(D)]
	add.l (4,a0),d0	| _310->block_size, tmp353
	move.l d2,d1	| _314, tmp354
	addq.l #4,d1	|, tmp354
	divu.l d1,d0	| tmp354, tmp356
	muls.l d2,d0	| _314, tmp358
	lea (-1296,a5),a0	|,, tmp362
	lea (28,a6,d0.l),a1	|, tmp361
	move.l (a1)+,(a0)+	|,
	move.l (-1296,a5),d2	| v, blk
	subq.w #1,d3	|, level
	jra .L1468	|
.L1466:
	move.l (8,a2),a4	| MEM[(const struct bfs_btree_ops_t * const *)tree_41(D) + 8B], prephitmp_496
	move.l (4,a4),-(sp)	| _304->key_size,
	pea (28,a6)	|
	pea (-520,a5)	|
	jsr _memcpy	|
	moveq #1,d0	|,
	move.l d0,(-4,a5)	|, bounds.have_upper
	lea (12,sp),sp	|,
	move.l (4,a4),d2	| prephitmp_496->key_size, _314
	moveq #-32,d0	|, tmp353
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_41(D)], MEM[(struct bfs_bio_t * *)tree_41(D)]
	add.l (4,a0),d0	| _310->block_size, tmp353
	move.l d2,d1	| _314, tmp354
	addq.l #4,d1	|, tmp354
	divu.l d1,d0	| tmp354, tmp356
	muls.l d2,d0	| _314, tmp358
	lea (-1296,a5),a0	|,, tmp362
	lea (28,a6,d0.l),a1	|, tmp361
	move.l (a1)+,(a0)+	|,
	move.l (-1296,a5),d2	| v, blk
	subq.w #1,d3	|, level
	jra .L1468	|
.L1582:
	move.l (-1300,a5),a6	| node, prephitmp_479
	move.l d6,a4	| cursor, cursor
	move.l a6,(-1304,a5)	| prephitmp_479, leaf
	move.l d0,d2	| err, i
	moveq #1,d0	|,
	move.l d0,(-1308,a5)	|, %sfp
	jra .L1469	|
.L1576:
	move.l a0,-(sp)	| _227,
	move.l a6,-(sp)	| leaf.91_85,
	move.l a3,-(sp)	| prephitmp_481,
	jsr _memcpy	|
	lea (12,sp),sp	|,
	jra .L1439	|
.L1581:
	move.l d7,d5	| ctx, ctx
	tst.l d3	| reached
	jeq .L1431	|
	clr.l d2	| changed
	tst.l (-1324,a5)	| %sfp
	jne .L1455	|
	jra .L1580	|
.L1486:
	move.l (-1320,a5),a3	| %sfp, prephitmp_481
	jra .L1435	|
.L1578:
	move.l (60,a2),d6	| tree_41(D)->generation, _91
	move.l d4,a6	| _153, _153
	move.l d5,d7	| ctx, ctx
	jra .L1473	|
.L1547:
	move.l a4,-(sp)	| cursor,
	move.l a2,-(sp)	| tree,
	lea _cursor_current,a3	|, tmp372
	jsr (a3)	| tmp372
	addq.l #8,sp	|,
	tst.l d0	|
	jeq .L1423	|
	move.l (16,a4),d0	| MEM[(const struct bfs_btree_cursor_t *)cursor_1].stop_index, _197
	move.l (4,a4),a0	| MEM[(const struct bfs_btree_cursor_t *)cursor_1].leaf, _198
	cmp.l (16,a0),d0	|, _197
	jcc .L1423	|
	move.l (8,a2),a1	| MEM[(const struct bfs_btree_t *)tree_41(D)].ops, _200
	move.l d3,-(sp)	| start_key,
	muls.l (4,a1),d0	| _200->key_size, tmp221
	pea (28,a0,d0.l)	|
	move.l (a1),a0	| _200->key_compare, _200->key_compare
	jsr (a0)	| _200->key_compare
	move.l d0,(-1308,a5)	|, %sfp
	addq.l #8,sp	|,
	jne .L1423	|
	move.l (4,a4),(-1304,a5)	| cursor_1->leaf, leaf
	move.l (16,a4),d2	| cursor_1->stop_index, i
	moveq #1,d1	|, positioned
	move.l d4,d0	| resume_exclusive, <retval>
	clr.l (32,a4)	| cursor_1->stopped
	jra .L1476	|
.L1422:
	pea (-1304,a5)	|
	pea (-1292,a5)	|
	move.l (-1320,a5),-(sp)	| %sfp,
	move.l d3,-(sp)	| start_key,
	move.l a2,-(sp)	| tree,
	jsr _scan_descend	|
	lea (20,sp),sp	|,
	move.l d3,d1	| start_key, positioned
	moveq #1,d2	|,
	move.l d2,(-1308,a5)	|, %sfp
	move.l d3,d2	| start_key, i
	clr.l (32,a4)	| cursor_1->stopped
	jra .L1476	|
.L1574:
	move.l (8,a2),a1	| MEM[(const struct bfs_btree_t *)tree_41(D)].ops, MEM[(const struct bfs_btree_t *)tree_41(D)].ops
	move.l d3,-(sp)	| start_key,
	pea (28,a0)	|
	move.l (a1),a0	| _211->key_compare, _211->key_compare
	jsr (a0)	| _211->key_compare
	addq.l #8,sp	|,
	tst.l d0	|
	jgt .L1425	|
	subq.l #1,d2	|, tmp228
	move.l (8,a2),a1	| MEM[(const struct bfs_btree_t *)tree_41(D)].ops, _215
	move.w #28,a0	|, tmp229
	muls.l (4,a1),d2	| _215->key_size, tmp228
	move.l (4,a4),d0	| MEM[(const struct bfs_btree_cursor_t *)cursor_1].leaf,
	add.l d2,a0	| tmp228, tmp229
	add.l a0,d0	| tmp230,
	move.l d0,-(sp)	|,
	move.l d3,-(sp)	| start_key,
	move.l (a1),d0	| _215->key_compare, _215->key_compare
	move.l d0,a0	| _215->key_compare,
	jsr (a0)	|
	addq.l #8,sp	|,
	tst.l d0	|
	jgt .L1425	|
	move.l (4,a4),(-1304,a5)	| cursor_1->leaf, leaf
	clr.l d1	| positioned
	move.l d1,d0	| positioned, <retval>
	move.l d1,(-1308,a5)	| positioned, %sfp
	move.l d1,d2	| positioned, i
	clr.l (32,a4)	| cursor_1->stopped
	jra .L1476	|
.L1575:
	moveq #-3,d2	|, err
	move.l (-1324,a5),a1	| %sfp, _n1
	move.l _SysBase,a6	| SysBase, _FreeVec_bn
#APP
| 21 "src/amiga/stdlib.h" 1
	jsr a6@(-0x2b2:W)
| 0 "" 2
#NO_APP
	move.l d2,d0	| err, <retval>
	jra .L1427	|
.L1491:
	moveq #-3,d2	|, err
	tst.l (-1324,a5)	| %sfp
	jne .L1432	|
	jra .L1573	|
.L1421:
	pea (-1304,a5)	|
	pea (-1292,a5)	|
	move.l (-1320,a5),-(sp)	| %sfp,
	move.l d3,-(sp)	| start_key,
	move.l a2,-(sp)	| tree,
	jsr _scan_descend	|
	lea (20,sp),sp	|,
	move.l a4,d1	| cursor, positioned
	moveq #1,d2	|,
	move.l d2,(-1308,a5)	|, %sfp
	move.l a4,d2	| cursor, i
	jra .L1476	|
.L1481:
	moveq #-8,d0	|, <retval>
	movem.l (-1372,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L1569:
	move.l a6,d2	| prephitmp_479, err
	tst.l (-1324,a5)	| %sfp
	jne .L1432	|
	jra .L1573	|
.L1488:
	move.l a4,d2	| cursor, err
	tst.l (-1324,a5)	| %sfp
	jne .L1432	|
	jra .L1573	|
.L1489:
	moveq #-2,d0	|, <retval>
	move.l (-1320,a5),-(sp)	| %sfp,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_41(D)],
	move.l d0,(-1328,a5)	|,
	jsr _bfs_bio_free_buffer	|
	move.l (-1328,a5),d0	|,
	jra .L1414	|
.L1490:
	move.l (-1332,a5),d6	| %sfp,
	clr.l d2	| i
	jra .L1583	|
.L1484:
	moveq #-2,d0	|, <retval>
	movem.l (-1372,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L1573:
	move.l d2,d0	| err, <retval>
	move.l (-1320,a5),-(sp)	| %sfp,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_41(D)],
	move.l d0,(-1328,a5)	|,
	jsr _bfs_bio_free_buffer	|
	move.l (-1328,a5),d0	|,
	jra .L1414	|
	.align	2
	.globl	_bfs_btree_scan_cursor
_bfs_btree_scan_cursor:
	move.l d2,-(sp)	|,
	lea (8,sp),a0	|,, tmp39
	move.l (a0)+,d0	| tree, tree
	move.l (a0)+,d1	| cursor, cursor
	move.l (a0)+,a1	| start_key, start_key
	move.l (a0)+,d2	| cb, cb
	move.l (a0),-(sp)	| ctx,
	move.l d2,-(sp)	| cb,
	clr.l -(sp)	|
	move.l a1,-(sp)	| start_key,
	move.l d1,-(sp)	| cursor,
	move.l d0,-(sp)	| tree,
	jsr _btree_scan_cursor_impl	|
	lea (24,sp),sp	|,
	move.l (sp)+,d2	|,
	rts
	.align	2
	.globl	_bfs_btree_scan_cursor_resume
_bfs_btree_scan_cursor_resume:
	movem.l a3/a2/d5/d4/d3/d2,-(sp)	|,
	lea (28,sp),a0	|,, tmp55
	move.l (a0)+,a2	| tree, tree
	move.l (a0)+,a3	| cursor, cursor
	move.l (a0)+,d2	| cb, cb
	move.l (a0),d3	| ctx, ctx
	move.l a2,d0	|,
	jeq .L1597	|
	move.l (a2),a0	| tree_3(D)->bio, _5
	move.l a0,d0	|,
	jeq .L1597	|
	move.l (a0),a1	| _5->ops, _6
	move.l a1,d0	|,
	jeq .L1597	|
	tst.l (8,a2)	| tree_3(D)->ops
	jeq .L1597	|
	tst.l d2	| cb
	jeq .L1597	|
	move.l (76,a1),a1	| _6->mutation_epoch, _9
	move.l a1,d0	|,
	jeq .L1588	|
	move.l a3,d0	|,
	jeq .L1588	|
	tst.l (28,a3)	| cursor_10(D)->mutation_epoch_valid
	jeq .L1588	|
	move.l a0,-(sp)	| _5,
	jsr (a1)	| _9
	addq.l #4,sp	|,
	moveq #-1,d4	|,
	moveq #-1,d5	|,
	sub.l d1,d5	| epoch,
	subx.l d0,d4	| epoch,
	jeq .L1588	|
	move.l (20,a3),d4	| cursor_10(D)->mutation_epoch,
	move.l (24,a3),d5	| cursor_10(D)->mutation_epoch,
	sub.l d1,d5	| epoch,
	subx.l d0,d4	| epoch,
	jne .L1588	|
	tst.l (12,a2)	| tree_3(D)->root
	jeq .L1588	|
	move.l a3,-(sp)	| cursor,
	move.l a2,-(sp)	| tree,
	jsr _bfs_btree_cursor_stop_key	|
	addq.l #8,sp	|,
	tst.l d0	|
	jeq .L1588	|
	move.l (a2),a0	| MEM[(const struct bfs_btree_t *)tree_3(D)].bio, _21
	moveq #-3,d0	|, <retval>
	move.l a0,d1	|,
	jeq .L1585	|
	move.l (12,a2),d1	| MEM[(const struct bfs_btree_t *)tree_3(D)].root, _22
	jne .L1590	|
	tst.l (16,a2)	| MEM[(const struct bfs_btree_t *)tree_3(D)].height
	jne .L1585	|
	move.l d3,-(sp)	| ctx,
	move.l d2,-(sp)	| cb,
	pea 1.w	|
	clr.l -(sp)	|
	move.l a3,-(sp)	| cursor,
	move.l a2,-(sp)	| tree,
	jsr _btree_scan_cursor_impl	|
	lea (24,sp),sp	|,
.L1585:
	movem.l (sp)+,d2/d3/d4/d5/a2/a3	|,
	rts
.L1590:
	cmp.l (8,a0),d1	| _21->block_count, _22
	jcc .L1585	|
	move.l (16,a2),d1	| MEM[(const struct bfs_btree_t *)tree_3(D)].height, tmp52
	subq.l #1,d1	|, tmp52
	moveq #31,d4	|,
	cmp.l d1,d4	| tmp52,
	jcs .L1585	|
	move.l d3,-(sp)	| ctx,
	move.l d2,-(sp)	| cb,
	pea 1.w	|
	clr.l -(sp)	|
	move.l a3,-(sp)	| cursor,
	move.l a2,-(sp)	| tree,
	jsr _btree_scan_cursor_impl	|
	lea (24,sp),sp	|,
	jra .L1585	|
.L1588:
	moveq #-9,d0	|, <retval>
	movem.l (sp)+,d2/d3/d4/d5/a2/a3	|,
	rts
.L1597:
	moveq #-8,d0	|, <retval>
	movem.l (sp)+,d2/d3/d4/d5/a2/a3	|,
	rts
	.align	2
	.globl	_bfs_btree_search_floor
_bfs_btree_search_floor:
	lea (-36,sp),sp	|,
	movem.l a6/a5/a4/a3/a2/d7/d6/d5/d4/d3/d2,-(sp)	|,
	lea (84,sp),a0	|,,
	move.l (a0)+,a6	| tree, tree
	move.l (a0)+,d5	| key, key
	move.l (a0)+,(92,sp)	| key_out, key_out
	move.l (a0),(96,sp)	| val_out, val_out
	move.l a6,d0	|,
	jeq .L1646	|
	move.l (a6),a1	| tree_16(D)->bio, _18
	move.l a1,d0	|,
	jeq .L1646	|
	tst.l (8,a6)	| tree_16(D)->ops
	jeq .L1646	|
	tst.l d5	| key
	jeq .L1646	|
	tst.l (92,sp)	| key_out
	jeq .L1646	|
	tst.l (96,sp)	| val_out
	jeq .L1646	|
	move.l (12,a6),d0	| tree_16(D)->root, _23
	jeq .L1647	|
	moveq #-3,d2	|,
	cmp.l (8,a1),d0	| _18->block_count, _23
	jcc .L1616	|
	move.l (16,a6),d0	| MEM[(const struct bfs_btree_t *)tree_16(D)].height, tmp203
	subq.l #1,d0	|, tmp203
	moveq #31,d1	|,
	cmp.l d0,d1	| tmp203,
	jcs .L1616	|
	move.l (4,a1),-(sp)	| _18->block_size,
	move.l a1,-(sp)	| _18,
	jsr _bfs_bio_alloc_buffer	|
	addq.l #8,sp	|,
	tst.l d0	| _79
	jeq .L1650	|
	move.l (12,a6),d4	| tree_16(D)->root, blk
	move.w (18,a6),d2	| tree_16(D)->height, expected_level
	subq.w #1,d2	|, expected_level
	moveq #32,d3	|, ivtmp_112
	clr.w (66,sp)	| %sfp
	clr.l (58,sp)	| %sfp
	move.l (58,sp),(62,sp)	| %sfp, %sfp
	moveq #72,d6	|, tmp358
	lea _node_view,a4	|, tmp356
	lea (8,a6),a0	|, tree,
	add.l sp,d6	|, tmp358
	move.l a0,(54,sp)	|, %sfp
	lea _node_search.isra.12,a5	|, tmp360
	move.l d0,(46,sp)	|, %sfp
.L1619:
	pea (68,sp)	|
	clr.l -(sp)	|
	move.w d2,-(sp)	| expected_level,
	clr.w -(sp)	|
	move.l (58,sp),-(sp)	| %sfp,
	move.l d4,-(sp)	| blk,
	move.l a6,-(sp)	| tree,
	jsr (a4)	| tmp356
	move.l d0,d7	|, <retval>
	lea (24,sp),sp	|,
	jne .L1660	|
	move.l d6,-(sp)	| tmp358,
	move.l d5,-(sp)	| key,
	move.l (76,sp),-(sp)	| node,
	move.l (66,sp),-(sp)	| %sfp,
	jsr (a5)	| tmp360
	move.l (84,sp),a0	| node, node.110_36
	lea (16,sp),sp	|,
	tst.w (20,a0)	|
	jeq .L1661	|
	tst.w d2	| expected_level
	jeq .L1662	|
	tst.l (72,sp)	| found
	jeq .L1634	|
	addq.l #1,d0	|, idx
.L1634:
	tst.l d0	| idx
	jeq .L1635	|
	move.w d2,(66,sp)	| expected_level, %sfp
	move.l d0,(58,sp)	| idx, %sfp
	move.l d4,(62,sp)	| blk, %sfp
.L1635:
	move.l (a6),a1	| MEM[(struct bfs_bio_t * *)tree_16(D)], _192
	move.l (8,a6),a2	| MEM[(const struct bfs_btree_t *)tree_16(D)].ops, MEM[(const struct bfs_btree_t *)tree_16(D)].ops
	move.l (4,a2),d1	| _195->key_size, _196
	moveq #-32,d4	|, tmp319
	add.l (4,a1),d4	| _192->block_size, tmp319
	move.l d1,d7	| _196, tmp320
	addq.l #4,d7	|, tmp320
	divu.l d7,d4	| tmp320, tmp322
	muls.l d1,d4	| _196, tmp324
	move.l d4,a3	| tmp324,
	lsl.l #2,d0	|, tmp326
	lea (28,a3,d0.l),a2	|, tmp327
	add.l a2,a0	| tmp327, tmp329
	lea (76,sp),a2	|,, tmp330
	move.l (a0)+,(a2)+	|,
	move.l (76,sp),d4	| v, blk
	subq.w #1,d2	|, expected_level
	subq.w #1,d3	|, ivtmp_112
	jne .L1619	|
	move.l (46,sp),-(sp)	| %sfp,
	move.l a1,-(sp)	| _192,
	jsr _bfs_bio_free_buffer	|
	addq.l #8,sp	|,
	moveq #-3,d2	|,
.L1616:
	move.l d2,d0	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (36,sp),sp	|,
	rts
.L1660:
	move.l (46,sp),-(sp)	| %sfp,
	move.l (a6),-(sp)	| MEM[(struct bfs_bio_t * *)tree_16(D)],
	jsr _bfs_bio_free_buffer	|
	move.l d7,d0	| <retval>,
	addq.l #8,sp	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (36,sp),sp	|,
	rts
.L1661:
	move.l d0,d1	| idx, idx
	move.l a0,a2	| node.110_36, node.110_36
	move.l (46,sp),a3	| %sfp, _79
	tst.l (72,sp)	| found
	jne .L1663	|
	tst.l d0	| idx
	jne .L1664	|
	tst.l (62,sp)	| %sfp
	jeq .L1665	|
	move.l a3,-(sp)	| _79,
	move.l (66,sp),-(sp)	| %sfp,
	move.l a6,-(sp)	| tree,
	lea _node_read,a2	|, tmp353
	jsr (a2)	| tmp353
	move.l d0,d2	|,
	lea (12,sp),sp	|,
	jne .L1626	|
	move.w (66,sp),d0	| %sfp,
	moveq #-3,d2	|,
	cmp.w (20,a3),d0	|,
	jeq .L1666	|
.L1626:
	move.l a3,-(sp)	| _79,
	move.l (a6),-(sp)	| MEM[(struct bfs_bio_t * *)tree_16(D)],
	jsr _bfs_bio_free_buffer	|
	move.l d2,d0	|,
	addq.l #8,sp	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (36,sp),sp	|,
	rts
.L1662:
	move.l (46,sp),-(sp)	| %sfp,
	move.l (a6),-(sp)	| MEM[(struct bfs_bio_t * *)tree_16(D)],
	jsr _bfs_bio_free_buffer	|
	moveq #-3,d1	|,
	move.l d1,d0	|,
	addq.l #8,sp	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (36,sp),sp	|,
	rts
.L1663:
	move.l (8,a6),a1	| tree_16(D)->ops, tree_16(D)->ops
	move.l (4,a1),d0	| _46->key_size, _47
	move.l d1,d2	| idx, tmp214
	muls.l d0,d2	| _47, tmp214
	move.l d0,-(sp)	| _47,
	pea (28,a0,d2.l)	|
	move.l (100,sp),-(sp)	| key_out,
	lea _memcpy,a4	|, tmp221
	move.l d1,(62,sp)	|,
	jsr (a4)	| tmp221
	move.l (8,a6),a0	| tree_16(D)->ops,
	move.l (8,a0),d2	| _49->val_size, _50
	move.l (4,a0),d1	| _49->key_size,
	moveq #-28,d0	|, tmp224
	move.l (a6),a0	| MEM[(struct bfs_bio_t * *)tree_16(D)],
	add.l (4,a0),d0	| _81->block_size, tmp224
	move.l d2,d3	| _50, tmp225
	add.l d1,d3	|, tmp225
	divu.l d3,d0	| tmp225, tmp227
	muls.l d1,d0	|, tmp229
	move.l (62,sp),d1	|,
	move.l d0,a0	| tmp229,
	muls.l d2,d1	| _50, tmp231
	lea (28,a0,d1.l),a1	|, tmp232
	move.l d2,-(sp)	| _50,
	pea (a2,a1.l)	|
	move.l (116,sp),-(sp)	| val_out,
	jsr (a4)	| tmp249
	move.l a3,-(sp)	| _79,
	move.l (a6),-(sp)	| MEM[(struct bfs_bio_t * *)tree_16(D)],
	jsr _bfs_bio_free_buffer	|
	lea (32,sp),sp	|,
.L1670:
	move.l d7,d0	| <retval>,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (36,sp),sp	|,
	rts
.L1665:
	move.l a3,-(sp)	| _79,
	move.l (a6),-(sp)	| MEM[(struct bfs_bio_t * *)tree_16(D)],
	jsr _bfs_bio_free_buffer	|
	moveq #-5,d1	|,
	move.l d1,d0	|,
	addq.l #8,sp	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (36,sp),sp	|,
	rts
.L1666:
	move.l (a6),a0	| MEM[(struct bfs_bio_t * *)tree_16(D)],
	move.l (8,a6),a1	| MEM[(const struct bfs_btree_t *)tree_16(D)].ops, MEM[(const struct bfs_btree_t *)tree_16(D)].ops
	move.l (4,a1),d2	| _169->key_size, _170
	moveq #-32,d0	|, tmp336
	add.l (4,a0),d0	| _166->block_size, tmp336
	move.l d2,d1	| _170, tmp337
	addq.l #4,d1	|, tmp337
	divu.l d1,d0	| tmp337, tmp339
	muls.l d2,d0	| _170, tmp341
	move.l d0,a4	| tmp341,
	move.l (58,sp),d0	| %sfp,
	lea (24,a4,d0.l*4),a1	|, tmp345
	lea (76,sp),a4	|,,
	add.l a3,a1	| _79, tmp347
	move.l (a1)+,(a4)+	|,
	move.l (76,sp),d2	| v, blk
	move.w (66,sp),d3	| %sfp, left_level
	subq.w #1,d3	|, left_level
	move.l a3,-(sp)	| _79,
	move.l a0,-(sp)	|,
	lea _bfs_bio_free_buffer,a3	|, tmp357
	jsr (a3)	| tmp357
	move.l (a6),a1	| MEM[(struct bfs_bio_t * *)tree_16(D)], _124
	move.l (4,a1),-(sp)	| _124->block_size,
	move.l a1,-(sp)	| _124,
	jsr _bfs_bio_alloc_buffer	|
	move.l d0,a4	|, _126
	lea (16,sp),sp	|,
	tst.l d0	| _126
	jeq .L1650	|
	moveq #32,d4	|, ivtmp_69
.L1632:
	move.l a4,-(sp)	| _126,
	move.l d2,-(sp)	| blk,
	move.l a6,-(sp)	| tree,
	jsr (a2)	| tmp353
	lea (12,sp),sp	|,
	tst.l d0	| <retval>
	jne .L1654	|
	cmp.w (20,a4),d3	|, left_level
	jeq .L1629	|
	moveq #-3,d2	|,
	move.l a4,-(sp)	| _126,
	move.l (a6),-(sp)	| MEM[(struct bfs_bio_t * *)tree_16(D)],
	jsr (a3)	| tmp357
	addq.l #8,sp	|,
.L1669:
	move.l d2,d0	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (36,sp),sp	|,
	rts
.L1629:
	move.l (16,a4),d5	|, _136
	jeq .L1667	|
	tst.w d3	| left_level
	jeq .L1668	|
	move.l (a6),a1	| MEM[(struct bfs_bio_t * *)tree_16(D)], _179
	move.l (8,a6),a0	| MEM[(const struct bfs_btree_t *)tree_16(D)].ops, MEM[(const struct bfs_btree_t *)tree_16(D)].ops
	move.l (4,a0),d2	| _182->key_size, _183
	moveq #-32,d0	|, tmp304
	add.l (4,a1),d0	| _179->block_size, tmp304
	move.l d2,d1	| _183, tmp305
	addq.l #4,d1	|, tmp305
	divu.l d1,d0	| tmp305, tmp307
	muls.l d2,d0	| _183, tmp309
	move.l d0,a5	| tmp309,
	lsl.l #2,d5	|, tmp311
	lea (28,a5,d5.l),a0	|, tmp312
	lea (76,sp),a5	|,, tmp315
	add.l a4,a0	| _126, tmp314
	move.l (a0)+,(a5)+	|,
	move.l (76,sp),d2	| v, blk
	subq.w #1,d3	|, left_level
	subq.w #1,d4	|, ivtmp_69
	jne .L1632	|
	move.l a4,-(sp)	| _126,
	move.l a1,-(sp)	| _179,
	jsr (a3)	| tmp357
	moveq #-3,d1	|,
	move.l d1,d0	|,
	addq.l #8,sp	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (36,sp),sp	|,
	rts
.L1654:
	move.l d0,d2	| <retval>,
	move.l a4,-(sp)	| _126,
	move.l (a6),-(sp)	| MEM[(struct bfs_bio_t * *)tree_16(D)],
	jsr (a3)	| tmp357
	addq.l #8,sp	|,
	jra .L1669	|
.L1646:
	moveq #-8,d1	|,
	move.l d1,d0	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (36,sp),sp	|,
	rts
.L1647:
	moveq #-5,d1	|,
	move.l d1,d0	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (36,sp),sp	|,
	rts
.L1664:
	move.l (8,a6),a1	| tree_16(D)->ops, tree_16(D)->ops
	move.l (4,a1),d0	| _52->key_size, _53
	move.l d1,d2	| idx, _54
	subq.l #1,d2	|, _54
	move.l d0,d1	| _53, tmp242
	muls.l d2,d1	| _54, tmp242
	move.l d0,-(sp)	| _53,
	pea (28,a0,d1.l)	|
	move.l (100,sp),-(sp)	| key_out,
	lea _memcpy,a4	|, tmp249
	jsr (a4)	| tmp249
	move.l (8,a6),a0	| tree_16(D)->ops,
	move.l (8,a0),d3	| _56->val_size, _57
	move.l (4,a0),d4	| _56->key_size, _98
	moveq #-28,d0	|, tmp252
	move.l (a6),a0	| MEM[(struct bfs_bio_t * *)tree_16(D)],
	add.l (4,a0),d0	| _95->block_size, tmp252
	move.l d3,d1	| _57, tmp253
	add.l d4,d1	| _98, tmp253
	divu.l d1,d0	| tmp253, tmp255
	muls.l d4,d0	| _98, tmp257
	move.l d0,a0	| tmp257,
	muls.l d3,d2	| _57, tmp259
	lea (28,a0,d2.l),a1	|, tmp260
	move.l d3,-(sp)	| _57,
	pea (a2,a1.l)	|
	move.l (116,sp),-(sp)	| val_out,
	jsr (a4)	| tmp249
	move.l a3,-(sp)	| _79,
	move.l (a6),-(sp)	| MEM[(struct bfs_bio_t * *)tree_16(D)],
	jsr _bfs_bio_free_buffer	|
	lea (32,sp),sp	|,
	jra .L1670	|
.L1650:
	moveq #-2,d1	|,
	move.l d1,d0	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (36,sp),sp	|,
	rts
.L1668:
	move.l (8,a6),a0	| MEM[(const struct bfs_btree_t *)tree_16(D)].ops,
	move.l d0,d3	| <retval>,
	move.l (4,a0),d0	| _139->key_size, _140
	subq.l #1,d5	|, _141
	move.l d0,d1	| _140, tmp275
	muls.l d5,d1	| _141, tmp275
	move.l d0,-(sp)	| _140,
	pea (28,a4,d1.l)	|
	move.l (100,sp),-(sp)	| key_out,
	lea _memcpy,a2	|, tmp282
	jsr (a2)	| tmp282
	move.l (8,a6),a0	| MEM[(const struct bfs_btree_t *)tree_16(D)].ops,
	move.l (8,a0),d1	| _145->val_size, _146
	move.l (4,a0),a0	| _145->key_size,
	moveq #-28,d0	|, tmp285
	move.l (a6),a1	| MEM[(struct bfs_bio_t * *)tree_16(D)],
	add.l (4,a1),d0	| _147->block_size, tmp285
	move.l d1,d2	| _146, tmp286
	add.l a0,d2	|, tmp286
	divu.l d2,d0	| tmp286, tmp288
	move.l a0,d2	|,
	muls.l d0,d2	| tmp288,
	muls.l d1,d5	| _146, tmp292
	move.l d2,a0	|,
	lea (28,a0,d5.l),a0	|,
	move.l d1,-(sp)	| _146,
	pea (a4,a0.l)	|
	move.l (116,sp),-(sp)	| val_out,
	jsr (a2)	| tmp282
	move.l a4,-(sp)	| _126,
	move.l (a6),-(sp)	| MEM[(struct bfs_bio_t * *)tree_16(D)],
	jsr (a3)	| tmp357
	move.l d3,d0	|,
	lea (32,sp),sp	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (36,sp),sp	|,
	rts
.L1667:
	move.l a4,-(sp)	| _126,
	move.l (a6),-(sp)	| MEM[(struct bfs_bio_t * *)tree_16(D)],
	jsr (a3)	| tmp357
	moveq #-5,d1	|,
	move.l d1,d0	|,
	addq.l #8,sp	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (36,sp),sp	|,
	rts
	.align	2
_leaf_remove_at:
	movem.l a4/a3/a2/d7/d6/d5/d4/d3/d2,-(sp)	|,
	move.l (40,sp),a4	| tree, tree
	move.l (44,sp),a2	| buf, buf
	move.l (8,a4),a0	| tree_5(D)->ops, _6
	move.l (48,sp),d2	| idx, idx
	move.l (4,a0),d4	| _6->key_size, ks
	move.l (8,a0),d5	| _6->val_size, vs
	move.l (16,a2),d3	|, _46
	subq.l #1,d3	|, _46
	cmp.l d2,d3	| idx, _46
	jls .L1672	|
	move.l d4,d1	| ks, pretmp_55
	addq.l #1,d2	|, idx
	move.l d2,d0	| idx, _37
	muls.l d1,d0	| pretmp_55, _37
	move.l d0,d6	| _37,
	lea _memcpy,a3	|, tmp93
	sub.l d1,d6	| pretmp_55,
	move.l d4,-(sp)	| ks,
	pea (28,a2,d0.l)	|
	pea (28,a2,d6.l)	|
	jsr (a3)	| tmp93
	move.l (8,a4),a0	| tree_5(D)->ops,
	move.l (4,a0),d7	| _23->key_size, _24
	move.l (8,a0),d1	| _23->val_size, _25
	moveq #-28,d0	|, tmp75
	move.l (a4),a0	| MEM[(struct bfs_bio_t * *)tree_5(D)],
	add.l (4,a0),d0	| _20->block_size, tmp75
	move.l d7,d6	| _24, tmp76
	add.l d1,d6	| _25, tmp76
	divu.l d6,d0	| tmp76, tmp78
	move.w #28,a0	|, keys_end
	muls.l d7,d0	| _24, tmp80
	add.l d0,a0	| tmp80, keys_end
	move.l d2,d0	| idx, _30
	muls.l d1,d0	| _25, _30
	move.l d0,d6	| _30,
	sub.l d1,d6	| _25,
	move.l d6,d1	|, tmp81
	add.l a0,d1	| keys_end, tmp82
	add.l a0,d0	| keys_end, tmp84
	move.l d5,-(sp)	| vs,
	pea (a2,d0.l)	|
	pea (a2,d1.l)	|
	jsr (a3)	| tmp93
	lea (24,sp),sp	|,
	cmp.l d2,d3	| idx, _46
	jls .L1672	|
.L1677:
	move.l (8,a4),a0	| MEM[(const struct bfs_btree_ops_t * *)tree_5(D) + 8B], MEM[(const struct bfs_btree_ops_t * *)tree_5(D) + 8B]
	move.l (4,a0),d1	| pretmp_53->key_size, pretmp_55
	addq.l #1,d2	|, idx
	move.l d2,d0	| idx, _37
	muls.l d1,d0	| pretmp_55, _37
	move.l d0,d6	| _37,
	sub.l d1,d6	| pretmp_55,
	move.l d4,-(sp)	| ks,
	pea (28,a2,d0.l)	|
	pea (28,a2,d6.l)	|
	jsr (a3)	| tmp93
	move.l (8,a4),a0	| tree_5(D)->ops,
	move.l (4,a0),d7	| _23->key_size, _24
	move.l (8,a0),d1	| _23->val_size, _25
	moveq #-28,d0	|, tmp75
	move.l (a4),a0	| MEM[(struct bfs_bio_t * *)tree_5(D)],
	add.l (4,a0),d0	| _20->block_size, tmp75
	move.l d7,d6	| _24, tmp76
	add.l d1,d6	| _25, tmp76
	divu.l d6,d0	| tmp76, tmp78
	move.w #28,a0	|, keys_end
	muls.l d7,d0	| _24, tmp80
	add.l d0,a0	| tmp80, keys_end
	move.l d2,d0	| idx, _30
	muls.l d1,d0	| _25, _30
	move.l d0,d6	| _30,
	sub.l d1,d6	| _25,
	move.l d6,d1	|, tmp81
	add.l a0,d1	| keys_end, tmp82
	add.l a0,d0	| keys_end, tmp84
	move.l d5,-(sp)	| vs,
	pea (a2,d0.l)	|
	pea (a2,d1.l)	|
	jsr (a3)	| tmp93
	lea (24,sp),sp	|,
	cmp.l d2,d3	| idx, _46
	jhi .L1677	|
.L1672:
	move.l d3,(16,a2)	| _46, MEM[(struct bfs_btnode_hdr_t *)buf_4(D)].num_keys
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4	|,
	rts
	.align	2
_internal_remove_at:
	subq.l #8,sp	|,
	movem.l a4/a3/a2/d7/d6/d5/d4/d3/d2,-(sp)	|,
	move.l (48,sp),a3	| tree, tree
	move.l (52,sp),a2	| buf, buf
	move.l (56,sp),d6	| idx, idx
	move.l (8,a3),a0	| tree_8(D)->ops, tree_8(D)->ops
	move.l (16,a2),d5	|, _21
	move.l (4,a0),d4	| _9->key_size, ks
	move.l d5,d3	| _21, _33
	subq.l #1,d3	|, _33
	cmp.l d6,d3	| idx, _33
	jls .L1679	|
	move.l d4,d1	| ks, pretmp_88
	move.l d6,d2	| idx, i
	addq.l #1,d2	|, i
	move.l d2,d0	| i, _26
	muls.l d1,d0	| pretmp_88, _26
	move.l d0,d7	| _26,
	lea _memcpy,a4	|, tmp98
	sub.l d1,d7	| pretmp_88,
	move.l d4,-(sp)	| ks,
	pea (28,a2,d0.l)	|
	pea (28,a2,d7.l)	|
	jsr (a4)	| tmp98
	lea (12,sp),sp	|,
	cmp.l d2,d3	| i, _33
	jls .L1679	|
.L1690:
	move.l (8,a3),a0	| MEM[(const struct bfs_btree_ops_t * *)tree_8(D) + 8B], MEM[(const struct bfs_btree_ops_t * *)tree_8(D) + 8B]
	move.l (4,a0),d1	| pretmp_86->key_size, pretmp_88
	addq.l #1,d2	|, i
	move.l d2,d0	| i, _26
	muls.l d1,d0	| pretmp_88, _26
	move.l d0,d7	| _26,
	sub.l d1,d7	| pretmp_88,
	move.l d4,-(sp)	| ks,
	pea (28,a2,d0.l)	|
	pea (28,a2,d7.l)	|
	jsr (a4)	| tmp98
	lea (12,sp),sp	|,
	cmp.l d2,d3	| i, _33
	jhi .L1690	|
.L1679:
	move.l d6,d4	| idx, i
	addq.l #1,d4	|, i
	cmp.l d4,d5	| i, _21
	jls .L1682	|
	addq.l #2,d6	|, tmp78
	move.l d6,d1	| tmp78, ivtmp.770
	lsl.l #2,d1	|, ivtmp.770
	not.l d4	| tmp102
	add.l d5,d4	| _21, tmp101
	cmp.l d6,d5	| tmp78, _21
	jcs .L1691	|
.L1683:
	move.l (8,a3),a0	| tree_8(D)->ops, tree_8(D)->ops
	move.l (4,a0),d5	| _44->key_size, _45
	moveq #-32,d0	|, tmp81
	move.l (a3),a0	| MEM[(struct bfs_bio_t * *)tree_8(D)], MEM[(struct bfs_bio_t * *)tree_8(D)]
	add.l (4,a0),d0	| _41->block_size, tmp81
	move.l d5,d2	| _45, tmp82
	addq.l #4,d2	|, tmp82
	divu.l d2,d0	| tmp82, tmp84
	muls.l d5,d0	| _45, _48
	move.l d0,a0	| _48,
	lea (28,a0,d1.l),a1	|, tmp88
	lea (40,sp),a4	|,, tmp91
	add.l a2,a1	| buf, tmp90
	move.l (a1)+,(a4)+	|,
	move.l (40,sp),(36,sp)	| v, v
	lea (24,a0,d1.l),a0	|, tmp93
	add.l a2,a0	| buf, tmp97
	lea (36,sp),a1	|,, tmp96
	move.l (a1)+,(a0)+	|,
	addq.l #4,d1	|, ivtmp.770
	dbra d4,.L1683	| tmp101,
	clr.w d4	| tmp101
	subq.l #1,d4	| tmp101
	jcc .L1683	|
.L1682:
	move.l d3,(16,a2)	| _33, MEM[(struct bfs_btnode_hdr_t *)buf_6(D)].num_keys
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4	|,
	addq.l #8,sp	|,
	rts
.L1691:
	move.l (8,a3),a0	| tree_8(D)->ops, tree_8(D)->ops
	clr.l d4	| tmp101
	move.l (4,a0),d5	| _44->key_size, _45
	moveq #-32,d0	|, tmp81
	move.l (a3),a0	| MEM[(struct bfs_bio_t * *)tree_8(D)], MEM[(struct bfs_bio_t * *)tree_8(D)]
	add.l (4,a0),d0	| _41->block_size, tmp81
	move.l d5,d2	| _45, tmp82
	addq.l #4,d2	|, tmp82
	divu.l d2,d0	| tmp82, tmp84
	muls.l d5,d0	| _45, _48
	move.l d0,a0	| _48,
	lea (28,a0,d1.l),a1	|, tmp88
	lea (40,sp),a4	|,, tmp91
	add.l a2,a1	| buf, tmp90
	move.l (a1)+,(a4)+	|,
	move.l (40,sp),(36,sp)	| v, v
	lea (24,a0,d1.l),a0	|, tmp93
	add.l a2,a0	| buf, tmp97
	lea (36,sp),a1	|,, tmp96
	move.l (a1)+,(a0)+	|,
	addq.l #4,d1	|, ivtmp.770
	dbra d4,.L1683	| tmp101,
	clr.w d4	| tmp101
	subq.l #1,d4	| tmp101
	jcc .L1683	|
	jra .L1682	|
	.align	2
	.globl	_bfs_btree_delete
_bfs_btree_delete:
	link.w a5,#-4648	|,
	movem.l a6/a4/a3/a2/d7/d6/d5/d4/d3/d2,-(sp)	|,
	move.l (8,a5),a2	| tree, tree
	move.l (12,a5),d6	| key, key
	move.l a2,d0	|,
	jeq .L1808	|
	move.l (a2),a1	| tree_58(D)->bio, _60
	move.l a1,d0	|,
	jeq .L1808	|
	tst.l (4,a2)	| tree_58(D)->alloc
	jeq .L1808	|
	tst.l d6	| key
	jeq .L1808	|
	clr.l (56,a2)	| tree_58(D)->free_sink_err
	lea (-3276,a5),a0	|,, tmp609
	moveq #50,d0	|, tmp610
.L1694:
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	dbra d0,.L1694	| tmp610,
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	move.l (12,a2),d0	| tree_58(D)->root, _65
	jeq .L1809	|
	moveq #-3,d2	|, <retval>
	cmp.l (8,a1),d0	| _60->block_count, _65
	jcc .L1692	|
	move.l (16,a2),d0	| MEM[(const struct bfs_btree_t *)tree_58(D)].height, tmp611
	subq.l #1,d0	|, tmp611
	moveq #31,d1	|,
	cmp.l d0,d1	| tmp611,
	jcs .L1692	|
	lea (-4308,a5),a0	|,, tmp1161
	moveq #15,d0	|, tmp1162
.L1776:
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	dbra d0,.L1776	| tmp1162,
	clr.l (a0)+	|
	clr.l (a0)+	|
	pea (-4308,a5)	|
	move.l d6,-(sp)	| key,
	move.l a2,-(sp)	| tree,
	jsr _owned_leaf_in_place	|
	move.l d0,a3	|, owned_leaf
	lea (12,sp),sp	|,
	tst.l d0	| owned_leaf
	jeq .L1882	|
	pea (-4564,a5)	|
	move.l d6,-(sp)	| key,
	move.l a3,-(sp)	| owned_leaf,
	pea (8,a2)	|
	jsr _node_search.isra.12	|
	lea (16,sp),sp	|,
	moveq #-5,d2	|, <retval>
	tst.l (-4564,a5)	| found
	jeq .L1692	|
	move.l (16,a2),a0	| tree_58(D)->height, prephitmp_1144
	moveq #1,d1	|, iftmp.117_27
	cmp.l a0,d1	| prephitmp_1144, iftmp.117_27
	jeq .L1697	|
	move.l (8,a2),a1	| MEM[(const struct bfs_btree_t *)tree_58(D)].ops, _322
	moveq #-28,d1	|, tmp616
	move.l (a2),a4	| MEM[(struct bfs_bio_t * *)tree_58(D)], MEM[(struct bfs_bio_t * *)tree_58(D)]
	add.l (4,a4),d1	| _325->block_size, tmp616
	move.l (8,a1),d2	| _322->val_size, tmp617
	add.l (4,a1),d2	| _322->key_size, tmp617
	divu.l d2,d1	| tmp617, tmp619
	lsr.l #1,d1	|, iftmp.117_27
.L1697:
	cmp.l (16,a3),d1	|, iftmp.117_27
	jcs .L1883	|
	tst.l (36,a2)	| MEM[(const struct bfs_btree_t *)tree_58(D)].free_sink.defer
	jeq .L1700	|
.L1885:
	pea (1,a0.l*2)	|
	move.l a2,-(sp)	| tree,
	jsr (_mutation_headroom.part.3)	|
	move.l d0,d2	|, <retval>
	addq.l #8,sp	|,
	jeq .L1884	|
.L1692:
	move.l d2,d0	| <retval>,
	movem.l (-4688,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L1882:
	move.l (16,a2),a0	| tree_58(D)->height, prephitmp_1144
	tst.l (36,a2)	| MEM[(const struct bfs_btree_t *)tree_58(D)].free_sink.defer
	jne .L1885	|
.L1700:
	move.l (a2),a1	| tree_58(D)->bio, _85
	move.l (4,a1),(-4640,a5)	| _85->block_size, %sfp
	move.l a0,d0	| prephitmp_1144,
	muls.l (-4640,a5),d0	| %sfp,
	move.l d0,-(sp)	|,
	move.l a1,-(sp)	| _85,
	lea _bfs_bio_alloc_buffer,a3	|, tmp626
	jsr (a3)	| tmp626
	move.l d0,(-4632,a5)	|, %sfp
	addq.l #8,sp	|,
	jeq .L1813	|
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_58(D)], _424
	move.l (4,a0),-(sp)	| _424->block_size,
	move.l a0,-(sp)	| _424,
	jsr (a3)	| tmp626
	move.l d0,(-4628,a5)	|, %sfp
	addq.l #8,sp	|,
	jeq .L1886	|
	move.l (12,a2),d3	| tree_58(D)->root, blk
	move.l a5,d0	|,
	add.l #-4564,d0	|,
	move.l (-4632,a5),a1	| %sfp,
	moveq #32,d5	|, ivtmp_1048
	clr.l d4	| depth
	lea (8,a2),a0	|, tree,
	lea _node_read,a3	|, tmp1255
	move.l a0,(-4636,a5)	|, %sfp
	move.l a1,d7	|, ivtmp.851
	move.l d0,a6	|, ivtmp.850
.L1707:
	move.l (16,a2),d2	| tree_58(D)->height, _92
	cmp.l d2,d4	| _92, depth
	jcc .L1868	|
	move.l d7,-(sp)	| ivtmp.851,
	move.l d3,-(sp)	| blk,
	move.l a2,-(sp)	| tree,
	jsr (a3)	| tmp1255
	lea (12,sp),sp	|,
	tst.l d0	| <retval>
	jeq .L1704	|
	move.l d0,d2	| <retval>, <retval>
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_58(D)],
.L1708:
	move.l (-4628,a5),-(sp)	| %sfp,
	move.l a0,-(sp)	|,
.L1867:
	lea _bfs_bio_free_buffer,a3	|, tmp1224
	jsr (a3)	| tmp1224
	move.l (-4632,a5),-(sp)	| %sfp,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_58(D)],
	jsr (a3)	| tmp1224
	lea (16,sp),sp	|,
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	jsr _mutation_abort	|
.L1891:
	move.l d2,d0	| <retval>,
	movem.l (-4688,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L1883:
	move.l d0,-(sp)	| idx,
	move.l a3,-(sp)	| owned_leaf,
	move.l a2,-(sp)	| tree,
	jsr _leaf_remove_at	|
	addq.l #1,(60,a2)	|, tree_58(D)->generation
	clr.l d0	|
	movem.l (-4688,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L1704:
	move.w d2,d1	|, tmp630
	subq.w #1,d1	|, tmp630
	sub.w d4,d1	| depth, _97
	move.l d7,a0	| ivtmp.851,
	cmp.w (20,a0),d1	|, _97
	jeq .L1887	|
.L1868:
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_58(D)], pretmp_1149
	moveq #-3,d2	|, <retval>
.L1889:
	move.l (-4628,a5),-(sp)	| %sfp,
	move.l a1,-(sp)	| pretmp_1149,
	jra .L1867	|
.L1887:
	move.l d3,(a6)	| blk, MEM[base: _1011, offset: 0B]
	tst.w d1	| _97
	jeq .L1888	|
	pea (-4572,a5)	|
	move.l d6,-(sp)	| key,
	move.l d7,-(sp)	| ivtmp.851,
	move.l (-4636,a5),-(sp)	| %sfp,
	jsr _node_search.isra.12	|
	lea (16,sp),sp	|,
	tst.l (-4572,a5)	| found
	jeq .L1706	|
	addq.l #1,d0	|, idx
.L1706:
	move.l d0,(4,a6)	| idx, MEM[base: _1011, offset: 4B]
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_58(D)], pretmp_1149
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_t *)tree_58(D)].ops, MEM[(const struct bfs_btree_t *)tree_58(D)].ops
	move.l (4,a0),d2	| _705->key_size, _706
	moveq #-32,d1	|, tmp636
	add.l (4,a1),d1	| _702->block_size, tmp636
	move.l d2,d3	| _706, tmp637
	addq.l #4,d3	|, tmp637
	divu.l d3,d1	| tmp637, tmp639
	muls.l d2,d1	| _706, tmp641
	move.l d1,a4	| tmp641,
	lsl.l #2,d0	|, tmp643
	lea (28,a4,d0.l),a0	|, tmp644
	lea (-4568,a5),a4	|,, tmp647
	add.l d7,a0	| ivtmp.851, tmp646
	move.l (a0)+,(a4)+	|,
	move.l (-4568,a5),d3	| v, blk
	addq.l #1,d4	|, depth
	addq.l #8,a6	|, ivtmp.850
	add.l (-4640,a5),d7	| %sfp, ivtmp.851
	subq.w #1,d5	|, ivtmp_1048
	jne .L1707	|
	moveq #-3,d2	|, <retval>
	jra .L1889	|
.L1884:
	move.l (16,a2),a0	| tree_58(D)->height, prephitmp_1144
	jra .L1700	|
.L1888:
	move.l d7,a6	| ivtmp.851, ivtmp.851
	move.l d0,d2	| <retval>, <retval>
	pea (-4596,a5)	|
	move.l d6,-(sp)	| key,
	move.l d7,-(sp)	| ivtmp.851,
	pea (8,a2)	|
	jsr _node_search.isra.12	|
	lea (16,sp),sp	|,
	tst.l (-4596,a5)	| found
	jne .L1890	|
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_58(D)],
	moveq #-5,d2	|, <retval>
	jra .L1708	|
.L1809:
	moveq #-5,d1	|,
	move.l d1,d0	|,
	movem.l (-4688,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L1808:
	moveq #-8,d1	|,
	move.l d1,d0	|,
	movem.l (-4688,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L1890:
	move.l d0,-(sp)	| idx,
	move.l d7,-(sp)	| ivtmp.851,
	move.l a2,-(sp)	| tree,
	jsr _leaf_remove_at	|
	lea (12,sp),sp	|,
	tst.l d4	| depth
	jne .L1710	|
	tst.l (16,a6)	|
	jne .L1711	|
	move.l (-4564,a5),d0	| path[0].blk, _117
	jne .L1712	|
.L1715:
	clr.l (12,a2)	| tree_58(D)->root
	addq.l #1,(60,a2)	|, tree_58(D)->generation
	clr.l (16,a2)	| tree_58(D)->height
	move.l (-4628,a5),-(sp)	| %sfp,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_58(D)],
	lea _bfs_bio_free_buffer,a3	|, tmp1222
	jsr (a3)	| tmp1222
	move.l (-4632,a5),-(sp)	| %sfp,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_58(D)],
	jsr (a3)	| tmp1222
	lea (16,sp),sp	|,
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	jsr _mutation_commit	|
	move.l (56,a2),d1	| tree_58(D)->free_sink_err,
.L1898:
	move.l d1,d0	|,
	movem.l (-4688,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L1712:
	move.l (12,a6),-(sp)	|,
	move.l (8,a6),-(sp)	|,
	move.l d0,-(sp)	| _117,
	pea (-3276,a5)	|
	jsr (_mutation_retire_txn.part.9)	|
	move.l d0,d2	|, <retval>
	lea (16,sp),sp	|,
	jeq .L1715	|
.L1714:
	move.l (-4628,a5),-(sp)	| %sfp,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_58(D)],
	lea _bfs_bio_free_buffer,a3	|, tmp1224
	jsr (a3)	| tmp1224
	move.l (-4632,a5),-(sp)	| %sfp,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_58(D)],
	jsr (a3)	| tmp1224
	lea (16,sp),sp	|,
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	jsr _mutation_abort	|
	jra .L1891	|
.L1710:
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_t *)tree_58(D)].ops, _269
	move.l (4,a0),(-4636,a5)	| _269->key_size, %sfp
	move.l (8,a0),(-4616,a5)	| _269->val_size, %sfp
	moveq #-28,d0	|, tmp658
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_58(D)], MEM[(struct bfs_bio_t * *)tree_58(D)]
	add.l (4,a0),d0	| _277->block_size, tmp658
	move.l (-4616,a5),d1	| %sfp, tmp659
	add.l (-4636,a5),d1	| %sfp, tmp659
	divu.l d1,d0	| tmp659, tmp661
	lsr.l #1,d0	|, tmp663
	cmp.l (16,a6),d0	|, tmp663
	jls .L1814	|
	move.l d4,d1	| depth,
	subq.l #1,d1	|,
	move.l d1,(-4624,a5)	|, %sfp
	move.l d1,d0	|, tmp664
	muls.l (-4640,a5),d0	| %sfp, tmp664
	add.l (-4632,a5),d0	| %sfp, tmp664
	move.l d0,a4	| tmp664, parent
	move.l (-4560,a5,d1.l*8),(-4620,a5)	| path[_133].child_idx, %sfp
	move.l (16,a4),d7	|, _253
	cmp.l (-4620,a5),d7	| %sfp, _253
	jhi .L1892	|
	tst.l (-4620,a5)	| %sfp
	jne .L1893	|
	tst.l d7	| _253
	jne .L1894	|
	clr.l (-4636,a5)	| %sfp
	move.l #_cow_node,(-4620,a5)	|, %sfp
.L1716:
	pea (-4592,a5)	|
	move.l a6,-(sp)	| ivtmp.851,
	move.l d3,-(sp)	| blk,
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	move.l (-4620,a5),a0	| %sfp,
	jsr (a0)	|
	move.l d0,d2	|, <retval>
	lea (20,sp),sp	|,
	jne .L1739	|
	move.l d4,d6	| depth, d
	lea (-8,d4.l*8),a0	|, _1046
	subq.l #1,d6	|, d
	lea (-4564,a5,a0.l),a1	|, ivtmp.824
	move.l (-4640,a5),d0	| %sfp, tmp872
	muls.l d6,d0	| d, tmp872
	add.l (-4632,a5),d0	| %sfp, tmp872
	move.l d0,a4	| tmp872, ivtmp.826
	lea (-4568,a5,a0.l),a6	|, ivtmp.830
	move.l a1,d3	| ivtmp.824, ivtmp.824
.L1769:
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_t *)tree_58(D)].ops, MEM[(const struct bfs_btree_t *)tree_58(D)].ops
	move.l (8,a6),d1	| MEM[base: _1020, offset: 8B], ci
	move.l (4,a0),d4	| _718->key_size, _719
	moveq #-32,d0	|, tmp877
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_58(D)], MEM[(struct bfs_bio_t * *)tree_58(D)]
	add.l (4,a0),d0	| _715->block_size, tmp877
	move.l d4,d2	| _719, tmp878
	addq.l #4,d2	|, tmp878
	divu.l d2,d0	| tmp878, tmp880
	muls.l d4,d0	| _719, tmp882
	move.l d0,a1	| tmp882,
	lsl.l #2,d1	|, tmp884
	lea (28,a1,d1.l),a0	|, tmp885
	move.l (-4592,a5),(-4588,a5)	| new_blk, v
	add.l a4,a0	| ivtmp.826, _726
	lea (-4588,a5),a1	|,, tmp887
	move.l (a1)+,(a0)+	|,
	tst.l (-4636,a5)	| %sfp
	jeq .L1740	|
	tst.l d6	| d
	jeq .L1741	|
	move.l (8,a2),(-4648,a5)	| MEM[(const struct bfs_btree_t *)tree_58(D)].ops, %sfp
	move.l (-4648,a5),a0	| %sfp,
	move.l (4,a0),d7	| _549->key_size, _550
	moveq #-32,d0	|, tmp891
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_58(D)], MEM[(struct bfs_bio_t * *)tree_58(D)]
	add.l (4,a0),d0	| _546->block_size, tmp891
	move.l d7,d1	| _550, tmp892
	addq.l #4,d1	|, tmp892
	divu.l d1,d0	| tmp892, tmp893
	move.l d0,d1	| tmp893, tmp895
	lsr.l #1,d1	|, tmp895
	cmp.l (16,a4),d1	|, tmp895
	jls .L1816	|
	move.l a4,a1	| ivtmp.826,
	sub.l (-4640,a5),a1	| %sfp,
	move.l a1,(-4616,a5)	|, %sfp
	move.l (a6),(-4624,a5)	| MEM[base: _1020, offset: 0B], %sfp
	move.l (16,a1),d4	|, _555
	cmp.l (-4624,a5),d4	| %sfp, _555
	jhi .L1895	|
	tst.l (-4624,a5)	| %sfp
	jne .L1804	|
	tst.l d4	| _555
	jne .L1896	|
.L1740:
	pea (-4592,a5)	|
	move.l a4,-(sp)	| ivtmp.826,
	move.l d3,a0	| ivtmp.824,
	move.l (a0),-(sp)	| MEM[base: _1027, offset: 0B],
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	move.l (-4620,a5),a0	| %sfp,
	jsr (a0)	|
	move.l d0,d5	|, <retval>
	lea (20,sp),sp	|,
	jne .L1865	|
	subq.l #8,d3	|, ivtmp.824
	sub.l (-4640,a5),a4	| %sfp, ivtmp.826
	subq.l #8,a6	|, ivtmp.830
	dbra d6,.L1769	| d,
	clr.w d6	| d
	subq.l #1,d6	| d
	jcc .L1769	|
.L1802:
	move.l (-4592,a5),d1	| new_blk,
	moveq #1,d0	|,
	cmp.l (16,a2),d0	| tree_58(D)->height,
	jcc .L1770	|
	move.l (-4632,a5),a0	| %sfp,
	tst.w (20,a0)	|
	jeq .L1770	|
	tst.l (16,a0)	|
	jeq .L1897	|
.L1770:
	move.l d1,(12,a2)	|, tree_58(D)->root
	addq.l #1,(60,a2)	|, tree_58(D)->generation
.L1907:
	move.l (-4628,a5),-(sp)	| %sfp,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_58(D)],
	lea _bfs_bio_free_buffer,a3	|, tmp1222
	jsr (a3)	| tmp1222
	move.l (-4632,a5),-(sp)	| %sfp,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_58(D)],
	jsr (a3)	| tmp1222
	lea (16,sp),sp	|,
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	jsr _mutation_commit	|
	move.l (56,a2),d1	| tree_58(D)->free_sink_err,
	jra .L1898	|
.L1896:
	moveq #1,d0	|,
	move.l d0,(-4612,a5)	|, %sfp
.L1800:
	move.l (-4648,a5),a1	| %sfp,
	move.l (4,a1),d1	| _806->key_size, _807
	moveq #-32,d0	|, tmp1044
	add.l (4,a0),d0	| _803->block_size, tmp1044
	move.l d1,d2	| _807, tmp1045
	addq.l #4,d2	|, tmp1045
	divu.l d2,d0	| tmp1045, tmp1047
	muls.l d1,d0	| _807, tmp1049
	move.l (-4612,a5),d1	| %sfp,
	lsl.l #2,d1	|,
	move.l d1,a0	|, tmp1052
	lea (28,a0,d0.l),a0	|, tmp1053
	add.l (-4616,a5),a0	| %sfp, tmp1055
	lea (-4568,a5),a1	|,, tmp1056
	move.l (a0)+,(a1)+	|,
	move.l (-4568,a5),(-4608,a5)	| v, %sfp
	move.w (20,a4),d2	|, _625
	move.l (-4628,a5),-(sp)	| %sfp,
	move.l (-4608,a5),-(sp)	| %sfp,
	move.l a2,-(sp)	| tree,
	jsr (a3)	| tmp1255
	move.l d0,d5	|, <retval>
	lea (12,sp),sp	|,
	jne .L1865	|
	move.l (-4628,a5),a0	| %sfp,
	cmp.w (20,a0),d2	|, _625
	jeq .L1899	|
.L1752:
	moveq #-3,d2	|, <retval>
.L1739:
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_58(D)],
	jra .L1708	|
.L1899:
	move.l (16,a4),(-4612,a5)	|, %sfp
	move.l (-4628,a5),a0	| %sfp,
	move.l (16,a0),d4	|, _633
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_ops_t * *)tree_58(D) + 8B], MEM[(const struct bfs_btree_ops_t * *)tree_58(D) + 8B]
	move.l (4,a0),d0	| _417->key_size, _629
	move.l d0,d1	| _629,
	muls.l (-4612,a5),d1	| %sfp,
	muls.l (-4624,a5),d0	| %sfp, tmp1198
	move.l d7,-(sp)	| _550,
	move.l (-4616,a5),a0	| %sfp,
	pea (28,a0,d0.l)	|
	pea (28,a4,d1.l)	|
	jsr _memcpy	|
	move.l (-4612,a5),d2	| %sfp, _1155
	lea (12,sp),sp	|,
	addq.l #1,d2	|, _1155
	tst.l d4	| _633
	jeq .L1794	|
.L1760:
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_ops_t * *)tree_58(D) + 8B], MEM[(const struct bfs_btree_ops_t * *)tree_58(D) + 8B]
	move.l (4,a0),d0	| _418->key_size, _638
	move.l d5,d1	| i, tmp1059
	add.l d2,d1	| _1155, tmp1059
	muls.l d0,d1	| _638, tmp1060
	muls.l d5,d0	| i, tmp1063
	move.l d7,-(sp)	| _550,
	move.l (-4628,a5),a0	| %sfp,
	pea (28,a0,d0.l)	|
	pea (28,a4,d1.l)	|
	jsr _memcpy	|
	addq.l #1,d5	|, i
	lea (12,sp),sp	|,
	cmp.l d5,d4	| i, _633
	jne .L1760	|
.L1794:
	move.l d2,d5	| _1155, _1122
	lsl.l #2,d5	|, _1122
	clr.l d2	| ivtmp.806
	move.l d2,d7	| ivtmp.806, i
	move.l a6,(-4604,a5)	| ivtmp.830, %sfp
	move.l d3,(-4600,a5)	| ivtmp.824, %sfp
.L1761:
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_t *)tree_58(D)].ops, MEM[(const struct bfs_btree_t *)tree_58(D)].ops
	move.l (4,a0),d3	| _831->key_size, _832
	moveq #-32,d0	|, tmp1074
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_58(D)],
	add.l (4,a0),d0	| _828->block_size, tmp1074
	move.l d3,d1	| _832, tmp1075
	addq.l #4,d1	|, tmp1075
	divu.l d1,d0	| tmp1075, tmp1077
	move.w #28,a0	|, keys_end
	muls.l d3,d0	| _832, tmp1079
	add.l d0,a0	| tmp1079, keys_end
	move.l a0,d0	| keys_end, tmp1081
	add.l d2,d0	| ivtmp.806, tmp1081
	add.l (-4628,a5),d0	| %sfp, tmp1081
	lea (-4568,a5),a6	|,, tmp1084
	move.l d0,a1	| tmp1081, tmp1083
	move.l (a1)+,(a6)+	|,
	move.l (-4568,a5),(-4576,a5)	| v, v
	move.l d5,d0	| _1122, tmp1085
	add.l d2,d0	| ivtmp.806, tmp1085
	add.l a0,d0	| keys_end, tmp1086
	lea (a4,d0.l),a0	| ivtmp.826, tmp1086, tmp1090
	lea (-4576,a5),a1	|,, tmp1089
	move.l (a1)+,(a0)+	|,
	addq.l #1,d7	|, i
	addq.l #4,d2	|, ivtmp.806
	cmp.l d7,d4	| i, _633
	jcc .L1761	|
	move.l (-4604,a5),a6	| %sfp, ivtmp.830
	move.l (-4600,a5),d3	| %sfp, ivtmp.824
	move.l (-4612,a5),a1	| %sfp,
	move.l d4,a0	| _633,
	lea (1,a0,a1.l),a0	|,
	move.l a0,(16,a4)	|, MEM[(struct bfs_btnode_hdr_t *)node_234].num_keys
	tst.l (-4608,a5)	| %sfp
	jne .L1762	|
.L1763:
	move.l (-4624,a5),-(sp)	| %sfp,
	move.l (-4616,a5),-(sp)	| %sfp,
	move.l a2,-(sp)	| tree,
	jsr _internal_remove_at	|
	lea (12,sp),sp	|,
	jra .L1740	|
.L1762:
	move.l (-4628,a5),a0	| %sfp,
	move.l (12,a0),-(sp)	|,
	move.l (8,a0),-(sp)	|,
	move.l (-4608,a5),-(sp)	| %sfp,
	pea (-3276,a5)	|
	jsr (_mutation_retire_txn.part.9)	|
	move.l d0,d5	|, <retval>
	lea (16,sp),sp	|,
	jeq .L1763	|
.L1865:
	move.l d5,d2	| <retval>, <retval>
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_58(D)],
	jra .L1708	|
.L1816:
	clr.l (-4636,a5)	| %sfp
	jra .L1740	|
.L1895:
	move.l (-4624,a5),d1	| %sfp,
	addq.l #1,d1	|,
	move.l d1,(-4612,a5)	|, %sfp
	muls.l d7,d0	| _550, tmp897
	lsl.l #2,d1	|,
	move.l d1,a0	|, tmp899
	lea (28,a0,d0.l),a0	|, tmp900
	add.l a1,a0	|, tmp902
	lea (-4568,a5),a1	|,, tmp903
	move.l (a0)+,(a1)+	|,
	move.l (-4568,a5),(-4608,a5)	| v, %sfp
	move.w (20,a4),d2	|, _559
	move.l (-4628,a5),-(sp)	| %sfp,
	move.l (-4608,a5),-(sp)	| %sfp,
	move.l a2,-(sp)	| tree,
	jsr (a3)	| tmp1255
	move.l d0,d5	|, <retval>
	lea (12,sp),sp	|,
	jne .L1865	|
	move.l (-4628,a5),a0	| %sfp,
	cmp.w (20,a0),d2	|, _559
	jne .L1752	|
	move.l (8,a2),(-4648,a5)	| MEM[(const struct bfs_btree_t *)tree_58(D)].ops, %sfp
	move.l (-4648,a5),a0	| %sfp,
	move.l (4,a0),d1	| _563->key_size, _564
	moveq #-32,d0	|, tmp1181
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_58(D)], MEM[(struct bfs_bio_t * *)tree_58(D)]
	add.l (4,a0),d0	| _560->block_size, tmp1181
	move.l d1,d2	| _564, tmp1182
	addq.l #4,d2	|, tmp1182
	divu.l d2,d0	| tmp1182, tmp1184
	lsr.l #1,d0	|, tmp1186
	move.l (-4628,a5),a1	| %sfp,
	cmp.l (16,a1),d0	|, tmp1186
	jcs .L1900	|
	tst.l (-4624,a5)	| %sfp
	jeq .L1800	|
.L1804:
	move.l (-4624,a5),d0	| %sfp,
	subq.l #1,d0	|,
	move.l d0,(-4612,a5)	|, %sfp
	move.l (-4648,a5),a1	| %sfp,
	move.l (4,a1),d2	| _768->key_size, _769
	lsl.l #2,d0	|,
	move.l d0,(-4604,a5)	|, %sfp
	moveq #-32,d0	|, tmp968
	add.l (4,a0),d0	| _765->block_size, tmp968
	move.l d2,d1	| _769, tmp969
	addq.l #4,d1	|, tmp969
	divu.l d1,d0	| tmp969, tmp971
	move.l (-4604,a5),a1	| %sfp,
	muls.l d2,d0	| _769, tmp973
	lea (28,a1,d0.l),a0	|, tmp975
	add.l (-4616,a5),a0	| %sfp, tmp977
	lea (-4568,a5),a1	|,, tmp978
	move.l (a0)+,(a1)+	|,
	move.l (-4568,a5),(-4600,a5)	| v, %sfp
	move.w (20,a4),d2	|, _592
	move.l (-4628,a5),-(sp)	| %sfp,
	move.l (-4600,a5),-(sp)	| %sfp,
	move.l a2,-(sp)	| tree,
	jsr (a3)	| tmp1255
	move.l d0,d5	|, <retval>
	lea (12,sp),sp	|,
	jne .L1865	|
	move.l (-4628,a5),a0	| %sfp,
	cmp.w (20,a0),d2	|, _592
	jne .L1752	|
	move.l (16,a0),(-4608,a5)	|, %sfp
	move.l (8,a2),(-4648,a5)	| MEM[(const struct bfs_btree_t *)tree_58(D)].ops, %sfp
	move.l (-4648,a5),a0	| %sfp,
	move.l (4,a0),d0	| _596->key_size, _597
	moveq #-32,d2	|, tmp1189
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_58(D)], MEM[(struct bfs_bio_t * *)tree_58(D)]
	add.l (4,a0),d2	| _593->block_size, tmp1189
	move.l d0,d1	| _597, tmp1190
	addq.l #4,d1	|, tmp1190
	divu.l d1,d2	| tmp1190, tmp1191
	move.l d2,d1	| tmp1191, tmp1193
	lsr.l #1,d1	|, tmp1193
	cmp.l (-4608,a5),d1	| %sfp, tmp1193
	jcc .L1901	|
	move.l (16,a4),d5	|, _602
	jeq .L1818	|
	move.l d5,d2	| _602, i
	move.l (-4628,a5),d4	| %sfp, _309
.L1755:
	subq.l #1,d2	|, i
	move.l d2,d1	| i,
	muls.l d0,d1	| _597,
	move.l d1,a0	|, _607
	lea (28,a0,d0.l),a1	|, tmp981
	move.l d7,-(sp)	| _550,
	pea (28,a0,a4.l)	|
	pea (a4,a1.l)	|
	jsr _memcpy	|
	lea (12,sp),sp	|,
	tst.l d2	| i
	jeq .L1754	|
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_ops_t * *)tree_58(D) + 8B], MEM[(const struct bfs_btree_ops_t * *)tree_58(D) + 8B]
	move.l (4,a0),d0	| pretmp_1166->key_size, _597
	jra .L1755	|
.L1813:
	moveq #-2,d1	|,
	move.l d1,d0	|,
	movem.l (-4688,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L1814:
	move.l d2,(-4636,a5)	| <retval>, %sfp
	move.l #_cow_node,(-4620,a5)	|, %sfp
	jra .L1716	|
.L1711:
	pea (-4568,a5)	|
	move.l d7,-(sp)	| ivtmp.851,
	move.l (-4564,a5),-(sp)	| path[0].blk,
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	jsr _cow_node	|
	move.l d0,d2	|, <retval>
	lea (20,sp),sp	|,
	jne .L1714	|
	move.l (-4568,a5),(12,a2)	| new_blk, tree_58(D)->root
	addq.l #1,(60,a2)	|, tree_58(D)->generation
	move.l (-4628,a5),-(sp)	| %sfp,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_58(D)],
	lea _bfs_bio_free_buffer,a3	|, tmp1222
	jsr (a3)	| tmp1222
	move.l (-4632,a5),-(sp)	| %sfp,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_58(D)],
	jsr (a3)	| tmp1222
	lea (16,sp),sp	|,
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	jsr _mutation_commit	|
	move.l (56,a2),d1	| tree_58(D)->free_sink_err,
	jra .L1898	|
.L1901:
	cmp.l (-4624,a5),d4	| %sfp, _555
	jhi .L1902	|
	move.l (-4604,a5),a1	| %sfp,
	muls.l d0,d2	| _597, tmp1097
	lea (28,a1,d2.l),a0	|, tmp1099
	add.l (-4616,a5),a0	| %sfp, tmp1101
	lea (-4568,a5),a1	|,, tmp1102
	move.l (a0)+,(a1)+	|,
	move.l (-4568,a5),(-4624,a5)	| v, %sfp
	move.w (20,a4),d2	|, _648
	move.l (-4628,a5),-(sp)	| %sfp,
	move.l (-4624,a5),-(sp)	| %sfp,
	move.l a2,-(sp)	| tree,
	jsr (a3)	| tmp1255
	move.l d0,d5	|, <retval>
	lea (12,sp),sp	|,
	jne .L1865	|
	move.l (-4628,a5),a0	| %sfp,
	cmp.w (20,a0),d2	|, _648
	jne .L1752	|
	move.l (16,a0),d2	|, _657
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_ops_t * *)tree_58(D) + 8B], MEM[(const struct bfs_btree_ops_t * *)tree_58(D) + 8B]
	move.l (16,a4),d4	|, _656
	move.l (4,a0),d0	| _419->key_size, _652
	move.l d0,d1	| _652, tmp1208
	muls.l d2,d1	| _657, tmp1208
	muls.l (-4612,a5),d0	| %sfp, tmp1211
	move.l d7,-(sp)	| _550,
	move.l (-4616,a5),a0	| %sfp,
	pea (28,a0,d0.l)	|
	move.l (-4628,a5),a0	| %sfp,
	pea (28,a0,d1.l)	|
	jsr _memcpy	|
	lea (12,sp),sp	|,
	addq.l #1,d2	|, _1160
	tst.l d4	| _656
	jeq .L1796	|
.L1765:
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_ops_t * *)tree_58(D) + 8B], MEM[(const struct bfs_btree_ops_t * *)tree_58(D) + 8B]
	move.l (4,a0),d0	| _420->key_size, _661
	move.l d5,d1	| i, tmp1105
	add.l d2,d1	| _1160, tmp1105
	muls.l d0,d1	| _661, tmp1106
	muls.l d5,d0	| i, tmp1109
	move.l d7,-(sp)	| _550,
	pea (28,a4,d0.l)	|
	move.l (-4628,a5),a1	| %sfp,
	pea (28,a1,d1.l)	|
	jsr _memcpy	|
	addq.l #1,d5	|, i
	lea (12,sp),sp	|,
	cmp.l d5,d4	| i, _656
	jne .L1765	|
.L1796:
	move.l d2,d0	| _1160,
	lsl.l #2,d0	|,
	clr.l d7	| ivtmp.814
	move.l d7,d5	| ivtmp.814, i
	move.l a6,(-4608,a5)	| ivtmp.830, %sfp
	move.l d0,(-4600,a5)	| _1065, %sfp
	move.l d2,(-4604,a5)	| _1160, %sfp
.L1766:
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_t *)tree_58(D)].ops, MEM[(const struct bfs_btree_t *)tree_58(D)].ops
	move.l (4,a0),d1	| _869->key_size, _870
	moveq #-32,d0	|, tmp1120
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_58(D)], MEM[(struct bfs_bio_t * *)tree_58(D)]
	add.l (4,a0),d0	| _866->block_size, tmp1120
	move.l d1,d2	| _870, tmp1121
	addq.l #4,d2	|, tmp1121
	divu.l d2,d0	| tmp1121, tmp1123
	move.w #28,a0	|, keys_end
	muls.l d1,d0	| _870, tmp1125
	add.l d0,a0	| tmp1125, keys_end
	move.l a0,d0	| keys_end, tmp1127
	add.l d7,d0	| ivtmp.814, tmp1127
	add.l a4,d0	| ivtmp.826, tmp1129
	move.l d0,a1	| tmp1129,
	lea (-4568,a5),a6	|,, tmp1130
	move.l (a1)+,(a6)+	|,
	move.l (-4568,a5),(-4572,a5)	| v, v
	move.l (-4600,a5),d0	| %sfp, tmp1131
	add.l d7,d0	| ivtmp.814, tmp1131
	add.l a0,d0	| keys_end, tmp1132
	lea (-4572,a5),a1	|,,
	add.l (-4628,a5),d0	| %sfp, tmp1132
	move.l d0,a0	| tmp1132, tmp1136
	move.l (a1)+,(a0)+	|,
	addq.l #1,d5	|, i
	addq.l #4,d7	|, ivtmp.814
	cmp.l d5,d4	| i, _656
	jcc .L1766	|
	move.l (-4608,a5),a6	| %sfp, ivtmp.830
	move.l (-4604,a5),d0	| %sfp,
	move.l (-4628,a5),a0	| %sfp,
	add.l d0,d4	|, _656
	move.l d4,(16,a0)	| _656, MEM[(struct bfs_btnode_hdr_t *)_309].num_keys
	move.l d3,a0	| ivtmp.824,
	move.l (a0),d0	| MEM[base: _1029, offset: 0B], _344
	jne .L1767	|
	move.l (a2),a0	| tree_58(D)->bio, tree_58(D)->bio
	move.l (4,a0),-(sp)	| _345->block_size,
	move.l (-4628,a5),-(sp)	| %sfp,
	move.l a4,-(sp)	| ivtmp.826,
	jsr _memcpy	|
	move.l d3,a0	| ivtmp.824,
	move.l (-4624,a5),(a0)	| %sfp, MEM[base: _1029, offset: 0B]
	move.l (-4612,a5),-(sp)	| %sfp,
	move.l (-4616,a5),-(sp)	| %sfp,
	move.l a2,-(sp)	| tree,
	jsr _internal_remove_at	|
	move.l (-4612,a5),(a6)	| %sfp, MEM[base: _1020, offset: 0B]
	lea (24,sp),sp	|,
	jra .L1740	|
.L1767:
	move.l (12,a4),-(sp)	|,
	move.l (8,a4),-(sp)	|,
	move.l d0,-(sp)	| _344,
	pea (-3276,a5)	|
	jsr (_mutation_retire_txn.part.9)	|
	move.l d0,d5	|, <retval>
	lea (16,sp),sp	|,
	jne .L1865	|
	move.l (a2),a0	| tree_58(D)->bio, tree_58(D)->bio
	move.l (4,a0),-(sp)	| _345->block_size,
	move.l (-4628,a5),-(sp)	| %sfp,
	move.l a4,-(sp)	| ivtmp.826,
	jsr _memcpy	|
	move.l d3,a0	| ivtmp.824,
	move.l (-4624,a5),(a0)	| %sfp, MEM[base: _1029, offset: 0B]
	move.l (-4612,a5),-(sp)	| %sfp,
	move.l (-4616,a5),-(sp)	| %sfp,
	move.l a2,-(sp)	| tree,
	jsr _internal_remove_at	|
	move.l (-4612,a5),(a6)	| %sfp, MEM[base: _1020, offset: 0B]
	lea (24,sp),sp	|,
	jra .L1740	|
.L1754:
	move.l d4,(-4628,a5)	| _309, %sfp
	addq.l #1,d5	|, i
	jeq .L1756	|
	move.l (8,a2),(-4648,a5)	| MEM[(const struct bfs_btree_t *)tree_58(D)].ops, %sfp
	move.l d5,d0	| i, ivtmp.794
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_58(D)], MEM[(struct bfs_bio_t * *)tree_58(D)]
	lsl.l #2,d0	|, ivtmp.794
	move.l d5,d1	| i, tmp1262
	subq.l #1,d1	|, tmp1262
	move.l (-4628,a5),(-4636,a5)	| %sfp, %sfp
	move.l a6,(-4604,a5)	| ivtmp.830, %sfp
	move.l d3,(-4624,a5)	| ivtmp.824, %sfp
	move.l (-4648,a5),d2	| %sfp, MEM[(const struct bfs_btree_t *)tree_58(D)].ops
	move.l a0,a1	| MEM[(struct bfs_bio_t * *)tree_58(D)], MEM[(struct bfs_bio_t * *)tree_58(D)]
	move.l d2,a0	| MEM[(const struct bfs_btree_t *)tree_58(D)].ops, MEM[(const struct bfs_btree_t *)tree_58(D)].ops
.L1757:
	move.l (4,a0),d3	| _793->key_size, _794
	moveq #-32,d2	|, tmp994
	add.l (4,a1),d2	| _790->block_size, tmp994
	move.l d3,d4	| _794, tmp995
	addq.l #4,d4	|, tmp995
	divu.l d4,d2	| tmp995, tmp997
	muls.l d3,d2	| _794, tmp997
	move.l d2,a0	| tmp997, _797
	lea (24,a0,d0.l),a1	|, tmp1001
	move.l a5,d2	|, tmp1004
	add.l #-4568,d2	|, tmp1004
	move.l d2,a6	| tmp1004,
	add.l a4,a1	| ivtmp.826, tmp1003
	move.l (a1)+,(a6)+	|,
	move.l (-4568,a5),(-4580,a5)	| v, v
	lea (28,a0,d0.l),a0	|, tmp1006
	add.l a4,a0	| ivtmp.826, tmp1010
	lea (-4580,a5),a1	|,, tmp1009
	move.l (a1)+,(a0)+	|,
	subq.l #4,d0	|, ivtmp.794
	dbra d1,.L1822	| tmp1262,
	clr.w d1	| tmp1262
	subq.l #1,d1	| tmp1262
	jcc .L1822	|
	move.l (-4636,a5),(-4628,a5)	| %sfp, %sfp
	move.l (-4604,a5),a6	| %sfp, ivtmp.830
	move.l (-4624,a5),d3	| %sfp, ivtmp.824
.L1756:
	move.l (-4612,a5),d0	| %sfp, tmp1013
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_ops_t * *)tree_58(D) + 8B], MEM[(const struct bfs_btree_ops_t * *)tree_58(D) + 8B]
	muls.l (4,a0),d0	| _415->key_size, tmp1013
	move.l d7,-(sp)	| _550,
	move.l (-4616,a5),a0	| %sfp,
	pea (28,a0,d0.l)	|
	pea (28,a4)	|
	jsr _memcpy	|
	move.l (-4608,a5),-(sp)	| %sfp,
	move.l (-4628,a5),-(sp)	| %sfp,
	move.l a2,-(sp)	| tree,
	jsr _get_child	|
	move.l d0,-(sp)	|,
	clr.l -(sp)	|
	move.l a4,-(sp)	| ivtmp.826,
	move.l a2,-(sp)	| tree,
	move.l #_set_child,d2	|, tmp1023
	move.l d2,a1	| tmp1023,
	jsr (a1)	|
	move.l d5,(16,a4)	| i, MEM[(struct bfs_btnode_hdr_t *)node_234].num_keys
	move.l (-4608,a5),d4	| %sfp,
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_ops_t * *)tree_58(D) + 8B], MEM[(const struct bfs_btree_ops_t * *)tree_58(D) + 8B]
	subq.l #1,d4	|,
	move.l (4,a0),d0	| _416->key_size, _613
	move.l (-4612,a5),d1	| %sfp, tmp1025
	muls.l d0,d1	| _613, tmp1025
	lea (36,sp),sp	|,
	muls.l d4,d0	|, tmp1028
	move.l d7,(sp)	| _550,
	move.l (-4628,a5),a0	| %sfp,
	pea (28,a0,d0.l)	|
	move.l (-4616,a5),a1	| %sfp,
	pea (28,a1,d1.l)	|
	jsr _memcpy	|
	move.l (-4628,a5),a0	| %sfp,
	move.l d4,(16,a0)	|, MEM[(struct bfs_btnode_hdr_t *)_309].num_keys
	pea (-4568,a5)	|
	move.l a0,-(sp)	|,
	move.l (-4600,a5),-(sp)	| %sfp,
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	move.l (-4620,a5),a1	| %sfp,
	jsr (a1)	|
	move.l d0,d5	|, <retval>
	lea (32,sp),sp	|,
	jne .L1865	|
	move.l (-4568,a5),-(sp)	| new_sib,
	move.l (-4612,a5),-(sp)	| %sfp,
	move.l (-4616,a5),-(sp)	| %sfp,
	move.l a2,-(sp)	| tree,
	move.l d2,a0	| tmp1023,
	jsr (a0)	|
	lea (16,sp),sp	|,
	move.l d5,(-4636,a5)	| <retval>, %sfp
	jra .L1740	|
.L1822:
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_t *)tree_58(D)].ops, MEM[(const struct bfs_btree_t *)tree_58(D)].ops
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_58(D)], MEM[(struct bfs_bio_t * *)tree_58(D)]
	jra .L1757	|
.L1892:
	move.l (-4620,a5),d1	| %sfp,
	addq.l #1,d1	|,
	move.l d1,(-4612,a5)	|, %sfp
	move.l d1,-(sp)	|,
	move.l d0,-(sp)	| parent,
	move.l a2,-(sp)	| tree,
	move.l #_get_child,d5	|, tmp1247
	move.l d5,a0	| tmp1247,
	jsr (a0)	|
	move.l d0,d6	|, sib_blk
	move.l (-4628,a5),-(sp)	| %sfp,
	move.l d0,-(sp)	| sib_blk,
	move.l a2,-(sp)	| tree,
	jsr (a3)	| tmp1255
	move.l d0,d2	|, <retval>
	lea (24,sp),sp	|,
	jne .L1739	|
	move.l (-4628,a5),a0	| %sfp,
	tst.w (20,a0)	|
	jne .L1868	|
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_t *)tree_58(D)].ops, _238
	move.l (4,a0),d2	| _238->key_size, _219
	moveq #-28,d0	|, tmp1166
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_58(D)], MEM[(struct bfs_bio_t * *)tree_58(D)]
	add.l (4,a1),d0	| _249->block_size, tmp1166
	move.l d2,d1	| _219, tmp1167
	add.l (8,a0),d1	| _238->val_size, tmp1167
	divu.l d1,d0	| tmp1167, tmp1169
	lsr.l #1,d0	|, tmp1171
	move.l (-4628,a5),a0	| %sfp,
	cmp.l (16,a0),d0	|, tmp1171
	jcs .L1903	|
	tst.l (-4620,a5)	| %sfp
	jne .L1803	|
.L1798:
	move.l (-4612,a5),-(sp)	| %sfp,
	move.l a4,-(sp)	| parent,
	move.l a2,-(sp)	| tree,
	move.l d5,a1	| tmp1247,
	jsr (a1)	|
	move.l d0,(-4624,a5)	|, %sfp
	move.l (-4628,a5),-(sp)	| %sfp,
	move.l d0,-(sp)	|,
	move.l a2,-(sp)	| tree,
	jsr (a3)	| tmp1255
	move.l d0,d2	|, <retval>
	lea (24,sp),sp	|,
	jne .L1739	|
	move.l (-4628,a5),a0	| %sfp,
	tst.w (20,a0)	|
	jne .L1868	|
	move.l (16,a6),(-4612,a5)	|, %sfp
	move.l (-4628,a5),a0	| %sfp,
	move.l (16,a0),a1	|, _487
	move.l a1,d0	|,
	jeq .L1786	|
	move.l d2,d7	| <retval>, i
	move.l #_memcpy,d2	|, tmp1236
	move.l a4,(-4600,a5)	| parent, %sfp
	move.l a1,d6	| _487, _487
	move.l d3,(-4608,a5)	| blk, %sfp
	move.l a2,(-4604,a5)	| tree, %sfp
	move.l (-4628,a5),a2	| %sfp, _309
.L1731:
	move.l (-4604,a5),a1	| %sfp,
	move.l (8,a1),a0	| MEM[(const struct bfs_btree_ops_t * *)tree_58(D) + 8B], MEM[(const struct bfs_btree_ops_t * *)tree_58(D) + 8B]
	move.l (4,a0),d0	| _410->key_size, _508
	move.l (-4612,a5),d5	| %sfp, _1019
	add.l d7,d5	| i, _1019
	move.l d0,d1	| _508, tmp787
	muls.l d5,d1	| _1019, tmp787
	muls.l d7,d0	| i, tmp790
	move.l (-4636,a5),-(sp)	| %sfp,
	pea (28,a2,d0.l)	|
	pea (28,a6,d1.l)	|
	move.l d2,a4	| tmp1236,
	jsr (a4)	|
	move.l (-4604,a5),a0	| %sfp,
	move.l (8,a0),a1	| MEM[(const struct bfs_btree_t *)tree_58(D)].ops, _495
	move.l (4,a1),a0	| _495->key_size, _496
	move.l (-4604,a5),a4	| %sfp,
	move.l (8,a1),d0	| _495->val_size, _497
	moveq #-28,d1	|, tmp800
	move.l (a4),a1	| MEM[(struct bfs_bio_t * *)tree_58(D)], MEM[(struct bfs_bio_t * *)tree_58(D)]
	add.l (4,a1),d1	| _492->block_size, tmp800
	lea (a0,d0.l),a1	| _496, _497, tmp801
	move.l a1,d3	| tmp801,
	divu.l d3,d1	|, tmp803
	move.l a0,d3	| _496,
	move.w #28,a0	|, tmp805
	muls.l d1,d3	| tmp803,
	muls.l d0,d5	| _497, tmp806
	add.l d3,a0	|, tmp805
	add.l a0,d5	| keys_end,
	muls.l d7,d0	| i, tmp809
	add.l a0,d0	| keys_end, tmp810
	move.l (-4616,a5),-(sp)	| %sfp,
	pea (a2,d0.l)	|
	pea (a6,d5.l)	|
	move.l d2,a0	| tmp1236,
	jsr (a0)	|
	addq.l #1,d7	|, i
	lea (24,sp),sp	|,
	cmp.l d7,d6	| i, _487
	jne .L1731	|
	move.l (-4600,a5),a4	| %sfp, parent
	move.l d6,a1	| _487, _487
	move.l (-4608,a5),d3	| %sfp, blk
	move.l (-4604,a5),a2	| %sfp, tree
.L1786:
	move.l (-4612,a5),d0	| %sfp,
	add.l a1,d0	| _487,
	move.l d0,(16,a6)	|, MEM[(struct bfs_btnode_hdr_t *)_99].num_keys
	move.l (-4628,a5),a0	| %sfp,
	move.l (24,a0),(24,a6)	|, MEM[(struct bfs_btnode_hdr_t *)_99].right_sibling
	tst.l (-4624,a5)	| %sfp
	jne .L1732	|
.L1733:
	move.l (-4620,a5),-(sp)	| %sfp,
	move.l a4,-(sp)	| parent,
	move.l a2,-(sp)	| tree,
	jsr _internal_remove_at	|
	moveq #1,d0	|,
	lea (12,sp),sp	|,
	move.l d0,(-4636,a5)	|, %sfp
	move.l #_cow_node,(-4620,a5)	|, %sfp
	jra .L1716	|
.L1893:
	move.l #_get_child,d5	|, tmp1247
.L1803:
	move.l (-4620,a5),d0	| %sfp,
	subq.l #1,d0	|,
	move.l d0,(-4612,a5)	|, %sfp
	move.l d0,-(sp)	|,
	move.l a4,-(sp)	| parent,
	move.l a2,-(sp)	| tree,
	move.l d5,a0	| tmp1247,
	jsr (a0)	|
	move.l d0,(-4608,a5)	|, %sfp
	move.l (-4628,a5),-(sp)	| %sfp,
	move.l d0,-(sp)	|,
	move.l a2,-(sp)	| tree,
	jsr (a3)	| tmp1255
	move.l d0,d2	|, <retval>
	lea (24,sp),sp	|,
	jne .L1739	|
	move.l (-4628,a5),a0	| %sfp,
	tst.w (20,a0)	|
	jne .L1868	|
	move.l (16,a0),d6	|, _436
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_t *)tree_58(D)].ops, _430
	moveq #-28,d0	|, tmp1173
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_58(D)], MEM[(struct bfs_bio_t * *)tree_58(D)]
	add.l (4,a1),d0	| _66->block_size, tmp1173
	move.l (4,a0),d1	| _430->key_size, tmp1174
	add.l (8,a0),d1	| _430->val_size, tmp1174
	divu.l d1,d0	| tmp1174, tmp1176
	lsr.l #1,d0	|, tmp1178
	cmp.l d0,d6	| tmp1178, _436
	jls .L1904	|
	move.l (16,a6),(-4624,a5)	|, %sfp
	jeq .L1905	|
	move.l (-4624,a5),d7	| %sfp, i
	move.l #_memcpy,d2	|, tmp1236
.L1727:
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_ops_t * *)tree_58(D) + 8B], MEM[(const struct bfs_btree_ops_t * *)tree_58(D) + 8B]
	move.l (4,a0),a1	| _407->key_size, _457
	subq.l #1,d7	|, i
	move.l a1,d0	| _457,
	muls.l d7,d0	| i,
	lea (28,a1,d0.l),a1	|, tmp754
	move.l (-4636,a5),-(sp)	| %sfp,
	pea (28,a6,d0.l)	|
	pea (a6,a1.l)	|
	move.l d2,a0	| tmp1236,
	jsr (a0)	|
	move.l (8,a2),a1	| MEM[(const struct bfs_btree_t *)tree_58(D)].ops, _444
	move.l (4,a1),a0	| _444->key_size, _445
	move.l (8,a1),d1	| _444->val_size, _446
	moveq #-28,d0	|, tmp765
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_58(D)], MEM[(struct bfs_bio_t * *)tree_58(D)]
	add.l (4,a1),d0	| _441->block_size, tmp765
	move.l a0,d5	| _445, tmp766
	add.l d1,d5	| _446, tmp766
	divu.l d5,d0	| tmp766, tmp768
	move.l a0,d5	| _445,
	move.w #28,a0	|, tmp770
	muls.l d0,d5	| tmp768,
	move.l d7,d0	| i, _451
	muls.l d1,d0	| _446, _451
	add.l d0,d1	| _451, tmp771
	add.l d5,a0	|, tmp770
	lea (a0,d1.l),a1	| keys_end, tmp771, tmp772
	add.l d0,a0	| _451, tmp774
	move.l (-4616,a5),-(sp)	| %sfp,
	pea (a6,a0.l)	|
	pea (a6,a1.l)	|
	move.l d2,a0	| tmp1236,
	jsr (a0)	|
	lea (24,sp),sp	|,
	tst.l d7	| i
	jne .L1727	|
.L1728:
	move.l d6,d5	| _436, _172
	subq.l #1,d5	|, _172
	move.l d5,d0	| _172, tmp712
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_ops_t * *)tree_58(D) + 8B], MEM[(const struct bfs_btree_ops_t * *)tree_58(D) + 8B]
	muls.l (4,a0),d0	| _408->key_size, tmp712
	move.l (-4636,a5),-(sp)	| %sfp,
	move.l (-4628,a5),a0	| %sfp,
	pea (28,a0,d0.l)	|
	lea (28,a6),a0	|, ivtmp.851,
	move.l a0,(-4620,a5)	|, %sfp
	move.l a0,-(sp)	|,
	move.l d2,a0	| tmp1236,
	jsr (a0)	|
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_t *)tree_58(D)].ops, _469
	move.l (4,a0),d7	| _469->key_size, _470
	move.l (8,a0),d6	| _469->val_size, _471
	moveq #-28,d0	|, tmp722
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_58(D)], MEM[(struct bfs_bio_t * *)tree_58(D)]
	add.l (4,a0),d0	| _466->block_size, tmp722
	move.l d7,d1	| _470, tmp723
	add.l d6,d1	| _471, tmp723
	divu.l d1,d0	| tmp723, tmp725
	move.w #28,a0	|, keys_end
	muls.l d7,d0	| _470, tmp727
	add.l d0,a0	| tmp727, keys_end
	move.l d5,d0	| _172, tmp729
	muls.l d6,d0	| _471, tmp729
	add.l a0,d0	| keys_end, tmp730
	move.l (-4616,a5),-(sp)	| %sfp,
	move.l (-4628,a5),a1	| %sfp,
	pea (a1,d0.l)	|
	pea (a6,a0.l)	|
	move.l d2,a0	| tmp1236,
	jsr (a0)	|
	move.l (-4624,a5),d0	| %sfp,
	addq.l #1,d0	|,
	move.l d0,(16,a6)	|, MEM[(struct bfs_btnode_hdr_t *)_99].num_keys
	move.l (-4628,a5),a0	| %sfp,
	move.l d5,(16,a0)	| _172, MEM[(struct bfs_btnode_hdr_t *)_309].num_keys
	move.l (-4612,a5),d0	| %sfp, tmp740
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_ops_t * *)tree_58(D) + 8B], MEM[(const struct bfs_btree_ops_t * *)tree_58(D) + 8B]
	muls.l (4,a0),d0	| _409->key_size, tmp740
	move.l (-4636,a5),-(sp)	| %sfp,
	move.l (-4620,a5),-(sp)	| %sfp,
	pea (28,a4,d0.l)	|
	move.l d2,a1	| tmp1236,
	jsr (a1)	|
	lea (36,sp),sp	|,
	pea (-4568,a5)	|
	move.l (-4628,a5),-(sp)	| %sfp,
	move.l (-4608,a5),-(sp)	| %sfp,
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	move.l #_cow_node,(-4620,a5)	|, %sfp
	move.l (-4620,a5),a0	| %sfp,
	jsr (a0)	|
	move.l d0,d2	|, <retval>
	lea (20,sp),sp	|,
	jne .L1739	|
.L1849:
	move.l (-4568,a5),-(sp)	|,
	move.l (-4612,a5),-(sp)	| %sfp,
	move.l a4,-(sp)	| parent,
	move.l a2,-(sp)	| tree,
	jsr _set_child	|
	lea (16,sp),sp	|,
	move.l d2,(-4636,a5)	| <retval>, %sfp
	jra .L1716	|
.L1732:
	move.l (12,a0),-(sp)	|,
	move.l (8,a0),-(sp)	|,
	move.l (-4624,a5),-(sp)	| %sfp,
	pea (-3276,a5)	|
	jsr (_mutation_retire_txn.part.9)	|
	move.l d0,d2	|, <retval>
	lea (16,sp),sp	|,
	jne .L1739	|
	jra .L1733	|
.L1900:
	move.l (16,a4),d2	|, _581
	move.l d1,d0	| _564,
	muls.l d2,d0	| _581,
	muls.l (-4624,a5),d1	| %sfp, tmp908
	move.l d7,-(sp)	| _550,
	move.l (-4616,a5),a0	| %sfp,
	pea (28,a0,d1.l)	|
	pea (28,a4,d0.l)	|
	jsr _memcpy	|
	move.l d5,-(sp)	| <retval>,
	move.l (-4628,a5),-(sp)	| %sfp,
	move.l a2,-(sp)	| tree,
	jsr _get_child	|
	addq.l #1,d2	|, _258
	move.l d0,-(sp)	|,
	move.l d2,-(sp)	| _258,
	move.l a4,-(sp)	| ivtmp.826,
	move.l a2,-(sp)	| tree,
	jsr _set_child	|
	move.l d2,(16,a4)	| _258, MEM[(struct bfs_btnode_hdr_t *)node_234].num_keys
	move.l (-4624,a5),d0	| %sfp, tmp920
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_ops_t * *)tree_58(D) + 8B], MEM[(const struct bfs_btree_ops_t * *)tree_58(D) + 8B]
	lea (36,sp),sp	|,
	muls.l (4,a0),d0	| _412->key_size, tmp920
	move.l d7,(sp)	| _550,
	move.l (-4628,a5),a0	| %sfp,
	pea (28,a0)	|
	move.l (-4616,a5),a1	| %sfp,
	pea (28,a1,d0.l)	|
	jsr _memcpy	|
	move.l (-4628,a5),a0	| %sfp,
	move.l (16,a0),d4	|, _569
	move.l d4,d2	| _569, _732
	subq.l #1,d2	|, _732
	lea (12,sp),sp	|,
	jeq .L1817	|
	move.l a0,a1	|, _309
	move.l d3,(-4636,a5)	| ivtmp.824, %sfp
.L1745:
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_ops_t * *)tree_58(D) + 8B], MEM[(const struct bfs_btree_ops_t * *)tree_58(D) + 8B]
	addq.l #1,d5	|, i
	move.l (4,a0),d1	| _413->key_size, _585
	move.l d5,d0	| i, _586
	muls.l d1,d0	| _585, _586
	move.l d0,d3	| _586,
	sub.l d1,d3	| _585,
	move.l d7,-(sp)	| _550,
	pea (28,a1,d0.l)	|
	pea (28,a1,d3.l)	|
	move.l a1,(-4644,a5)	|,
	jsr _memcpy	|
	lea (12,sp),sp	|,
	move.l (-4644,a5),a1	|,
	cmp.l d5,d2	| i, _732
	jne .L1745	|
	move.l a1,(-4628,a5)	| _309, %sfp
	move.l (-4636,a5),d3	| %sfp, ivtmp.824
	tst.l d4	| _569
	jeq .L1749	|
	moveq #4,d1	|, ivtmp.785
	move.l d4,d7	| _569, tmp1260
	subq.l #1,d7	|, tmp1260
	move.l a4,(-4636,a5)	| ivtmp.826, %sfp
.L1748:
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_t *)tree_58(D)].ops, MEM[(const struct bfs_btree_t *)tree_58(D)].ops
	move.l (4,a0),d4	| _755->key_size, _756
	moveq #-32,d0	|, tmp947
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_58(D)], MEM[(struct bfs_bio_t * *)tree_58(D)]
	add.l (4,a0),d0	| _752->block_size, tmp947
	move.l d4,d2	| _756, tmp948
	addq.l #4,d2	|, tmp948
	divu.l d2,d0	| tmp948, tmp950
	move.w #28,a0	|, keys_end
	muls.l d4,d0	| _756, tmp952
	add.l d0,a0	| tmp952, keys_end
	move.l a0,d0	| keys_end, tmp954
	add.l d1,d0	| ivtmp.785, tmp954
	add.l (-4628,a5),d0	| %sfp, tmp954
	lea (-4568,a5),a4	|,, tmp957
	move.l d0,a1	| tmp954, tmp956
	move.l (a1)+,(a4)+	|,
	move.l (-4568,a5),(-4584,a5)	| v, v
	lea (-4,a0,d1.l),a0	|, tmp959
	lea (-4584,a5),a1	|,, tmp962
	add.l (-4628,a5),a0	| %sfp, tmp963
	move.l (a1)+,(a0)+	|,
	addq.l #4,d1	|, ivtmp.785
	dbra d7,.L1748	| tmp1260,
	clr.w d7	| tmp1260
	subq.l #1,d7	| tmp1260
	jcc .L1748	|
	move.l (-4636,a5),a4	| %sfp, ivtmp.826
.L1749:
	move.l (-4628,a5),a0	| %sfp,
	move.l d5,(16,a0)	| i, MEM[(struct bfs_btnode_hdr_t *)_309].num_keys
	pea (-4568,a5)	|
	move.l a0,-(sp)	|,
	move.l (-4608,a5),-(sp)	| %sfp,
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	move.l (-4620,a5),a1	| %sfp,
	jsr (a1)	|
	move.l d0,d5	|, <retval>
	lea (20,sp),sp	|,
	jne .L1865	|
	move.l (-4568,a5),-(sp)	| new_sib,
	move.l (-4612,a5),-(sp)	| %sfp,
	move.l (-4616,a5),-(sp)	| %sfp,
	move.l a2,-(sp)	| tree,
	jsr _set_child	|
	lea (16,sp),sp	|,
	move.l d5,(-4636,a5)	| <retval>, %sfp
	jra .L1740	|
.L1894:
	moveq #1,d0	|,
	move.l d0,(-4612,a5)	|, %sfp
	move.l #_get_child,d5	|, tmp1247
	jra .L1798	|
.L1886:
	move.l (-4632,a5),-(sp)	| %sfp,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_58(D)],
	jsr _bfs_bio_free_buffer	|
	moveq #-2,d1	|,
	move.l d1,d0	|,
	movem.l (-4688,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L1904:
	cmp.l (-4620,a5),d7	| %sfp, _253
	jhi .L1906	|
	move.l (-4612,a5),-(sp)	| %sfp,
	move.l a4,-(sp)	| parent,
	move.l a2,-(sp)	| tree,
	move.l d5,a1	| tmp1247,
	jsr (a1)	|
	move.l d0,d5	|, sib_blk
	move.l (-4628,a5),-(sp)	| %sfp,
	move.l d0,-(sp)	| sib_blk,
	move.l a2,-(sp)	| tree,
	jsr (a3)	| tmp1255
	move.l d0,d2	|, <retval>
	lea (24,sp),sp	|,
	jne .L1739	|
	move.l (-4628,a5),a0	| %sfp,
	tst.w (20,a0)	|
	jne .L1868	|
	move.l (16,a0),(-4620,a5)	|, %sfp
	move.l (16,a6),a0	|,
	move.l a0,d0	|,
	jeq .L1788	|
	move.l d2,d7	| <retval>, i
	move.l #_memcpy,d2	|, tmp1236
	move.l a0,(-4604,a5)	|, %sfp
	move.l d3,(-4600,a5)	| blk, %sfp
.L1736:
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_ops_t * *)tree_58(D) + 8B], MEM[(const struct bfs_btree_ops_t * *)tree_58(D) + 8B]
	move.l (4,a0),d0	| _411->key_size, _539
	move.l (-4620,a5),d6	| %sfp, _1018
	add.l d7,d6	| i, _1018
	move.l d0,d1	| _539, tmp825
	muls.l d6,d1	| _1018, tmp825
	muls.l d7,d0	| i, tmp828
	move.l (-4636,a5),-(sp)	| %sfp,
	pea (28,a6,d0.l)	|
	move.l (-4628,a5),a0	| %sfp,
	pea (28,a0,d1.l)	|
	move.l d2,a0	| tmp1236,
	jsr (a0)	|
	move.l (8,a2),a1	| MEM[(const struct bfs_btree_t *)tree_58(D)].ops, _526
	move.l (4,a1),a0	| _526->key_size, _527
	move.l (8,a1),d1	| _526->val_size, _528
	moveq #-28,d0	|,
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_58(D)], MEM[(struct bfs_bio_t * *)tree_58(D)]
	add.l (4,a1),d0	| _523->block_size,
	lea (a0,d1.l),a1	| _527, _528, tmp839
	move.l a1,d3	| tmp839,
	divu.l d3,d0	|,
	move.l d0,(-4608,a5)	|, %sfp
	move.l a0,d3	| _527,
	move.w #28,a0	|, tmp843
	muls.l d0,d3	|,
	move.l d1,d0	| _528, tmp844
	muls.l d6,d0	| _1018, tmp844
	add.l d3,a0	|, tmp843
	add.l a0,d0	| keys_end, tmp845
	muls.l d7,d1	| i, tmp847
	add.l d1,a0	| tmp847, tmp848
	move.l (-4616,a5),-(sp)	| %sfp,
	pea (a6,a0.l)	|
	move.l (-4628,a5),a0	| %sfp,
	pea (a0,d0.l)	|
	move.l d2,a1	| tmp1236,
	jsr (a1)	|
	addq.l #1,d7	|, i
	lea (24,sp),sp	|,
	cmp.l (-4604,a5),d7	| %sfp, i
	jne .L1736	|
	move.l (-4604,a5),a0	| %sfp,
	move.l (-4600,a5),d3	| %sfp, blk
.L1788:
	move.l (-4620,a5),d0	| %sfp,
	add.l a0,d0	|,
	move.l (-4628,a5),a0	| %sfp,
	move.l d0,(16,a0)	|, MEM[(struct bfs_btnode_hdr_t *)_309].num_keys
	move.l (24,a6),(24,a0)	|, MEM[(struct bfs_btnode_hdr_t *)_309].right_sibling
	tst.l d3	| blk
	jne .L1737	|
.L1738:
	move.l (a2),a0	| tree_58(D)->bio, tree_58(D)->bio
	move.l (4,a0),-(sp)	| _222->block_size,
	move.l (-4628,a5),-(sp)	| %sfp,
	move.l a6,-(sp)	| ivtmp.851,
	jsr _memcpy	|
	move.l d5,(-4564,a5,d4.l*8)	| sib_blk, path[depth_1102].blk
	move.l (-4612,a5),-(sp)	| %sfp,
	move.l a4,-(sp)	| parent,
	move.l a2,-(sp)	| tree,
	jsr _internal_remove_at	|
	move.l (-4624,a5),a0	| %sfp,
	move.l (-4612,a5),(-4560,a5,a0.l*8)	| %sfp, path[_133].child_idx
	lea (24,sp),sp	|,
	move.l d5,d3	| sib_blk, blk
	moveq #1,d0	|,
	move.l d0,(-4636,a5)	|, %sfp
	move.l #_cow_node,(-4620,a5)	|, %sfp
	jra .L1716	|
.L1903:
	move.l (16,a6),d5	|, _116
	muls.l d5,d2	| _116, tmp667
	move.l (-4636,a5),-(sp)	| %sfp,
	moveq #28,d0	|,
	add.l (-4628,a5),d0	| %sfp,
	move.l d0,(-4624,a5)	|, %sfp
	move.l d0,-(sp)	|,
	pea (28,a6,d2.l)	|
	move.l #_memcpy,d2	|, tmp1236
	move.l d2,a0	| tmp1236,
	jsr (a0)	|
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_t *)tree_58(D)].ops, _167
	move.l (4,a0),d7	| _167->key_size, _166
	move.l (8,a0),a1	| _167->val_size, _164
	moveq #-28,d0	|, tmp677
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_58(D)], MEM[(struct bfs_bio_t * *)tree_58(D)]
	add.l (4,a0),d0	| _173->block_size, tmp677
	move.l a1,d1	| _164, tmp678
	add.l d7,d1	| _166, tmp678
	divu.l d1,d0	| tmp678, tmp680
	move.w #28,a0	|, keys_end
	muls.l d7,d0	| _166, tmp682
	add.l d0,a0	| tmp682, keys_end
	move.l a1,d0	| _164,
	muls.l d5,d0	| _116,
	add.l a0,d0	| keys_end,
	move.l (-4616,a5),-(sp)	| %sfp,
	move.l (-4628,a5),d1	| %sfp,
	pea (a0,d1.l)	|
	pea (a6,d0.l)	|
	move.l d2,a0	| tmp1236,
	jsr (a0)	|
	addq.l #1,d5	|, _116
	move.l d5,(16,a6)	| _116, MEM[(struct bfs_btnode_hdr_t *)_99].num_keys
	clr.l -(sp)	|
	move.l (-4628,a5),-(sp)	| %sfp,
	move.l a2,-(sp)	| tree,
	jsr _leaf_remove_at	|
	move.l (-4620,a5),d0	| %sfp, tmp696
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_ops_t * *)tree_58(D) + 8B], MEM[(const struct bfs_btree_ops_t * *)tree_58(D) + 8B]
	lea (32,sp),sp	|,
	muls.l (4,a0),d0	| _406->key_size, tmp696
	move.l (-4636,a5),(sp)	| %sfp,
	move.l (-4624,a5),-(sp)	| %sfp,
	pea (28,a4,d0.l)	|
	move.l d2,a1	| tmp1236,
	jsr (a1)	|
	pea (-4568,a5)	|
	move.l (-4628,a5),-(sp)	| %sfp,
	move.l d6,-(sp)	| sib_blk,
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	move.l #_cow_node,(-4620,a5)	|, %sfp
	move.l (-4620,a5),a0	| %sfp,
	jsr (a0)	|
	move.l d0,d2	|, <retval>
	lea (32,sp),sp	|,
	jne .L1739	|
	jra .L1849	|
.L1818:
	moveq #1,d5	|, i
	move.l d5,d0	| i, ivtmp.794
	lsl.l #2,d0	|, ivtmp.794
	move.l d5,d1	| i, tmp1262
	subq.l #1,d1	|, tmp1262
	move.l (-4628,a5),(-4636,a5)	| %sfp, %sfp
	move.l a6,(-4604,a5)	| ivtmp.830, %sfp
	move.l d3,(-4624,a5)	| ivtmp.824, %sfp
	move.l (-4648,a5),d2	| %sfp, MEM[(const struct bfs_btree_t *)tree_58(D)].ops
	move.l a0,a1	| MEM[(struct bfs_bio_t * *)tree_58(D)], MEM[(struct bfs_bio_t * *)tree_58(D)]
	move.l d2,a0	| MEM[(const struct bfs_btree_t *)tree_58(D)].ops, MEM[(const struct bfs_btree_t *)tree_58(D)].ops
	jra .L1757	|
.L1902:
	move.l (-4624,a5),d0	| %sfp,
	addq.l #1,d0	|,
	move.l d0,(-4612,a5)	|, %sfp
	jra .L1800	|
.L1817:
	move.l d2,d5	| _732, i
	moveq #4,d1	|, ivtmp.785
	move.l d4,d7	| _569, tmp1260
	subq.l #1,d7	|, tmp1260
	move.l a4,(-4636,a5)	| ivtmp.826, %sfp
	jra .L1748	|
.L1905:
	move.l #_memcpy,d2	|, tmp1236
	jra .L1728	|
.L1737:
	move.l (12,a6),-(sp)	|,
	move.l (8,a6),-(sp)	|,
	move.l d3,-(sp)	| blk,
	pea (-3276,a5)	|
	jsr (_mutation_retire_txn.part.9)	|
	move.l d0,d2	|, <retval>
	lea (16,sp),sp	|,
	jne .L1739	|
	jra .L1738	|
.L1906:
	move.l (-4620,a5),d0	| %sfp,
	addq.l #1,d0	|,
	move.l d0,(-4612,a5)	|, %sfp
	jra .L1798	|
.L1897:
	clr.l -(sp)	|
	move.l a0,-(sp)	|,
	move.l a2,-(sp)	| tree,
	jsr _get_child	|
	move.l d0,d3	|, final_root
	move.l (20,a2),a0	| MEM[(const uint64_t * *)tree_58(D) + 20B], _405
	lea (12,sp),sp	|,
	move.l a0,d0	|,
	jeq .L1771	|
	move.l (4,a0),a1	| *_405,
	move.l (a0),a0	| *_405, iftmp.35_674
.L1772:
	move.l (-4592,a5),d0	| new_blk, new_blk.126_387
	jne .L1773	|
.L1774:
	subq.l #1,(16,a2)	|, tree_58(D)->height
	move.l d3,(12,a2)	| final_root, tree_58(D)->root
	addq.l #1,(60,a2)	|, tree_58(D)->generation
	jra .L1907	|
.L1741:
	pea (-4592,a5)	|
	move.l a4,-(sp)	| ivtmp.826,
	move.l (-4564,a5),-(sp)	| path[0].blk,
	pea (-3276,a5)	|
	move.l a2,-(sp)	| tree,
	move.l (-4620,a5),a0	| %sfp,
	jsr (a0)	|
	move.l d0,d2	|, <retval>
	lea (20,sp),sp	|,
	jeq .L1802	|
	jra .L1739	|
.L1773:
	move.l a1,-(sp)	|,
	move.l a0,-(sp)	|,
	move.l d0,-(sp)	| new_blk.126_387,
	pea (-3276,a5)	|
	jsr (_mutation_retire_txn.part.9)	|
	move.l d0,d2	|, <retval>
	lea (16,sp),sp	|,
	jne .L1739	|
	jra .L1774	|
.L1771:
	move.l (24,a2),a0	| MEM[(const uint64_t *)tree_58(D) + 24B], iftmp.35_674
	move.l (28,a2),a1	| MEM[(const uint64_t *)tree_58(D) + 24B],
	jra .L1772	|
	.align	2
_block_set_add:
	movem.l a6/a3/a2/d7/d6/d5/d4/d3/d2,-(sp)	|,
	move.l (40,sp),a2	| set, set
	move.l (44,sp),d4	| blk, blk
	move.l a2,d0	|,
	jeq .L1925	|
	tst.l d4	| blk
	jeq .L1925	|
	move.l (4,a2),d1	| set_5(D)->capacity,
	jeq .L1926	|
	move.l d1,d0	|, tmp65
	lsr.l #1,d0	|, tmp65
	cmp.l (8,a2),d0	| set_5(D)->count, tmp65
	jhi .L1940	|
	move.l d1,d0	|, tmp77
	add.l d1,d0	|, tmp77
	cmp.l d1,d0	|, tmp77
	jcs .L1914	|
	cmp.l #1073741823,d0	|, tmp77
	jhi .L1914	|
	move.l d0,d3	| tmp77, prephitmp_81
	lsl.l #2,d3	|, prephitmp_81
	move.l d0,d5	| tmp77, prephitmp_84
.L1910:
	move.l _SysBase,a6	| SysBase, _AllocVec_bn
	move.l d3,d0	| prephitmp_81, _n1
	moveq #1,d1	|, _n2
	swap d1	| _n2
#APP
| 18 "src/amiga/stdlib.h" 1
	jsr a6@(-0x2ac:W)
| 0 "" 2
#NO_APP
	move.l d0,d2	| _AllocVec_re, _AllocVec_re2.100_33
	jeq .L1914	|
	move.l d3,-(sp)	| prephitmp_81,
	clr.l -(sp)	|
	move.l d0,-(sp)	| _AllocVec_re2.100_33,
	jsr _memset	|
	move.l d5,d3	| prephitmp_84, _15
	move.l (a2),a1	| MEM[(bfs_blk_t * *)set_5(D)], pretmp_80
	subq.l #1,d3	|, _15
	move.l a1,a3	| pretmp_80, ivtmp.860
	lea (12,sp),sp	|,
	move.l (4,a2),d6	| MEM[(size_t *)set_5(D) + 4B], pretmp_77
.L1915:
	dbra d6,.L1919	| pretmp_77,
	clr.w d6	| pretmp_77
	subq.l #1,d6	| pretmp_77
	jcc .L1919	|
.L1943:
	move.l a1,d0	|,
	jeq .L1920	|
	move.l _SysBase,a6	| SysBase, _FreeVec_bn
#APP
| 21 "src/amiga/stdlib.h" 1
	jsr a6@(-0x2b2:W)
| 0 "" 2
#NO_APP
.L1920:
	move.l d2,(a2)	| _AllocVec_re2.100_33, MEM[(bfs_blk_t * *)set_5(D)]
	move.l d5,(4,a2)	| prephitmp_84, MEM[(size_t *)set_5(D) + 4B]
	move.l d4,d1	| blk, tmp74
	muls.l #-1640531535,d1	|, tmp74
	and.l d3,d1	| _15, slot
.L1921:
	move.l d1,d0	| slot, tmp75
	lsl.l #2,d0	|, tmp75
	add.l d2,d0	| prephitmp_11, tmp75
	move.l d0,a0	| tmp75, _19
	move.l (a0),d0	| *_19, _20
	jeq .L1941	|
.L1922:
	cmp.l d4,d0	| blk, _20
	jeq .L1927	|
	addq.l #1,d1	|, _21
	and.l d3,d1	| _15, slot
	move.l d1,d0	| slot, tmp75
	lsl.l #2,d0	|, tmp75
	add.l d2,d0	| prephitmp_11, tmp75
	move.l d0,a0	| tmp75, _19
	move.l (a0),d0	| *_19, _20
	jne .L1922	|
.L1941:
	move.l d4,(a0)	| blk, *_19
	addq.l #1,(8,a2)	|, set_5(D)->count
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a6	|,
	rts
.L1940:
	move.l (a2),d2	| set_5(D)->slots, prephitmp_11
	move.l d1,d3	|, _15
	subq.l #1,d3	|, _15
	move.l d4,d1	| blk, tmp74
	muls.l #-1640531535,d1	|, tmp74
	and.l d3,d1	| _15, slot
	jra .L1921	|
.L1919:
	move.l (a3)+,d7	| MEM[base: _87, offset: 0B], blk
	jeq .L1915	|
	move.l d7,d0	| blk, tmp72
	muls.l #-1640531535,d0	|, tmp72
	and.l d3,d0	| _15, slot
	move.l d0,d1	| slot, tmp73
	lsl.l #2,d1	|, tmp73
	add.l d2,d1	| _AllocVec_re2.100_33, tmp73
	move.l d1,a0	| tmp73, _46
	tst.l (a0)	| *_46
	jeq .L1942	|
.L1918:
	addq.l #1,d0	|, _43
	and.l d3,d0	| _15, slot
	move.l d0,d1	| slot, tmp73
	lsl.l #2,d1	|, tmp73
	add.l d2,d1	| _AllocVec_re2.100_33, tmp73
	move.l d1,a0	| tmp73, _46
	tst.l (a0)	| *_46
	jne .L1918	|
.L1942:
	move.l d7,(a0)	| blk, *_46
	dbra d6,.L1919	| pretmp_77,
	clr.w d6	| pretmp_77
	subq.l #1,d6	| pretmp_77
	jcc .L1919	|
	jra .L1943	|
.L1927:
	moveq #-6,d0	|, <retval>
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a6	|,
	rts
.L1926:
	move.l #1024,d3	|, prephitmp_81
	move.l #256,d5	|, prephitmp_84
	jra .L1910	|
.L1914:
	moveq #-2,d0	|, <retval>
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a6	|,
	rts
.L1925:
	moveq #-8,d0	|, <retval>
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a6	|,
	rts
	.align	2
_walk_nodes_recursive:
	lea (-16,sp),sp	|,
	movem.l a6/a5/a4/a3/a2/d7/d6/d5/d4/d3/d2,-(sp)	|,
	lea (64,sp),a0	|,, tmp173
	move.l (a0)+,a2	| tree, tree
	move.l (a0)+,d5	| blk, blk
	move.l (a0)+,a4	| visit, visit
	move.l (a0)+,d2	| depth, depth
	move.l (a0)+,d0	| expected_level, expected_level
	move.l (a0)+,d7	| seen, seen
	move.l (a0)+,a5	| lower, lower
	move.l (a0),(92,sp)	| upper, upper
	move.w d0,d4	| expected_level, expected_level
	clr.l d3	| <retval>
	tst.l d5	| blk
	jeq .L1944	|
	moveq #31,d0	|,
	cmp.l d2,d0	| depth,
	jlt .L1985	|
	move.l d5,-(sp)	| blk,
	move.l d7,-(sp)	| seen,
	jsr _block_set_add	|
	move.l d0,d3	|, <retval>
	addq.l #8,sp	|,
	moveq #-6,d1	|,
	cmp.l d0,d1	| <retval>,
	jeq .L1985	|
	tst.l d0	| <retval>
	jeq .L1988	|
.L1944:
	move.l d3,d0	| <retval>,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (16,sp),sp	|,
	rts
.L1988:
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_20(D)], _74
	move.l (4,a0),-(sp)	| _74->block_size,
	move.l a0,-(sp)	| _74,
	jsr _bfs_bio_alloc_buffer	|
	move.l d0,a3	|, _48
	addq.l #8,sp	|,
	tst.l d0	| _48
	jeq .L1967	|
	move.l d0,-(sp)	| _48,
	move.l d5,-(sp)	| blk,
	move.l a2,-(sp)	| tree,
	jsr _node_read	|
	move.l d0,d3	|, <retval>
	lea (12,sp),sp	|,
	jne .L1948	|
	cmp.w (20,a3),d4	|, expected_level
	jeq .L1949	|
	moveq #-3,d3	|, <retval>
.L1948:
	move.l a3,-(sp)	| _48,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_20(D)],
	jsr _bfs_bio_free_buffer	|
	addq.l #8,sp	|,
.L1990:
	move.l d3,d0	| <retval>,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (16,sp),sp	|,
	rts
.L1949:
	move.l (16,a3),d6	|, _22
	move.l a5,d0	|,
	jeq .L1950	|
	move.l (8,a2),a0	| tree_20(D)->ops, tree_20(D)->ops
	move.l a5,-(sp)	| lower,
	pea (28,a3)	|
	move.l (a0),a0	| _24->key_compare, _24->key_compare
	jsr (a0)	| _24->key_compare
	addq.l #8,sp	|,
	tst.l d0	|
	jlt .L1952	|
.L1950:
	tst.l (92,sp)	| upper
	jeq .L1951	|
	move.l (8,a2),a0	| tree_20(D)->ops, _30
	move.l (92,sp),-(sp)	| upper,
	move.l d6,d0	| _22, tmp125
	subq.l #1,d0	|, tmp125
	muls.l (4,a0),d0	| _30->key_size, tmp126
	pea (28,a3,d0.l)	|
	move.l (a0),a0	| _30->key_compare, _30->key_compare
	jsr (a0)	| _30->key_compare
	addq.l #8,sp	|,
	tst.l d0	|
	jge .L1952	|
.L1951:
	tst.w (20,a3)	|
	jeq .L1953	|
	move.l (a2),a0	| tree_20(D)->bio, _38
	move.l (8,a2),a1	| tree_20(D)->ops, tree_20(D)->ops
	move.l (4,a1),(48,sp)	| _41->key_size, %sfp
	move.w #-32,a6	|, tmp131
	move.l (48,sp),a1	| %sfp, tmp132
	add.l (4,a0),a6	| _38->block_size, tmp131
	addq.l #4,a1	|, tmp132
	move.l a6,d0	| tmp131,
	move.l a1,d1	| tmp132,
	divu.l d1,d0	|,
	move.w #28,a1	|, keys_end
	muls.l (48,sp),d0	| %sfp, tmp136
	add.l d0,a1	| tmp136, keys_end
	tst.w d4	| expected_level
	jeq .L1989	|
	move.w d4,d0	| expected_level, tmp138
	subq.w #1,d0	|, tmp138
	and.l #65535,d0	|,
	move.l d2,d1	| depth, _148
	addq.l #1,d1	|, _148
	move.l a3,d4	| _48, ivtmp.866
	add.l a1,d4	| keys_end, ivtmp.866
	clr.l d2	| i
	lea _walk_nodes_recursive,a6	|, tmp169
	move.l d3,(48,sp)	| <retval>, %sfp
	move.l d0,d3	| _138, _138
	move.l a5,(52,sp)	| lower, %sfp
	move.l d1,a5	| _148, _148
.L1959:
	cmp.l d6,d2	| _22, i
	jeq .L1968	|
	move.l d2,d0	| i, tmp140
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_ops_t * *)tree_20(D) + 8B], MEM[(const struct bfs_btree_ops_t * *)tree_20(D) + 8B]
	muls.l (4,a0),d0	| _72->key_size, tmp140
	lea (28,a3,d0.l),a0	|,
	move.l a0,d1	|, iftmp.130_4
.L1955:
	tst.l d2	| i
	jeq .L1969	|
	move.l d2,d0	| i, tmp143
	subq.l #1,d0	|, tmp143
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_ops_t * *)tree_20(D) + 8B], MEM[(const struct bfs_btree_ops_t * *)tree_20(D) + 8B]
	muls.l (4,a0),d0	| _73->key_size, tmp144
	lea (28,a3,d0.l),a0	|,
	move.l a0,d0	|, iftmp.131_5
.L1956:
	lea (56,sp),a0	|,, tmp148
	move.l d4,a1	| ivtmp.866, ivtmp.866
	move.l (a1)+,(a0)+	|,
	move.l d1,-(sp)	| iftmp.130_4,
	move.l d0,-(sp)	| iftmp.131_5,
	move.l d7,-(sp)	| seen,
	move.l d3,-(sp)	| _138,
	move.l a5,-(sp)	| _148,
	move.l a4,-(sp)	| visit,
	move.l (80,sp),-(sp)	| v,
	move.l a2,-(sp)	| tree,
	jsr (a6)	| tmp169
	lea (32,sp),sp	|,
	tst.l d0	| err
	jne .L1957	|
	tst.l (12,a4)	| visit_55(D)->stopped
	jne .L1957	|
	addq.l #1,d2	|, i
	addq.l #4,d4	|, ivtmp.866
	cmp.l d6,d2	| _22, i
	jls .L1959	|
	move.l (48,sp),d3	| %sfp, <retval>
.L1962:
	move.l (a4),a0	| visit_55(D)->node_cb, _68
	move.l a0,d0	|,
	jeq .L1948	|
	move.l (8,a4),-(sp)	| visit_55(D)->ctx,
	move.l d5,-(sp)	| blk,
	jsr (a0)	| _68
	addq.l #4,sp	|,
	move.l a3,(sp)	| _48,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_20(D)],
	jsr _bfs_bio_free_buffer	|
	addq.l #8,sp	|,
	jra .L1990	|
.L1952:
	move.l a3,-(sp)	| _48,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_20(D)],
	jsr _bfs_bio_free_buffer	|
	addq.l #8,sp	|,
.L1985:
	moveq #-3,d1	|,
	move.l d1,d0	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (16,sp),sp	|,
	rts
.L1953:
	move.l (4,a4),a0	| visit_55(D)->entry_cb, pretmp_127
	move.l a0,d0	|,
	jeq .L1962	|
	tst.l d6	| _22
	jeq .L1962	|
	clr.l d7	| i
.L1964:
	move.l (8,a2),a1	| MEM[(const struct bfs_btree_t *)tree_20(D)].ops, _101
	move.l (4,a1),d2	| _101->key_size, _102
	move.l (8,a1),d4	| _101->val_size, _103
	move.l (8,a4),-(sp)	| visit_55(D)->ctx,
	moveq #-28,d1	|, tmp152
	move.l (a2),a1	| MEM[(struct bfs_bio_t * *)tree_20(D)], MEM[(struct bfs_bio_t * *)tree_20(D)]
	add.l (4,a1),d1	| _98->block_size, tmp152
	move.l d2,d0	| _102, tmp153
	add.l d4,d0	| _103, tmp153
	divu.l d0,d1	| tmp153, tmp155
	muls.l d2,d1	| _102, tmp157
	move.l d1,a5	| tmp157,
	muls.l d7,d4	| i, tmp159
	lea (28,a5,d4.l),a1	|, tmp160
	pea (a3,a1.l)	|
	muls.l d7,d2	| i, tmp162
	pea (28,a3,d2.l)	|
	jsr (a0)	| pretmp_127
	lea (12,sp),sp	|,
	tst.l d0	|
	jeq .L1991	|
	addq.l #1,d7	|, i
	cmp.l d6,d7	| _22, i
	jeq .L1962	|
	move.l (4,a4),a0	| visit_55(D)->entry_cb, pretmp_127
	jra .L1964	|
.L1957:
	move.l a3,-(sp)	| _48,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_20(D)],
	move.l d0,(52,sp)	|,
	jsr _bfs_bio_free_buffer	|
	addq.l #8,sp	|,
	move.l (44,sp),d0	|,
	move.l d0,d1	| err,
	move.l d1,d0	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (16,sp),sp	|,
	rts
.L1991:
	moveq #1,d0	|,
	move.l d0,(12,a4)	|, visit_55(D)->stopped
	move.l a3,-(sp)	| _48,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_20(D)],
	jsr _bfs_bio_free_buffer	|
	move.l d3,d0	| <retval>,
	addq.l #8,sp	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (16,sp),sp	|,
	rts
.L1989:
	move.l a3,-(sp)	| _48,
	move.l a0,-(sp)	| _38,
	jsr _bfs_bio_free_buffer	|
	moveq #-3,d1	|,
	move.l d1,d0	|,
	addq.l #8,sp	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (16,sp),sp	|,
	rts
.L1967:
	moveq #-2,d1	|,
	move.l d1,d0	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (16,sp),sp	|,
	rts
.L1969:
	move.l (52,sp),d0	| %sfp, iftmp.131_5
	jra .L1956	|
.L1968:
	move.l (92,sp),d1	| upper, iftmp.130_4
	jra .L1955	|
	.align	2
	.globl	_bfs_btree_walk
_bfs_btree_walk:
	link.w a5,#-28	|,
	movem.l a6/d5/d4/d3/d2,-(sp)	|,
	lea (8,a5),a0	|,, tmp56
	move.l (a0)+,a1	| tree, tree
	move.l (a0)+,d1	| node_cb, node_cb
	move.l (a0)+,d3	| entry_cb, entry_cb
	move.l (a0),d4	| ctx, ctx
	move.l a1,d0	|,
	jeq .L1999	|
	tst.l (a1)	| tree_3(D)->bio
	jeq .L1999	|
	tst.l (8,a1)	| tree_3(D)->ops
	jeq .L1999	|
	tst.l d1	| node_cb
	jeq .L2007	|
.L1994:
	move.l (12,a1),d0	| tree_3(D)->root, _9
	jeq .L2008	|
	move.l (16,a1),d2	| tree_3(D)->height, _11
	move.l d2,a0	| _11, tmp47
	subq.l #1,a0	|, tmp47
	moveq #31,d5	|,
	cmp.l a0,d5	| tmp47,
	jcs .L2001	|
	lea (-28,a5),a0	|,, tmp48
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	lea (-16,a5),a0	|,, tmp49
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	move.l d1,(-16,a5)	| node_cb, visit.node_cb
	move.l d3,(-12,a5)	| entry_cb, visit.entry_cb
	move.l d4,(-8,a5)	| ctx, visit.ctx
	clr.l -(sp)	|
	clr.l -(sp)	|
	pea (-28,a5)	|
	subq.w #1,d2	|, tmp51
	move.w d2,-(sp)	| tmp51,
	clr.w -(sp)	|
	clr.l -(sp)	|
	pea (-16,a5)	|
	move.l d0,-(sp)	| _9,
	move.l a1,-(sp)	| tree,
	jsr _walk_nodes_recursive	|
	move.l (-28,a5),a1	| seen.slots, _26
	move.l d0,d2	|, <retval>
	move.l a1,d0	|,
	jeq .L1992	|
	move.l _SysBase,a6	| SysBase, _FreeVec_bn
#APP
| 21 "src/amiga/stdlib.h" 1
	jsr a6@(-0x2b2:W)
| 0 "" 2
#NO_APP
.L1992:
	move.l d2,d0	| <retval>,
	movem.l (-48,a5),d2/d3/d4/d5/a6	|,
	unlk a5	|
	rts
.L2008:
	tst.l (16,a1)	| tree_3(D)->height
	jne .L2001	|
	move.l d0,d1	| _9,
	move.l d1,d0	|,
	movem.l (-48,a5),d2/d3/d4/d5/a6	|,
	unlk a5	|
	rts
.L2007:
	tst.l d3	| entry_cb
	jne .L1994	|
.L1999:
	moveq #-8,d1	|,
	move.l d1,d0	|,
	movem.l (-48,a5),d2/d3/d4/d5/a6	|,
	unlk a5	|
	rts
.L2001:
	moveq #-3,d1	|,
	move.l d1,d0	|,
	movem.l (-48,a5),d2/d3/d4/d5/a6	|,
	unlk a5	|
	rts
	.align	2
	.globl	_bfs_btree_walk_nodes
_bfs_btree_walk_nodes:
	move.l (8,sp),d0	| cb, cb
	jeq .L2011	|
	move.l (12,sp),-(sp)	| ctx,
	clr.l -(sp)	|
	move.l d0,-(sp)	| cb,
	move.l (16,sp),-(sp)	| tree,
	jsr _bfs_btree_walk	|
	lea (16,sp),sp	|,
	rts
.L2011:
	moveq #-8,d0	|, <retval>
	rts
	.align	2
_compact_cb:
	move.l a2,-(sp)	|,
	move.l (16,sp),a2	| ctx, ctx
	move.l (12,sp),-(sp)	| val,
	move.l (12,sp),-(sp)	| key,
	move.l (a2)+,-(sp)	| MEM[(struct compact_ctx_t *)ctx_1(D)].new_tree,
	jsr _bfs_btree_insert	|
	move.l d0,(a2)	| _7, MEM[(struct compact_ctx_t *)ctx_1(D)].rc
	lea (12,sp),sp	|,
	seq d0	| tmp41
	extb.l d0	| tmp40
	neg.l d0	|
	move.l (sp)+,a2	|,
	rts
	.align	2
	.globl	_bfs_btree_free_block
_bfs_btree_free_block:
	subq.l #4,sp	|,
	movem.l a3/a2/d7/d6/d5/d4/d2,-(sp)	|,
	move.l (36,sp),a2	| tree, tree
	move.l (40,sp),d2	| blk, blk
	move.l a2,d0	|,
	jeq .L2026	|
	move.l (a2),a0	| tree_4(D)->bio, _6
	move.l a0,d0	|,
	jeq .L2026	|
	tst.l (4,a2)	| tree_4(D)->alloc
	jeq .L2026	|
	tst.l d2	| blk
	jeq .L2026	|
	move.l (4,a0),-(sp)	| _6->block_size,
	move.l a0,-(sp)	| _6,
	jsr _bfs_bio_alloc_buffer	|
	move.l d0,a3	|, _15
	addq.l #8,sp	|,
	tst.l d0	| _15
	jeq .L2027	|
	move.l d0,-(sp)	| _15,
	move.l d2,-(sp)	| blk,
	move.l a2,-(sp)	| tree,
	jsr _node_read	|
	lea (12,sp),sp	|,
	tst.l d0	| <retval>
	jeq .L2040	|
	move.l a3,-(sp)	| _15,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_4(D)],
	move.l d0,(36,sp)	|,
	jsr _bfs_bio_free_buffer	|
	addq.l #8,sp	|,
	move.l (28,sp),d0	|,
	movem.l (sp)+,d2/d4/d5/d6/d7/a2/a3	|,
	addq.l #4,sp	|,
	rts
.L2040:
	move.l (20,a2),a0	| MEM[(const uint64_t * *)tree_4(D) + 20B], _19
	move.l (8,a3),d4	|, _18
	move.l (12,a3),d5	|,
	move.l a0,d0	|,
	jeq .L2018	|
	move.l (a0),d0	| *_19, iftmp.35_20
	move.l (4,a0),d1	| *_19,
.L2019:
	move.l d4,d6	| _18,
	move.l d5,d7	|,
	sub.l d1,d7	| iftmp.35_20,
	subx.l d0,d6	| iftmp.35_20,
	jcc .L2041	|
	move.l (36,a2),a0	| tree_4(D)->free_sink.defer, _25
	move.l a0,d0	|,
	jeq .L2022	|
	move.l d2,-(sp)	| blk,
	move.l (32,a2),-(sp)	| tree_4(D)->free_sink.ctx,
	jsr (a0)	| _25
	addq.l #8,sp	|,
	tst.l d0	| derr
	jeq .L2022	|
	tst.l (56,a2)	|
	jne .L2022	|
	move.l d0,(56,a2)	| derr,
.L2022:
	move.l a3,-(sp)	| _15,
	move.l (a2)+,-(sp)	| MEM[(struct bfs_bio_t * *)tree_4(D)],
	jsr _bfs_bio_free_buffer	|
	move.l (52,a2),d0	| tree_4(D)->free_sink_err, <retval>
	addq.l #8,sp	|,
	movem.l (sp)+,d2/d4/d5/d6/d7/a2/a3	|,
	addq.l #4,sp	|,
	rts
.L2026:
	moveq #-8,d0	|, <retval>
	movem.l (sp)+,d2/d4/d5/d6/d7/a2/a3	|,
	addq.l #4,sp	|,
	rts
.L2041:
	move.l d2,-(sp)	| blk,
	move.l a2,-(sp)	| tree,
	jsr _node_dealloc	|
	addq.l #8,sp	|,
	tst.l d0	| _23
	jeq .L2022	|
	tst.l (56,a2)	|
	jne .L2022	|
	move.l d0,(56,a2)	| derr,
	jra .L2022	|
.L2018:
	move.l (24,a2),d0	| MEM[(const uint64_t *)tree_4(D) + 24B], iftmp.35_20
	move.l (28,a2),d1	| MEM[(const uint64_t *)tree_4(D) + 24B],
	jra .L2019	|
.L2027:
	moveq #-2,d0	|, <retval>
	movem.l (sp)+,d2/d4/d5/d6/d7/a2/a3	|,
	addq.l #4,sp	|,
	rts
	.align	2
_utilization_walk_recursive:
	lea (-32,sp),sp	|,
	movem.l a6/a5/a4/a3/a2/d7/d6/d5/d4/d3/d2,-(sp)	|,
	lea (80,sp),a0	|,, tmp171
	move.l (a0)+,a2	| tree, tree
	move.l (a0)+,d2	| blk, blk
	move.l (a0)+,a3	| ctx, ctx
	move.l (a0)+,d5	| depth, depth
	move.l (a0)+,d4	| expected_level, expected_level
	move.l (a0)+,d7	| lower, lower
	move.l (a0),(104,sp)	| upper, upper
	clr.l d3	| <retval>
	tst.l d2	| blk
	jeq .L2042	|
	moveq #31,d0	|,
	cmp.l d5,d0	| depth,
	jlt .L2070	|
	move.l d2,-(sp)	| blk,
	pea (16,a3)	|
	jsr _block_set_add	|
	move.l d0,d3	|, <retval>
	addq.l #8,sp	|,
	moveq #-6,d1	|,
	cmp.l d0,d1	| <retval>,
	jeq .L2070	|
	tst.l d0	| <retval>
	jeq .L2073	|
.L2042:
	move.l d3,d0	| <retval>,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (32,sp),sp	|,
	rts
.L2073:
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_18(D)], _61
	move.l (4,a0),-(sp)	| _61->block_size,
	move.l a0,-(sp)	| _61,
	jsr _bfs_bio_alloc_buffer	|
	move.l d0,a5	|, _44
	addq.l #8,sp	|,
	tst.l d0	| _44
	jeq .L2060	|
	move.l d0,-(sp)	| _44,
	move.l d2,-(sp)	| blk,
	move.l a2,-(sp)	| tree,
	jsr _node_read	|
	move.l d0,d3	|, <retval>
	lea (12,sp),sp	|,
	jne .L2046	|
	cmp.w (20,a5),d4	|, expected_level
	jeq .L2047	|
	moveq #-3,d3	|, <retval>
.L2046:
	move.l a5,-(sp)	| _44,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_18(D)],
	jsr _bfs_bio_free_buffer	|
	move.l d3,d0	| <retval>,
	addq.l #8,sp	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (32,sp),sp	|,
	rts
.L2047:
	move.l (16,a5),d6	|, _20
	tst.l d7	| lower
	jeq .L2048	|
	move.l (8,a2),a0	| tree_18(D)->ops, tree_18(D)->ops
	move.l d7,-(sp)	| lower,
	pea (28,a5)	|
	move.l (a0),a0	| _22->key_compare, _22->key_compare
	jsr (a0)	| _22->key_compare
	addq.l #8,sp	|,
	tst.l d0	|
	jlt .L2050	|
.L2048:
	tst.l (104,sp)	| upper
	jeq .L2049	|
	move.l (8,a2),a0	| tree_18(D)->ops, _28
	move.l (104,sp),-(sp)	| upper,
	move.l d6,d0	| _20, tmp119
	subq.l #1,d0	|, tmp119
	muls.l (4,a0),d0	| _28->key_size, tmp120
	pea (28,a5,d0.l)	|
	move.l (a0),a0	| _28->key_compare, _28->key_compare
	jsr (a0)	| _28->key_compare
	addq.l #8,sp	|,
	tst.l d0	|
	jge .L2050	|
.L2049:
	move.l d6,(52,sp)	| _20, %sfp
	clr.l (48,sp)	| %sfp
	move.l (48,sp),d1	| %sfp,
	move.l (52,sp),d2	| %sfp,
	add.l d2,(4,a3)	|, ctx_14(D)->keys
	move.l (a3),d0	| ctx_14(D)->keys,
	addx.l d1,d0	|,
	move.l d0,(a3)	|, ctx_14(D)->keys
	tst.w (20,a5)	|
	jeq .L2051	|
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_18(D)], MEM[(struct bfs_bio_t * *)tree_18(D)]
	move.l (8,a2),a1	| tree_18(D)->ops, tree_18(D)->ops
	move.l (4,a1),(48,sp)	| _74->key_size, %sfp
	move.w #-32,a1	|, tmp166
	add.l (4,a0),a1	| _71->block_size, tmp166
	move.l (48,sp),a0	| %sfp,
	addq.l #4,a0	|,
	move.l a1,d2	| tmp166, tmp130
	move.l a0,d1	|,
	divu.l d1,d2	|, tmp130
	move.l d2,(60,sp)	| tmp130, %sfp
	clr.l (56,sp)	| %sfp
	move.l (56,sp),d1	| %sfp,
	move.l (60,sp),d2	| %sfp,
	add.l d2,(12,a3)	|, ctx_14(D)->capacity
	move.l (8,a3),d0	| ctx_14(D)->capacity,
	addx.l d1,d0	|,
	move.l d0,(8,a3)	|, ctx_14(D)->capacity
	subq.w #1,d4	|, tmp133
	and.l #65535,d4	|, _131
	addq.l #1,d5	|, _140
	clr.l d2	| i
	lea _utilization_walk_recursive,a4	|, tmp167
	move.l (48,sp),d1	| %sfp, pretmp_137
	move.l d3,(56,sp)	| <retval>, %sfp
	move.l a3,(48,sp)	| ctx, %sfp
	move.l d7,(64,sp)	| lower, %sfp
	move.l a0,d7	|, tmp165
	move.l a1,d0	| tmp166, tmp166
.L2057:
	cmp.l d6,d2	| _20, i
	jeq .L2061	|
	move.l d1,d3	| pretmp_137, tmp134
	muls.l d2,d3	| i, tmp134
	lea (28,a5,d3.l),a6	|, iftmp.136_3
.L2052:
	tst.l d2	| i
	jeq .L2062	|
	move.l d2,d3	| i, tmp136
	subq.l #1,d3	|, tmp136
	muls.l d1,d3	| pretmp_137, tmp136
	lea (28,a5,d3.l),a1	|, iftmp.137_4
.L2053:
	divu.l d7,d0	| tmp165, tmp143
	lea (28,d2.l*4),a0	|, tmp147
	muls.l d1,d0	| pretmp_137, tmp145
	add.l a0,d0	| tmp147, tmp148
	lea (72,sp),a3	|,, tmp151
	lea (a5,d0.l),a0	| _44, tmp148, tmp150
	move.l (a0)+,(a3)+	|,
	move.l a6,-(sp)	| iftmp.136_3,
	move.l a1,-(sp)	| iftmp.137_4,
	move.l d4,-(sp)	| _131,
	move.l d5,-(sp)	| _140,
	move.l (64,sp),-(sp)	| %sfp,
	move.l (92,sp),-(sp)	| v,
	move.l a2,-(sp)	| tree,
	jsr (a4)	| tmp167
	lea (28,sp),sp	|,
	tst.l d0	| err
	jne .L2074	|
	addq.l #1,d2	|, i
	cmp.l d6,d2	| _20, i
	jhi .L2075	|
	move.l (8,a2),a0	| MEM[(const struct bfs_btree_ops_t * *)tree_18(D) + 8B], MEM[(const struct bfs_btree_ops_t * *)tree_18(D) + 8B]
	move.l (4,a0),d1	| pretmp_132->key_size, pretmp_137
	moveq #-32,d0	|, tmp166
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_18(D)], MEM[(struct bfs_bio_t * *)tree_18(D)]
	add.l (4,a0),d0	| pretmp_138->block_size, tmp166
	move.l d1,d7	| pretmp_137, tmp165
	addq.l #4,d7	|, tmp165
	jra .L2057	|
.L2050:
	move.l a5,-(sp)	| _44,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_18(D)],
	jsr _bfs_bio_free_buffer	|
	addq.l #8,sp	|,
.L2070:
	moveq #-3,d1	|,
	move.l d1,d0	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (32,sp),sp	|,
	rts
.L2051:
	move.l (a2)+,a0	| MEM[(struct bfs_bio_t * *)tree_18(D)], _86
	moveq #-28,d0	|, tmp156
	move.l (4,a2),a1	| tree_18(D)->ops, _89
	add.l (4,a0),d0	| _86->block_size, tmp156
	move.l (4,a1),d1	| _89->key_size, tmp157
	add.l (8,a1),d1	| _89->val_size, tmp157
	divu.l d1,d0	| tmp157, tmp159
	move.l d0,(68,sp)	| tmp159, %sfp
	clr.l (64,sp)	| %sfp
	move.l (64,sp),d1	| %sfp,
	move.l (68,sp),d2	| %sfp,
	add.l d2,(12,a3)	|, ctx_14(D)->capacity
	move.l (8,a3),d0	| ctx_14(D)->capacity,
	addx.l d1,d0	|,
	move.l d0,(8,a3)	|, ctx_14(D)->capacity
	move.l a5,-(sp)	| _44,
	move.l a0,-(sp)	| _86,
	jsr _bfs_bio_free_buffer	|
	addq.l #8,sp	|,
.L2076:
	move.l d3,d0	| <retval>,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (32,sp),sp	|,
	rts
.L2075:
	move.l (56,sp),d3	| %sfp, <retval>
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_18(D)], _86
	move.l a5,-(sp)	| _44,
	move.l a0,-(sp)	| _86,
	jsr _bfs_bio_free_buffer	|
	addq.l #8,sp	|,
	jra .L2076	|
.L2074:
	move.l a5,-(sp)	| _44,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_18(D)],
	move.l d0,(52,sp)	|,
	jsr _bfs_bio_free_buffer	|
	addq.l #8,sp	|,
	move.l (44,sp),d0	|,
	move.l d0,d1	| err,
	move.l d1,d0	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (32,sp),sp	|,
	rts
.L2060:
	moveq #-2,d1	|,
	move.l d1,d0	|,
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (32,sp),sp	|,
	rts
.L2062:
	move.l (64,sp),a1	| %sfp, iftmp.137_4
	jra .L2053	|
.L2061:
	move.l (104,sp),a6	| upper, iftmp.136_3
	jra .L2052	|
	.align	2
_discard_node_cb:
	move.l a2,-(sp)	|,
	move.l (12,sp),a2	| ctx, ctx
	tst.l (4,a2)	| MEM[(struct discard_ctx_t *)ctx_1(D)].err
	jeq .L2081	|
	move.l (sp)+,a2	|,
	rts
.L2081:
	move.l (8,sp),-(sp)	| blk,
	move.l (a2)+,-(sp)	| MEM[(struct discard_ctx_t *)ctx_1(D)].tree,
	jsr _node_dealloc	|
	move.l d0,(a2)	|, MEM[(struct discard_ctx_t *)ctx_1(D)].err
	addq.l #8,sp	|,
	move.l (sp)+,a2	|,
	rts
	.align	2
	.globl	_bfs_btree_compact_build_swap
_bfs_btree_compact_build_swap:
	link.w a5,#-104	|,
	movem.l a6/a3/a2/d5/d4/d3/d2,-(sp)	|,
	move.l (8,a5),a2	| tree, tree
	move.l (12,a5),a0	| old_root_out, old_root_out
	move.l a2,d0	|,
	jeq .L2102	|
	move.l (a2),a1	| tree_4(D)->bio, _6
	move.l a1,d0	|,
	jeq .L2102	|
	tst.l (4,a2)	| tree_4(D)->alloc
	jeq .L2102	|
	move.l a0,d0	|,
	jeq .L2102	|
	move.l (12,a2),(a0)	| tree_4(D)->root, *old_root_out_8(D)
	move.l (12,a2),d0	| tree_4(D)->root, _11
	jne .L2084	|
.L2089:
	clr.l d2	| <retval>
.L2082:
	move.l d2,d0	| <retval>,
	movem.l (-132,a5),d2/d3/d4/d5/a2/a3/a6	|,
	unlk a5	|
	rts
.L2084:
	cmp.l (8,a1),d0	| _6->block_count, _11
	jcc .L2085	|
	move.l (16,a2),d1	| MEM[(const struct bfs_btree_t *)tree_4(D)].height, _47
	move.l d1,a0	| _47, tmp63
	subq.l #1,a0	|, tmp63
	moveq #31,d2	|,
	cmp.l a0,d2	| tmp63,
	jcs .L2085	|
	lea (-88,a5),a0	|,, tmp80
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l -(sp)	|
	clr.l -(sp)	|
	subq.w #1,d1	|, tmp81
	move.w d1,-(sp)	| tmp81,
	clr.w -(sp)	|
	clr.l -(sp)	|
	lea (-88,a5),a3	|,, tmp85
	move.l a3,-(sp)	| tmp85,
	move.l d0,-(sp)	| _11,
	move.l a2,-(sp)	| tree,
	jsr _utilization_walk_recursive	|
	move.l d0,d2	|, <retval>
	move.l (-72,a5),a1	| MEM[(struct block_set_t *)&ctx + 16B].slots, _54
	lea (28,sp),sp	|,
	move.l a1,d0	|,
	jeq .L2098	|
	move.l _SysBase,a6	| SysBase, _FreeVec_bn
#APP
| 21 "src/amiga/stdlib.h" 1
	jsr a6@(-0x2b2:W)
| 0 "" 2
#NO_APP
.L2098:
	clr.l (-72,a5)	| MEM[(struct block_set_t *)&ctx + 16B].slots
	clr.l (-64,a5)	| MEM[(struct block_set_t *)&ctx + 16B].count
	clr.l (-68,a5)	| MEM[(struct block_set_t *)&ctx + 16B].capacity
	tst.l d2	| <retval>
	jne .L2082	|
	move.l (-80,a5),d4	| ctx.capacity, _56
	move.l (-76,a5),d5	| ctx.capacity,
	move.l d4,d3	| _56,
	or.l d5,d3	|,
	jeq .L2082	|
	move.l (-88,a5),d2	| ctx.keys, ctx.keys
	move.l (-84,a5),d3	| ctx.keys,
	move.l d2,d0	| ctx.keys, tmp66
	move.l d3,d1	|,
	add.l d1,d1	| tmp66
	addx.l d0,d0	| tmp66
	add.l d1,d1	| tmp66
	addx.l d0,d0	| tmp66
	add.l d3,d1	| ctx.keys, tmp67
	addx.l d2,d0	| ctx.keys, tmp67
	move.l d4,d2	| _56, tmp70
	move.l d5,d3	|,
	add.l d1,d1	| tmp67, tmp68
	addx.l d0,d0	| tmp67, tmp68
	add.l d3,d3	| tmp70
	addx.l d2,d2	| tmp70
	add.l d3,d3	| tmp70
	addx.l d2,d2	| tmp70
	add.l d3,d3	| tmp70
	addx.l d2,d2	| tmp70
	add.l d3,d5	| tmp70, tmp71
	addx.l d2,d4	| tmp70, tmp71
	move.l d0,d2	| tmp68,
	move.l d1,d3	|,
	sub.l d5,d3	| tmp71,
	subx.l d4,d2	| tmp71,
	jcc .L2089	|
	move.l (20,a2),a0	| MEM[(const uint64_t * *)tree_4(D) + 20B], _38
	move.l a0,d0	|,
	jeq .L2091	|
	move.l (a0),d0	| *_38, iftmp.35_39
	move.l (4,a0),d1	| *_38,
.L2092:
	move.l d1,-(sp)	|,
	move.l d0,-(sp)	|,
	clr.l -(sp)	|
	move.l (8,a2),-(sp)	| tree_4(D)->ops,
	move.l (4,a2),-(sp)	| tree_4(D)->alloc,
	move.l (a2),-(sp)	| tree_4(D)->bio,
	move.l a3,-(sp)	| tmp85,
	jsr _bfs_btree_init	|
	move.l d0,d2	|, <retval>
	lea (28,sp),sp	|,
	jne .L2082	|
	move.l (20,a2),(-68,a5)	| tree_4(D)->txn_id_ptr, new_tree.txn_id_ptr
	move.l a3,(-104,a5)	| tmp85, ctx.new_tree
	move.l d0,(-100,a5)	| <retval>, ctx.rc
	pea (-104,a5)	|
	pea _compact_cb	|
	move.l d0,-(sp)	| <retval>,
	move.l d0,-(sp)	| <retval>,
	move.l d0,-(sp)	| <retval>,
	move.l a2,-(sp)	| tree,
	jsr _btree_scan_cursor_impl	|
	lea (24,sp),sp	|,
	tst.l d0	| _42
	jne .L2103	|
	move.l (-100,a5),d1	| ctx.rc,
	jeq .L2094	|
	move.l d1,d3	|, iftmp.134_2
	tst.l (-76,a5)	| new_tree.root
	jne .L2112	|
.L2095:
	move.l d3,d1	| iftmp.134_2,
	move.l d1,d0	|,
	movem.l (-132,a5),d2/d3/d4/d5/a2/a3/a6	|,
	unlk a5	|
	rts
.L2085:
	moveq #-3,d1	|,
	move.l d1,d0	|,
	movem.l (-132,a5),d2/d3/d4/d5/a2/a3/a6	|,
	unlk a5	|
	rts
.L2103:
	move.l d0,d3	| _42, iftmp.134_2
	tst.l (-76,a5)	| new_tree.root
	jeq .L2095	|
.L2112:
	lea (-88,a5),a0	|,,
	move.l a0,(-96,a5)	|, discard.tree
	clr.l (-92,a5)	| discard.err
	pea (-96,a5)	|
	clr.l -(sp)	|
	pea _discard_node_cb	|
	move.l a3,-(sp)	| tmp85,
	jsr _bfs_btree_walk	|
	move.l d0,d2	|, <retval>
	jne .L2082	|
	move.l (-92,a5),d1	| discard.err,
	jeq .L2095	|
	move.l d1,d0	|,
	movem.l (-132,a5),d2/d3/d4/d5/a2/a3/a6	|,
	unlk a5	|
	rts
.L2102:
	moveq #-8,d1	|,
	move.l d1,d0	|,
	movem.l (-132,a5),d2/d3/d4/d5/a2/a3/a6	|,
	unlk a5	|
	rts
.L2091:
	move.l (24,a2),d0	| MEM[(const uint64_t *)tree_4(D) + 24B], iftmp.35_39
	move.l (28,a2),d1	| MEM[(const uint64_t *)tree_4(D) + 24B],
	jra .L2092	|
.L2094:
	move.l (-76,a5),(12,a2)	| new_tree.root, tree_4(D)->root
	addq.l #1,(60,a2)	|, tree_4(D)->generation
	move.l (-72,a5),(16,a2)	| new_tree.height, tree_4(D)->height
	move.l d1,d0	|,
	movem.l (-132,a5),d2/d3/d4/d5/a2/a3/a6	|,
	unlk a5	|
	rts
	.align	2
_mutation_headroom.part.3:
	move.l d2,-(sp)	|,
	move.l (8,sp),a0	| tree, tree
	move.l (40,a0),a1	| tree_1(D)->free_sink.headroom, _2
	move.l (12,sp),d2	| blocks, blocks
	move.l a1,d0	|,
	jeq .L2116	|
	move.l (48,a0),d0	| tree_1(D)->free_sink.capacity,
	jeq .L2116	|
	cmp.l d0,d2	|, blocks
	jhi .L2117	|
	move.l (32,a0),-(sp)	| tree_1(D)->free_sink.ctx,
	jsr (a1)	| _2
	addq.l #4,sp	|,
	clr.l d1	| <retval>
	cmp.l d2,d0	| blocks,
	jcs .L2121	|
	move.l d1,d0	| <retval>,
	move.l (sp)+,d2	|,
	rts
.L2117:
	moveq #-4,d1	|, <retval>
	move.l d1,d0	| <retval>,
	move.l (sp)+,d2	|,
	rts
.L2121:
	moveq #-9,d1	|, <retval>
	move.l d1,d0	| <retval>,
	move.l (sp)+,d2	|,
	rts
.L2116:
	moveq #-8,d1	|, <retval>
	move.l d1,d0	| <retval>,
	move.l (sp)+,d2	|,
	rts
	.align	2
_node_within_bounds.isra.6:
	movem.l a4/a3/a2/d2,-(sp)	|,
	move.l (20,sp),a4	| ISRA.192, ISRA.192
	move.l (24,sp),a3	| node, node
	move.l (28,sp),a2	| bounds, bounds
	move.l (16,a3),d2	|, _2
	jeq .L2128	|
	tst.l (1024,a2)	| bounds_3(D)->have_lower
	jne .L2125	|
.L2127:
	moveq #1,d0	|, <retval>
	tst.l (1028,a2)	| bounds_3(D)->have_upper
	jne .L2134	|
	movem.l (sp)+,d2/a2/a3/a4	|,
	rts
.L2134:
	move.l (a4),a0	| *ISRA.192_27(D), _12
	pea (512,a2)	|
	subq.l #1,d2	|, tmp58
	muls.l (4,a0),d2	| _12->key_size, tmp59
	pea (28,a3,d2.l)	|
	move.l (a0),a0	| _12->key_compare, _12->key_compare
	jsr (a0)	| _12->key_compare
	add.l d0,d0	| <retval>
	subx.l d0,d0	| <retval>
	neg.l d0	| <retval>
	addq.l #8,sp	|,
	movem.l (sp)+,d2/a2/a3/a4	|,
	rts
.L2125:
	move.l (a4),a0	| *ISRA.192_27(D), *ISRA.192_27(D)
	move.l a2,-(sp)	| bounds,
	pea (28,a3)	|
	move.l (a0),a0	| _6->key_compare, _6->key_compare
	jsr (a0)	| _6->key_compare
	addq.l #8,sp	|,
	tst.l d0	|
	jge .L2127	|
.L2128:
	clr.l d0	| <retval>
	movem.l (sp)+,d2/a2/a3/a4	|,
	rts
	.align	2
_mutation_retire_txn.part.9:
	movem.l a2/d3/d2,-(sp)	|,
	move.l (16,sp),a2	| mutation, mutation
	move.l (20,sp),d2	| blk, blk
	move.l (2180,a2),d3	| mutation_1(D)->retired_count, _19
	jeq .L2141	|
	cmp.l (548,a2),d2	| mutation_1(D)->retired_blocks, blk
	jeq .L2143	|
	lea (552,a2),a1	|, mutation, ivtmp.905
	clr.l d1	| i
	move.l d3,d0	| _19, tmp47
	subq.l #1,d0	|, tmp47
.L2139:
	move.l d1,a0	| i, i
	addq.l #1,a0	|, i
	dbra d0,.L2140	| tmp47,
	clr.w d0	| tmp47
	subq.l #1,d0	| tmp47
	jcc .L2140	|
	cmp.l #136,d3	|, _19
	jeq .L2144	|
	move.l d2,(548,a2,a0.l*4)	| blk, mutation_1(D)->retired_blocks
	addq.l #2,d1	|, _6
	move.l (24,sp),d2	| block_txn,
	move.l (28,sp),d3	| block_txn,
	move.l d2,(1092,a2,a0.l*8)	|, mutation_1(D)->retired_txns
	move.l d3,(1096,a2,a0.l*8)	|, mutation_1(D)->retired_txns
	move.l d1,(2180,a2)	| _6, mutation_1(D)->retired_count
	clr.l d0	| <retval>
.L2135:
	movem.l (sp)+,d2/d3/a2	|,
	rts
.L2140:
	move.l a0,d1	| i, i
	cmp.l (a1)+,d2	| MEM[base: _22, offset: 0B], blk
	jne .L2139	|
.L2143:
	clr.l d0	| <retval>
	movem.l (sp)+,d2/d3/a2	|,
	rts
.L2141:
	move.l d3,a0	| _19, i
	moveq #1,d0	|,
	move.l d2,(548,a2,a0.l*4)	| blk, mutation_1(D)->retired_blocks
	move.l (24,sp),d2	| block_txn,
	move.l (28,sp),d3	| block_txn,
	move.l d2,(1092,a2,a0.l*8)	|, mutation_1(D)->retired_txns
	move.l d3,(1096,a2,a0.l*8)	|, mutation_1(D)->retired_txns
	move.l d0,(2180,a2)	|, mutation_1(D)->retired_count
	clr.l d0	| <retval>
	jra .L2135	|
.L2144:
	moveq #-4,d0	|, <retval>
	movem.l (sp)+,d2/d3/a2	|,
	rts
	.align	2
_key_fits_at.isra.10:
	movem.l a4/a3/a2/d4/d3/d2,-(sp)	|,
	lea (28,sp),a0	|,, tmp82
	move.l (a0)+,a4	| ISRA.208, ISRA.208
	move.l (a0)+,a3	| leaf, leaf
	move.l (a0)+,d2	| index, index
	move.l (a0)+,a2	| bounds, bounds
	move.l (a0),d3	| new_key, new_key
	move.l (16,a3),d4	|, _2
	tst.l d2	| index
	jne .L2149	|
.L2152:
	addq.l #1,d2	|, _14
	cmp.l d4,d2	| _2, _14
	jcs .L2169	|
.L2150:
	tst.l (1024,a2)	| bounds_22(D)->have_lower
	jne .L2170	|
	moveq #1,d0	|, <retval>
	tst.l (1028,a2)	| bounds_22(D)->have_upper
	jne .L2171	|
.L2148:
	movem.l (sp)+,d2/d3/d4/a2/a3/a4	|,
	rts
.L2149:
	move.l (a4),a0	| *ISRA.208_43(D), _5
	move.l d3,-(sp)	| new_key,
	move.l d2,d0	| index, tmp66
	subq.l #1,d0	|, tmp66
	muls.l (4,a0),d0	| _5->key_size, tmp67
	pea (28,a3,d0.l)	|
	move.l (a0),a0	| _5->key_compare, _5->key_compare
	jsr (a0)	| _5->key_compare
	addq.l #8,sp	|,
	tst.l d0	|
	jlt .L2152	|
.L2156:
	clr.l d0	| <retval>
.L2172:
	movem.l (sp)+,d2/d3/d4/a2/a3/a4	|,
	rts
.L2171:
	move.l (a4),a0	| *ISRA.208_43(D), *ISRA.208_43(D)
	pea (512,a2)	|
	move.l d3,-(sp)	| new_key,
	move.l (a0),a0	| _29->key_compare, _29->key_compare
	jsr (a0)	| _29->key_compare
	add.l d0,d0	| <retval>
	subx.l d0,d0	| <retval>
	neg.l d0	| <retval>
	addq.l #8,sp	|,
	movem.l (sp)+,d2/d3/d4/a2/a3/a4	|,
	rts
.L2169:
	move.l (a4),a0	| *ISRA.208_43(D), _15
	muls.l (4,a0),d2	| _15->key_size, tmp71
	pea (28,a3,d2.l)	|
	move.l d3,-(sp)	| new_key,
	move.l (a0),a0	| _15->key_compare, _15->key_compare
	jsr (a0)	| _15->key_compare
	addq.l #8,sp	|,
	tst.l d0	|
	jlt .L2150	|
	clr.l d0	| <retval>
	jra .L2172	|
.L2170:
	move.l (a4),a0	| *ISRA.208_43(D), *ISRA.208_43(D)
	move.l a2,-(sp)	| bounds,
	move.l d3,-(sp)	| new_key,
	move.l (a0),a0	| _24->key_compare, _24->key_compare
	jsr (a0)	| _24->key_compare
	addq.l #8,sp	|,
	tst.l d0	|
	jlt .L2156	|
	moveq #1,d0	|, <retval>
	tst.l (1028,a2)	| bounds_22(D)->have_upper
	jeq .L2148	|
	jra .L2171	|
	.align	2
_node_search.isra.12:
	subq.l #4,sp	|,
	movem.l a4/a3/a2/d5/d4/d3/d2,-(sp)	|,
	lea (36,sp),a0	|,, tmp76
	move.l (a0)+,a3	| ISRA.224, ISRA.224
	move.l (a0)+,a2	| buf, buf
	move.l (a0)+,d5	| search_key, search_key
	move.l (a0),a4	| found, found
	move.l (16,a2),d4	|, hi
	clr.l (a4)	| *found_3(D)
	move.l (a3),a0	| *ISRA.224_52(D), _5
	move.l (a0),a1	| _5->key_compare, _6
	cmp.l #_bfs_btree_key_compare_be32,a1	|, _6
	jeq .L2174	|
	tst.l d4	| hi
	jeq .L2175	|
	clr.l d3	| <retval>
	move.l d4,d2	| hi, tmp69
	sub.l d3,d2	| <retval>, tmp69
	lsr.l #1,d2	|, tmp70
	move.l d5,-(sp)	| search_key,
	move.l d2,d0	| mid, tmp71
	muls.l (4,a0),d0	| prephitmp_58->key_size, tmp71
	pea (28,a2,d0.l)	|
	jsr (a1)	| _6
	addq.l #8,sp	|,
	tst.l d0	| cmp
	jlt .L2194	|
.L2181:
	tst.l d0	| cmp
	jeq .L2195	|
	cmp.l d3,d2	| <retval>, mid
	jls .L2173	|
.L2196:
	move.l (a3),a0	| *ISRA.224_52(D), _5
	move.l d2,d4	| mid, hi
	move.l d4,d2	| hi, tmp69
	sub.l d3,d2	| <retval>, tmp69
	lsr.l #1,d2	|, tmp70
	move.l (a0)+,a1	| pretmp_59->key_compare, _6
	add.l d3,d2	| <retval>, mid
	move.l d5,-(sp)	| search_key,
	move.l d2,d0	| mid, tmp71
	muls.l (a0),d0	| prephitmp_58->key_size, tmp71
	pea (28,a2,d0.l)	|
	jsr (a1)	| _6
	addq.l #8,sp	|,
	tst.l d0	| cmp
	jge .L2181	|
.L2194:
	move.l d2,d3	| mid, <retval>
	addq.l #1,d3	|, <retval>
	move.l d4,d2	| hi, mid
	cmp.l d3,d2	| <retval>, mid
	jhi .L2196	|
.L2173:
	move.l d3,d0	| <retval>,
	movem.l (sp)+,d2/d3/d4/d5/a2/a3/a4	|,
	addq.l #4,sp	|,
	rts
.L2195:
	moveq #1,d0	|,
	move.l d0,(a4)	|, *found_3(D)
	move.l d2,d1	| mid,
	move.l d1,d0	|,
	movem.l (sp)+,d2/d3/d4/d5/a2/a3/a4	|,
	addq.l #4,sp	|,
	rts
.L2174:
	lea (28,sp),a3	|,, tmp60
	move.l d5,a1	| search_key, search_key
	move.l (a1)+,(a3)+	|,
	move.l (28,sp),d2	| v, v.0_8
	tst.l d4	| hi
	jeq .L2175	|
	move.l (4,a0),d5	| _5->key_size, pretmp_60
	clr.l d3	| <retval>
	move.l d4,d0	| hi, tmp61
	sub.l d3,d0	| <retval>, tmp61
	lsr.l #1,d0	|, tmp62
	move.l d0,d1	| mid, tmp64
	muls.l d5,d1	| pretmp_60, tmp64
	lea (28,sp),a0	|,, tmp68
	lea (28,a2,d1.l),a1	|, tmp67
	move.l (a1)+,(a0)+	|,
	move.l (28,sp),d1	| v, v.0_18
	cmp.l d2,d1	| v.0_8, v.0_18
	jcc .L2178	|
.L2197:
	move.l d0,d3	| mid, <retval>
	addq.l #1,d3	|, <retval>
	cmp.l d3,d4	| <retval>, hi
	jls .L2173	|
.L2193:
	move.l d4,d0	| hi, tmp61
	sub.l d3,d0	| <retval>, tmp61
	lsr.l #1,d0	|, tmp62
	add.l d3,d0	| <retval>, mid
	move.l d0,d1	| mid, tmp64
	muls.l d5,d1	| pretmp_60, tmp64
	lea (28,sp),a0	|,, tmp68
	lea (28,a2,d1.l),a1	|, tmp67
	move.l (a1)+,(a0)+	|,
	move.l (28,sp),d1	| v, v.0_18
	cmp.l d2,d1	| v.0_8, v.0_18
	jcs .L2197	|
.L2178:
	cmp.l d2,d1	| v.0_8, v.0_18
	jls .L2198	|
	move.l d0,d4	| mid, hi
	cmp.l d3,d4	| <retval>, hi
	jhi .L2193	|
	jra .L2173	|
.L2175:
	clr.l d0	|
	movem.l (sp)+,d2/d3/d4/d5/a2/a3/a4	|,
	addq.l #4,sp	|,
	rts
.L2198:
	moveq #1,d1	|,
	move.l d1,(a4)	|, *found_3(D)
	move.l d0,d1	| mid,
	move.l d1,d0	|,
	movem.l (sp)+,d2/d3/d4/d5/a2/a3/a4	|,
	addq.l #4,sp	|,
	rts
	.align	2
_child_bounds.isra.13:
	movem.l a4/a3/a2/d2,-(sp)	|,
	lea (20,sp),a0	|,, tmp72
	move.l (a0)+,a4	| ISRA.231, ISRA.231
	move.l (a0)+,a2	| parent, parent
	move.l (a0)+,d2	| child, child
	move.l (a0),a3	| bounds, bounds
	jne .L2207	|
	cmp.l (16,a2),d2	|, child
	jcs .L2208	|
.L2199:
	movem.l (sp)+,d2/a2/a3/a4	|,
	rts
.L2208:
	move.l (a4),a0	| *ISRA.231_17(D), *ISRA.231_17(D)
	move.l (4,a0),d0	| _20->key_size, _21
	muls.l d0,d2	| _21, tmp63
	move.l d0,-(sp)	| _21,
	pea (28,a2,d2.l)	|
	pea (512,a3)	|
	jsr _memcpy	|
	moveq #1,d0	|,
	move.l d0,(1028,a3)	|, bounds_10(D)->have_upper
	lea (12,sp),sp	|,
	movem.l (sp)+,d2/a2/a3/a4	|,
	rts
.L2207:
	move.l (a4),a0	| *ISRA.231_17(D), *ISRA.231_17(D)
	move.l (4,a0),d1	| _3->key_size, _4
	move.l d2,d0	| child, tmp51
	subq.l #1,d0	|, tmp51
	muls.l d1,d0	| _4, tmp52
	move.l d1,-(sp)	| _4,
	pea (28,a2,d0.l)	|
	move.l a3,-(sp)	| bounds,
	jsr _memcpy	|
	moveq #1,d0	|,
	move.l d0,(1024,a3)	|, bounds_10(D)->have_lower
	lea (12,sp),sp	|,
	cmp.l (16,a2),d2	|, child
	jcc .L2199	|
	jra .L2208	|
	.align	2
_replace_root_leaf.part.22:
	link.w a5,#-3304	|,
	movem.l a6/a4/a3/a2/d7/d6/d5/d4/d3/d2,-(sp)	|,
	lea (8,a5),a0	|,, tmp101
	move.l (a0)+,a2	| tree, tree
	move.l (a0)+,(12,a5)	| keys, keys
	move.l (a0)+,d6	| vals, vals
	move.l (a0)+,d7	| count, count
	move.l (a0)+,d3	| transfer_old_root, transfer_old_root
	move.l (a0)+,d2	| allow_in_place, allow_in_place
	move.l (a0),a6	| published, published
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_1(D)], _2
	move.l (4,a0),-(sp)	| _2->block_size,
	move.l a0,-(sp)	| _2,
	lea _bfs_bio_alloc_buffer,a4	|, tmp73
	jsr (a4)	| tmp73
	move.l (a2),a0	| MEM[(struct bfs_bio_t * *)tree_1(D)], _5
	move.l d0,a3	|, _4
	move.l (4,a0),-(sp)	| _5->block_size,
	move.l a0,-(sp)	| _5,
	jsr (a4)	| tmp73
	move.l d0,a4	|, new_buf
	lea (16,sp),sp	|,
	move.l a3,d1	|,
	jeq .L2210	|
	tst.l d0	| new_buf
	jeq .L2210	|
	move.l (12,a2),(-3296,a5)	| tree_1(D)->root, %sfp
	move.l a3,-(sp)	| _4,
	move.l (-3296,a5),-(sp)	| %sfp,
	move.l a2,-(sp)	| tree,
	jsr _node_read	|
	lea (12,sp),sp	|,
	tst.l d0	| <retval>
	jne .L2213	|
	moveq #-3,d0	|, <retval>
	tst.w (20,a3)	|
	jne .L2213	|
	tst.l d3	| transfer_old_root
	jeq .L2262	|
	move.l (8,a3),(-3304,a5)	|, %sfp
	move.l (12,a3),(-3300,a5)	|, %sfp
	move.l (20,a2),d0	| MEM[(const uint64_t * *)tree_1(D) + 20B], _16
	jeq .L2215	|
	move.l d0,a0	| _16,
	move.l (a0),(-3288,a5)	| *_16, %sfp
	move.l (4,a0),(-3284,a5)	| *_16, %sfp
	moveq #-10,d0	|, <retval>
	move.l (-3304,a5),d1	| %sfp,
	move.l (-3300,a5),d2	| %sfp,
	move.l (-3288,a5),d4	| %sfp,
	move.l (-3284,a5),d5	| %sfp,
	sub.l d5,d2	|,
	subx.l d4,d1	|,
	jeq .L2263	|
.L2213:
	move.l a3,-(sp)	| _4,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_1(D)],
	lea _bfs_bio_free_buffer,a3	|, tmp94
	move.l d0,(-3292,a5)	|,
	jsr (a3)	| tmp94
	move.l a4,-(sp)	| new_buf,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_1(D)],
	jsr (a3)	| tmp94
	move.l (-3292,a5),d0	|,
.L2209:
	movem.l (-3344,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
.L2262:
	move.l a5,d4	|, tmp100
	add.l #-3276,d4	|, tmp100
	move.l d4,a0	| tmp100, tmp97
	moveq #50,d0	|, tmp98
.L2233:
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	dbra d0,.L2233	| tmp98,
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	move.l d7,-(sp)	| count,
	move.l d6,-(sp)	| vals,
	move.l (12,a5),-(sp)	| keys,
	move.l a4,-(sp)	| new_buf,
	move.l a2,-(sp)	| tree,
	jsr _build_leaf	|
	move.l (24,a3),(24,a4)	|, MEM[(struct bfs_btnode_hdr_t *)_7].right_sibling
	lea (20,sp),sp	|,
	tst.l d2	| allow_in_place
	jeq .L2218	|
	move.l (a2),a0	| tree_1(D)->bio, _25
	move.l a0,d0	|,
	jeq .L2218	|
	move.l (a0),a0	| MEM[(const struct bfs_bio_t *)_25].ops, _26
	move.l a0,d0	|,
	jeq .L2218	|
	tst.l (44,a0)	| _26->defer_node_block
	jeq .L2218	|
	tst.l (48,a0)	| _26->flush_deferred
	jeq .L2218	|
	tst.l (52,a0)	| _26->discard_deferred
	jeq .L2218	|
	move.l (8,a3),(-3288,a5)	|, %sfp
	move.l (12,a3),(-3284,a5)	|, %sfp
	move.l (20,a2),a0	| MEM[(const uint64_t * *)tree_1(D) + 20B], _32
	move.l a0,d0	|,
	jeq .L2264	|
	move.l (a0),d0	| *_32, iftmp.35_33
	move.l (4,a0),d1	| *_32,
.L2221:
	move.l (-3288,a5),d5	| %sfp,
	move.l (-3284,a5),d6	| %sfp,
	sub.l d1,d6	| iftmp.35_33,
	subx.l d0,d5	| iftmp.35_33,
	jne .L2218	|
	move.l (-3296,a5),-(sp)	| %sfp,
	move.l a2,-(sp)	| tree,
	jsr _owned_contains	|
	addq.l #8,sp	|,
	tst.l d0	|
	jeq .L2218	|
	move.l (-3296,a5),(-1092,a5)	| %sfp, mutation.staged_blocks
	move.l a4,(-548,a5)	| new_buf, mutation.staged_images
	moveq #1,d0	|,
	move.l d0,(-4,a5)	|, mutation.staged_count
	move.l d0,(a6)	|, *published_37(D)
	move.l d4,-(sp)	| tmp100,
	move.l a2,-(sp)	| tree,
	jsr _mutation_commit	|
	move.l (56,a2),d0	| tree_1(D)->free_sink_err, <retval>
	addq.l #8,sp	|,
	sub.l a4,a4	| new_buf
	move.l a3,-(sp)	| _4,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_1(D)],
	lea _bfs_bio_free_buffer,a3	|, tmp94
	move.l d0,(-3292,a5)	|,
	jsr (a3)	| tmp94
	move.l a4,-(sp)	| new_buf,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_1(D)],
	jsr (a3)	| tmp94
	move.l (-3292,a5),d0	|,
	jra .L2209	|
.L2263:
	clr.l (56,a2)	| tree_1(D)->free_sink_err
	move.l a5,d4	|, tmp100
	add.l #-3276,d4	|, tmp100
	move.l d4,a0	| tmp100, tmp78
	moveq #50,d0	|, tmp79
.L2217:
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	dbra d0,.L2217	| tmp79,
	clr.l (a0)+	|
	clr.l (a0)+	|
	clr.l (a0)+	|
	move.l d7,-(sp)	| count,
	move.l d6,-(sp)	| vals,
	move.l (12,a5),-(sp)	| keys,
	move.l a4,-(sp)	| new_buf,
	move.l a2,-(sp)	| tree,
	jsr _build_leaf	|
	move.l (24,a3),(24,a4)	|, MEM[(struct bfs_btnode_hdr_t *)_7].right_sibling
	lea (20,sp),sp	|,
.L2218:
	move.l d4,-(sp)	| tmp100,
	move.l a2,-(sp)	| tree,
	jsr _mutation_alloc	|
	move.l d0,d2	|,
	addq.l #8,sp	|,
	jne .L2222	|
	move.l (4,a2),a1	| MEM[(struct bfs_allocator_t * *)tree_1(D) + 4B], _40
	move.l (8,a1),a0	| _40->error, _41
	move.l a0,d0	|,
	jeq .L2225	|
	move.l a1,-(sp)	| _40,
	jsr (a0)	| _41
	addq.l #4,sp	|,
	tst.l d0	| <retval>
	jeq .L2225	|
.L2224:
	move.l d4,-(sp)	| tmp100,
	move.l a2,-(sp)	| tree,
	move.l d0,(-3292,a5)	|,
	jsr _mutation_abort	|
	addq.l #8,sp	|,
	move.l (-3292,a5),d0	|,
.L2265:
	move.l a3,-(sp)	| _4,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_1(D)],
	lea _bfs_bio_free_buffer,a3	|, tmp94
	move.l d0,(-3292,a5)	|,
	jsr (a3)	| tmp94
	move.l a4,-(sp)	| new_buf,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_1(D)],
	jsr (a3)	| tmp94
	move.l (-3292,a5),d0	|,
	jra .L2209	|
.L2225:
	moveq #-4,d0	|, <retval>
	move.l d4,-(sp)	| tmp100,
	move.l a2,-(sp)	| tree,
	move.l d0,(-3292,a5)	|,
	jsr _mutation_abort	|
	addq.l #8,sp	|,
	move.l (-3292,a5),d0	|,
	jra .L2265	|
.L2222:
	cmp.l (-3296,a5),d0	| %sfp, new_root
	jne .L2226	|
	subq.l #1,(-2732,a5)	|, mutation.new_count
	moveq #-3,d0	|, <retval>
	move.l d4,-(sp)	| tmp100,
	move.l a2,-(sp)	| tree,
	move.l d0,(-3292,a5)	|,
	jsr _mutation_abort	|
	addq.l #8,sp	|,
	move.l (-3292,a5),d0	|,
	jra .L2265	|
.L2226:
	move.l a4,(-3280,a5)	| new_buf, buf
	clr.l -(sp)	|
	pea (-3280,a5)	|
	move.l d0,-(sp)	| new_root,
	move.l a2,-(sp)	| tree,
	jsr _node_write_image	|
	lea (16,sp),sp	|,
	tst.l d0	| <retval>
	jne .L2224	|
	tst.l d3	| transfer_old_root
	jne .L2228	|
	tst.l (-3296,a5)	| %sfp
	jne .L2266	|
.L2228:
	move.l d2,(12,a2)	|, tree_1(D)->root
	addq.l #1,(60,a2)	|, tree_1(D)->generation
	moveq #1,d0	|,
	move.l d0,(a6)	|, *published_37(D)
	move.l d4,-(sp)	| tmp100,
	move.l a2,-(sp)	| tree,
	jsr _mutation_commit	|
	addq.l #8,sp	|,
	move.l (56,a2),d0	| tree_1(D)->free_sink_err, <retval>
	move.l a3,-(sp)	| _4,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_1(D)],
	lea _bfs_bio_free_buffer,a3	|, tmp94
	move.l d0,(-3292,a5)	|,
	jsr (a3)	| tmp94
	move.l a4,-(sp)	| new_buf,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_1(D)],
	jsr (a3)	| tmp94
	move.l (-3292,a5),d0	|,
	jra .L2209	|
.L2264:
	move.l (24,a2),d0	| MEM[(const uint64_t *)tree_1(D) + 24B], iftmp.35_33
	move.l (28,a2),d1	| MEM[(const uint64_t *)tree_1(D) + 24B],
	jra .L2221	|
.L2266:
	move.l (12,a3),-(sp)	|,
	move.l (8,a3),-(sp)	|,
	move.l (-3296,a5),-(sp)	| %sfp,
	move.l d4,-(sp)	| tmp100,
	jsr (_mutation_retire_txn.part.9)	|
	lea (16,sp),sp	|,
	tst.l d0	| <retval>
	jeq .L2228	|
	move.l d4,-(sp)	| tmp100,
	move.l a2,-(sp)	| tree,
	move.l d0,(-3292,a5)	|,
	jsr _mutation_abort	|
	addq.l #8,sp	|,
	move.l (-3292,a5),d0	|,
	jra .L2265	|
.L2215:
	move.l (24,a2),(-3288,a5)	| MEM[(const uint64_t *)tree_1(D) + 24B], %sfp
	move.l (28,a2),(-3284,a5)	| MEM[(const uint64_t *)tree_1(D) + 24B], %sfp
	moveq #-10,d0	|, <retval>
	move.l (-3304,a5),d1	| %sfp,
	move.l (-3300,a5),d2	| %sfp,
	move.l (-3288,a5),d4	| %sfp,
	move.l (-3284,a5),d5	| %sfp,
	sub.l d5,d2	|,
	subx.l d4,d1	|,
	jne .L2213	|
	jra .L2263	|
.L2210:
	move.l a3,-(sp)	| _4,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_1(D)],
	lea _bfs_bio_free_buffer,a3	|, tmp75
	jsr (a3)	| tmp75
	move.l a4,-(sp)	| new_buf,
	move.l (a2),-(sp)	| MEM[(struct bfs_bio_t * *)tree_1(D)],
	jsr (a3)	| tmp75
	moveq #-2,d0	|, <retval>
	movem.l (-3344,a5),d2/d3/d4/d5/d6/d7/a2/a3/a4/a6	|,
	unlk a5	|
	rts
	.align	2
_rekey_descend_to_leaf.constprop.24:
	lea (-12,sp),sp	|,
	movem.l a6/a5/a4/a3/a2/d7/d6/d5/d4/d3/d2,-(sp)	|,
	lea (60,sp),a0	|,, tmp102
	move.l (a0)+,a6	| tree, tree
	move.l (a0)+,(64,sp)	| key, key
	move.l (a0)+,d2	| node_bufs, node_bufs
	move.l (a0)+,d0	| path, path
	move.l (a0)+,(76,sp)	| depth_out, depth_out
	move.l (a0),d5	| bounds, bounds
	move.l (a6),a0	| tree_1(D)->bio, tree_1(D)->bio
	move.l (4,a0),(44,sp)	| _2->block_size, %sfp
	move.l (12,a6),d6	| tree_1(D)->root, block
	addq.l #4,d0	|, path
	move.l d0,a2	| path, ivtmp.925
	moveq #32,d4	|, ivtmp_75
	clr.l d3	| depth
	lea _node_read,a4	|, tmp96
	lea (8,a6),a3	|, tree, _24
	lea _node_search.isra.12,a5	|, tmp99
.L2274:
	move.l (16,a6),d7	| tree_1(D)->height, _6
	cmp.l d7,d3	| _6, depth
	jcc .L2271	|
	move.l d2,-(sp)	| ivtmp.922,
	move.l d6,-(sp)	| block,
	move.l a6,-(sp)	| tree,
	jsr (a4)	| tmp96
	lea (12,sp),sp	|,
	tst.l d0	| <retval>
	jeq .L2289	|
.L2267:
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (12,sp),sp	|,
	rts
.L2289:
	move.w d7,d1	|, tmp76
	subq.w #1,d1	|, tmp76
	sub.w d3,d1	| depth, _14
	move.l d2,a0	| ivtmp.922,
	cmp.w (20,a0),d1	|, _14
	jeq .L2290	|
.L2271:
	moveq #-3,d0	|, <retval>
.L2292:
	movem.l (sp)+,d2/d3/d4/d5/d6/d7/a2/a3/a4/a5/a6	|,
	lea (12,sp),sp	|,
	rts
.L2290:
	move.l d6,(-4,a2)	| block, MEM[base: _67, offset: 4294967292B]
	tst.w d1	| _14
	jeq .L2291	|
	pea (48,sp)	|
	move.l (68,sp),-(sp)	| key,
	move.l d2,-(sp)	| ivtmp.922,
	move.l a3,-(sp)	| _24,
	jsr (a5)	| tmp99
	lea (16,sp),sp	|,
	tst.l (48,sp)	| found
	jeq .L2272	|
	addq.l #1,d0	|, index
.L2272:
	move.l d0,(a2)	| index, MEM[base: _67, offset: 0B]
	tst.l d5	| bounds
	jeq .L2273	|
	move.l d5,-(sp)	| bounds,
	move.l d0,-(sp)	| index,
	move.l d2,-(sp)	| ivtmp.922,
	move.l a3,-(sp)	| _24,
	jsr _child_bounds.isra.13	|
	move.l (a2),d0	| MEM[base: _67, offset: 0B], index
	lea (16,sp),sp	|,
.L2273:
	move.l (8,a6),a0	| tree_1(D)->ops, tree_1(D)->ops
	move.l (4,a0),d6	| _51->key_size, _52
	moveq #-32,d1	|, tmp83
	move.l (a6),a0	| MEM[(struct bfs_bio_t * *)tree_1(D)],
	add.l (4,a0),d1	| _48->block_size, tmp83
	move.l d6,d7	| _52, tmp84
	addq.l #4,d7	|, tmp84
	divu.l d7,d1	| tmp84, tmp86
	muls.l d6,d1	| _52, tmp88
	move.l d1,a1	| tmp88,
	lsl.l #2,d0	|, tmp90
	lea (28,a1,d0.l),a0	|, tmp91
	lea (52,sp),a1	|,, tmp94
	add.l d2,a0	| ivtmp.922, tmp93
	move.l (a0)+,(a1)+	|,
	move.l (52,sp),d6	| v, block
	addq.l #1,d3	|, depth
	add.l (44,sp),d2	| %sfp, ivtmp.922
	addq.l #8,a2	|, ivtmp.925
	subq.w #1,d4	|, ivtmp_75
	jne .L2274	|
	moveq #-3,d0	|, <retval>
	jra .L2292	|
.L2291:
	move.l (76,sp),a0	| depth_out,
	move.l d3,(a0)	| depth, *depth_out_23(D)
	jra .L2267	|
