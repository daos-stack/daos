/**
 * (C) Copyright 2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos, basic testing for catastrophic recovery.
 */
#define D_LOGFAC	DD_FAC(tests)
#include "daos_test.h"

#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <daos_mgmt.h>

/*
 * Will enable accurate query result verification after DAOS-13520 resolved.
 * #define CR_ACCURATE_QUERY_RESULT	1
 */

/* Start pool service may take sometime, let's wait for at most CR_WAIT_MAX * 2 seconds. */
#define CR_WAIT_MAX	(45)
/* 256MB for CR pool size. */
#define CR_POOL_SIZE	(1 << 28)

struct test_cont {
	uuid_t	uuid;
	char	label[DAOS_PROP_LABEL_MAX_LEN];
};

/* Instance Status */

static inline bool
cr_ins_status_init(const char *status)
{
	return status != NULL && strcmp(status, "INIT") == 0;
}

static inline bool
cr_ins_status_running(const char *status)
{
	return status != NULL && strcmp(status, "RUNNING") == 0;
}

static inline bool
cr_ins_status_completed(const char *status)
{
	return status != NULL && strcmp(status, "COMPLETED") == 0;
}

static inline bool
cr_ins_status_stopped(const char *status)
{
	return status != NULL && strcmp(status, "STOPPED") == 0;
}

static inline bool
cr_ins_status_failed(const char *status)
{
	return status != NULL && strcmp(status, "FAILED") == 0;
}

static inline bool
cr_ins_status_paused(const char *status)
{
	return status != NULL && strcmp(status, "PAUSED") == 0;
}

static inline bool
cr_ins_status_implicated(const char *status)
{
	return status != NULL && strcmp(status, "IMPLICATED") == 0;
}

/* Instance Scan Phase */

static inline bool
cr_ins_phase_is_prepare(const char *phase)
{
	return phase != NULL && strcmp(phase, "PREPARE") == 0;
}

static inline bool
cr_ins_phase_is_done(const char *phase)
{
	return phase != NULL && strcmp(phase, "DONE") == 0;
}

/* Pool Status */

static inline bool
cr_pool_status_unchecked(const char *status)
{
	return status != NULL && strcmp(status, "CPS_UNCHECKED") == 0;
}

static inline bool
cr_pool_status_checking(const char *status)
{
	return status != NULL && strcmp(status, "CPS_CHECKING") == 0;
}

static inline bool
cr_pool_status_checked(const char *status)
{
	return status != NULL && strcmp(status, "CPS_CHECKED") == 0;
}

static inline bool
cr_pool_status_failed(const char *status)
{
	return status != NULL && strcmp(status, "CPS_FAILED") == 0;
}

static inline bool
cr_pool_status_paused(const char *status)
{
	return status != NULL && strcmp(status, "CPS_PAUSED") == 0;
}

static inline bool
cr_pool_status_pending(const char *status)
{
	return status != NULL && strcmp(status, "CPS_PENDING") == 0;
}

static inline bool
cr_pool_status_stopped(const char *status)
{
	return status != NULL && strcmp(status, "CPS_STOPPED") == 0;
}

static inline bool
cr_pool_status_implicated(const char *status)
{
	return status != NULL && strcmp(status, "CPS_IMPLICATED") == 0;
}

/* Pool Scan Phase */

static inline bool
cr_pool_phase_is_prepare(const char *phase)
{
	return phase != NULL && strcmp(phase, "CSP_PREPARE") == 0;
}

static inline bool
cr_pool_phase_is_done(const char *phase)
{
	return phase != NULL && strcmp(phase, "CSP_DONE") == 0;
}

static inline void
cr_dump_pools(uint32_t pool_nr, uuid_t uuids[])
{
	int	i;

	if (pool_nr > 0) {
		print_message("For the following %d pool(s):\n", pool_nr);
		for (i = 0; i < pool_nr; i++)
			print_message(DF_UUIDF "\n", DP_UUID(uuids[i]));
	}
}

/* dmg command */

static inline int
cr_debug_set_params_internal(test_arg_t *arg, uint64_t fail_loc, bool nowait)
{
	int	rc;
	int	i = 0;

	/* The system maybe just started, wait for a while for primary group initialization. */
	if (fail_loc != 0 && !nowait)
		sleep(5);

	for (i = 0; i < 10; i++) {
		rc = daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_LOC, fail_loc, 0, NULL);
		if (rc == 0 || rc != -DER_TIMEDOUT || nowait)
			break;

		sleep(2);
	}

	print_message("CR: set fail_loc as " DF_X64 ": " DF_RC "\n", fail_loc, DP_RC(rc));

	return rc;
}

static inline int
cr_debug_set_params(test_arg_t *arg, uint64_t fail_loc)
{
	return cr_debug_set_params_internal(arg, fail_loc, false);
}

static inline int
cr_debug_set_params_nowait(test_arg_t *arg, uint64_t fail_loc)
{
	return cr_debug_set_params_internal(arg, fail_loc, true);
}

static inline int
cr_fault_inject(uuid_t uuid, bool mgmt, const char *fault)
{
	int	rc;

	print_message("CR: injecting fault %s for pool " DF_UUID "\n", fault, DP_UUID(uuid));
	rc = dmg_fault_inject(dmg_config_file, uuid, mgmt, fault);
	if (rc != 0)
		print_message("CR: pool " DF_UUID " inject fault %s failed: "DF_RC"\n",
			       DP_UUID(uuid), fault, DP_RC(rc));

	return rc;
}

static inline int
cr_mode_switch(bool enable)
{
	print_message("CR: %s check mode\n", enable ? "enable" : "disable");
	return dmg_check_switch(dmg_config_file, enable);
}

static inline int
cr_system_start(void)
{
	print_message("CR: starting system ...\n");
	return dmg_system_start_rank(dmg_config_file, CRT_NO_RANK);
}

static inline int
cr_system_stop(bool force)
{
	print_message("CR: stopping system with %s ...\n", force ? "force" : "non-force");
	return dmg_system_stop_rank(dmg_config_file, CRT_NO_RANK, force);
}

static inline int
cr_rank_reint(uint32_t rank, bool start)
{
	int	rc;

	print_message("CR: reintegrating the rank %u ...\n", rank);
	rc = dmg_system_reint_rank(dmg_config_file, rank);
	if (rc != 0)
		return rc;

	if (start) {
		print_message("CR: starting the rank %u ...\n", rank);
		rc = dmg_system_start_rank(dmg_config_file, rank);
	}

	return rc;
}

static inline int
cr_rank_exclude(test_arg_t *arg, struct test_pool *pool, int *rank, bool wait)
{
	int	count;
	int	rc;
	int	i;
	int	j;

	D_ASSERT(pool->svc != NULL);

	/*
	 * The check leader (elected by control plane, usually on rank 0) and
	 * PS leader maybe on different ranks, do not exclude such two ranks.
	 */
	count = pool->svc->rl_nr + 2;
	if (!test_runable(arg, count)) {
		print_message("Need enough targets (%u/%u vs %d) for test, skip\n",
			      arg->srv_nnodes, arg->srv_ntgts, count);
		return 1;
	}

	for (i = 1, *rank = -1; i < count && *rank < 0; i++) {
		for (j = 0; j < pool->svc->rl_nr; j++) {
			if (pool->svc->rl_ranks[j] == i)
				break;
		}

		if (j >= pool->svc->rl_nr)
			*rank = i;
	}

	D_ASSERT(*rank >= 0);

	rc = cr_debug_set_params(arg, DAOS_CHK_ENGINE_DEATH | DAOS_FAIL_ALWAYS);
	if (rc != 0)
		return rc;

	print_message("CR: stopping the rank %d ...\n", *rank);
	rc = dmg_system_stop_rank(dmg_config_file, *rank, false);
	if (rc != 0)
		return rc;

	/* The *rank is stopped, that may cause set_params to timeout, do not wait. */
	cr_debug_set_params_nowait(arg, 0);

	print_message("CR: excluding the rank %d ...\n", *rank);
	rc = dmg_system_exclude_rank(dmg_config_file, *rank);
	if (rc == 0 && wait) {
		print_message("CR: sleep 30 seconds for the rank death event\n");
		sleep(30);
	}

	return rc;
}

static inline int
cr_check_start(uint32_t flags, uint32_t pool_nr, uuid_t uuids[], const char *policies)
{
	print_message("CR: starting checker with flags %x, policies %s ...\n", flags,
		      policies != NULL ? policies : "(null)");
	cr_dump_pools(pool_nr, uuids);

	return dmg_check_start(dmg_config_file, flags, pool_nr, uuids, policies);
}

static inline int
cr_check_stop(uint32_t pool_nr, uuid_t uuids[])
{
	print_message("CR: stopping checker ...\n");
	cr_dump_pools(pool_nr, uuids);
	return dmg_check_stop(dmg_config_file, pool_nr, uuids);
}

static inline int
cr_check_query(uint32_t pool_nr, uuid_t uuids[], struct daos_check_info *dci)
{
	print_message("CR: query checker ...\n");
	cr_dump_pools(pool_nr, uuids);
	return dmg_check_query(dmg_config_file, pool_nr, uuids, dci);
}

static inline int
cr_check_repair(uint64_t seq, uint32_t opt, bool for_all)
{
	print_message("CR: handle check interaction for seq %lu, option %u ...\n",
		      (unsigned long)seq, opt);
	return dmg_check_repair(dmg_config_file, seq, opt, for_all);
}

static inline int
cr_check_set_policy(uint32_t flags, const char *policies)
{
	print_message("CR: set checker policy with flags %x, policy %s ...\n",
		      flags, policies != NULL ? policies : "(null)");
	return dmg_check_set_policy(dmg_config_file, flags, policies);
}

static struct daos_check_report_info *
cr_locate_dcri(struct daos_check_info *dci, struct daos_check_report_info *base, uuid_t uuid)
{
	struct daos_check_report_info	*last = &dci->dci_reports[dci->dci_report_nr - 1];
	struct daos_check_report_info	*dcri = NULL;
	bool				 found = false;

	if (base != NULL)
		dcri = base + 1;
	else
		dcri = &dci->dci_reports[0];

	while (dcri <= last) {
		if (uuid_compare(dcri->dcri_uuid, uuid) == 0) {
			found = true;
			break;
		}

		dcri++;
	}

	D_ASSERTF(found, "Cannot found inconsistency report for "DF_UUIDF"\n", DP_UUID(uuid));

	return dcri;
}

static void
cr_dci_fini(struct daos_check_info *dci)
{
	int	i;

	D_FREE(dci->dci_status);
	D_FREE(dci->dci_phase);

	if (dci->dci_pools != NULL) {
		for (i = 0; i < dci->dci_pool_nr; i++) {
			D_FREE(dci->dci_pools[i].dcpi_status);
			D_FREE(dci->dci_pools[i].dcpi_phase);
		}

		D_FREE(dci->dci_pools);
	}

	D_FREE(dci->dci_reports);
}

static void
cr_cleanup(test_arg_t *arg, struct test_pool *pools, uint32_t nr)
{
	int	rc;
	int	i;

	for (i = 0; i < nr; i++) {
		d_rank_list_free(pools[i].svc);
		d_rank_list_free(pools[i].alive_svc);
		D_FREE(pools[i].label);

		if (uuid_is_null(pools[i].pool_uuid) || pools[i].destroyed)
			continue;

		if (daos_handle_is_valid(pools[i].poh)) {
			print_message("CR: disconnecting pool " DF_UUID "\n",
				      DP_UUID(pools[i].pool_uuid));
			/*
			 * The connection may have already been evicted by checker. So disconnect()
			 * may fail. It is not fatal as long as there is not corruption.
			 */
			daos_pool_disconnect(pools[i].poh, NULL);
		}

		rc = dmg_pool_destroy(dmg_config_file, pools[i].pool_uuid, arg->group, 1);
		if (rc != 0 && rc != -DER_NONEXIST && rc != -DER_MISC)
			print_message("CR: dmg_pool_destroy failed: "DF_RC"\n", DP_RC(rc));
	}
}

static void
cr_ins_wait(uint32_t pool_nr, uuid_t uuids[], struct daos_check_info *dci)
{
	int	rc;
	int	i;

	print_message("CR: waiting check instance ...\n");

	for (i = 0; i < CR_WAIT_MAX; i++) {
		cr_dci_fini(dci);

		rc = dmg_check_query(dmg_config_file, pool_nr, uuids, dci);
		assert_rc_equal(rc, 0);

		if (!cr_ins_status_init(dci->dci_status) && !cr_ins_status_running(dci->dci_status))
			break;

		sleep(2);
	}
}

static void
cr_pool_wait(uint32_t pool_nr, uuid_t uuids[], struct daos_check_info *dci)
{
	int	rc;
	int	i;

	print_message("CR: waiting check pool ...\n");
	cr_dump_pools(pool_nr, uuids);

	for (i = 0; i < CR_WAIT_MAX; i++) {
		cr_dci_fini(dci);

		rc = dmg_check_query(dmg_config_file, pool_nr, uuids, dci);
		assert_rc_equal(rc, 0);

		if (!cr_ins_status_init(dci->dci_status) && dci->dci_pools != NULL &&
		    !cr_pool_status_checking(dci->dci_pools[0].dcpi_status))
			break;

		sleep(2);
	}
}

static int
cr_ins_verify(struct daos_check_info *dci, uint32_t exp_status)
{
	print_message("CR: verify instance status, expected %u\n", exp_status);

	switch (exp_status) {
	case TCIS_INIT:
		if (!cr_ins_status_init(dci->dci_status)) {
			print_message("CR instance status %s is not init\n", dci->dci_status);
			return -DER_INVAL;
		}
		if (!cr_ins_phase_is_prepare(dci->dci_phase)) {
			print_message("CR instance phase %s is not prepare\n", dci->dci_phase);
			return -DER_INVAL;
		}
		break;
	case TCIS_RUNNING:
		if (!cr_ins_status_running(dci->dci_status)) {
			print_message("CR instance status %s is not running\n", dci->dci_status);
			return -DER_INVAL;
		}
		break;
	case TCIS_COMPLETED:
		if (!cr_ins_status_completed(dci->dci_status)) {
			print_message("CR instance status %s is not completed\n", dci->dci_status);
			return -DER_INVAL;
		}
		if (!cr_ins_phase_is_done(dci->dci_phase)) {
			print_message("CR instance phase %s is not done\n", dci->dci_phase);
			return -DER_INVAL;
		}
		break;
	case TCIS_STOPPED:
		if (!cr_ins_status_stopped(dci->dci_status)) {
			print_message("CR instance status %s is not stopped\n", dci->dci_status);
			return -DER_INVAL;
		}
		if (cr_ins_phase_is_done(dci->dci_phase)) {
			print_message("CR instance phase should not be done\n");
			return -DER_INVAL;
		}
		break;
	case TCIS_FAILED:
		if (!cr_ins_status_failed(dci->dci_status)) {
			print_message("CR instance status %s is not failed\n", dci->dci_status);
			return -DER_INVAL;
		}
		if (cr_ins_phase_is_done(dci->dci_phase)) {
			print_message("CR instance phase should not be done\n");
			return -DER_INVAL;
		}
		break;
	case TCIS_PAUSED:
		if (!cr_ins_status_paused(dci->dci_status)) {
			print_message("CR instance status %s is not paused\n", dci->dci_status);
			return -DER_INVAL;
		}
		if (cr_ins_phase_is_done(dci->dci_phase)) {
			print_message("CR instance phase should not be done\n");
			return -DER_INVAL;
		}
		break;
	case TCIS_IMPLICATED:
		if (!cr_ins_status_implicated(dci->dci_status)) {
			print_message("CR instance status %s is not implicated\n", dci->dci_status);
			return -DER_INVAL;
		}
		if (cr_ins_phase_is_done(dci->dci_phase)) {
			print_message("CR instance phase should not be done\n");
			return -DER_INVAL;
		}
		break;
	default:
		print_message("CR: invalid expected instance status %d\n", exp_status);
		break;
	}

	return 0;
}

