# BFS Hit-Rate-Sitzung (Mac, FS-UAE)

**Wozu:** Der Zähler im Handler sagt, wie viele der aufgelisteten Einträge aus einem
Verzeichnis kamen, das schon einmal unverändert aufgelistet wurde. Daran entscheidet
sich, ob ein Ergebnis-Cache für ExAll sich lohnt. Du benutzt den Amiga so, wie du ihn
sonst benutzt, 10 bis 20 Minuten. Der Zähler ändert nichts an den Listings.

**Wichtig:** Der Systemordner des Kits ist ein Minimalsystem ohne `icon.library` und
`workbench.library`. Eine Workbench startet damit nicht, das Kit öffnet die Konsole
(Ergebnis der Prüfung in `System/workbench-check.txt`). Die Konsolenbefehle (`Dir`, `List`,
`Copy`, `Delete`, `Protect`, `Search`, `Filenote`) benutzen ExNext, nicht ExAll. Für die
Frage nach ExAll brauchst du Programme, die ExAll aufrufen, also eine Workbench (Weg B
oder C). Welche Programme das sind, zeigt die Auswertung (`ExAll-Aufrufe`).

## Weg C: deine eigene Workbench (empfohlen)

```sh
./attach-to-config.sh /Pfad/zu/deiner-konfiguration.fs-uae
```

Das Skript schreibt neben dein Original eine Kopie `...-hitrate.fs-uae` und gibt aus,
was es der Kopie hinzugefügt hat: die BFS-Platte mit dem instrumentierten Handler und den
Ordner `Hitrate` (Label `HR`). Dein Original und alle anderen Dateien bleiben unverändert;
eine vorhandene Zieldatei überschreibt es nie. Dann:

1. Die Zeile aus `Startup-Zeilen.txt` ans Ende von `S:User-Startup` deiner Workbench setzen.
2. `fs-uae ...-hitrate.fs-uae` starten. BFS erscheint als weiteres Laufwerk (3.400 Dateien in
   470 Verzeichnissen: `Projekte`, `Berichte`, `Messdaten`, `Archiv` mit 600 Dateien,
   `Werkzeuge`). Die Zähler beginnen bei 0. Benutze BFS wie sonst.
3. Nach der letzten Aktion 15 Sekunden warten, dann das Fenster schließen. Alle 10 Sekunden
   schreibt ein Hintergrundprozess die Zähler nach `Hitrate/hitrate.txt` (im Kit-Ordner).

Der Handler ist für `cpu = 68040` gebaut, das Skript warnt, wenn deine Konfiguration anders
ist. Getestet habe ich nur mit einer Stellvertreter-Konfiguration, nicht mit deiner Workbench.

## Weg B: Workbench des Kits (ungeprüft)

`icon.library` und `workbench.library` aus `Libs:` deiner Workbench 3.2 nach `System/Libs/`
im Kit kopieren, dann `./start.sh`. Das Kit probiert dann `LoadWB` (Rückgabewert in
`System/loadwb-rc.txt`, 0 heißt gestartet). Ob zwei Dateien reichen, habe ich nicht prüfen
können. Zähler: `System/hitrate.txt`. Geht es nicht, bleibt die Konsole.

## Weg A: Konsole (Funktionstest, nur ExNext)

`./start.sh` (oder `./start.sh shell`). Dort zum Beispiel `Dir BFS:Projekte ALL`,
`List BFS:Archiv`, `Dir BFS:Berichte`, mehrfach; Ändern des Volumes mit
`Copy BFS:Berichte TO BFS:Neu ALL`, `Delete BFS:Neu ALL`. Zähler: `System/hitrate.txt`.

## Auswerten

```sh
python3 summarize.py Hitrate/hitrate.txt     # Weg C
python3 summarize.py System/hitrate.txt      # Weg A und B
```

Das Skript druckt die Tabelle der Klassen (A neu, B gleiches Lock, C anderes Lock, D Volume
geändert) und das Ergebnis der Entscheidungsregel. Unter 2.000 Einträgen oder 100 Starts steht
dort „keine Entscheidung“: länger benutzen, jede Sitzung beginnt bei 0. Ruft kein Programm
ExAll auf, sagt das Skript es dazu. **Ablegen:** die Zählerdatei nach
`docs/qualification/evidence/bfs-exall-hit-rate-2026-10-23/sessions/<Datum>-hitrate.txt` im
Repository kopieren (eine Datei je Sitzung). Ohne Datei: in einer Shell
`HR:hitrate BFS: >HR:hitrate.txt` (Weg C) oder `hitrate BFS: >SYS:hitrate.txt` (Weg A, B).

## Zu wissen

Die Schwellen der Regel stehen im Plan (`docs/plans/bfs-exall-result-cache-v1.md`, Abschnitt
„Hit-rate measurement“). Du hast sie am 23. Oktober 2026 unverändert angenommen, sie gelten
fest. Die Zahlen zeigen die Nutzung einer Person auf einer emulierten Maschine, sie zählen
Einträge, nicht Zeit, und sind keine Statistik der Amiga-Welt.
