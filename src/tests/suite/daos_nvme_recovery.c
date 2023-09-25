/**
 * (C) Copyright 2019-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
		daos_debug_set_params(arg->group, rank, DMG_KEY_FAIL_LOC,
				     fail_loc, tgtidx, NULL);
	par_barrier(PAR_COMM_WORLD);
}

static void
reset_fail_loc(test_arg_t *arg)
{
	if (arg->myrank == 0)
		daos_fail_loc_reset();
	par_barrier(PAR_COMM_WORLD);
}

static bool
is_nvme_enabled(test_arg_t *arg)
{
	daos_pool_info_t	 pinfo = { 0 };
	struct daos_pool_space	*ps = &pinfo.pi_space;
	int			 rc;

	pinfo.pi_bits = DPI_ALL;
	rc = test_pool_get_info(arg, &pinfo, NULL /* engine_ranks */);
	assert_rc_equal(rc, 0);

	return ps->ps_free_min[DAOS_MEDIA_NVME] != 0;
}

/* Online/Offline faulty reaction */
static void
nvme_fault_reaction(void **state, int mode)
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
	int			 faulty_disk_idx = 0;
	uuid_t			 offline_pool_uuid;

	FAULT_INJECTION_REQUIRED();

	if (!is_nvme_enabled(arg)) {
		print_message("NVMe isn't enabled.\n");
		skip();
	}

	/**
	* If test need multiple pool with both mode offline and online
	* create the another pool which will be offline by default.
	*/
	if (mode == 2) {
		char	*env;
		int	size_gb;
		daos_size_t	scm_size = (daos_size_t)4 << 30/*Default 4G*/;
		daos_size_t	nvme_size;

		/* Use the SCM size if set with environment */
		env = getenv("POOL_SCM_SIZE");
		if (env) {
			size_gb = atoi(env);
			if (size_gb != 0)
				scm_size = (daos_size_t)size_gb << 30;
		}

		/* NVMe size is 4x of SCM size */
		nvme_size = scm_size * 4;
		print_message("Creating another offline pool mode, ");
		print_message("Size: SCM = %" PRId64 " NVMe =%" PRId64 "\n",
			      scm_size, nvme_size);

		/* create another offline pool*/
		print_message("create another offline pool");
		rc = dmg_pool_create(dmg_config_file,
				     geteuid(), getegid(),
				     arg->group, NULL /* tgts */,
				     scm_size, nvme_size,
				     NULL /* prop */,
				     arg->pool.svc /* svc */,
				     offline_pool_uuid);
		assert_rc_equal(rc, 0);
	}

	/**
	 * Get the Total number of NVMe devices from all the servers.
	 */
	rc = dmg_storage_device_list(dmg_config_file, &ndisks, NULL);
	assert_rc_equal(rc, 0);

	/**
	 * Get the Device info of all NVMe devices.
	 */
	D_ALLOC_ARRAY(devices, ndisks);
	rc = dmg_storage_device_list(dmg_config_file, NULL, devices);
	assert_rc_equal(rc, 0);
	for (i = 0; i < ndisks; i++) {
		if (devices[i].rank != rank)
			continue;
		else
			faulty_disk_idx = i;

		print_message("Rank=%d UUID=" DF_UUIDF " state=%s host=%s tgts=",
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

	oid = daos_test_oid_gen(arg->coh, obj_class, 0, 0, arg->myrank);
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
		assert_rc_equal(rc, 0);
		rc = strcmp(daos_target_state_enum_to_str(tgt_info.ta_state),
			    "UPIN");
		assert_int_equal(rc, 0);
	}
	print_message("All targets are in UPIN\n");

	if (mode == 0) {
		print_message("Disconnect the pool for offline failure\n");
		rc = daos_cont_close(arg->coh, NULL);
		assert_rc_equal(rc, 0);
		rc = daos_pool_disconnect(arg->pool.poh, NULL);
		assert_rc_equal(rc, 0);
	}

	/** Inject error on random target index */
	srand(time(NULL));
	fail_loc_tgt = rand() % per_node_tgt_cnt;
	print_message("Error injection on tgt %"PRIu64" to simulate device"
		      " faulty.\n", fail_loc_tgt);
	set_fail_loc(arg, rank, fail_loc_tgt,
		     DAOS_NVME_FAULTY | DAOS_FAIL_ALWAYS);

	if (mode == 0) {
		/**
		*  Continue to check blobstore until state is "OUT"
		*  or max test retry count is hit (5 min).
		*/
		rc = wait_and_verify_blobstore_state(
			devices[faulty_disk_idx].device_id,
			/*expected state*/"out", arg->group);
		assert_rc_equal(rc, 0);

		/**
		* Connect the pool for query check.
		 */
		print_message("Connect the pool to get the pool query\n");
		rc = daos_pool_connect(arg->pool.pool_str, arg->group,
				       DAOS_PC_RW, &arg->pool.poh,
				       &arg->pool.pool_info, NULL /* ev */);
		assert_rc_equal(rc, 0);
		/* Set container handle as invalid so it does not close again*/
		arg->coh = DAOS_HDL_INVAL;
	}

	/* Verify that the DAOS_NVME_FAULTY reaction got triggered. Target should
	 * be in the DOWN state to trigger rebuild (or DOWNOUT if rebuild already
	 * completed).
	 */
	print_message("Waiting for faulty reaction being triggered...\n");
	rc = wait_and_verify_pool_tgt_state(arg->pool.poh, fail_loc_tgt,
						    rank, "DOWN|DOWNOUT");
	assert_rc_equal(rc, 0);
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
	par_barrier(PAR_COMM_WORLD);

	print_message("Waiting for faulty reaction done...\n");
	/**
	 * Verify all mapped device targets are in DOWNOUT state.
	 */
	for (i = 0; i < n_tgtidx; i++) {
		rc = wait_and_verify_pool_tgt_state(arg->pool.poh, tgtidx[i],
						    rank, "DOWNOUT");
		assert_rc_equal(rc, 0);
	}
	print_message("All mapped device targets are in DOWNOUT\n");

	/**
	 * Print the final pool target states.
	 */
	for (i = 0; i < per_node_tgt_cnt; i++) {
		rc = daos_pool_query_target(arg->pool.poh, i/*tgt*/,
					    rank, &tgt_info, NULL /*ev*/);
		assert_rc_equal(rc, 0);
		print_message("Pool target:%d, state:%s\n", i,
			      daos_target_state_enum_to_str(tgt_info.ta_state));
	}

	D_FREE(devices);
	print_message("Done\n");
}