static int
cr_pool_verify(struct daos_check_info *dci, uuid_t uuid, uint32_t exp_status,
	       uint32_t inconsistency_nr, uint32_t *classes, uint32_t *actions, int *exp_results)
{
	struct daos_check_pool_info	*dcpi;
	struct daos_check_report_info	*dcri;
	int				 result;
	int				 i;
	int				 j;

	print_message("CR: verify pool " DF_UUID " status, expected %u, inconsistency_nr %u\n",
		      DP_UUID(uuid), exp_status, inconsistency_nr);

	if (dci->dci_pool_nr != 1) {
		print_message("CR pool count %d (pool " DF_UUID ") is not 1\n",
			      dci->dci_pool_nr, DP_UUID(uuid));
		return -DER_INVAL;
	}

	dcpi = &dci->dci_pools[0];
	D_ASSERTF(uuid_compare(dcpi->dcpi_uuid, uuid) == 0,
		  "Unmatched pool UUID (1): " DF_UUID " vs " DF_UUID "\n",
		  DP_UUID(dcpi->dcpi_uuid), DP_UUID(uuid));

	switch (exp_status) {
	case TCPS_UNCHECKED:
		if (!cr_pool_status_unchecked(dcpi->dcpi_status)) {
			print_message("CR pool " DF_UUID " status %s is not unchecked\n",
				      DP_UUID(uuid), dcpi->dcpi_status);
			return -DER_INVAL;
		}
		if (!cr_pool_phase_is_prepare(dcpi->dcpi_phase)) {
			print_message("CR pool " DF_UUID " phase %s is not prepare\n",
				      DP_UUID(uuid), dcpi->dcpi_phase);
			return -DER_INVAL;
		}
		break;
	case TCPS_CHECKING:
		if (!cr_pool_status_checking(dcpi->dcpi_status)) {
			print_message("CR pool " DF_UUID " status %s is not checking\n",
				      DP_UUID(uuid), dcpi->dcpi_status);
			return -DER_INVAL;
		}
		break;
	case TCPS_CHECKED:
		if (!cr_pool_status_checked(dcpi->dcpi_status)) {
			print_message("CR pool " DF_UUID " status %s is not checked\n",
				      DP_UUID(uuid), dcpi->dcpi_status);
			return -DER_INVAL;
		}
		if (inconsistency_nr == 0 && !cr_pool_phase_is_done(dcpi->dcpi_phase)) {
			print_message("CR pool " DF_UUID " phase %s is not done\n",
				      DP_UUID(uuid), dcpi->dcpi_phase);
			return -DER_INVAL;
		}
		break;
	case TCPS_FAILED:
		if (!cr_pool_status_failed(dcpi->dcpi_status)) {
			print_message("CR pool " DF_UUID " status %s is not failed\n",
				      DP_UUID(uuid), dcpi->dcpi_status);
			return -DER_INVAL;
		}
		if (cr_pool_phase_is_done(dcpi->dcpi_phase)) {
			print_message("CR pool " DF_UUID " phase should not be done\n",
				      DP_UUID(uuid));
			return -DER_INVAL;
		}
		break;
	case TCPS_PAUSED:
		if (!cr_pool_status_paused(dcpi->dcpi_status)) {
			print_message("CR pool " DF_UUID " status %s is not paused\n",
				      DP_UUID(uuid), dcpi->dcpi_status);
			return -DER_INVAL;
		}
		if (cr_pool_phase_is_done(dcpi->dcpi_phase)) {
			print_message("CR pool " DF_UUID " phase should not be done\n",
				      DP_UUID(uuid));
			return -DER_INVAL;
		}
		break;
	case TCPS_PENDING:
		if (!cr_pool_status_pending(dcpi->dcpi_status)) {
			print_message("CR pool " DF_UUID " status %s is not pending\n",
				      DP_UUID(uuid), dcpi->dcpi_status);
			return -DER_INVAL;
		}
		if (cr_pool_phase_is_done(dcpi->dcpi_phase)) {
			print_message("CR pool " DF_UUID " phase should not be done\n",
				      DP_UUID(uuid));
			return -DER_INVAL;
		}
		break;
	case TCPS_STOPPED:
		if (!cr_pool_status_stopped(dcpi->dcpi_status)) {
			print_message("CR pool " DF_UUID " status %s is not stopped\n",
				      DP_UUID(uuid), dcpi->dcpi_status);
			return -DER_INVAL;
		}
		if (cr_pool_phase_is_done(dcpi->dcpi_phase)) {
			print_message("CR pool " DF_UUID " phase should not be done\n",
				      DP_UUID(uuid));
			return -DER_INVAL;
		}
		break;
	case TCPS_IMPLICATED:
		if (!cr_pool_status_implicated(dcpi->dcpi_status)) {
			print_message("CR pool " DF_UUID " status %s is not implicated\n",
				      DP_UUID(uuid), dcpi->dcpi_status);
			return -DER_INVAL;
		}
		if (cr_pool_phase_is_done(dcpi->dcpi_phase)) {
			print_message("CR pool " DF_UUID " phase should not be done\n",
				      DP_UUID(uuid));
			return -DER_INVAL;
		}
		break;
	default:
		print_message("CR: invalid expected pool status %d\n", exp_status);
		break;
	}

#ifdef CR_ACCURATE_QUERY_RESULT
	if (dci->dci_report_nr != inconsistency_nr) {
		print_message("CR pool " DF_UUID " has unexpected reports: %d vs %d\n",
			      DP_UUID(uuid), dci->dci_report_nr, inconsistency_nr);
		return -DER_INVAL;
	}
#endif

	for (i = 0, j = 0; i < dci->dci_report_nr && j < inconsistency_nr; i++) {
		dcri = &dci->dci_reports[i];
		if (uuid_compare(dcri->dcri_uuid, uuid) != 0) {
#ifdef CR_ACCURATE_QUERY_RESULT
			print_message("Detect unrelated inconsistency report: "
				      DF_UUID " vs " DF_UUID "\n",
				      DP_UUID(dcpi->dcpi_uuid), DP_UUID(uuid));
			return -DER_INVAL;
#else
			continue;
#endif
		}

		if (dcri->dcri_class != classes[j]) {
			print_message("CR pool " DF_UUID " reports unexpected inconsistency at "
				      "%d/%d: %u vs %u\n",
				      DP_UUID(uuid), i, j, dcri->dcri_class, classes[j]);
			return -DER_INVAL;
		}

		if (dcri->dcri_act != actions[j]) {
			print_message("CR pool " DF_UUID " reports unexpected solution at %d/%d: "
				      "%u vs %u\n",
				      DP_UUID(uuid), i, j, dcri->dcri_act, actions[j]);
			return -DER_INVAL;
		}

		if (exp_results != NULL)
			result = exp_results[j];
		else
			result = 0;

		if (dcri->dcri_result != result) {
			print_message("CR pool " DF_UUID " unexpected result at %d/%d: %d vs %d\n",
				      DP_UUID(uuid), i, j, dcri->dcri_result, result);
			return -DER_INVAL;
		}

		j++;
	}

	if (j != inconsistency_nr) {
		print_message("CR pool " DF_UUID " miss some inconsistency reports: %d vs %d\n",
			      DP_UUID(uuid), j, inconsistency_nr);
		return -DER_INVAL;
	}

	return 0;
}

