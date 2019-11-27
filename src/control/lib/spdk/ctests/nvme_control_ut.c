/**
* (C) Copyright 2019 Intel Corporation.
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
* provided in Contract No. 8F-30005.
* Any reproduction of computer software, computer software documentation, or
* portions thereof marked with this legend must also reproduce the markings.
*/

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "spdk/stdinc.h"
#include "spdk/nvme.h"
#include "spdk/env.h"

#include "../include/nvme_control.h"

/**
 * ==============================
 * nvme_control mock functions
 * ==============================
 */

//struct ret_t *__wrap_init_ret(void)
//{
//	return mock_ptr_type(struct ret_t *);
//}

//void __wrap_cleanup(void)
//{
//	printf("Mock cleanup()...\n");
//}
//
//void __wrap_collect(struct ret_t *ret)
//{
//	printf("Mock collect()...\n");
//}

/**
 * ===================
 * SPDK mock functions
 * ===================
 */

int
mock_spdk_nvme_probe_ok(const struct spdk_nvme_transport_id *trid,
			void *cb_ctx,
			spdk_nvme_probe_cb probe_cb,
			spdk_nvme_attach_cb attach_cb,
			spdk_nvme_remove_cb remove_cb)
{
	//check_expecteds(probe_cb);
	//check_expected(attach_cb);

	return 0; //mock_type(int);
}

int
mock_spdk_nvme_detach_ok(struct spdk_nvme_ctrlr *ctrlr)
{
	return 0;
}

/**
 * ===================
 * Test functions
 * ===================
 */
//static void test_nvme_discover_null_g_controllers(void **state)
//{
//	(void) state; /*unused*/
//	struct ret_t *rv, *ret;
//
//	/* ret_t values for mock init_ret */
//	ret = test_malloc(sizeof(struct ret_t));
//	ret->rc = 0;
//	ret->ctrlrs = NULL;
//	ret->nss = NULL;
//	snprintf(ret->err, sizeof(ret->err), "none");
//
//	/* Define param checks and return values */
//	will_return(__wrap_init_ret, &ret);
//	will_return(__wrap_spdk_nvme_probe, 0);
//	expect_any(__wrap_spdk_nvme_probe, probe_cb);
//	expect_any(__wrap_spdk_nvme_probe, attach_cb);
//
//	rv = nvme_discover();
//	if (rv != NULL) {
//		printf("nvme_discover() returned: %d\n", rv->rc);
//	}
//
//	test_free(ret);
//}

static void
test_nvme_discover_set_g_controllers(void **state)
{
	(void) state; /*unused*/
	struct ret_t *ret;

	/* ret_t values for mock init_ret */
//	ret = malloc(sizeof(struct ret_t));
//	ret->rc = 0;
//	ret->ctrlrs = NULL;
//	ret->nss = NULL;
//	snprintf(ret->err, sizeof(ret->err), "none");

	if (g_controllers == NULL) {
		g_controllers = malloc(sizeof(struct ctrlr_entry));
		g_controllers->ctrlr = NULL;
		g_controllers->dev_health = NULL;
		g_controllers->next = NULL;
	}

	ret = _nvme_discover(&mock_spdk_nvme_probe_ok,
			    &mock_spdk_nvme_detach_ok);
	assert_int_equal(ret->rc, 0);

	free(ret);
}

//static void test_nvme_discover_nvme_probe_fail(void **state)
//{
//	(void) state; /*unused*/
//	struct ret_t *rv, *ret;
//
//	/* ret_t values for mock init_ret */
//	ret = test_malloc(sizeof(struct ret_t));
//	ret->rc = 0;
//	ret->ctrlrs = NULL;
//	ret->nss = NULL;
//	snprintf(ret->err, sizeof(ret->err), "none");
//
//	/* Define param checks and return values */
//	will_return(__wrap_init_ret, &ret);
//	will_return(__wrap_spdk_nvme_probe, 1);
//	expect_any(__wrap_spdk_nvme_probe, probe_cb);
//	expect_any(__wrap_spdk_nvme_probe, attach_cb);
//
//	rv = nvme_discover();
//	if (rv != NULL) {
//		printf("nvme_discover() returned: %d\n", rv->rc);
//	}
//
//	test_free(ret);
//}

int
main(void)
{
	const struct CMUnitTest tests[] = {
		//cmocka_unit_test(test_nvme_discover_null_g_controllers),
		cmocka_unit_test(test_nvme_discover_set_g_controllers),
		//cmocka_unit_test(test_nvme_discover_nvme_probe_fail),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
