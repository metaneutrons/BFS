# BFS: Übergabe für die Weiterarbeit

## Zurückgenommener allocator-only Goal-Pilot vom 9. Oktober 2026

Nach dem Floor-Piloten wurde ein eigener residenter Root-Floor-Fast-Path nur in
alloc_data_at_goal versucht. Shared node_view und allgemeiner search_floor
blieben bytegenau unverändert; keine persistenten Hints, kein Public-API-/Format-
oder Garantie-Wechsel. Alle 14 festgelegten Cachy-Starts/28 Protokolle/23 Phasen
bestehen. Diagnose-Requests 517→262, Releases 521→266, Peeks unverändert 775.
Trotzdem Produktions-4-KiB normal 0,9509/1,1342 (Median 1,0425), durable 1,0023/
1,0135 (Median 1,0079); nur 1/4 schneller. Durable erster ExAll1000-Pass 1,3072/
1,1736 verletzt wiederholten 10-%-Guard. decision.json retain_locally=false.
Alle 586 Host-/ASAN-UBSAN-Tests in 63 Suites, acht neue adversariale Goal-Tests,
je acht all-call Probe-Oracles und sechs Adapter/sechs Policy-Tests bestehen.
Ein früher ASAN-Build erfasste noch Testautor-Zwischenstände mit vier fehlgeschlagenen
Assertions; ursprüngliches Log erhalten, finaler voller Lauf besteht vor den
Benchmarks. Luna prüfte Implementierung/Tests unabhängig. Nur die drei eigenen
Code-/Header-Deltas bytegenau zurückgenommen; neue aktive Testdatei entfernt,
Kandidat und acht Tests im 229-Datei-Archiv recoverable. Beide neu gebauten
Handler bytegenau auf Baseline. Alle alten Evidenzpakete unverändert.
Cachy-Rohdaten/Receipts und Summary/Decision unabhängig durch Luna geprüft;
beide JSON bytegenau regeneriert. Frisch gebauter Baseline-Host-Probe: acht OK.
Runner/Coffin wieder aktiv, kein Worker/Benchmark-Emulator. Keine CI, kein Commit/Push/
Merge/Issue-Write, kein weiterer Versuch. Scratch-Lease-Vermeidung nicht erneut
automatisch versuchen; nächster Kandidat sollte größere operation-lokale Arbeit
einsparen, ohne vorab einen Gewinn zu versprechen. Sparse/Read-Frage und <=5× PFS3
bleiben offen. Bericht: docs/qualification/bfs-goal-root-pilot-2026-10-09.md.

## Zurückgenommener FreeTree-Floor-Pilot vom 9. Oktober 2026