static int
cr_pool_create(void **state, struct test_pool *pool, bool connect, uint32_t fault)
{
	test_arg_t	*arg = *state;
	char		*ptr;
	int		 rc;

	pool->pool_size = CR_POOL_SIZE;
	print_message("CR: creating pool ...\n");
	rc = test_setup_pool_create(state, NULL, pool, NULL);
	if (rc != 0) {
		print_message("CR: pool creation failed: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	print_message("CR: getting label for pool " DF_UUID "\n", DP_UUID(pool->pool_uuid));
	rc = dmg_pool_get_prop(dmg_config_file, NULL, pool->pool_uuid, "label", &pool->label);
	if (rc != 0) {
		print_message("CR: pool " DF_UUID " get label failed: "DF_RC"\n",
			      DP_UUID(pool->pool_uuid), DP_RC(rc));
		return rc;
	}

	if (connect) {
		print_message("CR: connecting pool " DF_UUID "\n", DP_UUID(pool->pool_uuid));
		rc = daos_pool_connect(pool->pool_str, arg->group, DAOS_PC_RW, &pool->poh, NULL,
				       NULL);
		if (rc != 0) {
			print_message("CR: pool " DF_UUID " connect failed: "DF_RC"\n",
				      DP_UUID(pool->pool_uuid), DP_RC(rc));
			return rc;
		}

		if (arg->srv_ntgts == 0) {
			daos_pool_info_t	info = {0};

			rc = daos_pool_query(pool->poh, NULL, &info, NULL, NULL);
			if (rc != 0) {
				print_message("CR: pool " DF_UUID " query failed: "DF_RC"\n",
					      DP_UUID(pool->pool_uuid), DP_RC(rc));
				return rc;
			}

			arg->srv_ntgts = info.pi_ntargets;
			arg->srv_nnodes = info.pi_nnodes;
			arg->srv_disabled_ntgts = info.pi_ndisabled;
		}
	}

	switch (fault) {
	case TCC_NONE:
		break;
	case TCC_POOL_NONEXIST_ON_MS:
		rc = cr_fault_inject(pool->pool_uuid, true, "CIC_POOL_NONEXIST_ON_MS");
		break;
	case TCC_POOL_NONEXIST_ON_ENGINE:
		rc = cr_fault_inject(pool->pool_uuid, false, "CIC_POOL_NONEXIST_ON_ENGINE");
		break;
	case TCC_POOL_BAD_LABEL:
		rc = cr_fault_inject(pool->pool_uuid, true, "CIC_POOL_BAD_LABEL");
		if (rc == 0) {
			rc = strlen(pool->label);
			D_REALLOC(ptr, pool->label, rc, rc + 7);
			if (ptr == NULL) {
				print_message("CR: pool " DF_UUID " refresh label failed\n",
					      DP_UUID(pool->pool_uuid));
				rc = -DER_NOMEM;
			} else {
				strcat(ptr, "-fault");
				pool->label = ptr;
				rc = 0;
			}
		}
		break;
	default:
		print_message("CR: invalid type %d for pool " DF_UUID " fault injection\n", fault,
			       DP_UUID(pool->pool_uuid));
		rc = -DER_INVAL;
		break;
	}

	return rc;
}

static int
cr_pool_create_with_svc(void **state, struct test_pool *pool, bool connect, uint32_t fault)
{
	pool->svc = d_rank_list_alloc(1);
	if (pool->svc == NULL) {
		print_message("CR: failed to create svc list for create pool\n");
		return -DER_NOMEM;
	}

	return cr_pool_create(state, pool, connect, fault);
}

static int
cr_cont_create(void **state, struct test_pool *pool, struct test_cont *cont, int fault)
{
	char		 uuid_str[DAOS_UUID_STR_SIZE];
	test_arg_t	*arg = *state;
	daos_prop_t	*prop = NULL;
	mode_t		 saved;
	daos_handle_t	 coh;
	int		 fd;
	int		 rc;
	int		 rc1;

	saved = umask(0);
	strncpy(cont->label, "/tmp/cr_cont_XXXXXX", sizeof(cont->label) - 1);
	fd = mkstemp(cont->label);
	umask(saved);
	if (fd < 0) {
		print_message("CR: cont generate label failed: %s\n", strerror(errno));
		return d_errno2der(errno);
	}

	close(fd);
	unlink(cont->label);

	/* Move cr_cont_XXXXXX (including the terminated '\0') ahead to overwrite '/tmp/' */
	memmove(cont->label, &cont->label[5], strlen(cont->label) - 4);
	print_message("CR: creating container ...\n");
	if (fault >= 0)
		rc = daos_cont_create_with_label(pool->poh, cont->label, NULL, &cont->uuid, NULL);
	else
		rc = daos_cont_create(pool->poh, &cont->uuid, NULL, NULL);
	if (rc != 0) {
		print_message("CR: cont creation failed: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	if (fault != 0) {
		print_message("CR: opening container " DF_UUID " ...\n", DP_UUID(cont->uuid));
		if (fault < 0) {
			uuid_unparse_lower(cont->uuid, uuid_str);
			rc = daos_cont_open(pool->poh, uuid_str, DAOS_COO_RW, &coh, NULL, NULL);
		} else {
			rc = daos_cont_open(pool->poh, cont->label, DAOS_COO_RW, &coh, NULL, NULL);
		}
		if (rc != 0) {
			print_message("CR: cont " DF_UUID " open failed: "DF_RC"\n",
				      DP_UUID(cont->uuid), DP_RC(rc));
			return rc;
		}

		/* Inject fail_loc to generate inconsistent container label. */
		rc = cr_debug_set_params(arg, DAOS_CHK_CONT_BAD_LABEL | DAOS_FAIL_ALWAYS);
		assert_rc_equal(rc, 0);

		prop = daos_prop_alloc(1);
		assert_non_null(prop);

		/* cont->label is large enough to hold the new label. */
		D_ASSERT(sizeof(cont->label) > strlen(cont->label) + 7);
		strcat(cont->label, "-fault");
		prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_LABEL;
		D_STRNDUP(prop->dpp_entries[0].dpe_str, cont->label, strlen(cont->label));

		print_message("CR: set label for container " DF_UUID "\n", DP_UUID(cont->uuid));
		rc = daos_cont_set_prop(coh, prop, NULL);
		if (rc != 0)
			print_message("CR: cont " DF_UUID " set label failed: "DF_RC"\n",
				      DP_UUID(cont->uuid), DP_RC(rc));

		daos_prop_free(prop);
		cr_debug_set_params(arg, 0);

		print_message("CR: closing container " DF_UUID " ...\n", DP_UUID(cont->uuid));
		rc1 = daos_cont_close(coh, NULL);
		if (rc1 != 0) {
			print_message("CR: cont " DF_UUID " close failed: "DF_RC"\n",
				      DP_UUID(cont->uuid), DP_RC(rc1));
			if (rc == 0)
				rc = rc1;
		}
	}

	return rc;
}

static int
cr_cont_get_label(void **state, struct test_pool *pool, struct test_cont *cont, bool connect,
		  char **label)
{
	char		 uuid_str[DAOS_UUID_STR_SIZE];
	test_arg_t	*arg = *state;
	daos_prop_t	*prop = NULL;
	daos_handle_t	 coh;
	int		 rc;
	int		 rc1;

	if (connect) {
		print_message("CR: connecting pool " DF_UUID "\n", DP_UUID(pool->pool_uuid));
		rc = daos_pool_connect(pool->pool_str, arg->group, DAOS_PC_RW, &pool->poh, NULL,
				       NULL);
		if (rc != 0) {
			print_message("CR: pool " DF_UUID " connect failed: "DF_RC"\n",
				      DP_UUID(pool->pool_uuid), DP_RC(rc));
			return rc;
		}
	}

	print_message("CR: opening container " DF_UUID " ...\n", DP_UUID(cont->uuid));
	uuid_unparse_lower(cont->uuid, uuid_str);
	rc = daos_cont_open(pool->poh, uuid_str, DAOS_COO_RW, &coh, NULL, NULL);
	if (rc != 0) {
		print_message("CR: cont " DF_UUID " open failed: "DF_RC"\n",
			      DP_UUID(cont->uuid), DP_RC(rc));
		return rc;
	}

	prop = daos_prop_alloc(1);
	assert_non_null(prop);

	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_LABEL;
	print_message("CR: getting label for container " DF_UUID "\n", DP_UUID(cont->uuid));

	rc = daos_cont_query(coh, NULL, prop, NULL);
	if (rc != 0)
		print_message("CR: cont " DF_UUID " set label failed: "DF_RC"\n",
			      DP_UUID(cont->uuid), DP_RC(rc));
	else
		D_STRNDUP(*label, prop->dpp_entries[0].dpe_str,
			  strlen(prop->dpp_entries[0].dpe_str));

	daos_prop_free(prop);

	print_message("CR: closing container " DF_UUID " ...\n", DP_UUID(cont->uuid));
	rc1 = daos_cont_close(coh, NULL);
	if (rc1 != 0) {
		print_message("CR: cont " DF_UUID " close failed: "DF_RC"\n",
			      DP_UUID(cont->uuid), DP_RC(rc1));
		if (rc == 0)
			rc = rc1;
	}

	/*
	 * Do not disconnect the pool that may be reused by subsequent operation. cr_cleanup() will
	 * handle that finally.
	 */

	return rc;
}

/* Test Cases. */

/*
 * 1. Create pool1, pool2 and pool3.
 * 2. Fault injection to generate inconsistent pool label for all of them.
 * 3. Start checker on pool1 and pool2.
 * 4. Query checker, pool1 and pool2 should have been repaired, pool3 should not be repaired.
 * 5. Switch to normal mode and verify the labels.
 * 6. Cleanup.
 */
static void
cr_start_specified(void **state)
{
	test_arg_t		*arg = *state;
	struct test_pool	 pools[3] = { 0 };
	uuid_t			 uuids[3] = { 0 };
	struct daos_check_info	 dcis[3] = { 0 };
	char			*label = NULL;
	uint32_t		 class = TCC_POOL_BAD_LABEL;
	uint32_t		 action = TCA_TRUST_MS;
	int			 rc;
	int			 i;

	print_message("CR1: start checker for specified pools\n");

	for (i = 0; i < 3; i++) {
		rc = cr_pool_create(state, &pools[i], false, class);
		assert_rc_equal(rc, 0);

		uuid_copy(uuids[i], pools[i].pool_uuid);
	}

	rc = cr_system_stop(false);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(true);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_RESET, 2, uuids, NULL);
	assert_rc_equal(rc, 0);

	cr_ins_wait(1, &uuids[0], &dcis[0]);

	for (i = 1; i < 3; i++) {
		rc = cr_check_query(1, &uuids[i], &dcis[i]);
		assert_rc_equal(rc, 0);
	}

	for (i = 0; i < 3; i++) {
		rc = cr_ins_verify(&dcis[i], TCIS_COMPLETED);
		assert_rc_equal(rc, 0);
	}

	for (i = 0; i < 2; i++) {
		rc = cr_pool_verify(&dcis[i], uuids[i], TCPS_CHECKED, 1, &class, &action, NULL);
		assert_rc_equal(rc, 0);
	}

	rc = cr_pool_verify(&dcis[2], uuids[2], TCPS_UNCHECKED, 0, NULL, NULL, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(false);
	assert_rc_equal(rc, 0);

	rc = cr_system_start();
	assert_rc_equal(rc, 0);

	for (i = 0; i < 3; i++) {
		print_message("CR: getting label for pool " DF_UUID " after check\n",
			      DP_UUID(pools[i].pool_uuid));
		rc = dmg_pool_get_prop(dmg_config_file, pools[i].label, pools[i].pool_uuid, "label",
				       &label);
		assert_rc_equal(rc, 0);

		if (i < 2)
			D_ASSERTF(strcmp(label, pools[i].label) == 0,
				  "Pool (" DF_UUID ") label is not repaired: %s vs %s\n",
				  DP_UUID(pools[i].pool_uuid), label, pools[i].label);
		else
			D_ASSERTF(strcmp(label, pools[i].label) != 0,
				  "Pool (" DF_UUID ") label should not be repaired: %s\n",
				  DP_UUID(pools[i].pool_uuid), label);

		D_FREE(label);
		cr_dci_fini(&dcis[i]);
	}

	cr_cleanup(arg, pools, 3);
}

/*
 * 1. Create pool.
 * 2. Fault injection to make pool as orphan.
 * 3. Start checker with POOL_NONEXIST_ON_MS:CIA_INTERACT.
 * 4. Query checker, should show interaction.
 * 5. Check repair with re-add the orphan pool.
 * 6. Query checker, orphan pool should have been repaired.
 * 7. Switch to normal mode and verify the pool.
 * 8. Cleanup.
 */
static void
cr_leader_interaction(void **state)
{
	test_arg_t			*arg = *state;
	struct test_pool		 pool = { 0 };
	daos_mgmt_pool_info_t		 mgmt_pool = { 0 };
	struct daos_check_info		 dci = { 0 };
	struct daos_check_report_info	*dcri;
	uint32_t			 class = TCC_POOL_NONEXIST_ON_MS;
	uint32_t			 action;
	daos_size_t			 pool_nr = 1;
	int				 rc;
	int				 i;

	print_message("CR2: check leader side interaction\n");

	rc = cr_pool_create(state, &pool, false, class);
	assert_rc_equal(rc, 0);

	rc = cr_system_stop(false);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(true);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_RESET, 0, NULL, "POOL_NONEXIST_ON_MS:CIA_INTERACT");
	assert_rc_equal(rc, 0);

	cr_pool_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_RUNNING);
	assert_rc_equal(rc, 0);

	action = TCA_INTERACT;
	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_PENDING, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	dcri = cr_locate_dcri(&dci, NULL, pool.pool_uuid);
	action = TCA_READD;
	rc = -DER_MISC;

	for (i = 0; i < dcri->dcri_option_nr; i++) {
		if (dcri->dcri_options[i] == action) {
			rc = cr_check_repair(dcri->dcri_seq, i, false);
			break;
		}
	}
	assert_rc_equal(rc, 0);

	cr_ins_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_COMPLETED);
	assert_rc_equal(rc, 0);

	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_CHECKED, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(false);
	assert_rc_equal(rc, 0);

	rc = cr_system_start();
	assert_rc_equal(rc, 0);

	rc = dmg_pool_list(dmg_config_file, arg->group, &pool_nr, &mgmt_pool);
	assert_rc_equal(rc, 0);

	assert_rc_equal(pool_nr, 1);
	D_ASSERTF(uuid_compare(pool.pool_uuid, mgmt_pool.mgpi_uuid) == 0,
		  "Unmatched pool UUID: " DF_UUID " vs " DF_UUID "\n",
		  DP_UUID(pool.pool_uuid), DP_UUID(mgmt_pool.mgpi_uuid));

	cr_dci_fini(&dci);
	clean_pool_info(1, &mgmt_pool);
	cr_cleanup(arg, &pool, 1);
}

/*
 * 1. Create pool and container.
 * 2. Fault injection to make container label inconsistent.
 * 3. Start checker with CONT_BAD_LABEL:CIA_INTERACT
 * 4. Query checker, should show interaction.
 * 5. Check repair the container label with trust PS (pool/container service).
 * 6. Query checker, container label should have been repaired.
 * 7. Switch to normal mode and verify the container label.
 * 8. Cleanup.
 */
static void
cr_engine_interaction(void **state)
{
	test_arg_t			*arg = *state;
	struct test_pool		 pool = { 0 };
	struct test_cont		 cont = { 0 };
	struct daos_check_info		 dci = { 0 };
	struct daos_check_report_info	*dcri;
	char				*label = NULL;
	uint32_t			 class = TCC_CONT_BAD_LABEL;
	uint32_t			 action;
	int				 rc;
	int				 i;

	print_message("CR3: check engine side interaction\n");

	rc = cr_pool_create(state, &pool, true, TCC_NONE);
	assert_rc_equal(rc, 0);

	rc = cr_cont_create(state, &pool, &cont, 1);
	assert_rc_equal(rc, 0);

	rc = cr_system_stop(false);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(true);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_RESET, 0, NULL, "CONT_BAD_LABEL:CIA_INTERACT");
	assert_rc_equal(rc, 0);

	cr_pool_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_RUNNING);
	assert_rc_equal(rc, 0);

	action = TCA_INTERACT;
	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_PENDING, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	dcri = cr_locate_dcri(&dci, NULL, pool.pool_uuid);
	action = TCA_TRUST_PS;
	rc = -DER_MISC;

	for (i = 0; i < dcri->dcri_option_nr; i++) {
		if (dcri->dcri_options[i] == action) {
			rc = cr_check_repair(dcri->dcri_seq, i, false);
			break;
		}
	}
	assert_rc_equal(rc, 0);

	cr_ins_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_COMPLETED);
	assert_rc_equal(rc, 0);

	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_CHECKED, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(false);
	assert_rc_equal(rc, 0);

	rc = cr_system_start();
	assert_rc_equal(rc, 0);

	/* Former connection for the pool has been evicted by checkre. Let's re-connect the pool. */
	rc = cr_cont_get_label(state, &pool, &cont, true, &label);
	assert_rc_equal(rc, 0);

	D_ASSERTF(strcmp(label, cont.label) == 0,
		  "Cont (" DF_UUID ") label is not repaired: %s vs %s\n",
		  DP_UUID(cont.uuid), label, cont.label);

	D_FREE(label);
	cr_dci_fini(&dci);
	cr_cleanup(arg, &pool, 1);
}

/*
 * 1. Create pool1 and pool2.
 * 2. Fault injection to make inconsistent label for both of them.
 * 3. Start checker on pool1 and pool2 with POOL_BAD_LABEL:CIA_INTERACT
 * 4. Query checker, should show interaction.
 * 5. Check repair pool1's label with trust PS (trust MS is the default) and "for-all" option.
 * 6. Query checker, should be completed, both pool1 and pool2 label should have been repaired.
 * 7. Switch to normal mode and verify pools' labels.
 * 8. Cleanup.
 */
static void
cr_repair_forall_leader(void **state)
{
	test_arg_t			*arg = *state;
	struct test_pool		 pools[2] = { 0 };
	struct daos_check_info		 dci = { 0 };
	struct daos_check_report_info	*dcri;
	char				*ps_label = NULL;
	char				*ptr;
	char				 ms_label[DAOS_PROP_LABEL_MAX_LEN];
	uint32_t			 class = TCC_POOL_BAD_LABEL;
	uint32_t			 action;
	int				 rc;
	int				 i;

	print_message("CR4: check repair option - for-all, on leader\n");

	for (i = 0; i < 2; i++) {
		rc = cr_pool_create(state, &pools[i], false, class);
		assert_rc_equal(rc, 0);
	}

	rc = cr_system_stop(false);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(true);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_RESET, 0, NULL, "POOL_BAD_LABEL:CIA_INTERACT");
	assert_rc_equal(rc, 0);

	cr_pool_wait(1, &pools[0].pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_RUNNING);
	assert_rc_equal(rc, 0);

	action = TCA_INTERACT;
	rc = cr_pool_verify(&dci, pools[0].pool_uuid, TCPS_PENDING, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	dcri = cr_locate_dcri(&dci, NULL, pools[0].pool_uuid);
	action = TCA_TRUST_PS;
	rc = -DER_MISC;

	for (i = 0; i < dcri->dcri_option_nr; i++) {
		if (dcri->dcri_options[i] == action) {
			rc = cr_check_repair(dcri->dcri_seq, i, true);
			break;
		}
	}
	assert_rc_equal(rc, 0);

	for (i = 0; i < 2; i++) {
		cr_ins_wait(1, &pools[i].pool_uuid, &dci);

		rc = cr_ins_verify(&dci, TCIS_COMPLETED);
		assert_rc_equal(rc, 0);

		rc = cr_pool_verify(&dci, pools[i].pool_uuid, TCPS_CHECKED, 1, &class, &action,
				    NULL);
		assert_rc_equal(rc, 0);
	}

	rc = cr_mode_switch(false);
	assert_rc_equal(rc, 0);

	rc = cr_system_start();
	assert_rc_equal(rc, 0);

	for (i = 0; i < 2; i++) {
		/* The last 6 characters of pools[i].label is '-fault'. */
		ptr = strrchr(pools[i].label, '-');
		assert_non_null(ptr);

		memcpy(ms_label, pools[i].label, ptr - pools[i].label);
		ms_label[ptr - pools[i].label] = '\0';

		print_message("CR: getting label for pool " DF_UUID " after check\n",
			      DP_UUID(pools[i].pool_uuid));
		rc = dmg_pool_get_prop(dmg_config_file, ms_label, pools[i].pool_uuid, "label",
				       &ps_label);
		assert_rc_equal(rc, 0);

		D_ASSERTF(strcmp(ps_label, ms_label) == 0,
			  "Pool (" DF_UUID ") label is not repaired: %s vs %s\n",
			  DP_UUID(pools[i].pool_uuid), ps_label, ms_label);
		D_FREE(ps_label);
	}

	cr_dci_fini(&dci);
	cr_cleanup(arg, pools, 2);
}

