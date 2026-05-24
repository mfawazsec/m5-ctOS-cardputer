#!/usr/bin/env python3
"""
Agent 3: Boot QA
Builds and runs the host-compiled Unity boot-sequence test suite,
captures output and parses Unity's summary line.

Produces: JSON results on stdout.
"""

import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT      = Path(__file__).resolve().parent.parent.parent
TESTS_DIR = ROOT / "tests" / "host"


def build_tests():
    result = subprocess.run(
        ["make", "build"],
        cwd=str(TESTS_DIR),
        capture_output=True, text=True, timeout=60
    )
    return result.returncode, result.stdout + result.stderr


def run_tests():
    bin_path = TESTS_DIR / "build" / "test_boot"
    if not bin_path.exists():
        return 1, "Binary not found — build may have failed"
    result = subprocess.run(
        [str(bin_path)],
        capture_output=True, text=True, timeout=30
    )
    return result.returncode, result.stdout + result.stderr


def parse_unity_output(output):
    """
    Parse Unity test output lines like:
      test_boot_sequence.c:42:test_nvs_init_happy_path:PASS
      test_boot_sequence.c:55:test_nvs_dirty:FAIL: Expected 2 was 1
    and the summary:
      25 Tests 0 Failures 0 Ignored
    """
    tests = []
    summary = {"total": 0, "failures": 0, "ignored": 0}

    test_re  = re.compile(r'^(\S+\.c):(\d+):(\w+):(PASS|FAIL|IGNORE)(?::\s*(.*))?$')
    summ_re  = re.compile(r'^(\d+) Tests (\d+) Failures (\d+) Ignored')

    for line in output.splitlines():
        m = test_re.match(line.strip())
        if m:
            tests.append({
                "file":   m.group(1),
                "line":   int(m.group(2)),
                "name":   m.group(3),
                "result": m.group(4),
                "detail": m.group(5) or "",
            })
            continue
        m = summ_re.match(line.strip())
        if m:
            summary = {
                "total":    int(m.group(1)),
                "failures": int(m.group(2)),
                "ignored":  int(m.group(3)),
            }

    return tests, summary


def main():
    print("[boot-qa-agent] Building host test suite...", file=sys.stderr)
    build_rc, build_out = build_tests()
    if build_rc != 0:
        print(f"[boot-qa-agent] BUILD FAILED:\n{build_out}", file=sys.stderr)
        result = {
            "agent": "boot_qa",
            "build": "FAIL",
            "build_log": build_out,
            "tests": [],
            "summary": {"total": 0, "failures": 0, "ignored": 0},
        }
        print(json.dumps(result, indent=2))
        return 1

    print("[boot-qa-agent] Running tests...", file=sys.stderr)
    run_rc, run_out = run_tests()

    tests, summary = parse_unity_output(run_out)

    passes   = [t for t in tests if t["result"] == "PASS"]
    failures = [t for t in tests if t["result"] == "FAIL"]

    print(f"[boot-qa-agent] {summary['total']} tests, "
          f"{summary['failures']} failures", file=sys.stderr)

    result = {
        "agent": "boot_qa",
        "build": "PASS" if build_rc == 0 else "FAIL",
        "run_exit_code": run_rc,
        "raw_output": run_out,
        "tests": tests,
        "summary": summary,
        "passes":   len(passes),
        "failures_list": failures,
    }
    print(json.dumps(result, indent=2))
    return 0 if summary["failures"] == 0 and build_rc == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
