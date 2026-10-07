#!/usr/bin/env python3
"""Show how two siliscope reports differ.

The syntax snapshot before dataflow is a saved log, not a suppression file.
This compares two logs and prints only the delta: added and removed
diagnostics, grouped by rule, with a few sample lines. A finding is the
whole diagnostic, so a moved column or a rewritten message shows up.

  python3 tools/diff_report.py previous.txt current.txt

Summary lines (``findings:`` and ``no-checker:``) are checked against the
parsed diagnostics and are not themselves findings. The exit status is 0
when both logs parse, including when the delta is not zero.
"""

from __future__ import annotations

import re
import sys
from collections import Counter
from pathlib import Path

DIAG = re.compile(
    r"^(?P<file>.*):(?P<line>\d+):(?P<col>\d+): "
    r"(?P<sev>error|warning|advisory|style): "
    r"(?P<msg>.*) \[(?P<id>ss\.[A-Za-z0-9._-]+)\]$"
)
FINDINGS = re.compile(r"^findings: (\d+)\s*$")
NO_CHECKER = re.compile(r"^no-checker: (\d+)\s*$")

SAMPLES = 4
TOP_FILES = 8
Key = tuple[str, str, str, str, str, str]


class Report:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.findings: list[tuple[str, Key]] = []
        self.stated: int | None = None
        self.no_checker: int | None = None
        self.other: list[str] = []

    def parse(self) -> None:
        text = self.path.read_text(errors="replace")
        for raw in text.splitlines():
            line = raw.rstrip("\r")
            if not line:
                continue
            found = FINDINGS.fullmatch(line)
            if found:
                self.stated = int(found.group(1))
                continue
            missing = NO_CHECKER.fullmatch(line)
            if missing:
                self.no_checker = int(missing.group(1))
                continue
            diag = DIAG.fullmatch(line)
            if not diag:
                self.other.append(line)
                continue
            key = (
                diag.group("file"),
                diag.group("line"),
                diag.group("col"),
                diag.group("sev"),
                diag.group("msg"),
                diag.group("id"),
            )
            self.findings.append((line, key))


def rule_of(line: str) -> str:
    match = DIAG.fullmatch(line)
    return match.group("id") if match else "?"


def message_of(line: str) -> str:
    match = DIAG.fullmatch(line)
    return match.group("msg") if match else line


def file_of(line: str) -> str:
    match = DIAG.fullmatch(line)
    if not match:
        return line
    path = match.group("file")
    marker = "pump_station_v3/"
    at = path.find(marker)
    return path[at + len(marker) :] if at >= 0 else path


def print_groups(title: str, lines: list[str]) -> None:
    if not lines:
        print(f"{title}: 0")
        return
    print(f"{title}: {len(lines)}")
    by_rule: dict[str, list[str]] = {}
    for line in lines:
        by_rule.setdefault(rule_of(line), []).append(line)
    rules = sorted(by_rule, key=lambda rid: (-len(by_rule[rid]), rid))
    for rid in rules:
        group = by_rule[rid]
        print(f"  {len(group):6d}  {rid}")
        messages = Counter(message_of(line) for line in group)
        for msg, count in messages.most_common():
            print(f"  {count:6d}    {msg}")
    files = Counter(file_of(line) for line in lines)
    print("top files:")
    for path, count in files.most_common(TOP_FILES):
        print(f"  {count:6d}  {path}")
    print("samples:")
    for line in lines[:SAMPLES]:
        print(f"  {line}")


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print("usage: diff_report.py <previous> <current>", file=sys.stderr)
        return 2
    old_path = Path(argv[1])
    new_path = Path(argv[2])
    missing = [str(path) for path in (old_path, new_path) if not path.is_file()]
    if missing:
        print("missing log: " + ", ".join(missing), file=sys.stderr)
        return 2
    old = Report(old_path)
    new = Report(new_path)
    old.parse()
    new.parse()
    broken = False
    for report in (old, new):
        if report.other:
            broken = True
            print(f"{report.path}: {len(report.other)} unrecognized lines", file=sys.stderr)
            for line in report.other[:3]:
                print(f"  {line}", file=sys.stderr)
        if report.stated is not None and report.stated != len(report.findings):
            broken = True
            print(
                f"{report.path}: findings: {report.stated} but parsed {len(report.findings)}",
                file=sys.stderr,
            )
    if broken:
        return 1
    added, removed = lines_for_delta(old.findings, new.findings)
    old_nc = " ?" if old.no_checker is None else f" {old.no_checker}"
    new_nc = " ?" if new.no_checker is None else f" {new.no_checker}"
    print(f"previous: {len(old.findings)} findings, no-checker{old_nc}  {old.path}")
    print(f"current:  {len(new.findings)} findings, no-checker{new_nc}  {new.path}")
    print(f"delta: {len(new.findings) - len(old.findings):+d}")
    print_groups("added", added)
    print_groups("removed", removed)
    return 0


def lines_for_delta(
    old: list[tuple[str, Key]], new: list[tuple[str, Key]]
) -> tuple[list[str], list[str]]:
    old_count = Counter(key for _, key in old)
    new_count = Counter(key for _, key in new)
    added: list[str] = []
    removed: list[str] = []
    seen_new: Counter[str] = Counter()
    for line, key in new:
        seen_new[key] += 1
        if seen_new[key] > old_count[key]:
            added.append(line)
    seen_old: Counter[str] = Counter()
    for line, key in old:
        seen_old[key] += 1
        if seen_old[key] > new_count[key]:
            removed.append(line)
    return added, removed


if __name__ == "__main__":
    sys.exit(main(sys.argv))
