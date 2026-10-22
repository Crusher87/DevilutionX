#!/usr/bin/env python3
"""Reads the Skill Lab's notes (poe2_logs/skill_lab_notes.tsv) and lists what to work on.

The lab writes one line per change (Source/poe2/skill_lab_notes.cpp): a skill's status (not looked at, needs work,
approved) or a note for Claude, which may be pinned to a moment of the cast ("mark tick" is game ticks since the cast
began, 20 a second at normal speed, so the same moment at any lab speed). Each line also says how the lab was set up
then: class, weapon, gem level, floor, targets and speed.

This prints every skill that has notes or a status, needs work first, with its notes in order. Notes written before
the skill's latest "approved" are left out unless --all is given, since they were dealt with. It then lists the looks the
player changed with the lab's sliders (poe2_logs/skill_looks_mine.tsv): fold those into
mods/poe2/txtdata/poe2/skill_looks.tsv when they're settled. Last it lists the lab's recordings
(poe2_logs/skill_lab_clips/<skill>_<time>/frame_NNN.png, one picture per game tick of the cast).

Usage:
  python3 tools/skill_lab_notes.py [log folder] [--all] [--since YYYY-MM-DD] [--skill NAME]

The folder defaults to poe2_logs beside this script's folder (the game folder's tools/ and poe2_logs/).
"""
import argparse
import collections
import csv
import os
import sys

STATUS_ORDER = {"needs work": 0, "not looked at": 1, "approved": 2}


def read_lines(path):
    with open(path, encoding="utf-8", errors="replace", newline="") as f:
        return list(csv.DictReader(f, delimiter="\t", quoting=csv.QUOTE_NONE))


def setup(row):
    targets = row.get("targets", "")
    parts = [row.get("class", ""), row.get("weapon", ""), f"gem level {row.get('gem level', '')}", row.get("floor", ""), targets]
    if row.get("speed", "1x") != "1x":
        parts.append(f"speed {row['speed']}")
    return ", ".join(p for p in parts if p)


def print_looks(path):
    """The player's own looks, only the numbers that differ from plain."""
    if not os.path.exists(path):
        return
    plain = {"size": "1.00", "amount": "1.00", "length": "1.00", "brightness": "1.00", "speed": "1.00", "shake": "1.00", "tint": "0.00"}
    rows = read_lines(path)
    if not rows:
        return
    print(f"Looks changed in the Skill Lab ({len(rows)}):")
    for row in rows:
        changes = [f"{k} {row.get(k)}" for k, v in plain.items() if row.get(k, v) != v]
        if row.get("tint", "0.00") != "0.00":
            changes.append(f"colour #{row.get('colour', '')}")
        print(f"  {row.get('name') or row.get('key')} [{row.get('key')}]: {', '.join(changes) or 'plain'}")
    print()


def print_clips(folder):
    """The lab's recordings, newest last, with how many frames each has."""
    if not os.path.isdir(folder):
        return
    clips = sorted(d for d in os.listdir(folder) if os.path.isdir(os.path.join(folder, d)))
    if not clips:
        return
    print(f"Skill Lab recordings ({len(clips)}):")
    for clip in clips:
        frames = len([f for f in os.listdir(os.path.join(folder, clip)) if f.endswith(".png")])
        print(f"  {clip}: {frames} frames")
    print()


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folder", nargs="?", default=os.path.join(os.path.dirname(here), "poe2_logs"))
    parser.add_argument("--all", action="store_true", help="also list notes from before the latest approval")
    parser.add_argument("--since", help="only lines on or after this date (YYYY-MM-DD)")
    parser.add_argument("--skill", help="only this skill (any part of its name)")
    args = parser.parse_args()

    path = os.path.join(args.folder, "skill_lab_notes.tsv")
    print_looks(os.path.join(args.folder, "skill_looks_mine.tsv"))
    print_clips(os.path.join(args.folder, "skill_lab_clips"))
    if not os.path.exists(path):
        print(f"No Skill Lab notes yet ({path} not found).")
        return 0

    skills = collections.OrderedDict()
    for row in read_lines(path):
        name = row.get("skill", "")
        if not name:
            continue
        if args.since and row.get("date", "") < args.since:
            continue
        if args.skill and args.skill.lower() not in name.lower():
            continue
        entry = skills.setdefault(name, {"status": "not looked at", "notes": [], "last": ""})
        entry["last"] = row.get("date", "")
        if row.get("kind") == "status":
            entry["status"] = row.get("status", "not looked at")
            if entry["status"] == "approved" and not args.all:
                entry["notes"] = []
        elif row.get("kind") == "note":
            entry["notes"].append(row)

    if not skills:
        print("No Skill Lab notes match.")
        return 0

    counts = collections.Counter(e["status"] for e in skills.values())
    print(f"Skill Lab notes: {len(skills)} skills ({counts['needs work']} need work, {counts['approved']} approved, "
          f"{counts['not looked at']} not looked at), {sum(len(e['notes']) for e in skills.values())} open notes")
    print()
    for name, entry in sorted(skills.items(), key=lambda kv: (STATUS_ORDER.get(kv[1]["status"], 1), kv[0])):
        print(f"{name}: {entry['status']} (last {entry['last']})")
        for note in entry["notes"]:
            mark = ""
            if note.get("mark tick"):
                mark = f" [at {note['mark seconds']}s, tick {note['mark tick']}, hero frame {note['hero frame']}]"
            print(f"  - {note['note']}{mark}")
            print(f"      {note['date']}; {setup(note)}")
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
