/* Copyright (C) 2016 Intel Corporation
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
/**
 * This file is part of cart, it implements the debug subsystem based on clog.
 */

#include <stdlib.h>
#include <stdio.h>

#include <crt_errno.h>
#include <crt_util/common.h>

#define CRT_LOG_FILE_ENV	"CRT_LOG_FILE"
#define CRT_LOG_MASK_ENV	"CRT_LOG_MASK"

static pthread_mutex_t crt_log_lock = PTHREAD_MUTEX_INITIALIZER;
static int crt_log_refcount;
int crt_logfac;
int crt_mem_logfac;
int crt_misc_logfac;

#define CLOG_MAX_FAC_HINT	(16)

static void
crt_log_sync_mask_helper(bool acquire_lock)
{
	static int	log_mask_init;
	static char	*log_mask;

	if (acquire_lock)
		pthread_mutex_lock(&crt_log_lock);
	if (!log_mask_init)
		log_mask = getenv(CRT_LOG_MASK_ENV);

	if (log_mask != NULL)
		crt_log_setmasks(log_mask, -1);

	if (acquire_lock)
		pthread_mutex_unlock(&crt_log_lock);
}

void crt_log_sync_mask(void)
{
	crt_log_sync_mask_helper(true);
}

/**
 * Setup the clog facility names and mask.
 *
 * \param masks [IN]	 masks in crt_log_setmasks() format, or NULL.
 */
static inline int
setup_clog_facnamemask(void)
{
	int rc;

	/* first add the clog/mem/misc facility */
	crt_mem_logfac = crt_add_log_facility("MEM", "memory");
	if (crt_mem_logfac < 0) {
		C_PRINT_ERR("crt_add_log_facility failed, crt_mem_logfac %d.\n",
			    crt_mem_logfac);
		C_GOTO(out, rc = -CER_UNINIT);
	}
	crt_misc_logfac = crt_add_log_facility("MISC", "miscellaneous");
	if (crt_misc_logfac < 0) {
		C_PRINT_ERR("crt_add_log_facility failed,crt_misc_logfac %d.\n",
			    crt_misc_logfac);
		C_GOTO(out, rc = -CER_UNINIT);
	}
	rc = 0;
	/* add CRT specific log facility */
	crt_logfac = crt_add_log_facility("CRT", "CaRT");
	if (crt_logfac < 0) {
		C_PRINT_ERR("crt_add_log_facility failed, crt_logfac %d.\n",
			    crt_logfac);
		C_GOTO(out, rc = -CER_UNINIT);
	}

	/* Lock is already held */
	crt_log_sync_mask_helper(false);

out:
	return rc;
}

int
crt_log_init_adv(char *log_tag, char *log_file, unsigned int flavor,
		 uint64_t def_mask, uint64_t err_mask)
{
	int	 rc = 0;

	pthread_mutex_lock(&crt_log_lock);
	crt_log_refcount++;
	if (crt_log_refcount > 1) /* Already initialized */
		C_GOTO(out, rc);

	rc = crt_log_open(log_tag, CLOG_MAX_FAC_HINT, def_mask, err_mask,
			  log_file, flavor);
	if (rc != 0) {
		C_PRINT_ERR("crt_log_open failed: %d\n", rc);
		C_GOTO(out, rc = -CER_UNINIT);
	}

	rc = setup_clog_facnamemask();
	if (rc != 0)
		C_GOTO(out, rc = -CER_UNINIT);
out:
	if (rc != 0) {
		C_PRINT_ERR("crt_debug_init failed, rc: %d.\n", rc);
		crt_log_refcount--;
	}
	pthread_mutex_unlock(&crt_log_lock);
	return rc;
}

int
crt_log_init(void)
{
	char	*log_file;
	int flags = CLOG_FLV_LOGPID | CLOG_FLV_FAC | CLOG_FLV_TAG;

	log_file = getenv(CRT_LOG_FILE_ENV);
	if (log_file == NULL || strlen(log_file) == 0) {
		flags |= CLOG_FLV_STDOUT;
		log_file = NULL;
	}

	return crt_log_init_adv("CaRT", log_file, flags, CLOG_WARN, CLOG_EMERG);
}

void crt_log_fini(void)
{
	C_ASSERT(crt_log_refcount > 0);

	pthread_mutex_lock(&crt_log_lock);
	crt_log_refcount--;
	if (crt_log_refcount == 0)
		crt_log_close();
	pthread_mutex_unlock(&crt_log_lock);
}


