/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __UT_DLCK_CMD_MOCK_H__
#define __UT_DLCK_CMD_MOCK_H__

#include "../dlck_engine.h"

void
mock_exec_one(void *arg);

int
mock_arg_alloc_fn(struct dlck_engine *engine, int idx, void *custom, void **arg);

void
mock_arg_free_fn(void *custom, void **arg);

#define EXPECT_mock_arg_alloc_fn(ENGINE, IDX, RC, PTR)                                             \
	do {                                                                                       \
		expect_value(mock_arg_alloc_fn, engine, ENGINE);                                   \
		expect_value(mock_arg_alloc_fn, idx, IDX);                                         \
		expect_value(mock_arg_alloc_fn, custom, &Custom_alloc);                            \
		will_return(mock_arg_alloc_fn, RC);                                                \
		if ((RC) == DER_SUCCESS) {                                                         \
			will_return(mock_arg_alloc_fn, PTR);                                       \
		}                                                                                  \
	} while (0)

#define EXPECT_mock_arg_free_fn(PTR)                                                               \
	do {                                                                                       \
		expect_value(mock_arg_free_fn, custom, &Custom_free);                              \
		expect_value(mock_arg_free_fn, ptr, PTR);                                          \
	} while (0)

#endif /* __UT_DLCK_CMD_MOCK_H__ */