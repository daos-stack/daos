/**
 * (C) Copyright 2019-2020 Intel Corporation.
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
set_fail_loc(test_arg_t *arg, d_rank_t rank, uint64_t tgtidx,
	     uint64_t fail_loc)
{
	if (arg->myrank == 0)
		daos_mgmt_set_params(arg->group, rank, DMG_KEY_FAIL_LOC,
				     fail_loc, tgtidx, NULL);
	MPI_Barrier(MPI_COMM_WORLD);
}

static void
reset_fail_loc(test_arg_t *arg)
{
	if (arg->myrank == 0)
		daos_fail_loc_reset();
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
	test_arg_t		*arg = *state;
	daos_obj_id_t		 oid;
	struct ioreq		 req;
	daos_target_info_t	 tgt_info = { 0 };
	device_list		*devices = NULL;
	int			 ndisks;
	char			 dkey[DTS_KEY_LEN] = { 0 };
	char			 akey[DTS_KEY_LEN] = { 0 };
	int			 obj_class, key_nr = 10;
	int			 rank = 0, tgt_idx = 0;
	uint64_t		 fail_loc_tgt;
	int			 tgtidx[MAX_TEST_TARGETS_PER_DEVICE];
	int			 n_tgtidx = 0;
	int			 per_node_tgt_cnt = 0;
	int			 i, j, k, rc;

	if (!is_nvme_enabled(arg)) {
		print_message("NVMe isn't enabled.\n");
		skip();
	}

	/**
	 * Get the Total number of NVMe devices from all the servers.
	 */
	rc = dmg_storage_device_list(dmg_config_file, &ndisks, NULL);
	assert_int_equal(rc, 0);

	/**
	 * Get the Device info of all NVMe devices.
	 */
	D_ALLOC_ARRAY(devices, ndisks);
	rc = dmg_storage_device_list(dmg_config_file, NULL, devices);
	assert_int_equal(rc, 0);
	for (i = 0; i < ndisks; i++) {
		if (devices[i].rank != rank)
			continue;
		print_message("Rank=%d UUID=%s state=%s host=%s tgts=",
			      devices[i].rank, DP_UUID(devices[i].device_id),
			      devices[i].state, devices[i].host);
		for (j = 0; j < devices[i].n_tgtidx; j++)
			print_message("%d,", devices[i].tgtidx[j]);
		print_message("\n");
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

	/** Query test args to get total pool target count per node */
	assert_true(arg->srv_ntgts > arg->srv_nnodes);
	per_node_tgt_cnt = arg->srv_ntgts/arg->srv_nnodes;

	/**
	 * Verify initial states for all pool targets are UPIN by querying
	 * the pool target info.
	 */
	for (i = 0; i < per_node_tgt_cnt; i++) {
		rc = daos_pool_query_target(arg->pool.poh, i/*tgt*/,
					    rank, &tgt_info, NULL /*ev*/);
		assert_int_equal(rc, 0);
		rc = strcmp(daos_target_state_enum_to_str(tgt_info.ta_state),
			    "UPIN");
		assert_int_equal(rc, 0);
	}
	print_message("All targets are in UPIN\n");

	/** Inject error on random target index */
	srand(time(NULL));
	fail_loc_tgt = rand() % per_node_tgt_cnt;
	print_message("Error injection on tgt %"PRIu64" to simulate device"
		      " faulty.\n", fail_loc_tgt);
	set_fail_loc(arg, rank, fail_loc_tgt,
		     DAOS_NVME_FAULTY | DAOS_FAIL_ALWAYS);

	/* Verify that the DAOS_NVME_FAULTY reaction got triggered. Target should
	 * be in the DOWN state to trigger rebuild (or DOWNOUT if rebuild already
	 * completed).
	 */
	print_message("Waiting for faulty reaction being triggered...\n");
	rc = wait_and_verify_pool_tgt_state(arg->pool.poh, fail_loc_tgt,
						    rank, "DOWN|DOWNOUT");
	assert_int_equal(rc, 0);
	/* Need to reset lock when using DAOS_FAIL_ALWAYS flag */
	reset_fail_loc(arg);

	/**
	 * Look up all targets currently mapped to the device that is now faulty.
	 */
	for (i = 0; i < ndisks; i++) {
		if (devices[i].rank != rank)
			continue;
		n_tgtidx = devices[i].n_tgtidx;
		for (j = 0; j < n_tgtidx; j++) {
			if (devices[i].tgtidx[j] == fail_loc_tgt) {
				for (k = 0; k < devices[i].n_tgtidx; k++)
					tgtidx[k] = devices[i].tgtidx[k];
			}
		}
	}

	print_message("Waiting for rebuild done...\n");
	if (arg->myrank == 0)
		test_rebuild_wait(&arg, 1);
	MPI_Barrier(MPI_COMM_WORLD);

	print_message("Waiting for faulty reaction done...\n");
	/**
	 * Verify all mapped device targets are in DOWNOUT state.
	 */
	for (i = 0; i < n_tgtidx; i++) {
		rc = wait_and_verify_pool_tgt_state(arg->pool.poh, tgtidx[i],
						    rank, "DOWNOUT");
		assert_int_equal(rc, 0);
	}
	print_message("All mapped device targets are in DOWNOUT\n");

	/**
	 * Print the final pool target states.
	 */
	for (i = 0; i < per_node_tgt_cnt; i++) {
		rc = daos_pool_query_target(arg->pool.poh, i/*tgt*/,
					    rank, &tgt_info, NULL /*ev*/);
		assert_int_equal(rc, 0);
		print_message("Pool target:%d, state:%s\n", i,
			      daos_target_state_enum_to_str(tgt_info.ta_state));
	}

	D_FREE(devices);
	print_message("Done\n");
}

