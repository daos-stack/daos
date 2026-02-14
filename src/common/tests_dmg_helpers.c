/**
 * (C) Copyright 2020-2024 Intel Corporation.
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <pwd.h>
#include <grp.h>
#include <stdlib.h>

#include <daos/common.h>
#include <daos/tests_lib.h>
#include <daos.h>
#include <daos_srv/bio.h>
#include <libdaos_control.h>

/* Handle for libdaos_control context */
static uintptr_t dmg_ctx;

int
dmg_init(const char *dmg_config_file)
{
	struct daos_control_init_args args = {
	    .config_file = dmg_config_file,
	    .log_file    = "/tmp/suite_dmg.log",
	    .log_level   = "debug",
	};
	int rc;

	if (dmg_ctx != 0)
		return 0; /* Already initialized */

	rc = daos_control_init(&args, &dmg_ctx);
	if (rc != 0) {
		D_ERROR("daos_control_init failed: %d\n", rc);
		dmg_ctx = 0;
	}

	return rc;
}

void
dmg_fini(void)
{
	if (dmg_ctx != 0) {
		daos_control_fini(dmg_ctx);
		dmg_ctx = 0;
	}
}

int
dmg_pool_set_prop(const char *dmg_config_file,
		  const char *prop_name, const char *prop_value,
		  const uuid_t pool_uuid)
{
	int rc;

	rc = dmg_init(dmg_config_file);
	if (rc != 0)
		return rc;

	return daos_control_pool_set_prop(dmg_ctx, (uuid_t *)pool_uuid, (char *)prop_name,
					  (char *)prop_value);
}

int
dmg_pool_get_prop(const char *dmg_config_file, const char *label,
		  const uuid_t uuid, const char *name, char **value)
{
	int rc;

	D_ASSERT(name != NULL);
	D_ASSERT(value != NULL);

	rc = dmg_init(dmg_config_file);
	if (rc != 0)
		return rc;

	return daos_control_pool_get_prop(dmg_ctx, (char *)label, (uuid_t *)uuid, (char *)name,
					  value);
}

int
dmg_pool_create(const char *dmg_config_file,
		uid_t uid, gid_t gid, const char *grp,
		const d_rank_list_t *tgts,
		daos_size_t scm_size, daos_size_t nvme_size,
		daos_prop_t *prop,
		d_rank_list_t *svc, uuid_t uuid)
{
	struct daos_prop_entry *entry;
	daos_prop_t            *new_prop  = NULL;
	bool                    has_label = false;
	int                     rc;

	rc = dmg_init(dmg_config_file);
	if (rc != 0)
		return rc;

	/* Check if label property already exists */
	if (prop != NULL && prop->dpp_nr > 0) {
		entry = daos_prop_entry_get(prop, DAOS_PROP_PO_LABEL);
		if (entry != NULL)
			has_label = true;
	}

	if (!has_label) {
		char path[] = "/tmp/test_XXXXXX";
		char label[DAOS_PROP_LABEL_MAX_LEN + 1];
		int  tmp_fd;

		/* pool label is required, generate a unique one randomly */
		tmp_fd = mkstemp(path);
		if (tmp_fd < 0) {
			D_ERROR("failed to generate unique label: %s\n", strerror(errno));
			return d_errno2der(errno);
		}
		close(tmp_fd);
		unlink(path);

		/* Copy label portion (after /tmp/) to properly sized buffer */
		strncpy(label, &path[5], sizeof(label) - 1);
		label[sizeof(label) - 1] = '\0';

		/* Create new prop with label - treat empty prop same as NULL */
		if (prop == NULL || prop->dpp_nr == 0) {
			new_prop = daos_prop_alloc(1);
			if (new_prop == NULL)
				return -DER_NOMEM;
			new_prop->dpp_entries[0].dpe_type = DAOS_PROP_PO_LABEL;
			D_STRNDUP(new_prop->dpp_entries[0].dpe_str, label, DAOS_PROP_LABEL_MAX_LEN);
			if (new_prop->dpp_entries[0].dpe_str == NULL) {
				daos_prop_free(new_prop);
				return -DER_NOMEM;
			}
		} else {
			/* Copy existing props and add label */
			new_prop = daos_prop_alloc(prop->dpp_nr + 1);
			if (new_prop == NULL)
				return -DER_NOMEM;
			rc = daos_prop_copy(new_prop, prop);
			if (rc != 0) {
				daos_prop_free(new_prop);
				return rc;
			}
			new_prop->dpp_entries[prop->dpp_nr].dpe_type = DAOS_PROP_PO_LABEL;
			D_STRNDUP(new_prop->dpp_entries[prop->dpp_nr].dpe_str, label,
				  DAOS_PROP_LABEL_MAX_LEN);
			if (new_prop->dpp_entries[prop->dpp_nr].dpe_str == NULL) {
				daos_prop_free(new_prop);
				return -DER_NOMEM;
			}
		}
		prop = new_prop;
	}

	rc = daos_control_pool_create(dmg_ctx, uid, gid, (char *)grp, (d_rank_list_t *)tgts,
				      scm_size, nvme_size, prop, svc, (uuid_t *)uuid);

	if (new_prop != NULL)
		daos_prop_free(new_prop);

	return rc;
}

