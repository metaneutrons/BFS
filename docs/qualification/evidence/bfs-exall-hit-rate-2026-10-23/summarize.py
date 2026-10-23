#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Turn the hit-rate counters of a session into the decision-rule table.

Reads the file that `hitrate BFS:` wrote (tab separated key and value lines) and
prints, in German, the table of the plan (docs/plans/bfs-exall-result-cache-v1.md,
section "Hit-rate measurement") and the reading of its proposed decision rule.
The thresholds below are those of the plan, accepted by Fabian on 23 October 2026
without change. They are fixed and are not changed once data exists.

    summarize.py hitrate.txt [--json OUT]
"""
import argparse
import json
from pathlib import Path
import sys

# The plan's thresholds (percent of all entries listed by ExAll and ExNext).
MIN_ENTRIES = 2000
MIN_STARTS = 100
LOCK_OWNED_SHARE = 25.0
DIRECTORY_LEVEL_SHARE = 40.0
CHANGED_SHARE_OF_EXALL = 30.0
EXNEXT_SHARE = 60.0

KINDS = ("exall", "exnext")
TESTS = ("stamps", "counter")
SCOPES = ("kind", "any")
CLASSES = ("new", "same_lock", "other_lock", "changed")
LABELS = {"new": "A neu", "same_lock": "B gleiches Lock", "other_lock": "C anderes Lock",
          "changed": "D Volume geändert"}


def read_counters(path):
    values = {}
    for number, line in enumerate(Path(path).read_text(encoding="ascii").splitlines(), 1):
        if not line.strip():
            continue
        key, separator, value = line.partition("\t")
        if not separator or not value.strip().isdigit():
            raise SystemExit(f"{path}:{number}: keine Zeile 'Schlüssel<TAB>Zahl': {line!r}")
        values[key] = int(value)
    if values.get("version") != 1:
        raise SystemExit("Die Datei stammt nicht vom Hit-Rate-Zähler (Version 1).")
    return values


def table(values, test, scope):
    """starts and entries by kind and class for one validity test and scope."""
    return {kind: {cls: (values[f"starts.{kind}.{test}.{scope}.{cls}"],
                         values[f"entries.{kind}.{test}.{scope}.{cls}"]) for cls in CLASSES}
            for kind in KINDS}


def percent(part, whole):
    return 100.0 * part / whole if whole else 0.0


def evaluate(values):
    primary = table(values, "stamps", "kind")
    exall_entries = sum(e for _, e in primary["exall"].values())
    exnext_entries = sum(e for _, e in primary["exnext"].values())
    all_entries = exall_entries + exnext_entries
    starts = sum(s for kind in KINDS for s, _ in primary[kind].values())
    b = primary["exall"]["same_lock"][1]
    c = primary["exall"]["other_lock"][1]
    d = primary["exall"]["changed"][1]
    reading = {
        "entries": all_entries, "exall_entries": exall_entries, "exnext_entries": exnext_entries,
        "starts": starts,
        "enough_data": all_entries >= MIN_ENTRIES and starts >= MIN_STARTS,
        "b_share": percent(b, all_entries), "bc_share": percent(b + c, all_entries),
        "d_share_of_exall": percent(d, exall_entries),
        "exnext_share": percent(exnext_entries, all_entries)}
    if not reading["enough_data"]:
        reading["result"] = "keine Entscheidung"
    elif reading["b_share"] >= LOCK_OWNED_SHARE:
        reading["result"] = "weiter: zweiter Plan, Cache am Lock"
    elif reading["bc_share"] >= DIRECTORY_LEVEL_SHARE:
        reading["result"] = "weiter: Verzeichnis-Cache planen (überlebt UnLock)"
    else:
        reading["result"] = "stoppen: ein Ergebnis-Cache lohnt sich in dieser Nutzung nicht"
    reading["notes"] = []
    if reading["enough_data"] and reading["d_share_of_exall"] >= CHANGED_SHARE_OF_EXALL:
        reading["notes"].append(
            "Klasse D hält mindestens %.0f %% der ExAll-Einträge: die volumenweiten Stempel "
            "beenden die Gültigkeit oft, ein Zähler je Verzeichnis (offene Frage 2) "
            "ansehen, bevor das Ergebnis endgültig ist." % CHANGED_SHARE_OF_EXALL)
    if reading["enough_data"] and reading["exnext_share"] >= EXNEXT_SHARE:
        reading["notes"].append(
            "ExNext liefert mindestens %.0f %% aller Einträge: ein ExAll-Cache bedient die "
            "Minderheit, ExNext ist der Ort zum Hinsehen." % EXNEXT_SHARE)
    return reading


def print_table(values, test, scope, title):
    t = table(values, test, scope)
    all_entries = sum(e for kind in KINDS for _, e in t[kind].values())
    print(title)
    print(f"{'Klasse':22} {'ExAll Starts':>12} {'ExAll Einträge':>15} {'ExNext Starts':>14} "
          f"{'ExNext Einträge':>16} {'Anteil ExAll':>13} {'Anteil ExNext':>14}")
    for cls in CLASSES:
        (sa, ea), (sn, en) = t["exall"][cls], t["exnext"][cls]
        print(f"{LABELS[cls]:22} {sa:12d} {ea:15d} {sn:14d} {en:16d} "
              f"{percent(ea, all_entries):12.1f}% {percent(en, all_entries):13.1f}%")
    print()


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("counters", help="Datei, die `hitrate BFS:` geschrieben hat")
    parser.add_argument("--json", help="Ergebnis zusätzlich als JSON schreiben")
    arguments = parser.parse_args()
    values = read_counters(arguments.counters)
    reading = evaluate(values)

    print("Hit-Rate-Messung der ExAll- und ExNext-Starts")
    print("=" * 60)
    print_table(values, "stamps", "kind",
                "Hauptzählung (Gültigkeit nach Baumstempeln, Vergleich mit der letzten Runde "
                "derselben Art)")
    print(f"Aufgelistete Einträge: {reading['entries']} (ExAll {reading['exall_entries']}, "
          f"ExNext {reading['exnext_entries']}), Starts: {reading['starts']}")
    print(f"ExAll-Aufrufe: {values['calls.exall']}, ExNext-Aufrufe: {values['calls.exnext']}, "
          f"Runden mit Muster: {values['pattern_starts']}")
    print(f"Runden vollständig/abgebrochen/während Änderung: "
          f"ExAll {values['passes_complete.exall']}/{values['passes_abandoned.exall']}/"
          f"{values['passes_changed.exall']}, "
          f"ExNext {values['passes_complete.exnext']}/{values['passes_abandoned.exnext']}/"
          f"{values['passes_changed.exnext']}")
    print(f"Verzeichnisse in der Tabelle: {values['directories']}, "
          f"Verdrängungen: {values['evictions']}, Einträge ohne gezählten Start: "
          f"{values['orphan_entries.exall'] + values['orphan_entries.exnext']}, "
          f"Paketzähler: {values['volume_changes']}")
    if values["evictions"]:
        print("HINWEIS: Die Tabelle war voll, Verzeichnisse wurden verdrängt. Klasse A ist "
              "zu hoch gezählt.")
    print()
    print("Entscheidungsregel (Schwellen aus dem Plan, von Fabian am 23.10.2026 unverändert angenommen)")
    print(f"  Genug Daten (mind. {MIN_ENTRIES} Einträge und {MIN_STARTS} Starts): "
          f"{'ja' if reading['enough_data'] else 'NEIN'}")
    print(f"  Regel 2, Cache am Lock: ExAll-Einträge der Klasse B = {reading['b_share']:.1f} % "
          f"aller Einträge (Schwelle {LOCK_OWNED_SHARE:.0f} %)")
    print(f"  Regel 3, Verzeichnis-Cache: Klassen B und C = {reading['bc_share']:.1f} % "
          f"(Schwelle {DIRECTORY_LEVEL_SHARE:.0f} %)")
    print(f"  Klasse D = {reading['d_share_of_exall']:.1f} % der ExAll-Einträge, ExNext = "
          f"{reading['exnext_share']:.1f} % aller Einträge")
    print(f"  Ergebnis: {reading['result']}")
    for note in reading["notes"]:
        print(f"  Hinweis: {note}")
    print()
    print_table(values, "stamps", "any", "Zum Vergleich: Baumstempel, Vergleich mit der letzten "
                "Runde beider Arten (ein Cache, den auch ExNext nutzt)")
    print_table(values, "counter", "kind", "Zum Vergleich: Paketzähler des Bound als Gültigkeit "
                "(grober, jedes Schließen einer Datei zählt)")
    print_table(values, "counter", "any", "Zum Vergleich: Paketzähler, beide Arten")
    print("Die Zahlen zeigen die Nutzung einer Person auf einer emulierten Maschine in dieser "
          "Sitzung. Sie zählen Einträge, nicht Zeit, und sind keine Statistik der Amiga-Welt.")
    if arguments.json:
        Path(arguments.json).write_text(json.dumps({"counters": values, "reading": reading},
                                                    indent=1, sort_keys=True) + "\n",
                                        encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
