/*
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __GURT_DEBUG_SETUP_H__
#define __GURT_DEBUG_SETUP_H__

#include <gurt/dlog.h>
/**
 * \file
 * Debug setup macros
 */

/** @addtogroup GURT_DEBUG
 * @{
 */
#define DD_GURT_FAC(name) d_##name##_logfac
#ifndef DD_FAC
/** User definable facility name to variable name macro */
#define DD_FAC DD_GURT_FAC
#define D_USE_GURT_FAC
#endif /* !DD_FAC */

#define DD_FAC_DECL(name) DD_FAC(name)

#ifndef D_LOGFAC
#define D_LOGFAC DD_GURT_FAC(misc)
#endif

/**
 * Arguments to priority bit macros are
 *      flag            Variable name of the priority bit flag
 *      s_name          Short name of the flag
 *      l_name          Long name of the flag
 *      default_mask    Should always be 0 for debug bits
 *      arg             Argument passed along.  Use D_NOOP when not required
 *
 * \note DB_ALL is special in that it sets all bits in the bitfield. If one
 *       wants to always log, when any debug is enabled, use DB_ALL instead of
 *       DB_ANY.
 */
#define D_FOREACH_GURT_DB(ACTION, arg)                                                             \
	/** Set all debug bits */                                                                  \
	ACTION(DB_ALL, all, all, 0, arg)                                                           \
	/** Stream for uncategorized messages */                                                   \
	ACTION(DB_ANY, any, any, 0, arg)                                                           \
	/** Extremely verbose debug stream */                                                      \
	ACTION(DB_TRACE, trace, trace, 0, arg)                                                     \
	/** Memory operations */                                                                   \
	ACTION(DB_MEM, mem, mem, 0, arg)                                                           \
	/** Network operations */                                                                  \
	ACTION(DB_NET, net, net, 0, arg)                                                           \
	/** I/O operations */                                                                      \
	ACTION(DB_IO, io, io, 0, arg)                                                              \
	/** Test debug stream */                                                                   \
	ACTION(DB_TEST, test, test, 0, arg)

/** A few internal macros for argument manipulation */
#define DD_CONCAT_CACHE(x, y)                                 x##_cache
#define DD_CONCAT_FLAG(x, y)                                  DD_FLAG_##x##_##y
#define DD_CONCAT(x, y, op)                                   op(x, y)
#define DD_CACHE(fac)                                         DD_CONCAT(fac, D_NOOP, DD_CONCAT_CACHE)
#define DD_FLAG_NAME(mask, fac)                               DD_CONCAT(mask, fac, DD_CONCAT_FLAG)
#define DD_FLAG(mask, fac)                                    DD_CACHE(fac)[DD_FLAG_NAME(mask, fac) - 1]

/* These macros are intended to be used with FOREACH macros that define
 * debug bits for your library.   See D_FOREACH_GURT_DB for an example.  Not
 * all fields are used in individual macros.
 *
 * These macros can also be used standalone but are not quite as convenient
 * due to required but unused arguments
 */

/** Declare an extern for a debug flag variable
 *
 * \param flag		The name of the variable used to store the value of the
 *			debug bit.
 * \param s_name	Unused
 * \param l_name	Unused
 * \param mask		Unused
 * \param arg		Unused
 */
#define D_LOG_DECLARE_DB(flag, s_name, l_name, mask, arg)     extern d_dbug_t flag;

/** Instantiate a debug flag variable
 *
 * \param flag		The name of the variable used to store the value of the
 *			debug bit.
 * \param s_name	Unused
 * \param l_name	Unused
 * \param mask		Unused
 * \param arg		Unused
 */
#define D_LOG_INSTANTIATE_DB(flag, s_name, l_name, mask, arg) d_dbug_t flag;

/** Internal use macro as part of D_LOG_REGISTER_DB.  It's multiline on purpose
 *  so we can break from the foreach loop.  Checkpatch will complain but don't
 *  see a better way.
 */
