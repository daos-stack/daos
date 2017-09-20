/* Copyright (C) 2016-2017 Intel Corporation
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
 * This file is part of gurt, it implements the debug subsystem based on clog.
 */

#include <stdlib.h>
#include <stdio.h>

#include <gurt/common.h>

static pthread_mutex_t d_log_lock = PTHREAD_MUTEX_INITIALIZER;
static int d_log_refcount;

int d_misc_logfac;
int d_mem_logfac;

static void
d_log_sync_mask_helper(bool acquire_lock)
{
	static int	log_mask_init;
	static char	*log_mask;

	if (acquire_lock)
		pthread_mutex_lock(&d_log_lock);
	if (!log_mask_init) {
		log_mask = getenv(D_LOG_MASK_ENV);
		if (log_mask == NULL) {
			log_mask = getenv(CRT_LOG_MASK_ENV);
			fprintf(stderr, CRT_LOG_MASK_ENV
					" deprecated. Please use "
					D_LOG_MASK_ENV "\n");
		}
	}

	if (log_mask != NULL)
		d_log_setmasks(log_mask, -1);

	if (acquire_lock)
		pthread_mutex_unlock(&d_log_lock);
}

void
d_log_sync_mask(void)
{
	d_log_sync_mask_helper(true);
}

/* this macro contains a return statement */
#define D_ADD_LOG_FAC(name, aname, lname)				\
	do {								\
		d_##name##_logfac = d_add_log_facility(aname, lname);	\
		if (d_##name##_logfac < 0) {				\
			d_log(DERR, "d_add_log_facility failed, "	\
				    "d_##name##__logfac: %d.\n",	\
				    d_##name##_logfac);			\
			return -DER_UNINIT;				\
		}							\
	} while (0)

#define D_INIT_LOG_FAC(name, aname, lname)				\
	d_init_log_facility(&d_##name##_logfac, aname, lname)

/**
 * Setup the clog facility names and mask.
 *
 * \param masks [IN]	 masks in d_log_setmasks() format, or NULL.
 */

static inline int
setup_clog_facnamemask(void)
{
	/* add crt internally used the log facilities */
	D_INIT_LOG_FAC(misc, "MISC", "misc");
	D_INIT_LOG_FAC(mem, "MEM", "memory");

	/* Lock is already held */
	d_log_sync_mask_helper(false);

	return 0;
}

int
d_log_init_adv(char *log_tag, char *log_file, unsigned int flavor,
		 uint64_t def_mask, uint64_t err_mask)
{
	int	 rc = 0;

	pthread_mutex_lock(&d_log_lock);
	d_log_refcount++;
	if (d_log_refcount > 1) /* Already initialized */
		D_GOTO(out, rc);

	rc = d_log_open(log_tag, 0, def_mask, err_mask, log_file, flavor);
	if (rc != 0) {
		D_PRINT_ERR("d_log_open failed: %d\n", rc);
		D_GOTO(out, rc = -DER_UNINIT);
	}

	rc = setup_clog_facnamemask();
	if (rc != 0)
		D_GOTO(out, rc = -DER_UNINIT);
out:
	if (rc != 0) {
		D_PRINT_ERR("ddebug_init failed, rc: %d.\n", rc);
		d_log_refcount--;
	}
	pthread_mutex_unlock(&d_log_lock);
	return rc;
}

int
d_log_init(void)
{
	char	*log_file;
	int flags = DLOG_FLV_LOGPID | DLOG_FLV_FAC | DLOG_FLV_TAG;

	log_file = getenv(D_LOG_FILE_ENV);
	if (log_file == NULL) {
		log_file = getenv(CRT_LOG_FILE_ENV);
		fprintf(stderr, CRT_LOG_FILE_ENV " deprecated. Please use "
				D_LOG_FILE_ENV "\n");
	}
	if (log_file == NULL || strlen(log_file) == 0) {
		flags |= DLOG_FLV_STDOUT;
		log_file = NULL;
	}

	return d_log_init_adv("CaRT", log_file, flags, DLOG_WARN, DLOG_EMERG);
}

void d_log_fini(void)
{
	D_ASSERT(d_log_refcount > 0);

	pthread_mutex_lock(&d_log_lock);
	d_log_refcount--;
	if (d_log_refcount == 0)
		d_log_close();
	pthread_mutex_unlock(&d_log_lock);
}
