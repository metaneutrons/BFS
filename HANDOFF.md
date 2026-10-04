# BFS: Übergabe für die Weiterarbeit

Stand: 2026-10-04. Gearbeitet wird auf `main`. Die v3-Kette mit der Arbeit
dieser Runde liegt seit #83 als Squash-Commit `b8ea972` dort; ihre
Einzelcommits erhält der Tag `archive/format-v3-inline-extents` (siehe
Abschnitt 3).
Die vorige Übergabe zum Release v0.1.1 liegt unter
`docs/qualification/release-v0.1.1-handoff-2026-09-08.md`.

## 1. Sitzungsstart

```sh
cd ~/Source/Amiga/BFS
git status --short                  # lokale Änderungen zuerst sichern
git fetch origin
git switch main
git pull --ff-only
make -j8 host-test                  # Host-Tests
make amiga amiga-test               # Handler und AROS-Testprogramm
make ci-test                        # 50 AROS-Tests im Emulator
emulator-test/ci-local.sh 1800      # dieselben Tests unter Kickstart 3.2
```

Die lizenzierten Dateien liegen nicht im Repository und werden von Git
ignoriert. Für Kickstart-Läufe und Messungen müssen vorhanden sein:
`emulator-test/.assets/A1200.47.102.rom`, `emulator-test/.assets/{C,Libs}`
mit den Workbench-3.2-Befehlen und `emulator-test/.cache/pfs3aio`. Auf dem
Mac stammen ROM und `pfs3aio` aus `~/Downloads/bfs-claude-assets-2026-10-02.nBr8KF`
(Prüfsummen dort), `C` und `Libs` aus der unveränderten `Workbench3.2.adf`,
entpackt mit `xdftool`. `Format` liegt auf der Diskette in `System/` und muss
nach `.assets/C` kopiert werden, sonst bleibt die PFS3-Partition der
Messungen unformatiert und der Lauf hängt bis zum Timeout.

Auf dem Mac ist `make` im Shell-Snapshot von Claude Code eine defekte
zsh-Funktion; `command make` umgeht sie. `make quality-gates` braucht
ripgrep, das nicht installiert ist. In der letzten Sitzung lief es mit einem
Wrapper, der das in Claude Code eingebaute `rg` aufruft.

Der Reset-Test (`make reset-test`) braucht Xvfb und xdotool und läuft so nur
unter Linux.

## 2. Ziel und Stand

Ziel: BFS in jedem geprüften AmigaDOS-Workload höchstens fünfmal so langsam
wie PFS3, ohne Abstriche bei Integrität, Snapshots und Recovery.

Abschlussmessung auf Cachy (je 8 Läufe, Median des Verhältnisses je Lauf,
Schema 3 mit Anhängen in kleinen Schritten). Im Modus `compare` liegt kein
Lauf über 5×.

| Workload | vorher (`5aff558`) | jetzt | jetzt, `durable-compare` |
| --- | ---: | ---: | ---: |
| Create 40 | 2,47× | 1,72× | 1,88× |
| Lookup 400 | 1,10× | 1,06× | 1,12× |
| Read 40 | 1,31× | 1,45× | 1,38× |
| ExNext 400 | 2,84× | 2,69× | 2,72× |
| ExAll 400 | 3,90× | 3,62× | 3,46× |
| Write 8 MiB | 1,75× | 0,95× | 1,01× |
| Read 8 MiB | 1,02× | 1,02× | 1,03× |
| Delete 40 | 3,02× | 2,08× | 2,17× |
| Anhängen 1 MiB in 4-KiB-Schritten | 4,82× | 2,15× | 2,36× |
| Anhängen 256 KiB in 1-KiB-Schritten | 4,16× | 2,57× | 2,65× |
| Lesen der angehängten Dateien | 1,43× | 1,05× | 1,05× |

In `durable-compare` lag ein Lauf beim Anhängen in 1-KiB-Schritten bei 5,05×
(sechs Läufe 60–64 ms, zwei 94 und 125 ms). Einzelheiten, Zwischenreihen,
Kopienzahlen und Prüfsummen:
`docs/qualification/bfs-write-path-in-place-performance-2026-10-04.md`.

Eine Bestätigungsreihe des Stands vom 3. Oktober auf dem Mac (Schema 2)
hat die Cloud-Faktoren bestätigt; sie steht im selben Bericht.

## 3. Zweige und Commits

