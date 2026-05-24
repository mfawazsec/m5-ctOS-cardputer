#!/usr/bin/env python3
"""
Agent 1: Correctness Review
Runs cppcheck (if available) or a pure-Python AST-level static analysis
on all C/C++ sources in the ctOS project.

Produces: JSON findings on stdout.
"""

import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent


def find_sources():
    patterns = ["main/**/*.cpp", "main/**/*.h",
                "components/**/*.cpp", "components/**/*.h"]
    files = []
    for pat in patterns:
        files.extend(ROOT.glob(pat))
    return [str(f) for f in files if ".git" not in str(f)]


def run_cppcheck(sources):
    """Run cppcheck and parse XML output."""
    findings = []
    try:
        result = subprocess.run(
            ["cppcheck",
             "--enable=all",
             "--suppress=missingInclude",
             "--suppress=unusedFunction",
             "--xml", "--xml-version=2",
             "--std=c++17",
             "-I", str(ROOT / "main"),
             "-I", str(ROOT / "components" / "cardputer_keyboard"),
             *sources],
            capture_output=True, text=True, timeout=120
        )
        # cppcheck outputs XML to stderr
        import xml.etree.ElementTree as ET
        try:
            root = ET.fromstring(result.stderr)
            for err in root.iter("error"):
                loc = err.find("location")
                findings.append({
                    "tool": "cppcheck",
                    "severity": err.get("severity", "unknown"),
                    "id":       err.get("id", ""),
                    "msg":      err.get("msg", ""),
                    "file":     loc.get("file", "") if loc is not None else "",
                    "line":     int(loc.get("line", 0)) if loc is not None else 0,
                })
        except ET.ParseError:
            pass
    except FileNotFoundError:
        return None  # cppcheck not installed
    except subprocess.TimeoutExpired:
        return []
    return findings


def python_static_checks(sources):
    """
    Pure-Python heuristic checks for common C/C++ bugs.
    Not a full parser — pattern-based but effective for known anti-patterns.
    """
    findings = []

    patterns = [
        # Dangerous string functions without bounds
        (r'\bstrcpy\s*\(', "correctness", "UNSAFE_STRCPY",
         "Use strlcpy instead of strcpy to prevent buffer overflow"),
        (r'\bstrcat\s*\(', "correctness", "UNSAFE_STRCAT",
         "Use strlcat instead of strcat to prevent buffer overflow"),
        (r'\bsprintf\s*\(', "correctness", "UNSAFE_SPRINTF",
         "Use snprintf instead of sprintf — no length bound"),
        (r'\bgets\s*\(', "correctness", "UNSAFE_GETS",
         "gets() is banned — no bounds checking whatsoever"),

        # Unchecked malloc
        (r'\b(?:malloc|calloc|realloc)\s*\([^)]+\)\s*;', "correctness", "UNCHECKED_MALLOC",
         "malloc() return value not checked for NULL"),

        # Double free risk
        (r'\bfree\s*\(\s*\w+\s*\)\s*;(?!.*= NULL)', "correctness", "POTENTIAL_DOUBLE_FREE",
         "Pointer not set to NULL after free() — risk of double-free"),

        # strtok is not re-entrant
        (r'\bstrtok\s*\(', "correctness", "STRTOK_NOT_REENTRANT",
         "strtok() is not thread-safe; use strtok_r() in RTOS context"),

        # Potential integer overflow in size computation
        (r'malloc\s*\(\s*\w+\s*\*\s*sizeof', "correctness", "MALLOC_INT_OVERFLOW",
         "malloc(n * sizeof) may overflow; use calloc() or check bounds first"),
    ]

    for src in sources:
        try:
            text = Path(src).read_text(errors="replace")
            lines = text.splitlines()
        except Exception:
            continue

        rel = os.path.relpath(src, ROOT)
        for lineno, line in enumerate(lines, 1):
            # Skip comment-only lines
            stripped = line.strip()
            if stripped.startswith("//") or stripped.startswith("*"):
                continue
            for pat, severity, code, msg in patterns:
                if re.search(pat, line):
                    findings.append({
                        "tool": "python-static",
                        "severity": severity,
                        "id": code,
                        "msg": msg,
                        "file": rel,
                        "line": lineno,
                        "context": stripped[:120],
                    })

    return findings


def main():
    sources = find_sources()
    print(f"[correctness-agent] Scanning {len(sources)} source files...",
          file=sys.stderr)

    all_findings = []

    # Try cppcheck first
    cppcheck_findings = run_cppcheck(sources)
    if cppcheck_findings is None:
        print("[correctness-agent] cppcheck not found — using Python checks only",
              file=sys.stderr)
    else:
        all_findings.extend(cppcheck_findings)
        print(f"[correctness-agent] cppcheck: {len(cppcheck_findings)} findings",
              file=sys.stderr)

    # Always run Python checks
    py_findings = python_static_checks(sources)
    all_findings.extend(py_findings)
    print(f"[correctness-agent] python-static: {len(py_findings)} findings",
          file=sys.stderr)

    # Deduplicate by (file, line, id)
    seen = set()
    deduped = []
    for f in all_findings:
        key = (f.get("file"), f.get("line"), f.get("id"))
        if key not in seen:
            seen.add(key)
            deduped.append(f)

    result = {
        "agent": "correctness",
        "sources_scanned": len(sources),
        "findings": deduped,
        "summary": {
            "total": len(deduped),
            "cppcheck": len(cppcheck_findings) if cppcheck_findings else 0,
            "python_static": len(py_findings),
        }
    }
    print(json.dumps(result, indent=2))
    return 0 if len(deduped) == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
