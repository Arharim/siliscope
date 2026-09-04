"""Sanity-check canonical YAML: unique ids, required fields, no third-party catalogs."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RULES = ROOT / "ruleset" / "rules"

REQUIRED = {"id", "title", "severity", "languages", "check", "default", "summary"}
SEVERITY = {"error", "warning", "advisory", "style"}
CHECK = {"syntax", "types", "cfg", "dataflow", "callgraph", "review", "process"}
DEFAULT = {"on", "off", "advisory"}
FORBIDDEN_KEYS = {"sources", "maps_to", "pvs", "misra", "barr", "hicpp"}

# Import the same parser as the indexer.
sys.path.insert(0, str(ROOT / "tools"))
from generate_ruleset_index import parse_simple  # noqa: E402


def main() -> int:
    errors: list[str] = []
    seen: dict[str, Path] = {}
    count = 0
    for path in sorted(RULES.glob("*.yaml")):
        rules = parse_simple(path)
        if not rules:
            errors.append(f"{path.name}: no rules parsed")
            continue
        for r in rules:
            count += 1
            rid = r.get("id", "")
            missing = REQUIRED - r.keys()
            if missing:
                errors.append(f"{path.name} {rid}: missing {sorted(missing)}")
            if rid in seen:
                errors.append(f"duplicate id {rid}: {seen[rid].name} and {path.name}")
            seen[rid] = path
            if r.get("severity") not in SEVERITY:
                errors.append(f"{rid}: bad severity {r.get('severity')!r}")
            if r.get("check") not in CHECK:
                errors.append(f"{rid}: bad check {r.get('check')!r}")
            if r.get("default") not in DEFAULT:
                errors.append(f"{rid}: bad default {r.get('default')!r}")
            if rid and not rid.startswith("ss."):
                errors.append(f"{rid}: id must start with ss.")
            leaked = FORBIDDEN_KEYS & r.keys()
            if leaked:
                errors.append(f"{rid}: third-party catalog field {sorted(leaked)}")
    if errors:
        print(f"{count} rules, {len(errors)} problems:")
        for e in errors:
            print(" -", e)
        return 1
    print(f"ok: {count} rules, unique ids")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
