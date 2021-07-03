/**
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of the DAOS profile.
 */
#define D_LOGFAC	DD_FAC(common)

#include <daos/common.h>
#include <daos/profile.h>
#include <daos_errno.h>
#include <gurt/list.h>

#define PF_MAX_NAME_SIZE	64
#define DEFAULT_CHUNK_SIZE  10240
#define DEFAULT_CHUNK_CNT_LIMIT	100

char *profile_op_names[] = {
	[OBJ_PF_UPDATE_PREP] = "update_prep",
	[OBJ_PF_UPDATE_DISPATCH] = "update_dispatch",
	[OBJ_PF_UPDATE_LOCAL] = "update_local",
	[OBJ_PF_UPDATE_END] = "update_end",
	[OBJ_PF_BULK_TRANSFER] = "bulk_transfer",
	[OBJ_PF_UPDATE_REPLY] = "update_repl",
	[OBJ_PF_UPDATE] = "update",
	[VOS_UPDATE_END] = "vos_update_end",
};

static void
profile_chunk_destroy(struct daos_profile_chunk *chunk)
{
	d_list_del(&chunk->dpc_chunk_list);
	D_FREE(chunk->dpc_chunks);
	D_FREE_PTR(chunk);
}

static struct daos_profile_chunk *
profile_chunk_alloc(int chunk_size)
{
	struct daos_profile_chunk *chunk;

	D_ALLOC_PTR(chunk);
	if (chunk == NULL)
		return NULL;

	D_INIT_LIST_HEAD(&chunk->dpc_chunk_list);
	D_ALLOC_ARRAY(chunk->dpc_chunks, chunk_size);
	chunk->dpc_chunk_size = chunk_size;
	chunk->dpc_chunk_offset = 0;
	if (chunk->dpc_chunks == NULL) {
		profile_chunk_destroy(chunk);
		return NULL;
	}
	return chunk;
}

static struct daos_profile *
profile_alloc(int op_cnt)
{
	struct daos_profile *dp;

	D_ALLOC_PTR(dp);
	if (dp == NULL)
		return NULL;

	D_ALLOC_ARRAY(dp->dp_ops, op_cnt);
	if (dp->dp_ops == NULL) {
		D_FREE_PTR(dp);
		return NULL;
	}
	dp->dp_ops_cnt = op_cnt;

	return dp;
}

void
daos_profile_destroy(struct daos_profile *dp)
{
	struct daos_profile_chunk *dpc;
	struct daos_profile_chunk *tmp;
	int			  i;

	for (i = 0; i < dp->dp_ops_cnt; i++) {
		struct daos_profile_op	*dpo;

		dpo = &dp->dp_ops[i];

		d_list_for_each_entry_safe(dpc, tmp, &dpo->dpo_chunk_list,
					   dpc_chunk_list)
			profile_chunk_destroy(dpc);

		d_list_for_each_entry_safe(dpc, tmp, &dpo->dpo_chunk_idle_list,
					   dpc_chunk_list)
			profile_chunk_destroy(dpc);
	}

	if (dp->dp_dir_path)
		D_FREE(dp->dp_dir_path);

	D_FREE_PTR(dp);
}

static int
profile_get_new_chunk(struct daos_profile_op *dpo)
{
	struct daos_profile_chunk *chunk;

	if (!d_list_empty(&dpo->dpo_chunk_idle_list)) {
		/* Try to get one from idle list */
		chunk = d_list_entry(dpo->dpo_chunk_idle_list.next,
				     struct daos_profile_chunk,
				     dpc_chunk_list);
		d_list_move_tail(&chunk->dpc_chunk_list, &dpo->dpo_chunk_list);
		dpo->dpo_chunk_cnt++;
		D_ASSERT(chunk->dpc_chunk_offset == 0);
		D_ASSERT(dpo->dpo_chunk_cnt <= dpo->dpo_chunk_total_cnt);
	} else if (dpo->dpo_chunk_total_cnt < DEFAULT_CHUNK_CNT_LIMIT) {
		/* Allocate new one */
		chunk = profile_chunk_alloc(DEFAULT_CHUNK_SIZE);
		if (chunk == NULL)
			return -DER_NOMEM;

		d_list_add_tail(&chunk->dpc_chunk_list,
				&dpo->dpo_chunk_list);
		dpo->dpo_chunk_total_cnt++;
		dpo->dpo_chunk_cnt++;
	} else {
		/* Reach the limit, Let's reuse the oldest (i.e. 1st) in list */
		chunk = d_list_entry(dpo->dpo_chunk_list.next,
				     struct daos_profile_chunk,
				     dpc_chunk_list);
		D_DEBUG(DB_TRACE, "Reuse the old profile buffer %p\n", chunk);
	}

	dpo->dpo_current_chunk = chunk;
	return 0;
}

static int
profile_op_init(struct daos_profile_op *dpo, int id, char *name)
{
	int rc;

	dpo->dpo_op = id;
	dpo->dpo_op_name = name;
	D_INIT_LIST_HEAD(&dpo->dpo_chunk_list);
	D_INIT_LIST_HEAD(&dpo->dpo_chunk_idle_list);
	dpo->dpo_acc_cnt = 0;
	dpo->dpo_acc_val = 0;
	dpo->dpo_chunk_total_cnt = 0;
	dpo->dpo_chunk_cnt = 0;

	rc = profile_get_new_chunk(dpo);

	return rc;
}

