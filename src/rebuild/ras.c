/**
 * (C) Copyright 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * rebuild: RAS
 *
 * This file contains RAS helpers for rebuild events.
 */
#define D_LOGFAC       DD_FAC(rebuild)

#include <daos_srv/daos_engine.h>

static int
raise_ras(ras_event_t id, ras_sev_t sev,
	  uuid_t *pool, uint32_t map_ver, char *op_str, char *msg)
{
	char *data = NULL;

	if ((pool == NULL) || uuid_is_null(*pool)) {
		D_ERROR("invalid pool\n");
		return -DER_INVAL;
	}

	D_ASPRINTF(data, "map_ver: [%"PRIu32"] op: [%s]", map_ver, op_str);
	if (data == NULL)
		return -DER_NOMEM;

	ds_notify_ras_event(id, msg, RAS_TYPE_INFO, sev,
			    NULL /* hwid */, NULL /* rank */,
			    NULL /* inc */, NULL /* jobid */,
			    pool, NULL /* cont */,
			    NULL /* objid */, NULL /* ctlop */, data);
	D_FREE(data);
	return 0;
}

int
rebuild_notify_ras_start(uuid_t *pool, uint32_t map_ver, char *op_str)
{
	char *msg = "Pool rebuild started.";

	return raise_ras(RAS_POOL_REBUILD_START, RAS_SEV_NOTICE,
			 pool, map_ver, op_str, msg);
}

int
rebuild_notify_ras_end(uuid_t *pool, uint32_t map_ver, char *op_str, int op_rc)
{
	char		*msg	= NULL;
	char		*status	= NULL;
	ras_event_t	 ev_id	= RAS_POOL_REBUILD_END;
	ras_sev_t	 ev_sev = RAS_SEV_NOTICE;
	int		 rc	= 0;

	if (op_rc != 0) {
		ev_id = RAS_POOL_REBUILD_FAILED;
		ev_sev = RAS_SEV_ERROR;
		D_ASPRINTF(status, "failed: "DF_RC, DP_RC(op_rc));
		if (status == NULL)
			return -DER_NOMEM;
	}
	D_ASPRINTF(msg, "Pool rebuild %s", op_rc ? status : "finished.");
	if (msg == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}

	rc = raise_ras(ev_id, ev_sev, pool, map_ver, op_str, msg);
out:
	D_FREE(status);
	D_FREE(msg);
	return rc;
}
