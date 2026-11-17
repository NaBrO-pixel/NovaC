#!/usr/bin/env python3
"""NovaC compiler test runner (NOVAC_COMPILER_ARCHITECTURE.md §12).

Suites (directory -> ncc mode):
  tests/lex    --dump-tokens   stdout compared with <name>.expected
  tests/parse  --dump-ast      stdout compared with <name>.expected
  tests/sema   --emit=typed    stdout compared with <name>.expected
  tests/diag   --check         no stdout; diagnostics only
  tests/regress (mode from a `// MODE:` line, default --check)

In every file, diagnostics are declared inline:
  // ERROR E4010          an error with this code on this line
  // ERROR@+1 E2001       ... on the next line (@-N: N lines above)
  // WARNING W4001        a warning
The run fails on any missing or unexpected diagnostic, on an exit status
other than 1 (errors expected) or 0 (none expected), and on any sanitizer
report. `// ARGS: -Werror` adds command-line arguments.

  --update    rewrite .expected files from the current output (review the diff!)
  --selftest  run the fixtures in tests/selftest, every one of which must FAIL
"""

import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir))

SUITES = {
    "lex": ["--dump-tokens"],
    "parse": ["--dump-ast"],
    "sema": ["--emit=typed"],
    "diag": ["--check"],
    "regress": None,
    "examples": ["--check"],   # the programs in examples/ must stay valid
}
MODES = {
    "tokens": ["--dump-tokens"],
    "ast": ["--dump-ast"],
    "typed": ["--emit=typed"],
    "check": ["--check"],
}
GOLDEN_SUITES = {"lex", "parse", "sema"}

EXPECT_RE = re.compile(r"\b(ERROR|WARNING)(@[+-]\d+)?\s+([EW]\d{4})")
DIAG_RE = re.compile(r"^(?P<file>[^\s:][^:]*):(?P<line>\d+):(?P<col>\d+): (?P<sev>error|warning)\[(?P<code>[EW]\d{4})\]")
SANITIZER_RE = re.compile(r"AddressSanitizer|LeakSanitizer|runtime error:|UndefinedBehaviorSanitizer")


def expectations(text):
    exp = []
    for i, line in enumerate(text.split("\n"), 1):
        idx = line.find("//")
        if idx < 0:
            continue
        for m in EXPECT_RE.finditer(line[idx:]):
            offset = int(m.group(2)[1:]) if m.group(2) else 0
            sev = "error" if m.group(1) == "ERROR" else "warning"
            exp.append((i + offset, sev, m.group(3)))
    return exp


def directive(text, name):
    m = re.search(r"//\s*" + name + r":\s*(.*)", text)
    return m.group(1).split() if m else []


def run_case(ncc, path, suite, update):
    """Returns a list of failure messages (empty = pass)."""
    rel = os.path.relpath(path, ROOT)
    with open(path, encoding="utf-8", errors="surrogateescape") as f:
        text = f.read()
    if SUITES.get(suite) is not None:
        mode_args = SUITES[suite]
    else:
        mode = (directive(text, "MODE") or ["check"])[0]
        mode_args = MODES.get(mode, ["--check"])
    args = [ncc] + mode_args + directive(text, "ARGS") + [rel]
    proc = subprocess.run(args, cwd=ROOT, capture_output=True, text=True, errors="replace", timeout=60)
    failures = []

    if SANITIZER_RE.search(proc.stderr):
        failures.append("sanitizer report:\n" + proc.stderr)
        return failures
    if proc.returncode not in (0, 1):
        failures.append(f"exit status {proc.returncode} (crash or internal error):\n{proc.stderr}")
        return failures

    got = []
    for line in proc.stderr.split("\n"):
        m = DIAG_RE.match(line)
        if m and os.path.normpath(m.group("file")) == os.path.normpath(rel):
            got.append((int(m.group("line")), m.group("sev"), m.group("code")))
        elif line.startswith("ncc: error"):
            got.append((0, "error", line))
    expected = expectations(text)
    missing = list(expected)
    unexpected = []
    for g in got:
        if g in missing:
            missing.remove(g)
        else:
            unexpected.append(g)
    for line, sev, code in missing:
        failures.append(f"missing {sev} {code} on line {line}")
    for line, sev, code in unexpected:
        failures.append(f"unexpected {sev} {code} on line {line}")
    if failures:
        failures.append("compiler output:\n" + proc.stderr)

    # Under -Werror a warning is reported as an error: annotate it `// ERROR W4001`.
    want_status = 1 if any(sev == "error" for _, sev, _ in expected) else 0
    if not failures and proc.returncode != want_status:
        failures.append(f"exit status {proc.returncode}, expected {want_status}")

    golden = os.path.splitext(path)[0] + ".expected"
    if suite in GOLDEN_SUITES or (suite == "regress" and os.path.exists(golden)):
        if update:
            with open(golden, "w", encoding="utf-8") as f:
                f.write(proc.stdout)
        elif not os.path.exists(golden):
            failures.append(f"missing golden file {os.path.relpath(golden, ROOT)} (run with --update)")
        else:
            with open(golden, encoding="utf-8") as f:
                want = f.read()
            if want != proc.stdout:
                failures.append(diff(want, proc.stdout))
    elif proc.stdout:
        failures.append("unexpected output on stdout:\n" + proc.stdout)
    return failures


