# BFS: Uebergabe und verbleibende Arbeiten

Stand: 2026-09-06. Letzter gesicherter Implementierungscommit: `f1bca83`.
Branch: `chore/repository-hardening`, vollstaendig gepusht. Der Arbeitsbaum
war vor Erstellung dieser Datei sauber. Diese Datei ist die neue lokale Aenderung.

## 1. Auftrag und Abschlusskriterium

Fabian hat den Audit, die Reparaturen und den Release-Readiness-Plan freigegeben.
Die Arbeit wurde wegen des nahen Rate-Limits pausiert, nicht abgeschlossen.
Dieser Handoff dokumentiert die Wiederaufnahme; er ersetzt weder Abnahme noch Plan.

- Verbindlicher Umfang und Abnahme: [Release-Readiness-Plan](docs/plans/release-readiness.md).
- Epic: [#6](https://github.com/metaneutrons/BFS/issues/6).
- M1 Repository/Build: [#7](https://github.com/metaneutrons/BFS/issues/7).
- M2 Filesystem: [#8](https://github.com/metaneutrons/BFS/issues/8).
- M3 Amiga: [#9](https://github.com/metaneutrons/BFS/issues/9).
- M4 Release: [#10](https://github.com/metaneutrons/BFS/issues/10).

Fertig sind wir erst, wenn alle anwendbaren Plan-Kriterien nachweislich erfuellt
sind: funktionale Lieferungen gemergt, finale Tests bestanden, echter geharteter
Release-Pfad verifiziert, Repo-Doctor ohne offene anwendbare Findings und
Installationsanleitung praktisch getestet. Keine Zusage von Fehlerfreiheit.

Ziele: 68020, 68030, 68040, 68060 und Apollo 68080. AMMX bleibt zurueckgestellt.
Physische Apollo-/Amiga-Hardware ist nicht qualifiziert; Emulator- und
Compiler-Erfolg sind kein Ersatz. MPL-2.0, C99 und das bestehende Diskformat
bleiben erhalten. Keine neue grosse Refactoring-Runde ohne konkreten Befund.

## 2. Wichtiges Update: Amiga-CI ist gruen

Der fruehere Checkpoint in Issue #10 meldete die Integration noch als laufend.
Bei Erstellung dieses Handoffs wurden API und Joblog erneut gelesen:

- [Run 34052408560](https://github.com/metaneutrons/BFS/actions/runs/34052408560)
  fuer PR #13 ist erfolgreich, einschliesslich `CI Success`.
- Amiga-Integration: 22m48s; Log meldet `PROFILE full`, `PASS path_45`,
  `SUMMARY 46 46 0` und `Amiga integration test passed (46 checks)`.
- GCC, Clang, ASan/UBSan, Clang Static Analyzer, Repository Quality,
  Commit Hygiene, Coverage und Codacy sind ebenfalls gruen.
- PR #13 ist trotzdem noch offen: Ruleset `22394435` verlangt weiterhin
  `host-tests`, einen vom neuen Workflow nicht mehr ausgegebenen Check-Namen.

Die unmittelbar naechste Aufgabe ist deshalb die gesicherte Uebernahme dieser
Evidenz und die Korrektur des Required-Check-Kontexts, nicht ein weiterer
identischer lokaler Langlauf.

## 3. Git- und Lieferstand

Repository: `metaneutrons/BFS`.
Aktueller lokaler Projektpfad: `/Volumes/Dev/Source/Amiga/BFS`.
Der alte Pfad `/Volumes/Dev/Source/BFS` existiert nicht mehr.

| Lieferung | Stand |
| --- | --- |
| PR #11 Repository-Konventionen | Gemergt; Squash `13b3939` |
| PR #12 Filesystem-Core | Gemergt; Squash `cedda17f8c2fe4e6389fa9a520f1a89941695605` |
| `main` | API-bestaetigt weiterhin `cedda17f8c2fe4e6389fa9a520f1a89941695605` |
| [PR #13](https://github.com/metaneutrons/BFS/pull/13) | Offen, alle Checks gruen; Head `3b4e54db64d1ba9a995198ab265241f90939d121` |
| `chore/repository-hardening` | Implementierungs-/Sicherungsbranch; lokal und remote `f1bca83` |
| [PR #4](https://github.com/metaneutrons/BFS/pull/4) | Alter Release-Please-PR `chore(main): release bfs 0.1.1`; noch NICHT mergen |

PR #13 heisst exakt:
`fix(amiga): qualify handler startup, DOS packets and reproducible builds`.

Noch auszuliefernde funktionale M4-Commits, in dieser Reihenfolge:

1. `9101c40`: `build: bind release archives and runtime SBOMs to source identity`
2. `f4e9f72`: `fix(release): use portable LHA directory headers and prove tamper rejection`
3. `f1bca83`: `ci: stage signed releases behind identity and publication gates`

Den Sicherungsbranch NICHT pauschal nach `main` mergen. Er enthaelt die
ungesquashte Audit-Historie; fruehere Lieferungen sind dort bereits als Squashes.
M4 von dem nach PR #13 aktualisierten `origin/main` abzweigen und nur die drei
genannten Commits cherry-picken. Vorher Diff und Abhaengigkeiten pruefen.

Lokale Worktrees, alle vor Wiederverwendung erneut auf Aenderungen pruefen:

| Pfad | Stand/Zweck |
| --- | --- |
| `/Volumes/Dev/Source/Amiga/BFS` | `chore/repository-hardening`, M4-Implementierung |
| `/private/tmp/bfs-hardening.QW2jl7/amiga-publish` | `delivery/amiga-qualification`, PR #13, `3b4e54d` |
| `/private/tmp/bfs-hardening.QW2jl7/verify` | Detached `3b4e54d`, lokale Amiga-Evidenz |
| `/private/tmp/bfs-hardening.QW2jl7/core-qualified` | Detached `54eaebc`, fruehere Core-Qualifikation |

`/tmp` ist nicht dauerhafte Evidenzablage. Wichtige Ergebnisse in Issues mit
Commit-/Run-Identitaet sichern; benoetigte CI-Artefakte rechtzeitig herunterladen.
Keine ROMs, Fremdbinaries, HDFs oder geheimen Werte committen.

## 4. Bereits belegte Ergebnisse

### Core und Repository

- 261 Host-Tests in 32 Suites; GCC/Clang und Sanitizer qualifiziert.
- Neue Linux-CI fand zusaetzliche Test-Fixture-Leaks, die auf macOS nicht
  sichtbar waren. Bereinigung ist in PR #13 enthalten (`909f446`, gespiegelt
  als `6e943a6`); Linux ARM64 und GitHub Linux x86 Sanitizer danach erfolgreich.
- Aktuelle GitHub-Coverage: 87,8% Zeilen (3583/4079), 96,6% Funktionen,
  63,1% Branches. Mindestgrenze bleibt 85% Zeilen.
- gcovr meldet fuenf auffaellig grosse CRC-Schleifenzaehler. Nicht als
  warnungsfreie Messung darstellen; `warn_once` ist keine stille Bereinigung.
- 38 lokale Quality-/Release-Gate-Tests bestanden; actionlint, shellcheck,
  Repository-Asset-Audit und Secret-Scan ebenfalls bestanden.
- Fremdbinaries werden fuer CI heruntergeladen und per Identitaet geprueft.
  Keine solchen Binaries im aktuell versionierten Baum.

### Amiga

- Fuenf CPU-Handler bauen; 68080 separat, ohne AMMX.
- PR #13 korrigiert u.a. StackSwap vor Eintritt in den C-Stackframe,
  DOS-Packet-ABI, gemeinsame Handles, Resize-Positionen, ExAll und Pfadauflosung.
- Letzte relevante Fehler: ACTION_SAME_LOCK erwartet auf Packet-Ebene Boolean,
  nicht die LOCK_*-Rueckgabecodes der DOS-Bibliothek; leere Restpfade muessen
  das aufgeloeste Verzeichnis statt pauschal das Ausgangs-Lock verwenden.
- Siehe `docs/amiga-packets.md`, `docs/static-review.md` und Issue #9.
- Lokaler QUICK46 erfolgreich. Der parallele lokale FULL46-Lauf wurde zur
  Pause absichtlich beendet; er ist weder Erfolgs- noch Fehlernachweis.
  Der spaeter erfolgreiche GitHub-FULL46 ersetzt diesen offenen Testnachweis.
- VBCC-Startup wurde assembliert, aber kein vollstaendiger VBCC-Build qualifiziert.
- Keine direkte DOS-Pfad-Geraetefehler-Injektion und keine Hardwarequalifikation
  behaupten, wo nur Core-Fault-Injection bzw. Emulator-Evidenz vorliegt.

Identitaeten des erfolgreichen GitHub-FULL46, aus dem Joblog:

| Bestandteil | SHA-256 |
| --- | --- |
| Getesteter Handler | `4abd41539d59ce1b11c20bd590abcb31ca82f1ceb5a164b5f7788b23a760a394` |
| Getestetes bfs-test | `2aa850c4f14518657a91efbe25033ad5e8e985b15efafe7adc668a9f3b0ee48a` |
| AROS ROM | `eef8edc2bdede6d9e7d3ab57cc6a02d4c65c190666a496e930e3d9c447bdac59` |
| AROS EXT | `a3520cb8482b5475611386cf3e68f84f66534752a3c78e5e88d39c5dee89db54` |

Gepinntes Compiler-Image:
`amigadev/m68k-amigaos-gcc@sha256:b18080e6ffca8f793e0f539536a9138e9d2a548ca1a301c7483f43ee15fedfed`.
Der Integrationslauf testet den Entwicklungs-Handler mit `-O2`; der Release-Smoke
muss zusaetzlich den wirklich verpackten `-Os`-Handler ausfuehren.

### Lokale Release-Vorpruefungen

- Quellstandgebundener Build aller Ziele erfolgreich; Link-Maps erfassen die
  tatsaechlich ausgewaehlten libgcc-/libnix-Objekte und deren Input-Hashes.
- TAR und LHA: zweimal bytegleich verpackt, exaktes Inventar von 17 Dateien,
  gleiche Nutzdaten in beiden Formaten, korrumpiertes gzip gezielt abgewiesen.
- LHA muss `cq2g2` verwenden: `g` setzt den Headerlevel zurueck. Ohne das
  abschliessende `2` waren Pfade fuer libarchive unlesbar.
- Homebrew `lha` kann Lhasa, also nur einen Decoder, liefern. Fuer Erstellung
  `tools/install-lha.sh` verwenden; gepinnter lokaler Archiver liegt unter
  `/tmp/bfs-hardening.QW2jl7/bin/lha`.
- Echte TAR-SBOM bestand den offiziellen SPDX-Validator 0.8.5; unaufgeloeste
  Referenz wurde in einer isolierten Gegenprobe abgewiesen. Dependencies gehasht.
- Live-App-Probe: genau BFS, getrennte minimale Release- und Dispatch-Grants;
  alle Prueftokens widerrufen. Aktuelle Installation-Tokens sind JWT-artig.
- `repository.permissions` ist fuer App-Tokens trotz erteilter Rechte false.
  GET der Git-receive-pack-Ankuendigung prueft Contents write ohne Mutation:
  passender Token HTTP200, read-only-Gegenprobe HTTP403.
- Diese lokalen Ergebnisse sind KEIN Beleg fuer einen veroeffentlichten Release,
  OIDC-Signaturen, GitHub-Attestierungen oder Promotion. Es wurde in M4 noch
  kein neuer Release und kein Test-/Produktionstag publiziert.

## 5. Offene Arbeiten in Ausfuehrungsreihenfolge

### A. Amiga-Lieferung abschliessen

1. Run34052408560 erneut lesen, PR-Head auf unveraendertes `3b4e54d` pruefen.
   Artefakt `amiga-integration-evidence` herunterladen; Ergebnisdatei,
   Completion-Marker, Inventar, Testprofil und Hashes mit dem Joblog abgleichen.
2. Issue #9 und PR #13 um FULL46-Erfolg samt Compiler-/ROM-/Binary-Identitaeten
   ergaenzen. Veraltete Pending-Aussagen im letzten Checkpoint richtigstellen.
3. Ruleset `22394435` live lesen und ausschliesslich den Required-Check-Kontext
   `host-tests` durch `CI Success` ersetzen. Dieser Kontext ist jetzt real
   ausgegeben: Job `101542026378`, success. Kein Admin-/App-Bypass hinzufuegen.
4. Alle anderen Schutzregeln erhalten: strict/up-to-date, PR, Squash-only,
   lineare Historie, aufgeloeste Diskussionen, kein Force-Push/Loeschen.
   Aenderung per API zuruecklesen, danach PR #13 regulaer squash-mergen.
5. Merge-Commit und Main-CI dokumentieren. M1/M2/M3-Kriterien in den Issues
   einzeln abgleichen; eine gruene PR ist nicht automatisch die Epic-Abnahme.

### B. M4 liefern und verbleibende technische Risiken pruefen

1. Neuen Lieferbranch/Worktree vom aktualisierten Main erzeugen; die drei
   M4-Commits aus Abschnitt3 uebernehmen. Keine alten Core-Aenderungen erneut
   einbringen. Handoff-Datei gegebenenfalls separat als Dokumentation uebernehmen.
2. Release-Skripte und Workflow-Verkettung reviewen, insbesondere:
   - `build_identity.py` bindet Build-Inputs, HEAD und Binary-Hashes. Nach
     jedem neuen Commit/cherry-pick neu bauen, sonst wird Packaging abgewiesen.
   - Noch pruefen: schmutzige Packaging-Skripte/README/Lizenzdateien koennen
     ausserhalb der engeren Build-Input-Liste liegen. Keine falsche Commit-
     Zuordnung fuer ausgelieferte Dateien zulassen.
   - Noch pruefen: Metadaten nennen feste CPU-/Optimierungsflags; frei
     ueberschriebene Make-/Compilerflags werden nicht dynamisch erfasst.
   - Noch pruefen: Runtime-Manifest und Map-/Input-Identitaeten bleiben zwischen
     Build und Packaging unveraendert; unbekannte Inputs muessen abbrechen.
   - Runtime-Provenienz ehrlich begrenzen: Image-/Dateihashes sind bekannt,
     exakte Upstream-Source-Commits aller Compiler-Runtimes nicht. Keine
     Versionen oder source commits erfinden; siehe `THIRD-PARTY.md`.
3. Konkrete noch offene Gate-/Live-Proben abschliessen:
   - Finaler sauberer Build, stale/dirty-source-Gegenprobe, beide echten Archive,
     Runtime-Inventar, beide SBOMs und gehashte Validator-Installation.
   - Den exakten aktuellen CLI-Preflight mit App-Token testen, nicht nur den
     isolierten HTTP-Endpunkt. GITHUB_TOKEN-Verhalten im Release-Job ist offen.
   - Release-App-Environment in echtem Workflow pruefen; keine Secrets blind
     ersetzen. Ein lokaler Keychain-Rohabruf war nicht direkt als PEM lesbar;
     vorhandene sichere PEM-Kopie funktionierte. Kein Secretproblem bewiesen.
   - Unit-Fixtures fuer Candidate/Signaturen sind Strukturtests, keine
     kryptographischen Positivproben.
   - Timeout/SIGTERM/Prozessgruppen-Cleanup des Emulatorrunners gezielt pruefen,
     soweit bestehende Tests abgetrennte Nachkommen noch nicht abdecken.
4. M4-PR oeffnen. Quality, Codacy und gesamte CI pruefen. Neue Python-/Shell-
   Findings beheben oder eng begruendet disponieren; keine globalen Suppressions.
   Neue CI prueft bereits echte Archive nach dem Fuenf-CPU-Build.
5. Finalen Kandidaten mit GCC/Clang, ASan/UBSan, Analyzer und >=85% Core-
   Zeilenabdeckung qualifizieren. Identische Core-Quellen koennen begruendet
   Evidenz behalten; neue Release-/Packaging-Aenderungen brauchen eigene Tests.

### C. Dokumentation und reale Installation

1. README-Installation korrigieren: Mountlist ohne Device/Geometrie ist nicht
   ausreichend; der bisherige `Format`-Befehl wurde nicht praktisch verifiziert.
   `bfsformat`, Handler-Packets und Mount-Konfiguration gemeinsam abgleichen.
2. Eine konkrete, sichere Anleitung fuer ein isoliertes Emulator-Testimage
   erstellen und deren Codebloecke unveraendert ausfuehren. Niemals echte
   Nutzerdatenpartitionen formatieren. Kein erfundener universeller Mountlist.
3. Dokumentieren: CPU-Dateiauswahl, Default=020, 080 ohne AMMX, Checksummen,
   Signatur-/Attestierungspruefung, Runtime-Lizenzen, bekannte Grenzen und
   tatsaechlich getestete Plattformen. Keine Hardware-Installation behaupten.
4. Nach echtem Publish Download-/Verifikationsanleitung gegen genau die
   oeffentlichen Release-Assets testen, nicht nur lokale Dateien.

### D. Echten Release-Pfad qualifizieren

1. Erst nach lokal gruenen Positiv-/Negativproben und geprueftem Workflow einen
   geplanten Testtag `v<manifest-core>-<suffix>` verwenden. Jeder Tag ist durch
   Ruleset `22394437` dauerhaft verbraucht; niemals verschieben oder loeschen.
2. Testtag muss Quell-HEAD und Core-Version aus `version.txt` sowie
   `.release-please-manifest.json` treffen. Er darf niemals promoted werden.
3. Gesamten Workflow ausfuehren und Step-Ergebnisse lesen:
   - Einmal bauen, Archive reproduzierbar erzeugen und zuruecklesen.
   - Clean-Room-FULL46 mit verpacktem Handler/Testbinary ohne Build-Toolchain.
   - Isoliert kaputten Handler abweisen, Exitcode UND erwarteten Grund pruefen.
   - Beide SPDX-SBOMs validieren, beide Payloads mit Cosign keyless signieren.
   - Payloads, SBOMs, Bundles und SHA256SUMS mit GitHub attestieren.
   - Exakt sieben Candidate-Assets und sechs Checksum-Eintraege pruefen.
   - Echte kryptographische Tamper-Proben. Aktuelle Regex fuer Cosign und
     GitHub-404 gegen beobachtete Fehler verifizieren; kein beliebiger Fehler
     darf als erfolgreiche Gegenprobe durchgehen.
   - Read-only-Publication-Preflight, leerer Draft, Upload, Download-Vergleich,
     Prerelease ohne latest, anonymer oeffentlicher Download und erneute Pruefung.
4. API-Verhalten fuer Draft-by-tag, Asset-State/Size/Digest, Rechte und
   Publikationsstatus ist noch end-to-end offen. Nur echtes404 bedeutet
   fehlender Draft; 401/403/429/5xx/Netzfehler muessen abbrechen.
5. Erst danach Release Please auf Main tatsaechlich ausfuehren. Bestehenden
   PR #4 aktualisieren lassen und Diff/Version/Changelog/CI erneut reviewen.
   Nicht einfach den alten Stand mergen. Produktions-Tags nur durch Release Please.
6. Den echten Weg vom Versions-PR-Merge ueber App-Token, Draft-/Tag-Erstellung
   und getrennten Workflow-Dispatch qualifizieren. Ein manueller Dispatch
   allein beweist diese Strecke nicht. Keine zusaetzlichen Tag-/Release-Trigger.
7. Stabile Promotion nur nach allen Gates; Assets unveraendert lassen,
   latest-Zuordnung und oeffentlich geladene Bytes anschliessend kontrollieren.
   Keine weiteren Paketkanaele sind fuer BFS beauftragt.
8. Tag, Source-Commit, Run-IDs, Assetnamen/-Hashes und reale Resultate in Issue
   #10 dokumentieren. Scheitert ein neuer Tag, erst Ursache reparieren; alte
   Tag-Identitaet und bereits ausgelieferte Bytes nicht umschreiben.

### E. Schlussabnahme und bewusst letzter Restpunkt

1. Repo-Standard-Skill erneut anwenden: Profil hardened; C-spezifisch anpassen,
   Rust-/TypeScript-only-Kriterien nicht kuenstlich erzwingen; MPL-2.0 erhalten.
   Lokaler Skill: `/Users/fabian/.claude/skills/repo-standard/SKILL.md`.
2. API-Doctor mit `templates/check-repo-standard.sh metaneutrons/BFS --publishes`
   aus dem Skill ausfuehren; zusaetzlich den lokalen Survey aus `references/doctor.md`.
   Templates/Scripts nicht aus dem Repo-CWD unter falschem Pfad starten.
3. Branch-/Tag-Regeln, tatsaechlich ausgegebene Pflichtchecks, geschuetzte
   Environments `release`/`release-please`, minimale Tokenrechte, gepinnte Actions,
   Dependency-Pflege, Hooks, Attribution-Guard und Fehlerweitergabe verifizieren.
   Keine offenen anwendbaren Findings mit einem gruenen Teilcheck ueberdecken.
4. Alle Lieferungen reguliert mergen und jeden Meilenstein gegen den Plan
   abnehmen. Issues erst bei vollstaendiger Evidenz schliessen. Bekannte
   Filesystem-Grenzen in `docs/failure-semantics.md` erhalten.
5. ERST WENN ALLES ANDERE ERLEDIGT IST: Fabian an GitHub Support erinnern.
   Nach filter-repo sind die beschreibbaren Refs bereinigt, aber GitHubs
   versteckte PR-Refs #1/#2/#3/#5 halten alte Fremdbinaries weiter erreichbar.
   Diese nicht durch erneute Force-Pushes oder weitere lokale Filterlaeufe
   vermeintlich reparieren. Serverseitige Bereinigung ist separat erforderlich.
   Fabian wollte diese Erinnerung ausdruecklich ans Ende stellen.

## 6. Wiederaufnahme und Belegablage

Zuerst diese Datei, den Plan und die aktuellen Issues lesen; dann Git-/API-Stand
neu pruefen. Keine alten Pending-Aussagen aus dem Checkpoint ungeprueft uebernehmen.
Keine laufenden lokalen Testprozesse aus diesem Handoff muessen fortgesetzt werden.
Nicht unbesehen andere Worktrees loeschen oder Benutzer-Aenderungen zuruecksetzen.

Wichtige lokale Evidenz unter `/tmp/bfs-hardening.QW2jl7/`:

| Datei/Verzeichnis | Inhalt |
| --- | --- |
| `ci-amiga-3b4e54d.log` | Bei Handoff-Erstellung gelesenes, erfolgreiches FULL46-CI-Joblog |
| `ci-coverage-3b4e54d.log` | GitHub-Coverage einschliesslich Warnungen |
| `linux-sanitize-fixed.log` | Zusaetzlicher Linux-ARM64-Sanitizerlauf |
| `amiga-samelock-quick.log` | Lokaler QUICK46-Erfolg |
| `amiga-samelock-five-cpu.log` | Lokaler Fuenf-CPU-Build |
| `amiga-samelock-full.log` | Absichtlich abgebrochener lokaler FULL46, kein Pass |
| `m4-final-gates.log` | 38 Quality-Tests, actionlint/shellcheck |
| `release-local-qualified/` | Lokale echte Archive; keine publizierten Release-Assets |
| `release-local-one/` | Lokaler TAR-SPDX-Positivnachweis |
| `m4-source-bound-build.log` | Frueher source-bound Build, vor den letzten Commits |

Die lokale Build-Identitaet kann nach `f4e9f72`/`f1bca83` nicht mehr zu HEAD
passen. Das ist beabsichtigt: vor neuem Packaging neu bauen.

Fuer Git-Commits Conventional Commits verwenden, nie AI-Co-Author-Trailer.
Secretwerte weder in dieses Dokument noch in Issues, Logs oder Kommandoausgaben.
Bei Wiederaufnahme keine Nutzung von Reset-Credits und keine Automation ohne
gesonderten Auftrag. Fabian wuenscht eine gruendliche, aber budgetbewusste
Fortsetzung ab diesem Stand statt eines neuen Gesamtaudits von vorne.
