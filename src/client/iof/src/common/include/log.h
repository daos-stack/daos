/* Copyright (C) 2016-2019 Intel Corporation
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
#ifndef __LOG_H__
#define __LOG_H__
#include <sys/types.h>
#include <stdio.h>

#include <inttypes.h>

#define D_LOG_USE_V2
#ifndef DD_FAC
#define DD_FAC(name) iof_##name##_logfac
#endif
#ifndef D_LOGFAC
#define D_LOGFAC DD_FAC(iof)
#endif

#include <gurt/debug_setup.h>

#define IOF_FOREACH_LOG_FAC(ACTION, arg) \
	ACTION(iof, iof, arg)            \
	ACTION(il,  ioil, arg)           \
	ACTION(cli, client, arg)         \
	ACTION(cn, cnss, arg)            \
	ACTION(ctrl, ctrlfs, arg)        \
	ACTION(ion, ionss, arg)          \
	ACTION(test,  test, arg)

IOF_FOREACH_LOG_FAC(D_LOG_DECLARE_FAC, D_NOOP)

#include <gurt/debug.h>

/* Allow changing the default so these macros can be
 * used by files that don't log to the default facility
 */

#define IOF_LOG_WARNING(fmt, ...)	\
	D_WARN(fmt "\n", ## __VA_ARGS__)

#define IOF_LOG_ERROR(fmt, ...)		\
	D_ERROR(fmt "\n", ## __VA_ARGS__)

#define IOF_LOG_DEBUG(fmt, ...)	\
	D_DEBUG(DB_ANY, fmt "\n", ## __VA_ARGS__)

#define IOF_LOG_INFO(fmt, ...)		\
	D_INFO(fmt "\n", ## __VA_ARGS__)

/* A couple of helper functions so we can append '\n'
 * without changing all of instances of the macros
 */
#define IOF_TRACE_HELPER(func, ptr, fmt, ...) \
	func(ptr, fmt "\n", ## __VA_ARGS__)

#define IOF_TRACE_DEBUG_HELPER(func, ptr, fmt, ...) \
	D_TRACE_DEBUG(DB_ANY, ptr, fmt "\n", ## __VA_ARGS__)

/* IOF_TRACE marcos defined for tracing descriptors and RPCs
 * in the logs. UP() is used to register a new descriptor -
 * this includes giving it a "type" and also a parent to build
 * a descriptor hierarchy. Then DOWN() will de-register
 * the descriptor.
 *
 * For RPCs only, LINK() is used to link an RPC to a
 * descriptor in the hierarchy. RPCs are not registered
 * (warning if UP and LINK are both called for the same pointer).
 *
 * All other logging remains the same for WARNING/ERROR/
 * DEBUG/INFO, however just takes an extra argument for the
 * lowest-level descriptor to tie the logging message to.
 */
#define IOF_TRACE_WARNING(ptr, ...)			\
	IOF_TRACE_HELPER(D_TRACE_WARN, ptr, "" __VA_ARGS__)

#define IOF_TRACE_ERROR(ptr, ...)			\
	IOF_TRACE_HELPER(D_TRACE_ERROR, ptr, "" __VA_ARGS__)

#define IOF_TRACE_DEBUG(ptr, ...)			\
	IOF_TRACE_DEBUG_HELPER(DB_ANY, ptr, "" __VA_ARGS__)

#define IOF_TRACE_INFO(ptr, ...)			\
	IOF_TRACE_HELPER(D_TRACE_INFO, ptr, "" __VA_ARGS__)

/* Register a descriptor with a parent and a type */
#define IOF_TRACE_UP(ptr, parent, type)					\
	D_TRACE_DEBUG(DB_ANY, ptr, "Registered new '%s' from %p\n",	\
		      type, parent)

/* Link an RPC to a descriptor */
#define IOF_TRACE_LINK(ptr, parent, type)			\
	D_TRACE_DEBUG(DB_ANY, ptr, "Link '%s' to %p\n", type, parent)

/* De-register a descriptor, including all aliases */
#define IOF_TRACE_DOWN(ptr)					\
	D_TRACE_DEBUG(DB_ANY, ptr, "Deregistered\n")

/* Register as root of hierarchy, used in place of IOF_TRACE_UP */
#define IOF_TRACE_ROOT(ptr, type)				\
	D_TRACE_DEBUG(DB_ANY, ptr, "Registered new '%s' as root\n", type)

/** Initialize iof log facilities */
void iof_log_init(void);
/* Close the iof log */
void iof_log_close(void);

#endif /* __LOG_H__ */
