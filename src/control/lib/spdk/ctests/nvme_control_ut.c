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

#include <gurt/types.h>
#include <spdk/stdinc.h>
#include <spdk/nvme.h>
#include <spdk/env.h>
#include "nvme_internal.h"

#include "../include/nvme_control.h"
#include "../include/nvme_control_common.h"

#if D_HAS_WARNING(4, "-Wframe-larger-than=")
	#pragma GCC diagnostic ignored "-Wframe-larger-than="
#endif

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

	(void)ctrlr;
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
			spdk_nvme_probe_cb pcb,
			spdk_nvme_attach_cb acb,
			spdk_nvme_remove_cb rcb)
{
	(void)trid;
	(void)cb_ctx;
	(void)pcb;
	(void)acb;
	(void)rcb;
	return 0;
}

int
mock_spdk_nvme_probe_fail(const struct spdk_nvme_transport_id *trid,
			void *cb_ctx,
			spdk_nvme_probe_cb pcb,
			spdk_nvme_attach_cb acb,
			spdk_nvme_remove_cb rcb)
{
	(void)trid;
	(void)cb_ctx;
	(void)pcb;
	(void)acb;
	(void)rcb;
	return -1;
}

int
mock_spdk_nvme_detach_ok(struct spdk_nvme_ctrlr *ctrlr)
{
	(void)ctrlr;
	return 0;
}

const struct spdk_nvme_ctrlr_data *
mock_spdk_nvme_ctrlr_get_data(struct spdk_nvme_ctrlr *ctrlr)
{
	struct spdk_nvme_ctrlr_data *data;

	data = malloc(sizeof(struct spdk_nvme_ctrlr_data));

	(void)ctrlr;
	return (const struct spdk_nvme_ctrlr_data *)data;
}

struct spdk_pci_device *
mock_spdk_nvme_ctrlr_get_pci_device(struct spdk_nvme_ctrlr *ctrlr)
{
	struct spdk_pci_device *dev;

	dev = malloc(sizeof(struct spdk_pci_device));

	(void)ctrlr;
	return dev;
}

int
mock_spdk_pci_device_get_socket_id(struct spdk_pci_device *dev)
{
	(void)dev;
	return 1;
}

/**
 * ===================
 * Test functions
 * ===================
 */

static void
test_nvme_discover_null_g_controllers(void **state)
{
	struct ret_t *ret;

	(void) state; /*unused*/

	assert_null(g_controllers);

	ret = _nvme_discover(&mock_spdk_nvme_probe_ok,
			     &mock_spdk_nvme_detach_ok,
			     &mock_get_dev_health_logs);
	assert_int_equal(ret->rc, 0);

	assert_null(ret->ctrlrs);

	free(ret);
}

static void
test_nvme_discover_set_g_controllers(void **state)
{
	struct ret_t *ret;

	(void) state; /*unused*/

	assert_null(g_controllers);
	g_controllers = malloc(sizeof(struct ctrlr_entry));
	g_controllers->ctrlr = NULL;
	g_controllers->dev_health = NULL;
	g_controllers->next = NULL;

	ret = _nvme_discover(&mock_spdk_nvme_probe_ok,
			     &mock_spdk_nvme_detach_ok,
			     &mock_get_dev_health_logs);
	assert_int_equal(ret->rc, 0);

	assert_null(ret->ctrlrs);

	free(ret);
}

static void
test_nvme_discover_probe_fail(void **state)
{
	struct ret_t *ret;

	(void) state; /*unused*/

	assert_null(g_controllers);
	g_controllers = malloc(sizeof(struct ctrlr_entry));
	g_controllers->ctrlr = NULL;
	g_controllers->dev_health = NULL;
	g_controllers->next = NULL;

	ret = _nvme_discover(&mock_spdk_nvme_probe_fail,
			     &mock_spdk_nvme_detach_ok,
			     &mock_get_dev_health_logs);
	assert_int_equal(ret->rc, -1);

	assert_null(ret->ctrlrs);

	free(ret);
}

static void
test_nvme_collect(void **state)
{
	struct ret_t		       *ret;
	struct spdk_nvme_ctrlr_opts	opts = {};
	struct spdk_nvme_transport_id	trid1 = {};
	struct spdk_nvme_transport_id	trid2 = {};
	struct spdk_nvme_ctrlr		ctrlr1;
	struct spdk_nvme_ctrlr		ctrlr2;
	struct ctrlr_entry	       *entry1;
	struct ctrlr_entry	       *entry2;

	(void) state;

	ret = init_ret();

	memset(&ctrlr1, 0, sizeof(struct spdk_nvme_ctrlr));
	snprintf(ctrlr1.trid.traddr, sizeof(ctrlr1.trid.traddr),
		 "0000:01:00.0");
	ctrlr1.trid.trtype = SPDK_NVME_TRANSPORT_PCIE;
	memset(&ctrlr2, 0, sizeof(struct spdk_nvme_ctrlr));
	snprintf(ctrlr2.trid.traddr, sizeof(ctrlr2.trid.traddr),
		 "0000:02:00.0");
	ctrlr2.trid.trtype = SPDK_NVME_TRANSPORT_PCIE;

	memset(&trid1, 0, sizeof(trid1));
	trid1.trtype = SPDK_NVME_TRANSPORT_PCIE;
	snprintf(trid1.traddr, sizeof(trid1.traddr),
		 "0000:01:00.0");
	memset(&trid2, 0, sizeof(trid2));
	trid2.trtype = SPDK_NVME_TRANSPORT_PCIE;
	snprintf(trid2.traddr, sizeof(trid2.traddr),
		 "0000:02:00.0");

	/* attach_cb gets called with matching trid & ctrlr.trid by probe for
	 * each controller on the bus
	 */
	assert_null(g_controllers);
	attach_cb(NULL, &trid1, &ctrlr1, &opts);
	assert_non_null(g_controllers);
	entry1 = g_controllers;
	attach_cb(NULL, &trid2, &ctrlr2, &opts);
	assert_non_null(g_controllers);
	entry2 = g_controllers;
	assert_ptr_not_equal(entry1, entry2);

	assert_null(ret->ctrlrs);
	_collect(ret, &mock_spdk_nvme_ctrlr_get_data,
		 &mock_spdk_nvme_ctrlr_get_pci_device,
		 &mock_spdk_pci_device_get_socket_id);

	if (ret->rc != 0)
		fprintf(stderr, "collect err: %s\n", ret->err);
	assert_return_code(ret->rc, 0);
	assert_non_null(ret->ctrlrs);
	assert_string_equal(ret->ctrlrs->pci_addr, "0000:01:00.0");
	assert_non_null(ret->ctrlrs->next);
	assert_string_equal(ret->ctrlrs->next->pci_addr, "0000:02:00.0");
	assert_null(ret->ctrlrs->next->next);

	free(ret);
}

int
teardown(void ** state)
{
	nvme_cleanup(&mock_spdk_nvme_detach_ok);

	return 0;
}

int
main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_nvme_discover_null_g_controllers),
		cmocka_unit_test(test_nvme_discover_set_g_controllers),
		cmocka_unit_test(test_nvme_discover_probe_fail),
		cmocka_unit_test(test_nvme_collect),
	};

	return cmocka_run_group_tests(tests, NULL, teardown);
}
