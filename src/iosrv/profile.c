/**
 * (C) Copyright 2020 Intel Corporation.
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
 * This file is part of the DAOS server. It implements the DAOS server profile
 * API.
 */
#define D_LOGFAC       DD_FAC(server)

#include <abt.h>
#include <daos/common.h>
#include <daos/event.h>
#include <daos_errno.h>
#include <daos_srv/bio.h>
#include <daos_srv/smd.h>
#include <gurt/list.h>
#include "drpc_internal.h"
#include "srv_internal.h"

/**
 * During profiling, it will only allocate 10K size chunk each time, once it
 * uses up all spaces. it will be allocate new chunk. The dump ULT will start
 * dumpping the full chunk into the profile file when the total chunks num
 * reaches to the 75%, then move those chunks to the idle list, and reuse them
 * later. When the total chunk number reaches to the hard limit, then it will
 * start to overwrite the first chunk, i.e. the earilest profiling might lose
 * if it still not dump to the file.
 */
#define DEFAULT_CHUNK_SIZE  10240
#define DEFAULT_CHUNK_CNT_LIMIT	100
#define DEFAULT_DUMP_CHUNK_THRESHOLD 75	/* 75% of CHUNK_CNT_LIMIT */

static void
srv_profile_chunk_destroy(struct srv_profile_chunk *chunk)
{
	d_list_del(&chunk->spc_chunk_list);
	D_FREE(chunk->spc_chunks);
	D_FREE_PTR(chunk);
}

struct srv_profile_chunk *
srv_profile_chunk_alloc(int chunk_size)
{
	struct srv_profile_chunk *chunk;

	D_ALLOC_PTR(chunk);
	if (chunk == NULL)
		return NULL;

	D_INIT_LIST_HEAD(&chunk->spc_chunk_list);
	D_ALLOC(chunk->spc_chunks, chunk_size * sizeof(*chunk->spc_chunks));
	chunk->spc_chunk_size = chunk_size;
	chunk->spc_chunk_offset = 0;
	if (chunk->spc_chunks == NULL) {
		srv_profile_chunk_destroy(chunk);
		return NULL;
	}
	return chunk;
}

struct srv_profile *
srv_profile_alloc(int op_cnt)
{
	struct srv_profile	*sp;

	D_ALLOC_PTR(sp);
	if (sp == NULL)
		return NULL;

	D_ALLOC_ARRAY(sp->sp_ops, op_cnt);
	if (sp->sp_ops == NULL) {
		D_FREE_PTR(sp);
		return NULL;
	}
	sp->sp_ops_cnt = op_cnt;

	return sp;
}

void
srv_profile_destroy(struct srv_profile *sp)
{
	struct srv_profile_chunk *spc;
	struct srv_profile_chunk *tmp;
	int			 i;

	for (i = 0; i < sp->sp_ops_cnt; i++) {
		struct srv_profile_op	*spo;

		spo = &sp->sp_ops[i];

		d_list_for_each_entry_safe(spc, tmp, &spo->pro_chunk_list,
					   spc_chunk_list) {
			srv_profile_chunk_destroy(spc);
		}

		d_list_for_each_entry_safe(spc, tmp, &spo->pro_chunk_idle_list,
					   spc_chunk_list) {
			srv_profile_chunk_destroy(spc);
		}
	}

	if (sp->sp_dir_path)
		D_FREE(sp->sp_dir_path);

	ABT_thread_free(&sp->sp_dump_thread);
	D_FREE_PTR(sp);
}

static int
srv_profile_get_new_chunk(struct srv_profile_op *spo)
{
	struct srv_profile_chunk *chunk;

	if (!d_list_empty(&spo->pro_chunk_idle_list)) {
		/* Try to get one from idle list */
		chunk = d_list_entry(spo->pro_chunk_idle_list.next,
				     struct srv_profile_chunk,
				     spc_chunk_list);
		d_list_move_tail(&chunk->spc_chunk_list, &spo->pro_chunk_list);
		spo->pro_chunk_cnt++;
		D_ASSERT(chunk->spc_chunk_offset == 0);
		D_ASSERT(spo->pro_chunk_cnt <= spo->pro_chunk_total_cnt);
	} else if (spo->pro_chunk_total_cnt < DEFAULT_CHUNK_CNT_LIMIT) {
		/* Allocate new one */
		chunk = srv_profile_chunk_alloc(DEFAULT_CHUNK_SIZE);
		if (chunk == NULL)
			return -DER_NOMEM;

		d_list_add_tail(&chunk->spc_chunk_list,
				&spo->pro_chunk_list);
		spo->pro_chunk_total_cnt++;
		spo->pro_chunk_cnt++;
	} else {
		/* Reach the limit, Let's reuse the oldest (i.e. 1st) in list */
		chunk = d_list_entry(spo->pro_chunk_list.next,
				     struct srv_profile_chunk,
				     spc_chunk_list);
	}

	spo->pro_current_chunk = chunk;
	return 0;
}