/*
 * 1. Create pool1 and pool2. Create container under both of them.
 * 2. Fault injection to make inconsistent container label for both of them.
 * 3. Start checker on pool1 and pool2 with CONT_BAD_LABEL:CIA_INTERACT
 * 4. Query checker, should show interaction.
 * 5. Check repair pool1/cont's label with trust target (trust PS/CS is the default) and "for-all".
 * 6. Query checker, should be completed, both containers' label should have been repaired.
 * 7. Switch to normal mode and verify containers' labels.
 * 8. Cleanup.
 */
static void
cr_repair_forall_engine(void **state)
{
	test_arg_t			*arg = *state;
	struct test_pool		 pools[2] = { 0 };
	struct test_cont		 conts[2] = { 0 };
	struct daos_check_info		 dci = { 0 };
	struct daos_check_report_info	*dcri;
	char				*target_label = NULL;
	char				*ptr;
	char				 ps_label[DAOS_PROP_LABEL_MAX_LEN];
	uint32_t			 class = TCC_CONT_BAD_LABEL;
	uint32_t			 action;
	int				 rc;
	int				 i;

	print_message("CR5: check repair option - for-all, on engine\n");

	for (i = 0; i < 2; i++) {
		rc = cr_pool_create(state, &pools[i], true, TCC_NONE);
		assert_rc_equal(rc, 0);

		rc = cr_cont_create(state, &pools[i], &conts[i], 1);
		assert_rc_equal(rc, 0);
	}

	rc = cr_system_stop(false);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(true);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_RESET, 0, NULL, "CONT_BAD_LABEL:CIA_INTERACT");
	assert_rc_equal(rc, 0);

	cr_pool_wait(1, &pools[0].pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_RUNNING);
	assert_rc_equal(rc, 0);

	action = TCA_INTERACT;
	rc = cr_pool_verify(&dci, pools[0].pool_uuid, TCPS_PENDING, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	dcri = cr_locate_dcri(&dci, NULL, pools[0].pool_uuid);
	action = TCA_TRUST_TARGET;
	rc = -DER_MISC;

	for (i = 0; i < dcri->dcri_option_nr; i++) {
		if (dcri->dcri_options[i] == action) {
			rc = cr_check_repair(dcri->dcri_seq, i, true);
			break;
		}
	}
	assert_rc_equal(rc, 0);

	for (i = 0; i < 2; i++) {
		cr_ins_wait(1, &pools[i].pool_uuid, &dci);

		rc = cr_ins_verify(&dci, TCIS_COMPLETED);
		assert_rc_equal(rc, 0);

		rc = cr_pool_verify(&dci, pools[i].pool_uuid, TCPS_CHECKED, 1, &class, &action,
				    NULL);
		assert_rc_equal(rc, 0);
	}

	rc = cr_mode_switch(false);
	assert_rc_equal(rc, 0);

	rc = cr_system_start();
	assert_rc_equal(rc, 0);

	for (i = 0; i < 2; i++) {
		/* The last 6 characters of conts[i].label is '-fault'. */
		ptr = strrchr(conts[i].label, '-');
		assert_non_null(ptr);

		memcpy(ps_label, conts[i].label, ptr - conts[i].label);
		ps_label[ptr - conts[i].label] = '\0';

		rc = cr_cont_get_label(state, &pools[i], &conts[i], true, &target_label);
		assert_rc_equal(rc, 0);

		D_ASSERTF(strcmp(target_label, ps_label) == 0,
			  "Cont (" DF_UUID ") label is not repaired: %s vs %s\n",
			  DP_UUID(conts[i].uuid), target_label, ps_label);
		D_FREE(target_label);
	}

	cr_dci_fini(&dci);
	cr_cleanup(arg, pools, 2);
}

/*
 * 1. Create pool.
 * 2. Fault injection to generate inconsistent pool label.
 * 3. Start checker with POOL_NONEXIST_ON_MS:CIA_INTERACT.
 * 4. Query checker, should show interaction.
 * 5. Stop checker.
 * 6. Query checker, instance should be stopped.
 * 7. Switch to normal mode to verify the pool label that should not be repaired.
 * 8. Cleanup.
 */
static void
cr_stop_leader_interaction(void **state)
{
	test_arg_t		*arg = *state;
	struct test_pool	 pool = { 0 };
	struct daos_check_info	 dci = { 0 };
	char			*label = NULL;
	uint32_t		 class = TCC_POOL_BAD_LABEL;
	uint32_t		 action = TCA_INTERACT;
	int			 rc;

	print_message("CR6: stop checker with pending check leader interaction\n");

	rc = cr_pool_create(state, &pool, false, class);
	assert_rc_equal(rc, 0);

	rc = cr_system_stop(false);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(true);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_RESET, 0, NULL, "POOL_BAD_LABEL:CIA_INTERACT");
	assert_rc_equal(rc, 0);

	cr_pool_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_RUNNING);
	assert_rc_equal(rc, 0);

	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_PENDING, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_check_stop(0, NULL);
	assert_rc_equal(rc, 0);

	cr_ins_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_STOPPED);
	assert_rc_equal(rc, 0);

	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_STOPPED, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(false);
	assert_rc_equal(rc, 0);

	rc = cr_system_start();
	assert_rc_equal(rc, 0);

	print_message("CR: getting label for pool " DF_UUID " after check\n",
		       DP_UUID(pool.pool_uuid));
	rc = dmg_pool_get_prop(dmg_config_file, pool.label, pool.pool_uuid, "label", &label);
	assert_rc_equal(rc, 0);

	D_ASSERTF(strcmp(label, pool.label) != 0,
		  "Pool (" DF_UUID ") label should not be repaired: %s\n",
		  DP_UUID(pool.pool_uuid), label);

	D_FREE(label);
	cr_dci_fini(&dci);
	cr_cleanup(arg, &pool, 1);
}

/*
 * 1. Create pool and container.
 * 2. Fault injection to make container label inconsistent.
 * 3. Start checker with CONT_BAD_LABEL:CIA_INTERACT
 * 4. Query checker, should show interaction.
 * 5. Stop checker.
 * 6. Query checker, instance should be stopped.
 * 7. Switch to normal mode to verify the container label that should not be repaired.
 * 8. Cleanup.
 */
static void
cr_stop_engine_interaction(void **state)
{
	test_arg_t		*arg = *state;
	struct test_pool	 pool = { 0 };
	struct test_cont	 cont = { 0 };
	struct daos_check_info	 dci = { 0 };
	char			*label = NULL;
	uint32_t		 class = TCC_CONT_BAD_LABEL;
	uint32_t		 action = TCA_INTERACT;
	int			 rc;

	print_message("CR7: stop checker with pending check engine interaction\n");

	rc = cr_pool_create(state, &pool, true, TCC_NONE);
	assert_rc_equal(rc, 0);

	rc = cr_cont_create(state, &pool, &cont, 1);
	assert_rc_equal(rc, 0);

	rc = cr_system_stop(false);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(true);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_RESET, 0, NULL, "CONT_BAD_LABEL:CIA_INTERACT");
	assert_rc_equal(rc, 0);

	cr_pool_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_RUNNING);
	assert_rc_equal(rc, 0);

	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_PENDING, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_check_stop(0, NULL);
	assert_rc_equal(rc, 0);

	cr_ins_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_STOPPED);
	assert_rc_equal(rc, 0);

	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_STOPPED, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(false);
	assert_rc_equal(rc, 0);

	rc = cr_system_start();
	assert_rc_equal(rc, 0);

	/* Former connection for the pool has been evicted by checkre. Let's re-connect the pool. */
	rc = cr_cont_get_label(state, &pool, &cont, true, &label);
	assert_rc_equal(rc, 0);

	D_ASSERTF(strcmp(label, cont.label) != 0,
		  "Cont (" DF_UUID ") label should not be repaired: %s\n",
		  DP_UUID(cont.uuid), label);

	D_FREE(label);
	cr_dci_fini(&dci);
	cr_cleanup(arg, &pool, 1);
}

/*
 * 1. Create pool1, pool2 and pool3.
 * 2. Fault injection to generate inconsistent pool label for all of them.
 * 3. Start checker on pools with BAD_POOL_LABEL:CIA_INTERACT.
 * 4. Query checker, should show interaction.
 * 5. Stop checker on pool1 and pool2.
 * 6. Query checker, instance should still run, but checking of pool1 and pool2 should be stopped.
 * 7. Check repair pool3's label with trust MS.
 * 8. Query checker, instance should be completed.
 * 9. Switch to normal mode to verify the labels:
 *    pool1 and pool2 should not be fixed, pool3 should have been fixed.
 * 10. Cleanup.
 */
static void
cr_stop_specified(void **state)
{
	test_arg_t			*arg = *state;
	struct test_pool		 pools[3] = { 0 };
	uuid_t				 uuids[3] = { 0 };
	struct daos_check_info		 dcis[3] = { 0 };
	struct daos_check_report_info	*dcri;
	char				*label = NULL;
	uint32_t			 class = TCC_POOL_BAD_LABEL;
	uint32_t			 action;
	int				 rc;
	int				 i;

	print_message("CR8: stop checker for specified pools\n");

	for (i = 0; i < 3; i++) {
		rc = cr_pool_create(state, &pools[i], false, class);
		assert_rc_equal(rc, 0);

		uuid_copy(uuids[i], pools[i].pool_uuid);
	}

	rc = cr_system_stop(false);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(true);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_RESET, 0, NULL, "POOL_BAD_LABEL:CIA_INTERACT");
	assert_rc_equal(rc, 0);

	action = TCA_INTERACT;
	for (i = 0; i < 3; i++) {
		cr_pool_wait(1, &uuids[i], &dcis[i]);

		rc = cr_ins_verify(&dcis[i], TCIS_RUNNING);
		assert_rc_equal(rc, 0);

		rc = cr_pool_verify(&dcis[i], uuids[i], TCPS_PENDING, 1, &class, &action, NULL);
		assert_rc_equal(rc, 0);
	}

	rc = cr_check_stop(2, uuids);
	assert_rc_equal(rc, 0);

	for (i = 0; i < 3; i++) {
		cr_dci_fini(&dcis[i]);
		rc = cr_check_query(1, &uuids[i], &dcis[i]);
		assert_rc_equal(rc, 0);
	}

	for (i = 0; i < 2; i++) {
		rc = cr_ins_verify(&dcis[i], TCIS_RUNNING);
		assert_rc_equal(rc, 0);

		rc = cr_pool_verify(&dcis[i], uuids[i], TCPS_STOPPED, 1, &class, &action, NULL);
		assert_rc_equal(rc, 0);
	}

	rc = cr_ins_verify(&dcis[2], TCIS_RUNNING);
	assert_rc_equal(rc, 0);

	rc = cr_pool_verify(&dcis[2], uuids[2], TCPS_PENDING, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	dcri = cr_locate_dcri(&dcis[2], NULL, uuids[2]);
	action = TCA_TRUST_MS;
	rc = -DER_MISC;

	for (i = 0; i < dcri->dcri_option_nr; i++) {
		if (dcri->dcri_options[i] == action) {
			rc = cr_check_repair(dcri->dcri_seq, i, false);
			break;
		}
	}
	assert_rc_equal(rc, 0);

	cr_ins_wait(1, &uuids[2], &dcis[2]);

	rc = cr_ins_verify(&dcis[2], TCIS_COMPLETED);
	assert_rc_equal(rc, 0);

	rc = cr_pool_verify(&dcis[2], uuids[2], TCPS_CHECKED, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(false);
	assert_rc_equal(rc, 0);

	rc = cr_system_start();
	assert_rc_equal(rc, 0);

	for (i = 0; i < 3; i++) {
		print_message("CR: getting label for pool " DF_UUID " after check\n",
			      DP_UUID(pools[i].pool_uuid));
		rc = dmg_pool_get_prop(dmg_config_file, pools[i].label, pools[i].pool_uuid, "label",
				       &label);
		assert_rc_equal(rc, 0);

		if (i > 1)
			D_ASSERTF(strcmp(label, pools[i].label) == 0,
				  "Pool (" DF_UUID ") label is not repaired: %s vs %s\n",
				  DP_UUID(pools[i].pool_uuid), label, pools[i].label);
		else
			D_ASSERTF(strcmp(label, pools[i].label) != 0,
				  "Pool (" DF_UUID ") label should not be repaired: %s\n",
				  DP_UUID(pools[i].pool_uuid), label);

		D_FREE(label);
		cr_dci_fini(&dcis[i]);
	}

	cr_cleanup(arg, pools, 3);
}

/*
 * 1. Create pool.
 * 2. Fault injection to make the pool as orphan.
 * 3. Start checker with POOL_NONEXIST_ON_MS:CIA_IGNORE
 * 4. Query checker, instance should be completed, but orphan pool is ignored.
 * 5. Restart checker with specified pool uuid and POOL_NONEXIST_ON_MS:CIA_INTERACT.
 * 6. Query checker, that should show interaction for the orphan pool.
 * 7. Check repair with ignore the orphan pool.
 * 8. Restart checker with POOL_NONEXIST_ON_MS:CIA_DEFAULT but not specify pool uuid.
 * 9. Query checker, the orphan pool should have been repaired.
 * 10. Switch to normal mode and verify the pool.
 * 11. Cleanup.
 */
static void
cr_auto_reset(void **state)
{
	test_arg_t			*arg = *state;
	struct test_pool		 pool = { 0 };
	daos_mgmt_pool_info_t		 mgmt_pool = { 0 };
	struct daos_check_info		 dci = { 0 };
	struct daos_check_report_info	*dcri;
	uint32_t			 class = TCC_POOL_NONEXIST_ON_MS;
	uint32_t			 action;
	daos_size_t			 pool_nr = 1;
	int				 rc;
	int				 i;

	print_message("CR9: reset checker automatically if former instance completed\n");

	rc = cr_pool_create(state, &pool, false, class);
	assert_rc_equal(rc, 0);

	rc = cr_system_stop(false);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(true);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_RESET, 0, NULL, "POOL_NONEXIST_ON_MS:CIA_IGNORE");
	assert_rc_equal(rc, 0);

	cr_ins_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_COMPLETED);
	assert_rc_equal(rc, 0);

	action = TCA_IGNORE;
	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_CHECKED, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_NONE, 1, &pool.pool_uuid, "POOL_NONEXIST_ON_MS:CIA_INTERACT");
	assert_rc_equal(rc, 0);

	cr_pool_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_RUNNING);
	assert_rc_equal(rc, 0);

	action = TCA_INTERACT;
	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_PENDING, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	dcri = cr_locate_dcri(&dci, NULL, pool.pool_uuid);
	action = TCA_IGNORE;
	rc = -DER_MISC;

	for (i = 0; i < dcri->dcri_option_nr; i++) {
		if (dcri->dcri_options[i] == action) {
			rc = cr_check_repair(dcri->dcri_seq, i, false);
			break;
		}
	}
	assert_rc_equal(rc, 0);

	cr_ins_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_COMPLETED);
	assert_rc_equal(rc, 0);

	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_CHECKED, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_NONE, 0, NULL, "POOL_NONEXIST_ON_MS:CIA_DEFAULT");
	assert_rc_equal(rc, 0);

	cr_ins_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_COMPLETED);
	assert_rc_equal(rc, 0);

	action = TCA_READD;
	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_CHECKED, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(false);
	assert_rc_equal(rc, 0);

	rc = cr_system_start();
	assert_rc_equal(rc, 0);

	rc = dmg_pool_list(dmg_config_file, arg->group, &pool_nr, &mgmt_pool);
	assert_rc_equal(rc, 0);

	assert_rc_equal(pool_nr, 1);
	D_ASSERTF(uuid_compare(pool.pool_uuid, mgmt_pool.mgpi_uuid) == 0,
		  "Unmatched pool UUID: " DF_UUID " vs " DF_UUID "\n",
		  DP_UUID(pool.pool_uuid), DP_UUID(mgmt_pool.mgpi_uuid));

	cr_dci_fini(&dci);
	clean_pool_info(1, &mgmt_pool);
	cr_cleanup(arg, &pool, 1);
}