Ein begrenzter Fast Path ohne Scratch-Lease bei validierter residenter Root-
Leaf wurde implementiert und nach einem vorab festgelegten 14-Start-Piloten
wieder zurückgenommen. Alle 28 BFS/PFS3-Protokolle/23 Phasen bestehen. Acht
Produktionsstarts bilden je zwei Baseline/Kandidat-Paare normal/durable mit
umgekehrten FS-/Handler-Reihenfolgen; je ein identisches Baseline-Paar dient
als Kontrolle, zwei weitere Starts sind getrennte Diagnose. Keine Verlängerung.
4-KiB-Wachstum: normal 1,0330/0,9576 (Median 0,9953), durable 0,9101/0,9901
(Median 0,9501), drei von vier Paaren nicht langsamer. Diagnose-Pufferrequests
517→260, Releases 521→264 bei unveränderten Write-/Inode-/Map-/Goal-/I/O-/CRC-
Aufrufen. Trotzdem normal Delete40 1,1428/1,3655 und durable erster ExNext40-
Pass 1,3563/1,2584; beide überschreiten den vorher festgelegten 10-%-Guard in
beiden Paaren. `decision.json`: `retain_locally=false`. Durable 8-MiB-Lesen
ebenfalls 1,3810/1,0504. Kein Rauschen als bewiesene Ursache ausgeben.
584 Host- und 584 ASAN/UBSAN-Tests in 63 Suites plus je neun Probe-Oracles
bestehen; Leak-Erkennung hier nicht verfügbar. Je sechs Summary-/Decision-
Tests bestehen. Luna prüfte Implementierung, vollständige Rohdaten und
bytegenaue Summary/Decision-Regeneration unabhängig. Alle 229 eingefrorenen
Quell-/Harness-Dateien, Runtime-Inputs und getrennten 67-Asset-Kohorten stimmen.
Aktiver B-tree-Code und Probe-Test sind bytegenau auf den vorherigen Stand
zurückgesetzt; neue aktive Floor-Testdatei entfernt, im Kandidatenarchiv
weiter recoverable. Produktions-/Sidecar-Handler bytegenau reproduziert.
Frisch gebauter zurückgesetzter Host-Probe besteht alle acht Baseline-Oracles.
Keine fremden lokalen Änderungen zurückgesetzt. Runner/Coffin wieder aktiv,
kein Benchmark-Emulator/Worker; keine CI, kein Commit/Push/Merge/Issue-Write.
Vorheriger Sparse-Kandidat, Read-Regressionsfrage und <=5× PFS3 bleiben offen.
Nächster Versuch müsste den gemeinsamen Hot-Node-View-Pfad unverändert lassen;
die Messungen beweisen nicht, dass dessen Refactoring die Nachteile verursacht.
Kein zweiter Versuch in diesem Schritt. Bericht und gesicherte Evidenz:
`docs/qualification/bfs-floor-resident-pilot-2026-10-09.md`.

## Abgeschlossene Write-Path-Diagnose vom 9. Oktober 2026

Alle vier vorab festgelegten Cachy-Starts bestehen: normal/durable mit beiden
FS-Reihenfolgen, ein unveränderter Sparse-Kandidat, kein Handlervergleich.
Alle acht schema2-Rohprotokolle enthalten sämtliche 23 Phasen. Optionale
Sidecar v1/Packet3013 misst jeden Inode-Read/-Write, Extent-Map und Goal-
Allokationsversuch; ABI16/Default64 und Produktionshandler sind unverändert.
Durable 4-KiB-Wachstum: 256 Writes, 515 logische Inode-Reads (16,977–18,306 ms),
257 Inode-Writes (9,862–11,612 ms), 256 Maps (3,120–3,855 ms), 256 Goal-Versuche
(14,641–15,605 ms), Device-Write 11,141–12,456 ms. Keine Device-Reads/Node-CRC
im Work; Flush 1,983/2,240 ms mit fünf Writes, drei Updates, vier Write-CRCs
und einem Commit. Normal/BFS-first hat diesmal einen Commit im Work;
Normal/PFS3-first ist mit 205,660 ms deutlich langsamer. Beide bleiben erhalten.
514 B-tree-Pufferanforderungen sind KEINE 514 bewiesenen Heap-Allokationen:
der Cache kann vier Scratch-Puffer wiederverwenden. Direkte B-tree-malloc-
Zähler erfassen nicht die Allokationen im Cache-Callback.
Nächster begrenzter Implementierungskandidat: FreeTree-Vorgängersuche bei
zusammenhängenden Goal-Allokationen über eine bereits validierte residente
Leaf beschleunigen; alle Bounds-/CRC-/Struktur-/Generation-/Ownership-/COW-
Prüfungen und Fallbacks erhalten. Die 15–16 ms schließen Range-Mutation ein,
also keinen solchen Zeitgewinn zusagen. Kein Fast Path in diesem Schritt.
Inode-Rechte/Refresh nicht ungesichert zwischen Packets cachen; vorhandener
operation-lokaler Seed vermeidet bereits den zusätzlichen Publish-Read.
Alle Timer sind inklusive/nestend, nicht addieren/subtrahieren/extrapolieren.
Je acht Host-/ASAN-UBSAN-Oracles, 49 Split-, 66 bestehende Verifier- und acht
Summary-Tests bestehen. LeakSanitizer hier nicht verfügbar; kein Leak-Nachweis.
Luna prüfte Integration, Rohdaten, Summary und Puffersemantik unabhängig.
Alle 228 eingefrorenen Quell-/Harness-Dateien, Runtime-Inputs und 67 Assets
stimmen. Nach den Messungen nur lokale Build-Hinweise im Fehlerpfad korrigiert;
Original und Diff sind erhalten. Vollständige textbasierte Evidenz gesichert.
Runner und Coffin sind wieder aktiv, kein Benchmark-Emulator/Worker läuft.
Keine CI, kein Commit/Push/Merge/Issue-Write. Sparse bleibt vorläufig; Read-
Regressionsfrage und <=5× PFS3 bleiben offen. Kein Pooling mit alten Kohorten.
Bericht: `docs/qualification/bfs-write-path-profile-2026-10-09.md`.

