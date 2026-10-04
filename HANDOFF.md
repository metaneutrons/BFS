# BFS: Übergabe für die Weiterarbeit

Stand: 2026-10-04, Zweig `format/v3-inline-extents`. Die Arbeit dieser
Runde liegt in zehn Commits ab `4ab8756`, lokal und nicht gepusht (siehe
Abschnitt 3). Die vorige Übergabe zum Release v0.1.1 liegt unter
`docs/qualification/release-v0.1.1-handoff-2026-09-08.md`.

## 1. Sitzungsstart

```sh
cd ~/Source/Amiga/BFS
git status --short                  # lokale Änderungen zuerst sichern
git fetch origin
git switch format/v3-inline-extents
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

## 3. Zweige und offene Commits

- PR #82 `perf/txn-owned-nodes` nach `main` ist offen.
- `perf/write-path-stages` baut darauf auf.
- `format/v3-inline-extents` baut darauf auf; ohne PR und damit ohne CI-Lauf.
- Die Arbeit dieser Runde liegt in zehn Commits auf
  `format/v3-inline-extents`, lokal und nicht gepusht. Jeder Commit besteht
  für sich `make host-test`; die Commits mit Pufferübernahme und Änderungen
  an Ort und Stelle zusätzlich `make sanitize`.
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
Commits seit `main`.

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

1. Push der Commits dieser Runde (Abschnitt 3) und PR-Strategie für die
   Kette #82, `perf/write-path-stages`, v3.
2. Ausreißer beim Anhängen: einzelne Läufe brauchen fast doppelt so lange.
   Ungeklärt, ob Host-Störung oder ein Timer-Commit innerhalb der Phase.
3. ExAll liegt bei rund 3,6×. Etwa 44 % der Host-Arbeit der Auflistung ist
   das Inode-Lesen je Eintrag; eine Sortierung hilft beim einstufigen
   Inode-Baum des Benchmarks nicht.
4. Auf dem Copy-on-Write-Pfad werden Elternknoten auch dann neu geschrieben,
   wenn das Blatt an Ort und Stelle bleibt; relevant erst bei höheren Bäumen.
5. Feature-Masken im Superblock: Entscheidung offen.
6. Kickstart-Job in der CI: ob Secret `CI_TEST_ENV` und das verschlüsselte
   Archiv noch existieren, ist ungeklärt.
7. Löschen sehr großer Dateien braucht pro Block einen Eintrag in der
   Freigabe-Warteschlange, die auf dem Amiga per malloc wächst.
8. Nicht getestet: echte Hardware, MorphOS und OS4 nativ, andere
   Kickstart-Versionen, 68000.

## 8. Arbeitsregeln

- Commits nach Conventional Commits, auf Englisch, als
  `metaneutrons <436979+metaneutrons@users.noreply.github.com>`, ohne
  KI-Zuschreibung; `tools/check-commit-hygiene.sh` lässt sonst die CI
  scheitern.
- ROMs, Workbench-Dateien, PFS3, HDFs und Buildartefakte nie committen.
- Shell: `cp`, `mv` und `rm` sind auf `-i` gesetzt und zsh läuft mit
  `noclobber`. In Skripten `command cp`, `cp -f` oder Python verwenden und
  `>!` statt `>`.
- Vor einem Push mindestens `make -j8 host-test`, `make sanitize`,
  `make analyze`, `make quality-gates`, `make conformance-test` und
  `make ci-test`; bei Handler-Änderungen zusätzlich Kickstart und
  `make compatibility-test`.
