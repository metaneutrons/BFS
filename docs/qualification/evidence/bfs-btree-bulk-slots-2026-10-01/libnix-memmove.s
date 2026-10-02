
build/btree-bulk-slots-libnix-memmove.o:     file format amiga


Disassembly of section .text:

00000000 00000000 _memmove:
   0:	2f02           	move.l d2,-(sp)
   2:	242f 0008      	move.l 8(sp),d2
   6:	2f2f 0010      	move.l 16(sp),-(sp)
   a:	2f02           	move.l d2,-(sp)
   c:	2f2f 0014      	move.l 20(sp),-(sp)
  10:	4eb9 0000 0000 	jsr 0 0 _bcopy
  16:	2002           	move.l d2,d0
  18:	4fef 000c      	lea 12(sp),sp
  1c:	241f           	move.l (sp)+,d2
  1e:	4e75           	rts
