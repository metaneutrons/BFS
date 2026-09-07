# Changelog

## [0.1.1](https://github.com/metaneutrons/BFS/compare/v0.1.0...v0.1.1) (2026-09-07)


### Features

* add 68080 release build ([be94f40](https://github.com/metaneutrons/BFS/commit/be94f404e06ef4a940bb4a2e74609a7d5c9799a0))
* add comprehensive host test suite and diagnostic tools ([7778dc9](https://github.com/metaneutrons/BFS/commit/7778dc9ae5d5f7fed18e2305addcf0fa29f5d675))
* **amiga:** add AmigaOS DOS packet handler and block I/O backend ([798ef9b](https://github.com/metaneutrons/BFS/commit/798ef9bd1f577747677f75af5b197ba4b12f1d1e))
* **amiga:** DOS handler and block I/O for snapshots; geometry hardening ([2410330](https://github.com/metaneutrons/BFS/commit/24103301bcdbd5be982ddaf1bd5c206f3a25f7fc))
* **core:** add inode and directory management layers ([253dd0b](https://github.com/metaneutrons/BFS/commit/253dd0b60e9feeb48ae34c8359515ac94921f7d0))
* **core:** add transaction manager and dual-superblock support ([2317c54](https://github.com/metaneutrons/BFS/commit/2317c54d1d7d3beb077d3a02f6b9589d2dbc0365))
* **core:** extent-mapped file I/O and namespace ops with sound locking ([8afbf31](https://github.com/metaneutrons/BFS/commit/8afbf31a11efe47dcce183c66fab53c59f22ae65))
* **core:** free-space allocator reserve pool with auto-refill ([2999920](https://github.com/metaneutrons/BFS/commit/2999920ada84b7fcdd1e28692f2400446478ef58))
* **core:** fs handle scaffold, rwlock abstraction, unaligned-safe accessors ([e4aa486](https://github.com/metaneutrons/BFS/commit/e4aa4865297a193dfc3671d0498827fe79ce7bdc))
* **core:** implement file I/O with extent-based block mapping ([3e0ac3d](https://github.com/metaneutrons/BFS/commit/3e0ac3dfd40f88ecbeda213d869a096f6f5bc140))
* **core:** implement robust B+tree engine with COW support ([309d596](https://github.com/metaneutrons/BFS/commit/309d596586757c44726f9c1d1e88c36d308f8a0e))
* **core:** implement self-hosting free space allocator ([f6c8252](https://github.com/metaneutrons/BFS/commit/f6c82526facbbc18d7a430ea82b48d083a7b8607))
* **core:** online B+tree compaction and COW free tracking ([778a02b](https://github.com/metaneutrons/BFS/commit/778a02b6dd2668e4266994453fc8ed16e3d3784d))
* **core:** per-file extent remap and batched truncate ([c8ddad8](https://github.com/metaneutrons/BFS/commit/c8ddad82c0de9b325dff3bf46a4950013ed1b440))
* **core:** snapshot subsystem — refcount sharing, COW, resumable delete ([ff5a5e2](https://github.com/metaneutrons/BFS/commit/ff5a5e26e0b81dc7bf971e0224c73b9c252a7572))
* implement hard links, soft links, and B+tree based snapshots ([a906f6e](https://github.com/metaneutrons/BFS/commit/a906f6e89ec3c7adda4b212ce7586cd0fa06e8ce))
* initial project scaffolding and build system ([80b313c](https://github.com/metaneutrons/BFS/commit/80b313c5104165324cdad4ad186b944f612cd821))


### Bug Fixes

* **amiga:** qualify handler startup, DOS packets and reproducible builds ([b923962](https://github.com/metaneutrons/BFS/commit/b923962c4091ca05b951d08e985a21740d25390b))
* **ci:** accept completed negative smoke failures ([#21](https://github.com/metaneutrons/BFS/issues/21)) ([b88c63d](https://github.com/metaneutrons/BFS/commit/b88c63d369a11e89c27079dcdeb96c54d6bd8ac7))
* **ci:** update release-please action configuration for v4 ([dad709f](https://github.com/metaneutrons/BFS/commit/dad709f2f7abf381d7bfab1fed6436c671524dd7))
* **core:** bound on-disk extent length in truncate (C4) ([84c0c74](https://github.com/metaneutrons/BFS/commit/84c0c74c2102346298275896d51072b928610f91))
* **core:** cow_node distinguishes I/O errors from ENOSPC ([1832e9b](https://github.com/metaneutrons/BFS/commit/1832e9bc1ea70650252e3fac4039a26819fafc31))
* **core:** eliminate btree_free_node's silent block drop ([#41](https://github.com/metaneutrons/BFS/issues/41)) ([3e7cc1e](https://github.com/metaneutrons/BFS/commit/3e7cc1e4c8ae9265644f5176320ebe6ac92fbb69))
* **core:** flush inode before mid-write/truncate commits (C1) ([a8a2155](https://github.com/metaneutrons/BFS/commit/a8a21555dab63eda9d73c7324a37a02594f929cc))
* **core:** harden error handling in COW, tree walks and snapshots ([7465893](https://github.com/metaneutrons/BFS/commit/74658933d6de39c67f61f4a806a5663a4a32242a))
* **core:** harden filesystem ownership and recovery ([#12](https://github.com/metaneutrons/BFS/issues/12)) ([cedda17](https://github.com/metaneutrons/BFS/commit/cedda17f8c2fe4e6389fa9a520f1a89941695605))
* **core:** reject corrupt B+tree node headers from disk (C2) ([20f6d37](https://github.com/metaneutrons/BFS/commit/20f6d37274b59f4073c2d9e41fc93463c0b8a946))
* **core:** resolve unaligned pointer warnings by accessing emergency pool via SB pointer ([3dfe00d](https://github.com/metaneutrons/BFS/commit/3dfe00d30970c0ebffd4e81674ce0a58fef6c1ac))
* **release:** align Release Please tags with release workflow ([#22](https://github.com/metaneutrons/BFS/issues/22)) ([134aff5](https://github.com/metaneutrons/BFS/commit/134aff5a8506e75061c1d1b32289fc76302fd8c3))
* **release:** find draft releases through GitHub list API ([#23](https://github.com/metaneutrons/BFS/issues/23)) ([23dd64e](https://github.com/metaneutrons/BFS/commit/23dd64ef1027c7037ecbca17502c4c99a8807060))
* remove vendored third-party binaries ([#5](https://github.com/metaneutrons/BFS/issues/5)) ([4897439](https://github.com/metaneutrons/BFS/commit/489743932b3417586c3b09b4d2d69d78231d8df8))
* resolve low-severity review nits ([#38](https://github.com/metaneutrons/BFS/issues/38)/[#40](https://github.com/metaneutrons/BFS/issues/40)/[#42](https://github.com/metaneutrons/BFS/issues/42)) ([e681b1f](https://github.com/metaneutrons/BFS/commit/e681b1fcde2966210fe3d6a8d8e08c874b37fb75))
