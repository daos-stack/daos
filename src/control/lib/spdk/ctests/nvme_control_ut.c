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

int
mock_get_dev_health_logs(struct spdk_nvme_ctrlr *ctrlr,
			 struct dev_health_entry *entry)
{
	struct spdk_nvme_health_information_page health_page;

	memset(&health_page, 0, sizeof(health_page));
	entry->health_page = health_page;

	return 0;
}

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
	return 0;
}

int
mock_spdk_nvme_probe_fail(const struct spdk_nvme_transport_id *trid,
			void *cb_ctx,
			spdk_nvme_probe_cb probe_cb,
			spdk_nvme_attach_cb attach_cb,
			spdk_nvme_remove_cb remove_cb)
{
	return -1;
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

static void
test_nvme_discover_null_g_controllers(void **state)
{
	(void) state; /*unused*/
	struct ret_t *ret;

	assert_true(g_controllers == NULL);

	ret = _nvme_discover(&mock_spdk_nvme_probe_ok,
			     &mock_spdk_nvme_detach_ok,
			     &mock_get_dev_health_logs);
	assert_int_equal(ret->rc, 0);

	assert_true(ret->ctrlrs == NULL);

	free(ret);
}

static void
test_nvme_discover_set_g_controllers(void **state)
{
	(void) state; /*unused*/
	struct ret_t *ret;

	if (g_controllers != NULL) {
		assert_true(false);
	}
	g_controllers = malloc(sizeof(struct ctrlr_entry));
	g_controllers->ctrlr = NULL;
	g_controllers->dev_health = NULL;
	g_controllers->next = NULL;

	ret = _nvme_discover(&mock_spdk_nvme_probe_ok,
			     &mock_spdk_nvme_detach_ok,
			     &mock_get_dev_health_logs);
	assert_int_equal(ret->rc, 0);

	assert_true(ret->ctrlrs == NULL);

	free(ret);
}

static void
test_nvme_discover_probe_fail(void **state)
{
	(void) state; /*unused*/
	struct ret_t *ret;

	if (g_controllers != NULL) {
		assert_true(false);
	}
	g_controllers = malloc(sizeof(struct ctrlr_entry));
	g_controllers->ctrlr = NULL;
	g_controllers->dev_health = NULL;
	g_controllers->next = NULL;

	ret = _nvme_discover(&mock_spdk_nvme_probe_fail,
			     &mock_spdk_nvme_detach_ok,
			     &mock_get_dev_health_logs);
	assert_int_equal(ret->rc, -1);

	assert_true(ret->ctrlrs == NULL);

	free(ret);
}

/* TODO: work out how we can access nvme_internal.h to
 *       access the opaque spdk_nvme_ctrlr type to mock.
static void
test_nvme_collect(void **state)
{
	(void) state;
	struct ret_t *ret;
	struct spdk_nvme_transport_id trid = {};
	struct spdk_nvme_ctrlr_opts opts = {};
	struct spdk_nvme_ctrlr ctrlr1;
	struct spdk_nvme_ctrlr ctrlr2;

	if (g_controllers != NULL) {
		assert_true(false);
	}
	g_controllers = malloc(sizeof(struct ctrlr_entry));
	g_controllers->ctrlr = NULL;
	g_controllers->dev_health = NULL;
	g_controllers->next = NULL;

	memset(&ctrlr1, 0, sizeof(struct spdk_nvme_ctrlr));
	snprintf(ctrlr1.trid.traddr, sizeof(ctrlr1.trid.traddr),
		 "0000:01:00.0");
	ctrlr1->trid.trtype = SPDK_NVME_TRANSPORT_PCIE;
	memset(&ctrlr2, 0, sizeof(struct spdk_nvme_ctrlr));
	snprintf(ctrlr2.trid.traddr, sizeof(ctrlr2.trid.traddr),
		 "0000:02:00.0");
	ctrlr2.trid.trtype = SPDK_NVME_TRANSPORT_PCIE;

	memset(&trid, 0, sizeof(trid));
	trid.trtype = SPDK_NVME_TRANSPORT_PCIE;

	attach_cb(NULL, &trid, &ctrlr1, &opts);
	attach_cb(NULL, &trid, &ctrlr2, &opts);

	assert_true(ret->ctrlrs == NULL);

	collect();

	assert_true(ret->ctrlrs != NULL);

	free(ret);
}
*/

int
main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_nvme_discover_null_g_controllers),
		cmocka_unit_test(test_nvme_discover_set_g_controllers),
		cmocka_unit_test(test_nvme_discover_probe_fail),
		/*cmocka_unit_test(test_nvme_collect),*/
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