## Abgeschlossene Cachy Read-/Flush-Diagnose vom 9. Oktober 2026

Alle zwölf vorab festgelegten Starts bestehen: je zwei M5/Sparse-Paare normal
und durable mit umgekehrter FS-/Handler-Reihenfolge, plus je ein M5/M5-Paar.
Alle 24 Rohprotokolle enthalten sämtliche 23 Phasen. Die neue All-Call-CRC-
Diagnose bleibt getrennt von den Produktionskohorten und lokalen Mac-Checks.
8-MiB-Lesen enthält 86,79–89,22 % Verify im Guest, Read selbst 26,159–35,264 ms;
Grown-File-Lesen 81,44–84,08 % Verify, Read 5,696–7,485 ms. Beide ohne Volume-
Flush; 8 MiB ohne Node-CRC, Grown-File genau ein Read-CRC. Alle 1.840 gepaarten
I/O-/CRC-Aufrufvergleiche über sämtliche Phasen/Scopes sind identisch.
Durable Grown-File-Read: Work-Verhältnisse 0,9618/1,0309, Read 0,8731/1,1471.
Durable 8-MiB-Read: 0,9403/1,3468. Die alte 25-%-Regressionsfrage bleibt offen;
zwei Paare beweisen weder Gleichwertigkeit noch Rauschen als Ursache.
Durable 4-KiB-Wachstum ist im Work 9,1 %/6,4 % langsamer, Flush 0,9682/1,0339.
Work: 256 Writes, keine Node-CRC/Updates; Flush: fünf Writes, drei Updates,
vier Write-CRCs und ein Commit, jeweils identisch M5/Sparse. Nächster begrenzter
Messpunkt: die 256 Packet-/Core-Writes mit 515 logischen Inode-Reads, 516
B-tree-Suchen und 256 Extent-Maps sowie Device-Zeit getrennt untersuchen.
Logische Inode-Reads sind keine Device-Reads; diese sind im 4-KiB-Work null.
Inklusive/nestende Probezeiten nicht addieren oder zu exklusiver CPU-Zeit
subtrahieren. Keine weitere CRC-Optimierung für Phasen ohne CRC-Aufruf ableiten.
21 Summary-, 26 Split- und 66 bestehende Verifier-Tests bestehen. Luna prüfte
Rohdaten und Summary unabhängig; Hashes aller 223 exportierten Quell-/Harness-
Dateien, Runtime-Inputs und 67 gemeinsamen Assets stimmen. Lokal stimmen 222
der 223 Exportdateien; einzig der bereits ergänzte Host-CRC-Test unterscheidet
sich vom früheren Export, nicht Handler-/Guest-/Harness-Quellen. Beide
Testfassungen und Diff sind erhalten. Ein Docker-PID-
Preflight-Fehler vor dem Stop sowie eine falsche ASCII-Sortierannahme im
Summary wurden korrigiert und dokumentiert; kein Messlauf fehlt oder ist
wiederholt/gefiltert. Runner und Coffin sind nach autorisiertem Stop wieder
aktiv, kein Benchmark-Emulator läuft. Keine CI, kein Commit/Push/Merge und
kein Core-Optimierungsversuch. Sparse bleibt vorläufig, <=5× PFS3 bleibt offen.
Bericht: `docs/qualification/bfs-read-flush-cachy-2026-10-09.md`.