static void
cr_pause(void **state, bool force)
{
	test_arg_t			*arg = *state;
	struct test_pool		 pool = { 0 };
	struct daos_check_info		 dci = { 0 };
	uint32_t			 class = TCC_POOL_BAD_LABEL;
	uint32_t			 action = TCA_INTERACT;
	int				 rc;
	int				 i;

	rc = cr_pool_create(state, &pool, false, class);
	assert_rc_equal(rc, 0);

	rc = cr_system_stop(false);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(true);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_RESET, 0, NULL, "POOL_BAD_LABEL:CIA_INTERACT");
	assert_rc_equal(rc, 0);

	cr_pool_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_RUNNING);
	assert_rc_equal(rc, 0);

	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_PENDING, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_system_stop(force);
	assert_rc_equal(rc, 0);

	rc = cr_system_start();
	assert_rc_equal(rc, 0);

	for (i = 0; i < CR_WAIT_MAX; i += 5) {
		/* Sleep for a while after system re-started under check mode. */
		sleep(5);

		cr_dci_fini(&dci);
		rc = cr_check_query(1, &pool.pool_uuid, &dci);
		if (rc == 0)
			break;

		assert_rc_equal(rc, -DER_INVAL);
	}

	rc = cr_ins_verify(&dci, TCIS_PAUSED);
	assert_rc_equal(rc, 0);

	/* Only show the old repair information. */
	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_PAUSED, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(false);
	assert_rc_equal(rc, 0);

	rc = cr_system_start();
	assert_rc_equal(rc, 0);

	cr_dci_fini(&dci);
	cr_cleanup(arg, &pool, 1);
}

/*
 * 1. Create pool.
 * 2. Fault injection to generate inconsistent pool label.
 * 3. Start checker with "-p POOL_BAD_LABEL:CIA_INTERACT".
 * 4. Query checker, it will show the interaction.
 * 5. Stop the system, that will pause the check instance.
 * 6. Start the system and query the checker, it should show 'pause' status.
 * 7. Switch to normal mode and cleanup.
 */
static void
cr_shutdown(void **state)
{
	print_message("CR10: checker shutdown\n");

	cr_pause(state, false);
}

/*
 * 1. Create pool.
 * 2. Fault injection to generate inconsistent pool label.
 * 3. Start checker with "-p POOL_BAD_LABEL:CIA_INTERACT".
 * 4. Query checker, it will show the interaction.
 * 5. Stop the system by force, that will stop the check instance without cleanup.
 * 6. Start the system and query the checker, it should show 'pause' status.
 * 7. Switch to normal mode and cleanup.
 */
static void
cr_crash(void **state)
{
	print_message("CR11: checker crash\n");

	cr_pause(state, true);
}

/*
 * 1. Create pool.
 * 2. Fault injection to make the pool as orphan.
 * 3. Set fail_loc to make check leader to be blocked after CHK__CHECK_SCAN_PHASE__CSP_POOL_LIST.
 * 4. Start checker.
 * 5. Query checker, it will show that the orphan pool has been repaired.
 * 6. Switch to normal mode that will pause the check instance.
 * 7. Start the system.
 * 8. Fault injection to make the pool as orphan again.
 * 9. Start checker again without any option.
 * 10. Query checker, it will only show the old repair information, the new orphan inconsistency
 *     should be skipped.
 * 11. Switch to normal mode.
 * 12. Verify the pool is still orphan.
 * 13. Reset fail_loc and cleanup.
 */
static void
cr_leader_resume(void **state)
{
	test_arg_t			*arg = *state;
	struct test_pool		 pool = { 0 };
	daos_mgmt_pool_info_t		 mgmt_pool = { 0 };
	struct daos_check_info		 dci = { 0 };
	uint32_t			 class = TCC_POOL_NONEXIST_ON_MS;
	uint32_t			 action = TCA_READD;
	daos_size_t			 pool_nr = 1;
	int				 rc;

	print_message("CR12: check leader resume from former stop/paused phase\n");

	rc = cr_pool_create(state, &pool, false, class);
	assert_rc_equal(rc, 0);

	rc = cr_system_stop(false);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(true);
	assert_rc_equal(rc, 0);

	/* Inject fail_loc to block pool ult and wait for the pause signal. */
	rc = cr_debug_set_params(arg, DAOS_CHK_LEADER_BLOCK | DAOS_FAIL_ALWAYS);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_RESET, 0, NULL, NULL);
	assert_rc_equal(rc, 0);

	cr_pool_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_RUNNING);
	assert_rc_equal(rc, 0);

	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_CHECKING, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(false);
	assert_rc_equal(rc, 0);

	rc = cr_system_start();
	assert_rc_equal(rc, 0);

	rc = cr_fault_inject(pool.pool_uuid, true, "CIC_POOL_NONEXIST_ON_MS");
	assert_rc_equal(rc, 0);

	rc = cr_system_stop(false);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(true);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_NONE, 0, NULL, NULL);
	assert_rc_equal(rc, 0);

	cr_ins_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_COMPLETED);
	assert_rc_equal(rc, 0);

	/* Only show the old repair information. */
	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_CHECKED, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(false);
	assert_rc_equal(rc, 0);

	rc = cr_system_start();
	assert_rc_equal(rc, 0);

	rc = dmg_pool_list(dmg_config_file, arg->group, &pool_nr, &mgmt_pool);
	assert_rc_equal(rc, 0);

	/* No pool will be found since the pool become orphan again and is not repaired. */
	assert_rc_equal(pool_nr, 0);

	/* The following is for cleanup, include the repairing of orphan pool before destroy. */

	cr_debug_set_params(arg, 0);

	rc = cr_system_stop(false);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(true);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_RESET, 0, NULL, NULL);
	assert_rc_equal(rc, 0);

	cr_ins_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_COMPLETED);
	assert_rc_equal(rc, 0);

	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_CHECKED, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(false);
	assert_rc_equal(rc, 0);

	rc = cr_system_start();
	assert_rc_equal(rc, 0);

	cr_dci_fini(&dci);
	cr_cleanup(arg, &pool, 1);
}

/*
 * 1. Create pool.
 * 2. Fault injection to generate inconsistent pool label.
 * 3. Set fail_loc to make check engine to be blocked after CHK__CHECK_SCAN_PHASE__CSP_POOL_CLEANUP.
 * 4. Start checker with option "-p POOL_BAD_LABEL:CIA_TRUST_PS".
 * 5. Query checker, it will show that the inconsistent pool label has been repaired.
 * 6. Switch to normal mode that will pause the check instance.
 * 7. Start the system.
 * 8. Fault injection to make the pool label to be inconsistent again.
 * 9. Start checker again without any option.
 * 10. Query checker, it will only show the old repair information, the new inconsistent pool label
 *     should be skipped.
 * 11. Switch to normal mode.
 * 12. Verify the pool label is still inconsistent since related phase is skipped.
 * 13. Reset fail_loc and cleanup.
 */
static void
cr_engine_resume(void **state)
{
	test_arg_t			*arg = *state;
	struct test_pool		 pool = { 0 };
	struct daos_check_info		 dci = { 0 };
	char				*label = NULL;
	uint32_t			 class = TCC_POOL_BAD_LABEL;
	uint32_t			 action = TCA_TRUST_PS;
	int				 rc;

	print_message("CR13: check engine resume from former stop/paused phase\n");

	rc = cr_pool_create(state, &pool, false, class);
	assert_rc_equal(rc, 0);

	rc = cr_system_stop(false);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(true);
	assert_rc_equal(rc, 0);

	/* Inject fail_loc to block pool ult and wait for the pause signal. */
	rc = cr_debug_set_params(arg, DAOS_CHK_LEADER_BLOCK | DAOS_FAIL_ALWAYS);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_RESET, 0, NULL, "POOL_BAD_LABEL:CIA_TRUST_PS");
	assert_rc_equal(rc, 0);

	cr_pool_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_RUNNING);
	assert_rc_equal(rc, 0);

	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_CHECKING, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(false);
	assert_rc_equal(rc, 0);

	rc = cr_system_start();
	assert_rc_equal(rc, 0);

	rc = cr_fault_inject(pool.pool_uuid, true, "CIC_POOL_BAD_LABEL");
	assert_rc_equal(rc, 0);

	rc = cr_system_stop(false);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(true);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_NONE, 0, NULL, NULL);
	assert_rc_equal(rc, 0);

	cr_ins_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_COMPLETED);
	assert_rc_equal(rc, 0);

	/* Only show the old repair information. */
	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_CHECKED, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	cr_debug_set_params(arg, 0);

	rc = cr_mode_switch(false);
	assert_rc_equal(rc, 0);

	rc = cr_system_start();
	assert_rc_equal(rc, 0);

	print_message("CR: getting label for pool " DF_UUID " after check\n",
		      DP_UUID(pool.pool_uuid));
	rc = dmg_pool_get_prop(dmg_config_file, pool.label, pool.pool_uuid, "label", &label);
	assert_rc_equal(rc, 0);

	D_ASSERTF(strcmp(label, pool.label) != 0,
			  "Pool (" DF_UUID ") label should not be repaired: %s\n",
			  DP_UUID(pool.pool_uuid), label);

	D_FREE(label);
	cr_dci_fini(&dci);
	cr_cleanup(arg, &pool, 1);
}

/*
 * 1. Create pool1 and pool2.
 * 2. Create pool1/cont1, pool2/cont2.
 * 3. Fault injection to generate inconsistent label for both pool1 and pool2.
 * 4. Fault injection to generate inconsistent label for both cont1 and cont2.
 * 5. Start checker with "POOL_BAD_LABEL:CIA_IGNORE,CONT_BAD_LABEL:CIA_INTERACT".
 * 6. Query checker, should show interaction for cont1's label and cont2's label.
 * 7. Stop checker.
 * 8. Restart checker on pool1 with "POOL_BAD_LABEL:CIA_INTERACT" and 'reset' option.
 * 9. Query checker, should show interaction for pool1's label, pool2 should be in stopped status.
 * 10. Stop checker.
 * 11. Query checker, instance should be stopped.
 * 12. Restart checker on pool2 with "POOL_BAD_LABEL:CIA_INTERACT,CONT_BAD_LABEL:CIA_INTERACT".
 * 13. Query checker, should show interaction for cont2's label.
 * 14. Stop checker and switch to normal mode.
 * 15. Cleanup.
 */
static void
cr_reset_specified(void **state)
{
	test_arg_t		*arg = *state;
	struct test_pool	 pools[2] = { 0 };
	struct test_cont	 conts[2] = { 0 };
	struct daos_check_info	 dcis[2] = { 0 };
	uint32_t		 classes[3];
	uint32_t		 actions[3];
	int			 rc;
	int			 i;

	print_message("CR14: reset checker for specified pools\n");

	/*
	 * The classes are sorted with order, otherwise the subsequent
	 * cr_pool_verify with multiple inconsistency will hit toruble.
	 */
	classes[0] = TCC_POOL_BAD_LABEL;
	classes[1] = TCC_CONT_BAD_LABEL;
	classes[2] = TCC_CONT_BAD_LABEL;
	actions[0] = TCA_IGNORE;
	actions[1] = TCA_INTERACT;
	actions[2] = TCA_INTERACT;

	for (i = 0; i < 2; i++) {
		rc = cr_pool_create(state, &pools[i], true, classes[0]);
		assert_rc_equal(rc, 0);

		rc = cr_cont_create(state, &pools[i], &conts[i], 1);
		assert_rc_equal(rc, 0);
	}

	rc = cr_system_stop(false);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(true);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_RESET, 0, NULL,
			    "POOL_BAD_LABEL:CIA_IGNORE,CONT_BAD_LABEL:CIA_INTERACT");
	assert_rc_equal(rc, 0);

	for (i = 0; i < 2; i++) {
		cr_pool_wait(1, &pools[i].pool_uuid, &dcis[i]);

		rc = cr_ins_verify(&dcis[i], TCIS_RUNNING);
		assert_rc_equal(rc, 0);

		rc = cr_pool_verify(&dcis[i], pools[i].pool_uuid, TCPS_PENDING, 2, classes, actions,
				    NULL);
		assert_rc_equal(rc, 0);
	}

	rc = cr_check_stop(0, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_RESET, 1, &pools[0].pool_uuid, "POOL_BAD_LABEL:CIA_INTERACT");
	assert_rc_equal(rc, 0);

	cr_pool_wait(1, &pools[0].pool_uuid, &dcis[0]);

	rc = cr_ins_verify(&dcis[0], TCIS_RUNNING);
	assert_rc_equal(rc, 0);

	/* Pool1's report is for pool label interaction. */
	rc = cr_pool_verify(&dcis[0], pools[0].pool_uuid, TCPS_PENDING, 1, &classes[0], &actions[1],
			    NULL);
	assert_rc_equal(rc, 0);

	cr_dci_fini(&dcis[1]);
	rc = cr_check_query(1,  &pools[1].pool_uuid, &dcis[1]);
	assert_rc_equal(rc, 0);

	/* Pool2's (old) report should be still there. */
	rc = cr_pool_verify(&dcis[1], pools[1].pool_uuid, TCPS_STOPPED, 2, classes, actions, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_check_stop(0, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_NONE, 1, &pools[1].pool_uuid,
			    "POOL_BAD_LABEL:CIA_INTERACT,CONT_BAD_LABEL:CIA_INTERACT");
	assert_rc_equal(rc, 0);

	cr_pool_wait(1, &pools[1].pool_uuid, &dcis[1]);

	rc = cr_ins_verify(&dcis[1], TCIS_RUNNING);
	assert_rc_equal(rc, 0);

	/* There are 3 reports for pool2: two are old (since not reset), another one is new. */
	rc = cr_pool_verify(&dcis[1], pools[1].pool_uuid, TCPS_PENDING, 3, classes, actions, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_check_stop(0, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(false);
	assert_rc_equal(rc, 0);

	rc = cr_system_start();
	assert_rc_equal(rc, 0);

	for (i = 0; i < 2; i++)
		cr_dci_fini(&dcis[i]);
	cr_cleanup(arg, pools, 2);
}

/*
 * 1. Create pool.
 * 2. Fault injection to generate inconsistent pool label.
 * 3. Set fail_loc to fail pool label update.
 * 4. Start checker with option "--failout=on" and "POOL_BAD_LABEL:CIA_TRUST_PS".
 * 5. Query checker, instance should failed, pool should be "failed".
 * 6. Restart checker with option "--reset --failout=off" and "POOL_BAD_LABEL:CIA_TRUST_PS".
 * 7. Query checker, pool should be "checked" with failed inconsistency repair report.
 * 8. Reset fail_loc.
 * 9. Switch to normal mode to verify the pool label.
 * 10. Cleanup.
 */
static void
cr_failout(void **state)
{
	test_arg_t			*arg = *state;
	struct test_pool		 pool = { 0 };
	struct daos_check_info		 dci = { 0 };
	char				*label = NULL;
	uint32_t			 class = TCC_POOL_BAD_LABEL;
	uint32_t			 action = TCA_TRUST_PS;
	int				 result = -DER_IO;
	int				 rc;

	print_message("CR15: check start option - failout\n");

	rc = cr_pool_create(state, &pool, false, class);
	assert_rc_equal(rc, 0);

	rc = cr_system_stop(false);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(true);
	assert_rc_equal(rc, 0);

	/* Inject fail_loc to fail pool label repair. */
	rc = cr_debug_set_params(arg, DAOS_CHK_LEADER_FAIL_REGPOOL | DAOS_FAIL_ALWAYS);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_FAILOUT | TCSF_RESET, 0, NULL, "POOL_BAD_LABEL:CIA_TRUST_PS");
	assert_rc_equal(rc, 0);

	cr_ins_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_FAILED);
	assert_rc_equal(rc, 0);

	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_FAILED, 1, &class, &action, &result);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_RESET | TCSF_NO_FAILOUT, 0, NULL, "POOL_BAD_LABEL:CIA_TRUST_PS");
	assert_rc_equal(rc, 0);

	cr_ins_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_COMPLETED);
	assert_rc_equal(rc, 0);

	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_CHECKED, 1, &class, &action, &result);
	assert_rc_equal(rc, 0);

	cr_debug_set_params(arg, 0);

	rc = cr_mode_switch(false);
	assert_rc_equal(rc, 0);

	rc = cr_system_start();
	assert_rc_equal(rc, 0);

	print_message("CR: getting label for pool " DF_UUID " after check\n",
		      DP_UUID(pool.pool_uuid));
	rc = dmg_pool_get_prop(dmg_config_file, pool.label, pool.pool_uuid, "label", &label);
	assert_rc_equal(rc, 0);

	D_ASSERTF(strcmp(label, pool.label) != 0,
			  "Pool (" DF_UUID ") label should not be repaired: %s\n",
			  DP_UUID(pool.pool_uuid), label);

	D_FREE(label);
	cr_dci_fini(&dci);
	cr_cleanup(arg, &pool, 1);
}

