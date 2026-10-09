	.build_version macos, 26, 0	sdk_version 26, 5
	.section	__TEXT,__const
	.globl	_cursor_bytes                   ; @cursor_bytes
	.p2align	3, 0x0
_cursor_bytes:
	.quad	336                             ; 0x150

	.globl	_cursor_path_bytes              ; @cursor_path_bytes
	.p2align	3, 0x0
_cursor_path_bytes:
	.quad	260                             ; 0x104

.subsections_via_symbols