## Getrennte Read und Flush Diagnose vom 9. Oktober 2026

Neue Modi `split-compare` und `split-durable-compare` erfassen Work/Volume-Flush,
Open/Read/Write/Handle-Flush/Close und den byteweisen Datenvergleich getrennt.
I/O- und Node-CRC-Aufrufe sowie inklusive Ticks sind für Work und Volume-Flush
separat erfasst; Diagnose-Builds messen jeden CRC-Aufruf, ABI16/Default64 und
Produktionshandler bleiben unverändert. Beide Produktionshandler und bisherigen
Probes sind bytegenau reproduziert. Kein neuer Core-Optimierungsversuch.
Zwei lokale Mac-Protokollprüfungen bestehen alle 23 Phasen: normal M5 und
durable Sparse, KEIN gepaarter Performancevergleich und keine Vermischung mit
Cachy-Kohorten. 8-MiB-Lesen enthält etwa 92 % Datenvergleich im Prüfprogramm;
Read selbst 8,623/7,727 ms, Verify 102,744/103,340 ms. Grown-File-Lesen enthält
86–87 % Verify. Beide Read-Phasen haben keinen Volume-Flush; normale Reads
keine Node-CRC-Aufrufe, durable Grown-File-Read genau einen mit null Messticks.
Nullticks bei positiven Aufrufen bedeuten keine nachgewiesenen Nullkosten.
Durable 4-KiB-Wachstum: Work 42,111 ms, Volume-Flush 0,704 ms, dabei fünf
Blockschreibvorgänge, drei Updates und vier Write-CRCs. Intervalle sind
inklusive/nestend, nicht addieren oder als exklusive CPU-Zeit ausgeben.
Die alte 25-%-Read-Regressionsfrage und <=5× PFS3 bleiben offen.
Ein erster lokaler Lauf mit größeren Guest-Stackframes resetete und ist
abgewiesen/separat erhalten. Snapshot-Scratch ist jetzt statisch/seriell;
Phaseframe 2784→1960 Bytes. Split-Modi setzen zusätzlich Stack 32768.
26 neue und 66 bestehende Verifier-Tests sowie je sechs Host-Probe-Oracles
für Default64/All-Call1 bestehen. Luna implementierte Parser-Tests und prüfte
die Attribution unabhängig. Keine CI, kein Commit/Push/Merge, kein Emulator
aus dieser Aufgabe mehr aktiv. Die hier noch vorbereitete zwölfteilige Cachy-
Diagnose wurde anschließend ausdrücklich freigegeben und vollständig beendet;
aktueller Stand und wiederhergestellte Dienste stehen im Abschnitt oben.
Dieser Abschnitt beschreibt die vorausgehenden lokalen Protokollprüfungen,
nicht einen gepaarten Cachy-Vergleich. Bericht und vollständige lokale Rohdaten:
`docs/qualification/bfs-read-flush-profile-2026-10-09.md`.

## Abgeschlossene Append und Create Bestätigung vom 9. Oktober 2026

