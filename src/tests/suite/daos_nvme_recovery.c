/**
 * (C) Copyright 2019-2022 Intel Corporation.
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
	assert_rc_equal(rc, 0);

	return ps->ps_free_min[DAOS_MEDIA_NVME] != 0;
}

/**
 *  Compare the transport address to see if the device is a VMD device.
 *  A regular NVMe SSD will have a domain starting with "0000:", while
 *  a VMD device will have a traddr in BDF format (ex: "5d0505:01:00.0").
 */
static bool
is_vmd_enabled(const char *device_traddr)
	{
	int	rc;

	rc = strncmp(device_traddr, "\"0000:", 6);
	if (rc != 0)
		return true;

	return false;
}

/**
 * Online/Offline faulty reaction tests.
 *
 * Mode 0 = Offline
 * Mode 1 = Online
 * Mode 2 = Online and Offline
 */
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
	 * Mixed offline and online mode. Requires additional pool (offline
	 * by default).
	 */
	if (mode == 2) {
		char	   *env;
		int	    size_gb;
		daos_size_t scm_size = (daos_size_t)4 << 30; /*Default 4G*/
		daos_size_t nvme_size;

		/* Use the SCM size if set with environment */
		env = getenv("POOL_SCM_SIZE");
		if (env) {
			size_gb = atoi(env);
			if (size_gb != 0)
				scm_size = (daos_size_t)size_gb << 30;
		}

		/* NVMe size is 4x of SCM size */
		nvme_size = scm_size * 4;
		/* Create additional offline pool */
		print_message("Creating pool in offline mode\n");
		print_message("\tSize: SCM=%" PRId64 ", NVMe=%" PRId64 "\n",
			      scm_size, nvme_size);

		rc = dmg_pool_create(dmg_config_file, geteuid(), getegid(),
				     arg->group, NULL /* tgts */,
				     scm_size, nvme_size,
				     NULL /* prop */,
				     arg->pool.svc /* svc */,
				     offline_pool_uuid);
		assert_rc_equal(rc, 0);
	}

	/* Get the total number of NVMe devices from all the servers */
	rc = dmg_storage_device_list(dmg_config_file, &ndisks, NULL);
	assert_rc_equal(rc, 0);

	/* Get the device info of all NVMe devices */
	D_ALLOC_ARRAY(devices, ndisks);
	rc = dmg_storage_device_list(dmg_config_file, NULL, devices);
	assert_rc_equal(rc, 0);
	for (i = 0; i < ndisks; i++) {
		if (devices[i].rank != rank)
			continue;
		else
			faulty_disk_idx = i;

		print_message("UUID="DF_UUIDF" Traddr=%s State=%s LED=%s Rank=%d Host=%s\n",
			       DP_UUID(devices[i].device_id), devices[i].traddr,
			       devices[i].state, devices[i].led, devices[i].rank,
			       devices[i].host);
		print_message("\tVOS tgts= ");
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

	/* Query test args to get total pool target count per node */
	assert_true(arg->srv_ntgts > arg->srv_nnodes);
	per_node_tgt_cnt = arg->srv_ntgts/arg->srv_nnodes;

	/**
	 * Verify initial states for all pool targets are UPIN by querying
	 * the pool target info.
	 */
	for (i = 0; i < per_node_tgt_cnt; i++) {
		rc = daos_pool_query_target(arg->pool.poh, i /*tgt*/,
					    rank, &tgt_info, NULL /*ev*/);
		assert_rc_equal(rc, 0);
		rc = strcmp(daos_target_state_enum_to_str(tgt_info.ta_state),
			    "UPIN");
		assert_int_equal(rc, 0);
	}
	print_message("All targets are in UPIN\n");

	/* Offline mode */
	if (mode == 0) {
		print_message("Disconnecting the pool for offline failure\n");
		rc = daos_cont_close(arg->coh, NULL);
		assert_rc_equal(rc, 0);
		rc = daos_pool_disconnect(arg->pool.poh, NULL);
		assert_rc_equal(rc, 0);
	}

	/* Inject error on random target index to simulate faulty SSD*/
	srand(time(NULL));
	fail_loc_tgt = rand() % per_node_tgt_cnt;
	print_message("Inject error on target %"PRIu64"\n", fail_loc_tgt);
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

		/* Connect the pool for query check */
		print_message("Connecting the pool to get the pool query\n");
		rc = daos_pool_connect(arg->pool.pool_str, arg->group,
				       DAOS_PC_RW, &arg->pool.poh,
				       &arg->pool.pool_info, NULL /* ev */);
		assert_rc_equal(rc, 0);
		/* Set container handle as invalid so it does not close again */
		arg->coh = DAOS_HDL_INVAL;
	}

	/**
	 * Verify that DAOS_NVME_FAULTY reaction got triggered. Target should be
	 * in the DOWN state to trigger rebuild (or DOWNOUT if rebuild already
	 * completed).
	 */
	print_message("Waiting for faulty reaction to trigger...\n");
	rc = wait_and_verify_pool_tgt_state(arg->pool.poh, fail_loc_tgt,
						    rank, "DOWN|DOWNOUT");
	assert_rc_equal(rc, 0);
	/* Need to reset lock when using DAOS_FAIL_ALWAYS flag */
	reset_fail_loc(arg);

	/* Look up all targets currently mapped to the faulty device */
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

	print_message("Waiting for rebuild to finish...\n");
	if (arg->myrank == 0)
		test_rebuild_wait(&arg, 1);
	MPI_Barrier(MPI_COMM_WORLD);

	/* Verify all mapped device targets are in DOWNOUT state */
	print_message("Waiting for faulty reaction to complete...\n");
	for (i = 0; i < n_tgtidx; i++) {
		rc = wait_and_verify_pool_tgt_state(arg->pool.poh, tgtidx[i],
						    rank, "DOWNOUT");
		assert_rc_equal(rc, 0);
	}
	print_message("All targets are in DOWNOUT\n");

	/* Print the final pool target states */
	for (i = 0; i < per_node_tgt_cnt; i++) {
		rc = daos_pool_query_target(arg->pool.poh, i /*tgt*/,
					    rank, &tgt_info, NULL /*ev*/);
		assert_rc_equal(rc, 0);
		print_message("Pool target:%d, state:%s\n", i,
			      daos_target_state_enum_to_str(tgt_info.ta_state));
	}

	D_FREE(devices);
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

