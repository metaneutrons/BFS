	.build_version macos, 26, 0	sdk_version 26, 5
	.section	__TEXT,__const
	.globl	_inode_hint_bytes               ; @inode_hint_bytes
	.p2align	3, 0x0
_inode_hint_bytes:
	.quad	2048                            ; 0x800

	.globl	_inode_hint_slot_bytes          ; @inode_hint_slot_bytes
	.p2align	3, 0x0
_inode_hint_slot_bytes:
	.quad	32                              ; 0x20

	.globl	_cursor_bytes                   ; @cursor_bytes
	.p2align	3, 0x0
_cursor_bytes:
	.quad	48                              ; 0x30

.subsections_via_symbols
