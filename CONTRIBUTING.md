# Contributing to m5-ctOS

## Toolchain Setup (Apple Silicon)

### 1. Install ESP-IDF v5.x

```bash
brew install cmake ninja dfu-util
mkdir ~/esp && cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && git checkout v5.3.1 && git submodule update --init --recursive
./install.sh esp32s3
```

Add to your shell profile (`~/.zshrc` or `~/.bashrc`):
```bash
alias get_idf='. $HOME/esp/esp-idf/export.sh'
```

Then every new terminal: `get_idf`

### 2. Clone and setup repo

```bash
git clone https://github.com/mfawazsec/m5-ctos-cardputer
cd m5-ctos-cardputer
make setup
```

### 3. Build

```bash
get_idf
make build
```

### 4. Flash

Connect Cardputer via USB-C. On macOS, the port is typically `/dev/tty.usbserial-*`:

```bash
ls /dev/tty.usb*   # find the port
make flash PORT=/dev/tty.usbserial-XXXX
```

---

## Branch Strategy

| Branch | Purpose |
|--------|---------|
| `main` | Stable releases. Protected: requires PR + CI. |
| `dev` | Active development. All work merges here first. |
| `module/<name>` | Per-module feature branches. |

**Workflow:**
1. Branch from `dev`: `git checkout -b module/your-module dev`
2. Develop and test on hardware
3. Open PR to `dev`
4. After CI passes: merge to `dev`
5. At milestones: PR from `dev` to `main`

---

## Commit Format

Use [Conventional Commits](https://www.conventionalcommits.org/):

```
feat: add CSI amplitude threshold motion detection
fix: correct I2S PDM clock config for SPM1423
docs: add RESEARCH.md citations for WiKI-Eve module
chore: update sdkconfig.defaults for PSRAM OCT mode
module: implement ult_jammer frequency sweep mode
```

---

## Adding a New Module

1. Create `modules/<name>/` directory structure:
   ```
   modules/<name>/
   ├── src/main.cpp
   ├── include/module_api.h
   ├── CMakeLists.txt
   ├── manifest.json
   ├── README.md
   └── RESEARCH.md
   ```

2. Implement `module_main(const ctos_api_t *api)` as the entry point.

3. Fill `manifest.json` with all required fields (see schema in Phase 1.3 of the implementation plan).

4. Build: `make module MOD=<name>`

5. Package: `make package MOD=<name>` → produces `<name>-<version>.ctm`

6. Install on device: upload `.ctm` via web UI at `http://192.168.4.1/modules`

7. Add citations to `RESEARCH.md` — all source papers must be cited.

---

## Code Style

- C/C++: clang-format (`.clang-format` at repo root). Run `make format` before committing.
- Python: black. Run `make format` before committing.
- No tabs in C/C++; 4-space indent.
- No copyright headers required; author in manifest.json is sufficient.

---

## Security Notes

- **Never commit secrets.** PIN is stored in ESP32 NVS only, never in source.
- **Never commit compiled `.bin` files.** CI produces these as artifacts.
- **Never commit `.ctm` module bundles.** These are built artifacts.
- All modules are for authorized security research and education only.
- Document the threat model and authorization context in each `RESEARCH.md`.
