
build/memory-copy-short-candidate.o:     file format amiga


Disassembly of section .text:

00000000 00000000 _memcpy:
   0:	202f 000c      	move.l 12(sp),d0
   4:	0c80 0000 002c 	cmpi.l #44,d0
   a:	642c           	bcc.s 38 38 _memcpy+0x38
   c:	206f 0004      	movea.l 4(sp),a0
  10:	226f 0008      	movea.l 8(sp),a1
  14:	2200           	move.l d0,d1
  16:	e489           	lsr.l #2,d1
  18:	6708           	beq.s 22 22 _memcpy+0x22
  1a:	5381           	subq.l #1,d1
  1c:	20d9           	move.l (a1)+,(a0)+
  1e:	51c9 fffc      	dbf d1,1c 1c _memcpy+0x1c
  22:	0280 0000 0003 	andi.l #3,d0
  28:	6708           	beq.s 32 32 _memcpy+0x32
  2a:	5380           	subq.l #1,d0
  2c:	10d9           	move.b (a1)+,(a0)+
  2e:	51c8 fffc      	dbf d0,2c 2c _memcpy+0x2c
  32:	202f 0004      	move.l 4(sp),d0
  36:	4e75           	rts
  38:	48e7 3f3e      	movem.l d2-d7/a2-a6,-(sp)
  3c:	206f 0030      	movea.l 48(sp),a0
  40:	226f 0034      	movea.l 52(sp),a1
  44:	202f 0038      	move.l 56(sp),d0
  48:	0c80 0000 002c 	cmpi.l #44,d0
  4e:	6514           	bcs.s 64 64 _memcpy+0x64
  50:	4cd9 7cfc      	movem.l (a1)+,d2-d7/a2-a6
  54:	48d0 7cfc      	movem.l d2-d7/a2-a6,(a0)
  58:	41e8 002c      	lea 44(a0),a0
  5c:	0480 0000 002c 	subi.l #44,d0
  62:	60e4           	bra.s 48 48 _memcpy+0x48
  64:	2200           	move.l d0,d1
  66:	e489           	lsr.l #2,d1
  68:	6708           	beq.s 72 72 _memcpy+0x72
  6a:	5381           	subq.l #1,d1
  6c:	20d9           	move.l (a1)+,(a0)+
  6e:	51c9 fffc      	dbf d1,6c 6c _memcpy+0x6c
  72:	0280 0000 0003 	andi.l #3,d0
  78:	6708           	beq.s 82 82 _memcpy+0x82
  7a:	5380           	subq.l #1,d0
  7c:	10d9           	move.b (a1)+,(a0)+
  7e:	51c8 fffc      	dbf d0,7c 7c _memcpy+0x7c
  82:	202f 0030      	move.l 48(sp),d0
  86:	4cdf 7cfc      	movem.l (sp)+,d2-d7/a2-a6
  8a:	4e75           	rts

0000008c 0000008c _memset:
  8c:	48e7 3f3e      	movem.l d2-d7/a2-a6,-(sp)
  90:	206f 0030      	movea.l 48(sp),a0
  94:	222f 0034      	move.l 52(sp),d1
  98:	202f 0038      	move.l 56(sp),d0
  9c:	2248           	movea.l a0,a1
  9e:	0281 0000 00ff 	andi.l #255,d1
  a4:	2401           	move.l d1,d2
  a6:	e18a           	lsl.l #8,d2
  a8:	8282           	or.l d2,d1
  aa:	2401           	move.l d1,d2
  ac:	4842           	swap d2
  ae:	8282           	or.l d2,d1
  b0:	2401           	move.l d1,d2
  b2:	2601           	move.l d1,d3
  b4:	2801           	move.l d1,d4
  b6:	2a01           	move.l d1,d5
  b8:	2c01           	move.l d1,d6
  ba:	2e01           	move.l d1,d7
  bc:	2441           	movea.l d1,a2
  be:	2641           	movea.l d1,a3
  c0:	2841           	movea.l d1,a4
  c2:	2a41           	movea.l d1,a5
  c4:	0c80 0000 002c 	cmpi.l #44,d0
  ca:	6510           	bcs.s dc dc _memset+0x50
  cc:	48d0 3cfe      	movem.l d1-d7/a2-a5,(a0)
  d0:	41e8 002c      	lea 44(a0),a0
  d4:	0480 0000 002c 	subi.l #44,d0
  da:	60e8           	bra.s c4 c4 _memset+0x38
  dc:	2400           	move.l d0,d2
  de:	e48a           	lsr.l #2,d2
  e0:	6708           	beq.s ea ea _memset+0x5e
  e2:	5382           	subq.l #1,d2
  e4:	20c1           	move.l d1,(a0)+
  e6:	51ca fffc      	dbf d2,e4 e4 _memset+0x58
  ea:	202f 0038      	move.l 56(sp),d0
  ee:	0280 0000 0003 	andi.l #3,d0
  f4:	6708           	beq.s fe fe _memset+0x72
  f6:	5380           	subq.l #1,d0
  f8:	10c1           	move.b d1,(a0)+
  fa:	51c8 fffc      	dbf d0,f8 f8 _memset+0x6c
  fe:	2009           	move.l a1,d0
 100:	4cdf 7cfc      	movem.l (sp)+,d2-d7/a2-a6
 104:	4e75           	rts
 106:	0000           	Address 0x0000000000000108 is out of bounds.
.short 0x0000
