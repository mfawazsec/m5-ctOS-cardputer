##
## tests/host/Makefile — Build and run ctOS host-side unit tests
##
## Usage:
##   make          # build + run all tests
##   make build    # build only
##   make run      # run only (must build first)
##   make clean    # remove build artifacts
##

CC      = gcc
CFLAGS  = -std=gnu11 -Wall -Wextra -Wshadow \
           -D_GNU_SOURCE \
           -g -O0 \
           -I. \
           -Imocks \
           -Imocks/freertos \
           -Istubs \
           -Iunity

SRCS    = boot_sim.c \
          stubs/mock_globals.c \
          unity/unity.c

TEST_SRCS = test_boot_sequence.c

BIN     = build/test_boot

.PHONY: all build run clean

all: build run

build: $(BIN)

$(BIN): $(SRCS) $(TEST_SRCS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(TEST_SRCS)
	@echo "==> Build OK: $(BIN)"

run: $(BIN)
	@echo ""
	@echo "══════════════════════════════════════════"
	@echo "  ctOS Host Boot-Sequence Test Suite"
	@echo "══════════════════════════════════════════"
	@./$(BIN) 2>&1; \
	  EXIT=$$?; \
	  echo ""; \
	  if [ $$EXIT -eq 0 ]; then \
	    echo "✅  All tests PASSED"; \
	  else \
	    echo "❌  Some tests FAILED (exit $$EXIT)"; \
	  fi; \
	  exit $$EXIT

clean:
	rm -rf build/