Der Sparse-CRC-Kandidat bleibt nur vorläufig lokal erhalten, ohne allgemeine
Performance-Freigabe. 40 neue Frischstarts mit identischen Builds bestehen
alle Daten-/Identitäts-/RDB-/Reihenfolgeprüfungen: je acht M5/Sparse-Paare
normal/durable mit ausgeglichener Handler- und Dateisystemreihenfolge plus
je zwei M5/M5-Kontrollpaare. Keine alten/neuen Kohorten still zusammenlegen.
4-KiB-Dateiwachstum ist im neuen Median normal praktisch gleichauf, durable
2,5 % langsamer (6/8; alle vier Reihenfolgen-Mediane langsamer). Create und
1-KiB-Wachstum zeigen keinen langsameren Durable-Median. Die früheren 5–7 %
beim 4-KiB-Wachstum wiederholen sich nicht in derselben Größenordnung.
Aber das anschließende Lesen der gewachsenen Dateien wird durable im Median
25 % langsamer (7/8; alle vier Reihenfolgen-Mediane langsamer). Normales
8-MiB-Lesen wird 5,3 % langsamer. Die identischen M5-Kontrollen streuen stark;
zwei Paare je Modus beweisen weder Rauschen als Ursache noch Regressionsfreiheit.
Die Regressionsfrage bleibt offen. Append-Phasen erzeugen per MODE_NEWFILE
eine neue Datei und vergrößern sie; sie prüfen kein Anhängen an existierende
Dateien. Nächster begrenzter Schritt: Read-/Flush-Zeit sowie I/O-/CRC-Arbeit
dieser Phasen trennen, nicht erneut ungeprüft eine volle Messreihe starten.
Große wiederholte ExAll-Listings werden erneut 11–16,5 % schneller, liegen
aber weiterhin 14–16× hinter PFS3; 52/184 normale und 41/184 durable
Kandidaten-Phasenwerte überschreiten 5×. Das Gesamtziel bleibt offen.
24 neue Summary-/Asset-Tests und 25 erneut geprüfte alte Parser-Oracles
bestehen. Alle 366 eingefrorenen Quelldateien stimmen vor/nach den Messungen,
die acht Kandidatendateien auch im lokalen Arbeitsbaum. Installierte Builds
stimmen vor/nach allen Läufen; 67 sonstige Asset-Hashes sind in allen 40 gleich.
Ein erster Asset-Test-Import scheiterte am Bindestrich im Helfer-Dateinamen;
der Importname ist korrigiert, der Fehlversuch separat erhalten. Luna hat
Zahlen und Auswertung unabhängig geprüft. Keine neue Core-/Guest-Änderung.
Der CI-Guard nutzte einen auf Cachy nicht vorhandenen Dienstnamen. Die
tatsächlichen GitHub-/GitLab-Runner sind separat als geladen/inaktiv/PID 0
geprüft; der ausgeführte Original-Runner bleibt unverändert dokumentiert.
Kein Emulator läuft mehr, kein CI-/Commit-/Push-/Merge-/Issue-Auftrag ausgeführt.
Bericht: `docs/qualification/bfs-sparse-crc-append-confirmation-2026-10-09.md`.
Die vorausgehende 784-Einträge-CRC-Evidenz bleibt unverändert verifizierbar.

## Vorläufig beibehaltener CRC-Kandidat vom 9. Oktober 2026

