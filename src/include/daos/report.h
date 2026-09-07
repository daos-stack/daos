/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_REPORT_H__
#define __DAOS_REPORT_H__

enum report_opts {
	REPORT_ERROR,
	REPORT_WARNING,
	REPORT_MSG,
	/** flags occupy the highest bits */
	REPORT_NO_PREFIX  = (1 << 29),
	REPORT_INDENT_INC = (1 << 30),
	REPORT_INDENT_DEC = (1 << 31),
	REPORT_FLAGS_MASK = (REPORT_NO_PREFIX | REPORT_INDENT_INC | REPORT_INDENT_DEC),
};

typedef void (*report_fn_t)(void *arg, enum report_opts opts, const char *fmt, ...);

static inline void
report_fn_nop(void *arg, enum report_opts ops, const char *fmt, ...)
{
}

#endif /* __DAOS_REPORT_H__ */
