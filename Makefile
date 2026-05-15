PORT ?= /dev/tty.usbserial-*
BAUD ?= 460800
MOD  ?=

.PHONY: setup build flash clean module package lint format

setup:
	@echo "==> Installing ESP-IDF dependencies..."
	@which idf.py >/dev/null 2>&1 || (echo "ERROR: idf.py not found. Source ESP-IDF: . $$IDF_PATH/export.sh" && exit 1)
	@pip install esptool pyserial
	@echo "==> Installing Python tooling..."
	@pip install black
	@echo "==> Setup complete."

build:
	@echo "==> Building m5-ctOS base OS..."
	idf.py build

flash:
	@echo "==> Flashing to $(PORT)..."
	idf.py -p $(PORT) -b $(BAUD) flash monitor

flash-only:
	@echo "==> Flash without monitor..."
	idf.py -p $(PORT) -b $(BAUD) flash

monitor:
	idf.py -p $(PORT) monitor

clean:
	@echo "==> Cleaning build artifacts..."
	idf.py fullclean
	find . -name "*.ctm" -delete

module:
	@test -n "$(MOD)" || (echo "Usage: make module MOD=<name>" && exit 1)
	@test -d modules/$(MOD) || (echo "Module '$(MOD)' not found under modules/" && exit 1)
	@echo "==> Building module: $(MOD)"
	cd modules/$(MOD) && idf.py build

package:
	@test -n "$(MOD)" || (echo "Usage: make package MOD=<name>" && exit 1)
	@echo "==> Packaging module: $(MOD)"
	python3 tools/package_module.py modules/$(MOD)

lint:
	@echo "==> Running clang-format check..."
	find main modules -name "*.cpp" -o -name "*.h" | xargs clang-format --dry-run --Werror
	@echo "==> Running Python black check..."
	black --check tools/

format:
	@echo "==> Formatting C/C++ sources..."
	find main modules -name "*.cpp" -o -name "*.h" | xargs clang-format -i
	@echo "==> Formatting Python tooling..."
	black tools/

size:
	idf.py size

menuconfig:
	idf.py menuconfig

help:
	@echo "m5-ctOS build targets:"
	@echo "  make setup              - install dependencies"
	@echo "  make build              - compile base OS"
	@echo "  make flash PORT=<port>  - flash to device"
	@echo "  make monitor            - serial monitor"
	@echo "  make clean              - wipe build artifacts"
	@echo "  make module MOD=<name>  - build a module"
	@echo "  make package MOD=<name> - package module as .ctm"
	@echo "  make lint               - check formatting"
	@echo "  make format             - auto-format sources"