/* Verify device states after NVMe set to faulty*/
static void
nvme_test_verify_device_stats(void **state)
{
	test_arg_t	*arg = *state;
	device_list	*devices = NULL;
	int		ndisks;
	int		rc, i;
	char		*server_config_file;
	char		*log_file;
	int		rank_pos = 0;

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

	if (ndisks <= 1) {
		print_message("Need Minimum 2 disks for test\n");
		skip();
	}

	/*
	 * Get the rank 0 position from array devices.
	 */
	for (i = 0; i < ndisks; i++) {
		if (devices[i].rank == 0)
			rank_pos = i;
	}

	/*
	 * Get the server config file from running process on server.
	 * Verify log_mask in server.yaml file, It should be 'DEBUG' to verify
	 * different state of NVMe drives. Skip the test if log_mask is not
	 * set to DEBUG.
	 */
	D_ALLOC(server_config_file, 512);
	D_ALLOC(log_file, 1024);
	rc = get_server_config(devices[rank_pos].host,
			       server_config_file);
	assert_int_equal(rc, 0);
	print_message("server_config_file = %s\n", server_config_file);

	get_log_file(devices[rank_pos].host, server_config_file,
		     " log_file", log_file);
	rc = verify_server_log_mask(devices[rank_pos].host,
				    server_config_file, "DEBUG");
	if (rc) {
		print_message("Log Mask != DEBUG in %s.\n",
			      server_config_file);
		skip();
	}

	print_message("LOG FILE = %s\n", log_file);

	/**
	*Set single device for rank0 to faulty.
	*/
	print_message("NVMe with UUID=%s on host=%s\" set to Faulty\n",
		      DP_UUID(devices[rank_pos].device_id),
		devices[rank_pos].host);
	rc = dmg_storage_set_nvme_fault(dmg_config_file,
					devices[rank_pos].host,
		devices[rank_pos].device_id, 1);
	assert_int_equal(rc, 0);
	sleep(60);

	/**
	* Verify Rank0 device state change from NORMAL to FAULTY.
	* Verify "FAULTY -> TEARDOWN" and "TEARDOWN -> OUT" device states found
	* in server log.
	*/
	rc = dmg_storage_device_list(dmg_config_file, NULL, devices);
	assert_int_equal(rc, 0);
	/*
	 * Get the rank 0 position from array devices.
	 */
	for (i = 0; i < ndisks; i++) {
		if (devices[i].rank == 0)
			rank_pos = i;
	}
	assert_string_equal(devices[rank_pos].state, "\"FAULTY\"");

	rc = verify_state_in_log(devices[rank_pos].host, log_file,
				 "NORMAL -> FAULTY");
	if (rc != 0) {
		print_message("NORMAL -> FAULTY not found in log %s\n",
			      log_file);
		assert_int_equal(rc, 0);
	}

	rc = verify_state_in_log(devices[rank_pos].host, log_file,
				 "FAULTY -> TEARDOWN");
	if (rc != 0) {
		print_message("FAULTY -> TEARDOWN not found in %s\n",
			      log_file);
		assert_int_equal(rc, 0);
	}

	rc = verify_state_in_log(devices[rank_pos].host, log_file,
				 "TEARDOWN -> OUT");
	if (rc != 0) {
		print_message("TEARDOWN -> OUT not found in log %s\n",
			      log_file);
		assert_int_equal(rc, 0);
	}

	/*
	 * FIXME: Add FAULTY disks back to the system, when feature available.
	 */

	/* Tear down */
	D_FREE(server_config_file);
	D_FREE(log_file);
	D_FREE(devices);
}

