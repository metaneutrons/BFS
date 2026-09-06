# BFS: Uebergabe und verbleibende Arbeiten

Stand: 2026-09-07. Repository: `metaneutrons/BFS`.
Aktueller Main-Commit: `243339acd2f575857b33095fc1a138c079e9880f`
(`ci: harden release qualification and publication`).

Diese Datei ist die technische Uebergabe fuer die noch nicht abgeschlossene
Release-Abnahme. Sie ersetzt nicht den verbindlichen Plan in
`docs/plans/release-readiness.md`.

## 1. Auftrag und Abschlusskriterium

Ziel ist ein belastbarer Release fuer Amiga 68020, 68030, 68040, 68060 und
68080. AMMX bleibt ausdruecklich zurueckgestellt. Ein erfolgreicher Emulator-
oder Compilerlauf ist keine Hardwarequalifikation.

Fertig sind wir erst, wenn:

- alle funktionalen Aenderungen reguliert auf `main` gemergt sind;
- die finale CI einschliesslich Amiga-FULL46 gruen ist;
- der echte Release-Workflow reproduzierbar baut, signiert, attestiert und
  seine oeffentlichen Assets selbst wieder prueft;
- Release Please und der Versionsfluss tatsaechlich qualifiziert sind;
- README und Installationsanleitung praktisch gegen ein isoliertes Testimage
  geprueft sind;
- Repo-Standard-Skill und Repo-Doctor ohne offene anwendbare Findings laufen.

Fehlerfreiheit darf daraus nicht zugesagt werden. Diskformat, C99 und
MPL-2.0 bleiben unveraendert.

Referenzen:

