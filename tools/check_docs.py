#!/usr/bin/env python3
"""Consistency checks for the NovaC documentation.

Run from the repository root (or via `make check-docs`). Exits non-zero
and lists every problem found. Standard library only.

Checks:
  1. Required repository files exist.
  2. Relative Markdown links resolve, including #anchors (GitHub slugs).
  3. Every cited decision (D-NNN) and open question (Q-NN) is defined in
     docs/NOVAC_DECISIONS.md, exactly once.
  4. The keyword lists in NOVAC_GRAMMAR.md and NOVAC_SPEC.md are identical
     and free of duplicates.
  5. Every diagnostic code cited anywhere (E1234 / W1234) is defined in
     exactly one table row across the docs.
  6. FEATURE_STATUS.md: unique IDs, statuses from the allowed vocabulary,
     each row under the section matching its status.
  7. "Phase N" references use phases 0-9 only.
  8. The implementation agrees with the docs: the keyword list in
     src/lex/token.h equals the documented one, and every diagnostic code
     the compiler emits (src/) or the tests expect (tests/) is defined in
     the docs.
"""

import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir))

REQUIRED_FILES = [
    "README.md",
    "LICENSE",
    ".gitignore",
    "CONTRIBUTING.md",
    "CHANGELOG.md",
    "Makefile",
    "src/README.md",
    "tests/README.md",
    "docs/NOVAC_SPEC.md",
    "docs/NOVAC_GRAMMAR.md",
    "docs/NOVAC_MEMORY_MODEL.md",
    "docs/NOVAC_TYPE_SYSTEM.md",
    "docs/NOVAC_ABI.md",
    "docs/NOVAC_COMPILER_ARCHITECTURE.md",
    "docs/NOVAC_ROADMAP.md",
    "docs/NOVAC_DECISIONS.md",
    "docs/FEATURE_STATUS.md",
    "docs/NOVARISOS_INTEGRATION.md",
]

STATUS_VOCAB = {"Implemented", "Planned", "Open", "Unsupported"}
DECISION_STATUS_VOCAB = {"Accepted", "Provisional", "Open"}

problems = []


def problem(path, msg):
    problems.append(f"{os.path.relpath(path, ROOT)}: {msg}")


def markdown_files():
    out = []
    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames if d not in (".git", "build")]
        for name in filenames:
            if name.endswith(".md"):
                out.append(os.path.join(dirpath, name))
    return sorted(out)


def strip_code(text):
    """Return text with fenced code blocks and inline code replaced by
    blanks (line structure preserved), so links and IDs inside code are
    not checked as prose."""
    out_lines = []
    in_fence = False
    for line in text.split("\n"):
        if line.lstrip().startswith("```"):
            in_fence = not in_fence
            out_lines.append("")
            continue
        out_lines.append("" if in_fence else re.sub(r"`[^`]*`", "``", line))
    return "\n".join(out_lines)


def github_slug(heading):
    s = heading.strip().lower()
    s = re.sub(r"`", "", s)
    # Keep letters, digits, spaces, hyphens, underscores; drop the rest.
    s = "".join(ch for ch in s if ch.isalnum() or ch in " -_")
    return s.replace(" ", "-")


def anchors_of(path, cache={}):
    if path not in cache:
        with open(path, encoding="utf-8") as f:
            text = strip_code(f.read())
        slugs = {}
        result = set()
        for line in text.split("\n"):
            m = re.match(r"^(#{1,6})\s+(.*?)\s*#*\s*$", line)
            if not m:
                continue
            base = github_slug(m.group(2))
            n = slugs.get(base, 0)
            result.add(base if n == 0 else f"{base}-{n}")
            slugs[base] = n + 1
        cache[path] = result
    return cache[path]


def check_required():
    for rel in REQUIRED_FILES:
        if not os.path.isfile(os.path.join(ROOT, rel)):
            problems.append(f"missing required file: {rel}")


def check_links(files):
    link_re = re.compile(r"\[[^\]]*\]\(([^)\s]+)\)")
    for path in files:
        with open(path, encoding="utf-8") as f:
            text = strip_code(f.read())
        for lineno, line in enumerate(text.split("\n"), 1):
            for target in link_re.findall(line):
                if re.match(r"^[a-z]+:", target):
                    continue  # http:, https:, mailto:
                file_part, _, anchor = target.partition("#")
                dest = path if not file_part else os.path.normpath(
                    os.path.join(os.path.dirname(path), file_part))
                if not os.path.exists(dest):
                    problem(path, f"line {lineno}: broken link target '{target}'")
                    continue
                if anchor and dest.endswith(".md") and anchor not in anchors_of(dest):
                    problem(path, f"line {lineno}: no anchor '#{anchor}' in {os.path.relpath(dest, ROOT)}")


def check_decisions(files):
    dpath = os.path.join(ROOT, "docs", "NOVAC_DECISIONS.md")
    with open(dpath, encoding="utf-8") as f:
        dtext = f.read()
    defined_d = re.findall(r"^## (D-\d{3}) ", dtext, re.M)
    defined_q = re.findall(r"^\| (Q-\d{2}) \|", dtext, re.M)
    for ids, kind in ((defined_d, "decision"), (defined_q, "question")):
        seen = set()
        for i in ids:
            if i in seen:
                problem(dpath, f"{kind} {i} defined more than once")
            seen.add(i)
    # Decision numbering must be contiguous (append-only log).
    nums = [int(d[2:]) for d in defined_d]
    if nums != list(range(1, len(nums) + 1)):
        problem(dpath, "decision IDs are not numbered contiguously from D-001")
    for status in re.findall(r"^\*\*Status:\*\* (\w+)", dtext, re.M):
        if status not in DECISION_STATUS_VOCAB:
            problem(dpath, f"unknown decision status '{status}'")
    defined = set(defined_d) | set(defined_q)
    for path in files:
        with open(path, encoding="utf-8") as f:
            text = f.read()
        for ref in sorted(set(re.findall(r"\b([DQ]-\d{2,3})\b", text))):
            if ref not in defined:
                problem(path, f"cites undefined {ref}")


