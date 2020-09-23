/**
 * (C) Copyright 2019 Intel Corporation.
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
 * This file is part of daos
 *
 * tests/suite/daos_nvme_recovery.c
 *
 *
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_test.h"
#include "daos_iotest.h"

static void
set_fail_loc(test_arg_t *arg, d_rank_t rank, uint64_t fail_loc)
{
	if (arg->myrank == 0)
		daos_mgmt_set_params(arg->group, rank, DMG_KEY_FAIL_LOC,
				     fail_loc, 0, NULL);
	MPI_Barrier(MPI_COMM_WORLD);
}

static bool
is_nvme_enabled(test_arg_t *arg)
{
	daos_pool_info_t	 pinfo = { 0 };
	struct daos_pool_space	*ps = &pinfo.pi_space;
	int			 rc;

	pinfo.pi_bits = DPI_ALL;
	rc = test_pool_get_info(arg, &pinfo);
	assert_int_equal(rc, 0);

	return ps->ps_free_min[DAOS_MEDIA_NVME] != 0;
}

/* Online faulty reaction */
static void
nvme_recov_1(void **state)
{
	test_arg_t	*arg = *state;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	char		 dkey[DTS_KEY_LEN] = { 0 };
	char		 akey[DTS_KEY_LEN] = { 0 };
	int		 obj_class, key_nr = 10;
	int		 rank = 0, tgt_idx = 0;
	int		 i, j;

	if (!is_nvme_enabled(arg)) {
		print_message("NVMe isn't enabled.\n");
		skip();
	}

	if (arg->pool.pool_info.pi_nnodes < 2)
		obj_class = DAOS_OC_R1S_SPEC_RANK;
	else
		obj_class = DAOS_OC_R2S_SPEC_RANK;

	oid = dts_oid_gen(obj_class, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, rank);
	oid = dts_oid_set_tgt(oid, tgt_idx);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	print_message("Generating data on obj "DF_OID"...\n", DP_OID(oid));
	for (i = 0; i < key_nr; i++) {
		dts_key_gen(dkey, DTS_KEY_LEN, "dkey");
		for (j = 0; j < key_nr; j++) {
			dts_key_gen(akey, DTS_KEY_LEN, "akey");
			insert_single(dkey, akey, 0, "data", strlen("data") + 1,
				      DAOS_TX_NONE, &req);
		}
	}
	ioreq_fini(&req);

	print_message("Error injection to simulate device faulty.\n");
	set_fail_loc(arg, rank, DAOS_NVME_FAULTY | DAOS_FAIL_ONCE);

	/*
	 * FIXME: Due to lack of infrastructures for checking each target
	 *	  status, let's just wait for an arbitrary time and hope the
	 *	  faulty reaction & rebuild is triggered.
	 */
	print_message("Waiting for faulty reaction being triggered...\n");
	sleep(60);

	print_message("Waiting for rebuild done...\n");
	if (arg->myrank == 0)
		test_rebuild_wait(&arg, 1);
	MPI_Barrier(MPI_COMM_WORLD);

	/*
	 * FIXME: Need to verify target is in DOWNOUT when the infrastructure
	 *	  is ready.
	 */
	print_message("Waiting for faulty reaction done...\n");
	sleep(60);
	print_message("Done\n");
}

