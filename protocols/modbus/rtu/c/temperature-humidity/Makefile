.DEFAULT_GOAL := help

CC = gcc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra
PLATFORM ?= pc
NAME := temperature_humidity
TARGET := $(NAME)_$(PLATFORM)
SRC := src/$(NAME).c

ifneq ($(PLATFORM),pc)
ifneq ($(PLATFORM),plc)
$(error PLATFORM must be pc or plc)
endif
endif

.PHONY: help build run check clean

help:
	@printf '%s\n' 'make build [PLATFORM=pc|plc]' 'make check' 'make run PLATFORM=plc ARGS="--help"' 'make clean' 'Build on the target machine; PLATFORM only selects the output name.'

build: $(TARGET)

$(TARGET): $(SRC) Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SRC) $(LDFLAGS) $(LDLIBS) -o $@

check:
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wpedantic -Werror -fsyntax-only $(SRC)

run: build
	./$(TARGET) $(ARGS)

clean:
	$(RM) $(NAME)_pc $(NAME)_plc