Ein Sparse-CRC-Kandidat ist lokal implementiert, gemessen und vorläufig
beibehalten; eine allgemeine Performance-Freigabe ist nicht erteilt.
Nur der benutzte Schlüsselpräfix von Directory-Leaves nutzt eine
exakte CRC-Berechnung über tatsächlich geprüfte Nullfolgen; alle Bytes, Alt-
Padding, Struktur-/Level-/Parent-Prüfungen, Format v3 und 30 Buffers bleiben
abgedeckt. Kein I/O-Batching oder weiterer Scan-Cache-Versuch. Zwei m68k-Proben
bestehen je 1.297 Oracle-Fälle und 2.594 Registerprüfungen. Der synthetische
Short-Key-Fall benötigt im gepaarten Median 40,81 % der normalen CRC-Zeit;
dichte Daten werden langsamer und sind nicht allgemein aktiviert.
Vier Produktionspiloten zeigen etwa 6–12 % schnellere wiederholte große
Listings, aber starke Streuung kleiner Phasen/Kontrollen. Vier ABI16/schema15-
Diagnosen bestätigen identische Reads/CRC-Aufrufe/Cache-Zähler. Je 577 Host-
und Sanitizer-Tests in 62 Suites, 117 Qualitätstests, 25 Summary-Oracles,
21 Konformitäts- und 30 Fault-/Qualifikations-Contracts bestehen.
Alle 40 vorgeschriebenen Vergleiche sind strikt daten-/identitätsgeprüft,
darunter 32 Normal-/Durable-Läufe. Große ExAll-Listings werden im gepaarten
Median 11–15 % schneller, ExNext je nach Größe/Modus 4–20 %. Warme 40er-Fälle
ändern sich im Median höchstens etwa 1,8 %. Aber 4-KiB-Append zeigt 4,7 %/7,0 %
langsamere Mediane und je 5/8 langsamere Paare; Durable-Create ist ebenfalls
im Median langsamer. Diese mögliche Regression nicht als Rauschen wegreden.
Nächster begrenzter Schritt: Append/Create bei identischen Binaries und
PFS3-Kalibratoren gezielt bestätigen, bevor der Kandidat freigegeben wird.
52 lokale Amiga-Runtime- und neun Kickstart-Kompatibilitätstests bestehen;
auf Linux zusätzlich 21 Konformitäts- und 30 Fault-Contracts. Ein fehlender
Git-Metadaten-Export verursachte zunächst zwei Contract-Fehler; derselbe Lauf
besteht nach Ergänzung echter Basis-Commit-Metadaten ohne Code-/Oracleänderung.
Die erste Mikroprobe nutzte einen inkompatiblen Funktionszeiger-Cast. Ein
fehlgeschlagener Guard-Build wurde wegen eines alten Binary-Hashes zunächst
falsch als Erfolg berichtet. Das ist dokumentiert, die alten Protocol1-Daten
sind nicht qualifizierend. Zwei frische Protocol2-Proben nutzen korrekt
typisierte, registertransparente Tail-Jump-Adapter und bestehen alle Prüfungen.
Ergebnis und Rohdaten:
`docs/qualification/bfs-sparse-key-crc-performance-2026-10-09.md`.
Das <=5×-Ziel bleibt offen: große ExAll-Listings liegen noch etwa 15–17× hinter
PFS3; zahlreiche Einzelwerte überschreiten 5. Alle 3.258 alten Evidenzeinträge
bleiben unverändert. Kein Commit/Push/Merge, keine CI und kein laufender Emulator.

## Vorausgehende Scan-Admission-Entscheidung vom 9. Oktober 2026

Auch der Two-Touch-Scan-Admission-Versuch ist verworfen. Vier Produktionspiloten
und vier ABI16/schema15-Diagnosevergleiche sind bei 30 Buffers strikt geprüft.
Repeated ExAll 400 wird im Pilot etwa 28 % schneller, aber ExAll 40 etwa
22 % und ExNext 1.000 etwa 24 % langsamer. Die Diagnosen wiederholen in beiden
Reihenfolgen fünf zusätzliche Reads/CRCs im warmen 40er-Fall, 1.840→2.860 bei
ExNext 1.000 und 1.713→1.823 bei ExAll 1.000. Nur ExAll 400 spart Reads/CRCs
(692→440). Alle acht Policy-/Teständerungen sind entfernt; sieben bestehende
Dateien stimmen bytegenau mit der eingefrorenen Baseline überein, die neue
Testdatei ist im rückspielbaren Patch gesichert. M5 und ABI16 bleiben unverändert.
Kandidat: 578 Host- und nach einem erzwungenen Allocation-Fault-Test-Neubau
578 Sanitizer-Tests in je 61 Suites. Ein früherer Sanitizerlauf enthielt ein
veraltetes Testbinary (577 Tests); dieser Log bleibt getrennt erhalten.
Wiederhergestellt: je 566 Host-/Sanitizer-Tests in 60 Suites sowie identische
Produktions-/Probe-/Guest-Binaries. 117 Qualitätstests und zwölf Summary-Oracles
bestehen. Bericht und Evidenz:
`docs/qualification/bfs-scan-admission-performance-2026-10-09.md`.
Keine volle 32er-Qualifikation, kein Commit/Push/Merge, keine CI und kein
laufender Emulator. Das <=5×-Ziel bleibt offen. Weitere generische Scan-Policies
sind nach den drei verworfenen Versuchen nicht begründet; der nächste begrenzte
Ansatz sollte Kosten frischer Leaf-Zugriffe isoliert senken, etwa über sicher
validiertes I/O-Batching oder CRC-CPU-Arbeit. Kein Speedup ist daraus belegt.

