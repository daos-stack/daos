/*
 * Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef __UT_DLCK_XSTREAM_MOCK_H__
#define __UT_DLCK_XSTREAM_MOCK_H__

#include "../dlck_xstream.h"

#define TARGETS_MAX        3
#define SYS_TAG            (DAOS_SERVER_TAG - DAOS_TGT_TAG)

/** per-execution-stream sentinels so that resource mix-ups between the streams are caught */
#define MOCK_POOL(IDX)     ((ABT_pool)(uintptr_t)(0x1000 + (IDX)))
#define MOCK_EVENTUAL(IDX) ((ABT_eventual)(uintptr_t)(0x2000 + (IDX)))
#define MOCK_THREAD(IDX)   ((ABT_thread)(uintptr_t)(0x3000 + (IDX)))
#define MOCK_XSTREAM(IDX)  ((ABT_xstream)(uintptr_t)(0x4000 + (IDX)))

#define EXPECT_DLCK_XSTREAM_CREATE(XS, TGT_ID, RC, STREAM, POOL)                                   \
	do {                                                                                       \
		expect_value(dlck_xstream_create, xs, XS);                                         \
		expect_value(dlck_xstream_create, xs->tgt_id, TGT_ID);                             \
		will_return(dlck_xstream_create, RC);                                              \
		if ((RC) == DER_SUCCESS) {                                                         \
			will_return(dlck_xstream_create, STREAM);                                  \
			will_return(dlck_xstream_create, POOL);                                    \
		}                                                                                  \
	} while (0)

#define EXPECT_DLCK_XSTREAM_INIT(XS, RC)                                                           \
	do {                                                                                       \
		expect_value(dlck_xstream_init, xs, XS);                                           \
		will_return(dlck_xstream_init, RC);                                                \
	} while (0)

#define EXPECT_DLCK_XSTREAM_STOP(XS, RC)                                                           \
	do {                                                                                       \
		expect_value(dlck_xstream_stop, xs, XS);                                           \
		will_return(dlck_xstream_stop, RC);                                                \
	} while (0)

#define EXPECT_DLCK_XSTREAM_STOP_ALL_NO_ERROR(XSS, TARGETS)                                        \
	do {                                                                                       \
		expect_value(dlck_xstream_stop_all_no_error, xss, XSS);                            \
		expect_value(dlck_xstream_stop_all_no_error, targets, TARGETS);                    \
		expect_function_call(dlck_xstream_stop_all_no_error);                              \
	} while (0)

#endif /* __UT_DLCK_XSTREAM_MOCK_H__ */
