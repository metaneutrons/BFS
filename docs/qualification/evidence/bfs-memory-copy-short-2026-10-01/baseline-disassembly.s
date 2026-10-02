
build/memory-copy-short-baseline.o:     file format amiga


Disassembly of section .text:

00000000 00000000 _memcpy:
   0:	48e7 3f3e      	movem.l d2-d7/a2-a6,-(sp)
   4:	206f 0030      	movea.l 48(sp),a0
   8:	226f 0034      	movea.l 52(sp),a1
   c:	202f 0038      	move.l 56(sp),d0
  10:	0c80 0000 002c 	cmpi.l #44,d0
  16:	6514           	bcs.s 2c 2c _memcpy+0x2c
  18:	4cd9 7cfc      	movem.l (a1)+,d2-d7/a2-a6
  1c:	48d0 7cfc      	movem.l d2-d7/a2-a6,(a0)
  20:	41e8 002c      	lea 44(a0),a0
  24:	0480 0000 002c 	subi.l #44,d0
  2a:	60e4           	bra.s 10 10 _memcpy+0x10
  2c:	2200           	move.l d0,d1
  2e:	e489           	lsr.l #2,d1
  30:	6708           	beq.s 3a 3a _memcpy+0x3a
  32:	5381           	subq.l #1,d1
  34:	20d9           	move.l (a1)+,(a0)+
  36:	51c9 fffc      	dbf d1,34 34 _memcpy+0x34
  3a:	0280 0000 0003 	andi.l #3,d0
  40:	6708           	beq.s 4a 4a _memcpy+0x4a
  42:	5380           	subq.l #1,d0
  44:	10d9           	move.b (a1)+,(a0)+
  46:	51c8 fffc      	dbf d0,44 44 _memcpy+0x44
  4a:	202f 0030      	move.l 48(sp),d0
  4e:	4cdf 7cfc      	movem.l (sp)+,d2-d7/a2-a6
  52:	4e75           	rts

00000054 00000054 _memset:
  54:	48e7 3f3e      	movem.l d2-d7/a2-a6,-(sp)
  58:	206f 0030      	movea.l 48(sp),a0
  5c:	222f 0034      	move.l 52(sp),d1
  60:	202f 0038      	move.l 56(sp),d0
  64:	2248           	movea.l a0,a1
  66:	0281 0000 00ff 	andi.l #255,d1
  6c:	2401           	move.l d1,d2
  6e:	e18a           	lsl.l #8,d2
  70:	8282           	or.l d2,d1
  72:	2401           	move.l d1,d2
  74:	4842           	swap d2
  76:	8282           	or.l d2,d1
  78:	2401           	move.l d1,d2
  7a:	2601           	move.l d1,d3
  7c:	2801           	move.l d1,d4
  7e:	2a01           	move.l d1,d5
  80:	2c01           	move.l d1,d6
  82:	2e01           	move.l d1,d7
  84:	2441           	movea.l d1,a2
  86:	2641           	movea.l d1,a3
  88:	2841           	movea.l d1,a4
  8a:	2a41           	movea.l d1,a5
  8c:	0c80 0000 002c 	cmpi.l #44,d0
  92:	6510           	bcs.s a4 a4 _memset+0x50
  94:	48d0 3cfe      	movem.l d1-d7/a2-a5,(a0)
  98:	41e8 002c      	lea 44(a0),a0
  9c:	0480 0000 002c 	subi.l #44,d0
  a2:	60e8           	bra.s 8c 8c _memset+0x38
  a4:	2400           	move.l d0,d2
  a6:	e48a           	lsr.l #2,d2
  a8:	6708           	beq.s b2 b2 _memset+0x5e
  aa:	5382           	subq.l #1,d2
  ac:	20c1           	move.l d1,(a0)+
  ae:	51ca fffc      	dbf d2,ac ac _memset+0x58
  b2:	202f 0038      	move.l 56(sp),d0
  b6:	0280 0000 0003 	andi.l #3,d0
  bc:	6708           	beq.s c6 c6 _memset+0x72
  be:	5380           	subq.l #1,d0
  c0:	10c1           	move.b d1,(a0)+
  c2:	51c8 fffc      	dbf d0,c0 c0 _memset+0x6c
  c6:	2009           	move.l a1,d0
  c8:	4cdf 7cfc      	movem.l (sp)+,d2-d7/a2-a6
  cc:	4e75           	rts
  ce:	0000           	Address 0x00000000000000d0 is out of bounds.
.short 0x0000