## Vorausgehende Cacheversuche vom 9. Oktober 2026

Der Scan-Cache-Demotion-Versuch ist ebenfalls verworfen. Vier Produktionspiloten
und vier ABI16/schema15-Diagnosevergleiche bestehen die strikte Prüfung bei
unverändert 30 Buffers. Repeated ExAll 400 verbessert sich im Pilot um etwa
21 %, aber warmes ExAll 40 wird 3,09× langsamer und ExNext 1.000 etwa 18 %
langsamer. Die Diagnosen reproduzieren in beiden Reihenfolgen 0→60 Reads/CRCs
bei ExAll 40 und 1.840→2.230 bei ExNext 1.000. Nur die acht Policy-/Test-
Änderungen sind entfernt; sieben bestehende Dateien entsprechen bytegenau
dem eingefrorenen Stand, die neue Testdatei bleibt im rückspielbaren Patch.
Kandidat: je 573 Host-/Sanitizer-Tests in 61 Suites. Wiederhergestellt: je
566 Tests in 60 Suites; Produktionsbinary identisch mit M5, ABI16-Probe und
neuer Guest identisch mit ihrem frischen Baseline-Build. 117 Qualitätstests
und elf Summary-Oracles bestehen. Keine volle 32er-Qualifikation, kein
Commit/Push/Merge und keine CI; kein Emulator läuft mehr.
Die zusätzliche Diagnose unterscheidet Directory-/Inode-Leaf-/Internal-
Views nach erwartetem Traversal-Level; positive Hint-Peeks sind nicht darin
enthalten. Normal-/Durable-Schema4 bleibt unverändert, ältere Schemas4–14
bleiben verifizierbar. Bericht und Evidenz:
`docs/qualification/bfs-scan-cache-performance-2026-10-09.md`.
Das <=5×-Ziel bleibt offen. Nächste Hypothese: Wiederverwendung erkennen und
Scan-Admission gezielt steuern; pauschale Directory-Leaf-Demotion ist ungeeignet.

Der vorausgehende Cursor-Path-Versuch ist ebenfalls verworfen. Vier
Produktionspiloten und vier Diagnosevergleiche bestehen die strikte Prüfung.
Große wiederholte Listings benötigen in beiden Diagnosewiederholungen jeweils
20 zusätzliche Reads/CRCs trotz weniger Directory-Views. Repeated ExAll mit
40 Einträgen zeigt im Pilot einen Median von 1,0401 gegenüber M5. Nur die
drei Implementierungs-/Testdateien dieses Versuchs wurden exakt zurückgesetzt;
vorherige Änderungen bleiben erhalten. Kandidat: 568 Host- und 568
Sanitizer-Tests. Wiederhergestellt: 565 Tests je Suite sowie identische
Produktions-/Probe-Binaries. Keine volle Messserie, kein Commit/Push, keine CI.
Der <=5x-PFS3-Nachweis bleibt offen. Evidenz und rekonstruierbarer Patch:
`docs/qualification/bfs-cursor-path-performance-2026-10-09.md`.
Damals nächster begrenzter Versuch: Scan-resistente Admission/Promotion bei weiterhin
30 Buffers, mit Misses nach Baum und Node-Level. Keine heimliche
Cache-Vergrößerung oder schwächere Validierung.

Der Exact-Key-Early-Return-Versuch wurde implementiert, korrektheitsgeprüft,
gemessen und verworfen. Acht normale Läufe je M3-, Blattbereich- und
Kandidaten-Handler sind strikt verifiziert. Warme 40er-ExAll-Läufe werden
gegenüber dem vorherigen Blattbereich-Stand im Median nicht schneller;
400er-ExAll und 4-KiB-Append zeigen langsamere Mediane. Nur der heutige
Early-Return-Code und sein zusätzlicher Test wurden entfernt. Der beibehaltene
Core und die elf Hint-Tests entsprechen dem vorherigen Stand bytegenau.

