#!/usr/bin/env python3
"""Run siliscope fixtures once. A negative test must emit its rule id.

Default, when a file has no ``ss-run`` directive:

  ok.*   profile embedded-c, exit 0, ``findings: 0``
  bad.*  profile embedded-c, exit != 0, stdout contains ``[<rule id>]``

The rule id is the parent directory name. A parse failure is a failed test
even when the process exits non-zero.

Extra runs are comments in the fixture::

    /* ss-run: expect=clean */
    /* ss-run: profile=strict expect=ss.ctrl.no-continue */
    /* ss-run: allow=ss.fn.no-stdarg:log_printf expect=clean */
    /* ss-run: extra=-ffreestanding expect=clean */
    /* ss-run: extra=@arm-cxx expect=ss.cpp.no-heap-stl */
    /* ss-run: also=other.c expect=ss.ctrl.no-recursion */

``expect=clean`` requires exit 0 and no findings. Any other expect value is
the diagnostic id that must appear in brackets. ``extra=@arm-cxx`` expands to
the ``arm-none-eabi`` libstdc++ include, its sysroot, and Clang's resource
directory, so a fixture can include ``<vector>`` without a version pinned in
the file. ``also=`` is another translation unit, a path relative to the
fixture. A companion file starts with ``/* ss-companion */`` and is not a
case of its own.
"""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from threading import Lock

ROOT = Path(__file__).resolve().parents[1]
CHECKS = ROOT / "tests" / "lit" / "checks"
PROBE = ROOT / "tests" / "lit" / "frontend" / "isr_attr.c"
RUN_RE = re.compile(r"ss-run:([^\n]*)")
_arm_cxx_cache: list[str] | None = None
_arm_cxx_lock = Lock()


def _version_key(name: str) -> tuple[int, ...]:
    parts: list[int] = []
    for piece in name.split("."):
        parts.append(int(piece) if piece.isdigit() else 0)
    return tuple(parts)


def discover_arm_cxx() -> list[str]:
    """-extra-arg tokens that make <vector> resolve for --target arm-none-eabi."""
    gxx = shutil.which("arm-none-eabi-g++")
    if not gxx:
        raise RuntimeError("arm-none-eabi-g++ is not on PATH")
    prefix = Path(gxx).resolve().parents[1]
    include = prefix / "arm-none-eabi" / "include" / "c++"
    versions = []
    if include.is_dir():
        versions = [p for p in include.iterdir() if (p / "cstdint").is_file()]
    if not versions:
        raise RuntimeError(f"no libstdc++ cstdint under {include}")
    root = sorted(versions, key=lambda p: _version_key(p.name))[-1]

    args: list[str] = []
    sysroot = prefix / "arm-none-eabi"
    if sysroot.is_dir():
        args.append(f"--sysroot={sysroot}")
    clang = shutil.which("clang")
    if not clang:
        raise RuntimeError("clang is not on PATH")
    resource = subprocess.check_output([clang, "-print-resource-dir"], text=True).strip()
    stddef = Path(resource) / "include" / "stddef.h"
    if not stddef.is_file():
        raise RuntimeError(f"Clang resource dir has no stddef.h: {resource}")
    args.append(f"-resource-dir={resource}")

    def add_isystem(path: Path) -> None:
        if path.is_dir():
            args.append("-isystem")
            args.append(str(path))

    add_isystem(root)
    bits = root / "arm-none-eabi"
    if (bits / "bits" / "c++config.h").is_file():
        add_isystem(bits)
    add_isystem(root / "backward")
    return args


def arm_cxx_args() -> list[str]:
    global _arm_cxx_cache
    with _arm_cxx_lock:
        if _arm_cxx_cache is None:
            _arm_cxx_cache = discover_arm_cxx()
        return list(_arm_cxx_cache)


