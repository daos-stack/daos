/**
 * (C) Copyright 2016 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
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
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

#include "dct_test.h"

/*TODO synchronous vs async test*/

static void
simple_ping_sync(void **state)
{
	/*struct dct_test_arg_t *arg = *state;*/
	dct_ping(10, NULL);
}

/* This arraydefines the functions that are called for each test.
 * Each element has associated with it a name, test function, and
 * startup and teardown functions affiliated with each test
 */
static const struct CMUnitTest ping_tests[] = {
	{"DCT100: Client-Server Ping",
	 simple_ping_sync, NULL, NULL}
};

/* General setup run before *all* tests Doesnt do much here, but have it in for
 * completeness
 */
static int
setup(void **state)
{
	/*setup the argument(s)*/
	dct_test_arg_t	*arg = malloc(sizeof(dct_test_arg_t));

	if (arg == NULL)
		return -1;


	/* Right now just hard code the target rank to zero  in the future
	 * we'll have the ability to send pings to speicfic targers
	 * right now its fixed to rank 0 in API implementation
	 */
	arg->tgt_rank = (daos_rank_t) 0;
	/*value to ping with, we should get back one higher than it*/
	arg->tgt_ping_val = 10;

	*state = arg;

	return 0;
}

/* General teardown, run after tests have completed
 * Currently doesnt do much, here for completness
 */
static int
teardown(void **state)
{
	return 0;
}

int
run_dct_ping_test()
{
	int rc = 0;

	/* Note: Name, array of CMunit tests, setup and teardown functions for
	 * the group of tests in its entirety
	 */
	rc = cmocka_run_group_tests_name("DCT Ping Tests", ping_tests,
					 setup, teardown);

	return rc;
}
