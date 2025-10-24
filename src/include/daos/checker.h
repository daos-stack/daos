/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_CHECKER_H__
#define __DAOS_CHECKER_H__

#include <daos_types.h>
#include <daos/common.h>
#include <daos/mem.h>

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
	int (*ck_printf)(struct checker *ck, const char *fmt, ...);
	unsigned ck_warnings_num;
};

#define CHECKER_ERROR_INFIX   "error: "
#define CHECKER_WARNING_INFIX "warning: "
#define CHECKER_OK_INFIX      "ok"

/** basic tests and helpers */

#define IS_CHECKER(ck)        (unlikely((ck) != NULL))

#define IS_NOT_CHECKER(dp)    (likely((ck) == NULL))

#define YES_NO_STR(cond)      ((cond) ? "yes" : "no")

/** direct print(f) macros with and without prefix */

#define CK_PRINT(ck, msg)                                                                          \
	do {                                                                                       \
		if (IS_CHECKER(ck)) {                                                              \
			(void)(ck)->ck_printf(ck, "%s" msg, (ck)->ck_prefix);                      \
		}                                                                                  \
	} while (0)

#define CK_PRINTF(ck, fmt, ...)                                                                    \
	do {                                                                                       \
		if (IS_CHECKER(ck)) {                                                              \
			(void)(ck)->ck_printf(ck, "%s" fmt, (ck)->ck_prefix, __VA_ARGS__);         \
		}                                                                                  \
	} while (0)

#define CK_PRINT_WO_PREFIX(ck, msg)                                                                \
	do {                                                                                       \
		if (IS_CHECKER(ck)) {                                                              \
			(void)(ck)->ck_printf(ck, msg);                                            \
		}                                                                                  \
	} while (0)

#define CK_PRINTF_WO_PREFIX(ck, fmt, ...)                                                          \
	do {                                                                                       \
		if (IS_CHECKER(ck)) {                                                              \
			(void)(ck)->ck_printf(ck, fmt, __VA_ARGS__);                               \
		}                                                                                  \
	} while (0)

/** append + new line shortcuts */

#define CK_APPENDL_OK(ck) CK_PRINT_WO_PREFIX(ck, CHECKER_OK_INFIX ".\n")

#define CK_APPENDL_RC(ck, rc)                                                                      \
	do {                                                                                       \
		if (rc == DER_SUCCESS) {                                                           \
			CK_APPENDL_OK(ck);                                                         \
		} else {                                                                           \
			CK_PRINTF_WO_PREFIX(ck, CHECKER_ERROR_INFIX DF_RC "\n", DP_RC(rc));        \
		}                                                                                  \
	} while (0)

#define CK_APPENDFL_ERR(ck, fmt, ...)                                                              \
	CK_PRINTF_WO_PREFIX(ck, CHECKER_ERROR_INFIX fmt "\n", __VA_ARGS__)

#define CK_APPENDFL_WARN(ck, fmt, ...)                                                             \
	do {                                                                                       \
		CK_PRINTF_WO_PREFIX(ck, CHECKER_WARNING_INFIX fmt "\n", __VA_ARGS__);              \
		++(ck)->ck_warnings_num;                                                           \
	} while (0)

/** print(f) + return code  + new line shortcuts */

#define CK_PRINTL_RC(ck, rc, msg)                                                                  \
	do {                                                                                       \
		if (rc == DER_SUCCESS) {                                                           \
			CK_PRINT(ck, msg ": " CHECKER_OK_INFIX ".\n");                             \
		} else {                                                                           \
			CK_PRINTF(ck, CHECKER_ERROR_INFIX msg ": " DF_RC "\n", DP_RC(rc));         \
		}                                                                                  \
	} while (0)

#define CK_PRINTFL_RC(ck, rc, fmt, ...)                                                            \
	do {                                                                                       \
		if (rc == DER_SUCCESS) {                                                           \
			CK_PRINTF(ck, fmt ": " CHECKER_OK_INFIX ".\n", __VA_ARGS__);               \
		} else {                                                                           \
			CK_PRINTF(ck, CHECKER_ERROR_INFIX fmt ": " DF_RC "\n", __VA_ARGS__,        \
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
		CK_PRINT(ck, "Max indent reached.\n");
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
		CK_PRINT(ck, "Min indent reached.\n");
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
