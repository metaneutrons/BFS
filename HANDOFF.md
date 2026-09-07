# BFS: Uebergabe und Abschlussstand

Stand: 2026-09-08. Repository: `metaneutrons/BFS`.

## 1. Ergebnis

Die Release-Abnahme M4 ist abgeschlossen. `main` steht auf
`f2deb47b2a689bd4e8dbcef36f3de449a5265095` (`chore(main): release 0.1.1`),
und der stabile Release `v0.1.1` ist oeffentlich als `latest` publiziert.

Qualifiziert und gebaut sind Amiga-Handler fuer 68020, 68030, 68040, 68060
und Apollo 68080. Der 68080-Build verwendet kein AMMX. Es liegt keine
physische Apollo-Hardwarequalifikation vor.

Das Abschlusskriterium war:

- funktionale Aenderungen reviewt, committed, gepusht und gemergt;
- GCC, Clang, ASan/UBSan, Static Analyzer und Coverage bestanden;
- CI inklusive `CI Success` und Amiga-FULL46 bestanden;
- Release Please und der echte Release-Workflow end-to-end bestanden;
- reproduzierbare Archive, SBOMs, Signaturen, Attestierungen und
  oeffentliche Byte-Readbacks verifiziert;
- Repo-Standard-Doctor ohne offene Findings bestanden.

Das ist eine belegte Release-Reife, keine Zusage allgemeiner Fehlerfreiheit.

## 2. Gemergte Lieferungen

- PR #11 Repository-Konventionen: `13b3939`
- PR #12 Filesystem-Core und Recovery: `cedda17f8c2fe4e6389fa9a520f1a89941695605`
- PR #13 Amiga-Delivery: `b923962c4091ca05b951d08e985a21740d25390b`
- PR #14 Release- und CI-Hardening: `243339acd2f575857b33095fc1a138c079e9880f`
- PR #21 dynamische Allocator-Reserve: `b88c63d`
- PR #22 Release-Please-Tag-/Workflow-Abgleich: `134aff5`
- PR #23 Draft-Release-Fallback ueber die paginierte GitHub-API:
  `23dd64e`
- PR #4 Release Please `v0.1.1`: `f2deb47`

Die Fremdbinaries sind nicht im Repository. Toolchains, AROS-ROMs, LHA und
Cosign werden in den Workflows geladen und dort geprueft.

## 3. `fill_08` und `many_03`

`fill_08` war ein realer Fehler in der Testdiagnostik, nicht der ausloesende
Core-Fehler. Der Test nahm nach einem Short Write die partielle Datei nicht
immer in das Cleanup auf. Ausserdem meldete `exnext_37` den Erfolg vor dem
Cleanup. Beides ist korrigiert.

Der eigentliche Laufzeitfehler lag bei `many_03`: Die feste Reserve von 96
Bloecken bewegte bei kleinen Transaktionen unnoetig viele Bloecke durch den
Free-Space-Baum. Die Reserve wird jetzt aus der Baumhoehe abgeleitet und
begrenzt. Danach bestand `many_03` lokal und in GitHub; der vollstaendige
Amiga-Lauf meldete `# SUMMARY 46 46 0`.

## 4. Test- und Ruleset-Evidenz

Lokale Nachweise auf dem finalen Code:

- `make host-test HOST_CC=gcc`: 31/31 Host-Binaries bestanden
- `make host-test HOST_CC=clang`: 31/31 Host-Binaries bestanden
- `make sanitize HOST_CC=clang`: alle Tests bestanden
- `make analyze`: erfolgreich
- `make coverage`: Core-Zeilen 88.4 % (`3997/4522`)
- `make amiga-test`: erfolgreich
- `BFS_TEST_TIMEOUT=1200 emulator-test/ci-test.sh`: `46/46`
- Repository-Asset-Audit: keine getrackten Binaries

GitHub-Nachweise:

- PR #23 CI [34163038783](https://github.com/metaneutrons/BFS/actions/runs/34163038783):
  alle Checks gruen, einschliesslich Amiga und `CI Success`
