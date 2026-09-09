.DEFAULT_GOAL := help

CC ?= gcc
CPPFLAGS ?=
CFLAGS ?= -O2
CFLAGS += -Wall -Wextra -Wpedantic
LDFLAGS ?=
LDLIBS += -lethercat -lrt -pthread

TARGET := bin/ethercat_csv_test
SOURCE := src/ethercat_csv_test.c
RUN_ARGS ?=

.PHONY: help all build check run clean

help:
	@echo "Targets:"
	@echo "  make check                 Check source, compiler and IgH header"
	@echo "  make build                 Build $(TARGET) on the target board"
	@echo "  make run RUN_ARGS='...'    Build and run (normally as root)"
	@echo "  make clean                 Remove generated binaries"

all: build

build: $(TARGET)

$(TARGET): $(SOURCE)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LDFLAGS) $(LDLIBS) -o $@

check:
	@test -f "$(SOURCE)" || { echo "Missing $(SOURCE)" >&2; exit 1; }
	@command -v "$(CC)" >/dev/null 2>&1 || { echo "Compiler not found: $(CC)" >&2; exit 1; }
	@printf '#include <ecrt.h>\n' | $(CC) $(CPPFLAGS) -E -x c - >/dev/null || { \
		echo "IgH header ecrt.h was not found. Install/copy the IgH EtherCAT Master development files." >&2; \
		exit 1; \
	}
	@echo "Basic build prerequisites are available."

run: build
	@test "$$(id -u)" -eq 0 || { echo "Run this target as root so the process can access /dev/EtherCAT0 and use real-time scheduling." >&2; exit 1; }
	./$(TARGET) $(RUN_ARGS)

clean:
	$(RM) -r bin