int
srv_profile_op_init(struct srv_profile_op *sp, int id, char *name)
{
	int rc;

	sp->pro_op = id;
	sp->pro_op_name = name;
	D_INIT_LIST_HEAD(&sp->pro_chunk_list);
	D_INIT_LIST_HEAD(&sp->pro_chunk_idle_list);
	sp->pro_acc_cnt = 0;
	sp->pro_acc_val = 0;
	sp->pro_chunk_total_cnt = 0;
	sp->pro_chunk_cnt = 0;

	rc = srv_profile_get_new_chunk(sp);

	return rc;
}

static void
srv_profile_chunk_next(struct srv_profile_op *spo)
{
	struct srv_profile_chunk *chunk = spo->pro_current_chunk;

	if (spo->pro_acc_cnt == 0)
		return;

	D_ASSERT(chunk != NULL);
	D_ASSERT(chunk->spc_chunk_offset <= chunk->spc_chunk_size);

	chunk->spc_chunks[chunk->spc_chunk_offset] =
				spo->pro_acc_val / spo->pro_acc_cnt;
	chunk->spc_chunk_offset++;
	spo->pro_acc_val = 0;
	spo->pro_acc_cnt = 0;
}

static int
spc_dump_file(struct srv_profile_op *sp, FILE *file,
	      struct srv_profile_chunk *spc)
{
	char string[64] = { 0 };
	int rc = 0;
	int i;

	for (i = 0; i < spc->spc_chunk_offset; i++) {
		size_t size;

		memset(string, 0, 64);
		/* Dump name and time cost to the file */
		snprintf(string, 64, "%s "DF_U64"\n", sp->pro_op_name,
			 spc->spc_chunks[i]);
		size = fwrite(string, 1, strlen(string), file);
		if (size != strlen(string)) {
			D_ERROR("dump failed: %s\n", strerror(errno));
			rc = daos_errno2der(errno);
			break;
		}
	}

	return rc;
}

static void
srv_profile_dump(void *arg)
{
	struct srv_profile	*sp = arg;
	d_rank_t	rank;
	FILE		*file;
	char		name[64] = { 0 };
	char		*path;
	int		rc;

	rc = crt_group_rank(NULL, &rank);
	if (rc) {
		D_ERROR("start dump ult failed: rc "DF_RC"\n", DP_RC(rc));
		return;
	}

	if (sp->sp_dir_path) {
		D_ALLOC(path, strlen(sp->sp_dir_path) + 64);
		if (path == NULL) {
			rc = -DER_NOMEM;
			D_ERROR("start dump ult failed: rc "DF_RC"\n",
				DP_RC(rc));
			return;
		}

		sprintf(name, "/profile-%d-%d.dump", rank, sp->sp_id);
		strcpy(path, sp->sp_dir_path);
		strcat(path, name);
	} else {
		sprintf(name, "./profile-%d-%d.dump", rank, sp->sp_id);
		path = name;
	}

	file = fopen(path, "a");
	if (file == NULL) {
		rc = daos_errno2der(errno);
		D_ERROR("open %s: %s\n", path, strerror(errno));
		goto out;
	}

	while (1) {
		int i;

		for (i = 0; i < sp->sp_ops_cnt; i++) {
			struct srv_profile_op *spo;
			struct srv_profile_chunk *spc;
			struct srv_profile_chunk *tmp;

			spo = &sp->sp_ops[i];
			/* Only start dump until it used up 3/4 total chunks */
			if (spo->pro_chunk_cnt < DEFAULT_DUMP_CHUNK_THRESHOLD &&
			    !sp->sp_stop)
				continue;

			d_list_for_each_entry_safe(spc, tmp,
						   &spo->pro_chunk_list,
						   spc_chunk_list) {
				if (spc == spo->pro_current_chunk) {
					/* Dump current chunk during stop */
					if (!sp->sp_stop)
						continue;
					/* Close the current one */
					srv_profile_chunk_next(&sp->sp_ops[i]);
				}

				if (spc->spc_chunk_offset > 0)
					sp->sp_empty = 0;
				rc = spc_dump_file(spo, file, spc);
				if (rc)
					break;
				/* move the spc to idle list */
				spo->pro_chunk_cnt--;
				spc->spc_chunk_offset = 0;
				d_list_move_tail(&spc->spc_chunk_list,
						 &spo->pro_chunk_idle_list);

				if (!sp->sp_stop &&
				    spo->pro_chunk_cnt <
					DEFAULT_DUMP_CHUNK_THRESHOLD)
					break;
			}
		}

		if (sp->sp_stop)
			break;
		ABT_thread_yield();
	}

	fclose(file);
	if (sp->sp_empty)
		unlink(path);
out:
	if (path != name)
		free(path);
}

