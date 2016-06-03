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
/**
 * This file is part of dct
 *
 * dct/tests/dct_test
 */
#include <getopt.h>
#include <dct_rpc.h>
#include <daos_ct.h>
#include <daos_event.h>
#include <daos/event.h>
#include <daos/transport.h>


static struct option opts[] = {
	{ "ping",		0,	NULL,   'p'},
	{  NULL,		0,	NULL,	 0 }
};

#define DEFAULT_TIMEOUT	20

int
ping_test()
{
	dct_ping(10, NULL);
	return 0;
}

int
main(int argc, char **argv)
{
	int	rc = 0;
	int	option;

	/* use full debug dy default for now */
	rc = setenv("DAOS_DEBUG", "-1", false);
	if (rc)
		D_ERROR("failed to enable full debug, %d\n", rc);

	rc = dct_init();
	if (rc != 0) {
		D_ERROR("dct init fails: rc = %d\n", rc);
		return rc;
	}


	while ((option = getopt_long(argc, argv, "p", opts, NULL)) != -1) {
		switch (option) {
		case 'p':
			rc = ping_test();
			break;

		default:
			rc = -EINVAL;
		}
	}

	dct_fini();
	return rc;
}
