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

#define POOL_SIZE 16777216ULL

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
	daos_handle_t vph, coh;
	uuid_t pool_uuid, container_uuid1, container_uuid2;
	vos_co_info_t cinfo;

	if (argc < 2) {
		fprintf(stderr,
			"Missing argument <exec> <pmem-file>\n");
		exit(-1);
	}

	file = strdup(argv[1]);
	if (file_exists(file))
		remove(file);

	rc = vos_init();
	if (rc) {
		fprintf(stderr, "VOS init error: %d\n", rc);
		return rc;
	}
	uuid_generate_time_safe(pool_uuid);
	rc = vos_pool_create(file,
			     pool_uuid, POOL_SIZE, &vph, NULL);
	if (rc) {
		fprintf(stderr, "vpool create failed with error : %d", rc);
		return rc;
	}
	fprintf(stdout, "Success creating pool at %s\n", file);

	uuid_generate_time_safe(container_uuid1);
	rc = vos_co_create(vph, container_uuid1, NULL);
	if (rc) {
		fprintf(stderr, "vos container 1 creation error\n");
		return rc;
	}
	fprintf(stdout, "Success creating container 1 at %s\n", file);

	uuid_generate_time_safe(container_uuid2);
	rc = vos_co_create(vph, container_uuid2, NULL);
	if (rc) {
		fprintf(stderr, "vos container 2 creation  error\n");
		return rc;
	}
	fprintf(stdout, "Success creating container 2 at %s\n", file);

	rc = vos_co_open(vph, container_uuid1, &coh, NULL);
	if (rc) {
		fprintf(stderr, "vos container 1 open error\n");
		return rc;
	}
	fprintf(stdout, "Success opening container 2 at %s\n", file);

	rc = vos_co_query(coh, &cinfo, NULL);
	if (rc) {
		fprintf(stderr, "vos container query error\n");
		return rc;
	}
	fprintf(stdout, "Success querying the container\n");
	fprintf(stdout, "Num Objects: %u\n", cinfo.pci_nobjs);
	fprintf(stdout, "Used Space : "DF_U64"\n", cinfo.pci_used);

	rc = vos_co_close(coh, NULL);
	if (rc) {
		fprintf(stderr, "vos container 1 close error\n");
		return rc;
	}
	fprintf(stdout, "Success closing container 2 at %s\n", file);

	rc = vos_co_destroy(vph, container_uuid2, NULL);
	if (rc) {
		fprintf(stderr, "vos container 2 destroy error\n");
		return rc;
	}
	fprintf(stdout, "Success destroying container 2 at %s\n", file);

	rc = vos_co_destroy(vph, container_uuid1, NULL);
	if (rc) {
		fprintf(stderr, "vos container 1 destroy error\n");
		return rc;
	}
	fprintf(stdout, "Success destroying container 1 at %s\n", file);

	vos_fini();
	remove(file);
	return rc;
}
