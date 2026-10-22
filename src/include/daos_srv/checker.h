/**
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
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
 * Print a report as a checker message.
 *
 * \p fmt == NULL indicates that no message will be printed, and the \p arg checker object will be
 * adjusted according to \p opts e.g. increasing or decreasing the indentation level.
 *
 * \param[in] arg	Checker.
 * \param[in] opts	Report options.
 * \param[in] fmt	Format.
 * \param[in] ...	Format's arguments.
 */
static inline void
ck_report(void *arg, enum report_opts opts, const char *fmt, ...)
{
	struct checker *ck     = arg;
	const char     *prefix = (opts & REPORT_NO_PREFIX) ? "" : ck->ck_prefix;
	va_list         args;

	if (fmt == NULL) {
		switch (opts) {
		case REPORT_INDENT_INC:
			ck->ck_level++;
			ck->ck_indent_set(ck);
			return;
		case REPORT_INDENT_DEC:
			ck->ck_level--;
			ck->ck_indent_set(ck);
			return;
		default:
			D_ASSERTF(0, "Unknown report options: %x\n", opts);
		}
		return;
	}

	va_start(args, fmt);

	if (opts & REPORT_RC) {
		int rc = va_arg(args, int);

		if (rc == 0) {
			ck_report(arg, REPORT_MSG, "%s: %s.\n", fmt, CHECKER_OK_INFIX);
		} else {
			ck_report(arg, REPORT_ERROR, "%s: " DF_RC "\n", fmt, DP_RC(rc));
		}

		va_end(args);

		return;
	}

	switch (opts & ~REPORT_FLAGS_MASK) {
	case REPORT_ERROR:
		ck_common_printf(ck, "%s%s", prefix, CHECKER_ERROR_INFIX);
		ck->ck_vprintf(ck, fmt, args);
		break;
	case REPORT_WARNING:
		ck_common_printf(ck, "%s%s", prefix, CHECKER_WARNING_INFIX);
		ck->ck_vprintf(ck, fmt, args);
		ck->ck_warnings_num++;
		break;
	case REPORT_MSG:
		ck_common_printf(ck, "%s", prefix);
		ck->ck_vprintf(ck, fmt, args);
		break;
	default:
		D_ASSERTF(0, "Unknown report type: %x\n", opts);
	}

	va_end(args);
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
		if (IS_CHECKER(ck)) {                                                              \
			CK_PRINTF_WO_PREFIX(ck, CHECKER_WARNING_INFIX fmt "\n", __VA_ARGS__);      \
			++(ck)->ck_warnings_num;                                                   \
		}                                                                                  \
	} while (0)

#define CK_APPENDL_WARN(ck, msg)                                                                   \
	do {                                                                                       \
		if (IS_CHECKER(ck)) {                                                              \
			CK_PRINT_WO_PREFIX(ck, CHECKER_WARNING_INFIX msg "\n");                    \
			++(ck)->ck_warnings_num;                                                   \
		}                                                                                  \
	} while (0)

#define CK_APPENDL(ck, msg) CK_PRINT_WO_PREFIX(ck, msg "\n")

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

/** fault injection helpers */

/**
 * Proxy the fault injection location from the checker-specific definition to the
 * DAOS fault injection definition.
 *
 * \param[in] fail_loc_in Checker-specific fault location.
 *
 * \return Corresponding DAOS fault location.
 */
static inline uint64_t
checker_fi_proxy(uint64_t fail_loc_in)
{
	switch (fail_loc_in) {
	case DLCK_FAULT_POOL_OPEN_UMEM:
		return DAOS_FAULT_POOL_OPEN_UMEM;
	case DLCK_FAULT_POOL_OPEN_MAGIC:
		return DAOS_FAULT_POOL_OPEN_MAGIC;
	case DLCK_FAULT_POOL_OPEN_VERSION:
		return DAOS_FAULT_POOL_OPEN_VERSION;
	case DLCK_FAULT_POOL_OPEN_UUID:
		return DAOS_FAULT_POOL_OPEN_UUID;
	case DAOS_FAULT_CONT_TREE_INV_CLASS:
		return DAOS_FAULT_BTREE_OPEN_INV_CLASS;
	case DAOS_FAULT_CONT_TREE_UNREG_CLASS:
		return DAOS_FAULT_BTREE_OPEN_UNREG_CLASS;
	case DAOS_FAULT_CONT_TREE_FEATURES:
		return DAOS_FAULT_BTREE_FEATURES;
	case DAOS_FAULT_GC_TREE_INV_CLASS:
		return DAOS_FAULT_BTREE_OPEN_INV_CLASS;
	case DAOS_FAULT_GC_TREE_UNREG_CLASS:
		return DAOS_FAULT_BTREE_OPEN_UNREG_CLASS;
	case DAOS_FAULT_GC_TREE_FEATURES:
		return DAOS_FAULT_BTREE_FEATURES;
	case DLCK_FAULT_CONT_DOES_NOT_EXIST:
		return DAOS_FAULT_CONT_DOES_NOT_EXIST;
	default:
		D_ASSERTF(0, "Unknown fail location: %lu\n", fail_loc_in);
		return 0;
	}
}

/**
 * Enter a fault injection point for the checker.
 *
 * This function assumes you enable a proxy fault via a config file. Each of the proxy faults have
 * only one corresponding DAOS fault location. This fault location is set progrmatically to fail
 * only once. Before exit this function disables faults injected via a config file so a
 * programmatically set fault can take effect. Please call the \ref checker_fi_exit() function after
 * exiting the tested code.
 *
 * \param[in] exp_fail_locs NULL-terminated array of expected fault locations.
 * \param[out] re_enable     Whether to re-enable fault injection after exit.
 */
static inline void
checker_fi_entry(const uint64_t *exp_fail_locs, bool *re_enable)
{
	uint64_t fail_loc_out;
	int      i = 0;

	if (!d_fault_inject_is_enabled()) {
		*re_enable = false;
		return;
	}

	for (i = 0; exp_fail_locs[i] != 0; i++) {
		if (DAOS_FAIL_CHECK(exp_fail_locs[i])) {
			fail_loc_out = checker_fi_proxy(exp_fail_locs[i]);
			daos_fail_loc_set(fail_loc_out | DAOS_FAIL_ONCE);

			/**
			 * Disable faults injected via the config file so the programmatically set
			 * fault can take effect.
			 */
			d_fault_inject_disable();
			*re_enable = true;

			return;
		}
	}
}

/**
 * Exit a fault injection point for the checker.
 *
 * This function re-enables fault injection if it was previously disabled by \ref
 * checker_fi_entry().
 *
 * \param[in] re_enable Whether to re-enable fault injection after exit.
 */
static inline void
checker_fi_exit(bool re_enable)
{
	if (re_enable) {
		d_fault_inject_enable();
	}
}

#endif /** __DAOS_CHECKER_H__ */