def keyword_block(path):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    m = re.search(r"<!-- keywords:begin -->(.*?)<!-- keywords:end -->", text, re.S)
    if not m:
        problem(path, "keyword block markers not found")
        return None
    body = m.group(1).replace("```text", "").replace("```", "")
    words = body.split()
    if len(words) != len(set(words)):
        dups = sorted({w for w in words if words.count(w) > 1})
        problem(path, f"duplicate keywords: {', '.join(dups)}")
    return set(words)


def check_keywords():
    g = keyword_block(os.path.join(ROOT, "docs", "NOVAC_GRAMMAR.md"))
    s = keyword_block(os.path.join(ROOT, "docs", "NOVAC_SPEC.md"))
    if g is not None and s is not None and g != s:
        problems.append("keyword lists differ: only in grammar: "
                        f"{sorted(g - s)}; only in spec: {sorted(s - g)}")


def check_diag_codes(files):
    code_re = re.compile(r"\b([EW]\d{4})\b")
    def_re = re.compile(r"^\| ([EW]\d{4}) \|", re.M)
    definitions = {}
    citations = []
    for path in files:
        with open(path, encoding="utf-8") as f:
            text = f.read()
        for code in def_re.findall(text):
            definitions.setdefault(code, []).append(path)
        for lineno, line in enumerate(text.split("\n"), 1):
            for code in code_re.findall(line):
                citations.append((path, lineno, code))
    for code, where in sorted(definitions.items()):
        if len(where) > 1:
            problems.append(f"diagnostic {code} defined in more than one table: "
                            + ", ".join(os.path.relpath(p, ROOT) for p in where))
    for path, lineno, code in citations:
        if code not in definitions:
            problem(path, f"line {lineno}: diagnostic {code} is cited but never defined")


def check_feature_status():
    path = os.path.join(ROOT, "docs", "FEATURE_STATUS.md")
    with open(path, encoding="utf-8") as f:
        lines = f.read().split("\n")
    section = None
    seen = set()
    for lineno, line in enumerate(lines, 1):
        m = re.match(r"^## (\w+)", line)
        if m:
            section = m.group(1)
            continue
        m = re.match(r"^\| (F-\d{3}) \|", line)
        if not m:
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        fid = cells[0]
        if fid in seen:
            problem(path, f"line {lineno}: duplicate feature ID {fid}")
        seen.add(fid)
        if len(cells) != 5:
            problem(path, f"line {lineno}: expected 5 columns, found {len(cells)}")
            continue
        status = cells[2]
        if status not in STATUS_VOCAB:
            problem(path, f"line {lineno}: unknown status '{status}'")
        elif section in STATUS_VOCAB and status != section:
            problem(path, f"line {lineno}: {fid} has status {status} under section '{section}'")


def check_implementation(files):
    token_h = os.path.join(ROOT, "src", "lex", "token.h")
    if os.path.isfile(token_h):
        with open(token_h, encoding="utf-8") as f:
            text = f.read()
        m = re.search(r"#define NC_TOKEN_KEYWORDS\(X\)(.*?)\n\n", text, re.S)
        impl = set(re.findall(r'X\(KW_\w+, "([^"]+)"\)', m.group(1))) if m else set()
        doc = keyword_block(os.path.join(ROOT, "docs", "NOVAC_GRAMMAR.md")) or set()
        if impl != doc:
            problems.append("src/lex/token.h keywords differ from NOVAC_GRAMMAR.md: only in source: "
                            f"{sorted(impl - doc)}; only in docs: {sorted(doc - impl)}")

    defined = set()
    for path in files:
        with open(path, encoding="utf-8") as f:
            defined.update(re.findall(r"^\| ([EW]\d{4}) \|", f.read(), re.M))
    for sub, pattern in (("src", r'"([EW]\d{4})"'), ("tests", r"\b(?:ERROR|WARNING)(?:@[+-]\d+)?\s+([EW]\d{4})")):
        base = os.path.join(ROOT, sub)
        for dirpath, _, names in os.walk(base):
            for name in names:
                if not name.endswith((".c", ".h", ".nc", ".py")):
                    continue
                path = os.path.join(dirpath, name)
                with open(path, encoding="utf-8", errors="replace") as f:
                    for lineno, line in enumerate(f, 1):
                        for code in re.findall(pattern, line):
                            if code not in defined:
                                problem(path, f"line {lineno}: diagnostic {code} is not defined in docs/")


def check_phases(files):
    for path in files:
        with open(path, encoding="utf-8") as f:
            text = f.read()
        for lineno, line in enumerate(text.split("\n"), 1):
            for n in re.findall(r"\bPhases? (\d+)", line):
                if not 0 <= int(n) <= 9:
                    problem(path, f"line {lineno}: reference to nonexistent Phase {n}")


def main():
    files = markdown_files()
    check_required()
    check_links(files)
    check_decisions(files)
    check_keywords()
    check_diag_codes(files)
    check_feature_status()
    check_phases(files)
    check_implementation(files)
    if problems:
        print(f"check_docs: {len(problems)} problem(s):")
        for p in problems:
            print(f"  - {p}")
        return 1
    print(f"check_docs: OK ({len(files)} Markdown files checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
