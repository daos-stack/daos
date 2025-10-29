/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_CHECKER_H__
#define __DAOS_CHECKER_H__

#include <daos_types.h>
#include <daos/btree.h>
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

/**
 * Print a btree report as a checker message.
 *
 * \param[in] arg	Checker.
 * \param[in] type	Btree report type.
 * \param[in] fmt	Format.
 * \param[in] ...	Format's arguments.
 */
static inline void
ck_report(void *arg, enum btr_report_type type, const char *fmt, ...)
{
	struct checker *ck = arg;
	va_list         args;

	va_start(args, fmt);

	switch (type) {
	case BTR_REPORT_ERROR:
		ck_common_printf(ck, "%s%s", ck->ck_prefix, CHECKER_ERROR_INFIX);
		ck->ck_vprintf(ck, fmt, args);
		break;
	case BTR_REPORT_WARNING:
		ck_common_printf(ck, "%s%s", ck->ck_prefix, CHECKER_WARNING_INFIX);
		ck_common_printf(ck, fmt, args);
		ck->ck_warnings_num++;
		break;
	case BTR_REPORT_MSG:
		ck_common_printf(ck, "%s", ck->ck_prefix);
		ck_common_printf(ck, fmt, args);
		break;
	default:
		D_ASSERTF(0, "Unknown report type: %x\n", type);
	}

	va_end(args);
}

/** basic helpers */

#define IS_CHECKER(ck)     (unlikely((ck) != NULL))

#define IS_NOT_CHECKER(dp) (likely((ck) == NULL))

#define YES_NO_STR(cond)   ((cond) ? "yes" : "no")

/** direct print(f) macros with and without prefix */

#define CK_PRINT(ck, msg)                                                                          \
	do {                                                                                       \
		if (IS_CHECKER(ck)) {                                                              \
			(void)ck_common_printf(ck, "%s" msg, (ck)->ck_prefix);                     \
		}                                                                                  \
	} while (0)

#define CK_PRINTF(ck, fmt, ...)                                                                    \
	do {                                                                                       \
		if (IS_CHECKER(ck)) {                                                              \
			(void)ck_common_printf(ck, "%s" fmt, (ck)->ck_prefix, __VA_ARGS__);        \
		}                                                                                  \
	} while (0)

#define CK_PRINT_WO_PREFIX(ck, msg)                                                                \
	do {                                                                                       \
		if (IS_CHECKER(ck)) {                                                              \
			(void)ck_common_printf(ck, msg);                                           \
		}                                                                                  \
	} while (0)

#define CK_PRINTF_WO_PREFIX(ck, fmt, ...)                                                          \
	do {                                                                                       \
		if (IS_CHECKER(ck)) {                                                              \
			(void)ck_common_printf(ck, fmt, __VA_ARGS__);                              \
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