/*
 * 1. Create pool and cont.
 * 2. Fault injection to generate empty label for the container property.
 * 3. Start checker with option "--auto=on -p CONT_BAD_LABEL:CIA_TRUST_TARGET".
 * 4. For bad container label, if the trusted label is empty, then need interaction by default,
 *    but under auto mode, it will be ignored.
 * 5. Query checker, should be completed, inconsistent container label should be "ignored".
 * 6. Restart checker with option "--reset --auto=off" and "-p CONT_BAD_LABEL:CIA_TRUST_TARGET".
 * 7. Query checker, it will show the interaction for the inconsistent container label.
 * 8. Switch to normal mode and cleanup.
 */
static void
cr_auto_repair(void **state)
{
	test_arg_t			*arg = *state;
	struct test_pool		 pool = { 0 };
	struct test_cont		 cont = { 0 };
	struct daos_check_info		 dci = { 0 };
	uint32_t			 class = TCC_CONT_BAD_LABEL;
	uint32_t			 action;
	int				 rc;

	print_message("CR16: check start option - auto repair\n");

	rc = cr_pool_create(state, &pool, true, TCC_NONE);
	assert_rc_equal(rc, 0);

	rc = cr_cont_create(state, &pool, &cont, -1);
	assert_rc_equal(rc, 0);

	rc = cr_system_stop(false);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(true);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_AUTO | TCSF_RESET, 0, NULL, "CONT_BAD_LABEL:CIA_TRUST_TARGET");
	assert_rc_equal(rc, 0);

	cr_ins_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_COMPLETED);
	assert_rc_equal(rc, 0);

	action = TCA_IGNORE;
	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_CHECKED, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_RESET | TCSF_NO_AUTO, 0, NULL, "CONT_BAD_LABEL:CIA_TRUST_TARGET");
	assert_rc_equal(rc, 0);

	cr_pool_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_RUNNING);
	assert_rc_equal(rc, 0);

	action = TCA_INTERACT;
	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_PENDING, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(false);
	assert_rc_equal(rc, 0);

	rc = cr_system_start();
	assert_rc_equal(rc, 0);

	cr_dci_fini(&dci);
	cr_cleanup(arg, &pool, 1);
}

/*
 * 1. Create pool1 and pool2.
 * 2. Fault injection to make pool2 as orphan.
 * 3. Start checker on pool1 without any option.
 * 4. Query checker, no inconsistency should be reported.
 * 5. Restart checker on pool1 with option "-O".
 * 6. Query checker, it should find out the orphan pool2 and repair it.
 * 7. Switch to normal mode to verify the pools.
 * 8. Cleanup.
 */
static void
cr_orphan_pool(void **state)
{
	test_arg_t			*arg = *state;
	struct test_pool		 pools[2] = { 0 };
	daos_mgmt_pool_info_t		 mgmt_pools[2] = { 0 };
	struct daos_check_info		 dci = { 0 };
	uint32_t			 class = TCC_POOL_NONEXIST_ON_MS;
	uint32_t			 action = TCA_READD;
	daos_size_t			 pool_nr = 2;
	int				 rc;

	print_message("CR17: check start option - scan orphan pools by force\n");

	rc = cr_pool_create(state, &pools[0], false, TCC_NONE);
	assert_rc_equal(rc, 0);

	rc = cr_pool_create(state, &pools[1], false, class);
	assert_rc_equal(rc, 0);

	rc = cr_system_stop(false);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(true);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_RESET, 1, &pools[0].pool_uuid, NULL);
	assert_rc_equal(rc, 0);

	cr_ins_wait(1, &pools[0].pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_COMPLETED);
	assert_rc_equal(rc, 0);

	rc = cr_pool_verify(&dci, pools[0].pool_uuid, TCPS_CHECKED, 0, NULL, NULL, NULL);
	assert_rc_equal(rc, 0);

	cr_pool_wait(1, &pools[1].pool_uuid, &dci);

	rc = cr_pool_verify(&dci, pools[1].pool_uuid, TCPS_UNCHECKED, 0, NULL, NULL, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_ORPHAN, 1, &pools[0].pool_uuid, NULL);
	assert_rc_equal(rc, 0);

	cr_ins_wait(1, &pools[1].pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_COMPLETED);
	assert_rc_equal(rc, 0);

	rc = cr_pool_verify(&dci, pools[1].pool_uuid, TCPS_CHECKED, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(false);
	assert_rc_equal(rc, 0);

	rc = cr_system_start();
	assert_rc_equal(rc, 0);

	rc = dmg_pool_list(dmg_config_file, arg->group, &pool_nr, mgmt_pools);
	assert_rc_equal(rc, 0);

	assert_rc_equal(pool_nr, 2);

	cr_dci_fini(&dci);
	clean_pool_info(2, mgmt_pools);
	cr_cleanup(arg, pools, 2);
}

static void
cr_fail_ps_sync(void **state, bool leader)
{
	test_arg_t			*arg = *state;
	struct test_pool		 pool = { 0 };
	struct daos_check_info		 dci = { 0 };
	uint32_t			 class = TCC_POOL_BAD_LABEL;
	uint32_t			 action = TCA_TRUST_PS;
	uint32_t			 fail_loc;
	int				 rc;

	rc = cr_pool_create(state, &pool, false, class);
	assert_rc_equal(rc, 0);

	rc = cr_system_stop(false);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(true);
	assert_rc_equal(rc, 0);

	if (leader)
		fail_loc = DAOS_CHK_PS_NOTIFY_LEADER;
	else
		fail_loc = DAOS_CHK_PS_NOTIFY_ENGINE;

	/* Inject fail_loc to skip notification from PS leader to check leader or pool shards. */
	rc = cr_debug_set_params(arg, fail_loc | DAOS_FAIL_ALWAYS);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_RESET, 0, NULL, "POOL_BAD_LABEL:CIA_TRUST_PS");
	assert_rc_equal(rc, 0);

	/* The pool wait will timeout since failed to notify some check engine/leader when done. */
	cr_pool_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_RUNNING);
	assert_rc_equal(rc, 0);

#if 0
	/* Disable the check because of DAOS-13989. */
	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_CHECKING, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);
#endif

	/* Start checker should fail since some check leader/engines are still running. */
	rc = cr_check_start(TCSF_NONE, 0, NULL, NULL);
	assert_rc_equal(rc, -DER_ALREADY);

	/* The pool wait will timeout. */
	cr_pool_wait(1, &pool.pool_uuid, &dci);

	/* Current running instance should not be affected by above failed check start. */
	rc = cr_ins_verify(&dci, TCIS_RUNNING);
	assert_rc_equal(rc, 0);

#if 0
	/* Disable the check because of DAOS-13989. */
	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_CHECKING, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);
#endif

	rc = cr_check_stop(0, NULL);
	assert_rc_equal(rc, 0);

	cr_ins_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_STOPPED);
	assert_rc_equal(rc, 0);

	cr_debug_set_params(arg, 0);

	rc = cr_check_start(TCSF_NONE, 0, NULL, NULL);
	assert_rc_equal(rc, 0);

	cr_ins_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_COMPLETED);
	assert_rc_equal(rc, 0);

	if (leader)
		/* The instance is resumed, so still hold former inconsistency report. */
		rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_CHECKED, 1, &class, &action, NULL);
	else
		/* Instance is reset automatically, old inconsistency report should have been discarded. */
		rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_CHECKED, 0, NULL, NULL, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(false);
	assert_rc_equal(rc, 0);

	rc = cr_system_start();
	assert_rc_equal(rc, 0);

	cr_dci_fini(&dci);
	cr_cleanup(arg, &pool, 1);
}

/*
 * 1. Create pool.
 * 2. Fault injection to generate inconsistent pool label.
 * 3. Set fail_loc to simulate PS leader failed to notify status update to check leader.
 * 4. Start checker with option "-p POOL_BAD_LABEL:CIA_TRUST_PS".
 * 5. Query checker, the instance should be in running with pool label repaired, although all
 *    engines have completed.
 * 6. Restart checker should fail since leader is still running.
 * 7. Query checker, the instance should still be in running, not stopped for the failed restart.
 * 8. Stop checker.
 * 9. Reset fail_loc.
 * 10. Restart checker without any option. The leader should resume from stopped point,
 *     engines will notify the completion.
 * 11. Query checker, it should be completed without repeatedly repairing the pool label.
 * 12. Switch to normal mode and cleanup.
 */
static void
cr_fail_sync_leader(void **state)
{
	print_message("CR18: PS leader fails to sync pool status with check leader\n");

	cr_fail_ps_sync(state, true);
}

/*
 * 1. Create pool.
 * 2. Fault injection to generate inconsistent pool label.
 * 3. Set fail_loc to simulate PS leader failed to notify status update to pool shards.
 * 4. Start checker with option "-p POOL_BAD_LABEL:CIA_TRUST_PS".
 * 5. Query checker, the instance should be in running, although the leader is already completed.
 * 6. Restart checker should fail since some engines are still running.
 * 7. Query checker, the instance should still be in running, not stopped for the failed restart.
 * 8. Stop checker.
 * 9. Reset fail_loc.
 * 10. Restart checker without any option. The leader instance will reset automatically since former
 *     leader was completed. Then the engines will be also reset accordingly.
 * 11. Query checker, it should be completed without repeatedly repairing the pool label.
 * 12. Switch to normal mode and cleanup.
 */
static void
cr_fail_sync_engine(void **state)
{
	print_message("CR19: PS leader fails to sync pool status with check engines\n");

	cr_fail_ps_sync(state, false);
}

/*
 * 1. Create pool.
 * 2. Fault injection to generate inconsistent pool label.
 * 3. Start checker with option "-p POOL_BAD_LABEL:CIA_INTERACT".
 * 4. Query checker, it should show the interaction.
 * 5. Stop some rank in the system.
 * 6. Check repair with trust MS to repair the pool label.
 * 7. Query checker, instance should be completed, the pool label should has been repaired.
 * 8. Switch to normal mode to verify the pool label.
 * 9. Cleanup.
 */
static void
cr_engine_death(void **state)
{
	test_arg_t			*arg = *state;
	struct test_pool		 pool = { 0 };
	struct daos_check_info		 dci = { 0 };
	struct daos_check_report_info	*dcri;
	char				*label = NULL;
	uint32_t			 class = TCC_POOL_BAD_LABEL;
	uint32_t			 action;
	int				 rank = -1;
	int				 rc;
	int				 i;

	print_message("CR20: check engine death during check\n");

	rc = cr_pool_create_with_svc(state, &pool, true, class);
	assert_rc_equal(rc, 0);

	rc = cr_system_stop(false);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(true);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_RESET, 0, NULL, "POOL_BAD_LABEL:CIA_INTERACT");
	assert_rc_equal(rc, 0);

	cr_pool_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_RUNNING);
	assert_rc_equal(rc, 0);

	action = TCA_INTERACT;
	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_PENDING, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_rank_exclude(arg, &pool, &rank, true);
	if (rc > 0)
		goto cleanup;
	assert_rc_equal(rc, 0);

	dcri = cr_locate_dcri(&dci, NULL, pool.pool_uuid);
	action = TCA_TRUST_MS;
	rc = -DER_MISC;

	for (i = 0; i < dcri->dcri_option_nr; i++) {
		if (dcri->dcri_options[i] == action) {
			/* Repair the pool label with the lost rank. */
			rc = cr_check_repair(dcri->dcri_seq, i, false);
			break;
		}
	}
	assert_rc_equal(rc, 0);

	cr_ins_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_COMPLETED);
	assert_rc_equal(rc, 0);

	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_CHECKED, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	/* Reint the rank for subsequent test. */
	rc = cr_rank_reint(rank, false);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(false);
	assert_rc_equal(rc, 0);

	rc = cr_system_start();
	assert_rc_equal(rc, 0);

	print_message("CR: getting label for pool " DF_UUID " after check\n",
		      DP_UUID(pool.pool_uuid));
	rc = dmg_pool_get_prop(dmg_config_file, pool.label, pool.pool_uuid, "label", &label);
	assert_rc_equal(rc, 0);

	D_ASSERTF(strcmp(label, pool.label) != 0,
			  "Pool (" DF_UUID ") label should not be repaired: %s\n",
			  DP_UUID(pool.pool_uuid), label);

	D_FREE(label);
	cr_dci_fini(&dci);

