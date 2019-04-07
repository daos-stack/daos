/* Copyright (C) 2018-2019 Intel Corporation
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
#include <cart/api.h>
#include <gurt/common.h>
#include <semaphore.h>
#include "test_ep_cred_common.h"

static void
test_init()
{
	uint32_t	flag;
	int		rc;
	crt_init_options_t opt = {0};

	fprintf(stderr, "local group: %s remote group: %s\n",
		test.tg_local_group_name, test.tg_remote_group_name);

	rc = d_log_init();
	assert(rc == 0);

	rc = sem_init(&test.tg_token_to_proceed, 0, 0);
	D_ASSERTF(rc == 0, "sem_init() failed.\n");

	opt.cio_use_credits = 1;
	opt.cio_ep_credits = test.tg_credits;

	flag = test.tg_is_service ? CRT_FLAG_BIT_SERVER : 0;
	flag |= CRT_FLAG_BIT_LM_DISABLE;
	rc = crt_init_opt(test.tg_local_group_name, flag, &opt);
	D_ASSERTF(rc == 0, "crt_init() failed, rc: %d\n", rc);

	rc = crt_group_rank(NULL, &test.tg_my_rank);
	D_ASSERTF(rc == 0, "crt_group_rank() failed. rc: %d\n", rc);

	rc = crt_proto_register(&my_proto_fmt_0);
	D_ASSERT(rc == 0);

	rc = crt_context_create(&test.tg_crt_ctx);
	D_ASSERTF(rc == 0, "crt_context_create() failed. rc: %d\n", rc);

	if (test.tg_is_service) {
		rc = crt_group_config_save(NULL, true);
		D_ASSERTF(rc == 0, "crt_group_config_save() failed. rc: %d\n",
			rc);
	}

	rc = pthread_create(&test.tg_tid, NULL, progress_thread,
			    &test.tg_thread_id);
	D_ASSERTF(rc == 0, "pthread_create() failed. rc: %d\n", rc);
}


static void
test_run()
{
	D_DEBUG(DB_TRACE, "test_run\n");
}

/************************************************/
static void
test_fini()
{
	int	rc;

	rc = pthread_join(test.tg_tid, NULL);
	D_ASSERTF(rc == 0, "pthread_join failed. rc: %d\n", rc);
	D_DEBUG(DB_TRACE, "joined progress thread.\n");

	rc = crt_context_destroy(test.tg_crt_ctx, false);
	D_ASSERTF(rc == 0, "crt_context_destroy() failed. rc: %d\n", rc);

	rc = sem_destroy(&test.tg_token_to_proceed);
	D_ASSERTF(rc == 0, "sem_destroy() failed.\n");

	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize() failed. rc: %d\n", rc);
	D_DEBUG(DB_TRACE, "exiting.\n");

	d_log_fini();
}

int
main(int argc, char **argv)
{
	int	rc;

	rc = test_parse_args(argc, argv);
	if (rc != 0) {
		fprintf(stderr, "test_parse_args() failed, rc: %d.\n", rc);
		return rc;
	}

	test_init();
	test_run();
	test_fini();

	return rc;
}
