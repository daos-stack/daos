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
 * Test for container creation and destroy.
 * vos/tests/vos_container_tests.c
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <daos_srv/vos.h>
#include <daos/common.h>

#include "vos_obj.h"

#define POOL_SIZE 10737418240ULL

bool
file_exists(const char *filename)
{
	FILE *fp;

	fp = fopen(filename, "r");
	if (fp) {
		fclose(fp);
		return true;
	}
	return false;
}

int
main(int argc, char *argv[]) {

	int			rc	= 0;
	char			*file	= NULL;
	uuid_t			pool_uuid, container_uuid;
	daos_handle_t		vph;
	daos_handle_t		coh;
	daos_unit_oid_t		oid;
	struct vos_obj		*obj = NULL;

	if (argc < 2) {
		fprintf(stderr,
			"Missing arguments <exec> <pmem-file>\n");
		exit(-1);
	}

	oid.id_shard = 1;
	oid.id_pad_32 = 0;
	oid.id_pub.lo = 1;
	oid.id_pub.mid = 2;
	oid.id_pub.hi = 3;

	file = strdup(argv[1]);
	if (file_exists(file))
		remove(file);
	uuid_generate_time_safe(pool_uuid);

	rc = vos_init();
	if (rc) {
		fprintf(stderr, "VOS init error: %d\n", rc);
		return rc;
	}

	rc = vos_pool_create(file,
			     pool_uuid, POOL_SIZE, &vph, NULL);
	if (rc) {
		fprintf(stderr, "vpool create failed with error : %d", rc);
		return rc;
	}
	fprintf(stdout, "Success creating pool at %s\n", file);

	uuid_generate_time_safe(container_uuid);
	rc = vos_co_create(vph, container_uuid, NULL);
	if (rc) {
		fprintf(stderr, "vos container creation error\n");
		return rc;
	}
	fprintf(stdout, "Success creating container at %s\n", file);

	rc = vos_co_open(vph, container_uuid, &coh, NULL);
	if (rc) {
		fprintf(stderr, "VOS container open error\n");
		return rc;
	}
	fprintf(stdout, "Success opening container at %s\n", file);

	rc = vos_oi_lookup(coh, oid, &obj);
	if (rc || obj == NULL) {
		fprintf(stderr,
			"Error in lookup object in object index table\n");
		return rc;
	}
	fprintf(stdout, "Success adding an object to object index\n");

	rc = vos_oi_lookup(coh, oid, &obj);
	if (rc || obj == NULL) {
		fprintf(stderr,
			"Error in lookup object in object index table\n");
		return rc;
	}
	fprintf(stdout, "Success looking up an object in object index\n");


	rc = vos_co_close(coh, NULL);
	if (rc) {
		fprintf(stderr, "Error in closing container\n");
		return rc;
	}
	fprintf(stdout, "Success closing a container\n");


	rc = vos_co_destroy(vph, container_uuid, NULL);
	if (rc) {
		fprintf(stderr, "vos container destroy error\n");
		return rc;
	}
	fprintf(stdout, "Success destroying container at %s\n", file);


	vos_fini();
	remove(file);
	return rc;
}