char *profile_op_names[] = {
	[OBJ_PF_UPDATE_PREP] = "update_prep",
	[OBJ_PF_UPDATE_DISPATCH] = "update_dispatch",
	[OBJ_PF_UPDATE_LOCAL] = "update_local",
	[OBJ_PF_UPDATE_END] = "update_end",
	[OBJ_PF_UPDATE_WAIT] = "update_end",
	[OBJ_PF_UPDATE_REPLY] = "update_repl",
	[OBJ_PF_UPDATE] = "update",
	[VOS_UPDATE_END] = "vos_update_end",
};

int
srv_profile_start(char *path, int avg)
{
	struct srv_profile **sp_p = &dss_get_module_info()->dmi_sp;
	int		   tgt_id = dss_get_module_info()->dmi_tgt_id;
	struct srv_profile *sp;
	int		   i;
	int		   rc;

	sp = srv_profile_alloc(PF_MAX_CNT);
	if (sp == NULL)
		return -DER_NOMEM;

	sp->sp_empty = 1;
	D_ASSERT(PF_MAX_CNT == ARRAY_SIZE(profile_op_names));
	for (i = 0; i < PF_MAX_CNT; i++) {
		rc = srv_profile_op_init(&sp->sp_ops[i], i,
					 profile_op_names[i]);
		if (rc)
			D_GOTO(out, rc);
	}

	if (path != NULL) {
		D_ALLOC(sp->sp_dir_path, strlen(path) + 1);
		if (sp->sp_dir_path == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		strcpy(sp->sp_dir_path, path);
	}

	sp->sp_stop = 0;
	sp->sp_avg = avg;
	sp->sp_id = tgt_id;

	/* TODO: this ULT probably need in a lower priority ULT */
	rc = dss_ult_create(srv_profile_dump, sp, DSS_ULT_MISC,
			    tgt_id, 0, &sp->sp_dump_thread);
	if (rc)
		goto out;

	*sp_p = sp;
out:
	if (rc && sp != NULL)
		srv_profile_destroy(sp);

	return rc;
}

int
srv_profile_count(struct srv_profile *sp, int id, int val)
{
	struct srv_profile_chunk *current;
	struct srv_profile_op	 *spo;

	spo = &sp->sp_ops[id];
	spo->pro_acc_val += val;
	spo->pro_acc_cnt++;
	if (spo->pro_acc_cnt >= sp->sp_avg && sp->sp_avg != -1) {
		D_ASSERT(spo->pro_current_chunk != NULL);
		current = spo->pro_current_chunk;
		/* Current profile chunk is full, get a new one */
		if (current->spc_chunk_offset == current->spc_chunk_size) {
			int	rc;

			rc = srv_profile_get_new_chunk(spo);
			if (rc)
				return rc;
		}
		srv_profile_chunk_next(spo);
	}

	return 0;
}

int
srv_profile_stop(void)
{
	struct dss_module_info	*dmi = dss_get_module_info(); 
	struct srv_profile *sp = dmi->dmi_sp;

	if (sp == NULL)
		return 0;

	sp->sp_stop = 1;

	ABT_thread_join(sp->sp_dump_thread);
	srv_profile_destroy(sp);
	dmi->dmi_sp = NULL;
	
	return 0;
}