/**
 * Verify device state (via dmg tool) and blobstore state (via server log)
 * transitions after an NVMe SSD is manually evicted. Device remains evicted
 * at the end of the test.
 * If the evicted device is a VMD device, locate the evicted device by verifying
 * the LED state is set to fault/on. Manually reset the LED to off at the end.
 */
static void
nvme_verify_states_faulty(void **state)
{
	test_arg_t	*arg = *state;
	device_list	*devices = NULL;
	int		ndisks;
	int		rc, i;
	char		*server_config_file;
	char		*log_file;
	int		rank_pos = 0;
	char		led_state[16];

	if (!is_nvme_enabled(arg)) {
		print_message("NVMe isn't enabled.\n");
		skip();
	}

	/* Get the total number of NVMe devices from all the servers */
	rc = dmg_storage_device_list(dmg_config_file, &ndisks, NULL);
	assert_rc_equal(rc, 0);
	print_message("Device List: (total=%d)\n", ndisks);

	/* Get the device info of all NVMe devices */
	D_ALLOC_ARRAY(devices, ndisks);
	rc = dmg_storage_device_list(dmg_config_file, NULL, devices);
	assert_rc_equal(rc, 0);
	for (i = 0; i < ndisks; i++)
		print_message("UUID="DF_UUIDF" Traddr=%s State=%s LED=%s Rank=%d Host=%s\n",
			       DP_UUID(devices[i].device_id), devices[i].traddr,
			       devices[i].state, devices[i].led, devices[i].rank,
			       devices[i].host);

	if (ndisks <= 1) {
		print_message("Need Minimum 2 disks for test\n");
		D_FREE(devices);
		skip();
	}

	/* Get the rank0 position from array devices */
	for (i = 0; i < ndisks; i++) {
		if (devices[i].rank == 0)
			rank_pos = i;
	}

	/**
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

	get_log_file(devices[rank_pos].host, server_config_file,
		     "control_log_file", log_file);
	rc = verify_server_log_mask(devices[rank_pos].host,
				    server_config_file, "DEBUG");
	if (rc) {
		print_message("log mask != DEBUG in %s.\n",
			      server_config_file);
		D_FREE(server_config_file);
		D_FREE(devices);
		D_FREE(log_file);
		skip();
	}

	/* Set single device for rank0 to faulty */
	print_message("Setting SSD to FAULTY (UUID="DF_UUIDF", host=%s)\n",
		      DP_UUID(devices[rank_pos].device_id),
		      devices[rank_pos].host);
	rc = dmg_storage_set_nvme_fault(dmg_config_file, devices[rank_pos].host,
					devices[rank_pos].device_id, 1);
	assert_rc_equal(rc, 0);
	sleep(60);

	/**
	 * Verify rank0 device state change from NORMAL to FAULTY.
	 * Verify "FAULTY -> TEARDOWN" and "TEARDOWN -> OUT" device states found
	 * in server log.
	 */
	rc = dmg_storage_device_list(dmg_config_file, NULL, devices);
	assert_rc_equal(rc, 0);

	/* Get the rank0 position from array devices */
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

	/**
	 * If the SSD  evicted is a VMD device, verify LED state is set to
	 * fault state (constant on) and then reset to off.
	 */
	if (!is_vmd_enabled(devices[rank_pos].traddr))
		goto teardown;
	print_message("Evicted a VMD device - verifying LED states\n");

	rc = dmg_storage_ledmanage_getstate(dmg_config_file,
					    devices[rank_pos].host,
					    devices[rank_pos].device_id,
					    led_state);
	assert_rc_equal(rc, 0);
	rc = strcasecmp(led_state, "\"ON\"");
	if (rc != 0) {
		print_message("LED not set to ON/FAULT state\n");
		assert_rc_equal(rc, 0);
	}
	print_message("LED state is %s\n", led_state);

	/* Reset the LED */
	rc = dmg_storage_ledmanage_reset(dmg_config_file,
					 devices[rank_pos].host,
					 devices[rank_pos].device_id);
	assert_rc_equal(rc, 0);
	rc = dmg_storage_ledmanage_getstate(dmg_config_file,
					    devices[rank_pos].host,
					    devices[rank_pos].device_id,
					    led_state);
	assert_rc_equal(rc, 0);
	rc = strcasecmp(led_state, "\"OFF\"");
	if (rc != 0) {
		print_message("LED not reset to OFF state\n");
		assert_rc_equal(rc, 0);
	}
	print_message("LED successfully reset to %s\n", led_state);

teardown:
	/* Tear down */
	D_FREE(server_config_file);
	D_FREE(log_file);
	D_FREE(devices);
}

