/* Copyright (C) 2017 Intel Corporation
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
 *
 * test for PMIx notification functionality.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <semaphore.h>
#include <signal.h>

#include <pmix.h>

static pmix_proc_t	myproc;
static size_t		my_evhdlr_ref;
static sem_t		shut_down;

static void
my_evhdlr_reg_cb(pmix_status_t status, size_t evhdlr_ref, void *cbdata)
{
	fprintf(stderr, "my_evhdlr_reg_cb called with status %d, ref %zu.\n",
		status, evhdlr_ref);
	my_evhdlr_ref = evhdlr_ref;
}

static void
my_evhdlr(size_t evhdlr_registration_id,
	   pmix_status_t status,
	   const pmix_proc_t *source,
	   pmix_info_t info[], size_t ninfo,
	   pmix_info_t *results, size_t nresults,
	   pmix_event_notification_cbfunc_fn_t cbfunc,
	   void *cbdata)
{
	fprintf(stderr, "rank %s:%d notified with status %d\n",
		myproc.nspace, myproc.rank, status);
	sem_post(&shut_down);
	if (cbfunc)
		cbfunc(PMIX_SUCCESS, NULL, 0, NULL, NULL, cbdata);
}

static void
my_evhdlr_dereg_cb(pmix_status_t status, void *cbdata)
{
	fprintf(stderr, "my_evhdlr_dereg_cb called with status %d", status);
}

static inline void
test_sem_timedwait(sem_t *sem, int sec, int line_number)
{
	struct timespec			deadline;
	int				rc;

	rc = clock_gettime(CLOCK_REALTIME, &deadline);
	if (rc != 0) {
		fprintf(stderr, "clock_gettime() failed at line %d rc: %d\n",
			line_number, rc);
		exit(rc);
	}
	deadline.tv_sec += sec;
	rc = sem_timedwait(sem, &deadline);
	if (rc != 0) {
		fprintf(stderr, "sem_timedwait() failed at line %d rc: %d\n",
			line_number, rc);
		exit(rc);
	}
}

int
main()
{
	pmix_value_t	 value;
	pmix_value_t	*val = &value;
	pmix_proc_t	 proc;
	uint32_t	 nprocs;
	int		 rc;


	rc = sem_init(&shut_down, 0, 0);
	if (rc != 0) {
		fprintf(stderr, "sem_init() failed, rc: %d\n", rc);
		exit(1);
	}
	rc = PMIx_Init(&myproc, NULL, 0);
	if (rc != PMIX_SUCCESS) {
		fprintf(stderr, "PMIxInit() failed. rc: %d\n", rc);
		exit(1);
	}

	PMIX_PROC_CONSTRUCT(&proc);
	strncpy(proc.nspace, myproc.nspace, PMIX_MAX_NSLEN);
	proc.rank = PMIX_RANK_WILDCARD;

	rc = PMIx_Get(&proc, PMIX_JOB_SIZE, NULL, 0, &val);
	if (rc != PMIX_SUCCESS) {
		fprintf(stderr, "PMIx_Get failed. rc: %d\n", rc);
		goto out;
	}
	nprocs = val->data.uint32;
	PMIX_VALUE_RELEASE(val);
	fprintf(stderr, "rank %s:%d job size %d\n", myproc.nspace, myproc.rank,
		nprocs);

	PMIx_Register_event_handler(NULL, 0, NULL, 0,
				    my_evhdlr,
				    my_evhdlr_reg_cb, NULL);

	/* call fence  to synchronize */
	PMIX_PROC_CONSTRUCT(&proc);
	strncpy(proc.nspace, myproc.nspace, PMIX_MAX_NSLEN);
	proc.rank = PMIX_RANK_WILDCARD;
	rc = PMIx_Fence(&proc, 1, NULL, 0);
	if (rc != PMIX_SUCCESS) {
		fprintf(stderr, "rank %s:%d: PMIx_Fence failed: %d\n",
			myproc.nspace, myproc.rank, rc);
		goto out;
	}

	if (myproc.rank == 1) {
		sleep(5);
		raise(SIGKILL);
	}

	test_sem_timedwait(&shut_down, 60, __LINE__);

	fprintf(stderr, "about to shut down.\n");
out:
	PMIx_Deregister_event_handler(my_evhdlr_ref, my_evhdlr_dereg_cb, NULL);

	rc = PMIx_Finalize(NULL, 0);
	if (rc != PMIX_SUCCESS)
		fprintf(stderr, "rank %s:%d: PMIx_Finalize failed, rc: %d\n",
			myproc.nspace, myproc.rank, rc);
	else
		fprintf(stderr, "rank %s:%d: PMIx_Finalize succeeded.\n",
			myproc.nspace, myproc.rank);
	rc = sem_destroy(&shut_down);
	if (rc != 0)
		fprintf(stderr, "sem_destroy() failed, rc: %d\n", rc);

	return rc;
}
