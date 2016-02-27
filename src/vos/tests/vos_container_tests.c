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
#include <stdbool.h>
#include <daos_srv/vos.h>
#include <inttypes.h>

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
main(int argc, char **argv) {


	int rc = 0;
	char *file = NULL;
	daos_handle_t vph, coh, coh_new;
	uuid_t pool_uuid, container_uuid;
	vos_co_info_t cinfo;

	file = strdup("/mnt/pmem_store/test_hash_table");
	if (file_exists(file))
		remove(file);

	uuid_generate_time_safe(pool_uuid);
	rc = vos_pool_create(file,
			     pool_uuid, POOL_SIZE, &vph, NULL);
	if (rc) {
		fprintf(stderr, "vpool create failed with error : %d", rc);
		return rc;
	} else {
		fprintf(stdout, "Success creating pool at %s\n", file);
	}

	uuid_generate_time_safe(container_uuid);
	rc = vos_co_create(vph, container_uuid, &coh, NULL);
	if (rc) {
		fprintf(stderr, "vos container creation error\n");
		return rc;
	} else {
		fprintf(stdout, "Success creating container at %s\n", file);
	}

	uuid_generate_time_safe(container_uuid);
	rc = vos_co_create(vph, container_uuid, &coh_new, NULL);
	if (rc) {
		fprintf(stderr, "vos container creation error\n");
		return rc;
	} else {
		fprintf(stdout, "Success creating container at %s\n", file);
	}


	rc = vos_co_close(coh, NULL);
	if (rc) {
		fprintf(stderr, "vos container close error\n");
		return rc;
	} else {
		fprintf(stdout, "Success closing container at %s\n", file);
	}

	rc = vos_co_open(vph, container_uuid, &coh, NULL);
	if (rc) {
		fprintf(stderr, "vos container open error\n");
		return rc;
	} else {
		fprintf(stdout, "Success opening container at %s\n", file);
	}

	rc = vos_co_query(coh, &cinfo, NULL);
	if (rc) {
		fprintf(stderr, "vos container query error\n");
		return rc;
	} else {
		fprintf(stdout, "Success destroying query\n");
		fprintf(stdout, "Num Objects: %u\n", cinfo.pci_nobjs);
		fprintf(stdout, "Used Space : %"PRIu64"\n", cinfo.pci_used);
	}

	rc = vos_co_destroy(coh, NULL);
	if (rc) {
		fprintf(stderr, "vos container destroy error\n");
		return rc;
	} else {
		fprintf(stdout, "Success destroying container at %s\n", file);
	}

	return rc;
}
