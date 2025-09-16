/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_DLCK_H__
#define __DAOS_DLCK_H__

#include <daos_types.h>
#include <daos/common.h>

#define DLCK_PRINT_INDENT_MAX 10
#define DLCK_PRINT_INDENT     '-'

/**
 * @enum dlck_event
 *
 * DLCK event types.
 */
enum dlck_event {
	DLCK_EVENT_INVALID = -1,
	DLCK_EVENT_ERROR   = 0,
	DLCK_EVENT_WARNING,
};

/**
 * @struct dlck_options
 *
 * DLCK control options.
 */
struct dlck_options {
	enum dlck_event non_zero_padding;
};

/**
 * @struct dlck_print
 *
 * Printer for DLCK purposes.
 */
struct dlck_print {
	/** input */
	struct dlck_options *options;
	/** printer fields */
	int (*dp_printf)(struct dlck_print *dp, const char *fmt, ...);
	void    *printf_custom;
	int      level;
	char     prefix[DLCK_PRINT_INDENT_MAX + 2]; /** ' ' and '\0' hence 2 characters */
	/** output */
	unsigned warnings_num;
};

#define DLCK_ERROR_INFIX   "error: "
#define DLCK_WARNING_INFIX "warning: "
#define DLCK_OK_INFIX      "ok"

/** basic tests and helpers */

#define IS_DLCK(dp)        (unlikely((dp) != NULL))

#define IS_NOT_DLCK(dp)    (likely((dp) == NULL))

#define YES_NO_STR(cond)   ((cond) ? "yes" : "no")

/** direct print(f) macros with and without prefix */

#define DLCK_PRINT(dp, msg)                                                                        \
	do {                                                                                       \
		if (IS_DLCK(dp)) {                                                                 \
			(void)(dp)->dp_printf(dp, "%s" msg, (dp)->prefix);                         \
		}                                                                                  \
	} while (0)

#define DLCK_PRINTF(dp, fmt, ...)                                                                  \
	do {                                                                                       \
		if (IS_DLCK(dp)) {                                                                 \
			(void)(dp)->dp_printf(dp, "%s" fmt, (dp)->prefix, __VA_ARGS__);            \
		}                                                                                  \
	} while (0)

#define DLCK_PRINT_WO_PREFIX(dp, msg)                                                              \
	do {                                                                                       \
		if (IS_DLCK(dp)) {                                                                 \
			(void)(dp)->dp_printf(dp, msg);                                            \
		}                                                                                  \
	} while (0)

#define DLCK_PRINTF_WO_PREFIX(dp, fmt, ...)                                                        \
	do {                                                                                       \
		if (IS_DLCK(dp)) {                                                                 \
			(void)(dp)->dp_printf(dp, fmt, __VA_ARGS__);                               \
		}                                                                                  \
	} while (0)

/** append + new line shortcuts */

#define DLCK_APPENDL_OK(dp) DLCK_PRINT_WO_PREFIX(dp, DLCK_OK_INFIX ".\n")

#define DLCK_APPENDL_RC(dp, rc)                                                                    \
	do {                                                                                       \
		if (rc == DER_SUCCESS) {                                                           \
			DLCK_APPENDL_OK(dp);                                                       \
		} else {                                                                           \
			DLCK_PRINTF_WO_PREFIX(dp, DLCK_ERROR_INFIX DF_RC "\n", DP_RC(rc));         \
		}                                                                                  \
	} while (0)

#define DLCK_APPENDFL_ERR(dp, fmt, ...)                                                            \
	DLCK_PRINTF_WO_PREFIX(dp, DLCK_ERROR_INFIX fmt "\n", __VA_ARGS__)

#define DLCK_APPENDFL_WARN(dp, fmt, ...)                                                           \
	do {                                                                                       \
		DLCK_PRINTF_WO_PREFIX(dp, DLCK_WARNING_INFIX fmt "\n", __VA_ARGS__);               \
		++(dp)->warnings_num;                                                              \
	} while (0)

/** print(f) + return code  + new line shortcuts */

#define DLCK_PRINTL_RC(dp, rc, msg)                                                                \
	do {                                                                                       \
		if (rc == DER_SUCCESS) {                                                           \
			DLCK_PRINT(dp, msg ": " DLCK_OK_INFIX ".\n");                              \
		} else {                                                                           \
			DLCK_PRINTF(dp, DLCK_ERROR_INFIX msg ": " DF_RC "\n", DP_RC(rc));          \
		}                                                                                  \
	} while (0)

#define DLCK_PRINTFL_RC(dp, rc, fmt, ...)                                                          \
	do {                                                                                       \
		if (rc == DER_SUCCESS) {                                                           \
			DLCK_PRINTF(dp, fmt ": " DLCK_OK_INFIX ".\n", __VA_ARGS__);                \
		} else {                                                                           \
			DLCK_PRINTF(dp, DLCK_ERROR_INFIX fmt ": " DF_RC "\n", __VA_ARGS__,         \
				    DP_RC(rc));                                                    \
		}                                                                                  \
	} while (0)

/**
 * An assert while run without DLCK. A DLCK message otherwise.
 *
 * \param[in] dp	DLCK print utility.
 * \param[in] msg	Message to print.
 * \param[in] cond	Condition to assert (without DLCK) or condition to check (with DLCK).
 */
#define DLCK_ASSERT(dp, msg, cond)                                                                 \
	do {                                                                                       \
		if (IS_DLCK(dp)) {                                                                 \
			DLCK_PRINTF(dp, msg "%s\n", YES_NO_STR(cond));                             \
		} else {                                                                           \
			D_ASSERT(cond);                                                            \
		}                                                                                  \
	} while (0)

/** manage DLCK print's indentation */

static inline void
dlck_print_indent_set(struct dlck_print *dp)
{
	memset(dp->prefix, DLCK_PRINT_INDENT, DLCK_PRINT_INDENT_MAX);
	if (dp->level > 0) {
		dp->prefix[dp->level]     = ' ';
		dp->prefix[dp->level + 1] = '\0';
	} else {
		dp->prefix[0] = '\0';
	}
}

static inline void
dlck_print_indent_inc(struct dlck_print *dp)
{
	if (IS_NOT_DLCK(dp)) {
		return;
	}

	if (dp->level == DLCK_PRINT_INDENT_MAX) {
		DLCK_PRINT(dp, "Max indent reached.\n");
		return;
	}

	dp->level++;
	dlck_print_indent_set(dp);
}

static inline void
dlck_print_indent_dec(struct dlck_print *dp)
{
	if (IS_NOT_DLCK(dp)) {
		return;
	}

	if (dp->level == 0) {
		DLCK_PRINT(dp, "Min indent reached.\n");
		return;
	}

	dp->level--;
	dlck_print_indent_set(dp);
}

#define DLCK_INDENT(print, exp)                                                                    \
	do {                                                                                       \
		dlck_print_indent_inc(dp);                                                         \
		exp;                                                                               \
		dlck_print_indent_dec(dp);                                                         \
	} while (0)

#endif /** __DAOS_DLCK_H__ */
