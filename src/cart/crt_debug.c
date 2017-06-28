/* Copyright (C) 2017-2018 Intel Corporation
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
#include <crt_internal.h>

#define DECLARE_FAC(name) int DD_FAC(name)

DECLARE_FAC(rpc);
DECLARE_FAC(bulk);
DECLARE_FAC(corpc);
DECLARE_FAC(grp);
DECLARE_FAC(lm);
DECLARE_FAC(hg);
DECLARE_FAC(pmix);
DECLARE_FAC(self_test);
DECLARE_FAC(iv);
DECLARE_FAC(ctl);

#define D_INIT_LOG_FAC(name, aname, lname)				\
	d_init_log_facility(&d_##name##_logfac, aname, lname)

int
crt_setup_log_fac(void)
{
	/* add crt internally used the log facilities */
	D_INIT_LOG_FAC(rpc, "RPC", "rpc");
	D_INIT_LOG_FAC(bulk, "BULK", "bulk");
	D_INIT_LOG_FAC(corpc, "CORPC", "corpc");
	D_INIT_LOG_FAC(grp, "GRP", "group");
	D_INIT_LOG_FAC(lm, "LM", "livenessmap");
	D_INIT_LOG_FAC(hg, "HG", "mercury");
	D_INIT_LOG_FAC(pmix, "PMIX", "pmix");
	D_INIT_LOG_FAC(self_test, "ST", "self_test");
	D_INIT_LOG_FAC(iv, "IV", "iv");

	d_log_sync_mask();

	return 0;
}