def diff(want, got):
    import difflib
    lines = difflib.unified_diff(want.splitlines(), got.splitlines(), "expected", "actual", lineterm="", n=2)
    return "output differs from golden file:\n" + "\n".join(list(lines)[:80])


# ---- driver tests: command-line behaviour -------------------------------------

DRIVER_CASES = [
    # (arguments, expected exit status, expected codes in stderr, text expected in stdout)
    (["--version"], 0, [], "ncc "),
    (["--help"], 0, [], "usage: ncc"),
    ([], 2, ["E0004"], ""),
    (["--frobnicate", "x.nc"], 2, ["E0002"], ""),
    (["--error-limit=abc", "x.nc"], 2, ["E0003"], ""),
    (["--emit=bogus", "x.nc"], 2, ["E0003"], ""),
    (["--check", "--dump-ast", "x.nc"], 2, ["E0003"], ""),
    (["-O2", "x.nc"], 2, ["E9002"], ""),
    (["-o", "out", "x.nc"], 2, ["E9002"], ""),
    (["--emit=llvm-ir", "x.nc"], 2, ["E9002"], ""),
    (["--target=x86_64-novaris", "x.nc"], 2, ["E9002"], ""),
    (["--check", "tests/driver/does-not-exist.nc"], 1, ["E0001"], ""),
    (["tests/driver/ok.nc"], 1, ["E9003"], ""),
    (["--check", "tests/driver/ok.nc"], 0, [], ""),
    (["--check", "tests/driver/ok.nc", "tests/driver/ok2.nc"], 0, [], ""),
    (["--check", "--error-limit=2", "tests/driver/many_errors.nc"], 1, ["E3001", "E3001"], ""),
    (["--check", "-Werror", "tests/driver/warning.nc"], 1, ["W4001"], ""),
    (["--check", "tests/driver/warning.nc"], 0, ["W4001"], ""),
]


def run_driver_cases(ncc):
    failures = []
    for args, status, codes, stdout_text in DRIVER_CASES:
        proc = subprocess.run([ncc] + args, cwd=ROOT, capture_output=True, text=True, timeout=60)
        name = "ncc " + " ".join(args)
        problems = []
        if SANITIZER_RE.search(proc.stderr):
            problems.append("sanitizer report:\n" + proc.stderr)
        if proc.returncode != status:
            problems.append(f"exit status {proc.returncode}, expected {status}")
        found = re.findall(r"\[([EW]\d{4})\]", proc.stderr)
        if sorted(found) != sorted(codes):
            problems.append(f"diagnostic codes {found}, expected {codes}")
        if stdout_text not in proc.stdout:
            problems.append(f"stdout lacks {stdout_text!r}")
        if problems:
            failures.append((name, problems + ["stderr:\n" + proc.stderr]))
    return failures


def collect(selected):
    cases = []
    for suite in SUITES:
        d = os.path.join(ROOT, suite) if suite == "examples" else os.path.join(ROOT, "tests", suite)
        if not os.path.isdir(d):
            continue
        for name in sorted(os.listdir(d)):
            if name.endswith(".nc"):
                path = os.path.join(d, name)
                if not selected or any(s in os.path.relpath(path, ROOT) for s in selected):
                    cases.append((suite, path))
    return cases


def selftest(ncc):
    """Every fixture in tests/selftest must FAIL. Fixtures name their suite
    with `// SUITE: <suite>`."""
    d = os.path.join(ROOT, "tests", "selftest")
    ok = True
    n = 0
    for name in sorted(os.listdir(d)):
        if not name.endswith(".nc"):
            continue
        n += 1
        path = os.path.join(d, name)
        with open(path, encoding="utf-8") as f:
            suite = (directive(f.read(), "SUITE") or ["diag"])[0]
        failures = run_case(ncc, path, suite, update=False)
        if failures:
            print(f"selftest ok   {name}: the runner reported: {failures[0].splitlines()[0]}")
        else:
            print(f"selftest FAIL {name}: the runner accepted a fixture that must fail")
            ok = False
    print(f"selftest: {n} fixtures, {'all rejected as required' if ok else 'RUNNER IS BROKEN'}")
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ncc", required=True)
    ap.add_argument("--update", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("filter", nargs="*", help="run only tests whose path contains one of these")
    a = ap.parse_args()
    ncc = os.path.abspath(a.ncc)

    if a.selftest:
        return 0 if selftest(ncc) else 1

    total = 0
    failed = 0
    for suite, path in collect(a.filter):
        total += 1
        failures = run_case(ncc, path, suite, a.update)
        if failures:
            failed += 1
            print(f"FAIL {os.path.relpath(path, ROOT)}")
            for f in failures:
                print("    " + f.replace("\n", "\n    "))
    if not a.filter:
        total += len(DRIVER_CASES)
        for name, problems in run_driver_cases(ncc):
            failed += 1
            print(f"FAIL {name}")
            for p in problems:
                print("    " + p.replace("\n", "\n    "))
    print(f"{total - failed} passed, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
