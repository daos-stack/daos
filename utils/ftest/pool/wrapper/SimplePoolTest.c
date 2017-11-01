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

/* generic */
#include <stdio.h>
#include <mpi.h>

/* daos specific */
#include <daos.h>
#include <daos_api.h>
#include <daos_mgmt.h>
#include <daos/common.h>

/* test code */
#include "test_types.h"

/**
 * argv should be:
 *     "destroy pool_uuid daos_server_grp force"
 */
int
destroy(int argc, char **argv)
{
	if (argc != 5)
		return TEST_FAILED;

	uuid_t uuid;
	char grp[30];
	int force;
	int rc;

	rc = uuid_parse(argv[2], uuid);
	strcpy(grp, argv[3]);
	force = atoi(argv[4]);

	rc = daos_pool_destroy(uuid, grp, force, NULL);

	if (rc)
		printf("\n<<<SimplePoolTest.c>>> Pool destroy result: %d\n",
		       rc);
	else
		printf("\n<<<SimplePoolTest.c>>> Pool destroyed.\n");

	fflush(stdout);

	return rc;
}

int
create(int argc, char **argv)
{
	uint32_t          rl_ranks = 0;
	uint64_t          pool_size = 1024*1024*1024;
	uuid_t            uuid;
	daos_rank_list_t  svc;
	char              setid[500];
	int               daos_rc;
	int               mode;
	unsigned int      gid, uid;
	char              *tgt;
	daos_rank_list_t  tgts;
	daos_rank_list_t  *tgts_ptr;
	uint32_t          rl_tgts[10];

	if (argc < 6)
		return TEST_FAILED;

	/* create a pool */
	svc.rl_nr.num = 1;
	svc.rl_nr.num_out = 0;
	svc.rl_ranks = &rl_ranks;

	mode = atoi(argv[2]);
	uid = atoi(argv[3]);
	gid = atoi(argv[4]);
	strcpy(setid, argv[5]);

	if (argc == 7) {
		int i = 0;

		tgt = strtok(argv[6], ",");
		rl_tgts[i++] = atoi(tgt);
		while ((tgt = strtok(NULL, ",")))
			rl_tgts[i++] = atoi(tgt);
		tgts.rl_nr.num = i;
		tgts.rl_nr.num_out = 0;
		tgts.rl_ranks = rl_tgts;
		tgts_ptr = &tgts;
	} else
		tgts_ptr = (daos_rank_list_t *)NULL;

	daos_rc = daos_pool_create(mode, uid, gid, (const char *)&setid,
				   tgts_ptr, "rubbish", pool_size, &svc,
				   uuid, NULL);
	if (daos_rc)
		printf("<<<SimplePoolTest.c>>> Pool create fail, result: %d\n",
		       daos_rc);
	else {
		char uuid_str[100];

		uuid_unparse(uuid, uuid_str);
		printf(uuid_str);
	}
	fflush(stdout);

	return daos_rc;
}

int
create_and_dump(int argc, char **argv)
{
	uint32_t          rl_ranks[10];
	uint64_t          pool_size = 1024*1024*1024;
	uuid_t            uuid;
	daos_rank_list_t  svc;
	char              setid[500];
	int               daos_rc;
	int               mode;
	unsigned int      gid, uid;

	if (argc != 6)
		return TEST_FAILED;

	/* create a pool */
	int i;

	for (i = 0; i < 10; i++)
		rl_ranks[i] = 999;

	svc.rl_nr.num = 1;
	svc.rl_nr.num_out = 0;
	svc.rl_ranks = rl_ranks;

	mode = atoi(argv[2]);
	uid = atoi(argv[3]);
	gid = atoi(argv[4]);
	strcpy(setid, argv[5]);

	/* int tgtct = atoi(argv[6]); */
	daos_rank_list_t  tgts;
	uint32_t          rl_tgts[1];

	rl_tgts[0] = 1;
	/* rl_tgts[1] = 1; */
	tgts.rl_nr.num = 1;
	tgts.rl_nr.num_out = 0;
	tgts.rl_ranks = rl_tgts;

	daos_rc = daos_pool_create(mode, uid, gid, (const char *)&setid,
				   &tgts, "rubbish", pool_size, &svc,
				   uuid, NULL);
	if (daos_rc)
		printf("<<<SimplePoolTest.c>>> Pool create fail, result: %d\n",
		       daos_rc);
	else {
		char uuid_str[100];

		uuid_unparse(uuid, uuid_str);
		printf("UUID> %s\n", uuid_str);
		printf("Number of out ranks: %i\n", svc.rl_nr.num_out);
		int i;

		for (i = 0; i < 10; i++)
			printf("[%i] = %i\n", i, svc.rl_ranks[i]);

		fflush(stdout);
	}

	return daos_rc;
}


