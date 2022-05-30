/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(chk)

#include <daos_srv/vos.h>
#include <daos_srv/daos_chk.h>
#include <daos_srv/daos_engine.h>

#include "chk_internal.h"

static struct sys_db	*chk_db;

static int
chk_db_fetch(char *key, int key_size, void *val, int val_size)
{
	d_iov_t	key_iov;
	d_iov_t	val_iov;

	d_iov_set(&key_iov, key, key_size);
	d_iov_set(&val_iov, val, val_size);

	return chk_db->sd_fetch(chk_db, CHK_DB_TABLE, &key_iov, &val_iov);
}

static int
chk_db_update(char *key, int key_size, void *val, int val_size)
{
	d_iov_t	key_iov;
	d_iov_t	val_iov;
	int	rc;

	if (chk_db->sd_tx_begin) {
		rc = chk_db->sd_tx_begin(chk_db);
		if (rc != 0)
			goto out;
	}

	d_iov_set(&key_iov, key, key_size);
	d_iov_set(&val_iov, val, val_size);

	rc = chk_db->sd_upsert(chk_db, CHK_DB_TABLE, &key_iov, &val_iov);

	if (chk_db->sd_tx_end)
		rc = chk_db->sd_tx_end(chk_db, rc);

out:
	return rc;
}

static int
chk_db_delete(char *key, int key_size)
{
	d_iov_t	key_iov;
	int	rc;

	if (chk_db->sd_tx_begin) {
		rc = chk_db->sd_tx_begin(chk_db);
		if (rc != 0)
			goto out;
	}

	d_iov_set(&key_iov, key, key_size);

	rc = chk_db->sd_delete(chk_db, CHK_DB_TABLE, &key_iov);

	if (chk_db->sd_tx_end)
		rc = chk_db->sd_tx_end(chk_db, rc);

out:
	return rc;
}

static int
chk_db_traverse(sys_db_trav_cb_t cb, void *args)
{
	return chk_db->sd_traverse(chk_db, CHK_DB_TABLE, cb, args);
}

int
chk_bk_fetch_leader(struct chk_bookmark *cbk)
{
	int	rc;

	rc = chk_db_fetch(CHK_BK_LEADER, strlen(CHK_BK_LEADER), cbk, sizeof(*cbk));
	if (rc != 0 && rc != -DER_NONEXIST)
		D_ERROR("Failed to fetch leader bookmark on rank %u: "DF_RC"\n",
			dss_self_rank(), DP_RC(rc));

	return rc;
}

int
chk_bk_update_leader(struct chk_bookmark *cbk)
{
	int	rc;

	rc = chk_db_update(CHK_BK_LEADER, strlen(CHK_BK_LEADER), cbk, sizeof(*cbk));
	if (rc != 0)
		D_ERROR("Failed to update leader bookmark on rank %u: "DF_RC"\n",
			dss_self_rank(), DP_RC(rc));

	return rc;
}

int
chk_bk_delete_leader(void)
{
	int	rc;

	rc = chk_db_delete(CHK_BK_LEADER, strlen(CHK_BK_LEADER));
	if (rc != 0)
		D_ERROR("Failed to delete leader bookmark on rank %u: "DF_RC"\n",
			dss_self_rank(), DP_RC(rc));

	return rc;
}

int
chk_bk_fetch_engine(struct chk_bookmark *cbk)
{
	int	rc;

	rc = chk_db_fetch(CHK_BK_ENGINE, strlen(CHK_BK_ENGINE), cbk, sizeof(*cbk));
	if (rc != 0 && rc != -DER_NONEXIST)
		D_ERROR("Failed to fetch engine bookmark on rank %u: "DF_RC"\n",
			dss_self_rank(), DP_RC(rc));

	return rc;
}

int
chk_bk_update_engine(struct chk_bookmark *cbk)
{
	int	rc;

	rc = chk_db_update(CHK_BK_ENGINE, strlen(CHK_BK_ENGINE), cbk, sizeof(*cbk));
	if (rc != 0)
		D_ERROR("Failed to update engine bookmark on rank %u: "DF_RC"\n",
			dss_self_rank(), DP_RC(rc));

	return rc;
}

int
chk_bk_delete_engine(void)
{
	int	rc;

	rc = chk_db_delete(CHK_BK_ENGINE, strlen(CHK_BK_ENGINE));
	if (rc != 0)
		D_ERROR("Failed to delete engine bookmark on rank %u: "DF_RC"\n",
			dss_self_rank(), DP_RC(rc));

	return rc;
}

int
chk_bk_fetch_pool(struct chk_bookmark *cbk, uuid_t uuid)
{
	char	uuid_str[DAOS_UUID_STR_SIZE];
	int	rc;

	uuid_unparse_lower(uuid, uuid_str);
	rc = chk_db_fetch(uuid_str, strlen(uuid_str), cbk, sizeof(*cbk));
	if (rc != 0 && rc != -DER_NONEXIST)
		D_ERROR("Failed to fetch pool "DF_UUIDF" bookmark on rank %u: "DF_RC"\n",
			DP_UUID(uuid), dss_self_rank(), DP_RC(rc));

	return rc;
}

