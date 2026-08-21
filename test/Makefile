# Host tests. No ESP-IDF, no hardware.
#
#   make -C test         drift check, then the host tests
#   make -C test test    host tests alone, for when no milk-lab-creations
#                        checkout is around for the drift check to compare against
#
# The conformance test builds its data from wire-format-fixture.json, a copy of
# the canonical fixture in github.com/junderdo/milk-lab-creations
# (docs/spec/wire-format-fixture.json).
#
# The device serial test compiles main/device_serial.c against the stand-ins in
# host/, so it needs OpenSSL's libcrypto to supply SHA-256 off-target.

CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O1 -MMD -MP
PYTHON ?= python3

BUILD_DIR := build
FIXTURE := wire-format-fixture.json
FIXTURE_HEADER := $(BUILD_DIR)/wire_format_fixture.h
CONFORMANCE := $(BUILD_DIR)/wire_format_conformance

VECTORS_HEADER := $(BUILD_DIR)/device_serial_vectors.h
SERIAL := $(BUILD_DIR)/device_serial_conformance

TESTS := $(CONFORMANCE) $(SERIAL)

.PHONY: all test drift check clean

# Drift first: a conformance pass against a stale copy of the fixture proves nothing.
all: check

test: $(TESTS)
	./$(CONFORMANCE)
	./$(SERIAL)

# Fails when this repo's copy of the fixture has drifted from the canonical one.
drift:
	./check-fixture-drift.sh

check: drift test

$(FIXTURE_HEADER): gen_fixture_header.py $(FIXTURE) | $(BUILD_DIR)
	$(PYTHON) gen_fixture_header.py $(FIXTURE) $@

$(CONFORMANCE): wire_format_conformance.c $(FIXTURE_HEADER) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I../main/types -I$(BUILD_DIR) -o $@ wire_format_conformance.c

$(VECTORS_HEADER): gen_serial_vectors.py | $(BUILD_DIR)
	$(PYTHON) gen_serial_vectors.py $@

$(SERIAL): device_serial_conformance.c host/host_efuse.c host/host_sha.c ../main/device_serial.c $(VECTORS_HEADER) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I../main -Ihost -I$(BUILD_DIR) -o $@ \
	    device_serial_conformance.c host/host_efuse.c host/host_sha.c ../main/device_serial.c -lcrypto

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

-include $(CONFORMANCE).d $(SERIAL).d

clean:
	rm -rf $(BUILD_DIR)
