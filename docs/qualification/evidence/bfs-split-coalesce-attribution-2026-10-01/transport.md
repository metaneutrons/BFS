# Transport and packaging receipts

The first guest completed and wrote its exact completion marker before the
local transport was interrupted. Large SSH stdout stopped after 833 bytes,
at the BFS results heading; no owned remote emulator or benchmark process
remained in the process checks. A subsequent rsync sender also stalled.
Only identified local task SSH PID 61042 and rsync/SSH PIDs 61913/61914 were
terminated. The original run wrapper ultimately exited 20 during interrupted
rsync; it did not reach its local verifier. Its partial run log and empty
outer log remain. The first workload was not rerun or discarded.

An unpaced 16,384-byte SSH read returned 0 bytes after its owned client
PID 62198 was terminated; a 512-byte read returned 512. A 512-byte-paced
16,384-byte read returned 16,384. Compression/IPQoS-none alone did not resolve
the stalled unpaced transfer. These observations establish a practical
transport workaround, not its cause. No SSH, routing or network configuration
was changed persistently.

Text-only gzip bundles were created once on Cachy, fetched in 512-byte chunks
with 0.05-second spacing, hashed and extracted into the prepared local systems.
Both raw systems then passed the same strict schema-12 consumer.

| Run | Bundle bytes | SHA-256 |
| --- | ---: | --- |
| BFS-first | 8664 | 83fabf2395b441cc6a7ef9ad686a475fc1ede84b1c89d29df311898be9a6d1a0 |
| PFS3-first | 36067 | b9d8215e10bfe1fba63b440b6cec1db3075ad2b6db5c87f45b25e4cd2d5b1dcd |

The second run uses remote runner-output.log and a short SSH exit receipt,
capturing runner exit 0. It keeps the same handler, guest, geometry and workload.
The second bundle also retains its full runner output. Both systems are later
recoverably archived, and all four raw TSV hashes agree before and after.

A generated private-patch command initially used zsh's special `path` loop
variable, replacing PATH in that one child shell; git could not be found and
the output patch was empty. That empty first artifact is retained. Using
`task_file` corrected the command; production source and binaries were never
changed. The persistence helper's first ShellCheck SC2295 quote warning was
retained and corrected. Fixture/consumer preliminary failures and their
corrections are separately retained by their authors. No semantic measurement
observation is removed on account of these tooling corrections.