static void
offline_fault_recovery(void **state)
{
	nvme_fault_reaction(state, 0 /* Offline */);
}

static void
online_fault_recovery(void **state)
{
	nvme_fault_reaction(state, 1 /* Online */);
}

static void
offline_and_online_fault_recovery(void **state)
{
	nvme_fault_reaction(state, 2 /* Offline and Online */);
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
	assert_rc_equal(rc, 0);
	print_message("Total Disks = %d\n", ndisks);

	/**
	*Get the Device info of all NVMe devices.
	*/
	D_ALLOC_ARRAY(devices, ndisks);
	rc = dmg_storage_device_list(dmg_config_file, NULL, devices);
	assert_rc_equal(rc, 0);
	for (i = 0; i < ndisks; i++)
		print_message("Rank=%d UUID=" DF_UUIDF " state=%s host=%s\n",
			      devices[i].rank, DP_UUID(devices[i].device_id),
			devices[i].state, devices[i].host);

	if (ndisks <= 1) {
		print_message("Need Minimum 2 disks for test\n");
		D_FREE(devices);
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
	D_ALLOC(server_config_file, DAOS_SERVER_CONF_LENGTH);
	D_ALLOC(log_file, 1024);
	rc = get_server_config(devices[rank_pos].host,
			       server_config_file);
	assert_rc_equal(rc, 0);
	print_message("server_config_file = %s\n", server_config_file);

	get_log_file(devices[rank_pos].host, server_config_file,
		     "control_log_file", log_file);
	rc = verify_server_log_mask(devices[rank_pos].host,
				    server_config_file, "DEBUG");
	if (rc) {
		print_message("Log Mask != DEBUG in %s.\n",
			      server_config_file);
		D_FREE(server_config_file);
		D_FREE(devices);
		D_FREE(log_file);
		skip();
	}

	print_message("LOG FILE = %s\n", log_file);

	/**
	*Set single device for rank0 to faulty.
	*/
	print_message("NVMe with UUID=" DF_UUIDF " on host=%s\" set to Faulty\n",
		      DP_UUID(devices[rank_pos].device_id),
		devices[rank_pos].host);
	rc = dmg_storage_set_nvme_fault(dmg_config_file,
					devices[rank_pos].host,
		devices[rank_pos].device_id, 1);
	assert_rc_equal(rc, 0);
	sleep(60);

	/**
	* Verify Rank0 device state change from NORMAL to FAULTY.
	* Verify "FAULTY -> TEARDOWN" and "TEARDOWN -> OUT" device states found
	* in server log.
	*/
	rc = dmg_storage_device_list(dmg_config_file, NULL, devices);
	assert_rc_equal(rc, 0);
	/*
	 * Get the rank 0 position from array devices.
	 */
	for (i = 0; i < ndisks; i++) {
		if (devices[i].rank == 0)
			rank_pos = i;
	}
	assert_string_equal(devices[rank_pos].state, "\"EVICTED\"");

	rc = verify_state_in_log(devices[rank_pos].host, log_file,
				 "NORMAL -> FAULTY");
	if (rc != 0) {
		print_message("NORMAL -> FAULTY not found in log %s\n",
			      log_file);
		assert_rc_equal(rc, 0);
	}

	rc = verify_state_in_log(devices[rank_pos].host, log_file,
				 "FAULTY -> TEARDOWN");
	if (rc != 0) {
		print_message("FAULTY -> TEARDOWN not found in %s\n",
			      log_file);
		assert_rc_equal(rc, 0);
	}

	rc = verify_state_in_log(devices[rank_pos].host, log_file,
				 "TEARDOWN -> OUT");
	if (rc != 0) {
		print_message("TEARDOWN -> OUT not found in log %s\n",
			      log_file);
		assert_rc_equal(rc, 0);
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
	int		 faulty_disk_idx = 0;
	int		 rc;

	if (!is_nvme_enabled(arg)) {
		print_message("NVMe isn't enabled.\n");
		skip();
	}

	/**
	 * Get the total number of NVMe devices from all the servers.
	 */
	rc = dmg_storage_device_list(dmg_config_file, &ndisks, NULL);
	assert_rc_equal(rc, 0);
	print_message("Total Disks = %d\n", ndisks);

	/**
	 * Get the Device info of all NVMe devices.
	 */
	D_ALLOC_ARRAY(devices, ndisks);
	rc = dmg_storage_device_list(dmg_config_file, NULL, devices);
	assert_rc_equal(rc, 0);
	for (i = 0; i < ndisks; i++) {
		print_message("Rank=%d UUID=" DF_UUIDF " state=%s host=%s\n",
			      devices[i].rank, DP_UUID(devices[i].device_id),
			      devices[i].state, devices[i].host);

		if (devices[i].rank == 0)
			faulty_disk_idx = i;
	}

	/**
	 * Set the object class and generate data on objects.
	 */
	if (arg->pool.pool_info.pi_nnodes < 2)
		obj_class = DAOS_OC_R1S_SPEC_RANK;
	else
		obj_class = DAOS_OC_R2S_SPEC_RANK;

	oid = daos_test_oid_gen(arg->coh, obj_class, 0, 0, arg->myrank);
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
	rc = daos_mgmt_get_bs_state(arg->group,
				    devices[faulty_disk_idx].device_id,
				    &blobstore_state, NULL /*ev*/);
	assert_rc_equal(rc, 0);

	rc = verify_blobstore_state(blobstore_state, "normal");
	assert_int_equal(rc, 0);
	print_message("Blobstore is in NORMAL state\n");

	/**
	 * Manually set first device returned to faulty via
	 * 'dmg storage set nvme-faulty'.
	 */
	print_message("NVMe with UUID=" DF_UUIDF " on host=%s\" set to Faulty\n",
		      DP_UUID(devices[faulty_disk_idx].device_id),
		      devices[faulty_disk_idx].host);
	rc = dmg_storage_set_nvme_fault(dmg_config_file,
					devices[faulty_disk_idx].host,
					devices[faulty_disk_idx].device_id,
					1);
	assert_rc_equal(rc, 0);

	/**
	 *  Continue to check blobstore state until "OUT" state is returned
	 *  or max test retry count is hit (5 min).
	 */
	rc = wait_and_verify_blobstore_state(devices[faulty_disk_idx].device_id,
					     /*expected state*/"out",
					     arg->group);
	assert_rc_equal(rc, 0);

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

	FAULT_INJECTION_REQUIRED();

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
	oid = daos_test_oid_gen(arg->coh, DAOS_OC_R1S_SPEC_RANK, 0, 0,
				arg->myrank);
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
	assert_rc_equal(rc, 0);
	print_message("Total Disks = %d\n", ndisks);

	/*
	 * Get the Device info of all NVMe devices
	 */
	D_ALLOC_ARRAY(devices, ndisks);
	rc = dmg_storage_device_list(dmg_config_file, NULL, devices);
	assert_rc_equal(rc, 0);

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
	D_ASSERT(control_log_file);
	D_ALLOC(server_config_file, DAOS_SERVER_CONF_LENGTH);
	D_ASSERT(server_config_file);
	rc = get_server_config(devices[rank_pos].host, server_config_file);
	assert_rc_equal(rc, 0);
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
	D_STRNDUP_S(write_errors, "bio_write_errs");
	rc = dmg_storage_query_device_health(dmg_config_file,
					     devices[rank_pos].host,
					     write_errors,
		devices[rank_pos].device_id);
	assert_rc_equal(rc, 0);
	print_message("Initial write_errors = %s\n", write_errors);

	/*
	 * Get the Initial read error
	 */
	D_STRNDUP_S(read_errors, "bio_read_errs");
	rc = dmg_storage_query_device_health(dmg_config_file,
					     devices[rank_pos].host,
					     read_errors,
		devices[rank_pos].device_id);
	assert_rc_equal(rc, 0);
	print_message("Initial read_errors = %s\n", read_errors);

	/*
	 * Inject BIO Read Errors on Rank1 device
	 */
	print_message("----Inject BIO Read Error----\n");
	set_fail_loc(arg, rank, 0, DAOS_NVME_READ_ERR | DAOS_FAIL_ONCE);

	/*
	 * Read the data which will induce the READ Error and expected to fail
	 * with DER_NVME_IO Error (no replica for retry).
	 */
	arg->expect_result = -DER_NVME_IO;
	lookup_single_with_rxnr(dkey, akey, /*idx*/0, fbuf,
				OW_IOD_SIZE, size, DAOS_TX_NONE, &req);

	/*
	 * Inject BIO Write Errors on Rank1 device
	 */
	print_message("----Inject BIO Write Error----\n");
	set_fail_loc(arg, rank, 0, DAOS_NVME_WRITE_ERR | DAOS_FAIL_ONCE);

	/*
	 * Insert the 4K record again which will induce WRITE Error and
	 * expected write succeeded (on retry).
	 */
	rx_nr = size / OW_IOD_SIZE;
	arg->expect_result = -DER_SUCCESS;
	insert_single_with_rxnr(dkey, akey, /*idx*/0, ow_buf, OW_IOD_SIZE,
				rx_nr, DAOS_TX_NONE, &req);

	/*
	 * Get the write error count after Injecting BIO write error.
	 * Verify the recent write err count is > the initial err count.
	 */
	arg->expect_result = 0;
	D_STRNDUP_S(check_errors, "bio_write_errs");
	rc = dmg_storage_query_device_health(dmg_config_file,
					     devices[rank_pos].host,
					     check_errors,
		devices[rank_pos].device_id);
	assert_rc_equal(rc, 0);
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
	assert_rc_equal(rc, 0);
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
			assert_rc_equal(rc, 0);
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
	 online_fault_recovery, NULL, test_case_teardown},
	{"NVMe Recovery 2: Verify device states after NVMe set to Faulty",
	 nvme_test_verify_device_stats, NULL, test_case_teardown},
	{"NVMe Recovery 3: Verify blobstore state NORMAL->OUT transition",
	 nvme_test_get_blobstore_state, NULL, test_case_teardown},
	{"NVMe Recovery 4: Verify NVMe IO error and notification",
	 nvme_test_simulate_IO_error, NULL, test_case_teardown},
	{"NVMe Recovery 5: Offline faulty reaction",
	 offline_fault_recovery, NULL, test_case_teardown},
	{"NVMe Recovery 6: Mixed type pool faulty reaction",
	 offline_and_online_fault_recovery, NULL, test_case_teardown},
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

	par_barrier(PAR_COMM_WORLD);
	if (sub_tests_size == 0) {
		sub_tests_size = ARRAY_SIZE(nvme_recov_tests);
		sub_tests = NULL;
	}

	rc = run_daos_sub_tests("DAOS_Nvme_Recov", nvme_recov_tests,
				ARRAY_SIZE(nvme_recov_tests), sub_tests,
				sub_tests_size, nvme_recov_test_setup,
				test_teardown);

	par_barrier(PAR_COMM_WORLD);

	return rc;
}