static void
profile_chunk_next(struct daos_profile_op *dpo)
{
	struct daos_profile_chunk *chunk = dpo->dpo_current_chunk;

	if (dpo->dpo_acc_cnt == 0)
		return;

	D_ASSERT(chunk != NULL);
	D_ASSERT(chunk->dpc_chunk_offset <= chunk->dpc_chunk_size);

	chunk->dpc_chunks[chunk->dpc_chunk_offset] =
				dpo->dpo_acc_val / dpo->dpo_acc_cnt;
	chunk->dpc_chunk_offset++;
	dpo->dpo_acc_val = 0;
	dpo->dpo_acc_cnt = 0;
}

static int
profile_dump_chunk(struct daos_profile_op *dpo, FILE *file,
		   struct daos_profile_chunk *dpc)
{
	char string[PF_MAX_NAME_SIZE] = { 0 };
	int rc = 0;
	int i;

	for (i = 0; i < dpc->dpc_chunk_offset; i++) {
		size_t size;

		memset(string, 0, PF_MAX_NAME_SIZE);
		/* Dump name and time cost to the file */
		snprintf(string, PF_MAX_NAME_SIZE, "%s "DF_U64"\n",
			 dpo->dpo_op_name, dpc->dpc_chunks[i]);
		size = fwrite(string, 1, strlen(string), file);
		if (size != strlen(string)) {
			D_ERROR("dump failed: %s\n", strerror(errno));
			rc = daos_errno2der(errno);
			break;
		}
	}

	return rc;
}

void
daos_profile_dump(struct daos_profile *dp)
{
	FILE	*file;
	char	name[PF_MAX_NAME_SIZE] = { 0 };
	char	*path;
	int	rc;
	int	i;

	if (dp->dp_dir_path) {
		D_ALLOC(path, strlen(dp->dp_dir_path) + PF_MAX_NAME_SIZE);
		if (path == NULL) {
			rc = -DER_NOMEM;
			D_ERROR("start dump ult failed: rc "DF_RC"\n",
				DP_RC(rc));
			return;
		}

		sprintf(name, "/profile-%d-%d.dump", dp->dp_rank, dp->dp_xid);
		strcpy(path, dp->dp_dir_path);
		strcat(path, name);
	} else {
		sprintf(name, "./profile-%d-%d.dump", dp->dp_rank, dp->dp_xid);
		path = name;
	}

	file = fopen(path, "a");
	if (file == NULL) {
		rc = daos_errno2der(errno);
		D_ERROR("open %s: %s\n", path, strerror(errno));
		goto out;
	}

	for (i = 0; i < dp->dp_ops_cnt; i++) {
		struct daos_profile_op *dpo;
		struct daos_profile_chunk *dpc;
		struct daos_profile_chunk *tmp;

		dpo = &dp->dp_ops[i];
		d_list_for_each_entry_safe(dpc, tmp,
					   &dpo->dpo_chunk_list,
					   dpc_chunk_list) {
			if (dpc == dpo->dpo_current_chunk)
				/* Close the current one */
				profile_chunk_next(&dp->dp_ops[i]);

			if (dpc->dpc_chunk_offset > 0)
				dp->dp_empty = 0;
			rc = profile_dump_chunk(dpo, file, dpc);
			if (rc)
				break;
			/* move the spc to idle list */
			dpo->dpo_chunk_cnt--;
			dpc->dpc_chunk_offset = 0;
			d_list_move_tail(&dpc->dpc_chunk_list,
					 &dpo->dpo_chunk_idle_list);
		}
	}

	fclose(file);
	if (dp->dp_empty)
		unlink(path);
out:
	if (path != name)
		free(path);
}

int
daos_profile_init(struct daos_profile **dp_p, char *path, int avg, int rank,
		  int tgt_id)
{
	struct daos_profile	*dp;
	int			i;
	int			rc;

	dp = profile_alloc(PF_MAX_CNT);
	if (dp == NULL)
		return -DER_NOMEM;

	dp->dp_empty = 1;
	D_ASSERT(ARRAY_SIZE(profile_op_names) == PF_MAX_CNT);
	for (i = 0; i < PF_MAX_CNT; i++) {
		rc = profile_op_init(&dp->dp_ops[i], i, profile_op_names[i]);
		if (rc)
			D_GOTO(out, rc);
	}

	if (path != NULL) {
		D_ALLOC(dp->dp_dir_path, strlen(path) + 1);
		if (dp->dp_dir_path == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		strcpy(dp->dp_dir_path, path);
	}

	dp->dp_avg = avg;
	dp->dp_xid = tgt_id;
	dp->dp_rank = rank;

	*dp_p = dp;
out:
	if (rc && dp != NULL)
		daos_profile_destroy(dp);

	return rc;
}

int
daos_profile_count(struct daos_profile *dp, int id, int val)
{
	struct daos_profile_chunk	*current;
	struct daos_profile_op		*dpo;

	dpo = &dp->dp_ops[id];
	dpo->dpo_acc_val += val;
	dpo->dpo_acc_cnt++;
	if (dpo->dpo_acc_cnt >= dp->dp_avg && dp->dp_avg != -1) {
		D_ASSERT(dpo->dpo_current_chunk != NULL);
		current = dpo->dpo_current_chunk;
		/* Current profile chunk is full, get a new one */
		if (current->dpc_chunk_offset == current->dpc_chunk_size) {
			int	rc;

			rc = profile_get_new_chunk(dpo);
			if (rc)
				return rc;
		}
		profile_chunk_next(dpo);
	}

	return 0;
}
