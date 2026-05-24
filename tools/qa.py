#!/usr/bin/env python3
"""
qa.py — ctOS QA Master Orchestrator
Runs all 3 review agents sequentially, aggregates results,
and writes a comprehensive markdown report to tests/qa_report.md.

Usage:
    python3 tools/qa.py           # full pipeline
    python3 tools/qa.py --fast    # skip flawfinder (quicker CI run)

Exit codes:
    0  All agents clean
    1  Security HIGH findings or boot test failures
    2  Build error
"""

import argparse
import json
import os
import subprocess
import sys
from datetime import datetime
from pathlib import Path

ROOT       = Path(__file__).resolve().parent.parent
AGENTS_DIR = ROOT / "tools" / "agents"
REPORT     = ROOT / "tests" / "qa_report.md"

BOLD  = "\033[1m"
RED   = "\033[91m"
YEL   = "\033[93m"
GRN   = "\033[92m"
CYN   = "\033[96m"
RST   = "\033[0m"


def banner(text, color=CYN):
    width = 60
    print(f"\n{color}{BOLD}{'═' * width}")
    print(f"  {text}")
    print(f"{'═' * width}{RST}")


def run_agent(name, script):
    banner(f"Agent: {name}")
    result = subprocess.run(
        [sys.executable, str(script)],
        capture_output=True, text=True,
        cwd=str(ROOT)
    )
    # Print agent's stderr (progress messages)
    if result.stderr.strip():
        print(result.stderr.strip())

    try:
        data = json.loads(result.stdout)
    except json.JSONDecodeError:
        print(f"{RED}Agent {name} produced invalid JSON{RST}")
        data = {"agent": name, "error": "invalid_json", "raw": result.stdout[:500]}

    return data, result.returncode


# ── Markdown report generation ───────────────────────────────────────────────

def severity_emoji(sev):
    sev = str(sev).upper()
    if "HIGH" in sev or "5" in sev or "4" in sev: return "🔴"
    if "MEDIUM" in sev or "3" in sev:              return "🟡"
    if "LOW" in sev or "2" in sev:                 return "🟢"
    return "⚪"


