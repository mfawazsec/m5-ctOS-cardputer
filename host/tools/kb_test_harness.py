#!/usr/bin/env python3
"""
kb_test_harness.py — Cardputer keyboard keycode validation harness.

Workflow:
  1. Build firmware (skipped with --no-build)
  2. Flash firmware to device (skipped with --no-flash on first iteration only)
  3. Interactive session: prompt user to press each key; reads KEY_TEST_RAW /
     KEY_TEST_CHAR lines emitted by the firmware over serial
  4. Compare received codes+chars against host/tools/tests/kb_keymap.json
  5. Print pass/fail matrix with counts
  6. If failures exist and they are char-only mismatches: auto-patch
     s_keymap_normal in cardputer_keyboard.cpp, rebuild, reflash, repeat
  7. Stop when 100% pass or --max-iters is reached

Firmware contract (two ESP_LOGI lines added to cardputer_kb_update):
  "KEY_TEST_RAW code=N"        on every key PRESS event from the FIFO
  "KEY_TEST_CHAR code=N char=N" when a code resolves to a non-zero ASCII char

Usage:
    python3 host/tools/kb_test_harness.py
    python3 host/tools/kb_test_harness.py --port /dev/ttyACM0 --no-build --no-flash
    python3 host/tools/kb_test_harness.py --max-iters 3 --key-timeout 30
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    sys.exit("pyserial not installed — run: pip install pyserial")

# ── Paths ──────────────────────────────────────────────────────────────────────
ROOT     = Path(__file__).resolve().parent.parent.parent
FIXTURE  = ROOT / "host" / "tools" / "tests" / "kb_keymap.json"
KBD_CPP  = ROOT / "firmware" / "components" / "cardputer_keyboard" / "cardputer_keyboard.cpp"
FIRMWARE = ROOT / "firmware"

BAUD           = 115200
BOOT_TIMEOUT   = 30   # seconds to wait for "keyboard ready" after flash
DEFAULT_KEY_TO = 20   # seconds to wait for each key press

# ── ANSI colours ──────────────────────────────────────────────────────────────
R = "\033[91m"; G = "\033[92m"; Y = "\033[93m"; C = "\033[96m"
W = "\033[0m";  B = "\033[1m"


def banner(msg, color=C):
    bar = "─" * 60
    print(f"\n{color}{B}{bar}\n  {msg}\n{bar}{W}")


# ── Serial port detection ──────────────────────────────────────────────────────

def detect_port():
    for candidate in ("/dev/ttyACM0", "/dev/ttyACM1", "/dev/ttyUSB0", "/dev/ttyUSB1"):
        if Path(candidate).exists():
            return candidate
    ports = list(serial.tools.list_ports.comports())
    if ports:
        return ports[0].device
    return None


# ── ESP-IDF wrapper ────────────────────────────────────────────────────────────

def _idf_cmd(args_str, timeout=300):
    idf_path = Path(os.environ.get("IDF_PATH", Path.home() / "esp" / "esp-idf"))
    export = idf_path / "export.sh"
    if not export.exists():
        sys.exit(f"ESP-IDF not found at {idf_path}. Set IDF_PATH or run setup-fedora.sh.")
    cmd = f'source "{export}" 2>/dev/null && idf.py -C "{FIRMWARE}" {args_str}'
    return subprocess.run(
        cmd, shell=True, executable="/bin/bash",
        capture_output=True, text=True, timeout=timeout
    )


def build_firmware():
    banner("Building firmware", Y)
    r = _idf_cmd("build", timeout=600)
    if r.returncode != 0:
        print(f"{R}BUILD FAILED:{W}\n{(r.stdout + r.stderr)[-4000:]}")
        return False
    print(f"{G}Build OK{W}")
    return True


def flash_firmware(port):
    banner(f"Flashing firmware → {port}", Y)
    r = _idf_cmd(f"-p {port} flash", timeout=120)
    if r.returncode != 0:
        print(f"{R}FLASH FAILED:{W}\n{(r.stdout + r.stderr)[-3000:]}")
        return False
    print(f"{G}Flash OK{W}")
    return True


# ── Fixture ────────────────────────────────────────────────────────────────────

def load_fixture():
    with open(FIXTURE) as f:
        data = json.load(f)
    return data["keys"]


# ── Serial helpers ─────────────────────────────────────────────────────────────

def wait_for_boot(ser, timeout=BOOT_TIMEOUT):
    """Block until the keyboard driver prints its ready message."""
    print(f"  Waiting for device boot (up to {timeout}s)...", end="", flush=True)
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            line = ser.readline().decode("utf-8", errors="replace").strip()
        except serial.SerialException:
            break
        if "keyboard ready" in line.lower() or "TCA8418 keyboard ready" in line:
            print(f" {G}ready{W}")
            return True
        if line:
            print(".", end="", flush=True)
    print(f" {Y}timeout — continuing anyway{W}")
    return False


def read_key_event(ser, key_timeout):
    """
    Wait up to key_timeout seconds for a KEY_TEST_RAW line.
    Then wait 1 s more for KEY_TEST_CHAR from the same update cycle.
    Returns (raw_code, char_val) — char_val is None for modifier keys.
    """
    ser.reset_input_buffer()
    deadline = time.time() + key_timeout
    raw_code = None

    while time.time() < deadline:
        try:
            line = ser.readline().decode("utf-8", errors="replace").strip()
        except serial.SerialException as exc:
            print(f"\n{R}Serial error: {exc}{W}")
            return None, None
        m = re.search(r"KEY_TEST_RAW code=(\d+)", line)
        if m:
            raw_code = int(m.group(1))
            break

    if raw_code is None:
        return None, None

    # Short window to catch KEY_TEST_CHAR from the same update() call
    char_val = None
    deadline2 = time.time() + 1.5
    while time.time() < deadline2:
        try:
            line = ser.readline().decode("utf-8", errors="replace").strip()
        except serial.SerialException:
            break
        m = re.search(r"KEY_TEST_CHAR code=(\d+) char=(\d+)", line)
        if m and int(m.group(1)) == raw_code:
            char_val = int(m.group(2))
            break

    return raw_code, char_val


# ── Test session ───────────────────────────────────────────────────────────────

def run_test_session(port, keys, key_timeout):
    banner(f"Interactive test session — {len(keys)} keys, {key_timeout}s per key")
    print(f"  Port: {port}  Baud: {BAUD}\n")

    results = []
    with serial.Serial(port, BAUD, timeout=0.5) as ser:
        time.sleep(1.0)
        wait_for_boot(ser)
        print()

        for idx, key in enumerate(keys):
            code     = key["code"]
            label    = key["label"]
            exp_char = key.get("normal")
            is_mod   = exp_char is None

            if exp_char is not None and 32 <= exp_char < 127:
                char_desc = f"'{chr(exp_char)}'"
            elif exp_char == 10:
                char_desc = "'\\n'"
            elif exp_char == 127:
                char_desc = "DEL(127)"
            elif exp_char is None:
                char_desc = "modifier"
            else:
                char_desc = f"0x{exp_char:02X}"

            print(
                f"[{idx+1:02d}/{len(keys)}]  Press  {B}{label:<7}{W}  "
                f"expect code={code:2d}  char={char_desc:<10}",
                end="", flush=True
            )

            raw_code, char_val = read_key_event(ser, key_timeout)

            if raw_code is None:
                print(f"  {Y}TIMEOUT{W}")
                results.append({
                    "key": key, "status": "TIMEOUT",
                    "got_code": None, "got_char": None
                })
                continue

            code_ok = (raw_code == code)
            if is_mod:
                char_ok = True
            else:
                char_ok = (char_val == exp_char)

            if code_ok and char_ok:
                status = "PASS"
            elif not code_ok:
                status = "WRONG_CODE"
            else:
                status = "WRONG_CHAR"

            color = G if status == "PASS" else R
            got_char_str = (
                f"'{chr(char_val)}'" if char_val and 32 <= char_val < 127
                else (f"0x{char_val:02X}" if char_val is not None else "none")
            )
            print(
                f"  got code={raw_code:2d} char={got_char_str:<8}  "
                f"{color}{status}{W}"
            )
            results.append({
                "key": key, "status": status,
                "got_code": raw_code, "got_char": char_val
            })

    return results


# ── Pass/fail matrix ───────────────────────────────────────────────────────────

def print_matrix(results, iteration):
    banner(f"Pass/Fail Matrix — iteration {iteration}")

    by_row = {}
    for r in results:
        row = r["key"]["row"]
        by_row.setdefault(row, []).append(r)

    row_labels = {
        0: "` 1 2 3 4 5 6",
        1: "q w e r t y u",
        2: "a s d f g h i",
        3: "z x c v b n j",
        4: "Fn Spc k l , . /",
        5: "Ctrl Alt o p ; ' Ent",
        6: "Shft Del m ← ↓ ↑ →",
    }

    for row in sorted(by_row):
        cells = sorted(by_row[row], key=lambda x: x["key"]["col"])
        row_str = f"  R{row} │"
        for c in cells:
            s     = c["status"]
            label = c["key"]["label"][:5]
            if s == "PASS":
                cell = f"{G}{'✓':>5}{W}"
            elif s == "TIMEOUT":
                cell = f"{Y}{'?TO':>5}{W}"
            elif s == "WRONG_CODE":
                cell = f"{R}{'!COD':>5}{W}"
            else:
                cell = f"{R}{'!CHR':>5}{W}"
            row_str += f" {label:<5}{cell} │"
        print(row_str)
        print(f"       │" + "─" * (len(cells) * 13) + "│")

    total  = len(results)
    passed = sum(1 for r in results if r["status"] == "PASS")
    wrong_code  = sum(1 for r in results if r["status"] == "WRONG_CODE")
    wrong_char  = sum(1 for r in results if r["status"] == "WRONG_CHAR")
    timeouts    = sum(1 for r in results if r["status"] == "TIMEOUT")

    pct = 100 * passed // total if total else 0
    color = G if passed == total else (Y if pct >= 80 else R)
    print(f"\n  {color}{B}{passed}/{total} keys pass ({pct}%){W}")
    if wrong_code:
        print(f"  {R}WRONG_CODE: {wrong_code}  ← hardware/stride issue, cannot auto-fix{W}")
    if wrong_char:
        print(f"  {Y}WRONG_CHAR: {wrong_char}  ← keymap mismatch, will auto-patch{W}")
    if timeouts:
        print(f"  {Y}TIMEOUT:    {timeouts}  ← key not pressed or no serial event{W}")

    return passed, total, wrong_char


# ── Keymap patcher ─────────────────────────────────────────────────────────────

def _fmt_char(v):
    """Format an ASCII value for a C char literal in the keymap array."""
    if v is None or v == 0:
        return " 0 "
    if v == ord("'"):
        return "'\\''"
    if v == ord("\\"):
        return "'\\\\'"
    if v == 10:
        return "'\\n'"
    if v == 13:
        return "'\\r'"
    if v == 127:
        return "127"
    if 32 <= v < 127:
        return f"'{chr(v)}'"
    return str(v)


def patch_keymap(results, fixture_keys):
    """
    Rebuild s_keymap_normal in cardputer_keyboard.cpp from the fixture.
    Only called when WRONG_CHAR failures exist — fixes all 70 slots at once.
    Returns True if the file was modified.
    """
    wrong_char_results = [r for r in results if r["status"] == "WRONG_CHAR"]
    if not wrong_char_results:
        return False

    banner("Auto-patching s_keymap_normal", Y)
    for r in wrong_char_results:
        k = r["key"]
        exp = k.get("normal")
        got = r["got_char"]
        print(f"  code={k['code']:2d} {k['label']:<7}: "
              f"got={_fmt_char(got)} expected={_fmt_char(exp)}")

    # Build slot array from fixture (70 slots, index = code-1)
    slots = [0] * 70
    for k in fixture_keys:
        code = k["code"]
        if 1 <= code <= 70:
            slots[code - 1] = k.get("normal") or 0

    row_headers = [
        "row0: 1-10 ", "row1: 11-20", "row2: 21-30", "row3: 31-40",
        "row4: 41-50", "row5: 51-60", "row6: 61-70",
    ]
    new_lines = []
    for row in range(7):
        vals = [_fmt_char(slots[row * 10 + col]) for col in range(10)]
        new_lines.append(f" /* {row_headers[row]} */ {','.join(vals)},")
    new_body = "\n".join(new_lines)

    src = KBD_CPP.read_text()
    pattern = re.compile(
        r"(static const char s_keymap_normal\[KBD_NCODES\] = \{)[^}]*(})",
        re.DOTALL
    )
    new_src, n = pattern.subn(
        lambda m: m.group(1) + "\n" + new_body + "\n" + m.group(2),
        src
    )
    if n == 0:
        print(f"{R}Could not locate s_keymap_normal array in {KBD_CPP}{W}")
        return False

    KBD_CPP.write_text(new_src)
    print(f"{G}Patched {len(wrong_char_results)} entry/entries in s_keymap_normal{W}")
    return True


# ── Main loop ──────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(description="Cardputer keyboard test harness")
    ap.add_argument("--port",       help="Serial port (auto-detected if omitted)")
    ap.add_argument("--no-build",   action="store_true", help="Skip build on first iteration")
    ap.add_argument("--no-flash",   action="store_true", help="Skip flash on first iteration")
    ap.add_argument("--max-iters",  type=int, default=5, metavar="N",
                    help="Max fix+reflash iterations before giving up (default 5)")
    ap.add_argument("--key-timeout", type=int, default=DEFAULT_KEY_TO, metavar="S",
                    help=f"Seconds to wait for each key press (default {DEFAULT_KEY_TO})")
    args = ap.parse_args()

    port = args.port or detect_port()
    if not port:
        sys.exit(
            f"{R}No serial port detected.{W}\n"
            "Plug in the Cardputer and retry, or use --port /dev/ttyACM0"
        )

    fixture_keys = load_fixture()
    banner("Cardputer Keyboard Test Harness", B)
    print(f"  Fixture : {FIXTURE}")
    print(f"  Port    : {port}")
    print(f"  Keys    : {len(fixture_keys)}")
    print(f"  Timeout : {args.key_timeout}s per key")
    print(f"  Max iter: {args.max_iters}")

    skip_build = args.no_build
    skip_flash = args.no_flash

    for iteration in range(1, args.max_iters + 1):
        banner(f"Iteration {iteration} / {args.max_iters}", C)

        # ── Build ──────────────────────────────────────────────────────────────
        if not skip_build:
            if not build_firmware():
                sys.exit(2)
        else:
            print(f"  {Y}Skipping build (--no-build){W}")

        # ── Flash ──────────────────────────────────────────────────────────────
        if not skip_flash:
            if not flash_firmware(port):
                sys.exit(2)
            time.sleep(2)
        else:
            print(f"  {Y}Skipping flash (--no-flash){W}")

        # From iteration 2 onward always build+flash
        skip_build = False
        skip_flash = False

        # ── Test session ───────────────────────────────────────────────────────
        results = run_test_session(port, fixture_keys, args.key_timeout)

        # ── Matrix ────────────────────────────────────────────────────────────
        passed, total, wrong_char_count = print_matrix(results, iteration)

        if passed == total:
            banner(f"✓ ALL {total} KEYS PASS — done in {iteration} iteration(s)", G)
            sys.exit(0)

        # ── Decide whether auto-fix is possible ────────────────────────────────
        wrong_code_count = sum(1 for r in results if r["status"] == "WRONG_CODE")
        timeout_count    = sum(1 for r in results if r["status"] == "TIMEOUT")

        if wrong_char_count == 0:
            banner(
                f"No auto-fixable WRONG_CHAR failures.\n"
                f"  WRONG_CODE={wrong_code_count} (hardware wiring — manual fix required)\n"
                f"  TIMEOUT={timeout_count} (key not pressed or no event received)",
                R
            )
            sys.exit(1)

        if iteration == args.max_iters:
            banner(f"Max iterations ({args.max_iters}) reached without 100% pass", R)
            sys.exit(1)

        # ── Patch + next iteration ─────────────────────────────────────────────
        patched = patch_keymap(results, fixture_keys)
        if not patched:
            banner("Patch step failed — stopping", R)
            sys.exit(1)

        print(f"\n  → Rebuilding and reflashing for iteration {iteration + 1}…")


if __name__ == "__main__":
    main()