/*
 * Verify blobstore state transitions from NORMAL->OUT after device is marked
 * as "FAULTY" by querying the internal blobstore device state by calling the
 * daos_mgmt_get_bs_state() C API.
 */
static void
nvme_test_get_blobstore_state(void **state)
{
	test_arg_t	*arg = *state;
	device_list	*devices = NULL;
	daos_obj_id_t	 oid;
	struct ioreq	 req;
	char		 dkey[DTS_KEY_LEN] = { 0 };
	char		 akey[DTS_KEY_LEN] = { 0 };
	int		 obj_class, key_nr = 10;
	int		 rank = 0, tgt_idx = 0;
	int		 blobstore_state;
	int		 i, j;
	int		 ndisks;
	int		 rc;

	if (!is_nvme_enabled(arg)) {
		print_message("NVMe isn't enabled.\n");
		skip();
	}

	/**
	 * Get the total number of NVMe devices from all the servers.
	 */
	rc = dmg_storage_device_list(dmg_config_file, &ndisks, NULL);
	assert_int_equal(rc, 0);
	print_message("Total Disks = %d\n", ndisks);

	/**
	 * Get the Device info of all NVMe devices.
	 */
	D_ALLOC_ARRAY(devices, ndisks);
	rc = dmg_storage_device_list(dmg_config_file, NULL, devices);
	assert_int_equal(rc, 0);
	for (i = 0; i < ndisks; i++)
		print_message("Rank=%d UUID=%s state=%s host=%s\n",
			      devices[i].rank, DP_UUID(devices[i].device_id),
			devices[i].state, devices[i].host);


	/**
	 * Set the object class and generate data on objects.
	 */
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

	/**
	 * Verify blobstore of first device returned is in "NORMAL" state
	 * before setting to faulty.
	 */
	rc = daos_mgmt_get_bs_state(arg->group, devices[0].device_id,
				    &blobstore_state, NULL /*ev*/);
	assert_int_equal(rc, 0);

	rc = verify_blobstore_state(blobstore_state, "normal");
	assert_int_equal(rc, 0);
	print_message("Blobstore is in NORMAL state\n");

	/**
	 * Manually set first device returned to faulty via
	 * 'dmg storage set nvme-faulty'.
	 */
	print_message("NVMe with UUID=%s on host=%s\" set to Faulty\n",
		      DP_UUID(devices[0].device_id),
		      devices[0].host);
	rc = dmg_storage_set_nvme_fault(dmg_config_file, devices[0].host,
					devices[0].device_id, 1);
	assert_int_equal(rc, 0);

	/**
	 *  Continue to check blobstore state until "OUT" state is returned
	 *  or max test retry count is hit (5 min).
	 */
	rc = wait_and_verify_blobstore_state(devices[0].device_id,
					     /*expected state*/"out",
					     arg->group);
	assert_int_equal(rc, 0);

	print_message("Blobstore is in OUT state\n");
	D_FREE(devices);
}

/* Simulate both an NVMe I/O read and write error.
 * Check error counters in BIO health stats to verify R/W error counts,
 * and also verify I/O error notification on the console output.
 */