int
dmg_pool_destroy(const char *dmg_config_file, const uuid_t uuid, const char *grp, int force)
{
	int rc;

	rc = dmg_init(dmg_config_file);
	if (rc != 0)
		return rc;

	return daos_control_pool_destroy(dmg_ctx, (uuid_t *)uuid, (char *)grp, force);
}

int
dmg_pool_evict(const char *dmg_config_file, const uuid_t uuid, const char *grp)
{
	int rc;

	rc = dmg_init(dmg_config_file);
	if (rc != 0)
		return rc;

	return daos_control_pool_evict(dmg_ctx, (uuid_t *)uuid, (char *)grp);
}

int
dmg_pool_update_ace(const char *dmg_config_file, const uuid_t uuid, const char *grp,
		    const char *ace)
{
	int rc;

	rc = dmg_init(dmg_config_file);
	if (rc != 0)
		return rc;

	return daos_control_pool_update_ace(dmg_ctx, (uuid_t *)uuid, (char *)grp, (char *)ace);
}

int
dmg_pool_delete_ace(const char *dmg_config_file, const uuid_t uuid, const char *grp,
		    const char *principal)
{
	int rc;

	rc = dmg_init(dmg_config_file);
	if (rc != 0)
		return rc;

	return daos_control_pool_delete_ace(dmg_ctx, (uuid_t *)uuid, (char *)grp,
					    (char *)principal);
}

int
dmg_pool_exclude(const char *dmg_config_file, const uuid_t uuid,
		 const char *grp, d_rank_t rank, int tgt_idx)
{
	int rc;

	rc = dmg_init(dmg_config_file);
	if (rc != 0)
		return rc;

	return daos_control_pool_exclude(dmg_ctx, (uuid_t *)uuid, (char *)grp, rank, tgt_idx);
}

int
dmg_pool_reintegrate(const char *dmg_config_file, const uuid_t uuid,
		     const char *grp, d_rank_t rank, int tgt_idx)
{
	int rc;

	rc = dmg_init(dmg_config_file);
	if (rc != 0)
		return rc;

	return daos_control_pool_reintegrate(dmg_ctx, (uuid_t *)uuid, (char *)grp, rank, tgt_idx);
}

int
dmg_pool_drain(const char *dmg_config_file, const uuid_t uuid,
	       const char *grp, d_rank_t rank, int tgt_idx)
{
	int rc;

	rc = dmg_init(dmg_config_file);
	if (rc != 0)
		return rc;

	return daos_control_pool_drain(dmg_ctx, (uuid_t *)uuid, (char *)grp, rank, tgt_idx);
}

