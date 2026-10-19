#!/usr/bin/env python3
"""Translate chapters with two fixed builds and write changed lines as TSV.

Usage: tools/e2e-pairs.py BASE_CLI BASE_DICTS HEAD_CLI HEAD_DICTS OUT.tsv DIR [DIR ...]
Each .txt chapter is one document; .ko.txt outputs are excluded. A leading # line
is omitted. Keys are folder/relative-path:original-line-number (one-based).
OUT.tsv has key, ja, old, new, without a header; fields use CSV-style TSV quoting.
Each CLI invocation gets a fresh HOME and cache. No chapter copies are retained.
"""
import argparse
import csv
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def translate(cli, dicts, text):
    with tempfile.TemporaryDirectory(prefix="hdtrans-pairs-") as temp:
        home = Path(temp)
        source = home / "input.txt"
        source.write_text(text, encoding="utf-8")
        env = os.environ.copy()
        env.update(HOME=temp, XDG_CACHE_HOME=str(home / "xdg"),
                   LOCALAPPDATA=str(home / "local"), HDTRANS_CACHE_DIR=str(home / "cache"))
        for name in ("HDTRANS_SNAPSHOT", "HDTRANS_VERIFY_DICTS", "HDTRANS_DRIVER_DUMP"):
            env.pop(name, None)
        result = subprocess.run([str(cli), "-dicts", str(dicts), "-in", str(source)],
                                env=env, capture_output=True, text=True, encoding="utf-8", check=True)
        lines = result.stdout.split("\n")
        expected = text.count("\n") + 1
        if len(lines) != expected:
            raise ValueError(f"{cli}: expected {expected} output lines, got {len(lines)}")
        return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base_cli", type=Path)
    parser.add_argument("base_dicts", type=Path)
    parser.add_argument("head_cli", type=Path)
    parser.add_argument("head_dicts", type=Path)
    parser.add_argument("out", type=Path, metavar="OUT.tsv")
    parser.add_argument("folders", nargs="+", type=Path, metavar="DIR")
    args = parser.parse_args()
    cli_a, cli_b = args.base_cli.resolve(), args.head_cli.resolve()
    dicts_a, dicts_b = args.base_dicts.resolve(), args.head_dicts.resolve()
    chapters, keys, seen = [], set(), set()
    for folder in args.folders:
        folder = folder.resolve()
        if not folder.is_dir():
            parser.error(f"not a chapter folder: {folder}")
        for path in sorted(folder.rglob("*.txt")):
            if path.name.endswith(".ko.txt") or path.resolve() in seen:
                continue
            key = folder.name + "/" + path.relative_to(folder).as_posix()
            if key in keys:
                parser.error(f"duplicate chapter key: {key}; use distinct folder names")
            chapters.append((path, key))
            keys.add(key)
            seen.add(path.resolve())
    if not chapters:
        parser.error("no .txt chapters found")
    if args.out.resolve() in seen:
        parser.error("OUT.tsv must not overwrite a chapter")
    changed = 0
    with args.out.open("w", encoding="utf-8", newline="") as output:
        writer = csv.writer(output, delimiter="\t", lineterminator="\n")
        for path, key in chapters:
            text = path.read_text(encoding="utf-8")
            offset = 1
            if text.startswith("#"):
                _, separator, text = text.partition("\n")
                offset = 2
                if not separator:
                    text = ""
            old = translate(cli_a, dicts_a, text)
            new = translate(cli_b, dicts_b, text)
            for number, (ja, before, after) in enumerate(zip(text.split("\n"), old, new), offset):
                if before != after:
                    writer.writerow((f"{key}:{number}", ja, before, after))
                    changed += 1
    print(f"{len(chapters)} chapters, {changed} changed lines", file=sys.stderr)


if __name__ == "__main__":
    main()