/* Verify device states after NVMe set to faulty*/
static void
nvme_recov_2(void **state)
{
	test_arg_t	*arg = *state;
	device_list	*devices = NULL;
	daos_obj_id_t	oid;
	char		data_buf[100];
	char		fetch_buf[100] = { 0 };
	struct ioreq	req;
	int		ndisks;
	int		rc, i;
	char		*server_config_file;
	char		*log_file;
	int		obj_class;

	if (!is_nvme_enabled(arg)) {
		print_message("NVMe isn't enabled.\n");
		skip();
	}

	/**
	*Get the Total number of NVMe devices from all the servers.
	*/
	rc = dmg_storage_device_list(dmg_config_file, &ndisks, NULL);
	assert_int_equal(rc, 0);
	print_message("Total Disks = %d\n", ndisks);

	/**
	*Get the Device info of all NVMe devices.
	*/
	D_ALLOC_ARRAY(devices, ndisks);
	rc = dmg_storage_device_list(dmg_config_file, NULL, devices);
	assert_int_equal(rc, 0);
	for (i = 0; i < ndisks; i++)
		print_message("Rank=%d UUID=%s state=%s host=%s\n",
			      devices[i].rank, DP_UUID(devices[i].device_id),
			devices[i].state, devices[i].host);

	/*
	 * Get the server config file from running process on server.
	 * Verify log_mask in server.yaml file, It should be 'DEBUG' to verify
	 * different state of NVMe drives. Skip the test if log_mask is not
	 * set to DEBUG.
	 */
	D_ALLOC(server_config_file, 512);
	D_ALLOC(log_file, 1024);
	for (i = 0; i < ndisks; i++) {
		if (devices[i].rank == 1) {
			rc = get_server_config(devices[i].host,
					       server_config_file);
			assert_int_equal(rc, 0);
			print_message("server_config_file = %s\n",
				      server_config_file);

			get_log_file(devices[i].host,
				server_config_file, " log_file", log_file);
			rc = verify_server_log_mask(devices[i].host,
						    server_config_file,
						    "DEBUG");
			if (rc) {
				print_message("Log Mask != DEBUG in %s.\n",
					      server_config_file);
				skip();
			}
		}
	}
	print_message("LOG FILE = %s\n", log_file);

	/** Prepare records **/
	if (arg->pool.pool_info.pi_nnodes < 2)
		obj_class = DAOS_OC_R1S_SPEC_RANK;
	else
		obj_class = DAOS_OC_R2S_SPEC_RANK;

	oid = dts_oid_gen(obj_class, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, 1);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);
	memset(data_buf, 'a', 100);

	/** Insert record **/
	print_message("Insert single record with 100 extents\n");
	insert_single_with_rxnr("dkey", "akey", 0, data_buf,
				1, 100, DAOS_TX_NONE, &req);

	/**
	*Set single device for rank1 to faulty.
	*/
	for (i = 0; i < ndisks; i++) {
		if (devices[i].rank == 1) {
			print_message(
				"NVMe with UUID=%s on host=%s\" set to Faulty\n",
				DP_UUID(devices[i].device_id),
				devices[i].host);
			rc = dmg_storage_set_nvme_fault(dmg_config_file,
							devices[i].host,
				devices[i].device_id, 1);
			assert_int_equal(rc, 0);
			break;
		}
	}
	sleep(60);

	/**
	* Verify Rank1 device state change from NORMAL to FAULTY.
	* Verify "FAULTY -> TEARDOWN" and "TEARDOWN -> OUT" device states found
	* in server log.
	*/
	rc = dmg_storage_device_list(dmg_config_file, NULL, devices);
	assert_int_equal(rc, 0);
	for (i = 0; i < ndisks; i++) {
		if (devices[i].rank == 1) {
			assert_string_equal(devices[i].state, "\"FAULTY\"");

			rc = verify_state_in_log(devices[i].host,
						 log_file, "TEARDOWN -> OUT");
			if (rc != 0) {
				print_message(
					"TEARDOWN -> OUT not found in log %s\n",
					log_file);
				assert_int_equal(rc, 0);
			}

			rc = verify_state_in_log(devices[i].host,
						 log_file,
						 "FAULTY -> TEARDOWN");
			if (rc != 0) {
				print_message(
					"FAULTY -> TEARDOWN not found in %s\n",
					log_file);
				assert_int_equal(rc, 0);
			}
			break;
		}

	}

	/** Lookup all the records and verify the content **/
	print_message("Lookup and Verify all the records:\n");
	lookup_single_with_rxnr("dkey", "akey", 0, fetch_buf,
				1, 100, DAOS_TX_NONE, &req);
	for (i = 0; i < 100; i++)
		assert_memory_equal(&fetch_buf[i], "a", 1);

	/*
	 * FIXME: Add FAULTY disks back to the system, when feature available.
	 */

	/* Tear down */
	D_FREE(server_config_file);
	D_FREE(log_file);
	D_FREE(devices);
}

