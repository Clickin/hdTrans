#!/usr/bin/env python3
"""Attribute judged-bad spans to the current CLI's word/ending choices (-why).

Usage: tools/attribute.py --verdicts FILE.tsv [FILE.tsv ...] --chapters FILE.txt
       [FILE.txt ...] --cli CLI --dicts DIR --output OUT.tsv
Verdict columns: line, ja, out, verdict, cause, span, fix (optional header).
A verdict other than 'ok' is bad. Chapter and verdict basenames must match,
e.g. sample.ja.txt and sample.verdicts.tsv. Line numbers include a leading #
header. Pairwise judge.py output has no spans and must first be annotated into
this format. Only still-identical source/output lines are attributed.
"""
import argparse
import csv
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def chapter_name(path):
    for suffix in (".verdicts.tsv", ".ja.txt", ".txt", ".tsv"):
        if path.name.endswith(suffix):
            return path.name[:-len(suffix)]
    return path.stem


def explain(cli, dicts, text):
    with tempfile.TemporaryDirectory(prefix="hdtrans-attribute-") as temp:
        home = Path(temp)
        source = home / "input.txt"
        source.write_text(text, encoding="utf-8")
        env = os.environ.copy()
        env.update(HOME=temp, XDG_CACHE_HOME=str(home / "xdg"),
                   LOCALAPPDATA=str(home / "local"), HDTRANS_CACHE_DIR=str(home / "cache"))
        for name in ("HDTRANS_SNAPSHOT", "HDTRANS_VERIFY_DICTS", "HDTRANS_DRIVER_DUMP"):
            env.pop(name, None)
        result = subprocess.run([str(cli), "-dicts", str(dicts), "-why", "-in", str(source)],
                                capture_output=True, text=True, encoding="utf-8", env=env, check=True)
    why = {}
    for line in result.stderr.splitlines():
        fields = line.split("\t")
        if fields[0] == "WHY" and len(fields) >= 17:
            why.setdefault(int(fields[1]), []).append(fields)
    return result.stdout.split("\n"), why


def owners_for(ja, span, words):
    if not span or span not in ja:
        return ""
    spans = []
    start = ja.find(span)
    while start >= 0:
        spans.append((start, start + len(span)))
        start = ja.find(span, start + 1)
    owners, position = [], 0
    for fields in words:
        surface = fields[5]
        if not surface:
            continue
        start = ja.find(surface, position)
        if start < 0:
            continue
        position = start + len(surface)
        if any(start < end and position > begin for begin, end in spans):
            layer = fields[13] or "defaults"
            rule = fields[14] or fields[7] + ":" + fields[8]
            owner = f"{layer}:{rule}:{surface}:{fields[9]}"
            if fields[16]:
                owner += f"+end={fields[15]}:{fields[16]}"
            owners.append(owner)
    return "|".join(owners)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verdicts", nargs="+", required=True, type=Path)
    parser.add_argument("--chapters", nargs="+", required=True, type=Path)
    parser.add_argument("--cli", required=True, type=Path)
    parser.add_argument("--dicts", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    chapters = {}
    for path in args.chapters:
        name = chapter_name(path)
        if name in chapters:
            parser.error(f"duplicate chapter name: {name}")
        chapters[name] = path
    inputs = args.chapters + args.verdicts
    if args.output.resolve() in {p.resolve() for p in inputs}:
        parser.error("output must not overwrite an input")
    stats = {"bad": 0, "same": 0, "changed": 0, "nospan": 0}
    with args.output.open("w", encoding="utf-8", newline="") as output:
        writer = csv.writer(output, delimiter="\t", lineterminator="\n")
        writer.writerow(("chapter", "line", "cause", "span", "fix", "ja", "out", "owners"))
        for verdict_path in sorted(args.verdicts):
            name = chapter_name(verdict_path)
            if name not in chapters:
                parser.error(f"no chapter for {verdict_path.name}")
            with verdict_path.open(encoding="utf-8", newline="") as source:
                rows = list(csv.reader(source, delimiter="\t"))
            if rows and rows[0][0] == "line":
                rows = rows[1:]
            if any(len(row) < 7 or not row[0].isdigit() or int(row[0]) < 1 for row in rows):
                parser.error(f"{verdict_path}: expected line, ja, out, verdict, cause, span, fix")
            bad = [row for row in rows if row[3] != "ok"]
            if not bad:
                continue
            text = chapters[name].read_text(encoding="utf-8")
            offset = 1 if text.startswith("#") else 0
            if offset:
                text = text.partition("\n")[2]
            ja_lines = text.split("\n")
            translated, why = explain(args.cli.resolve(), args.dicts.resolve(), text)
            for row in bad:
                stats["bad"] += 1
                number = int(row[0])
                index = number - 1 - offset
                ja, old, cause, span, fix = row[1], row[2], row[4], row[5], row[6]
                if (not 0 <= index < min(len(translated), len(ja_lines))
                        or ja_lines[index] != ja or translated[index].strip() != old.strip()):
                    stats["changed"] += 1
                    continue
                stats["same"] += 1
                if not span or span not in ja:
                    stats["nospan"] += 1
                owners = owners_for(ja, span, why.get(index + 1, []))
                writer.writerow((name, number, cause, span, fix, ja, translated[index], owners))
    print(stats, file=sys.stderr)


if __name__ == "__main__":
    main()