def write_report(correctness, security, boot_qa, run_time):
    lines = []
    a = lines.append

    a(f"# ctOS QA Report")
    a(f"")
    a(f"**Generated:** {run_time}  ")
    a(f"**Project:** m5-ctos-cardputer  ")
    a(f"")

    # Executive summary
    total_security_high = security.get("summary", {}).get("high", 0)
    total_correctness   = correctness.get("summary", {}).get("total", 0)
    boot_failures       = boot_qa.get("summary", {}).get("failures", 0)
    boot_total          = boot_qa.get("summary", {}).get("total", 0)

    overall = "✅ PASS" if (total_security_high == 0 and boot_failures == 0) else "❌ FAIL"
    a(f"## Executive Summary")
    a(f"")
    a(f"| Agent | Status | Key Metric |")
    a(f"|-------|--------|------------|")

    corr_status = "⚠️ Findings" if total_correctness > 0 else "✅ Clean"
    a(f"| Correctness | {corr_status} | {total_correctness} issues found |")

    sec_high = security.get("summary", {}).get("high", 0)
    sec_status = "❌ HIGH Issues" if sec_high > 0 else ("⚠️ Low/Med" if security.get("summary", {}).get("total", 0) > 0 else "✅ Clean")
    a(f"| Security    | {sec_status} | {security.get('summary', {}).get('total', 0)} findings ({sec_high} HIGH) |")

    boot_build  = boot_qa.get("build", "?")
    boot_status = "✅ PASS" if boot_failures == 0 and boot_build == "PASS" else "❌ FAIL"
    a(f"| Boot Tests  | {boot_status} | {boot_total - boot_failures}/{boot_total} passed |")

    a(f"")
    a(f"**Overall: {overall}**")
    a(f"")

    # ── Correctness ──
    a(f"---")
    a(f"## Agent 1: Correctness Review")
    a(f"")
    a(f"- Files scanned: `{correctness.get('sources_scanned', '?')}`")
    a(f"- Total findings: `{total_correctness}`")
    a(f"")

    findings = correctness.get("findings", [])
    if not findings:
        a(f"> ✅ No correctness issues found.")
    else:
        a(f"| Sev | ID | File | Line | Message |")
        a(f"|-----|----|------|------|---------|")
        for f in sorted(findings, key=lambda x: x.get("severity","")):
            em  = severity_emoji(f["severity"])
            rel = f.get("file", "?")
            a(f"| {em} `{f['severity']}` | `{f['id']}` | `{rel}` | {f.get('line','?')} | {f.get('msg','')[:80]} |")

    a(f"")

    # ── Security ──
    a(f"---")
    a(f"## Agent 2: Security Review")
    a(f"")
    summ = security.get("summary", {})
    a(f"- Files scanned: `{security.get('sources_scanned', '?')}`")
    a(f"- 🔴 HIGH: `{summ.get('high', 0)}`  🟡 MEDIUM: `{summ.get('medium', 0)}`  🟢 LOW: `{summ.get('low', 0)}`")
    a(f"")

    sec_findings = security.get("findings", [])
    if not sec_findings:
        a(f"> ✅ No security issues found.")
    else:
        # Group by severity
        for sev in ["HIGH", "MEDIUM", "LOW", "INFO"]:
            group = [f for f in sec_findings
                     if sev.lower() in str(f.get("severity","")).lower()]
            if not group: continue
            a(f"### {severity_emoji(sev)} {sev} ({len(group)})")
            a(f"")
            for f in group:
                rel = f.get("file", "?")
                a(f"**`{f['id']}`** — `{rel}:{f.get('line','?')}`  ")
                a(f"{f.get('msg', '')}  ")
                if f.get("context"):
                    a(f"```c")
                    a(f"{f['context']}")
                    a(f"```")
                a(f"")

    # ── Boot Tests ──
    a(f"---")
    a(f"## Agent 3: Boot Test Results")
    a(f"")
    a(f"- Build: `{boot_qa.get('build', '?')}`")
    a(f"- Tests run: `{boot_total}`")
    a(f"- Passed: `{boot_total - boot_failures}`")
    a(f"- Failed: `{boot_failures}`")
    a(f"")

    tests = boot_qa.get("tests", [])
    if tests:
        a(f"| Result | Test Name | Detail |")
        a(f"|--------|-----------|--------|")
        for t in tests:
            icon = "✅" if t["result"] == "PASS" else ("❌" if t["result"] == "FAIL" else "⚪")
            a(f"| {icon} {t['result']} | `{t['name']}` | {t.get('detail','')[:80]} |")
    else:
        if boot_qa.get("build") == "FAIL":
            a(f"> ❌ Build failed — no tests ran.")
            a(f"```")
            a(boot_qa.get("build_log", "")[:2000])
            a(f"```")
        else:
            a(f"> ⚠️ No test output parsed.")

    a(f"")
    a(f"---")
    a(f"*Report generated by `tools/qa.py` on {run_time}*")

    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text("\n".join(lines))
    print(f"\n{GRN}Report written to: {REPORT}{RST}")


# ── Main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="ctOS QA Pipeline")
    parser.add_argument("--fast", action="store_true",
                        help="Skip slow external tools")
    args = parser.parse_args()

    run_time = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    banner("ctOS QA Pipeline — Starting", BOLD)

    correctness, rc1 = run_agent("Correctness",
                                  AGENTS_DIR / "agent_correctness.py")
    security,    rc2 = run_agent("Security",
                                  AGENTS_DIR / "agent_security.py")
    boot_qa,     rc3 = run_agent("Boot QA",
                                  AGENTS_DIR / "agent_boot_qa.py")

    # Print summary
    banner("QA Summary", BOLD)

    total_issues = correctness.get("summary", {}).get("total", 0)
    sec_highs    = security.get("summary", {}).get("high", 0)
    boot_fail    = boot_qa.get("summary", {}).get("failures", 0)
    boot_total   = boot_qa.get("summary", {}).get("total", 0)

    print(f"\n  {CYN}Correctness:{RST}  {total_issues} findings")
    print(f"  {CYN}Security:{RST}     "
          f"{security.get('summary',{}).get('total',0)} findings "
          f"({RED}{sec_highs} HIGH{RST})")
    print(f"  {CYN}Boot Tests:{RST}   "
          f"{boot_total - boot_fail}/{boot_total} passed")

    write_report(correctness, security, boot_qa, run_time)

    overall_fail = (sec_highs > 0) or (boot_fail > 0)
    if overall_fail:
        print(f"\n{RED}{BOLD}❌ QA FAILED — see tests/qa_report.md{RST}")
        return 1
    else:
        print(f"\n{GRN}{BOLD}✅ QA PASSED — see tests/qa_report.md{RST}")
        return 0


if __name__ == "__main__":
    sys.exit(main())