def parse_runs(path: Path, rule_id: str) -> list[dict]:
    text = path.read_text(encoding="utf-8")
    found: list[dict] = []
    for raw in RUN_RE.findall(text):
        run = {"profile": "embedded-c", "expect": "", "allow": [], "extra": [], "also": []}
        for tok in raw.replace("*/", " ").split():
            if "=" not in tok:
                continue
            key, val = tok.split("=", 1)
            if key == "profile":
                run["profile"] = val
            elif key == "expect":
                run["expect"] = val
            elif key == "allow":
                run["allow"].append(val)
            elif key == "extra":
                run["extra"].append(val)
            elif key == "also":
                run["also"].append(val)
            else:
                raise SystemExit(f"{path}: unknown ss-run key {key}")
        if not run["expect"]:
            raise SystemExit(f"{path}: ss-run needs expect=")
        found.append(run)
    if found:
        return found
    stem = path.stem
    if stem.startswith("ok"):
        return [{"profile": "embedded-c", "expect": "clean", "allow": [], "extra": []}]
    if stem.startswith("bad"):
        return [{"profile": "embedded-c", "expect": rule_id, "allow": [], "extra": []}]
    raise SystemExit(f"{path}: no ss-run directive and name is not ok*/bad*")


def findings_of(out: str) -> int | None:
    for line in out.splitlines():
        if line.startswith("findings: "):
            try:
                return int(line.split()[1])
            except (IndexError, ValueError):
                return None
    return None


def header_int(text: str, key: str) -> int:
    m = re.search(rf"^{re.escape(key)}: (\d+)$", text, re.M)
    if not m:
        raise AssertionError(f"missing {key}: line")
    return int(m.group(1))


def list_rows(text: str) -> dict[str, list[str]]:
    rows: dict[str, list[str]] = {}
    for line in text.splitlines():
        parts = line.split("\t")
        if len(parts) >= 4 and parts[0].startswith("ss."):
            rows[parts[0]] = parts
    return rows


