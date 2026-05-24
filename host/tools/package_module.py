#!/usr/bin/env python3
"""
package_module.py - Package a built module into a .ctm bundle.

Usage:
  python3 tools/package_module.py modules/<name>

The module directory must contain:
  - manifest.json
  - build/<name>.bin  (from idf.py build)
  - README.md

Output: <name>.ctm in the current directory.
"""

import sys
import os
import json
import zipfile
import pathlib
import argparse


def validate_manifest(manifest: dict) -> None:
    required = ["id", "name", "version", "author", "category",
                "requires_hardware", "min_os_version", "entrypoint"]
    missing = [k for k in required if k not in manifest]
    if missing:
        raise ValueError(f"Manifest missing required fields: {missing}")
    if not manifest["id"].replace("_", "").isalnum():
        raise ValueError(f"Module id must be alphanumeric/underscore: {manifest['id']!r}")


def find_firmware_bin(module_dir: pathlib.Path, manifest: dict) -> pathlib.Path:
    entrypoint = manifest.get("entrypoint", "firmware.bin")
    # Try build/<module_id>.bin, then build/firmware.bin, then explicit entrypoint
    candidates = [
        module_dir / "build" / f"{manifest['id']}.bin",
        module_dir / "build" / entrypoint,
        module_dir / entrypoint,
    ]
    for path in candidates:
        if path.exists():
            return path
    raise FileNotFoundError(
        f"Could not find firmware binary for module '{manifest['id']}'. "
        f"Run 'make module MOD={manifest['id']}' first."
    )


def package_module(module_dir_str: str) -> str:
    module_dir = pathlib.Path(module_dir_str).resolve()
    if not module_dir.is_dir():
        raise FileNotFoundError(f"Module directory not found: {module_dir}")

    manifest_path = module_dir / "manifest.json"
    if not manifest_path.exists():
        raise FileNotFoundError(f"manifest.json not found in {module_dir}")

    with open(manifest_path) as f:
        manifest = json.load(f)

    validate_manifest(manifest)

    firmware_path = find_firmware_bin(module_dir, manifest)
    readme_path   = module_dir / "README.md"

    module_id = manifest["id"]
    version   = manifest["version"]
    out_name  = f"{module_id}-{version}.ctm"

    with zipfile.ZipFile(out_name, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(manifest_path, "manifest.json")
        z.write(firmware_path, "firmware.bin")
        if readme_path.exists():
            z.write(readme_path, "README.md")

    size_kb = os.path.getsize(out_name) / 1024
    print(f"Packaged: {out_name}  ({size_kb:.1f} KB)")
    print(f"  id:      {manifest['id']}")
    print(f"  name:    {manifest['name']}")
    print(f"  version: {manifest['version']}")
    print(f"  author:  {manifest['author']}")
    return out_name


def main():
    parser = argparse.ArgumentParser(description="Package a ctOS module into a .ctm bundle.")
    parser.add_argument("module_dir", help="Path to module directory (e.g. modules/ult_jammer)")
    args = parser.parse_args()

    try:
        out = package_module(args.module_dir)
        sys.exit(0)
    except (FileNotFoundError, ValueError, json.JSONDecodeError) as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