#define _D_LOG_ALLOCATE_DBG_BIT(dbgbit, s_name, l_name, mask, rc)                                  \
	rc = d_log_dbg_bit_alloc(&dbgbit, #s_name, #l_name);                                       \
	if (rc < 0) {                                                                              \
		rc = -DER_UNINIT;                                                                  \
		D_PRINT_ERR("Could not get debug bit " #s_name "\n");                              \
		break;                                                                             \
	}

/** Internal use macro as part of D_LOG_DEREGISTER_DB.  It's multiline on purpose
 *  so we can break from the foreach loop.  Checkpatch will complain but don't
 *  see a better way.
 */
#define _D_LOG_DEALLOCATE_DBG_BIT(dbgbit, s_name, l_name, mask, rc)                                \
	rc = d_log_dbg_bit_dealloc(#s_name);                                                       \
	if (rc < 0) {                                                                              \
		rc = -DER_UNINIT;                                                                  \
		D_PRINT_ERR("Could not free debug bit " #s_name "\n");                             \
		break;                                                                             \
	}

/** Register log facilities at runtime
 *
 * \param db_foreach	Optional debug bit foreach
 *
 * \return 0 on success, error otherwise
 */
#define D_LOG_REGISTER_DB(db_foreach)                                                              \
	({                                                                                         \
		int __rc = 0;                                                                      \
		do {                                                                               \
			db_foreach(_D_LOG_ALLOCATE_DBG_BIT, __rc)                                  \
		} while (0);                                                                       \
		__rc;                                                                              \
	})

/** Deregister log facilities at runtime
 *
 * \param db_foreach	Optional debug bit foreach
 *
 * \return 0 on success, error otherwise
 */
#define D_LOG_DEREGISTER_DB(db_foreach)                                                            \
	({                                                                                         \
		int __rc = 0;                                                                      \
		do {                                                                               \
			db_foreach(_D_LOG_DEALLOCATE_DBG_BIT, __rc)                                \
		} while (0);                                                                       \
		__rc;                                                                              \
	})

/** Declare an enumeration to identify a debug flag, facility combination
 *
 * \param flag		The name of the variable used to store the value of the
 *			debug bit.
 * \param s_name	Unused
 * \param l_name	Unused
 * \param mask		Unused
 * \param fac		The log facility
 */
#define _D_LOG_DECLARE_ENUM(flag, s_name, l_name, mask, fac)     DD_FLAG_NAME(flag, fac),

/** Internal macro for initializing facility cache defined by DD_*_CACHE */
#define _D_LOG_INITIALIZE_FIELD(flag, s_name, l_name, mask, fac) DLOG_UNINIT,

/**
 * These macros are intended to be used with FOREACH macros that define log
 * facilities in your library.   It will utilize internal FOREACH macros to
 * define debug bits as well as user defined macros.  See D_FOREACH_GURT_FAC for
 * an example.
 *
 * These macros can also be used standalone but are not quite as convenient due
 * to required but unused arguments
 */

/** Internal macro to declare a facility cache.  Used by DD_DECLARE_FAC */
#define _D_LOG_DECLARE_CACHE(s_name, user_dbg_bits)                                                \
	enum {                                                                                     \
		DD_FLAG_NAME(s_name, START),                                                       \
		D_FOREACH_PRIO_MASK(_D_LOG_DECLARE_ENUM, DD_FAC(s_name))                           \
		    D_FOREACH_GURT_DB(_D_LOG_DECLARE_ENUM, DD_FAC(s_name))                         \
			user_dbg_bits(_D_LOG_DECLARE_ENUM, DD_FAC(s_name))                         \
			    DD_FLAG_NAME(s_name, END),                                             \
	};                                                                                         \
	extern int DD_CACHE(DD_FAC(s_name))[DD_FLAG_NAME(s_name, END) - 1];

/** Internal macro to create a facility cache.  Used by DD_INSTANTIATE_FAC */
#define _D_LOG_INSTANTIATE_CACHE(s_name, user_dbg_bits)                                            \
	int DD_CACHE(DD_FAC(s_name))[DD_FLAG_NAME(s_name, END) - 1] = {                            \
	    D_FOREACH_PRIO_MASK(_D_LOG_INITIALIZE_FIELD, D_NOOP)                                   \
		D_FOREACH_GURT_DB(_D_LOG_INITIALIZE_FIELD, D_NOOP)                                 \
		    user_dbg_bits(_D_LOG_INITIALIZE_FIELD, D_NOOP)};

/** Internal macro to register a facility cache.  Used by
 *  ALLOCATE_LOG_FACILITY
 */
#define _D_ADD_CACHE(s_name)                                                                       \
	d_log_add_cache(DD_CACHE(DD_FAC(s_name)), ARRAY_SIZE(DD_CACHE(DD_FAC(s_name))));

/** Declare an extern to global facility variable
 *
 * \param s_name	Short name for the facility
 * \param l_name	Unused
 * \param user_dbg_bits	Unused
 */
#define D_LOG_DECLARE_FAC(s_name, l_name, user_dbg_bits)                                           \
	extern int DD_FAC_DECL(s_name);                                                            \
	_D_LOG_DECLARE_CACHE(s_name, user_dbg_bits)

/** Declare a global facility variable
 *
 * \param s_name	Short name for the facility
 * \param l_name	Unused
 * \param user_dbg_bits	Unused
 */
#define D_LOG_INSTANTIATE_FAC(s_name, l_name, user_dbg_bits)                                       \
	int DD_FAC_DECL(s_name);                                                                   \
	_D_LOG_INSTANTIATE_CACHE(s_name, user_dbg_bits)

/** Internal use macro as part of DD_REGISTER_FAC.  It's multiline on purpose
 *  so we can break from the foreach loop.  Checkpatch will complain but don't
 *  see a better way.
 */
#define _D_LOG_ALLOCATE_LOG_FACILITY(s_name, l_name, rc)                                           \
	rc = d_init_log_facility(&DD_FAC(s_name), #s_name, #l_name);                               \
	if (rc != 0) {                                                                             \
		rc = -DER_UNINIT;                                                                  \
		D_PRINT_ERR("Could not allocate " #s_name "\n");                                   \
		break;                                                                             \
	}                                                                                          \
	_D_ADD_CACHE(s_name);

/** Register log facilities at runtime
 *
 * \param fac_foreach	The foreach to declare log facilities
 *
 * \return 0 on success, error otherwise
 */
#define D_LOG_REGISTER_FAC(fac_foreach)                                                            \
	({                                                                                         \
		int __rc = 0;                                                                      \
		do {                                                                               \
			fac_foreach(_D_LOG_ALLOCATE_LOG_FACILITY, __rc)                            \
		} while (0);                                                                       \
		__rc;                                                                              \
	})
/** @}
 */
#endif /* __GURT_DEBUG_SETUP_H__ */
