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

$(BUILD)/channel.o: channel.c channel.h rpc.h wire.h bootstrap.h transport.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -c $< -o $@

$(BUILD)/bulk.o: bulk.c bulk.h transport.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -c $< -o $@

$(BUILD)/proc.o: proc.c proc.h bulk.h transport.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -c $< -o $@

$(BUILD)/libcrt_native_verbs.a: $(BUILD)/verbs.o $(BUILD)/wire.o $(BUILD)/bootstrap.o $(BUILD)/rpc.o $(BUILD)/channel.o $(BUILD)/bulk.o $(BUILD)/proc.o
	$(AR) rcs $@ $^

$(BUILD)/test_verbs: verbs.c transport.h tests/test_verbs.c tests/fake/infiniband/verbs.h | $(BUILD)
	$(CC) $(CFLAGS) $(WARN) -Itests/fake verbs.c tests/test_verbs.c -o $@

$(BUILD)/test_wire: wire.c wire.h tests/test_wire.c | $(BUILD)
	$(CC) $(CFLAGS) $(WARN) wire.c tests/test_wire.c -o $@

$(BUILD)/test_bootstrap: bootstrap.c bootstrap.h transport.h tests/test_bootstrap.c | $(BUILD)
	$(CC) $(CFLAGS) $(WARN) -pthread bootstrap.c tests/test_bootstrap.c -o $@

$(BUILD)/test_rpc: rpc.c rpc.h wire.c wire.h tests/test_rpc.c | $(BUILD)
	$(CC) $(CFLAGS) $(WARN) rpc.c wire.c tests/test_rpc.c -o $@

$(BUILD)/test_channel: channel.c channel.h transport.h bootstrap.h rpc.c rpc.h wire.c wire.h tests/test_channel.c | $(BUILD)
	$(CC) $(CFLAGS) $(WARN) -pthread channel.c rpc.c wire.c tests/test_channel.c -o $@

$(BUILD)/test_bulk: bulk.c bulk.h proc.c proc.h transport.h tests/test_bulk.c | $(BUILD)
	$(CC) $(CFLAGS) $(WARN) bulk.c proc.c tests/test_bulk.c -o $@

$(BUILD)/test_proc: proc.c proc.h bulk.c bulk.h rpc.c rpc.h wire.c wire.h tests/test_proc.c | $(BUILD)
	$(CC) $(CFLAGS) $(WARN) proc.c bulk.c rpc.c wire.c tests/test_proc.c -o $@

$(BUILD)/test_cart_proc: cart_proc.c cart_proc.h proc.c proc.h bulk.c bulk.h tests/test_cart_proc.c ../../include/cart/api.h ../../include/cart/types.h | $(BUILD)
	$(CC) $(CFLAGS) $(WARN) -pthread -Itests/compat -I../../include cart_proc.c proc.c bulk.c tests/test_cart_proc.c -o $@

test: $(BUILD)/test_verbs $(BUILD)/test_wire $(BUILD)/test_bootstrap $(BUILD)/test_rpc $(BUILD)/test_channel $(BUILD)/test_bulk $(BUILD)/test_proc $(BUILD)/test_cart_proc
	$(abspath $(BUILD))/test_verbs
	$(abspath $(BUILD))/test_wire
	$(abspath $(BUILD))/test_bootstrap
	$(abspath $(BUILD))/test_rpc
	$(abspath $(BUILD))/test_channel
	$(abspath $(BUILD))/test_bulk
	$(abspath $(BUILD))/test_proc
	$(abspath $(BUILD))/test_cart_proc

$(BUILD)/verbs_smoke: tests/verbs_smoke.c $(BUILD)/libcrt_native_verbs.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -D_POSIX_C_SOURCE=200809L $^ -libverbs -o $@

smoke: $(BUILD)/verbs_smoke

clean:
	$(RM) $(BUILD)/verbs.o $(BUILD)/wire.o $(BUILD)/bootstrap.o $(BUILD)/rpc.o $(BUILD)/channel.o
	$(RM) $(BUILD)/libcrt_native_verbs.a $(BUILD)/test_verbs $(BUILD)/test_wire $(BUILD)/test_bootstrap
	$(RM) $(BUILD)/test_rpc $(BUILD)/test_channel $(BUILD)/verbs_smoke
	$(RM) $(BUILD)/bulk.o $(BUILD)/test_bulk
	$(RM) $(BUILD)/proc.o $(BUILD)/test_proc
	$(RM) $(BUILD)/test_cart_proc
