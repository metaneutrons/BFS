# Independent actual result audit

Luna independently reran the strict summary and decision after all 14 starts.
Both outputs match the archived files byte for byte:

- Summary SHA-256: 1b65e9ec8d7b39001179fe0b09f27ffb8a35a4488d87df7d27f5004627114ae5.
- Decision SHA-256: 0e8ed1499f455338423ebccd8a2ebd810313ee0d6c29152396aced18e7e74752.

The exact fixed schedule, all 28 BFS/PFS3 TSV outputs and 23 phases per output
validate. Configuration, startup order, production without Stack override,
diagnostic Stack 32768 and all 14 actual RDB buffers 30 records validate. Each
run has 67 unique asset mappings, identical within cohorts; only the pinned
guest differs across production and diagnostic cohorts. Runtime receipts
match pre/post and against the pinned handler, PFS3 and guest identities.

All source receipts contain 229 entries. Remote pre/post, local measurement-time
before/final and remote-export logs match byte for byte; their SHA-256 is
652c1b03fe814ef7e01fba9f4611b79cdfacc9192a91d15e4e421555fab9b2b2.
The archived protocol passes all 229 source hash checks. Input check pre/post
logs match. Current source intentionally differs from the candidate after its
rejection; the preimage/byte-exact restoration is separately checked by root.

Normal primary ratios 0.9509359798/1.134151093 have median 1.0425435364;
durable 1.002310834/1.01352938 have median 1.0079201068. Only one of four target
pairs is not slower. Diagnostic buffer requests drop 517 to 262. Durable
first-pass ExAll1000 ratios 1.3072439634/1.1735537190 breach the repeated guard.
The negative `retain_locally=false` decision follows the pre-run policy.
There is no retry, exclusion, pooling or cause attribution. Controls do not
establish noise bounds or production clearance.
