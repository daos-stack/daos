/*
 * (C) Copyright 2016-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
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
/**
 * This file is part of CaRT.  It tests that header files can be included
 * and invoked from C++ applications
 */

#include <iostream>

#include <stdlib.h>
#include <gurt/debug.h>
#include <gurt/hash.h>
#include <gurt/common.h>
#include <gurt/list.h>
#include <cart/api.h>
#include <cart/types.h>
#include "wrap_cmocka.h"

using namespace std;

#define TEST_LINKAGE_BASE 0x010000000
#define TEST_LINKAGE_VER  0

#define CRT_ISEQ_LINKAGE	/* input fields */		 \
	((uint32_t)		(unused)		CRT_VAR)

#define CRT_OSEQ_LINKAGE	/* output fields */		 \
	((uint32_t)		(unused)		CRT_VAR)

CRT_RPC_DECLARE(crt_linkage, CRT_ISEQ_LINKAGE, CRT_OSEQ_LINKAGE);
CRT_RPC_DEFINE(crt_linkage, CRT_ISEQ_LINKAGE, CRT_OSEQ_LINKAGE);

enum {
	LINKAGE_TEST_OPC = CRT_PROTO_OPC(TEST_LINKAGE_BASE, TEST_LINKAGE_VER, 0)
};


struct crt_proto_rpc_format my_proto_rpc_linkage[] = {
	{
		prf_req_fmt	: &CQF_crt_linkage,
		prf_hdlr	: NULL,
		prf_co_ops	: NULL,
		prf_flags	: 0,
	}
};

struct crt_proto_format my_proto_fmt_linkage = {
	cpf_name : "my-proto-linkage",
	cpf_ver : TEST_LINKAGE_VER,
	cpf_count : ARRAY_SIZE(my_proto_rpc_linkage),
	cpf_prf : &my_proto_rpc_linkage[0],
	cpf_base : TEST_LINKAGE_BASE,
};

static void
test_crt_api_linkage(void **state)
{
	int	rc;
	char	bogus_client_group[32] = {"bogus_cli_group"};

	(void)state;

	setenv("OFI_INTERFACE", "lo", 1);
	setenv("CRT_PHY_ADDR_STR", "ofi+sockets", 1);

	rc = crt_init(bogus_client_group, 0x0);
	assert_int_equal(rc, 0);

	/* test RPC register */
	rc = crt_proto_register(&my_proto_fmt_linkage);
	assert_int_equal(rc, 0);

}

static void
test_log_linkage(void **state)
{
	int	fac;
	int	rc;

	(void)state;

	fac = d_log_allocfacility("log_link_test",
				  "Test linkage of crt log API");
	assert_int_not_equal(fac, -1);

	rc = crt_finalize();
	assert_int_equal(rc, 0);
}

static bool
key_cmp(struct d_hash_table *htable, d_list_t *rlink,
	const void *key, unsigned int ksize)
{
	return true;
}

static uint32_t
hop_rec_hash(struct d_hash_table *htable, d_list_t *link)
{
	return 0;
}

static d_hash_table_ops_t hash_ops = {
	hop_key_cmp  : key_cmp,
	hop_key_init : NULL,
	hop_key_hash : NULL,
	hop_rec_hash : hop_rec_hash
};

static void
test_hash_linkage(void **state)
{
	int			rc;
	struct d_hash_table	*table;

	(void)state;

	rc = d_hash_table_create(0, 1, NULL, &hash_ops, &table);

	assert_int_equal(rc, 0);
	assert_non_null(table);

	rc = d_hash_table_destroy(table, true);
	assert_int_equal(rc, 0);
}

static void
test_common_linkage(void **state)
{
	(void)state;
	d_hash_mix64(0);

	/* Just make sure we can call it.  This is primarily
	 * a compiler test but the routines should be callable
	 * too.
	 */
	assert_true(1);
}

int
main(int argc, char **argv)
{
	const struct CMUnitTest	tests[] = {
		cmocka_unit_test(test_crt_api_linkage),
		cmocka_unit_test(test_hash_linkage),
		cmocka_unit_test(test_common_linkage),
		cmocka_unit_test(test_log_linkage),
	};

	cout << "[==========] test linkage ...\n" ;

	return cmocka_run_group_tests_name("test_linkage", tests, NULL, NULL);
}