int
dmg_pool_extend(const char *dmg_config_file, const uuid_t uuid,
		const char *grp, d_rank_t *ranks, int rank_nr)
{
	int rc;

	rc = dmg_init(dmg_config_file);
	if (rc != 0)
		return rc;

	return daos_control_pool_extend(dmg_ctx, (uuid_t *)uuid, (char *)grp, ranks, rank_nr);
}

int
dmg_pool_list(const char *dmg_config_file, const char *group,
	      daos_size_t *npools, daos_mgmt_pool_info_t *pools)
{
	int rc;

	if (npools == NULL)
		return -DER_INVAL;

	rc = dmg_init(dmg_config_file);
	if (rc != 0)
		return rc;

	return daos_control_pool_list(dmg_ctx, (char *)group, npools, pools);
}

int
dmg_pool_rebuild_stop(const char *dmg_config_file, const uuid_t uuid, const char *grp, bool force)
{
	int rc;

	rc = dmg_init(dmg_config_file);
	if (rc != 0)
		return rc;

	return daos_control_pool_rebuild_stop(dmg_ctx, (uuid_t *)uuid, (char *)grp, force ? 1 : 0);
}

int
dmg_pool_rebuild_start(const char *dmg_config_file, const uuid_t uuid, const char *grp)
{
	int rc;

	rc = dmg_init(dmg_config_file);
	if (rc != 0)
		return rc;

	return daos_control_pool_rebuild_start(dmg_ctx, (uuid_t *)uuid, (char *)grp);
}

int
dmg_storage_device_list(const char *dmg_config_file, int *ndisks,
			device_list *devices)
{
	int rc;

	rc = dmg_init(dmg_config_file);
	if (rc != 0)
		return rc;

	return daos_control_storage_device_list(dmg_ctx, ndisks, devices);
}

int
dmg_storage_set_nvme_fault(const char *dmg_config_file,
			   char *host, const uuid_t uuid, int force)
{
	int rc;

	rc = dmg_init(dmg_config_file);
	if (rc != 0)
		return rc;

	return daos_control_storage_set_nvme_fault(dmg_ctx, host, (uuid_t *)uuid, force);
}

int
dmg_storage_query_device_health(const char *dmg_config_file, char *host,
				char *stats, const uuid_t uuid)
{
	int rc;

	rc = dmg_init(dmg_config_file);
	if (rc != 0)
		return rc;

	/* stats is used both as input (key name) and output (value) */
	return daos_control_storage_query_device_health(dmg_ctx, host, stats, stats, 256,
							(uuid_t *)uuid);
}

int verify_blobstore_state(int state, const char *state_str)
{
	if (strcasecmp(state_str, "FAULTY") == 0) {
		if (state == BIO_BS_STATE_FAULTY)
			return 0;
	}

	if (strcasecmp(state_str, "NORMAL") == 0) {
		if (state == BIO_BS_STATE_NORMAL)
			return 0;
	}

	if (strcasecmp(state_str, "TEARDOWN") == 0) {
		if (state == BIO_BS_STATE_TEARDOWN)
			return 0;
	}

	if (strcasecmp(state_str, "OUT") == 0) {
		if (state == BIO_BS_STATE_OUT)
			return 0;
	}

	if (strcasecmp(state_str, "SETUP") == 0) {
		if (state == BIO_BS_STATE_SETUP)
			return 0;
	}

	return 1;
}

int dmg_system_stop_rank(const char *dmg_config_file, d_rank_t rank, int force)
{
	int rc;

	if (rank == CRT_NO_RANK)
		return -DER_INVAL;

	rc = dmg_init(dmg_config_file);
	if (rc != 0)
		return rc;

	return daos_control_system_stop_rank(dmg_ctx, rank, force);
}

int dmg_system_start_rank(const char *dmg_config_file, d_rank_t rank)
{
	int rc;

	if (rank == CRT_NO_RANK)
		return -DER_INVAL;

	rc = dmg_init(dmg_config_file);
	if (rc != 0)
		return rc;

	return daos_control_system_start_rank(dmg_ctx, rank);
}

