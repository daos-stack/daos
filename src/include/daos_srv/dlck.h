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

#define DLCK_ERROR_INFIX      "error: "

/**
 * @struct dlck_print
 *
 * Printer for DLCK purposes.
 */
struct dlck_print {
	int (*dp_printf)(const char *fmt, ...);
	int  level;
	char prefix[DLCK_PRINT_INDENT_MAX + 2]; /** ' ' and '\0' hence 2 characters */
};

#define IS_DLCK(dp)     (unlikely((dp) != NULL))

#define IS_NOT_DLCK(dp) (likely((dp) == NULL))

#define DLCK_PRINT(print, msg)                                                                     \
	do {                                                                                       \
		if (IS_DLCK(print)) {                                                              \
			(void)(print)->dp_printf("%s" msg, (print)->prefix);                       \
		}                                                                                  \
	} while (0)

#define DLCK_PRINTF(print, fmt, ...)                                                               \
	do {                                                                                       \
		if (IS_DLCK(print)) {                                                              \
			(void)print->dp_printf("%s" fmt, print->prefix, __VA_ARGS__);              \
		}                                                                                  \
	} while (0)

#define DLCK_PRINTF_ERR(print, fmt, ...) DLCK_PRINTF(print, DLCK_ERROR_INFIX fmt, __VA_ARGS__)

#define DLCK_PRINT_WO_PREFIX(print, msg)                                                           \
	do {                                                                                       \
		if (IS_DLCK(print)) {                                                              \
			(void)print->dp_printf(msg);                                               \
		}                                                                                  \
	} while (0)

#define DLCK_PRINTF_WO_PREFIX(print, fmt, ...)                                                     \
	do {                                                                                       \
		if (IS_DLCK(print)) {                                                              \
			(void)print->dp_printf(fmt, __VA_ARGS__);                                  \
		}                                                                                  \
	} while (0)

#define DLCK_YES                       true
#define DLCK_NO                        false

#define DLCK_PRINT_YES_NO(print, cond) DLCK_PRINTF_WO_PREFIX(print, "%s.\n", (cond) ? "yes" : "no")

#define DLCK_PRINT_OK(print)           DLCK_PRINT_WO_PREFIX(print, "ok.\n")

#define DLCK_PRINT_RC(print, rc)                                                                   \
	DLCK_PRINTF_WO_PREFIX(print, DLCK_ERROR_INFIX DF_RC "\n", DP_RC(rc))

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

#define DLCK_DEBUG(dp, flag, fmt, ...)                                                             \
	do {                                                                                       \
		if (IS_DLCK(dp)) {                                                                 \
			DLCK_PRINTF(dp, fmt, __VA_ARGS__);                                         \
		} else {                                                                           \
			D_DEBUG(flag, fmt, __VA_ARGS__);                                           \
		}                                                                                  \
	} while (0)

#define DLCK_LOG(dp, level, fmt, ...)                                                              \
	do {                                                                                       \
		if (IS_DLCK(dp)) {                                                                 \
			DLCK_PRINTF(dp, fmt, __VA_ARGS__);                                         \
		} else {                                                                           \
			D_##level(fmt, __VA_ARGS__);                                               \
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
			DLCK_PRINT(dp, msg);                                                       \
			DLCK_PRINT_YES_NO(dp, cond);                                               \
		} else {                                                                           \
			D_ASSERT(cond);                                                            \
		}                                                                                  \
	} while (0)

/**
 * Validate the integrity of a btree.
 *
 * \param[in]	toh	Tree handle.
 * \param[in]	dp	DLCK print utility.
 *
 * \retval DER_SUCCESS		The tree is correct.
 * \retval -DER_NOTYPE		The tree is malformed.
 * \retval -DER_NONEXIST	The tree is malformed.
 * \retval -DER_*		Possibly other errors.
 */
int
dlck_dbtree_check(daos_handle_t toh, struct dlck_print *dp);

#endif /** __DAOS_DLCK_H__ */
