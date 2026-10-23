# BFS Hit-Rate-Sitzung (Mac, FS-UAE)

**Wozu:** Der Zähler im Handler sagt, wie viele der aufgelisteten Einträge aus einem
Verzeichnis kamen, das schon einmal unverändert aufgelistet wurde. Daran entscheidet
sich, ob ein Ergebnis-Cache für ExAll sich lohnt. Du benutzt den Amiga so, wie du
ihn sonst benutzt, 10 bis 20 Minuten. Der Zähler ändert nichts an den Listings.

## Ablauf

1. **Starten:** `./start.sh` in diesem Ordner. Nach etwa 15 Sekunden erscheint die
   Workbench. Das Volume **BFS** hat rund 3.400 Dateien in 460 Verzeichnissen
   (`Projekte`, `Berichte`, `Messdaten`, `Archiv` mit 600 Dateien, `Werkzeuge`).
   Die Zähler beginnen bei 0. Hängt etwas, Fenster schließen und
   `./start.sh shell` starten (nur die Konsole, ohne Workbench, alle Befehle laufen).
2. **Workbench benutzen:** Doppelklick auf das Volume BFS, Schubladen öffnen, tiefer
   hineingehen, Fenster schließen und später wieder öffnen. Im Menü
   *Window → Show → All Files* einschalten, dann *Window → View By* auf Name, Size
   und Date stellen, Fenster scrollen und nebeneinander öffnen.
3. **Shell:** Menü *Workbench → Execute Command…*, `NewShell` eingeben. Darin zum
   Beispiel `Dir BFS:Projekte ALL`, `Dir BFS:Archiv`, `Dir BFS:Messdaten ALL`,
   `Dir BFS:Berichte`, dieselben Befehle mehrmals und aus zwei Shells.
   **Mit laufender Workbench `List` nicht benutzen:** in diesem Minimalsystem hängt es
   (auch auf anderen Volumes). Im Shell-Modus geht `List`.
4. **Dateiauswahl:** `RequestFile DRAWER BFS:Projekte`, im Dialog in Unterordner und
   zurück wechseln, abbrechen, mehrfach. Ob dieser Dialog hier ein Listing auslöst,
   ist ungeprüft; die Zähler zeigen es.
5. **Kopieren und Löschen** (ändert das Volume): `MakeDir BFS:Neu`,
   `Copy BFS:Berichte TO BFS:Neu ALL`, `Delete BFS:Neu ALL`, dazwischen wieder ein
   Fenster öffnen oder `Dir BFS:Berichte`.
6. **Beenden:** nach der letzten Aktion 15 Sekunden warten. Ein Hintergrundprozess
   schreibt alle 10 Sekunden die Zähler in `System/hitrate.txt`. Dann das
   FS-UAE-Fenster schließen.

## Auswerten

```sh
python3 summarize.py System/hitrate.txt
```

Das Skript druckt die Tabelle der Klassen (A neu, B gleiches Lock, C anderes Lock,
D Volume geändert) und das Ergebnis der Entscheidungsregel. Ist die Datenmenge zu
klein (unter 2.000 Einträge oder 100 Starts), steht dort „keine Entscheidung“:
länger benutzen und neu starten, jede Sitzung beginnt bei 0.

**Ergebnisdatei ablegen:** `System/hitrate.txt` nach
`docs/qualification/evidence/bfs-exall-hit-rate-2026-10-23/sessions/<Datum>-hitrate.txt`
im Repository kopieren (eine Datei je Sitzung) und der Sitzung Bescheid sagen.

## Zu wissen

- Die Schwellen der Regel stehen im Plan (`docs/plans/bfs-exall-result-cache-v1.md`,
  Abschnitt „Hit-rate measurement“). Du hast sie am 23. Oktober 2026 unverändert
  angenommen, sie gelten fest und werden nach den ersten Daten nicht mehr geändert.
- Die Zahlen zeigen die Nutzung einer Person auf einer emulierten Maschine. Sie
  zählen Einträge, nicht Zeit, und sind keine Statistik der Amiga-Welt.
- Keine Datei? Dann in einer Shell `hitrate BFS: >SYS:hitrate.txt` von Hand.
