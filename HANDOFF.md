# BFS: Übergabe für die Weiterarbeit auf dem Mac

Stand: 2026-10-03, Zweig `format/v3-inline-extents`. Die vorige Übergabe zum
Release v0.1.1 liegt unter
`docs/qualification/release-v0.1.1-handoff-2026-09-08.md`.

## 1. Sitzungsstart

```sh
cd ~/Source/BFS
git status --short                  # lokale Änderungen zuerst sichern
git fetch origin
git switch format/v3-inline-extents
git pull --ff-only
make -j8 host-test                  # Host-Tests, gcc
make amiga amiga-test               # Handler und AROS-Testprogramm
make ci-test                        # 50 AROS-Tests im Emulator
emulator-test/ci-local.sh 1800      # dieselben Tests unter Kickstart 3.2
```

Die lizenzierten Dateien liegen nicht im Repository und werden von Git
ignoriert. Für Kickstart-Läufe und Messungen müssen vorhanden sein:
`emulator-test/.assets/A1200.47.102.rom`, `emulator-test/.assets/{C,L,Libs}`
mit den Workbench-3.2-Befehlen und `emulator-test/.cache/pfs3aio`.

Unter Kickstart beendet sich der Emulator nach den Tests nicht selbst;
`ci-local.sh` wertet aus, sobald der Timeout abläuft oder du das
Emulatorfenster schließt. Das Ergebnis steht vorher schon in
`emulator-test/.wb32/result.txt`.

Der Reset-Test (`make reset-test`) braucht Xvfb und xdotool und läuft so nur
unter Linux.

## 2. Ziel und Stand

Ziel: BFS in jedem geprüften AmigaDOS-Workload höchstens fünfmal so langsam
wie PFS3, ohne Abstriche bei Integrität, Snapshots und Recovery.

Letzte Messung (je 6 Läufe, FS-UAE 3.1.66 in der Cloud, A1200/68040,
Kickstart 47.102): kein Workload und kein Einzellauf über 5×.

| Workload | Faktor zu PFS3 | Spanne |
| --- | ---: | ---: |
| Create 40 | 2,49× | 2,33–2,67× |
| Lookup 400 | 1,09× | 1,01–1,15× |
| Read 40 | 1,29× | 1,15–1,54× |
| ExNext-Auflistung 400 | 2,72× | 2,16–3,13× |
| ExAll-Auflistung 400 | 3,93× | 3,76–4,18× |
| Write 8 MiB | 1,65× | 1,53–1,70× |
| Read 8 MiB | 1,00× | 0,96–1,05× |
| Delete 40 | 3,17× | 2,93–3,50× |

Einzelheiten, Einzelwerte und Prüfsummen:
`docs/qualification/bfs-directory-listing-performance-2026-10-03.md`.
Die absoluten Zeiten sind mit früheren Mac-Messungen (FS-UAE 3.2.35) nicht
vergleichbar, nur die Faktoren. Eine Bestätigungsreihe auf dem Mac steht aus.

## 3. Zweige

- PR #82 `perf/txn-owned-nodes` nach `main` ist offen (Umschreiben
  transaktionseigener Knoten an Ort und Stelle).
- `perf/write-path-stages` baut darauf auf (verzögerter Gruppen-Commit,
  verzögertes Schreiben eigener Knoten, Datenpfad).
- `format/v3-inline-extents` baut darauf auf, 37 Commits über
  `perf/write-path-stages` und 57 über `main`. Für diesen Zweig gibt es
  keinen PR und damit keinen CI-Lauf; die lokalen Gates waren alle grün.

Nichts mergen ohne Fabians Freigabe. PRs nur auf ausdrücklichen Wunsch.

## 4. Inhalt des v3-Zweigs

Format v3 (`e68aeb2`, Plan `docs/plans/bfs-format-v3-v1.md`): Inline-Extent
im Inode, Kommentar-Flag, v2 wird abgelehnt, `bfs format` ersetzt Medien mit
ausschließlich älteren Formaten.

Korrekturen:

- Referenzzähler mit Snapshots sind in jedem veröffentlichten Zustand exakt
  (`314fcf4`).