int
create_then_destroy(int argc, char **argv)
{
	uint32_t          rl_ranks = 0;
	uint64_t          pool_size = 1024*1024*1024;
	uuid_t            uuid;
	daos_rank_list_t  svc;
	char              setid[500];
	char              *tgt;
	int               daos_rc;
	int               mode;
	unsigned int      gid, uid;
	daos_rank_list_t  tgts;
	daos_rank_list_t  *tgts_ptr;
	uint32_t          rl_tgts[10];

	printf("argc is %i\n", argc);
	if (argc < 6)
		return TEST_FAILED;

	/* create a pool */
	svc.rl_nr.num = 1;
	svc.rl_nr.num_out = 0;
	svc.rl_ranks = &rl_ranks;

	mode = atoi(argv[2]);
	uid = atoi(argv[3]);
	gid = atoi(argv[4]);
	strcpy(setid, argv[5]);

	if (argc == 7) {
		printf("in the if argv[6] is %s\n", argv[6]);
		int i = 0;

		tgt = strtok(argv[6], ",");
		rl_tgts[i++] = atoi(tgt);
		while ((tgt = strtok(NULL, ","))) {
			rl_tgts[i++] = atoi(tgt);
			printf("tgt %i is %s\n", i-1, tgt);
		}
		tgts.rl_nr.num = i;
		tgts.rl_nr.num_out = 0;
		tgts.rl_ranks = rl_tgts;
		tgts_ptr = &tgts;
	} else
		tgts_ptr = (daos_rank_list_t *)NULL;

	daos_rc = daos_pool_create(mode, uid, gid, (const char *)&setid,
				   tgts_ptr, "rubbish", pool_size, &svc,
				   uuid, NULL);

	if (daos_rc)
		printf("<<<SimplePoolTest.c>>> Pool create fail, result: %d\n",
		       daos_rc);
	else {
		sleep(5);
		daos_rc = daos_pool_destroy(uuid, setid, 1, NULL);
		if (daos_rc)
			printf("<<<SimplePoolTest.c>>> Destroy result: %d\n",
			       daos_rc);
	}
	fflush(stdout);
	return daos_rc;
}

/**
 * Use the daos_pool_connect API to attach to a pool.
 * Expecting argv as follows:
 * "connect pool_uuid daos_server_grp mode"
 */
int
poolconnect(int argc, char **argv)
{
	uuid_t uuid;
	char grp[30];
	int rc;
	unsigned int flag;
	daos_handle_t poh;
	daos_pool_info_t info;

	printf("\n<<<SimplePoolTest.c>>> Connect argc: %i\n", argc);
	if (argc != 5)
		return TEST_FAILED;
	printf("\nargv2 %s argv3 %s argv4 %s\n", argv[2], argv[3], argv[4]);
	rc = uuid_parse(argv[2], uuid);
	strcpy(grp, argv[3]);
	if (strcmp(argv[4], FLAG_RO) == 0)
		flag = DAOS_PC_RO;
	else if (strcmp(argv[4], FLAG_RW) == 0)
		flag = DAOS_PC_RW;
	else if (strcmp(argv[4], FLAG_EX) == 0)
		flag = DAOS_PC_EX;

	flag = DAOS_PC_RO;
	strcpy(grp, "daos_server");
	rc = daos_pool_connect(uuid, grp, NULL, flag, &poh, &info, NULL);

	if (rc)
		printf("\n<<<SimplePoolTest.c>>> Pool connect result: %d\n",
		       rc);
	else
		printf("\n<<<SimplePoolTest.c>>> Connected to pool.\n");
	fflush(stdout);

	return rc;
}

int
main(int argc, char **argv)
{
	int daos_rc;
	int test_rc = TEST_SUCCESS;

	if (argc < 3) {
		printf("too few args\n");
		return TEST_FAILED;
	}

	/* get things ready */
	test_rc = setup(argc, argv);

	if (test_rc == TEST_FAILED)
		return test_rc;

	if (!strncmp(argv[1], POOL_CREATE, 5)) {
		daos_rc = create(argc, argv);
		if (daos_rc)
			test_rc = TEST_FAILED;

	} else if (!strncmp(argv[1], POOL_DESTROY, 5)) {

		daos_rc = destroy(argc, argv);
		if (daos_rc)
			test_rc = TEST_FAILED;
	} else if (!strncmp(argv[1], POOL_CREATE_AND_DESTROY, 5)) {

		daos_rc = create_then_destroy(argc, argv);
		if (daos_rc)
			test_rc = TEST_FAILED;
	} else if (!strncmp(argv[1], POOL_CONNECT, 5)) {

		daos_rc = poolconnect(argc, argv);
		if (daos_rc)
			test_rc = TEST_FAILED;

	} else if (!strncmp(argv[1], "dump", 4)) {

		daos_rc = create_and_dump(argc, argv);
		if (daos_rc)
			test_rc = TEST_FAILED;

	} else {
		test_rc = TEST_FAILED;
		printf("\n<<<SimplePoolTest.c>>> %s is not a valid request.\n",
		       argv[1]);
		fflush(stdout);
	}

	done();
	exit(test_rc);
}
