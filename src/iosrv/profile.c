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
struct srv_profile_chunk *
srv_profile_chunk_alloc(int chunk_size)
{
	struct srv_profile_chunk *chunk;

	D_ALLOC_PTR(chunk);
	if (chunk == NULL)
		return NULL;

	D_INIT_LIST_HEAD(&chunk->spc_chunk_list);

	D_ALLOC(chunk->spc_profiles,
		chunk_size * sizeof(*chunk->spc_profiles));
	if (chunk->spc_profiles == NULL) {
		D_FREE_PTR(chunk);
		return NULL;
	}

	chunk->spc_chunk_size = chunk_size;
	chunk->spc_idx = 0;
	return chunk;
}

void
srv_profile_chunk_destroy(struct srv_profile_chunk *chunk)
{
	d_list_del(&chunk->spc_chunk_list);
	D_FREE(chunk->spc_profiles);
	D_FREE_PTR(chunk);
}

struct srv_profile *
srv_profile_alloc()
{
	struct srv_profile	*sp;

	D_ALLOC_PTR(sp);
	if (sp) {
		D_INIT_LIST_HEAD(&sp->sp_list);
		D_INIT_LIST_HEAD(&sp->sp_idle_list);
	}

	return sp;
}

void
srv_profile_destroy(struct srv_profile *sp)
{
	struct srv_profile_chunk *spc;
	struct srv_profile_chunk *tmp;

	d_list_for_each_entry_safe(spc, tmp, &sp->sp_list,
				      spc_chunk_list) {
		srv_profile_chunk_destroy(spc);
	}

	d_list_for_each_entry_safe(spc, tmp, &sp->sp_idle_list,
				      spc_chunk_list) {
		srv_profile_chunk_destroy(spc);
	}

	if (sp->sp_dir_path)
		D_FREE(sp->sp_dir_path);

	ABT_thread_free(&sp->sp_dump_thread);
	D_FREE_PTR(sp);
}

static int
srv_profile_get_new_chunk(struct srv_profile *sp)
{
	struct srv_profile_chunk *chunk;

	if (!d_list_empty(&sp->sp_idle_list)) {
		/* Try to get one from idle list */
		chunk = d_list_entry(sp->sp_idle_list.next,
				     struct srv_profile_chunk,
				     spc_chunk_list);
		d_list_move_tail(&chunk->spc_chunk_list, &sp->sp_list);
		sp->sp_chunk_cnt++;
		D_ASSERT(sp->sp_chunk_cnt <= sp->sp_chunk_total_cnt);
	} else if (sp->sp_chunk_total_cnt < DEFAULT_CHUNK_CNT_LIMIT) {
		/* Allocate new one */
		chunk = srv_profile_chunk_alloc(DEFAULT_CHUNK_SIZE);
		if (chunk == NULL)
			return -DER_NOMEM;

		d_list_add_tail(&chunk->spc_chunk_list, &sp->sp_list);
		sp->sp_chunk_total_cnt++;
		sp->sp_chunk_cnt++;
	} else {
		/* Reach the limit, Let's reuse the oldest (i.e. 1st) in list */
		chunk = d_list_entry(sp->sp_list.next, struct srv_profile_chunk,
				     spc_chunk_list);
		chunk->spc_idx = 0;
	}

	sp->sp_current_chunk = chunk;
	return 0;
}