Der [Bericht](docs/qualification/bfs-exact-key-fastpath-2026-10-09.md) enthält
auch acht Piloten sowie zwei fertige und einen abgebrochenen Durable-Lauf.
Die geplante 50er-Reihe ist ausdrücklich nicht vollständig qualifiziert.
Restliche Messungen wurden beendet; kein FS-UAE läuft mehr auf Cachy.
Die erneut geprüften beibehaltenen Host-/Sanitizer-Reihen bestehen je 565
Tests. Frühere Performancearbeit und Evidenz bleiben erhalten, M5 bleibt
vorläufig und das <=5×-Ziel offen. Nächster größerer Hebel sind Verzeichnis-/
Inode-Cache-Misses, nicht ein weiterer reiner Dispatch-Schnellpfad.
Keine Remote-CI, Commits, Pushes oder Merges wurden gestartet.

## Beibehaltene Performancearbeit vom 8. Oktober 2026

Aktueller Zweig: `fix/metadata-listing-performance`, Basis `07216b7`.
Der Arbeitsbaum enthält die lokalen M1–M5-Änderungen und Evidenz; nichts davon
ist committed, gepusht oder gemergt. Nicht auf `main` wechseln und keine
Änderungen verwerfen. Die folgenden Abschnitte beschreiben den historischen
Stand vom 4. Oktober, nicht den aktuellen Arbeitsbaum.

Der [Plan](docs/plans/bfs-metadata-listing-performance-v1.md) und der
[aktuelle Bericht](docs/qualification/bfs-leaf-range-performance-2026-10-08.md)
sind maßgeblich. Reale 400-/1.000-Einträge-Verzeichnisse sind jetzt geprüft;
die alten „400“-Listingzeilen bedeuten zehn Durchläufe über 40 Dateien.
Das <=5×-Ziel ist offen: große wiederholte ExAll-Läufe liegen weiterhin bei
rund 17–19× PFS3. Die neuen Blattbereich-Hinweise verbessern größere Listings,
verlangsamen aber kleine warme ExAll-Läufe und zeigen Append-Regressionssignale.
Retention ist vorläufig. Der am 9. Oktober folgende vollständig geschützte
Exact-Key-Early-Return-Versuch wurde verworfen; siehe den aktuellen Abschnitt.

Beide vollständigen Host-/Sanitizer-Reihen bestehen je 565 Tests; 113
Qualitätstests, 21 Conformance-Tests, 52 Amiga-Integrationstests und neun
Kickstart-Kompatibilitätsszenarien bestehen. Alle 44 Messläufe sind strikt
verifiziert. Produktionsmessungen bleiben bei 30 Puffern; 64/128 allein lösen
die Lücke nicht. Format v3 und Dauerhaftigkeitspolitik bleiben unverändert.
Die Prüfsummen früherer Evidenz bleiben unverändert. `snapdog-runner` bleibt
gestoppt. Keine Remote-CI, Veröffentlichung oder Merge ohne neuen Auftrag.

## Historischer Stand vom 4. Oktober 2026

Gearbeitet wurde auf `main`. Die v3-Kette mit der Arbeit
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
11. Für `aros-toolchains`: Das in Release v0.1.4 mitgelieferte
    `aros-collect` 0.3.12 lässt beim Linken mit `startup.o`
    `__eh_frame_start` offen; AROS lädt das Programm dann nicht („file is
    not executable“). `aros-collect` 0.3.19 aus `aros-tools`, mit dem auch
    AROS-NX baut, löst es auf. BFS linkt deshalb mit dem `aros-collect` der
    installierten `aros-tools`. In AROS-NX selbst wurde beim Nachweis kein
    Fehler gefunden: Die Abstürze kamen von falsch gelinkten BFS-Programmen.

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
