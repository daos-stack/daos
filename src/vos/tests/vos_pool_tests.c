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
#include <fcntl.h>

#define POOL_SIZE 16777216ULL

int
main(int argc, char **argv)
{

	int		rc = 0, fd;
	char		*file1 = NULL, *file2 = NULL;
	daos_handle_t	vph[2];
	vos_pool_info_t	pinfo;
	uuid_t		uuid;

	if (argc < 2) {
		fprintf(stdout, "Insufficient Parameters\n");
		fprintf(stdout, "<exec><pmem-file-path>\n");
		exit(EXIT_FAILURE);
	}

	file1 = argv[1];
	uuid_generate_time_safe(uuid);

	rc = vos_init();
	if (rc) {
		fprintf(stderr, "VOS init error: %d\n", rc);
		return rc;
	}

	rc = vos_pool_create(file1, uuid, POOL_SIZE, &vph[0], NULL);
	if (rc) {
		fprintf(stderr, "vpool create failed with error : %d", rc);
		exit(EXIT_FAILURE);
	} else {
		fprintf(stdout, "Success creating pool at %s\n", file1);
	}

	rc = vos_pool_close(vph[0], NULL);
	if (rc) {
		fprintf(stderr, "vpool open failed with error : %d", rc);
		exit(EXIT_FAILURE);
	} else {
		fprintf(stdout, "Success closing pool at %s\n", file1);
	}

	file2 = strdup(file1);
	strcat(file2, ".1");

	fd = open(file2, O_CREAT | O_TRUNC | O_RDWR, 0666);
	if (fd < 0) {
		fprintf(stderr, "vpool open failed with error : %d", rc);
		exit(EXIT_FAILURE);
	}
	posix_fallocate(fd, 0, POOL_SIZE);

	rc = vos_pool_create(file2, uuid, 0, &vph[1], NULL);
	if (rc) {
		fprintf(stderr, "vpool create failed with error : %d", rc);
		exit(EXIT_FAILURE);
	} else {
		fprintf(stdout, "Success creating pool at %s\n", file2);
	}

	rc = vos_pool_destroy(vph[1], NULL);
	if (rc) {
		fprintf(stderr, "vos_pool_destroy failed\n");
		fprintf(stderr, "Error code: %d\n", rc);
		exit(EXIT_FAILURE);
	}
	fprintf(stdout, "Success Destroying pool %s\n", file2);


	rc = vos_pool_open(file1, uuid, &vph[0], NULL);
	if (rc) {
		fprintf(stderr, "vpool open failed with error : %d", rc);
		exit(EXIT_FAILURE);
	} else {
		fprintf(stdout, "Success opening pool at %s\n", file1);
	}

	rc = vos_pool_query(vph[0], &pinfo, NULL);
	if (rc) {
		fprintf(stderr, "vpool query failed with error : %d", rc);
		exit(EXIT_FAILURE);
	} else {
		printf("Statistics\n");
		printf("Containers: %u\n", pinfo.pif_ncos);
		printf("Objects: %u\n", pinfo.pif_nobjs);
		printf("Size: %" PRId64 "\n", pinfo.pif_size);
		printf("Available Size: %" PRId64 "\n", pinfo.pif_avail);
	}

	rc = vos_pool_destroy(vph[0], NULL);
	if (rc) {
		fprintf(stderr, "vos_pool_destroy failed\n");
		fprintf(stderr, "Error code: %d\n", rc);
		exit(EXIT_FAILURE);
	}
	fprintf(stdout, "Success Destroying pool %s\n", file1);

	vos_fini();
	remove(file1);
	remove(file2);
	if (file2)
		free(file2);
	return rc;
}
