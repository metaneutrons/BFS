; BFS — AmigaOS handler entry point (VBCC/vasm Motorola syntax)
;
; Minimal startup: just call _EntryPoint() from handler.c.

	section	text,code

	public	_EntryPoint
	public	start

	; Fake segment header for LoadSeg
	dc.l	0
	dc.l	16

start:
	bsr	_EntryPoint
	moveq	#0,d0
	rts