/**
 * Verify device state (via dmg tool) and blobstore state (via
 * daos_mgmt_get_bs_state() C API) transitions after an NVMe SSD is
 * manually evicted.
 * Add the evicted device back into use by replacing the device with
 * itself (no target reintegration will occur). Verify device state
 * after reintegration.
 */
static void
nvme_verify_states_faulty_reint(void **state)
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
	char		 led_state[16];
	int		 rc;

	if (!is_nvme_enabled(arg)) {
		print_message("NVMe isn't enabled.\n");
		skip();
	}

	/* Get the total number of NVMe devices from all the servers */
	rc = dmg_storage_device_list(dmg_config_file, &ndisks, NULL);
	assert_rc_equal(rc, 0);
	print_message("Device List: (total=%d)\n", ndisks);

	/* Get the device info of all NVMe devices */
	D_ALLOC_ARRAY(devices, ndisks);
	rc = dmg_storage_device_list(dmg_config_file, NULL, devices);
	assert_rc_equal(rc, 0);
	for (i = 0; i < ndisks; i++) {
		print_message("UUID="DF_UUIDF" Traddr=%s State=%s LED=%s Rank=%d Host=%s\n",
			       DP_UUID(devices[i].device_id), devices[i].traddr,
			       devices[i].state, devices[i].led, devices[i].rank,
			       devices[i].host);

		if (devices[i].rank == 0)
			faulty_disk_idx = i;
	}

	/* Set the object class and generate data on objects */
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

	rc = verify_blobstore_state(blobstore_state, "NORMAL");
	assert_int_equal(rc, 0);

	/**
	 * Manually set first device returned to faulty via
	 * 'dmg storage set nvme-faulty'.
	 */
	print_message("Setting SSD to FAULTY (UUID="DF_UUIDF", host=%s)\n",
		      DP_UUID(devices[faulty_disk_idx].device_id),
		      devices[faulty_disk_idx].host);
	rc = dmg_storage_set_nvme_fault(dmg_config_file,
					devices[faulty_disk_idx].host,
					devices[faulty_disk_idx].device_id, 1);
	assert_rc_equal(rc, 0);

	/**
	 *  Continue to check blobstore state until "OUT" state is returned
	 *  or max test retry count is hit (5 min).
	 */
	rc = wait_and_verify_blobstore_state(devices[faulty_disk_idx].device_id,
					     /*expected state*/"OUT",
					     arg->group);
	assert_rc_equal(rc, 0);
	print_message("Blobstore is in OUT state\n");

	rc = dmg_storage_device_list(dmg_config_file, NULL, devices);
	assert_rc_equal(rc, 0);
	print_message("UUID="DF_UUIDF" Traddr=%s State=%s LED=%s\n",
		      DP_UUID(devices[faulty_disk_idx].device_id),
		      devices[faulty_disk_idx].traddr,
		      devices[faulty_disk_idx].state,
		      devices[faulty_disk_idx].led);

	/**
	 * If the SSD evicted is a VMD device, verify LED state is set to
	 * fault state.
	 */
	if (!is_vmd_enabled(devices[faulty_disk_idx].traddr))
		goto replace;
	print_message("Evicted a VMD device - verifying LED states\n");

	rc = dmg_storage_ledmanage_getstate(dmg_config_file,
					    devices[faulty_disk_idx].host,
					    devices[faulty_disk_idx].device_id,
					    led_state);
	assert_rc_equal(rc, 0);
	rc = strcmp(led_state, "\"ON\"");
	if (rc != 0) {
		print_message("LED not set to ON/FAULT state\n");
		assert_rc_equal(rc, 0);
	}
	print_message("LED state is %s\n", led_state);