class Runner:
    def __init__(self, binary: str) -> None:
        self.binary = binary
        self.failures: list[str] = []
        self.passed = 0
        self.lock = Lock()

    def invoke(self, args: list[str]) -> tuple[int, str, str]:
        proc = subprocess.run(
            [self.binary, *args],
            cwd=ROOT,
            text=True,
            capture_output=True,
        )
        return proc.returncode, proc.stdout, proc.stderr

    def check(self, name: str, ok: bool, detail: str) -> None:
        with self.lock:
            if ok:
                self.passed += 1
                return
            self.failures.append(f"{name}\n{detail.rstrip()}")

    def run_case(self, path: Path, rule_id: str, run: dict) -> None:
        args = ["--ruleset-dir", "ruleset", "--profile", run["profile"], "--target", "arm-none-eabi"]
        for spec in run["allow"]:
            args.extend(["--allow", spec])
        label = f"{path.relative_to(ROOT)} profile={run['profile']} expect={run['expect']}"
        for arg in run.get("extra", []):
            if arg == "@arm-cxx":
                try:
                    expanded = arm_cxx_args()
                except RuntimeError as exc:
                    self.check(label, False, str(exc))
                    return
                for token in expanded:
                    args.extend(["-extra-arg", token])
            else:
                args.extend(["-extra-arg", arg])
        args.append(str(path.relative_to(ROOT)))
        for name in run.get("also", []):
            extra = path.parent / name
            if not extra.is_file():
                self.check(label, False, f"missing also={name}")
                return
            args.append(str(extra.relative_to(ROOT)))
        rc, out, err = self.invoke(args)
        findings = findings_of(out)
        if run["expect"] == "clean":
            good = rc == 0 and findings == 0
            detail = f"rc={rc} findings={findings}\nstdout:\n{out}\nstderr:\n{err}"
            self.check(label, good, detail)
            return
        marker = f"[{run['expect']}]"
        good = rc != 0 and findings is not None and findings > 0 and marker in out
        detail = f"rc={rc} findings={findings} marker={marker in out}\nstdout:\n{out}\nstderr:\n{err}"
        self.check(label, good, detail)

    def check_list(self, profile: str) -> tuple[dict[str, list[str]], str]:
        rc, out, err = self.invoke(["--ruleset-dir", "ruleset", "--profile", profile, "--list"])
        self.check(
            f"--list {profile} runs",
            rc == 0 and out.startswith(f"profile: {profile}\n"),
            f"rc={rc}\nstdout:\n{out}\nstderr:\n{err}",
        )
        if rc != 0:
            return {}, out
        rows = list_rows(out)
        try:
            enabled = header_int(out, "enabled")
            with_checker = header_int(out, "with-checker")
            no_checker = header_int(out, "no-checker")
        except AssertionError as exc:
            self.check(f"--list {profile} header", False, str(exc) + "\n" + out)
            return rows, out
        got_with = sum(1 for parts in rows.values() if parts[3] == "checker")
        got_without = sum(1 for parts in rows.values() if parts[3] == "no-checker")
        self.check(
            f"--list {profile} counts",
            enabled == len(rows) and with_checker == got_with and no_checker == got_without,
            f"enabled {enabled} rows {len(rows)} with {with_checker}/{got_with} "
            f"without {no_checker}/{got_without}",
        )
        return rows, out

    def check_profiles(self) -> None:
        c_rows, c_out = self.check_list("embedded-c")
        cpp_rows, cpp_out = self.check_list("embedded-cpp")
        strict_rows, strict_out = self.check_list("strict")
        style_rows, style_out = self.check_list("style")

        def has_lang(text: str, langs: str) -> bool:
            return f"\nlanguages: {langs}\n" in f"\n{text}"

        self.check("embedded-c languages", has_lang(c_out, "c"), c_out)
        self.check("embedded-cpp languages", has_lang(cpp_out, "cpp"), cpp_out)
        self.check("strict languages", has_lang(strict_out, "c cpp"), strict_out)

        def absent(rows: dict, rid: str) -> bool:
            return rid not in rows

        def present(rows: dict, rid: str, status: str | None = None) -> bool:
            if rid not in rows:
                return False
            return status is None or rows[rid][3] == status

        self.check(
            "embedded-c keeps shared rules",
            present(c_rows, "ss.ctrl.no-goto", "checker"),
            "ss.ctrl.no-goto missing",
        )
        for rid in ("ss.cpp.no-exceptions", "ss.cpp.nullptr", "ss.ctrl.no-continue"):
            self.check(f"embedded-c drops {rid}", absent(c_rows, rid), f"{rid} is enabled")
        self.check(
            "embedded-cpp keeps C++ rule",
            present(cpp_rows, "ss.cpp.no-exceptions", "checker"),
            "ss.cpp.no-exceptions missing",
        )
        self.check(
            "embedded-cpp still has rules without a checker",
            present(cpp_rows, "ss.conv.signed-unsigned-mix", "no-checker"),
            "ss.conv.signed-unsigned-mix missing",
        )
        self.check(
            "embedded-cpp keeps shared checker",
            present(cpp_rows, "ss.ctrl.no-goto", "checker"),
            "ss.ctrl.no-goto missing",
        )
        for rid in (
            "ss.mem.no-vla",
            "ss.mem.no-flexible-array",
            "ss.fn.prototype",
            "ss.fn.static-internal",
            "ss.pre.source-includes-own-header",
        ):
            self.check(f"embedded-cpp drops {rid}", absent(cpp_rows, rid), f"{rid} is enabled")
        c_only = {
            "ss.mem.no-vla",
            "ss.mem.no-flexible-array",
            "ss.fn.prototype",
            "ss.fn.static-internal",
            "ss.pre.source-includes-own-header",
        }
        cpp_only = {
            "ss.cpp.enum-class",
            "ss.cpp.init-members",
            "ss.cpp.no-cstyle-cast",
            "ss.cpp.no-default-args",
            "ss.cpp.no-exceptions",
            "ss.cpp.no-friend",
            "ss.cpp.no-heap-stl",
            "ss.cpp.no-implicit-conversion",
            "ss.cpp.no-move-const",
            "ss.cpp.no-rtti",
            "ss.cpp.no-throw-dtor",
            "ss.cpp.no-throw-spec",
            "ss.cpp.no-throwing-swap",
            "ss.cpp.no-using-directive",
            "ss.cpp.no-using-in-header",
            "ss.cpp.no-vector-bool",
            "ss.cpp.nullptr",
            "ss.cpp.override",
            "ss.cpp.private-data",
            "ss.cpp.special-members",
            "ss.cpp.virtual-dtor",
        }
        c_checkers = {rid for rid, parts in c_rows.items() if parts[3] == "checker"}
        cpp_checkers = {rid for rid, parts in cpp_rows.items() if parts[3] == "checker"}
        self.check(
            "embedded-cpp checker set is embedded-c minus C-only plus C++",
            cpp_checkers == (c_checkers - c_only) | cpp_only,
            f"only in C: {sorted(c_checkers - cpp_checkers)}\n"
            f"only in C++: {sorted(cpp_checkers - c_checkers)}",
        )
        self.check(
            "strict keeps inherited C++ rule",
            present(strict_rows, "ss.cpp.no-exceptions", "checker"),
            "ss.cpp.no-exceptions missing",
        )
        self.check(
            "strict enables no-continue",
            present(strict_rows, "ss.ctrl.no-continue", "checker"),
            "ss.ctrl.no-continue missing",
        )
        self.check(
            "style is naming only",
            present(style_rows, "ss.style.line-width", "no-checker") and absent(style_rows, "ss.ctrl.no-goto"),
            "style profile rule set",
        )

        rc, out, err = self.invoke(
            [
                "--ruleset-dir",
                "ruleset",
                "--profile",
                "style",
                "--target",
                "arm-none-eabi",
                "tests/lit/checks/ss.ctrl.no-goto/bad.c",
            ]
        )
        self.check("style ignores goto", rc == 0 and findings_of(out) == 0, f"rc={rc}\n{out}\n{err}")

        rc, out, err = self.invoke(
            [
                "--ruleset-dir",
                "ruleset",
                "--profile",
                "embedded-cpp",
                "--target",
                "arm-none-eabi",
                "tests/lit/checks/ss.mem.no-vla/bad.c",
            ]
        )
        self.check("embedded-cpp ignores VLA", rc == 0 and findings_of(out) == 0, f"rc={rc}\n{out}\n{err}")

        rc, out, err = self.invoke(
            [
                "--ruleset-dir",
                "ruleset",
                "--profile",
                "strict",
                "--target",
                "arm-none-eabi",
                "tests/lit/checks/ss.ctrl.no-goto/bad.c",
            ]
        )
        self.check(
            "strict flags goto",
            rc != 0 and "[ss.ctrl.no-goto]" in out,
            f"rc={rc}\n{out}\n{err}",
        )

        rc, out, err = self.invoke(
            ["--ruleset-dir", "ruleset", "--profile", "nosuch", "--list"]
        )
        self.check(
            "unknown profile",
            rc != 0 and "unknown profile" in err,
            f"rc={rc}\n{out}\n{err}",
        )

    def check_probe(self) -> None:
        rc, out, err = self.invoke(
            ["--ruleset-dir", "ruleset", "--probe", "--target", "arm-none-eabi", str(PROBE.relative_to(ROOT))]
        )

        def field(name: str) -> int | None:
            for line in out.splitlines():
                if line.startswith(name + ": "):
                    try:
                        return int(line.split()[1])
                    except (IndexError, ValueError):
                        return None
            return None

        good = (
            rc == 0
            and field("functions") == 1
            and field("interrupt") == 1
            and field("packed") == 1
            and findings_of(out) == 0
        )
        self.check("probe isr_attr", good, f"rc={rc}\n{out}\n{err}")


