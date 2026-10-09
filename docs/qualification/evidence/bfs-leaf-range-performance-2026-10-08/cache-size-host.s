	.build_version macos, 26, 0	sdk_version 26, 5
	.section	__TEXT,__const
	.globl	_cache_descriptor_bytes         ; @cache_descriptor_bytes
	.p2align	3, 0x0
_cache_descriptor_bytes:
	.quad	72                              ; 0x48

	.globl	_cache_object_bytes             ; @cache_object_bytes
	.p2align	3, 0x0
_cache_object_bytes:
	.quad	144                             ; 0x90

	.globl	_cache_scratch_slots            ; @cache_scratch_slots
	.p2align	3, 0x0
_cache_scratch_slots:
	.quad	4                               ; 0x4

.subsections_via_symbols