cleanup:
	cr_cleanup(arg, &pool, 1);
}

/*
 * 1. Create pool.
 * 2. Fault injection to make the pool as orphan.
 * 3. Start checker with option "-p POOL_NONEXIST_ON_MS:CIA_INTERACT".
 * 4. Query checker, it should show the interaction.
 * 5. Stop some rank in the system.
 * 6. Start the rank that is stopped just now - rejoin succeed.
 * 7. Query checker, it should still wait for the interaction.
 * 8. Check repair with destroying the orphan pool.
 * 9. Query checker, instance should be completed, the pool should has been destroyed.
 * 10. Restart checker with option "--reset".
 * 11. Query checker, it should complete without any inconsistency reported.
 * 12. Switch to normal mode and cleanup.
 */
static void
cr_engine_rejoin_succ(void **state)
{
	test_arg_t			*arg = *state;
	struct test_pool		 pool = { 0 };
	struct daos_check_info		 dci = { 0 };
	struct daos_check_report_info	*dcri;
	uint32_t			 class = TCC_POOL_NONEXIST_ON_MS;
	uint32_t			 action;
	int				 rank = -1;
	int				 rc;
	int				 i;

	print_message("CR21: check engine rejoins check instance successfully\n");

	rc = cr_pool_create_with_svc(state, &pool, true, class);
	assert_rc_equal(rc, 0);

	rc = cr_system_stop(false);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(true);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_RESET, 0, NULL, "POOL_NONEXIST_ON_MS:CIA_INTERACT");
	assert_rc_equal(rc, 0);

	cr_pool_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_RUNNING);
	assert_rc_equal(rc, 0);

	action = TCA_INTERACT;
	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_PENDING, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_rank_exclude(arg, &pool, &rank, false);
	if (rc > 0)
		goto cleanup;
	assert_rc_equal(rc, 0);

	/* Reint the rank immediately before the rank death event being detected. */
	rc = cr_rank_reint(rank, true);
	assert_rc_equal(rc, 0);

	cr_pool_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_RUNNING);
	assert_rc_equal(rc, 0);

	/* Still wait for the interaction. */
	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_PENDING, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	dcri = cr_locate_dcri(&dci, NULL, pool.pool_uuid);
	action = TCA_DISCARD;
	rc = -DER_MISC;

	for (i = 0; i < dcri->dcri_option_nr; i++) {
		if (dcri->dcri_options[i] == action) {
			rc = cr_check_repair(dcri->dcri_seq, i, false);
			break;
		}
	}
	assert_rc_equal(rc, 0);

	cr_ins_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_COMPLETED);
	assert_rc_equal(rc, 0);

	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_CHECKED, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_RESET, 0, NULL, NULL);
	assert_rc_equal(rc, 0);

	cr_ins_wait(0, NULL, &dci);

	rc = cr_ins_verify(&dci, TCIS_COMPLETED);
	assert_rc_equal(rc, 0);

	/* Neither pools nor inconsistency reports. */
	D_ASSERTF(dci.dci_pool_nr == 0, "The pool " DF_UUID "was not destroyed completedly (%d)\n",
		  DP_UUID(pool.pool_uuid), dci.dci_pool_nr);

	rc = cr_mode_switch(false);
	assert_rc_equal(rc, 0);

	rc = cr_system_start();
	assert_rc_equal(rc, 0);

	cr_dci_fini(&dci);

cleanup:
	cr_cleanup(arg, &pool, 1);
}

/*
 * 1. Create pool.
 * 2. Fault injection to make the pool as orphan.
 * 3. Start checker with option "-p POOL_NONEXIST_ON_MS:CIA_INTERACT".
 * 4. Query checker, it should show the interaction.
 * 5. Stop some rank in the system.
 * 6. Check repair with destroying the orphan pool, that should fail since we lost some pool shards
 *    during the check.
 * 7. Query checker, the instance should be completed, the pool should be failed.
 * 8. Start the rank that is stopped just now - rejoin failed since the former checker instance has
 *    already completed.
 * 9. Restart checker with option "--reset" and
 *    "POOL_LESS_SVC_WITHOUT_QUORUM:CIA_DISCARD,POOL_NONEXIST_ON_MS:CIA_DISCARD".
 * 10. Query checker, it should complete with the orphan pool destroyed.
 * 11. Switch to normal mode and cleanup.
 */
static void
cr_engine_rejoin_fail(void **state)
{
	test_arg_t			*arg = *state;
	struct test_pool		 pool = { 0 };
	struct daos_check_info		 dci = { 0 };
	struct daos_check_report_info	*dcri;
	uint32_t			 class = TCC_POOL_NONEXIST_ON_MS;
	uint32_t			 action;
	int				 rank = -1;
	int				 result;
	int				 rc;
	int				 i;

	print_message("CR22: check engine fails to rejoin check instance\n");

	rc = cr_pool_create_with_svc(state, &pool, true, class);
	assert_rc_equal(rc, 0);

	rc = cr_system_stop(false);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(true);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_RESET, 0, NULL, "POOL_NONEXIST_ON_MS:CIA_INTERACT");
	assert_rc_equal(rc, 0);

	cr_pool_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_RUNNING);
	assert_rc_equal(rc, 0);

	action = TCA_INTERACT;
	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_PENDING, 1, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_rank_exclude(arg, &pool, &rank, true);
	if (rc > 0)
		goto cleanup;
	assert_rc_equal(rc, 0);

	/* Destroy the pool, then related shard will be left on the stopped rank. */
	dcri = cr_locate_dcri(&dci, NULL, pool.pool_uuid);
	action = TCA_DISCARD;
	rc = -DER_MISC;

	for (i = 0; i < dcri->dcri_option_nr; i++) {
		if (dcri->dcri_options[i] == action) {
			/* Repair the inconsistency with the lost rank. */
			rc = cr_check_repair(dcri->dcri_seq, i, false);
			break;
		}
	}
	assert_rc_equal(rc, 0);

	cr_ins_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_COMPLETED);
	assert_rc_equal(rc, 0);

	/* The check on the pool will fail as -DER_HG or -DER_TIMEDOUT. */
	result = -DER_HG;
	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_FAILED, 1, &class, &action, &result);
	if (rc == -DER_INVAL) {
		result = -DER_TIMEDOUT;
		rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_FAILED, 1, &class, &action, &result);
	}
	assert_rc_equal(rc, 0);

	/* Reint the rank, rejoin will fail but not affect the rank start. */
	rc = cr_rank_reint(rank, true);
	assert_rc_equal(rc, 0);

	/* Wait for a while until the control plane to be ready for new check start. */
	cr_pool_wait(1, &pool.pool_uuid, &dci);

	rc = cr_check_start(TCSF_RESET, 0, NULL,
			    "POOL_LESS_SVC_WITHOUT_QUORUM:CIA_DISCARD,POOL_NONEXIST_ON_MS:CIA_DISCARD");
	assert_rc_equal(rc, 0);

	cr_ins_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_COMPLETED);
	assert_rc_equal(rc, 0);

	/* Some pool shards may have been destroyed, the left ones may have (or not) quorum. */
	class = TCC_POOL_LESS_SVC_WITHOUT_QUORUM;
	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_CHECKED, 1, &class, &action, NULL);
	if (rc == -DER_INVAL) {
		class = TCC_POOL_NONEXIST_ON_MS;
		rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_CHECKED, 1, &class, &action, NULL);
	}
	assert_rc_equal(rc, 0);

	/* The former excluded rank is not in the check ranks set, stop it explicitly. */
	rc = dmg_system_stop_rank(dmg_config_file, rank, false);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(false);
	assert_rc_equal(rc, 0);

	rc = cr_system_start();
	assert_rc_equal(rc, 0);

	cr_dci_fini(&dci);

cleanup:
	cr_cleanup(arg, &pool, 1);
}
/*
 * 1. Create pool1, pool2, pool3 and pool4. Create container under each of them.
 * 2. Fault injection to generate inconsistent pool label for pool1 and pool2, inconsistent
 *    container label for pool3/cont and pool4/cont.
 * 3. Set checker policies as all-interactive.
 * 4. Start checker on pool1 and pool3.
 * 5. Query checker, should show interaction.
 * 6. Stop checker on pool1.
 * 7. Start checker on pool2, should fail since former checker is still running for pool3.
 * 8. Check repair pool3/cont's label.
 * 9. Query checker, it should be completed, pool3/cont's label should have been fixed.
 * 10. Restart checker on pool1 (from stopped point) and pool2 (from beginning).
 * 11. Query checker, should show interaction.
 * 12. Stop checker on all pools.
 * 13. Query checker, should show stopped.
 * 14. Restart checker without any option, resume former check for pool1 and pool2.
 * 15. Check repair all reported inconsistency.
 * 16. Query checker, it should be completed.
 * 17. Restart checker without any option, it should check all pools.
 * 18. Query checker, it should be running, only pool4/cont's bad label needs interaction.
 * 19. Check repair pool4/cont's bad label.
 * 20. Query checker, it should be completed.
 * 21. Switch to normal mode and cleanup.
 */
static void
cr_multiple_pools(void **state)
{
	test_arg_t			*arg = *state;
	struct test_pool		 pools[4] = { 0 };
	struct test_cont		 conts[4] = { 0 };
	uuid_t				 uuids[4] = { 0 };
	struct daos_check_info		 dci = { 0 };
	struct daos_check_report_info	*dcri;
	uint32_t			 classes[2];
	uint32_t			 actions[3];
	int				 rc;
	int				 i;
	int				 j;

	print_message("CR23: control multiple pools check start/stop sequence\n");

	classes[0] = TCC_POOL_BAD_LABEL;
	classes[1] = TCC_CONT_BAD_LABEL;
	actions[0] = TCA_TRUST_MS;
	actions[1] = TCA_TRUST_PS;
	actions[2] = TCA_INTERACT;

	for (i = 0; i < 4; i++) {
		rc = cr_pool_create(state, &pools[i], true, i < 2 ? classes[0] : TCC_NONE);
		assert_rc_equal(rc, 0);

		rc = cr_cont_create(state, &pools[i], &conts[i], i < 2 ? 0 : 1);
		assert_rc_equal(rc, 0);
	}

	rc = cr_system_stop(false);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(true);
	assert_rc_equal(rc, 0);

	rc = cr_check_set_policy(TCPF_INTERACT, NULL);
	assert_rc_equal(rc, 0);

	uuid_copy(uuids[0], pools[0].pool_uuid);
	uuid_copy(uuids[1], pools[2].pool_uuid);

	rc = cr_check_start(TCSF_RESET, 2, uuids, NULL);
	assert_rc_equal(rc, 0);

	cr_pool_wait(1, &uuids[1], &dci);

	rc = cr_ins_verify(&dci, TCIS_RUNNING);
	assert_rc_equal(rc, 0);

	rc = cr_pool_verify(&dci, uuids[1], TCPS_PENDING, 1, &classes[1], &actions[2], NULL);
	assert_rc_equal(rc, 0);

	rc = cr_check_stop(1, &uuids[0]);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_NONE, 1, &pools[1].pool_uuid, NULL);
	assert_rc_equal(rc, -DER_ALREADY);

	dcri = cr_locate_dcri(&dci, NULL, uuids[1]);
	rc = -DER_MISC;

	for (i = 0; i < dcri->dcri_option_nr; i++) {
		if (dcri->dcri_options[i] == actions[1]) {
			rc = cr_check_repair(dcri->dcri_seq, i, false);
			break;
		}
	}
	assert_rc_equal(rc, 0);

	cr_ins_wait(1, &uuids[1], &dci);

	rc = cr_ins_verify(&dci, TCIS_COMPLETED);
	assert_rc_equal(rc, 0);

	rc = cr_pool_verify(&dci, uuids[1], TCPS_CHECKED, 1, &classes[1], &actions[1], NULL);
	assert_rc_equal(rc, 0);

	uuid_copy(uuids[1], pools[1].pool_uuid);

	rc = cr_check_start(TCSF_NONE, 2, uuids, NULL);
	assert_rc_equal(rc, 0);

	cr_pool_wait(1, &uuids[1], &dci);

	rc = cr_ins_verify(&dci, TCIS_RUNNING);
	assert_rc_equal(rc, 0);

	rc = cr_pool_verify(&dci, uuids[1], TCPS_PENDING, 1, &classes[0], &actions[2], NULL);
	assert_rc_equal(rc, 0);

	rc = cr_check_stop(0, NULL);
	assert_rc_equal(rc, 0);

	cr_ins_wait(1, &uuids[1], &dci);

	rc = cr_ins_verify(&dci, TCIS_STOPPED);
	assert_rc_equal(rc, 0);

	rc = cr_pool_verify(&dci, uuids[1], TCPS_STOPPED, 1, &classes[0], &actions[2], NULL);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_NONE, 2, uuids, NULL);
	assert_rc_equal(rc, 0);

	for (i = 0; i < 2; i++) {
		cr_pool_wait(1, &uuids[i], &dci);

		rc = cr_ins_verify(&dci, TCIS_RUNNING);
		assert_rc_equal(rc, 0);

		rc = cr_pool_verify(&dci, uuids[i], TCPS_PENDING, 1, &classes[0], &actions[2], NULL);
		assert_rc_equal(rc, 0);

		dcri = NULL;
		rc = -DER_MISC;

again:
		dcri = cr_locate_dcri(&dci, dcri, uuids[i]);
		for (j = 0; j < dcri->dcri_option_nr; j++) {
			if (dcri->dcri_options[j] == actions[0]) {
				rc = cr_check_repair(dcri->dcri_seq, j, false);
				break;
			}
		}

		/*
		 * Because of DAOS-13205, the inconsistency report may contain stale information,
		 * let's try next one.
		 */
		if (rc != 0)
			goto again;
	}

	cr_ins_wait(0, NULL, &dci);

	rc = cr_ins_verify(&dci, TCIS_COMPLETED);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_NONE, 0, NULL, NULL);
	assert_rc_equal(rc, 0);

	for (i = 0; i < 4; i++) {
		cr_pool_wait(1, &pools[i].pool_uuid, &dci);

		rc = cr_ins_verify(&dci, TCIS_RUNNING);
		assert_rc_equal(rc, 0);

		if (i < 3)
			rc = cr_pool_verify(&dci, pools[i].pool_uuid, TCPS_CHECKED, 0, NULL, NULL,
					    NULL);
		else
			rc = cr_pool_verify(&dci, pools[i].pool_uuid, TCPS_PENDING, 1, &classes[1],
					    &actions[2], NULL);
		assert_rc_equal(rc, 0);
	}

	dcri = cr_locate_dcri(&dci, NULL, pools[3].pool_uuid);
	rc = -DER_MISC;

	for (i = 0; i < dcri->dcri_option_nr; i++) {
		if (dcri->dcri_options[i] == actions[1]) {
			rc = cr_check_repair(dcri->dcri_seq, i, false);
			break;
		}
	}
	assert_rc_equal(rc, 0);

	cr_ins_wait(0, NULL, &dci);

	rc = cr_ins_verify(&dci, TCIS_COMPLETED);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(false);
	assert_rc_equal(rc, 0);

	rc = cr_system_start();
	assert_rc_equal(rc, 0);

	cr_dci_fini(&dci);
	cr_cleanup(arg, pools, 4);
}

