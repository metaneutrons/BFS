# Inode stamp qualification notes

Primary-agent record of the independent Luna reviews and executed gates.
This is a summary of the reviews, not a verbatim worker transcript.

## Independent review

Production review covered the seven changed core/header/handler files at the
SHA-256 identities in `inode-stamp-input-identities.log`. It found no blocking
COW, callback-order, field-update or recovery issue. The error-side
dirty/notification difference is explicitly disclosed in the qualification
report. A first identity comparison confused Git SHA-1 with SHA-256; the
corrected comparison used the supplied SHA-256 identities.

Independent test review covered the final seven functional and four
fault/ordering cases. It found no blocker and identified proof limits:
superblock BIO-magic counts do not inspect inode bytes at each publication or
prove hardware barriers; no external OS-notification or hardware-clock test
ran; first-data-BIO failure on a nonzero request with no progress is not
directly covered. The callback is a pure counter/stamp provider in the fault
tests and does not serve as a fault selector. Inode-write fault selection uses
the internal B-tree layout and exact inode signature, not callback side effects.
The prefix-write test proves exact recovery without asserting that the prefix
must necessarily create a bad CRC in every layout.

Independent probe extraction and normal timing calculations agree with the
primary summaries. All sixteen normal schema-1 TSVs have the expected six
integer timing metrics, filesystem drives and PASS=1. All four probe outputs
passed the strict workload/data verifier.

## Executed gates

- Normal host: 48 suites, 460 tests, zero failures,
  `inode-stamp-host-test.log`, isolated `build/host-inode-stamp`.
- ASan/UBSan host: 48 suites, 460 tests, zero failures,
  `inode-stamp-sanitizer-test.log`, isolated `build/host-inode-stamp-sanitized`.
  The actual make variable is HOST_CFLAGS; logged compilations include
  `-fsanitize=address,undefined -fno-omit-frame-pointer -O1`.
  ASAN_OPTIONS=detect_leaks=0 was used.
- Quality: 70 tests plus shell/static checks, `inode-stamp-quality.log`.
- Linux conformance and actual FUSE: 20 tests and the mounted-volume gate,
  `inode-stamp-linux-fuse.log`.
- Normal/probe m68k: two separate clean build directories reproduce both
  handler hashes, `inode-stamp-amiga.log` and `inode-stamp-amiga-rebuilt.log`.
- Existing targeted file/fs/hardware-fault/snapshot suites and the final
  focused seven/four timestamp suites pass in the retained focused logs.

The first seven-case focused functional run failed because its mask oracle
expected clearing 0x0F00 from 0xF00F to leave 0x000F. The supplied mask had no
set bits to clear. The test was corrected to clear 0xF000 before the final
normal/sanitizer and other gates. Its failed log remains unchanged apart from
copied-log trailing horizontal whitespace removal. Earlier two-case fault and
intermediate focused logs are development attempts, not substitutes for the
final four-case fault gate.

No CI, external publication, real power-cut apparatus, controller-lie test or
device-bound hardware-media qualification is claimed.
