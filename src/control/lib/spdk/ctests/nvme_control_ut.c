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

#include "../include/nvme_control_common.h"

#if D_HAS_WARNING(4, "-Wframe-larger-than=")
	#pragma GCC diagnostic ignored "-Wframe-larger-than="
#endif

static struct ret_t	*test_ret;

/**
 * ==============================
 * nvme_control mock functions
 * ==============================
 */

static int
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

static int
mock_spdk_nvme_probe_ok(const struct spdk_nvme_transport_id *trid,
			void *cb_ctx, spdk_nvme_probe_cb pcb,
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

static int
mock_spdk_nvme_probe_fail(const struct spdk_nvme_transport_id *trid,
			  void *cb_ctx, spdk_nvme_probe_cb pcb,
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

static int
mock_copy_ctrlr_data(struct ctrlr_t *cdst, struct ret_t *ret,
		     struct ctrlr_entry *csrc)
{
	(void)cdst;
	(void)ret;
	(void)csrc;
	return 0;
}

static struct spdk_pci_device *
mock_spdk_nvme_ctrlr_get_pci_device(struct spdk_nvme_ctrlr *ctrlr)
{
	struct spdk_pci_device *dev;

	dev = calloc(1, sizeof(struct spdk_pci_device));

	(void)ctrlr;
	return dev;
}

static int
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
test_discover_null_controllers(void **state)
{
	(void)state; /*unused*/

	test_ret = _discover(&mock_spdk_nvme_probe_ok, false,
			&mock_get_dev_health_logs);
	assert_int_equal(test_ret->rc, 0);

	assert_null(test_ret->ctrlrs);
}

static void
test_discover_set_controllers(void **state)
{
	(void)state; /*unused*/

	g_controllers = malloc(sizeof(struct ctrlr_entry));
	g_controllers->ctrlr = NULL;
	g_controllers->nss = NULL;
	g_controllers->dev_health = NULL;
	g_controllers->next = NULL;

	test_ret = _discover(&mock_spdk_nvme_probe_ok, false,
			&mock_get_dev_health_logs);
	assert_int_equal(test_ret->rc, 0);

	assert_null(test_ret->ctrlrs);
}

static void
test_discover_probe_fail(void **state)
{
	(void)state; /*unused*/

	g_controllers = malloc(sizeof(struct ctrlr_entry));
	g_controllers->ctrlr = NULL;
	g_controllers->nss = NULL;
	g_controllers->dev_health = NULL;
	g_controllers->next = NULL;

	test_ret = _discover(&mock_spdk_nvme_probe_fail, false,
			&mock_get_dev_health_logs);
	assert_int_equal(test_ret->rc, -1);

	assert_null(test_ret->ctrlrs);
}

static void
test_collect(void **state)
{
	struct spdk_nvme_ctrlr_opts	opts = {};
	struct spdk_nvme_transport_id	trid1 = {};
	struct spdk_nvme_transport_id	trid2 = {};
	struct spdk_nvme_ctrlr		ctrlr1;
	struct spdk_nvme_ctrlr		ctrlr2;
	struct ctrlr_entry	       *entry1;
	struct ctrlr_entry	       *entry2;

	(void)state;

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

	test_ret = init_ret();

	assert_null(test_ret->ctrlrs);
	_collect(test_ret, &mock_copy_ctrlr_data,
		 &mock_spdk_nvme_ctrlr_get_pci_device,
		 &mock_spdk_pci_device_get_socket_id);

	if (test_ret->rc != 0)
		fprintf(stderr, "collect err: %s\n", test_ret->err);
	assert_return_code(test_ret->rc, 0);
	assert_non_null(test_ret->ctrlrs);
	assert_string_equal(test_ret->ctrlrs->pci_addr, "0000:01:00.0");
	assert_non_null(test_ret->ctrlrs->next);
	assert_string_equal(test_ret->ctrlrs->next->pci_addr, "0000:02:00.0");
	assert_null(test_ret->ctrlrs->next->next);
}

static int
setup(void **state)
{
	assert_null(test_ret);
	assert_null(g_controllers);

	return 0;
}


static int
teardown(void **state)
{
	clean_ret(test_ret);
	free(test_ret);
	test_ret = NULL;
	cleanup(false);

	return 0;
}

int
main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup_teardown(test_discover_null_controllers,
						setup, teardown),
		cmocka_unit_test_setup_teardown(test_discover_set_controllers,
						setup, teardown),
		cmocka_unit_test_setup_teardown(test_discover_probe_fail, setup,
						teardown),
		cmocka_unit_test_setup_teardown(test_collect, setup, teardown),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
