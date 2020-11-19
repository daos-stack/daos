/*
 * (C) Copyright 2016-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
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
