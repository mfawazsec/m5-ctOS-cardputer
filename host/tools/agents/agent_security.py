#!/usr/bin/env python3
"""
Agent 2: Security Review — scans firmware/ sources.
"""

import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT    = Path(__file__).resolve().parent.parent.parent.parent  # host/tools/agents → repo root
FW_ROOT = ROOT / "firmware"


def find_sources():
    patterns = ["main/**/*.cpp", "main/**/*.h",
                "components/**/*.cpp", "components/**/*.h"]
    files = []
    for pat in patterns:
        files.extend(FW_ROOT.glob(pat))
    return [str(f) for f in files if ".git" not in str(f)]


def run_flawfinder(sources):
    findings = []
    try:
        result = subprocess.run(
            ["flawfinder", "--csv", "--quiet", *sources],
            capture_output=True, text=True, timeout=60
        )
        import csv, io
        reader = csv.DictReader(io.StringIO(result.stdout))
        for row in reader:
            findings.append({
                "tool": "flawfinder",
                "severity": f"level-{row.get('Level','?')}",
                "id": row.get("Name", ""),
                "msg": row.get("Warning", ""),
                "file": os.path.relpath(row.get("File", ""), ROOT),
                "line": int(row.get("Line", 0)),
                "context": row.get("Context", "").strip()[:120],
            })
    except FileNotFoundError:
        return None
    except (subprocess.TimeoutExpired, Exception):
        return []
    return findings


SECURITY_RULES = [
    # These two are now fixed — keep rules to catch regressions
    (r'"ctOS2024!"', "HIGH", "HARDCODED_DEFAULT_PASSWORD",
     "Default WiFi password is hardcoded in source — user may never change it."),
    # Match bare memcmp() but NOT ct_memcmp() (the safe wrapper we introduced)
    (r'(?<!ct_)\bmemcmp\s*\(\s*hash', "HIGH", "TIMING_SIDE_CHANNEL",
     "memcmp() for cryptographic comparison leaks timing. Use constant-time ct_memcmp()."),

    (r'const uint8_t default_hash\[32\]', "MEDIUM", "HARDCODED_PIN_HASH",
     "Default PIN hash (SHA-256 of '0000') is hardcoded. Enforce first-boot PIN change flow."),
    (r'esp_wifi_init\s*\([^)]+\)\s*;', "MEDIUM", "UNCHECKED_WIFI_INIT",
     "esp_wifi_init() return value not checked — WiFi state undefined on error."),
    (r'nvs_open\s*\(.*\)\s*;', "LOW", "NVS_OPEN_UNCHECKED",
     "nvs_open() return not checked at call site — handle may be invalid."),
    (r'snprintf\s*\(\s*\w+\s*,\s*sizeof\s*\(\w+\)\s*,.*strlen', "LOW",
     "SNPRINTF_WITH_UNVALIDATED_LEN",
     "strlen of external input fed into snprintf — validate before use."),
    (r'free\s*\(\s*arg\s*\)', "LOW", "FREE_WITHOUT_NULL_CHECK",
     "Ensure pointer is non-NULL before free() — especially after task creation failure paths."),
    (r'esp_wifi_set_config.*WIFI_IF_AP', "INFO", "AP_NO_RATE_LIMIT",
     "AP mode configured without client isolation or rate limiting."),
    (r'heap_caps_malloc\s*\([^)]+\)\s*;', "MEDIUM", "PSRAM_MALLOC_UNCHECKED",
     "heap_caps_malloc() return not checked — NULL dereference possible."),
    (r'\bstrtok\b', "MEDIUM", "STRTOK_CONCURRENCY",
     "strtok() uses static internal state — unsafe from multiple FreeRTOS tasks. Use strtok_r()."),
    (r'ESP_LOG[IWE]\s*\(.*(?:pass|password|pin|hash|secret)', "HIGH", "SECRET_IN_LOG",
     "Potential secret (password/pin/hash) logged — review this log statement."),
]


def run_custom_rules(sources):
    findings = []
    for src in sources:
        try:
            text = Path(src).read_text(errors="replace")
            lines = text.splitlines()
        except Exception:
            continue
        rel = os.path.relpath(src, ROOT)
        for lineno, line in enumerate(lines, 1):
            stripped = line.strip()
            if stripped.startswith("//") or stripped.startswith("*"):
                continue
            for pattern, severity, code, msg in SECURITY_RULES:
                if re.search(pattern, line, re.IGNORECASE):
                    findings.append({
                        "tool": "custom-security",
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
    print(f"[security-agent] Scanning {len(sources)} source files...", file=sys.stderr)
    all_findings = []
    ff_findings = run_flawfinder(sources)
    if ff_findings is None:
        print("[security-agent] flawfinder not found — using custom rules only", file=sys.stderr)
        ff_count = 0
    else:
        all_findings.extend(ff_findings)
        ff_count = len(ff_findings)
        print(f"[security-agent] flawfinder: {ff_count} findings", file=sys.stderr)
    custom = run_custom_rules(sources)
    all_findings.extend(custom)
    print(f"[security-agent] custom-rules: {len(custom)} findings", file=sys.stderr)
    seen = set()
    deduped = []
    for f in all_findings:
        key = (f.get("file"), f.get("line"), f.get("id"))
        if key not in seen:
            seen.add(key)
            deduped.append(f)
    highs   = [f for f in deduped if f["severity"] in ("HIGH", "level-4", "level-5")]
    mediums = [f for f in deduped if f["severity"] in ("MEDIUM", "level-3")]
    result = {
        "agent": "security",
        "sources_scanned": len(sources),
        "findings": deduped,
        "summary": {
            "total":   len(deduped),
            "high":    len(highs),
            "medium":  len(mediums),
            "low":     len(deduped) - len(highs) - len(mediums),
            "flawfinder": ff_count,
            "custom_rules": len(custom),
        }
    }
    print(json.dumps(result, indent=2))
    return 1 if highs else 0

if __name__ == "__main__":
    sys.exit(main())
