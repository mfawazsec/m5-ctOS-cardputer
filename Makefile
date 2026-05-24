##
## m5-ctos-cardputer — Root Makefile
##
## Two-directory layout:
##   firmware/   ESP-IDF project (build, flash, menuconfig)
##   host/       Dev tooling (QA agents, boot tests, flash scripts)
##
## Quick-start:
##   make flash       — build firmware + flash + monitor (logged)
##   make qa          — run all 3 QA agents + boot tests → host/tests/qa_report.md
##   make test        — run host boot-sequence unit tests (no device needed)
##

FW   = firmware
HOST = host

.PHONY: setup build flash flash-only flash-log monitor log \
        qa qa-fast test size menuconfig clean help

## ── Firmware targets ─────────────────────────────────────────────────────────

setup:
	@echo "==> Installing ESP-IDF dependencies..."
	@which idf.py > /dev/null 2>&1 || (echo "ERROR: idf.py not found. Source ESP-IDF: . $$IDF_PATH/export.sh" && exit 1)
	@pip install esptool pyserial flawfinder
	@pip install black
	@echo "==> Setup complete."

build:
	@echo "==> Building m5-ctOS firmware..."
	cd $(FW) && idf.py build

flash:
	@echo "==> Build + flash + monitor (logged)..."
	./$(HOST)/tools/linux-flash/flash.sh all

flash-only:
	@echo "==> Flash only (no build)..."
	./$(HOST)/tools/linux-flash/flash.sh flash-only

flash-log:
	@echo "==> Flash + monitor (logged)..."
	./$(HOST)/tools/linux-flash/flash.sh flash-log

monitor:
	./$(HOST)/tools/linux-flash/flash.sh monitor

log:
	./$(HOST)/tools/linux-flash/flash.sh log

size:
	cd $(FW) && idf.py size

menuconfig:
	cd $(FW) && idf.py menuconfig

clean:
	@echo "==> Cleaning firmware build artifacts..."
	cd $(FW) && idf.py fullclean
	find $(FW) -name "*.ctm" -delete

## ── Host targets ─────────────────────────────────────────────────────────────

qa:
	@echo "==> Running QA pipeline (3 agents)..."
	python3 $(HOST)/tools/qa.py

qa-fast:
	@echo "==> Running QA pipeline (fast mode)..."
	python3 $(HOST)/tools/qa.py --fast

test:
	@echo "==> Running host boot-sequence unit tests..."
	$(MAKE) -C $(HOST)/tests/host

## ── Module targets ───────────────────────────────────────────────────────────

MOD ?=

module:
	@test -n "$(MOD)" || (echo "Usage: make module MOD=<name>" && exit 1)
	@test -d $(FW)/modules/$(MOD) || (echo "Module '$(MOD)' not found under firmware/modules/" && exit 1)
	@echo "==> Building module: $(MOD)"
	cd $(FW)/modules/$(MOD) && idf.py build

package:
	@test -n "$(MOD)" || (echo "Usage: make package MOD=<name>" && exit 1)
	@echo "==> Packaging module: $(MOD)"
	python3 $(HOST)/tools/package_module.py $(FW)/modules/$(MOD)

## ── Code quality ─────────────────────────────────────────────────────────────

lint:
	@echo "==> Running clang-format check..."
	find $(FW)/main $(FW)/modules -name "*.cpp" -o -name "*.h" | xargs clang-format --dry-run --Werror
	@echo "==> Running Python black check..."
	black --check $(HOST)/tools/

format:
	@echo "==> Formatting C/C++ sources..."
	find $(FW)/main $(FW)/modules -name "*.cpp" -o -name "*.h" | xargs clang-format -i
	@echo "==> Formatting Python tooling..."
	black $(HOST)/tools/

## ── Help ─────────────────────────────────────────────────────────────────────

help:
	@echo ""
	@echo "m5-ctOS build targets:"
	@echo ""
	@echo "  Firmware:"
	@echo "    make setup              - install dependencies"
	@echo "    make build              - compile firmware"
	@echo "    make flash              - build + flash + monitor (logged)"
	@echo "    make flash-only         - flash only (no build)"
	@echo "    make flash-log          - flash + monitor (logged, no build)"
	@echo "    make monitor            - serial monitor (logged)"
	@echo "    make log                - alias for monitor"
	@echo "    make size               - show binary size breakdown"
	@echo "    make menuconfig         - ESP-IDF config menu"
	@echo "    make clean              - wipe build artifacts"
	@echo ""
	@echo "  Host / QA:"
	@echo "    make qa                 - run all 3 QA agents + boot tests"
	@echo "    make qa-fast            - same, skip slow external tools"
	@echo "    make test               - host boot-sequence tests (no device)"
	@echo "    make lint               - check code formatting"
	@echo "    make format             - auto-format sources"
	@echo ""
	@echo "  Modules:"
	@echo "    make module MOD=<name>  - build a module"
	@echo "    make package MOD=<name> - package module as .ctm"
	@echo ""
	@echo "  Logs saved to: host/tools/linux-flash/logs/"
	@echo "  QA report at:  host/tests/qa_report.md"
	@echo ""
