# Changelog

## [0.1.1](https://github.com/metaneutrons/BFS/compare/bfs-v0.1.0...bfs-v0.1.1) (2026-06-29)


### Features

* add comprehensive host test suite and diagnostic tools ([7778dc9](https://github.com/metaneutrons/BFS/commit/7778dc9ae5d5f7fed18e2305addcf0fa29f5d675))
* **amiga:** add AmigaOS DOS packet handler and block I/O backend ([798ef9b](https://github.com/metaneutrons/BFS/commit/798ef9bd1f577747677f75af5b197ba4b12f1d1e))
* **amiga:** DOS handler and block I/O for snapshots; geometry hardening ([c4870d4](https://github.com/metaneutrons/BFS/commit/c4870d4fdee96cc53bcfc9822fd4b734d6a36aae))
* **core:** add inode and directory management layers ([253dd0b](https://github.com/metaneutrons/BFS/commit/253dd0b60e9feeb48ae34c8359515ac94921f7d0))
* **core:** add transaction manager and dual-superblock support ([2317c54](https://github.com/metaneutrons/BFS/commit/2317c54d1d7d3beb077d3a02f6b9589d2dbc0365))
* **core:** extent-mapped file I/O and namespace ops with sound locking ([4eb33c7](https://github.com/metaneutrons/BFS/commit/4eb33c72b9fc662e1b2ea58f7e7d6aa40041c95a))
* **core:** free-space allocator reserve pool with auto-refill ([366a4f4](https://github.com/metaneutrons/BFS/commit/366a4f4c5ef3465ee1f9194dadb458e8f234f384))
* **core:** fs handle scaffold, rwlock abstraction, unaligned-safe accessors ([9ac7142](https://github.com/metaneutrons/BFS/commit/9ac71427e7727a9743f05f45261878b5f5bfc292))
* **core:** implement file I/O with extent-based block mapping ([3e0ac3d](https://github.com/metaneutrons/BFS/commit/3e0ac3dfd40f88ecbeda213d869a096f6f5bc140))
* **core:** implement robust B+tree engine with COW support ([309d596](https://github.com/metaneutrons/BFS/commit/309d596586757c44726f9c1d1e88c36d308f8a0e))
* **core:** implement self-hosting free space allocator ([f6c8252](https://github.com/metaneutrons/BFS/commit/f6c82526facbbc18d7a430ea82b48d083a7b8607))
* **core:** online B+tree compaction and COW free tracking ([6170047](https://github.com/metaneutrons/BFS/commit/617004747bf7cec30a1491bceca6c0d35d7bb5e3))
* **core:** per-file extent remap and batched truncate ([31750c3](https://github.com/metaneutrons/BFS/commit/31750c3cd3a5e5524df4d4d6b7e089c46ef4a164))
* **core:** snapshot subsystem — refcount sharing, COW, resumable delete ([305bb81](https://github.com/metaneutrons/BFS/commit/305bb811f659d05e88b6c03afccae128750443d8))
* implement hard links, soft links, and B+tree based snapshots ([a906f6e](https://github.com/metaneutrons/BFS/commit/a906f6e89ec3c7adda4b212ce7586cd0fa06e8ce))
* initial project scaffolding and build system ([80b313c](https://github.com/metaneutrons/BFS/commit/80b313c5104165324cdad4ad186b944f612cd821))


### Bug Fixes

* **ci:** update release-please action configuration for v4 ([f69f18a](https://github.com/metaneutrons/BFS/commit/f69f18ae41223ef04181c947150dd9d030d9d6df))
* **core:** bound on-disk extent length in truncate (C4) ([6432599](https://github.com/metaneutrons/BFS/commit/64325991eba142e8b8867277bb84b6476bf7b3ff))
* **core:** cow_node distinguishes I/O errors from ENOSPC ([6a64eb3](https://github.com/metaneutrons/BFS/commit/6a64eb377cd2f5e0e944a20324692b69d96188b9))
* **core:** eliminate btree_free_node's silent block drop ([#41](https://github.com/metaneutrons/BFS/issues/41)) ([673c3ba](https://github.com/metaneutrons/BFS/commit/673c3bacf18a4726ed4036a8df364d21e8acb2a6))
* **core:** flush inode before mid-write/truncate commits (C1) ([29ecd45](https://github.com/metaneutrons/BFS/commit/29ecd45302bc83430f6c169c71108802803a7eac))
* **core:** harden error handling in COW, tree walks and snapshots ([004938d](https://github.com/metaneutrons/BFS/commit/004938d8e453bc6eac2bbca850935b19a150ae5a))
* **core:** reject corrupt B+tree node headers from disk (C2) ([aadd2e4](https://github.com/metaneutrons/BFS/commit/aadd2e4496b27a74458b2523afecdb5bea5f53aa))
* **core:** resolve unaligned pointer warnings by accessing emergency pool via SB pointer ([25889e4](https://github.com/metaneutrons/BFS/commit/25889e44b383e6605d97e86b1cb1b5fa335f3bc4))
* resolve low-severity review nits ([#38](https://github.com/metaneutrons/BFS/issues/38)/[#40](https://github.com/metaneutrons/BFS/issues/40)/[#42](https://github.com/metaneutrons/BFS/issues/42)) ([6040b7d](https://github.com/metaneutrons/BFS/commit/6040b7de6a35bc71b8f4426be4b31cb7c16d6999))