/*
 * 1. Create pool.
 * 2. Set fail_loc to bypass notification about orphan process to check engines.
 * 3. Start checker without any option.
 * 4. Query checker, it should be completed.
 * 5. Switch to normal mode and cleanup.
 */
static void
cr_fail_sync_orphan(void **state)
{
	test_arg_t		*arg = *state;
	struct test_pool	 pool = { 0 };
	struct daos_check_info	 dci = { 0 };
	int			 rc;

	print_message("CR24: check leader failed to notify check engine about orphan process\n");

	rc = cr_pool_create(state, &pool, false, TCC_NONE);
	assert_rc_equal(rc, 0);

	rc = cr_system_stop(false);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(true);
	assert_rc_equal(rc, 0);

	/* Inject fail_loc to bypass notification about orphan process to check engines. */
	rc = cr_debug_set_params(arg, DAOS_CHK_SYNC_ORPHAN_PROCESS | DAOS_FAIL_ALWAYS);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_RESET, 0, NULL, NULL);
	assert_rc_equal(rc, 0);

	cr_ins_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_COMPLETED);
	assert_rc_equal(rc, 0);

	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_CHECKED, 0, NULL, NULL, NULL);
	assert_rc_equal(rc, 0);

	/* Check leader may be completed earlier than check engines in this case, double check. */
	cr_ins_wait(0, NULL, &dci);

	rc = cr_ins_verify(&dci, TCIS_COMPLETED);
	assert_rc_equal(rc, 0);

	cr_debug_set_params(arg, 0);

	rc = cr_mode_switch(false);
	assert_rc_equal(rc, 0);

	rc = cr_system_start();
	assert_rc_equal(rc, 0);

	cr_dci_fini(&dci);
	cr_cleanup(arg, &pool, 1);
}

/*
 * 1. Create pool1 and pool2.
 * 2. Fault injection to make inconsistent label for both of them.
 * 3. Start checker on pool1 and pool2 with POOL_BAD_LABEL:CIA_INTERACT
 * 4. Query checker, should show interaction for both pool1 and pool2.
 * 5. Check repair pool2's label with trust PS (trust MS is the default) and "for-all" option.
 * 6. Query checker, both pool1's and pool2's label should be fixed with trust PS.
 * 7. Switch to normal mode and verify pools' labels.
 * 8. Cleanup.
 */
static void
cr_inherit_policy(void **state)
{
	test_arg_t			*arg = *state;
	struct test_pool		 pools[2] = { 0 };
	struct daos_check_info		 dci = { 0 };
	struct daos_check_report_info	*dcri;
	char				*ps_label = NULL;
	char				*ptr;
	char				 ms_label[DAOS_PROP_LABEL_MAX_LEN];
	uint32_t			 class = TCC_POOL_BAD_LABEL;
	uint32_t			 action;
	int				 rc;
	int				 i;

	print_message("CR25: inherit check policy from former check repair\n");

	for (i = 0; i < 2; i++) {
		rc = cr_pool_create(state, &pools[i], false, class);
		assert_rc_equal(rc, 0);
	}

	rc = cr_system_stop(false);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(true);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_RESET, 0, NULL, "POOL_BAD_LABEL:CIA_INTERACT");
	assert_rc_equal(rc, 0);

	for (i = 0; i < 2; i++) {
		cr_pool_wait(1, &pools[i].pool_uuid, &dci);

		rc = cr_ins_verify(&dci, TCIS_RUNNING);
		assert_rc_equal(rc, 0);

		action = TCA_INTERACT;
		rc = cr_pool_verify(&dci, pools[i].pool_uuid, TCPS_PENDING, 1, &class, &action,
				    NULL);
		assert_rc_equal(rc, 0);
	}

	dcri = cr_locate_dcri(&dci, NULL, pools[1].pool_uuid);
	action = TCA_TRUST_PS;
	rc = -DER_MISC;

	for (i = 0; i < dcri->dcri_option_nr; i++) {
		if (dcri->dcri_options[i] == action) {
			rc = cr_check_repair(dcri->dcri_seq, i, true);
			break;
		}
	}
	assert_rc_equal(rc, 0);

	for (i = 0; i < 2; i++) {
		cr_ins_wait(1, &pools[i].pool_uuid, &dci);

		rc = cr_ins_verify(&dci, TCIS_COMPLETED);
		assert_rc_equal(rc, 0);

		rc = cr_pool_verify(&dci, pools[i].pool_uuid, TCPS_CHECKED, 1, &class, &action,
				    NULL);
		assert_rc_equal(rc, 0);
	}

	rc = cr_mode_switch(false);
	assert_rc_equal(rc, 0);

	rc = cr_system_start();
	assert_rc_equal(rc, 0);

	for (i = 0; i < 2; i++) {
		/* The last 6 characters of pools[i].label is '-fault'. */
		ptr = strrchr(pools[i].label, '-');
		assert_non_null(ptr);

		memcpy(ms_label, pools[i].label, ptr - pools[i].label);
		ms_label[ptr - pools[i].label] = '\0';

		print_message("CR: getting label for pool " DF_UUID " after check\n",
			      DP_UUID(pools[i].pool_uuid));
		rc = dmg_pool_get_prop(dmg_config_file, ms_label, pools[i].pool_uuid, "label",
				       &ps_label);
		assert_rc_equal(rc, 0);

		D_ASSERTF(strcmp(ps_label, ms_label) == 0,
			  "Pool (" DF_UUID ") label is not repaired: %s vs %s\n",
			  DP_UUID(pools[i].pool_uuid), ps_label, ms_label);
		D_FREE(ps_label);
	}

	cr_dci_fini(&dci);
	cr_cleanup(arg, pools, 2);
}

/*
 * 1. Create pool without inconsistency.
 * 2. Set fail_loc to simulate some engine failed to report pool shard when start checker.
 * 3. Start checker without options.
 * 4. Query checker, it should be completed, but the check for the pool should be failed.
 * 5. Switch to normal mode and cleanup.
 */
static void
cr_handle_fail_pool1(void **state)
{
	test_arg_t		*arg = *state;
	struct test_pool	 pool = { 0 };
	struct daos_check_info	 dci = { 0 };
	int			 rc;

	print_message("CR26: skip the pool if some engine failed to report some pool shard\n");

	rc = cr_pool_create(state, &pool, false, TCC_NONE);
	assert_rc_equal(rc, 0);

	rc = cr_system_stop(false);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(true);
	assert_rc_equal(rc, 0);

	rc = cr_debug_set_params(arg, DAOS_CHK_FAIL_REPORT_POOL1 | DAOS_FAIL_ALWAYS);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_RESET, 0, NULL, NULL);
	assert_rc_equal(rc, 0);

	cr_ins_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_COMPLETED);
	assert_rc_equal(rc, 0);

	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_FAILED, 0, NULL, NULL, NULL);
	assert_rc_equal(rc, 0);

	cr_debug_set_params(arg, 0);

	rc = cr_mode_switch(false);
	assert_rc_equal(rc, 0);

	rc = cr_system_start();
	assert_rc_equal(rc, 0);

	cr_dci_fini(&dci);
	cr_cleanup(arg, &pool, 1);
}

/*
 * 1. Create pool without inconsistency.
 * 2. Set fail_loc to simulate some engine failed to report pool shard when start checker.
 * 3. Start checker without options.
 * 4. Query checker, it should be completed, but the check for the pool maybe failed,
 *    depends on PS replicas count.
 * 5. Switch to normal mode and cleanup.
 */
static void
cr_handle_fail_pool2(void **state)
{
	test_arg_t		*arg = *state;
	struct test_pool	 pool = { 0 };
	struct daos_check_info	 dci = { 0 };
	daos_mgmt_pool_info_t	 mgmt_pool = { 0 };
	daos_size_t		 pool_nr = 1;
	uint32_t		 class;
	uint32_t		 action;
	uint32_t		 count;
	int			 rc;

	print_message("CR27: handle the pool if some engine failed to report some pool service\n");

	rc = cr_pool_create(state, &pool, false, TCC_NONE);
	assert_rc_equal(rc, 0);

	rc = dmg_pool_list(dmg_config_file, arg->group, &pool_nr, &mgmt_pool);
	assert_rc_equal(rc, 0);

	assert_rc_equal(pool_nr, 1);

	if (mgmt_pool.mgpi_svc->rl_nr == 1) {
		count = 1;
		class = TCC_POOL_LESS_SVC_WITHOUT_QUORUM;
		action = TCA_DISCARD;
	} else if (mgmt_pool.mgpi_svc->rl_nr == 2) {
		count = 1;
		class = TCC_POOL_LESS_SVC_WITHOUT_QUORUM;
		action = TCA_TRUST_PS;
	} else {
		count = 0;
		class = TCC_NONE;
		action = TCA_DEFAULT;
	}

	rc = cr_system_stop(false);
	assert_rc_equal(rc, 0);

	rc = cr_mode_switch(true);
	assert_rc_equal(rc, 0);

	rc = cr_debug_set_params(arg, DAOS_CHK_FAIL_REPORT_POOL2 | DAOS_FAIL_ALWAYS);
	assert_rc_equal(rc, 0);

	rc = daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_VALUE,
				   mgmt_pool.mgpi_svc->rl_ranks[0], 0, NULL);
	assert_rc_equal(rc, 0);

	rc = cr_check_start(TCSF_RESET, 0, NULL, NULL);
	assert_rc_equal(rc, 0);

	cr_ins_wait(1, &pool.pool_uuid, &dci);

	rc = cr_ins_verify(&dci, TCIS_COMPLETED);
	assert_rc_equal(rc, 0);

	rc = cr_pool_verify(&dci, pool.pool_uuid, TCPS_CHECKED, count, &class, &action, NULL);
	assert_rc_equal(rc, 0);

	rc = daos_debug_set_params(arg->group, -1, DMG_KEY_FAIL_VALUE, 0, 0, NULL);
	assert_rc_equal(rc, 0);

	cr_debug_set_params(arg, 0);

	rc = cr_mode_switch(false);
	assert_rc_equal(rc, 0);

	rc = cr_system_start();
	assert_rc_equal(rc, 0);

	cr_dci_fini(&dci);
	clean_pool_info(1, &mgmt_pool);
	cr_cleanup(arg, &pool, 1);
}

static const struct CMUnitTest cr_tests[] = {
	{ "CR1: start checker for specified pools",
	  cr_start_specified, async_disable, test_case_teardown},
	{ "CR2: check leader side interaction",
	  cr_leader_interaction, async_disable, test_case_teardown},
	{ "CR3: check engine side interaction",
	  cr_engine_interaction, async_disable, test_case_teardown},
	{ "CR4: check repair option - for-all, on leader",
	  cr_repair_forall_leader, async_disable, test_case_teardown},
	{ "CR5: check repair option - for-all, on engine",
	  cr_repair_forall_engine, async_disable, test_case_teardown},
	{ "CR6: stop checker with pending check leader interaction",
	  cr_stop_leader_interaction, async_disable, test_case_teardown},
	{ "CR7: stop checker with pending check engine interaction",
	  cr_stop_engine_interaction, async_disable, test_case_teardown},
	{ "CR8: stop checker for specified pools",
	  cr_stop_specified, async_disable, test_case_teardown},
	{ "CR9: reset checker automatically if former instance completed",
	  cr_auto_reset, async_disable, test_case_teardown},
	{ "CR10: checker shutdown",
	  cr_shutdown, async_disable, test_case_teardown},
	{ "CR11: checker crash",
	  cr_crash, async_disable, test_case_teardown},
	{ "CR12: check leader resume from former stop/paused phase",
	  cr_leader_resume, async_disable, test_case_teardown},
	{ "CR13: check engine resume from former stop/paused phase",
	  cr_engine_resume, async_disable, test_case_teardown},
	{ "CR14: reset checker for specified pools",
	  cr_reset_specified, async_disable, test_case_teardown},
	{ "CR15: check start option - failout",
	  cr_failout, async_disable, test_case_teardown},
	{ "CR16: check start option - auto repair",
	  cr_auto_repair, async_disable, test_case_teardown},
	{ "CR17: check start option - scan orphan pools by force",
	  cr_orphan_pool, async_disable, test_case_teardown},
	{ "CR18: PS leader fails to sync pool status with check leader",
	  cr_fail_sync_leader, async_disable, test_case_teardown},
	{ "CR19: PS leader fails to sync pool status with check engines",
	  cr_fail_sync_engine, async_disable, test_case_teardown},
	{ "CR20: check engine death during check",
	  cr_engine_death, async_disable, test_case_teardown},
	{ "CR21: check engine rejoins check instance successfully",
	  cr_engine_rejoin_succ, async_disable, test_case_teardown},
	{ "CR22: check engine fails to rejoin check instance",
	  cr_engine_rejoin_fail, async_disable, test_case_teardown},
	{ "CR23: control multiple pools check start/stop sequence",
	  cr_multiple_pools, async_disable, test_case_teardown},
	{ "CR24: check leader failed to notify check engine about orphan process",
	  cr_fail_sync_orphan, async_disable, test_case_teardown},
	{ "CR25: inherit check policy from former check repair",
	  cr_inherit_policy, async_disable, test_case_teardown},
	{ "CR26: skip the pool if some engine failed to report some pool shard",
	  cr_handle_fail_pool1, async_disable, test_case_teardown},
	{ "CR27: handle the pool if some engine failed to report some pool service",
	  cr_handle_fail_pool2, async_disable, test_case_teardown},
};

static int
cr_setup(void **state)
{
	return test_setup(state, SETUP_EQ, false, SMALL_POOL_SIZE, 0, NULL);
}

int
run_daos_cr_test(int rank, int size, int *sub_tests, int sub_tests_size)
{
	int	rc = 0;

	if (rank == 0) {
		if (sub_tests_size == 0)
			rc = cmocka_run_group_tests_name("DAOS_CR", cr_tests, cr_setup,
							 test_teardown);
		else
			rc = run_daos_sub_tests("DAOS_CR", cr_tests, ARRAY_SIZE(cr_tests),
						sub_tests, sub_tests_size, cr_setup, test_teardown);
	}

	par_bcast(PAR_COMM_WORLD, &rc, 1, PAR_INT, 0);

	return rc;
}
