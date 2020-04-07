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

#ifndef __CRT_DEBUG_H__
#define __CRT_DEBUG_H__

#ifndef CRT_USE_GURT_FAC
#define DD_FAC(name)	crt_##name##_logfac
#endif

#ifndef D_LOGFAC
#define D_LOGFAC DD_FAC(crt)
#endif

#include <gurt/dlog.h>
#include <gurt/debug_setup.h>

#define CRT_FOREACH_LOG_FAC(ACTION, arg)	\
	ACTION(crt,   cart,        arg)	\
	ACTION(rpc,   rpc,         arg)	\
	ACTION(bulk,  bulk,        arg)	\
	ACTION(corpc, corpc,       arg)	\
	ACTION(grp,   group,       arg)	\
	ACTION(lm,    livenessmap, arg)	\
	ACTION(hg,    mercury,     arg)	\
	ACTION(pmix,  pmix,        arg)	\
	ACTION(st,    self_test,   arg)	\
	ACTION(iv,    iv,          arg)	\
	ACTION(ctl,   ctl,         arg)

#ifndef CRT_USE_GURT_FAC
CRT_FOREACH_LOG_FAC(D_LOG_DECLARE_FAC, D_NOOP)
#endif

int crt_setup_log_fac(void);

#endif /* __CRT_DEBUG_H__ */