int dmg_system_reint_rank(const char *dmg_config_file, d_rank_t rank)
{
	int rc;

	if (rank == CRT_NO_RANK)
		return -DER_INVAL;

	rc = dmg_init(dmg_config_file);
	if (rc != 0)
		return rc;

	return daos_control_system_reint_rank(dmg_ctx, rank);
}

int dmg_system_exclude_rank(const char *dmg_config_file, d_rank_t rank)
{
	int rc;

	if (rank == CRT_NO_RANK)
		return -DER_INVAL;

	rc = dmg_init(dmg_config_file);
	if (rc != 0)
		return rc;

	return daos_control_system_exclude_rank(dmg_ctx, rank);
}

int
dmg_server_set_logmasks(const char *dmg_config_file, const char *masks, const char *streams,
			const char *subsystems)
{
	int rc;

	rc = dmg_init(dmg_config_file);
	if (rc != 0)
		return rc;

	return daos_control_server_set_logmasks(dmg_ctx, (char *)masks, (char *)streams,
						(char *)subsystems);
}

const char *
daos_target_state_enum_to_str(int state)
{
	switch (state) {
	case DAOS_TS_UNKNOWN: return "UNKNOWN";
	case DAOS_TS_DOWN_OUT: return "DOWNOUT";
	case DAOS_TS_DOWN: return "DOWN";
	case DAOS_TS_UP: return "UP";
	case DAOS_TS_UP_IN: return "UPIN";
	case DAOS_TS_DRAIN: return "DRAIN";
	}

	return "Undefined State";
}

int
dmg_fault_inject(const char *dmg_config_file, uuid_t uuid, bool mgmt, const char *fault)
{
	int rc;

	rc = dmg_init(dmg_config_file);
	if (rc != 0)
		return rc;

	return daos_control_fault_inject(dmg_ctx, (uuid_t *)uuid, mgmt ? 1 : 0, (char *)fault);
}

int
dmg_check_switch(const char *dmg_config_file, bool enable)
{
	int rc;

	rc = dmg_init(dmg_config_file);
	if (rc != 0)
		return rc;

	return daos_control_check_switch(dmg_ctx, enable ? 1 : 0);
}

int
dmg_check_start(const char *dmg_config_file, uint32_t flags, uint32_t pool_nr, uuid_t uuids[],
		const char *policies)
{
	int rc;

	rc = dmg_init(dmg_config_file);
	if (rc != 0)
		return rc;

	return daos_control_check_start(dmg_ctx, flags, pool_nr, uuids, (char *)policies);
}

int
dmg_check_stop(const char *dmg_config_file, uint32_t pool_nr, uuid_t uuids[])
{
	int rc;

	rc = dmg_init(dmg_config_file);
	if (rc != 0)
		return rc;

	return daos_control_check_stop(dmg_ctx, pool_nr, uuids);
}

int
dmg_check_query(const char *dmg_config_file, uint32_t pool_nr, uuid_t uuids[],
		struct daos_check_info *dci)
{
	int rc;

	rc = dmg_init(dmg_config_file);
	if (rc != 0)
		return rc;

	return daos_control_check_query(dmg_ctx, pool_nr, uuids, dci);
}

int
dmg_check_repair(const char *dmg_config_file, uint64_t seq, uint32_t opt)
{
	int rc;

	rc = dmg_init(dmg_config_file);
	if (rc != 0)
		return rc;

	return daos_control_check_repair(dmg_ctx, seq, opt);
}

int
dmg_check_set_policy(const char *dmg_config_file, uint32_t flags, const char *policies)
{
	int rc;

	rc = dmg_init(dmg_config_file);
	if (rc != 0)
		return rc;

	return daos_control_check_set_policy(dmg_ctx, flags, (char *)policies);
}