- Nach der Reset-Warnung wird jede Änderung vor der Antwort committet
  (`5c4eced`).
- ExamineFH liefert den Namen (`b15bca2`); ExNext und ExAll liefern ".."
  nicht mehr, sind linear und überspringen beim Löschen nichts (`27edbbe`).
- FUSE-Test lehnt v2 und v4 ab (`e46289e`); der CI-Schritt mit dem
  v0.1.3-Handler prüft jetzt dessen Ablehnung von v3-Medien (`9c40470`).

Leistung: Zusammenführen anschließender Extents (`b1ab322`), Hash-Index im
Cache (`04f9e06`), Baum-Scan über Cache-Ansichten mit Änderungszähler
(`b628193`), Blatt-Cursor für Auflistungen (`00187f0`), dichte
ExAll-Einträge (`2fcf1b0`), Blatt-Hinweis in der Suche (`5f7f552`), direkter
Vergleich von u32-Schlüsseln (`2652a56`), eine Namenskopie je ExAll-Eintrag
(`cd16c4a`).

Werkzeuge: Auflistungsphasen im Vergleichsbenchmark, Schema 2 (`872a332`),
Kickstart-Optionen für Reset- und Compatibility-Test (`48ca58a`),
Messreihen (`220fce1`).

## 5. Invarianten für neue Arbeit

- `bfs_btree_t.generation` ändert sich bei jedem Knotenschreiben und jeder
  Wurzel- oder Höhenänderung. Scan-Cursor und Such-Hinweis verlassen sich
  darauf. Wer Knoten oder Wurzel eines Baums auf einem neuen Weg ändert, muss
  den Zähler erhöhen.
- Ein Scan-Callback darf den Baum ändern. Der Scan sucht dann hinter dem
  zuletzt gelieferten Schlüssel neu und liefert keine danach gelöschten
  Schlüssel.
- Ein Cursor gilt nur bei gleichem Baum, gleicher Wurzel und gleichem Zähler
  und nur für Startschlüssel zwischen erstem und letztem Schlüssel seiner
  Blattkopie. Er darf das Mounten seines Baums nicht überleben.
- Der Such-Hinweis nutzt nur ein Blatt, das validiert im Cache liegt.
- Bäume mit u32-Schlüsseln verwenden `bfs_btree_key_compare_be32`, sonst
  entfällt die direkte Suche.
- ExAll-Einträge enthalten nur die Felder bis zum angefragten Typ; ein
  Eintrag, der nicht passt, kommt im nächsten Aufruf.

## 6. Messen

```sh
make amiga amiga-fs-compare-bench
emulator-test/bench-series.sh NAME 6 compare alt=PFAD/bfshandler neu=build/amiga/bfshandler
tools/bench-summary.py NAME-alt NAME-neu
```

Einen Vergleichs-Handler aus einem älteren Commit baut man in einem
`git worktree` mit `make amiga`. Die Reihe wechselt die Reihenfolge von BFS
und PFS3 und legt jeden Lauf unter `build/benchmark/` ab.
`make core-workload-profile` liefert deterministische Instruktionszahlen pro
Phase auf dem Host.

## 7. Offene Punkte und Entscheidungen

1. Bestätigungsmessung auf dem Mac mit `bench-series.sh`.
2. PR-Strategie für die Kette #82, `perf/write-path-stages`, v3.
3. Einzelblöcke werden absteigend vergeben; Dateien, die in 4-KiB-Schritten
   wachsen, liegen dadurch rückwärts und bekommen einen Extent je Block. Eine
   Vergabe mit Zielblock hinter dem letzten Dateiblock ist vorgeschlagen,
   nicht umgesetzt.
4. Feature-Masken im Superblock: Entscheidung offen.
5. Kickstart-Job in der CI: Bis zum 6. September lud die CI über das Secret
   `CI_TEST_ENV` ein verschlüsseltes Kickstart-Archiv; ob Secret und Archiv
   noch existieren, ist ungeklärt.
6. Löschen sehr großer Dateien braucht pro Block einen Eintrag in der
   Freigabe-Warteschlange, die auf dem Amiga per malloc wächst.
7. Nicht getestet: echte Hardware, MorphOS und OS4 nativ, andere
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
