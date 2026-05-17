/*
 * BFS — AmigaOS handler entry point
 *
 * Minimal startup: just call EntryPoint() from handler.c.
 * Stack swap handled by AmigaOS for filesystem handlers.
 */

	.globl _EntryPoint
	.globl start

	/* Fake segment header for LoadSeg */
	.long 0
	.long 16

start:
	bsr _EntryPoint
	moveq #0,d0
	rts