- Release-Please [34164653505](https://github.com/metaneutrons/BFS/actions/runs/34164653505):
  App-Token-Preflight, PR-Erzeugung und Dispatch bestanden
- finaler Main-CI [34164653510](https://github.com/metaneutrons/BFS/actions/runs/34164653510):
  alle Checks gruen, einschliesslich Amiga und `CI Success`
- Branch-Ruleset `22394435`: `CI Success`, Squash-only, lineare Historie,
  aktuelle Pflichtchecks und aufgeloeste Diskussionen
- Tag-Ruleset `22394437`: immutable Tags

Repo-Standard-Doctor:

- `check-repo-standard.sh metaneutrons/BFS --publishes`: 19 bestanden,
  0 fehlgeschlagen, 2 Hinweise
- Fixture-Test: 16 bestanden, 0 fehlgeschlagen
- Hinweise: provider-spezifische Secret-Patterns sind fuer das Benutzerkonto
  nicht abrufbar; die `release`-Umgebung hat bewusst kein statisches Secret.
  Beide Hinweise sind account- bzw. OIDC-seitig und keine offenen Codebefunde.

## 5. Release-Evidenz

### Qualification

Der unveraenderliche Tag `v0.1.0-qualification.3` zeigt auf Commit
`23dd64ef1027c7037ecbca17502c4c99a8807060` und wurde mit
[Run 34163917438](https://github.com/metaneutrons/BFS/actions/runs/34163917438)
qualifiziert. Der Run bestand mit reproduzierbarem Build, Clean-Room-Smoke,
SBOMs, keyless Sigstore-Signaturen, GitHub-Attestierungen, Tamper-Rejection,
Asset-Upload und oeffentlichem Readback.

Die sechs Eintraege der Kandidaten-`SHA256SUMS` sind im oeffentlichen
[Qualification-Release](https://github.com/metaneutrons/BFS/releases/tag/v0.1.0-qualification.3)
verifiziert. Die zentralen Archive haben folgende Digests:

| Asset | SHA-256 |
| --- | --- |
| `bfs-v0.1.0-qualification.3-amiga.tar.gz` | `435e15e34359c12121b782acdfc41828f2312b9762ab52d8f412f1a2dfd09e25` |
| `bfs-v0.1.0-qualification.3-amiga.lha` | `e6f820943d8ce2f0de0f90bdc915c190d8612d56d4c024b435fb15f63fcfa439` |
| `SHA256SUMS` | `83e0bca3f482783e8eee24c8eaeb090c904707963369cfffe8f37509742001cf` |

Der vorherige Tag `v0.1.0-qualification.2` wurde wegen des API-Draft-Fehlers
nicht wiederverwendet. Tags wurden nicht verschoben oder geloescht.

### Stable

Der unveraenderliche Tag `v0.1.1` zeigt auf `f2deb47b2a689bd4e8dbcef36f3de449a5265095`.
Der echte Stable-Workflow
[34164683697](https://github.com/metaneutrons/BFS/actions/runs/34164683697)
bestand vollstaendig. Die Release-Promotion lief erst nach allen
Verifikationsstufen. Der oeffentliche Release ist
[v0.1.1](https://github.com/metaneutrons/BFS/releases/tag/v0.1.1), nicht Draft,
nicht Prerelease und `latest`.

Alle sieben Stable-Assets wurden heruntergeladen und gegen `SHA256SUMS`
geprueft:

| Asset | SHA-256 |
| --- | --- |
| `bfs-v0.1.1-amiga.tar.gz` | `22be69b83988040e723242eda7bcaaba037008a4391d3609d93f851a76b18f5e` |
| `bfs-v0.1.1-amiga.lha` | `33c1f9394a3913b2e4d897d94a56aa573d4f7e92c3de134fc8e54afc2953c0eb` |
| `bfs-v0.1.1-amiga.tar.gz.spdx.json` | `13ed599790f89d216d1d48fe9243313a2627ef60467a430b052e22dac5d8ec4b` |
| `bfs-v0.1.1-amiga.lha.spdx.json` | `dcf6271b4a10135a344435331a0e2cf7edd1251d232fb3ab549e13170fb3f989` |
| `bfs-v0.1.1-amiga.tar.gz.sigstore.json` | `cfc97b245732de4e3f6cf1720dbf626f6a5b6a1e5e85edb48317185cbcf59f7f` |
| `bfs-v0.1.1-amiga.lha.sigstore.json` | `6081ca406309634ea17f97978e2c68b71a84b84ab775feb63407183f278eb548` |
| `SHA256SUMS` | `afb7250081dc2bd324c0118923713a249edf8801aaceb0f1c9120f114c6a109f` |

## 6. Dokumentation und Grenzen

README und Release-Readiness-Plan dokumentieren:

- Default-Handler 68020 sowie die Varianten 68030, 68040, 68060 und 68080;
- 68080 ohne AMMX und ohne daraus abgeleitete Hardwarebehauptung;
- `make release`, `make emulator-test` und die Amiga-Mount-/Format-Schritte;
- reproduzierbare Archive, Checksummen, SBOM-/Sigstore-Pruefung und Runtime-
  Lizenzen;
- die Grenze zwischen Compiler-/Emulatornachweis und echter Hardwareabnahme.

Es gibt keine behauptete physische Apollo-Qualifikation und keine AMMX-
Optimierung. Dependabot-PRs #15 bis #19 sind normale Wartungsarbeiten und
nicht Teil dieser abgeschlossenen Release-Abnahme; bei ihrer Annahme ist die
volle CI erneut zu bewerten.

Die bereinigten beschreibbaren Refs enthalten keine Fremdbinaries. GitHub kann
serverseitig versteckte alte PR-Refs weiterhin aufbewahren. Das ist eine
separate Support-Angelegenheit; dafuer keine weiteren lokalen Filter-Laeufe
oder Force-Pushes ausfuehren.

## 7. Wiederaufnahme-Regeln

- Keine Tags verschieben, loeschen oder wiederverwenden.
- Fremdbinaries ausschliesslich in CI laden und dort per Hash/Identitaet
  pruefen.
- Keine Hardwarequalifikation aus Emulator- oder Compiler-Evidenz ableiten.
- Neue funktionale Arbeit von aktuellem `origin/main` abzweigen und in
  funktional gruppierten Conventional-Commits liefern.
- Vor jeder neuen Release-Aussage alle betroffenen CI-, Asset- und Readback-
  Nachweise mit neuer Identitaet wiederholen.
- Keine Secrets, ROMs, HDFs oder Buildartefakte committen.