replace:
	/**
	 * Add the evicted device back into use by replacing the device with
	 * itself via 'dmg storage replace nvme'.
	 */
	print_message("Bringing SSD back online (UUID="DF_UUIDF", host=%s)\n",
		      DP_UUID(devices[faulty_disk_idx].device_id),
		      devices[faulty_disk_idx].host);
	rc = dmg_storage_replace_device(dmg_config_file,
					devices[faulty_disk_idx].host,
					devices[faulty_disk_idx].device_id,
					devices[faulty_disk_idx].device_id);
	assert_rc_equal(rc, 0);

	/**
	 * Continue to check blobstore state until "NORMAL" state is resumed
	 * or max test retry count is hit (5 min).
	 */
	rc = wait_and_verify_blobstore_state(devices[faulty_disk_idx].device_id,
					     /*expected state*/"NORMAL",
					     arg->group);
	assert_rc_equal(rc, 0);
	print_message("Blobstore is in NORMAL state\n");

	rc = dmg_storage_device_list(dmg_config_file, NULL, devices);
	assert_rc_equal(rc, 0);
	print_message("UUID="DF_UUIDF" Traddr=%s State=%s LED=%s\n",
		      DP_UUID(devices[faulty_disk_idx].device_id),
		      devices[faulty_disk_idx].traddr,
		      devices[faulty_disk_idx].state,
		      devices[faulty_disk_idx].led);

	/**
	 * If the SSD evicted is a VMD device, verify LED state is reset to
	 * off state after device replacement.
	 */
	if (!is_vmd_enabled(devices[faulty_disk_idx].traddr))
		goto teardown;
	print_message("Replaced a VMD device - verifying LED states\n");

	rc = dmg_storage_ledmanage_getstate(dmg_config_file,
					    devices[faulty_disk_idx].host,
					    devices[faulty_disk_idx].device_id,
					    led_state);
	assert_rc_equal(rc, 0);
	rc = strcasecmp(led_state, "\"OFF\"");
	if (rc != 0) {
		print_message("LED not reset to OFF state\n");
		assert_rc_equal(rc, 0);
	}
	print_message("LED successfully reset to %s\n", led_state);

teardown:
	D_FREE(devices);
}