/* Verify NVMe I/O error and notification*/
static void
nvme_test_simulate_IO_error(void **state)
{
	test_arg_t	*arg = *state;
	device_list	*devices = NULL;
	daos_obj_id_t	oid;
	struct ioreq	req;
	const char		dkey[] = "dkey";
	const char		akey[] = "akey";
	daos_size_t		size = 4 * 4096; /* record size */
	char		*ow_buf;
	char		*fbuf;
	char		*write_errors;
	char		*read_errors;
	char		*check_errors;
	char		*control_log_file;
	char		*server_config_file;
	int		rx_nr; /* number of record extents */
	int		rank_pos = 0, rank = 1;
	int		ndisks, rc, i;

	if (!is_nvme_enabled(arg)) {
		print_message("NVMe isn't enabled.\n");
		skip();
	}

	/*
	 *  Get the Total number of NVMe devices from all the servers.
	 */
	rc = dmg_storage_device_list(dmg_config_file, &ndisks, NULL);
	assert_int_equal(rc, 0);
	print_message("Total Disks = %d\n", ndisks);

	/*
	 * Get the Device info of all NVMe devices
	 */
	D_ALLOC_ARRAY(devices, ndisks);
	rc = dmg_storage_device_list(dmg_config_file, NULL, devices);
	assert_int_equal(rc, 0);

	/*
	 * Get the rank 1 position from array devices
	 */
	for (i = 0; i < ndisks; i++) {
		if (devices[i].rank == 1) {
			rank_pos = i;
			break;
		}
	}

	/*
	 * Get DAOS server file
	 */
	D_ALLOC(control_log_file, 1024);
	D_ALLOC(server_config_file, 512);
	rc = get_server_config(devices[rank_pos].host, server_config_file);
	assert_int_equal(rc, 0);
	print_message("server_config_file = %s\n", server_config_file);

	/*
	 * Get DAOS control log file
	 */
	get_log_file(devices[rank_pos].host,
		server_config_file, "control_log_file", control_log_file);
	print_message("Control Log File = %s\n", control_log_file);
	D_FREE(server_config_file);

	/*
	 * Get the Initial write error
	 */
	write_errors = strdup("write_errors");
	rc = dmg_storage_query_device_health(dmg_config_file,
		devices[rank_pos].host, write_errors,
		devices[rank_pos].device_id);
	assert_int_equal(rc, 0);
	print_message("Initial write_errors = %s\n", write_errors);

	/*
	 * Get the Initial read error
	 */
	read_errors = strdup("read_errors");
	rc = dmg_storage_query_device_health(dmg_config_file,
		devices[rank_pos].host, read_errors,
		devices[rank_pos].device_id);
	assert_int_equal(rc, 0);
	print_message("Initial read_errors = %s\n", read_errors);

	/*
	 * Allocate and set write buffer with data
	 */
	D_ALLOC(ow_buf, size);
	assert_non_null(ow_buf);
	dts_buf_render(ow_buf, size);
	/*
	 * Allocate and set fetch buffer
	 */
	D_ALLOC(fbuf, size);
	assert_non_null(fbuf);
	memset(fbuf, 0, size);

	/*
	 * Inject BIO Write Errors on Rank1 device
	 */
	print_message("----Inject BIO Write Error----\n");
	set_fail_loc(arg, rank, DAOS_NVME_BIO_WRITE_ERR);

	/*
	 * Prepare records
	 */
	oid = dts_oid_gen(DAOS_OC_R1S_SPEC_RANK, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, rank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/*
	 * Insert the initial 4K record which will go through NVMe
	 */
	rx_nr = size / OW_IOD_SIZE;
	insert_single_with_rxnr(dkey, akey, /*idx*/0, ow_buf, OW_IOD_SIZE,
				rx_nr, DAOS_TX_NONE, &req);

	/*
	 * Inject BIO Read Errors on Rank1 device
	 */
	print_message("----Inject BIO Read Error----\n");
	set_fail_loc(arg, rank, DAOS_NVME_BIO_READ_ERR);

	/*
	 * Read and verify the data
	 */
	lookup_single_with_rxnr(dkey, akey, /*idx*/0, fbuf,
			OW_IOD_SIZE, size, DAOS_TX_NONE, &req);
	assert_memory_equal(ow_buf, fbuf, size);

	/*
	 * Get the write error count after Injecting BIO write error.
	 * Verify the recent write err count is > the initial err count.
	 */
	check_errors = strdup("write_errors");
	rc = dmg_storage_query_device_health(dmg_config_file,
		devices[rank_pos].host, check_errors,
		devices[rank_pos].device_id);
	assert_int_equal(rc, 0);
	print_message("Final write_error = %s\n", check_errors);
	assert_true(atoi(check_errors) > atoi(write_errors));

	/*
	 * Get the read error count after Injecting BIO read error
	 * Verify the recent read err count is > the initial err count.
	 */
	strcpy(check_errors, "read_errors");
	rc = dmg_storage_query_device_health(dmg_config_file,
		devices[rank_pos].host, check_errors,
		devices[rank_pos].device_id);
	assert_int_equal(rc, 0);
	print_message("Final read_errors = %s\n", check_errors);
	assert_true(atoi(check_errors) > atoi(read_errors));

	/*
	 * Verify writeErr=true and readErr:true available in control log
	 */
	char control_err[][50] = {
		"detected blob I/O error! writeErr:true",
		"detected blob I/O error! readErr:true"};
	for (i = 0; control_err[i][0] != '\0'; i++) {
		rc = verify_state_in_log(devices[rank_pos].host,
			control_log_file, control_err[i]);
		if (rc != 0) {
			print_message(
				" %s not found in log %s\n", control_err[i],
				control_log_file);
			assert_int_equal(rc, 0);
		}
	}

	/* Tear down */
	D_FREE(ow_buf);
	D_FREE(fbuf);
	D_FREE(devices);
	D_FREE(write_errors);
	D_FREE(read_errors);
	D_FREE(check_errors);
	D_FREE(control_log_file);
	ioreq_fini(&req);
}

static const struct CMUnitTest nvme_recov_tests[] = {
	{"NVMe Recovery 1: Online faulty reaction",
	 nvme_recov_1, NULL, test_case_teardown},
	{"NVMe Recovery 2: Verify device states after NVMe set to Faulty",
	 nvme_recov_2, NULL, test_case_teardown},
	{"NVMe Recovery 3: Verify NVMe IO error and notification",
	 nvme_test_simulate_IO_error, NULL, test_case_teardown},
};

static int
nvme_recov_test_setup(void **state)
{
	int     rc;

	rc = test_setup(state, SETUP_CONT_CONNECT, true, DEFAULT_POOL_SIZE,
			NULL);

	return rc;
}

int
run_daos_nvme_recov_test(int rank, int size, int *sub_tests,
			 int sub_tests_size)
{
	int rc = 0;

	MPI_Barrier(MPI_COMM_WORLD);
	if (sub_tests_size == 0) {
		sub_tests_size = ARRAY_SIZE(nvme_recov_tests);
		sub_tests = NULL;
	}

	rc = run_daos_sub_tests("DAOS nvme recov tests", nvme_recov_tests,
				ARRAY_SIZE(nvme_recov_tests), sub_tests,
				sub_tests_size, nvme_recov_test_setup,
				test_teardown);

	MPI_Barrier(MPI_COMM_WORLD);

	return rc;
}