def collect_cases(filters: list[str]) -> list[tuple[Path, str, dict]]:
    cases: list[tuple[Path, str, dict]] = []
    for directory in sorted(p for p in CHECKS.iterdir() if p.is_dir()):
        rule_id = directory.name
        for path in sorted(directory.iterdir()):
            if path.suffix not in {".c", ".cpp"}:
                continue
            with path.open(encoding="utf-8") as handle:
                if handle.readline().startswith("/* ss-companion"):
                    continue
            if filters and not any(f in rule_id or f in path.name for f in filters):
                continue
            for run in parse_runs(path, rule_id):
                cases.append((path, rule_id, run))
    return cases


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: run_checks.py <siliscope> [rule-id...]", file=sys.stderr)
        return 2
    binary = sys.argv[1]
    filters = sys.argv[2:]
    runner = Runner(binary)
    rc, out, err = runner.invoke(["--version"])
    if "Clang LibTooling" not in out:
        print("siliscope has no LibTooling frontend.\n" + out + err, file=sys.stderr)
        print("Configure with: just build-clang <llvm>/lib/cmake/llvm <llvm>/lib/cmake/clang", file=sys.stderr)
        return 2

    cases = collect_cases(filters)
    with ThreadPoolExecutor(max_workers=8) as pool:
        list(pool.map(lambda item: runner.run_case(*item), cases))
    if not filters:
        runner.check_profiles()
        runner.check_probe()
    if runner.failures:
        print(f"{runner.passed} passed, {len(runner.failures)} failed")
        for item in runner.failures:
            print("---")
            print(item)
        return 1
    print(f"ok: {runner.passed} cases")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