/**
 * Simulate both an NVMe I/O read and write error. Check error counters in BIO
 * health stats to verify R/W error counts, and also verify I/O error
 * notification on the console output.
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

	/* Allocate and set write buffer with data */
	D_ALLOC(ow_buf, size);
	assert_non_null(ow_buf);
	dts_buf_render(ow_buf, size);
	/* Allocate and set fetch buffer */
	D_ALLOC(fbuf, size);
	assert_non_null(fbuf);
	memset(fbuf, 0, size);

	/* Prepare records */
	oid = daos_test_oid_gen(arg->coh, DAOS_OC_R1S_SPEC_RANK, 0, 0,
				arg->myrank);
	oid = dts_oid_set_rank(oid, rank);
	ioreq_init(&req, arg->coh, oid, DAOS_IOD_ARRAY, arg);

	/* Insert the initial 4K record which will go through NVMe */
	print_message("Insert single record\n");
	rx_nr = size / OW_IOD_SIZE;
	insert_single_with_rxnr(dkey, akey, 0 /*idx*/, ow_buf, OW_IOD_SIZE,
				rx_nr, DAOS_TX_NONE, &req);

	/* Get the total number of NVMe devices from all the servers */
	rc = dmg_storage_device_list(dmg_config_file, &ndisks, NULL);
	assert_rc_equal(rc, 0);

	/* Get the device info of all NVMe devices */
	D_ALLOC_ARRAY(devices, ndisks);
	rc = dmg_storage_device_list(dmg_config_file, NULL, devices);
	assert_rc_equal(rc, 0);

	/* Get the rank1 position from array devices */
	for (i = 0; i < ndisks; i++) {
		if (devices[i].rank == 1) {
			rank_pos = i;
			break;
		}
	}

	/* Get the DAOS server file and control log file */
	D_ALLOC(control_log_file, 1024);
	D_ALLOC(server_config_file, DAOS_SERVER_CONF_LENGTH);
	rc = get_server_config(devices[rank_pos].host, server_config_file);
	assert_rc_equal(rc, 0);
	get_log_file(devices[rank_pos].host, server_config_file,
		     "control_log_file", control_log_file);
	D_FREE(server_config_file);

	/* Get the initial write error */
	write_errors = strdup("bio_write_errs");
	rc = dmg_storage_query_device_health(dmg_config_file,
					     devices[rank_pos].host,
					     write_errors,
					     devices[rank_pos].device_id);
	assert_rc_equal(rc, 0);
	print_message("Initial write errors = %s\n", write_errors);

	/* Get the initial read error */
	read_errors = strdup("bio_read_errs");
	rc = dmg_storage_query_device_health(dmg_config_file,
					     devices[rank_pos].host,
					     read_errors,
					     devices[rank_pos].device_id);
	assert_rc_equal(rc, 0);
	print_message("Initial read errors = %s\n", read_errors);

	/* Inject BIO read errors on rank1 device */
	print_message("----Inject BIO Read Error----\n");
	set_fail_loc(arg, rank, 0, DAOS_NVME_READ_ERR | DAOS_FAIL_ONCE);

	/**
	 * Read the data which will induce the read error. Expected to fail
	 * with DER_IO error.
	 */
	arg->expect_result = -DER_IO;
	lookup_single_with_rxnr(dkey, akey, 0/*idx*/, fbuf, OW_IOD_SIZE, size,
				DAOS_TX_NONE, &req);

	/* Inject BIO write errors on rank1 device */
	print_message("----Inject BIO Write Error----\n");
	set_fail_loc(arg, rank, 0, DAOS_NVME_WRITE_ERR | DAOS_FAIL_ONCE);

	/**
	 * Insert the 4K record again which will induce a write error. Expected
	 * to fail with DER_IO error.
	 */
	rx_nr = size / OW_IOD_SIZE;
	insert_single_with_rxnr(dkey, akey, 0/*idx*/, ow_buf, OW_IOD_SIZE,
				rx_nr, DAOS_TX_NONE, &req);

	/**
	 * Get the write error count after injecting BIO write error.
	 * Verify the recent write err count is > the initial error count.
	 */
	arg->expect_result = 0;
	check_errors = strdup("bio_write_errs");
	rc = dmg_storage_query_device_health(dmg_config_file,
					     devices[rank_pos].host,
					     check_errors,
					     devices[rank_pos].device_id);
	assert_rc_equal(rc, 0);
	print_message("Final write errors = %s\n", check_errors);
	assert_true(atoi(check_errors) == atoi(write_errors) + 1);

	/**
	 * Get the read error count after injecting BIO read error.
	 * Verify the recent read err count is > the initial error count.
	 */
	strcpy(check_errors, "bio_read_errs");
	rc = dmg_storage_query_device_health(dmg_config_file,
					     devices[rank_pos].host,
					     check_errors,
					     devices[rank_pos].device_id);
	assert_rc_equal(rc, 0);
	print_message("Final read errors = %s\n", check_errors);
	assert_true(atoi(check_errors) == atoi(read_errors) + 1);

	/* Verify errors in control log */
	char control_err[][50] = {
		"detected blob I/O error! writeErr:true",
		"detected blob I/O error! readErr:true"};
	for (i = 0; i < 2 ; i++) {
		rc = verify_state_in_log(devices[rank_pos].host,
					 control_log_file, control_err[i]);
		if (rc != 0) {
			print_message("%s not found in log %s\n",
				      control_err[i], control_log_file);
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

/**
 * Identify all VMD devices used in the test by setting the status LED
 * on the SSDs to an "identify" state (fast blink). Reset all LEDs back
 * to off at the end of the test. Skip test if no VMD devices are present.
 */
static void
vmd_identify_led(void **state)
{
	test_arg_t	*arg = *state;
	device_list	*devices = NULL;
	int		 ndisks;
	char		 led_state[16];
	int		 i;
	int		 rc;

	if (!is_nvme_enabled(arg)) {
		print_message("NVMe isn't enabled.\n");
		skip();
	}

	/* Get the total number of NVMe devices from all the servers */
	rc = dmg_storage_device_list(dmg_config_file, &ndisks, NULL);
	assert_rc_equal(rc, 0);
	print_message("Device List: (total=%d)\n", ndisks);

	/* Get the device info of all NVMe devices */
	D_ALLOC_ARRAY(devices, ndisks);
	rc = dmg_storage_device_list(dmg_config_file, NULL, devices);
	assert_rc_equal(rc, 0);
	for (i = 0; i < ndisks; i++) {
		print_message("UUID="DF_UUIDF" Traddr=%s State=%s LED=%s Rank=%d Host=%s\n",
			       DP_UUID(devices[i].device_id), devices[i].traddr,
			       devices[i].state, devices[i].led, devices[i].rank,
			       devices[i].host);
	}

	/* Identify all VMD devices. Skip if the NVMe SSD is not a VMD device */
	for (i = 0; i < ndisks; i++) {
		if (!is_vmd_enabled(devices[i].traddr))
			continue;
		/**
		 * Manually set VMD LED to identify state via
		 * 'dmg storage identify vmd'.
		 */
		print_message("Identifying VMD device %s\n", devices[i].traddr);
		rc = dmg_storage_identify_vmd(dmg_config_file, devices[i].host,
					      devices[i].device_id);
		assert_rc_equal(rc, 0);
		rc = dmg_storage_ledmanage_getstate(dmg_config_file,
						    devices[i].host,
						    devices[i].device_id,
						    led_state);
		assert_rc_equal(rc, 0);
		rc = strcasecmp(led_state, "\"QUICK-BLINK\"");
		if (rc != 0) {
			print_message("LED not set to QUICK-BLINK state\n");
			assert_rc_equal(rc, 0);
		}
		print_message("LED state is %s\n", led_state);
	}

	/* Default duration of LED event is 60 seconds before reset */
	sleep(60);

	/* Verify all LED are reset */
	for (i = 0; i < ndisks; i++) {
		if (!is_vmd_enabled(devices[i].traddr))
			continue;
		rc = dmg_storage_ledmanage_getstate(dmg_config_file,
						    devices[i].host,
						    devices[i].device_id,
						    led_state);
		assert_rc_equal(rc, 0);
		rc = strcasecmp(led_state, "\"OFF\"");
		if (rc != 0) {
			print_message("LED not reset to OFF state\n");
			assert_rc_equal(rc, 0);
		}
		print_message("LED successfully reset to %s\n", led_state);
	}

	D_FREE(devices);
}

static const struct CMUnitTest nvme_recov_tests[] = {
	{"NVMe Recovery 1: Online faulty reaction",
	 online_fault_recovery, NULL, test_case_teardown},
	{"NVMe Recovery 2: Offline faulty reaction",
	 offline_fault_recovery, NULL, test_case_teardown},
	{"NVMe Recovery 3: Mixed type pool faulty reaction",
	 offline_and_online_fault_recovery, NULL, test_case_teardown},
	{"NVMe Recovery 4: Verify device states via log after NVMe SSD eviction",
	 nvme_verify_states_faulty, NULL, test_case_teardown},
	{"NVMe Recovery 5: Verify device states after NVMe SSD eviction/reint",
	 nvme_verify_states_faulty_reint, NULL, test_case_teardown},
	{"NVMe Recovery 6: Verify NVMe IO error and notification",
	 nvme_test_simulate_IO_error, NULL, test_case_teardown},
	{"NVMe Recovery 7: Identify an SSD via LED (VMD only)",
	 vmd_identify_led, NULL, test_case_teardown},
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

	rc = run_daos_sub_tests("DAOS_Nvme_Recov", nvme_recov_tests,
				ARRAY_SIZE(nvme_recov_tests), sub_tests,
				sub_tests_size, nvme_recov_test_setup,
				test_teardown);

	MPI_Barrier(MPI_COMM_WORLD);

	return rc;
}
