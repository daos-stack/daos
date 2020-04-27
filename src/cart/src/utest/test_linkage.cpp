/* Copyright (C) 2016-2020 Intel Corporation
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

static d_hash_table_ops_t hash_ops = {
	hop_key_cmp : key_cmp,
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

	return cmocka_run_group_tests(tests, NULL, NULL);
}