- #83 hat `format/v3-inline-extents` mit `perf/write-path-stages` und
  `perf/txn-owned-nodes` (#82, ohne Merge geschlossen) als `b8ea972` auf
  `main` gebracht, wie zuvor #81 die Kette #75–#80 als `79df2e9`. Der
  Commit ist als Breaking Change markiert (Format v3).
- Die Zweige sind gelöscht. Ihre Einzelcommits, auf die Berichte und Pläne
  verweisen, erhalten die Tags `archive/format-v3-inline-extents` (diese
  Kette), `archive/bfs-txn-free-tree` (#75–#80) und
  `archive/group-commit-inplace-experiments` (Prototyp `1d7483a`).
- Die native AROS-x86_64-Unterstützung (PR #86, Zweig `feat/aros-x86_64`)
  stammt aus einem uncommittet gefundenen Port. Nach dem Merge entfallen
  der Sicherungszweig `wip/aros-native-port` (`43bd6fb`) und der
  Codex-Worktree mit der uncommitteten Kopie (siehe Abschnitt 7).
- Die zehn Commits dieser Runde bestehen jeder für sich
  `make host-test`; die Commits mit Pufferübernahme und Änderungen an Ort
  und Stelle zusätzlich `make sanitize`.
  1. `4ab8756` test(bench): Anhänge-Phasen (Schema 3).
  2. `7d14490` fix(core): Notfall-Pool-Blöcke beim Nachholen von Freigaben
     in den Pool zurückgeben.
  3. `1a0beef` feat(core): `bfs_btree_update_key`.
  4. `de7ce9b` perf(core): Datenblöcke hinter dem letzten Block der Datei.
  5. `a50428a` perf(core): Zwischenstände in den Cache übernehmen.
  6. `21bfa29` perf(core): Enumeration am Stopp-Eintrag fortsetzen.
  7. `db97ae1` perf(core): Blätter der laufenden Transaktion an Ort und
     Stelle ändern.
  8. `9353dbe` perf(core): Scans kopieren nur belegte Einträge.
  9. `3304a15` perf(core): Verzeichniseinträge bei der Blattvalidierung
     prüfen.
  10. docs: Bericht, Evidenz und diese Übergabe.

Nichts mergen ohne Fabians Freigabe. PRs nur auf ausdrücklichen Wunsch.

## 4. Inhalt des v3-Zweigs

Format v3 (`e68aeb2`, Plan `docs/plans/bfs-format-v3-v1.md`): Inline-Extent
im Inode, Kommentar-Flag, v2 wird abgelehnt, `bfs format` ersetzt Medien mit
ausschließlich älteren Formaten.

Frühere Korrekturen und Leistungsarbeit: siehe
`docs/qualification/bfs-directory-listing-performance-2026-10-03.md` und die
Einzelcommits unter `archive/format-v3-inline-extents`.

Diese Runde (siehe Abschnitt 3 und den Bericht vom 4. Oktober):
Benchmark-Schema 3, Datenvergabe hinter dem letzten Dateiblock, Korrektur
des Notfall-Pools, Pufferübernahme, Änderungen an Ort und Stelle,
Stopp-Eintrag im Cursor, Teilkopie beim Scan, Eintragsprüfung bei der
Validierung.

## 5. Invarianten für neue Arbeit

- `bfs_btree_t.generation` ändert sich bei jedem Knotenschreiben, jeder
  Änderung an Ort und Stelle und jeder Wurzel- oder Höhenänderung.
  Scan-Cursor und Such-Hinweis verlassen sich darauf.
- Ein Scan-Callback darf den Baum ändern. Der Scan sucht dann hinter dem
  zuletzt gelieferten Schlüssel neu und liefert keine danach gelöschten
  Schlüssel.
- Ein Cursor gilt nur bei gleichem Baum, gleicher Wurzel und gleichem Zähler.
  Sein Stopp-Eintrag gilt nur, wenn der Startschlüssel genau diesem Eintrag
  entspricht. Er darf das Mounten seines Baums nicht überleben.
- Eine Ansicht in den Cache (`peek_valid_node`, `modify_dirty_node`) gilt nur
  bis zum nächsten BIO-Aufruf. Mit der Pufferübernahme gehört der alte
  Slot-Puffer danach dem Aufrufer und wird freigegeben.
- An Ort und Stelle geändert wird nur ein Blatt, das der laufenden Transaktion
  gehört, als dirty im Cache liegt und validiert ist, und nur wenn die
  Änderung das Blatt allein betrifft (keine Teilung, kein Umbau, Schlüssel
  bleibt zwischen Nachbarn und Trennschlüsseln). Danach darf nichts mehr
  fehlschlagen können.
- Ein validiertes Verzeichnisblatt enthält nur Einträge, die `dir_entry_ok`
  besteht; eine Änderung an Ort und Stelle prüft den Eintrag, den sie
  schreibt. Scans prüfen Einträge nicht mehr selbst.
- Dateidaten kommen aus `bfs_freespace_alloc_data`, Metadaten aus
  `bfs_freespace_alloc` und dem Vorrat. Daten nehmen nie das Ende des
  höchsten freien Bereichs.
- Blöcke des Notfall-Pools werden einzeln freigegeben, damit sie in den Pool
  zurückkehren.
- Bäume mit u32-Schlüsseln verwenden `bfs_btree_key_compare_be32`.
- ExAll-Einträge enthalten nur die Felder bis zum angefragten Typ; ein
  Eintrag, der nicht passt, kommt im nächsten Aufruf.

## 6. Messen

Messreihen laufen auf `cachy` (SSH-Alias, CachyOS-KVM-Gast, FS-UAE 3.2.35,
Xvfb). Dort liegt eine per rsync gespiegelte Arbeitskopie unter
`~/.cache/bfs-performance/work-2026-10-03` (ohne Git), das Kickstart-ROM unter
`~/Amiga/kick.a1200.47.102.rom`. Gastprogramme und Handler werden auf dem Mac
gebaut und kopiert; `make build/host/bfs` baut den Formatierer auf Cachy. Der
Docker-Container `snapdog-runner` ist auf Fabians Wunsch gestoppt und bleibt
aus.

```sh
make amiga amiga-fs-compare-bench
emulator-test/bench-series.sh NAME 8 compare alt=PFAD neu=build/amiga/bfshandler
tools/bench-summary.py NAME-alt NAME-neu
```

Auf Cachy streuen einzelne Läufe stark (bis Faktor zwei in beiden
Dateisystemen). Acht Läufe je Handler und der Median sind belastbarer als der
Mittelwert. `make core-workload-profile` braucht Valgrind, das weder auf dem
Mac noch auf Cachy installiert ist.

## 7. Offene Punkte und Entscheidungen

1. release-please baut den Release-PR #57 nach #83 wegen des Breaking
   Change neu auf; vor einem Release prüfen.
2. Codacy meldet für #83 zehn Funktionen oder Testdateien über der
   Längengrenze von Lizard, darunter `freespace_alloc_core`,
   `bfs_btree_scan_cursor` und `node_write_image`. Codacy ist kein
   Pflicht-Check; die übrigen Befunde sind behoben oder begründet
   unterdrückt.
3. Ausreißer beim Anhängen: einzelne Läufe brauchen fast doppelt so lange.
   Ungeklärt, ob Host-Störung oder ein Timer-Commit innerhalb der Phase.
4. ExAll liegt bei rund 3,6×. Etwa 44 % der Host-Arbeit der Auflistung ist
   das Inode-Lesen je Eintrag; eine Sortierung hilft beim einstufigen
   Inode-Baum des Benchmarks nicht.
5. Auf dem Copy-on-Write-Pfad werden Elternknoten auch dann neu geschrieben,
   wenn das Blatt an Ort und Stelle bleibt; relevant erst bei höheren Bäumen.
6. Feature-Masken im Superblock: Entscheidung offen.
7. Kickstart-Job in der CI: ob Secret `CI_TEST_ENV` und das verschlüsselte
   Archiv noch existieren, ist ungeklärt.
8. Löschen sehr großer Dateien braucht pro Block einen Eintrag in der
   Freigabe-Warteschlange, die auf dem Amiga per malloc wächst.
9. Nicht getestet: echte Hardware, MorphOS und OS4 nativ, andere
   Kickstart-Versionen, 68000.
10. Natives AROS x86_64 (PR #86): `make aros AROS_ROOT=<AROS-NX>` und
    `make aros-ci-test` bestehen alle 52 Integrationstests auf dem
    AROS-NX-ISO `7d0a509` unter QEMU (q35). Vereinbart: hier pausieren, bis
    #86 gemergt und ein BFS-Release getaggt ist; zuerst `aros-tools` und
    `aros-toolchains` (auch ESP32) fertigstellen, dann BFS als metamake-Paket
    in AROS-NX aufnehmen, das ein BFS-Release per `%fetch` holt. Gepflegt
    wird nur hier. Offen für die BFS-CI: ein festes AROS-NX-SDK und Boot-ISO
    als Release-Artefakt.
11. Beim Nachweis gefundene AROS-NX-Fehler, Korrekturen gehören nach
    AROS-NX: `Echo >SER:` stürzt auf q35 ab; ein Dateisystem, das `LoadSeg`
    nicht laden kann, stürzt beim Aktivieren ab, statt einen Fehler zu
    melden; der PCI-Treiber stürzt auf i440FX ohne MCFG-Tabelle ab (lokaler
    Zweig `fix/pcipc-null-legacy-tags-validation`).

## 8. Arbeitsregeln

- Commits nach Conventional Commits, auf Englisch, als
  `metaneutrons <436979+metaneutrons@users.noreply.github.com>`, ohne
  KI-Zuschreibung; `tools/check-commit-hygiene.sh` lässt sonst die CI
  scheitern.
- ROMs, Workbench-Dateien, PFS3, HDFs und Buildartefakte nie committen.
- GitHub löscht den Kopfzweig eines PRs beim Merge, und gemergt wird nur
  per Squash. Zitieren Berichte Einzelcommits eines Zweigs, vor dem Merge
  ein Tag `archive/<zweig>` auf sein Ende setzen und pushen.
- Shell: `cp`, `mv` und `rm` sind auf `-i` gesetzt und zsh läuft mit
  `noclobber`. In Skripten `command cp`, `cp -f` oder Python verwenden und
  `>!` statt `>`.
- Vor einem Push mindestens `make -j8 host-test`, `make sanitize`,
  `make analyze`, `make quality-gates`, `make conformance-test` und
  `make ci-test`; bei Handler-Änderungen zusätzlich Kickstart und
  `make compatibility-test`.
