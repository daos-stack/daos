/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/**
 * This file is part of dsm
 *
 * dsm/tests/dsm_test
 *
 * Author: Wang Di <di.wang@intel.com>
 */
#include <getopt.h>

#include <daos_m.h>

#include <daos/common.h>

static struct option opts[] = {
	{ "pool-connect",	0,	NULL,   'c'},
	{  NULL,		0,	NULL,	 0 }
};

static int
test_pool_connect(int argc, char *argv[])
{
	char	       *uuid_str = argv[argc - 1];
	uuid_t		uuid;
	daos_handle_t	poh;
	int		rc;

	D_DEBUG(DF_DSMC, "connecting to pool %s\n", argv[argc - 1]);

	rc = uuid_parse(uuid_str, uuid);
	if (rc != 0) {
		D_ERROR("invalid pool uuid: %s\n", uuid_str);
		return rc;
	}

	rc = dsm_pool_connect(uuid, NULL /* grp */, NULL /* tgts */,
			      DAOS_PC_RW, NULL /* failed */, &poh,
			      NULL /* ev */);
	if (rc != 0)
		return rc;

	D_DEBUG(DF_DSMC, "connected to pool %s: "DF_X64"\n", uuid_str,
		poh.cookie);

	rc = dsm_pool_disconnect(poh, NULL /* ev */);
	if (rc != 0)
		return rc;

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

	rc = dsm_init();
	if (rc != 0) {
		D_ERROR("dsm init fails: rc = %d\n", rc);
		return rc;
	}

	while ((option = getopt_long(argc, argv, "pc", opts, NULL)) != -1) {
		switch (option) {
		default:
			dsm_fini();
			return -EINVAL;
		case 'c':
			rc = test_pool_connect(argc, argv);
			break;
		}
		if (rc < 0) {
			D_ERROR("fails on %d: rc = %d\n", option, rc);
			break;
		}
	}

	dsm_fini();
	return rc;
}
