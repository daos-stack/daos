/* Copyright (C) 2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __GURT_DEBUG_SETUP_H__
#define __GURT_DEBUG_SETUP_H__

#include <gurt/dlog.h>
/**
 * \file
 * Debug macros and functions
 */

/** @addtogroup GURT_DEBUG
 * @{
 */
#define DD_FAC(name)	(d_##name##_logfac)

#define _D_LOG_FAC_DECL(name)	DD_FAC(name)

#ifndef D_LOGFAC
#define D_LOGFAC	DD_FAC(misc)
#endif

/** Arguments to priority bit macros are
 *      flag            Variable name of the priority bit flag
 *      s_name          Short name of the flag
 *      l_name          Long name of the flag
 *      default_mask    Should always be 0 for debug bits
 *      arg             Argument passed along.  Use D_NOOP when not required
 */
#define D_FOREACH_GURT_DB(ACTION, arg)          \
	/** All debug streams */                \
	ACTION(DB_ALL,   all,   all,   0, arg)  \
	/** Generic debug stream */             \
	ACTION(DB_ANY,   any,   any,   0, arg)  \
	/** Extremely verbose debug stream */   \
	ACTION(DB_TRACE, trace, trace, 0, arg)  \
	/** Memory operations */                \
	ACTION(DB_MEM,   mem,   mem,   0, arg)  \
	/** Network operations */               \
	ACTION(DB_NET,   net,   net,   0, arg)  \
	/** I/O operations */                   \
	ACTION(DB_IO,    io,    io,    0, arg)  \
	/** Test debug stream */                \
	ACTION(DB_TEST,  test,  test,  0, arg)

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
#define D_LOG_DECLARE_DB(flag, s_name, l_name, mask, arg)	\
	extern d_dbug_t	flag;

/** Instantiate a debug flag variable
 *
 * \param flag		The name of the variable used to store the value of the
 *			debug bit.
 * \param s_name	Unused
 * \param l_name	Unused
 * \param mask		Unused
 * \param arg		Unused
 */
#define D_LOG_INSTANTIATE_DB(flag, s_name, l_name, mask, arg)	\
	d_dbug_t	flag;

/** Internal use macro as part of D_LOG_REGISTER_DB.  It's multiline on purpose
 *  so we can break from the foreach loop.  Checkpatch will complain but don't
 *  see a better way.
 */
#define _D_LOG_ALLOCATE_DBG_BIT(dbgbit, s_name, l_name, mask, rc)	\
	rc = d_log_dbg_bit_alloc(&dbgbit, #s_name, #l_name);		\
	if (rc < 0)	{						\
		rc = -DER_UNINIT;					\
		D_PRINT_ERR("Could not get debug bit " #s_name "\n");	\
		break;							\
	}

/** Internal use macro as part of D_LOG_DEREGISTER_DB.  It's multiline on purpose
 *  so we can break from the foreach loop.  Checkpatch will complain but don't
 *  see a better way.
 */
#define _D_LOG_DEALLOCATE_DBG_BIT(dbgbit, s_name, l_name, mask, rc)	\
	rc = d_log_dbg_bit_dealloc(#s_name);				\
	if (rc < 0)	{						\
		rc = -DER_UNINIT;					\
		D_PRINT_ERR("Could not free debug bit " #s_name "\n");	\
		break;							\
	}

#define D_LOG_REGISTER_DB(db_foreach)					\
	({								\
		int __rc = 0;						\
		do {							\
			db_foreach(_D_LOG_ALLOCATE_DBG_BIT, __rc)	\
		} while (0);						\
		__rc;							\
	})

#define D_LOG_DEREGISTER_DB(db_foreach)					\
	({								\
		int __rc = 0;						\
		do {							\
			db_foreach(_D_LOG_DEALLOCATE_DBG_BIT, __rc)	\
		} while (0);						\
		__rc;							\
	})

/** These macros are used intended to be used with FOREACH macros that define
 *  log facilities in your library.   It will utilize internal FOREACH macros
 *  to define debug bits as well as user defined macros.  See
 *  D_FOREACH_GURT_FAC for an example.
 *
 * These macros can also be used standalone but are not quite as convenient
 * due to required but unused arguments
 */

/** Declare an extern to global facility variable
 *
 * \param s_name	Short name for the facility
 * \param l_name	Unused
 * \param user_dbg_bits	Unused
 */
#define D_LOG_DECLARE_FAC(s_name, l_name, user_dbg_bits)	\
	extern int _D_LOG_FAC_DECL(s_name);			\

/** Declare a global facility variable
 *
 * \param s_name	Short name for the facility
 * \param l_name	Unused
 * \param user_dbg_bits	Unused
 */
#define D_LOG_INSTANTIATE_FAC(s_name, l_name, user_dbg_bits)	\
	int _D_LOG_FAC_DECL(s_name);				\


/** Internal use macro as part of D_LOG_REGISTER_FAC.  It's multiline on purpose
 *  so we can break from the foreach loop.  Checkpatch will complain but don't
 *  see a better way.
 */
#define _D_LOG_ALLOCATE_LOG_FACILITY(s_name, l_name, rc)		\
	rc = d_init_log_facility(&DD_FAC(s_name), #s_name, #l_name);	\
	if (rc != 0) {							\
		rc = -DER_UNINIT;					\
		D_PRINT_ERR("Could not allocate " #s_name "\n");	\
		break;							\
	}

#define D_LOG_REGISTER_FAC(fac_foreach)					\
	({								\
		int __rc = 0;						\
		do {							\
			fac_foreach(_D_LOG_ALLOCATE_LOG_FACILITY, __rc)	\
		} while (0);						\
		__rc;							\
	})
/** @}
 */
#endif /* __GURT_DEBUG_SETUP_H__ */
