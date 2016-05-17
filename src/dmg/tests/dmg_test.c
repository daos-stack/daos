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
 * This file is part of dmg, a simple test case of dmg.
 */
#include <getopt.h>
#include <uuid/uuid.h>
#include <stdio.h>
#include <stdlib.h>

#include <daos_mgmt.h>
#include <daos_event.h>

#include <daos/common.h>

static struct option opts[] = {
	{ "pool create",	0,	NULL,	'c'},
	{  NULL,		0,	NULL,	 0 }
};

static int
test_pool_create(void)
{
	uuid_t		uuid;
	char		uuid_str[64] = {'\0'};
	daos_rank_list_t	svc;
	int		rc;
	daos_rank_t	ranks[8];

	printf("Creating pool ...\n");

	svc.rl_nr.num = 1;
	svc.rl_nr.num_out = 8;
	svc.rl_ranks = ranks;

	rc = dmg_pool_create(0 /* mode */, 0 /* uid */, 0 /* gid */,
			     "srv_grp" /* grp */, NULL /* tgts */,
			     "pmem" /* dev */, 1024 * 1024 * 1024 /* size */,
			     &svc /* svc */, uuid, NULL /* ev */);
	if (rc == 0) {
		uuid_unparse_lower(uuid, uuid_str);
		printf("Created pool %s.\n", uuid_str);
	} else {
		D_ERROR("dmg_pool_create failed, rc: %d.\n", rc);
	}

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

	rc = dmg_init();
	if (rc != 0) {
		D_ERROR("dmg init fails: rc = %d\n", rc);
		return rc;
	}

	while ((option = getopt_long(argc, argv, "c", opts, NULL)) != -1) {
		switch (option) {
		default:
			dmg_fini();
			return -EINVAL;
		case 'c':
			rc = test_pool_create();
			break;
		}
		if (rc < 0) {
			D_ERROR("fails on %d: rc = %d\n", option, rc);
			break;
		}
	}

	dmg_fini();

	return rc;
}
