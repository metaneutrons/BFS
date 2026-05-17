# Changelog

## [0.1.1](https://github.com/metaneutrons/BFS/compare/bfs-v0.1.0...bfs-v0.1.1) (2026-05-17)


### Features

* add comprehensive host test suite and diagnostic tools ([7778dc9](https://github.com/metaneutrons/BFS/commit/7778dc9ae5d5f7fed18e2305addcf0fa29f5d675))
* **amiga:** add AmigaOS DOS packet handler and block I/O backend ([798ef9b](https://github.com/metaneutrons/BFS/commit/798ef9bd1f577747677f75af5b197ba4b12f1d1e))
* **core:** add inode and directory management layers ([253dd0b](https://github.com/metaneutrons/BFS/commit/253dd0b60e9feeb48ae34c8359515ac94921f7d0))
* **core:** add transaction manager and dual-superblock support ([2317c54](https://github.com/metaneutrons/BFS/commit/2317c54d1d7d3beb077d3a02f6b9589d2dbc0365))
* **core:** implement file I/O with extent-based block mapping ([3e0ac3d](https://github.com/metaneutrons/BFS/commit/3e0ac3dfd40f88ecbeda213d869a096f6f5bc140))
* **core:** implement robust B+tree engine with COW support ([309d596](https://github.com/metaneutrons/BFS/commit/309d596586757c44726f9c1d1e88c36d308f8a0e))
* **core:** implement self-hosting free space allocator ([f6c8252](https://github.com/metaneutrons/BFS/commit/f6c82526facbbc18d7a430ea82b48d083a7b8607))
* implement hard links, soft links, and B+tree based snapshots ([a906f6e](https://github.com/metaneutrons/BFS/commit/a906f6e89ec3c7adda4b212ce7586cd0fa06e8ce))
* initial project scaffolding and build system ([80b313c](https://github.com/metaneutrons/BFS/commit/80b313c5104165324cdad4ad186b944f612cd821))


### Bug Fixes

* **ci:** update release-please action configuration for v4 ([f69f18a](https://github.com/metaneutrons/BFS/commit/f69f18ae41223ef04181c947150dd9d030d9d6df))
* **core:** resolve unaligned pointer warnings by accessing emergency pool via SB pointer ([25889e4](https://github.com/metaneutrons/BFS/commit/25889e44b383e6605d97e86b1cb1b5fa335f3bc4))
