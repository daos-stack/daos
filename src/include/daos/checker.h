/**
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_CHECKER_H__
#define __DAOS_CHECKER_H__

#include <stdarg.h>
#include <gurt/common.h>

#define CHECKER_INDENT_MAX 10

/**
 * @enum checker_event
 *
 * Checker event types.
 */
enum checker_event {
	CHECKER_EVENT_INVALID = -1,
	CHECKER_EVENT_ERROR   = 0,
	CHECKER_EVENT_WARNING,
};

/**
 * @struct checker_options
 *
 * Checker control options.
 */
struct checker_options {
	enum checker_event cko_non_zero_padding;
};

/**
 * @struct checker
 *
 * Checker state.
 */
struct checker {
	/** input */
	void                  *ck_private;
	struct checker_options ck_options;
	/** state */
	int                    ck_level;
	char                  *ck_prefix;
	int (*ck_indent_set)(struct checker *ck);
	/** output */
	int (*ck_vprintf)(struct checker *ck, const char *fmt, va_list ap);
	unsigned ck_warnings_num;
};

#define CHECKER_ERROR_INFIX   "error: "
#define CHECKER_WARNING_INFIX "warning: "
#define CHECKER_OK_INFIX      "ok"

/** helpers */

/**
 * Simple argument translation ... -> va_list
 *
 * \param[in] ck	Checker to call.
 * \param[in] fmt	Format.
 * \param[in] ...	Format's arguments.
 *
 * \retval DER_SUCCESS	Success.
 * \retval -DER_*	Error.
 */
static inline int
ck_common_printf(struct checker *ck, const char *fmt, ...)
{
	va_list args;
	int     rc;

	va_start(args, fmt);
	rc = ck->ck_vprintf(ck, fmt, args);
	va_end(args);

	return rc;
}

/** basic helpers */

/**
 * The IS_CHECKER and IS_NOT_CHECKER macros do two things:
 * 1. Check whether the checker is present (non-NULL) or absent (NULL).
 * 2. Provide branch-prediction hints.
 *
 * The checker code resides in the same binaries as the main execution code. It is essential that
 * adding the checker code does not slow down the main execution path. Therefore, branch-prediction
 * hints are necessary to avoid degrading performance on the main execution path.
 */
#define IS_CHECKER(ck)     (unlikely((ck) != NULL))
#define IS_NOT_CHECKER(ck) (likely((ck) == NULL))

#define YES_NO_STR(cond)   ((cond) ? "yes" : "no")

/** direct printf macros with and without prefix */

#define CK_PRINTF(ck, fmt, ...)                                                                    \
	do {                                                                                       \
		if (IS_CHECKER(ck)) {                                                              \
			(void)ck_common_printf(ck, "%s" fmt, (ck)->ck_prefix, ##__VA_ARGS__);      \
		}                                                                                  \
	} while (0)

#define CK_PRINTF_WO_PREFIX(ck, fmt, ...)                                                          \
	do {                                                                                       \
		if (IS_CHECKER(ck)) {                                                              \
			(void)ck_common_printf(ck, fmt, ##__VA_ARGS__);                            \
		}                                                                                  \
	} while (0)

/** append + new line shortcuts */

#define CK_APPENDL_OK(ck) CK_PRINTF_WO_PREFIX(ck, CHECKER_OK_INFIX ".\n")

#define CK_APPENDL_RC(ck, rc)                                                                      \
	do {                                                                                       \
		if (rc == DER_SUCCESS) {                                                           \
			CK_APPENDL_OK(ck);                                                         \
		} else {                                                                           \
			CK_PRINTF_WO_PREFIX(ck, CHECKER_ERROR_INFIX DF_RC "\n", DP_RC(rc));        \
		}                                                                                  \
	} while (0)

#define CK_APPENDFL_ERR(ck, fmt, ...)                                                              \
	CK_PRINTF_WO_PREFIX(ck, CHECKER_ERROR_INFIX fmt "\n", ##__VA_ARGS__)

#define CK_APPENDFL_WARN(ck, fmt, ...)                                                             \
	do {                                                                                       \
		if (IS_CHECKER(ck)) {                                                              \
			CK_PRINTF_WO_PREFIX(ck, CHECKER_WARNING_INFIX fmt "\n", ##__VA_ARGS__);    \
			++(ck)->ck_warnings_num;                                                   \
		}                                                                                  \
	} while (0)

#define CK_APPENDL(ck, msg) CK_PRINTF_WO_PREFIX(ck, msg "\n")

/** printf + return code  + new line shortcuts */

#define CK_PRINTFL_RC(ck, rc, fmt, ...)                                                            \
	do {                                                                                       \
		if (rc == DER_SUCCESS) {                                                           \
			CK_PRINTF(ck, fmt ": " CHECKER_OK_INFIX ".\n", ##__VA_ARGS__);             \
		} else {                                                                           \
			CK_PRINTF(ck, CHECKER_ERROR_INFIX fmt ": " DF_RC "\n", ##__VA_ARGS__,      \
				  DP_RC(rc));                                                      \
		}                                                                                  \
	} while (0)

/**
 * An assert while run without a checker. A checker message otherwise.
 *
 * \param[in] ck	Checker's state.
 * \param[in] msg	Message to print.
 * \param[in] cond	Condition to assert (without a checker) or condition to check (with a
 * checker).
 */
#define CK_ASSERT(ck, msg, cond)                                                                   \
	do {                                                                                       \
		if (IS_CHECKER(ck)) {                                                              \
			CK_PRINTF(ck, msg "%s\n", YES_NO_STR(cond));                               \
		} else {                                                                           \
			D_ASSERT(cond);                                                            \
		}                                                                                  \
	} while (0)

/** manage the checker print's indentation */

static inline void
checker_print_indent_inc(struct checker *ck)
{
	if (IS_NOT_CHECKER(ck)) {
		return;
	}

	if (ck->ck_level == CHECKER_INDENT_MAX) {
		CK_PRINTF(ck, "Max indent reached.\n");
		return;
	}

	ck->ck_level++;
	ck->ck_indent_set(ck);
}

static inline void
checker_print_indent_dec(struct checker *ck)
{
	if (IS_NOT_CHECKER(ck)) {
		return;
	}

	if (ck->ck_level == 0) {
		CK_PRINTF(ck, "Min indent reached.\n");
		return;
	}

	ck->ck_level--;
	ck->ck_indent_set(ck);
}

#define CK_INDENT(ck, exp)                                                                         \
	do {                                                                                       \
		checker_print_indent_inc(ck);                                                      \
		exp;                                                                               \
		checker_print_indent_dec(ck);                                                      \
	} while (0)

#endif /** __DAOS_CHECKER_H__ */
