# (C) Copyright 2026 Hewlett Packard Enterprise Development LP
# SPDX-License-Identifier: BSD-2-Clause-Patent
CC ?= cc
CFLAGS ?= -O2 -g
WARN = -std=c11 -Wall -Wextra -Werror
BUILD ?= build

.PHONY: all test smoke clean
all: $(BUILD)/libcrt_native_verbs.a

$(BUILD):
	mkdir -p $@

$(BUILD)/verbs.o: verbs.c transport.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -c $< -o $@

$(BUILD)/wire.o: wire.c wire.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -c $< -o $@

$(BUILD)/bootstrap.o: bootstrap.c bootstrap.h transport.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -c $< -o $@

$(BUILD)/rpc.o: rpc.c rpc.h wire.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -c $< -o $@

$(BUILD)/libcrt_native_verbs.a: $(BUILD)/verbs.o $(BUILD)/wire.o $(BUILD)/bootstrap.o $(BUILD)/rpc.o
	$(AR) rcs $@ $^

$(BUILD)/test_verbs: verbs.c transport.h tests/test_verbs.c tests/fake/infiniband/verbs.h | $(BUILD)
	$(CC) $(CFLAGS) $(WARN) -Itests/fake verbs.c tests/test_verbs.c -o $@

$(BUILD)/test_wire: wire.c wire.h tests/test_wire.c | $(BUILD)
	$(CC) $(CFLAGS) $(WARN) wire.c tests/test_wire.c -o $@

$(BUILD)/test_bootstrap: bootstrap.c bootstrap.h transport.h tests/test_bootstrap.c | $(BUILD)
	$(CC) $(CFLAGS) $(WARN) -pthread bootstrap.c tests/test_bootstrap.c -o $@

$(BUILD)/test_rpc: rpc.c rpc.h wire.c wire.h tests/test_rpc.c | $(BUILD)
	$(CC) $(CFLAGS) $(WARN) rpc.c wire.c tests/test_rpc.c -o $@

test: $(BUILD)/test_verbs $(BUILD)/test_wire $(BUILD)/test_bootstrap $(BUILD)/test_rpc
	$(abspath $(BUILD))/test_verbs
	$(abspath $(BUILD))/test_wire
	$(abspath $(BUILD))/test_bootstrap
	$(abspath $(BUILD))/test_rpc

$(BUILD)/verbs_smoke: tests/verbs_smoke.c $(BUILD)/libcrt_native_verbs.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -D_POSIX_C_SOURCE=200809L $^ -libverbs -o $@

smoke: $(BUILD)/verbs_smoke

clean:
	$(RM) $(BUILD)/verbs.o $(BUILD)/wire.o $(BUILD)/libcrt_native_verbs.a $(BUILD)/test_verbs $(BUILD)/test_wire $(BUILD)/verbs_smoke