static void
nvme_test_simulate_IO_error(void **state)
{
	test_arg_t	*arg = *state;
	device_list	*devices = NULL;
	daos_obj_id_t	oid;
	struct ioreq	req;
	const char	dkey[] = "dkey";
	const char	akey[] = "akey";
	daos_size_t	size = 4 * 4096; /* record size */
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
	 * Prepare records
	 */
	oid = dts_oid_gen(DAOS_OC_R1S_SPEC_RANK, 0, arg->myrank);
	oid = dts_oid_set_rank(oid, rank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/*
	 * Insert the initial 4K record which will go through NVMe
	 */
	print_message("Insert Initial record\n");
	rx_nr = size / OW_IOD_SIZE;
	insert_single_with_rxnr(dkey, akey, /*idx*/0, ow_buf,
				OW_IOD_SIZE, rx_nr, DAOS_TX_NONE, &req);

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
	get_log_file(devices[rank_pos].host, server_config_file,
		     "control_log_file", control_log_file);
	print_message("Control Log File = %s\n", control_log_file);
	D_FREE(server_config_file);

	/*
	 * Get the Initial write error
	 */
	write_errors = strdup("bio_write_errs");
	rc = dmg_storage_query_device_health(dmg_config_file,
					     devices[rank_pos].host,
					     write_errors,
		devices[rank_pos].device_id);
	assert_int_equal(rc, 0);
	print_message("Initial write_errors = %s\n", write_errors);

	/*
	 * Get the Initial read error
	 */
	read_errors = strdup("bio_read_errs");
	rc = dmg_storage_query_device_health(dmg_config_file,
					     devices[rank_pos].host,
					     read_errors,
		devices[rank_pos].device_id);
	assert_int_equal(rc, 0);
	print_message("Initial read_errors = %s\n", read_errors);

	/*
	 * Inject BIO Read Errors on Rank1 device
	 */
	print_message("----Inject BIO Read Error----\n");
	set_fail_loc(arg, rank, 0, DAOS_NVME_READ_ERR | DAOS_FAIL_ONCE);

	/*
	 * Read the data which will induce the READ Error and expected to fail
	 * with DER_IO Error.
	 */
	arg->expect_result = -DER_IO;
	lookup_single_with_rxnr(dkey, akey, /*idx*/0, fbuf,
				OW_IOD_SIZE, size, DAOS_TX_NONE, &req);

	/*
	 * Inject BIO Write Errors on Rank1 device
	 */
	print_message("----Inject BIO Write Error----\n");
	set_fail_loc(arg, rank, 0, DAOS_NVME_WRITE_ERR | DAOS_FAIL_ONCE);

	/*
	 * Insert the 4K record again which will induce WRITE Error and
	 * expected to fail with DER_IO Error.
	 */
	rx_nr = size / OW_IOD_SIZE;
	insert_single_with_rxnr(dkey, akey, /*idx*/0, ow_buf, OW_IOD_SIZE,
				rx_nr, DAOS_TX_NONE, &req);

	/*
	 * Get the write error count after Injecting BIO write error.
	 * Verify the recent write err count is > the initial err count.
	 */
	arg->expect_result = 0;
	check_errors = strdup("bio_write_errs");
	rc = dmg_storage_query_device_health(dmg_config_file,
					     devices[rank_pos].host,
					     check_errors,
		devices[rank_pos].device_id);
	assert_int_equal(rc, 0);
	print_message("Final write_error = %s\n", check_errors);
	assert_true(atoi(check_errors) == atoi(write_errors) + 1);

	/*
	 * Get the read error count after Injecting BIO read error
	 * Verify the recent read err count is > the initial err count.
	 */
	strcpy(check_errors, "bio_read_errs");
	rc = dmg_storage_query_device_health(dmg_config_file,
					     devices[rank_pos].host,
					     check_errors,
		devices[rank_pos].device_id);
	assert_int_equal(rc, 0);
	print_message("Final read_errors = %s\n", check_errors);
	assert_true(atoi(check_errors) == atoi(read_errors) + 1);

	/*
	 * Verify writeErr=true and readErr:true available in control log
	 */
	char control_err[][50] = {
		"detected blob I/O error! writeErr:true",
		"detected blob I/O error! readErr:true"};
	for (i = 0; i < 2 ; i++) {
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
	 nvme_test_verify_device_stats, NULL, test_case_teardown},
	{"NVMe Recovery 3: Verify blobstore state NORMAL->OUT transition",
	 nvme_test_get_blobstore_state, NULL, test_case_teardown},
	{"NVMe Recovery 4: Verify NVMe IO error and notification",
	 nvme_test_simulate_IO_error, NULL, test_case_teardown},
};

static int
nvme_recov_test_setup(void **state)
{
	int     rc;

	rc = test_setup(state, SETUP_CONT_CONNECT, true, DEFAULT_POOL_SIZE,
			0, NULL);

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
