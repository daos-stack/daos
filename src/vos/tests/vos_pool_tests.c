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
 * Test for pool creation and destroy.
 * vos/tests/vos_pool_tests.c
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <daos_srv/vos.h>
#include <inttypes.h>

#define POOL_SIZE 10737418240ULL

int
main(int argc, char **argv)
{

	int		rc = 0;
	char		*file = NULL;
	daos_handle_t	vph;
	vos_pool_info_t	pinfo;
	uuid_t		uuid;

	if (argc < 2) {
		fprintf(stdout, "Insufficient Parameters\n");
		fprintf(stdout, "<exec><pmem-file-path>\n");
		exit(-1);
	}

	file = argv[1];
	uuid_generate_time_safe(uuid);
	rc = vos_pool_create(file, uuid, POOL_SIZE, &vph, NULL);
	if (rc) {
		fprintf(stderr, "vpool create failed with error : %d", rc);
		exit(-1);
	} else {
		fprintf(stdout, "Success creating pool at %s\n", file);
	}

	rc = vos_pool_close(vph, NULL);
	if (rc) {
		fprintf(stderr, "vpool open failed with error : %d", rc);
		exit(-1);
	} else {
		fprintf(stdout, "Success closing pool at %s\n", file);
	}

	rc = vos_pool_open(file, uuid, &vph, NULL);
	if (rc) {
		fprintf(stderr, "vpool open failed with error : %d", rc);
		exit(-1);
	} else {
		fprintf(stdout, "Success opening pool at %s\n", file);
	}

	rc = vos_pool_query(vph, &pinfo, NULL);
	if (rc) {
		fprintf(stderr, "vpool query failed with error : %d", rc);
		exit(-1);
	} else {
		printf("Statistics\n");
		printf("Containers: %u\n", pinfo.pif_ncos);
		printf("Objects: %u\n", pinfo.pif_nobjs);
		printf("Size: %" PRId64 "\n", pinfo.pif_size);
		printf("Available Size: %" PRId64 "\n", pinfo.pif_avail);
	}

	rc = vos_pool_destroy(vph, NULL);
	if (rc) {
		fprintf(stderr, "vos_pool_destroy failed\n");
		fprintf(stderr, "Error code: %d\n", rc);
		exit(-1);
	} else {
		fprintf(stdout, "Success Destroying pool %s\n", file);
	}


	remove(file);
	return 0;
}
