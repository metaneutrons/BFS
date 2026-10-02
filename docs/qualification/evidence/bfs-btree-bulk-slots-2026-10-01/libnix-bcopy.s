
build/btree-bulk-slots-libnix-bcopy.o:     file format amiga


Disassembly of section .text:

00000000 00000000 ___bcopz:
   0:	2f0a           	move.l a2,-(sp)
   2:	2f02           	move.l d2,-(sp)
   4:	246f 000c      	movea.l 12(sp),a2
   8:	222f 0010      	move.l 16(sp),d1
   c:	202f 0014      	move.l 20(sp),d0
  10:	224a           	movea.l a2,a1
  12:	2041           	movea.l d1,a0
  14:	676a           	beq.s 80 80 ___bcopz+0x80
  16:	b28a           	cmp.l a2,d1
  18:	646c           	bcc.s 86 86 ___bcopz+0x86
  1a:	7410           	moveq #16,d2
  1c:	b480           	cmp.l d0,d2
  1e:	6452           	bcc.s 72 72 ___bcopz+0x72
  20:	0801 0000      	btst #0,d1
  24:	6706           	beq.s 2c 2c ___bcopz+0x2c
  26:	10d9           	move.b (a1)+,(a0)+
  28:	5380           	subq.l #1,d0
  2a:	2208           	move.l a0,d1
  2c:	0801 0001      	btst #1,d1
  30:	6704           	beq.s 36 36 ___bcopz+0x36
  32:	30d9           	move.w (a1)+,(a0)+
  34:	5580           	subq.l #2,d0
  36:	2200           	move.l d0,d1
  38:	ea89           	lsr.l #5,d1
  3a:	671c           	beq.s 58 58 ___bcopz+0x58
  3c:	5381           	subq.l #1,d1
  3e:	20d9           	move.l (a1)+,(a0)+
  40:	20d9           	move.l (a1)+,(a0)+
  42:	20d9           	move.l (a1)+,(a0)+
  44:	20d9           	move.l (a1)+,(a0)+
  46:	20d9           	move.l (a1)+,(a0)+
  48:	20d9           	move.l (a1)+,(a0)+
  4a:	20d9           	move.l (a1)+,(a0)+
  4c:	20d9           	move.l (a1)+,(a0)+
  4e:	51c9 ffee      	dbf d1,3e 3e ___bcopz+0x3e
  52:	4241           	clr.w d1
  54:	5381           	subq.l #1,d1
  56:	64e6           	bcc.s 3e 3e ___bcopz+0x3e
  58:	e9c0 16c3      	bfextu d0,27,3,d1
  5c:	670e           	beq.s 6c 6c ___bcopz+0x6c
  5e:	5381           	subq.l #1,d1
  60:	20d9           	move.l (a1)+,(a0)+
  62:	51c9 fffc      	dbf d1,60 60 ___bcopz+0x60
  66:	4241           	clr.w d1
  68:	5381           	subq.l #1,d1
  6a:	64f4           	bcc.s 60 60 ___bcopz+0x60
  6c:	7203           	moveq #3,d1
  6e:	c081           	and.l d1,d0
  70:	670e           	beq.s 80 80 ___bcopz+0x80
  72:	5380           	subq.l #1,d0
  74:	10d9           	move.b (a1)+,(a0)+
  76:	51c8 fffc      	dbf d0,74 74 ___bcopz+0x74
  7a:	4240           	clr.w d0
  7c:	5380           	subq.l #1,d0
  7e:	64f4           	bcc.s 74 74 ___bcopz+0x74
  80:	241f           	move.l (sp)+,d2
  82:	245f           	movea.l (sp)+,a2
  84:	4e75           	rts
  86:	43f2 0800      	lea (0,a2,d0.l),a1
  8a:	d280           	add.l d0,d1
  8c:	2041           	movea.l d1,a0
  8e:	7410           	moveq #16,d2
  90:	b480           	cmp.l d0,d2
  92:	6452           	bcc.s e6 e6 ___bcopz+0xe6
  94:	0801 0000      	btst #0,d1
  98:	6706           	beq.s a0 a0 ___bcopz+0xa0
  9a:	1121           	move.b -(a1),-(a0)
  9c:	5380           	subq.l #1,d0
  9e:	2208           	move.l a0,d1
  a0:	0801 0001      	btst #1,d1
  a4:	6704           	beq.s aa aa ___bcopz+0xaa
  a6:	3121           	move.w -(a1),-(a0)
  a8:	5580           	subq.l #2,d0
  aa:	2200           	move.l d0,d1
  ac:	ea89           	lsr.l #5,d1
  ae:	671c           	beq.s cc cc ___bcopz+0xcc
  b0:	5381           	subq.l #1,d1
  b2:	2121           	move.l -(a1),-(a0)
  b4:	2121           	move.l -(a1),-(a0)
  b6:	2121           	move.l -(a1),-(a0)
  b8:	2121           	move.l -(a1),-(a0)
  ba:	2121           	move.l -(a1),-(a0)
  bc:	2121           	move.l -(a1),-(a0)
  be:	2121           	move.l -(a1),-(a0)
  c0:	2121           	move.l -(a1),-(a0)
  c2:	51c9 ffee      	dbf d1,b2 b2 ___bcopz+0xb2
  c6:	4241           	clr.w d1
  c8:	5381           	subq.l #1,d1
  ca:	64e6           	bcc.s b2 b2 ___bcopz+0xb2
  cc:	e9c0 16c3      	bfextu d0,27,3,d1
  d0:	670e           	beq.s e0 e0 ___bcopz+0xe0
  d2:	5381           	subq.l #1,d1
  d4:	2121           	move.l -(a1),-(a0)
  d6:	51c9 fffc      	dbf d1,d4 d4 ___bcopz+0xd4
  da:	4241           	clr.w d1
  dc:	5381           	subq.l #1,d1
  de:	64f4           	bcc.s d4 d4 ___bcopz+0xd4
  e0:	7203           	moveq #3,d1
  e2:	c081           	and.l d1,d0
  e4:	679a           	beq.s 80 80 ___bcopz+0x80
  e6:	5380           	subq.l #1,d0
  e8:	1121           	move.b -(a1),-(a0)
  ea:	51c8 fffc      	dbf d0,e8 e8 ___bcopz+0xe8
  ee:	4240           	clr.w d0
  f0:	5380           	subq.l #1,d0
  f2:	64f4           	bcc.s e8 e8 ___bcopz+0xe8
  f4:	241f           	move.l (sp)+,d2
  f6:	245f           	movea.l (sp)+,a2
  f8:	4e75           	rts
  fa:	0000           	Address 0x00000000000000fc is out of bounds.
.short 0x0000
