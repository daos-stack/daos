/**
 * (C) Copyright 2017 Intel Corporation.
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

#ifndef RDB_TESTS_RPC_H
#define RDB_TESTS_RPC_H

enum rdbt_operation {
	RDBT_INIT	= 1,
	RDBT_FINI	= 2,
	RDBT_TEST	= 3
};

struct rdbt_init_in {
	uuid_t		tii_uuid;
	uint32_t	tii_nreplicas;
};

struct rdbt_init_out {
	int	tio_rc;
};

struct rdbt_fini_in {
};

struct rdbt_fini_out {
	int	tfo_rc;
};

struct rdbt_test_in {
	int	tti_update;
};

struct rdbt_test_out {
	int	tto_rc;
};

extern struct daos_rpc rdbt_rpcs[];

#endif /* RDB_TESTS_RPC_H */