- Plan: `docs/plans/release-readiness.md`
- Epic: [#6](https://github.com/metaneutrons/BFS/issues/6)
- M1 Repository/Build: [#7](https://github.com/metaneutrons/BFS/issues/7)
- M2 Filesystem: [#8](https://github.com/metaneutrons/BFS/issues/8)
- M3 Amiga: [#9](https://github.com/metaneutrons/BFS/issues/9)
- M4 Release: [#10](https://github.com/metaneutrons/BFS/issues/10)

## 2. Aktueller Implementierungsstand

### Gemergte Lieferungen

- PR #11 Repository-Konventionen: Squash `13b3939`
- PR #12 Filesystem-Core: Squash
  `cedda17f8c2fe4e6389fa9a520f1a89941695605`
- PR #13 Amiga-Delivery: Merge `b923962c4091ca05b951d08e985a21740d25390b`
- PR #14 Release- und CI-Hardening: Squash
  `243339acd2f575857b33095fc1a138c079e9880f`

PR #14 enthaelt die source-gebundene Build-Identitaet, reproduzierbare TAR- und
LHA-Archive, LHA-Header-/Tamper-Pruefungen, SBOM-Erzeugung und -Validierung,
getrennte Signing-/Attestation-/Publication-Gates sowie die korrigierte
Amiga-Testdiagnostik.

### CI- und Ruleset-Stand

PR #14 ist mit allen Pflichtpruefungen gemergt:

- Run [34063785775](https://github.com/metaneutrons/BFS/actions/runs/34063785775)
  ist vollstaendig gruen.
- Amiga-Job `101569469547`: `PROFILE full`, `# SUMMARY 46 46 0`,
  Laufzeit 37m47s.
- `CI Success` wurde als echter Required Check ausgegeben.
- GCC, Clang, ASan/UBSan, Static Analyzer, Coverage, Repository Quality,
  Commit Hygiene und Codacy sind gruen.
- Branch-Ruleset `22394435` verlangt `CI Success`, Squash-only, lineare
  Historie, aktuelle Pflichtchecks und aufgeloeste Diskussionen; es gibt keine
  Bypass-Akteure.
- Tag-Ruleset `22394437` bleibt unveraendert und immutable.

Die Amiga-CI verwendet fuer den Integrationsjob `BFS_TEST_TIMEOUT=2400` und
eine Jobgrenze von 60 Minuten. Das ist derzeit eine bewusste Qualifikations-
grenze, kein Nachweis optimaler Laufzeit.

## 3. Befund zu `fill_08`

`fill_08` war im fehlerhaften Lauf nicht der ausloesende Core-Fehler.

Der alte Lauf [34060249902](https://github.com/metaneutrons/BFS/actions/runs/34060249902)
brach nach 1800 Sekunden ab. Sein Log enthielt `PASS exnext_37`, aber weder
`fill_08` noch `diskfull_23` und keinen Summary-Eintrag. Die Ursache der
irrefuehrenden Zuordnung war:

- `test_exnext_complete` meldete den Test vor dem Cleanup als PASS;
- danach wurden noch 50 Dateien einzeln geloescht;
- `ACTION_DELETE_OBJECT` synchronisiert Standalone-Metadaten nach jeder
  Operation, wodurch dieser Cleanup-Pfad teuer ist;
- ein `bfsfsck` des abgebrochenen HDF meldete `Errors: 0 Warnings: 0 CLEAN`.

Im Testcode wurden deshalb zwei reale Testfehler behoben:

- `exnext_37` meldet PASS erst nach erfolgreichem Zaehlen und Cleanup;
- `fill_08` nimmt eine nach einem Short Write angelegte partielle Datei in
  das Cleanup auf und meldet Cleanup-Fehler separat.

Die gezielte lokale Pruefung von `fill_08` ist gruen. Der aktuelle GitHub-Lauf
fuehrte danach die komplette Reihenfolge aus und meldete:

- `PASS exnext_37`
- `PASS fill_08`
- `PASS diskfull_23`
- `# SUMMARY 46 46 0`

Getestete Identitaeten in Run 34063785775:

| Bestandteil | SHA-256 |
| --- | --- |
| Handler | `4abd41539d59ce1b11c20bd590abcb31ca82f1ceb5a164b5f7788b23a760a394` |
| `bfs-test` | `a6fdf7d697c426fc31c3cc5af40ee9a9078020be67125b3c096ea69dbe431982` |
| AROS ROM | `eef8edc2bdede6d9e7d3ab57cc6a02d4c65c190666a496e930e3d9c447bdac59` |
| AROS EXT | `a3520cb8482b5475611386cf3e68f84f66534752a3c78e5e88d39c5dee89db54` |

Der Handler-Hash ist gegenueber PR13 unveraendert; nur das Testbinary enthaelt
die neue, ehrliche Cleanup-Diagnostik. Ein Core-Defekt in `fill_08` ist damit
nicht belegt. Die lange Laufzeit bleibt ein beobachtetes Performance-Risiko und
darf nicht durch weitere blinde Timeout-Erhoehungen verdeckt werden.

## 4. Noch offene Arbeiten

### A. Handoff und Dokumentation

1. Diesen Handoff als separaten Dokumentations-PR gegen den aktuellen Main
   mergen. Veraltete Aussagen ueber offene PR13-, Required-Check- und FULL46-
   Zustaende duerfen nicht wieder eingefuehrt werden.
2. Repo-Standard-Skill erneut anwenden:
   `/Users/fabian/.codex/skills/repo-standard/SKILL.md`.
   Das Profil ist hardened. C-Kriterien anwenden; Rust-/TypeScript-only-
   Kriterien nicht kuenstlich erzwingen.
3. README-Installation korrigieren und praktisch an einem isolierten
   Emulator-Testimage ausfuehren. Mountlist, Geometrie, `bfsformat`,
   Handler-Packets und CPU-Dateiauswahl gemeinsam pruefen.
4. Dokumentieren: Default 020, 030/040/060/080, 080 ohne AMMX,
   Checksummen, SBOM-/Signatur-/Attestierungspruefung, Runtime-Lizenzen,
   bekannte Grenzen und tatsaechlich getestete Plattformen.
5. Keine Hardware- oder Apollo-Qualifikation behaupten, solange keine
   entsprechende reale Abnahme vorliegt.

### B. Release-Pfad end-to-end qualifizieren

1. Lokalen finalen Build auf dem aktuellen Main ausfuehren. Build-Identitaet,
   Link-Maps, Runtime-Inventar und alle Binary-Hashes muessen zusammenpassen.
2. Stale-/Dirty-Source-Gegenproben, echte TAR-/LHA-Archive, beide SBOMs,
   SPDX-Validierung und gehashte Validator-Abhaengigkeiten erneut pruefen.
3. Den exakten GitHub-Preflight mit den vorgesehenen minimalen App-Rechten
   testen. `GITHUB_TOKEN`-Verhalten im echten Release-Job ist noch offen.
4. Fuer die Qualifikation einen neuen, niemals wiederverwendeten Testtag
   verwenden, zum Beispiel `v<core>-qualification.1`. Tags nie verschieben
   oder loeschen; bei Fehlern neuen Tag verwenden.
5. Den manuellen Release-Workflow nur nach lokal gruenen Positiv-/Negativ-
   proben dispatchen. Zu pruefen sind:
   - alle fuenf CPU-Handler und die 68020-Tools;
   - reproduzierbare Archive und Clean-Room-FULL46 mit dem verpackten
     `-Os`-Handler, ohne Build-Toolchain;
   - korrumpierter Handler wird mit Exitcode und erwartetem Grund abgewiesen;
   - beide SBOMs, Signaturen, Attestierungen und `SHA256SUMS`;
   - exakt sieben Candidate-Assets und sechs Checksum-Eintraege;
   - echte kryptographische Tamper-Proben;
   - Read-only-Publication-Preflight, Draft, Upload, Download und oeffentliche
     Readback-Pruefung;
   - Prerelease ohne falsche `latest`-Promotion.
6. API-Fehler strikt behandeln: Nur ein echtes 404 darf "nicht vorhanden"
   bedeuten. 401/403/429/5xx und Netzfehler muessen abbrechen.
7. OIDC-Signaturen, GitHub-Attestierungen, Upload-/Download-Bytes und Asset-
   Digest gegen den exakten Kandidaten vergleichen. Lokale Archive beweisen
   keinen veroeffentlichten Release.

### C. Release Please

PR #4 (`chore(main): release bfs 0.1.1`) ist ein alter Release-Please-Stand
und darf nicht direkt gemergt werden. Nach der technischen Release-
Qualifikation:

1. Release-Please-Workflow und Manifest auf dem aktuellen Main pruefen.
2. Den Versions-PR neu erzeugen oder aktualisieren lassen.
3. Diff, Version, Changelog, Berechtigungen und CI erneut reviewen.
4. Den Versions-PR reguliert mergen; Produktions-Tags ausschliesslich ueber
   den qualifizierten Release-Please-Weg erzeugen.
5. Den Weg vom Versions-PR ueber App-Token, Tag-/Draft-Erzeugung,
   Release-Dispatch und Promotion end-to-end belegen.

### D. Issue-Evidenz

Issue #10 und der verlinkte Kommentar muessen erst nach der echten Release-
Qualifikation aktualisiert werden. Dort gehoeren dann hinein:

- Main-Commit und Release-Please-PR;
- Testtag und Release-Run-ID;
- Compiler-, ROM-, Handler-, Tool-, Asset- und SBOM-Hashes;
- alle oeffentlichen Assetnamen und Readback-Ergebnisse;
- ehrliche Grenzen: keine Hardwarequalifikation und AMMX nicht enthalten.

Issues erst schliessen, wenn die jeweilige Plan-Abnahme einzeln belegt ist.

### E. Schlussabnahme

1. `templates/check-repo-standard.sh metaneutrons/BFS --publishes` aus dem
   Repo-Standard-Skill ausfuehren, danach den lokalen Survey aus
   `references/doctor.md`.
2. Branch-/Tag-Regeln, Required Checks, geschuetzte Environments, minimale
   Tokenrechte, gepinnte Actions, Dependabot, Hooks und Attribution-Guard
   nochmals live verifizieren.
3. Vollstaendigen Git-Status und Tracked-Asset-Audit pruefen. Keine ROMs,
   HDFs, Fremdbinaries, Secrets oder lokale Buildartefakte committen.
4. Release-Download- und Verifikationsanleitung gegen genau die oeffentlichen
   Bytes testen.
5. Erst wenn alles andere erledigt ist, Fabian an GitHub Support erinnern:
   Die bereinigten beschreibbaren Refs sind sauber, aber die serverseitig
   versteckten PR-Refs #1/#2/#3/#5 koennen alte Fremdbinaries weiterhin
   erreichbar halten. Das ist separat serverseitig zu bereinigen; keine
   weiteren lokalen Filter-Laeufe oder Force-Pushes dafuer ausfuehren.

## 5. Arbeitsregeln fuer die Wiederaufnahme

- Immer vom aktuellen `origin/main` abzweigen; keine alte ungesquashte
  Sicherungshistorie pauschal mergen.
- Funktional gruppierte Conventional-Commits verwenden; niemals
  `Co-Authored-By: Codex` oder aehnliche Trailer.
- Fremdbinaries ausschliesslich in CI herunterladen und per Hash/Identitaet
  pruefen; nie ins Repository aufnehmen.
- Keine bestehende Benutzer-Aenderung zuruecksetzen.
- Keine Tags verschieben oder loeschen.
- Keine Secrets in Dateien, Issues, Logs oder Ausgaben aufnehmen.
- Keine Hardwarebehauptungen aus Emulator- oder Compiler-Evidenz ableiten.

## 6. Worktrees und Evidenz

Aktuell relevante Worktrees:

| Pfad | Zweck |
| --- | --- |
| `/tmp/bfs-hardening.QW2jl7/m4-delivery` | alter, bereits gemergter M4-Branch |
| `/tmp/bfs-hardening.QW2jl7/handoff` | aktueller Branch `docs/update-handoff` |

`/tmp` ist keine dauerhafte Evidenzablage. Wichtige Ergebnisse muessen mit
Commit-, Run- und Asset-Identitaet in den passenden Issues dokumentiert werden.
Vor jedem weiteren Test lokale Worktrees und laufende Prozesse pruefen.

Erfolgreicher Amiga-Vollnachweis:

- Run [34063785775](https://github.com/metaneutrons/BFS/actions/runs/34063785775)
- Job `101569469547`
- `PROFILE full`
- `# SUMMARY 46 46 0`
- Handler `4abd41539d59ce1b11c20bd590abcb31ca82f1ceb5a164b5f7788b23a760a394`
- `bfs-test` `a6fdf7d697c426fc31c3cc5af40ee9a9078020be67125b3c096ea69dbe431982`

Die lokale Build-Identitaet muss vor jeder neuen Packaging- oder Release-
Pruefung neu erzeugt werden. Keine alten lokalen Archive als aktuelle
Release-Evidenz behandeln.
