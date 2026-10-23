# BFS Hit-Rate-Sitzung (Mac, FS-UAE)

**Wozu:** Der Zähler im Handler sagt, wie viele der aufgelisteten Einträge aus einem
Verzeichnis kamen, das schon einmal unverändert aufgelistet wurde. Daran entscheidet
sich, ob ein Ergebnis-Cache für ExAll sich lohnt. Du benutzt den Amiga so, wie du
ihn sonst benutzt, 10 bis 20 Minuten. Der Zähler ändert nichts an den Listings.

**Wichtig:**

- Der Systemordner dieses Kits ist ein Minimalsystem ohne `icon.library` und
  `workbench.library`. Eine Workbench mit Schubladen und Menüs startet damit nicht. Das Kit
  prüft das beim Start (Ergebnis in `System/workbench-check.txt`) und öffnet dann die
  **Konsole**.
- Die Befehle der Konsole (`Dir`, `List`, `Copy`, `Delete`, `Protect`, `Search`, `Filenote`)
  benutzen ExNext, nicht ExAll (gemessen: ExAll-Aufrufe 0). Die Frage der Messung betrifft
  ExAll. Mit der Konsole allein zählst du nur ExNext, und die Regel kann nichts entscheiden
  (`summarize.py` sagt das). Weg A ist ein Funktionstest, für die Messung brauchst du
  Programme, die ExAll aufrufen. Welche das sind, zeigen die Zähler (`ExAll-Aufrufe`):
  Workbench-Fenster, Dateiauswahlen und deine üblichen Programme, also Weg B oder C.

## Weg A: Konsole (geht sofort, nur ExNext)

1. `./start.sh` im Kit-Ordner. Es erscheint die Konsole mit einem Hinweis, dass keine
   Workbench startet. Das Volume **BFS** hat rund 3.400 Dateien in 470 Verzeichnissen
   (`Projekte`, `Berichte`, `Messdaten`, `Archiv` mit 600 Dateien, `Werkzeuge`).
   Die Zähler beginnen bei 0.
2. Befehle eintippen: `Dir BFS:Projekte ALL`, `List BFS:Archiv`, `Dir BFS:Messdaten ALL`,
   `Dir BFS:Berichte`, dieselben Befehle mehrmals. Zum Ändern des Volumes:
   `MakeDir BFS:Neu`, `Copy BFS:Berichte TO BFS:Neu ALL`, `Delete BFS:Neu ALL`,
   danach wieder `Dir BFS:Berichte`.
3. Nach der letzten Aktion 15 Sekunden warten. Ein Hintergrundprozess schreibt alle 10
   Sekunden die Zähler in `System/hitrate.txt`. Dann das FS-UAE-Fenster schließen.

## Weg B: Workbench des Kits mit zwei Bibliotheken von dir (ungeprüft)

`icon.library` und `workbench.library` aus `Libs:` deiner Workbench 3.2 nach `System/Libs/`
im Kit kopieren, dann `./start.sh`. Das Kit probiert dann `LoadWB` und schreibt dessen
Rückgabewert nach `System/loadwb-rc.txt` (0 heißt: gestartet). Ob das ohne weitere Dateien
reicht, habe ich nicht prüfen können, die Bibliotheken liegen nicht in `emulator-test/.assets`.
Geht es nicht, bleibt die Konsole (Weg A). Mit `./start.sh shell` erzwingst du sie.

## Weg C: deine eigene Workbench

```sh
./attach-to-config.sh /Pfad/zu/deiner-konfiguration.fs-uae
```

Das Skript schreibt neben das Original eine Kopie `...-hitrate.fs-uae` (das Original bleibt
unverändert) mit der BFS-Platte, dem instrumentierten Handler und dem Ordner `Hitrate`.
Starte die Kopie mit `fs-uae ...-hitrate.fs-uae`. Trage die Zeile aus `Startup-Zeilen.txt`
ans Ende von `S:User-Startup` deiner Workbench ein. BFS erscheint dort als weiteres
Laufwerk, die Zähler stehen alle 10 Sekunden in `Hitrate/hitrate.txt`. Der Handler ist
für `cpu = 68040` gebaut. Getestet habe ich das mit einer Stellvertreter-Konfiguration,
nicht mit deiner Workbench.

## Auswerten

```sh
python3 summarize.py System/hitrate.txt      # Weg A und B
python3 summarize.py Hitrate/hitrate.txt     # Weg C
```

Das Skript druckt die Tabelle der Klassen (A neu, B gleiches Lock, C anderes Lock,
D Volume geändert) und das Ergebnis der Entscheidungsregel. Ist die Datenmenge zu
klein (unter 2.000 Einträge oder 100 Starts), steht dort „keine Entscheidung“:
länger benutzen und neu starten, jede Sitzung beginnt bei 0.

**Ergebnisdatei ablegen:** die Zählerdatei nach
`docs/qualification/evidence/bfs-exall-hit-rate-2026-10-23/sessions/<Datum>-hitrate.txt`
im Repository kopieren (eine Datei je Sitzung) und der Sitzung Bescheid sagen.

## Zu wissen

- Die Schwellen der Regel stehen im Plan (`docs/plans/bfs-exall-result-cache-v1.md`,
  Abschnitt „Hit-rate measurement“). Du hast sie am 23. Oktober 2026 unverändert
  angenommen, sie gelten fest und werden nach den ersten Daten nicht mehr geändert.
- Die Zahlen zeigen die Nutzung einer Person auf einer emulierten Maschine. Sie
  zählen Einträge, nicht Zeit, und sind keine Statistik der Amiga-Welt.
- Keine Datei? Dann in der Konsole `hitrate BFS: >SYS:hitrate.txt` von Hand (Weg A und B),
  bei Weg C `HR:hitrate BFS: >HR:hitrate.txt`.