int
chk_bk_update_pool(struct chk_bookmark *cbk, uuid_t uuid)
{
	char	uuid_str[DAOS_UUID_STR_SIZE];
	int	rc;

	uuid_unparse_lower(uuid, uuid_str);
	rc = chk_db_update(uuid_str, strlen(uuid_str), cbk, sizeof(*cbk));
	if (rc != 0)
		D_ERROR("Failed to update pool "DF_UUIDF" bookmark on rank %u: "DF_RC"\n",
			DP_UUID(uuid), dss_self_rank(), DP_RC(rc));

	return rc;
}

int
chk_bk_delete_pool(uuid_t uuid)
{
	char	uuid_str[DAOS_UUID_STR_SIZE];
	int	rc;

	uuid_unparse_lower(uuid, uuid_str);
	rc = chk_db_delete(uuid_str, strlen(uuid_str));
	if (rc != 0)
		D_ERROR("Failed to delete pool "DF_UUIDF" bookmark on rank %u: "DF_RC"\n",
			DP_UUID(uuid), dss_self_rank(), DP_RC(rc));

	return rc;
}

int
chk_prop_fetch(struct chk_property *cpp, d_rank_list_t **rank_list)
{
	d_rank_list_t	*ranks = NULL;
	int		 rc;

	rc = chk_db_fetch(CHK_PROPERTY, strlen(CHK_PROPERTY), cpp, sizeof(*cpp));
	if (rc == 0 && cpp->cp_rank_nr != 0 && rank_list != NULL) {
		ranks = d_rank_list_alloc(cpp->cp_rank_nr);
		if (ranks == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		rc = chk_db_fetch(CHK_RANKS, strlen(CHK_RANKS), ranks->rl_ranks,
				  sizeof(*ranks->rl_ranks) * ranks->rl_nr);
		/*
		 * CHK_PROPERTY and CHK_RANKS must be exist together.
		 * Otherwise there is local corruption.
		 */
		if (rc == -DER_NONEXIST) {
			d_rank_list_free(ranks);
			ranks = NULL;
			D_GOTO(out, rc = -DER_IO);
		}

		if (rc != 0)
			goto out;
	}

out:
	if (rank_list != NULL)
		*rank_list = ranks;

	if (rc != 0 && rc != -DER_NONEXIST)
		D_ERROR("Failed to fetch check property on rank %u: "DF_RC"\n",
			dss_self_rank(), DP_RC(rc));

	return rc;
}

int
chk_prop_update(struct chk_property *cpp, d_rank_list_t *rank_list)
{
	d_iov_t	key_iov;
	d_iov_t	val_iov;
	int	rc;

	if (chk_db->sd_tx_begin) {
		rc = chk_db->sd_tx_begin(chk_db);
		if (rc != 0)
			goto out;
	}

	if (cpp->cp_rank_nr != 0 && rank_list != NULL) {
		D_ASSERTF(cpp->cp_rank_nr == rank_list->rl_nr, "Invalid rank nr %u/%u\n",
			  cpp->cp_rank_nr, rank_list->rl_nr);

		d_iov_set(&key_iov, CHK_RANKS, strlen(CHK_RANKS));
		d_iov_set(&val_iov, rank_list->rl_ranks,
			  sizeof(*rank_list->rl_ranks) * rank_list->rl_nr);

		rc = chk_db->sd_upsert(chk_db, CHK_DB_TABLE, &key_iov, &val_iov);
		if (rc != 0)
			goto end;
	}

	d_iov_set(&key_iov, CHK_PROPERTY, strlen(CHK_PROPERTY));
	d_iov_set(&val_iov, cpp, sizeof(*cpp));

	rc = chk_db->sd_upsert(chk_db, CHK_DB_TABLE, &key_iov, &val_iov);

end:
	if (chk_db->sd_tx_end)
		rc = chk_db->sd_tx_end(chk_db, rc);

out:
	if (rc != 0)
		D_ERROR("Failed to update check property on rank %u: "DF_RC"\n",
			dss_self_rank(), DP_RC(rc));

	return rc;
}

int
chk_traverse_pools(sys_db_trav_cb_t cb, void *args)
{
	int	rc;

	rc = chk_db_traverse(cb, args);
	if (rc < 0)
		D_ERROR("Failed to traverse pools on rank %u for pause: "DF_RC"\n",
			dss_self_rank(), DP_RC(rc));

	return rc;
}

void
chk_vos_init(void)
{
	chk_db = vos_db_get();
}

void
chk_vos_fini(void)
{
	chk_db = NULL;
}
