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
#define D_LOGFAC	DD_FAC(rdb)

#include <daos/rpc.h>
#include "rpc.h"

static struct crt_msg_field *rdbt_init_in_fields[] = {
	&CMF_UUID,	/* uuid */
	&CMF_UINT32	/* nreplicas */
};

static struct crt_msg_field *rdbt_init_out_fields[] = {
	&CMF_INT	/* rc */
};

static struct crt_req_format DQF_RDBT_INIT =
	DEFINE_CRT_REQ_FMT(rdbt_init_in_fields, rdbt_init_out_fields);

static struct crt_msg_field *rdbt_fini_in_fields[] = {
};

static struct crt_msg_field *rdbt_fini_out_fields[] = {
	&CMF_INT	/* rc */
};

static struct crt_req_format DQF_RDBT_FINI =
	DEFINE_CRT_REQ_FMT(rdbt_fini_in_fields, rdbt_fini_out_fields);

static struct crt_msg_field *rdbt_test_in_fields[] = {
	&CMF_INT	/* update */
};

static struct crt_msg_field *rdbt_test_out_fields[] = {
	&CMF_INT	/* rc */
};

static struct crt_req_format DQF_RDBT_TEST =
	DEFINE_CRT_REQ_FMT(rdbt_test_in_fields, rdbt_test_out_fields);

struct daos_rpc rdbt_rpcs[] = {
	{
		.dr_name	= "RDBT_INIT",
		.dr_opc		= RDBT_INIT,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_RDBT_INIT
	}, {
		.dr_name	= "RDBT_FINI",
		.dr_opc		= RDBT_FINI,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_RDBT_FINI
	}, {
		.dr_name	= "RDBT_TEST",
		.dr_opc		= RDBT_TEST,
		.dr_ver		= 1,
		.dr_flags	= 0,
		.dr_req_fmt	= &DQF_RDBT_TEST
	}, {
	}
};