static int
spc_dump_file(struct srv_profile *sp, FILE *file, struct srv_profile_chunk *spc)
{
	char string[64] = { 0 };
	int rc = 0;
	int i;

	for (i = 0; i < spc->spc_idx; i++) {
		int id = spc->spc_profiles[i].pro_id;
		size_t size;

		memset(string, 0, 64);
		/* Dump name and time cost to the file */
		snprintf(string, 64, "%s "DF_U64"\n", sp->sp_names[id],
			 spc->spc_profiles[i].pro_time);
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
	struct srv_profile_chunk *spc;
	struct srv_profile_chunk *tmp;
	d_rank_t	rank;
	FILE		*file;
	int		tgt_id;
	char		name[64] = { 0 };
	char		*path;
	int		rc;

	tgt_id = dss_get_module_info()->dmi_xs_id;
	rc = crt_group_rank(NULL, &rank);
	if (rc) {
		D_ERROR("start dump ult failed: rc "DF_RC"\n", DP_RC(rc));
		return;
	}

	if (sp->sp_dir_path) {
		D_ALLOC(path, strlen(sp->sp_dir_path) + 64);
		if (path == NULL) {
			rc = -DER_NOMEM;
			D_ERROR("start dump ult failed: rc "DF_RC"\n", DP_RC(rc));
			return;
		}

		sprintf(name, "/profile-%d-%d.dump", rank, tgt_id);
		strcpy(path, sp->sp_dir_path);
		strcat(path, name);
	} else {
		sprintf(name, "./profile-%d-%d.dump", rank, tgt_id);
		path = name;
	}

	file = fopen(path, "a");
	if (file == NULL) {
		rc = daos_errno2der(errno);
		D_ERROR("open %s: %s\n", path, strerror(errno));
		goto out;
	}

	while (1) {
		if (sp->sp_chunk_cnt < DEFAULT_DUMP_CHUNK_THRESHOLD &&
		    !sp->sp_stop) {
			ABT_thread_yield();
			continue;
		}
		/* Only start dump until it used up 3/4 of total chunks */
		d_list_for_each_entry_safe(spc, tmp, &sp->sp_list,
					   spc_chunk_list) {
			if (spc->spc_idx == spc->spc_chunk_size ||
			    sp->sp_stop) {
				d_list_del(&spc->spc_chunk_list);
				rc = spc_dump_file(sp, file, spc);
				if (rc)
					break;
				/* move the spc to idle list */
				spc->spc_idx = 0;
				sp->sp_chunk_cnt--;
				d_list_move_tail(&spc->spc_chunk_list,
						 &sp->sp_idle_list);
				if (!sp->sp_stop)
					ABT_thread_yield();
			}
			if (!sp->sp_stop &&
			    sp->sp_chunk_cnt < DEFAULT_DUMP_CHUNK_THRESHOLD)
				break;
		}

		if (sp->sp_stop && d_list_empty(&sp->sp_list))
			break;
	}

	fclose(file);
out:
	if (path != name)
		free(path);
}

int
srv_profile_start(struct srv_profile **sp_p, char *path, char **names)
{
	struct srv_profile *sp;
	int		   tgt_id = dss_get_module_info()->dmi_tgt_id;
	int		   rc;

	sp = srv_profile_alloc();
	if (sp == NULL)
		return -DER_NOMEM;

	if (path != NULL) {
		D_ALLOC(sp->sp_dir_path, strlen(path) + 1);
		if (sp->sp_dir_path == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		strcpy(sp->sp_dir_path, path);
	}

	rc = srv_profile_get_new_chunk(sp);
	if (rc)
		goto out;

	sp->sp_names = names;

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

static int
srv_profile_get_next_chunk(struct srv_profile *sp)
{
	int rc;

	rc = srv_profile_get_new_chunk(sp);
	return rc;

}

int
srv_profile_count(struct srv_profile *sp, int id, int time)
{
	struct srv_profile_chunk *current;

	D_ASSERT(sp->sp_current_chunk != NULL);
	current = sp->sp_current_chunk;

	if (current->spc_idx == current->spc_chunk_size) {
		int	rc;

		rc = srv_profile_get_next_chunk(sp);
		if (rc)
			return rc;
		current = sp->sp_current_chunk;
	}

	current->spc_profiles[current->spc_idx].pro_time = time;
	current->spc_profiles[current->spc_idx].pro_id = id;
	current->spc_idx++;

	return 0;
}

int
srv_profile_stop(struct srv_profile *sp)
{
	sp->sp_stop = 1;

	ABT_thread_join(sp->sp_dump_thread);
	srv_profile_destroy(sp);

	return 0;
}
