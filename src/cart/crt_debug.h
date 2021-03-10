/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
	ACTION(external, external, arg) \
	ACTION(st,    self_test,   arg)	\
	ACTION(iv,    iv,          arg)	\
	ACTION(ctl,   ctl,         arg)

#ifndef CRT_USE_GURT_FAC
CRT_FOREACH_LOG_FAC(D_LOG_DECLARE_FAC, D_NOOP)
#endif

int crt_setup_log_fac(void);

#endif /* __CRT_DEBUG_H__ */
