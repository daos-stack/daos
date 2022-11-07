/**
 * (C) Copyright 2021-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(tests)

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <getopt.h>

#include <daos/tests_lib.h>
#include <daos/common.h>
#include <daos/btree_class.h>
#include <daos_srv/vea.h>
#include "../vea_internal.h"

char pool_file[PATH_MAX];
uint64_t heap_size		= (100UL << 20);	/* 100MB */
uint64_t pool_capacity		= (1024ULL << 30);	/* 1TB */
unsigned int cont_per_pool	= 1;
unsigned int obj_per_cont	= 100;
unsigned int test_duration	= (2 * 60);		/* 2 mins */
unsigned int rand_seed;
bool loading_test;					/* test loading pool */

uint64_t start_ts;
unsigned int stats_intvl	= 5;			/* seconds */

enum {
	CONT_STREAM_IO	= 0,
	CONT_STREAM_AGG,
	CONT_STREAM_CNT,
};

#define VS_BLK_SIZE		(1UL << 12)	/* 4k bytes */
#define VS_RSRV_CNT_MAX		10		/* extents */
#define VS_FREE_CNT_MAX		30		/* extents */
#define VS_MERGE_CNT_MAX	10		/* extents */
#define VS_UPD_BLKS_MAX		256		/* 1MB */
#define VS_AGG_BLKS_MAX		1024		/* 4MB */

struct vs_perf_cntr {
	uint64_t	vpc_count;		/* sample counter */
	uint64_t	vpc_tot;		/* total us */
	uint32_t	vpc_max;		/* max us */
	uint32_t	vpc_min;		/* min us */
};

enum {
	VS_OP_RESERVE	= 0,
	VS_OP_PUBLISH,
	VS_OP_FREE,
	VS_OP_MERGE,
	VS_OP_MAX,
};

struct vea_stress_list {
	d_list_t			 vsl_list;		/* vea_resrvd_ext list */
	d_list_t			*vsl_cursor;
	unsigned int			 vsl_count;		/* list item count */
};

struct vea_stress_cont;

struct vea_stress_obj {
	d_list_t			 vso_link;		/* link to vsp_objs */
	struct vea_stress_list		 vso_alloc_list;	/* allocated extents */
	struct vea_stress_cont		*vso_cont;
	uint64_t			 vso_alloc_blks;	/* allocated blocks */
};

struct vea_stress_cont {
	struct vea_hint_df		*vsc_hd[CONT_STREAM_CNT];
	struct vea_hint_context		*vsc_hc[CONT_STREAM_CNT];
	struct vea_stress_obj		*vsc_objs;
};

struct vea_stress_pool {
	struct umem_instance		 vsp_umm;
	struct umem_tx_stage_data	 vsp_txd;
	struct vea_space_df		*vsp_vsd;
	struct vea_space_info		*vsp_vsi;
	d_list_t			 vsp_objs;		/* non-empty objects */
	struct vea_stress_list		 vsp_punched_list;	/* punched extents */
	uint64_t			 vsp_tot_blks;		/* total blocks */
	uint64_t			 vsp_free_blks;		/* free blocks */
	uint64_t			 vsp_alloc_blks;	/* allocated blocks */
	struct vs_perf_cntr		 vsp_cntr[VS_OP_MAX];
	struct vea_stress_cont		 vsp_conts[0];
};

static void
vs_counter_inc(struct vs_perf_cntr *cntr, uint64_t ts)
{
	uint64_t elapsed = daos_getutime();

	if (elapsed > ts)
		elapsed -= ts;
	else
		elapsed = 0;

	cntr->vpc_count++;
	cntr->vpc_tot += elapsed;
	if (cntr->vpc_max < elapsed)
		cntr->vpc_max = elapsed;
	if (cntr->vpc_min > elapsed)
		cntr->vpc_min = elapsed;
}

static bool
vs_list_empty(struct vea_stress_list *vs_head)
{
	if (vs_head->vsl_count > 0) {
		D_ASSERT(!d_list_empty(&vs_head->vsl_list));
		return false;
	}
	D_ASSERT(d_list_empty(&vs_head->vsl_list));
	return true;
}

static void
vs_list_move_cursor(struct vea_stress_list *vs_head)
{
	D_ASSERT(!vs_list_empty(vs_head));

	vs_head->vsl_cursor = vs_head->vsl_cursor->next;
	/* Adjust cursor when it reaches header */
	if (vs_head->vsl_cursor == &vs_head->vsl_list)
		vs_head->vsl_cursor = vs_head->vsl_list.next;
}

static void
vs_list_teardown(struct vea_stress_list *vs_head)
{
	struct vea_resrvd_ext	*rsrvd, *tmp;

	d_list_for_each_entry_safe(rsrvd, tmp, &vs_head->vsl_list, vre_link) {
		d_list_del_init(&rsrvd->vre_link);
		D_ASSERT(vs_head->vsl_count > 0);
		vs_head->vsl_count--;
		D_FREE(rsrvd);
	}
	D_ASSERT(vs_head->vsl_count == 0);
}

static void
vs_list_init(struct vea_stress_list *vs_head)
{

	D_INIT_LIST_HEAD(&vs_head->vsl_list);
	vs_head->vsl_cursor = &vs_head->vsl_list;
	vs_head->vsl_count = 0;
}

static void
vs_list_splice_init(struct vea_stress_list *vs_list, struct vea_stress_list *vs_head)
{
	if (vs_list_empty(vs_list))
		return;

	d_list_splice(&vs_list->vsl_list, &vs_head->vsl_list);
	vs_head->vsl_count += vs_list->vsl_count;
	vs_list_init(vs_list);
}

static void
vs_list_insert(d_list_t *list, unsigned int count, struct vea_stress_list *vs_head)
{
	if (count == 0) {
		D_ASSERT(d_list_empty(list));
		return;
	}

	d_list_splice_init(list, &vs_head->vsl_list);
	vs_head->vsl_count += count;
}

/*
 * Pop a random vea_resrvd_ext from @vs_head, when @max_blks is specified, the popped
 * extent must be smaller than @max_blks.
 */
static struct vea_resrvd_ext *
vs_list_pop_one(struct vea_stress_list *vs_head, unsigned int max_blks)
{
	struct vea_resrvd_ext	*rsrvd;
	d_list_t		*start_pos, *picked;
	unsigned int		 rand_steps;

	if (vs_list_empty(vs_head))
		return NULL;

	D_ASSERT(vs_head->vsl_cursor != NULL);
	if (vs_head->vsl_cursor == &vs_head->vsl_list)
		vs_head->vsl_cursor = vs_head->vsl_list.next;

	/* To pick a random item, move random steps from current cursor */
	rand_steps = rand() % min(100, vs_head->vsl_count);
	while (rand_steps) {
		vs_list_move_cursor(vs_head);
		rand_steps--;
	}

	/* Search for a qualified item start from current position */
	start_pos = picked = vs_head->vsl_cursor;
	rsrvd = d_list_entry(picked, struct vea_resrvd_ext, vre_link);
	while (max_blks && rsrvd->vre_blk_cnt >= max_blks) {
		vs_list_move_cursor(vs_head);
		/* Reached start position */
		if (start_pos == vs_head->vsl_cursor) {
			picked = NULL;
			break;
		}
		picked = vs_head->vsl_cursor;
		rsrvd = d_list_entry(picked, struct vea_resrvd_ext, vre_link);
	}

	/* No qualified item found */
	if (picked == NULL) {
		vs_head->vsl_cursor = start_pos;
		return NULL;
	}

	vs_list_move_cursor(vs_head);
	/* Cursor is to be deleted, reset to header */
	if (picked == vs_head->vsl_cursor)
		vs_head->vsl_cursor = &vs_head->vsl_list;

	d_list_del_init(&rsrvd->vre_link);
	vs_head->vsl_count--;

	return rsrvd;
}

static inline bool
need_punch(struct vea_stress_pool *vs_pool)
{
	/* Punch an object when half blocks are allocated */
	return vs_pool->vsp_alloc_blks > (vs_pool->vsp_tot_blks / 2);
}

static int
vs_punch(struct vea_stress_pool *vs_pool)
{
	struct vea_stress_obj	*vs_obj;

	vs_obj = d_list_pop_entry(&vs_pool->vsp_objs, struct vea_stress_obj, vso_link);
	if (vs_obj == NULL) {
		fprintf(stderr, "no object can be punched\n");
		return -DER_INVAL;
	}

	vs_list_splice_init(&vs_obj->vso_alloc_list, &vs_pool->vsp_punched_list);

	D_ASSERT(vs_pool->vsp_alloc_blks >= vs_obj->vso_alloc_blks);
	vs_pool->vsp_alloc_blks -= vs_obj->vso_alloc_blks;
	vs_obj->vso_alloc_blks = 0;

	return 0;
}

static inline unsigned int
get_random_count(unsigned int max)
{
	unsigned int cnt;

	cnt = (rand() % (max + 1));
	return cnt == 0 ? 1 : cnt;
}

static inline struct vea_stress_cont *
pick_update_cont(struct vea_stress_pool *vs_pool)
{
	unsigned int cont_idx = rand() % cont_per_pool;

	return &vs_pool->vsp_conts[cont_idx];
}

static inline struct vea_stress_obj *
pick_update_obj(struct vea_stress_cont *vs_cont)
{
	unsigned int obj_idx = rand() % obj_per_cont;

	return &vs_cont->vsc_objs[obj_idx];
}

/* Perform few allocations for a random object */
static int
vs_update(struct vea_stress_pool *vs_pool)
{
	struct vea_stress_cont	*vs_cont;
	struct vea_stress_obj	*vs_obj;
	struct vea_hint_context	*hint;
	d_list_t		 r_list, a_list;
	struct vea_resrvd_ext	*rsrvd, *dup;
	unsigned int		 blk_cnt, rsrv_cnt, alloc_blks = 0;
	uint64_t		 cur_ts;
	int			 i, rc;

	vs_cont = pick_update_cont(vs_pool);
	vs_obj = pick_update_obj(vs_cont);

	hint = vs_cont->vsc_hc[CONT_STREAM_IO];
	D_ASSERT(hint != NULL);

	D_INIT_LIST_HEAD(&r_list);
	D_INIT_LIST_HEAD(&a_list);

	rsrv_cnt = get_random_count(VS_RSRV_CNT_MAX);
	for (i = 0; i < rsrv_cnt; i++) {
		blk_cnt = get_random_count(VS_UPD_BLKS_MAX);

		cur_ts = daos_getutime();
		rc = vea_reserve(vs_pool->vsp_vsi, blk_cnt, hint, &r_list);
		if (rc != 0) {
			fprintf(stderr, "failed to reserve %u blks for io\n", blk_cnt);
			goto error;
		}
		vs_counter_inc(&vs_pool->vsp_cntr[VS_OP_RESERVE], cur_ts);

		/*
		 * Reserved list will be freed on publish, duplicate it to track the
		 * allocated extents.
		 */
		rsrvd = d_list_entry(r_list.prev, struct vea_resrvd_ext, vre_link);
		D_ASSERT(rsrvd->vre_blk_cnt == blk_cnt);
		D_ALLOC_PTR(dup);
		if (dup == NULL) {
			fprintf(stderr, "failed to alloc dup ext\n");
			rc = -DER_NOMEM;
			goto error;
		}

		D_INIT_LIST_HEAD(&dup->vre_link);
		dup->vre_blk_off = rsrvd->vre_blk_off;
		dup->vre_blk_cnt = rsrvd->vre_blk_cnt;
		d_list_add(&dup->vre_link, &a_list);
		alloc_blks += dup->vre_blk_cnt;
	}

	cur_ts = daos_getutime();
	rc = umem_tx_begin(&vs_pool->vsp_umm, &vs_pool->vsp_txd);
	D_ASSERT(rc == 0);

	rc = vea_tx_publish(vs_pool->vsp_vsi, hint, &r_list);
	D_ASSERT(rc == 0);

	rc = umem_tx_commit(&vs_pool->vsp_umm);
	D_ASSERT(rc == 0);
	vs_counter_inc(&vs_pool->vsp_cntr[VS_OP_PUBLISH], cur_ts);

	vs_list_insert(&a_list, rsrv_cnt, &vs_obj->vso_alloc_list);
	vs_obj->vso_alloc_blks += alloc_blks;

	D_ASSERT(vs_pool->vsp_free_blks > alloc_blks);
	vs_pool->vsp_free_blks -= alloc_blks;
	vs_pool->vsp_alloc_blks += alloc_blks;

	if (d_list_empty(&vs_obj->vso_link))
		d_list_add_tail(&vs_obj->vso_link, &vs_pool->vsp_objs);

	return rc;
error:
	vea_cancel(vs_pool->vsp_vsi, hint, &r_list);
	d_list_for_each_entry_safe(rsrvd, dup, &a_list, vre_link) {
		d_list_del_init(&rsrvd->vre_link);
		D_FREE(rsrvd);
	}

	return rc;
}

/* Free few punched extents */
static int
vs_reclaim(struct vea_stress_pool *vs_pool)
{
	struct vea_resrvd_ext	*rsrvd, *tmp;
	unsigned int		 free_cnt;
	d_list_t		 f_list;
	uint64_t		 cur_ts;
	int			 i, rc;

	D_ASSERT(!vs_list_empty(&vs_pool->vsp_punched_list));
	free_cnt = get_random_count(VS_FREE_CNT_MAX);
	D_INIT_LIST_HEAD(&f_list);

	for (i = 0; i < free_cnt; i++) {
		rsrvd = vs_list_pop_one(&vs_pool->vsp_punched_list, 0);
		if (rsrvd == NULL) {
			D_ASSERT(i > 0);
			break;
		}
		d_list_add_tail(&rsrvd->vre_link, &f_list);
	}

	cur_ts = daos_getutime();
	rc = umem_tx_begin(&vs_pool->vsp_umm, &vs_pool->vsp_txd);
	D_ASSERT(rc == 0);

	d_list_for_each_entry_safe(rsrvd, tmp, &f_list, vre_link) {
		d_list_del_init(&rsrvd->vre_link);
		rc = vea_free(vs_pool->vsp_vsi, rsrvd->vre_blk_off, rsrvd->vre_blk_cnt);
		D_ASSERT(rc == 0);
		vs_pool->vsp_free_blks += rsrvd->vre_blk_cnt;
		D_FREE(rsrvd);
	}

	rc = umem_tx_commit(&vs_pool->vsp_umm);
	D_ASSERT(rc == 0);
	vs_counter_inc(&vs_pool->vsp_cntr[VS_OP_FREE], cur_ts);

	return rc;
}

/* Coalesce few allocated extents from an object */
static int
vs_coalesce(struct vea_stress_pool *vs_pool)
{
	struct vea_hint_context	*hint;
	struct vea_stress_cont	*vs_cont;
	struct vea_stress_obj	*vs_obj;
	struct vea_resrvd_ext	*rsrvd, *tmp, *dup;
	d_list_t		 f_list, r_list, a_list;
	unsigned int		 merge_cnt, merge_blks = 0;
	uint64_t		 cur_ts;
	int			 i, rc;

	/* No non-empty objs */
	if (d_list_empty(&vs_pool->vsp_objs))
		return 0;

	merge_cnt = get_random_count(VS_MERGE_CNT_MAX);
	D_INIT_LIST_HEAD(&f_list);
	D_INIT_LIST_HEAD(&r_list);
	D_INIT_LIST_HEAD(&a_list);

	vs_obj = d_list_entry(vs_pool->vsp_objs.next, struct vea_stress_obj, vso_link);
	/* Re-insert to tail */
	d_list_del_init(&vs_obj->vso_link);
	d_list_add_tail(&vs_obj->vso_link, &vs_pool->vsp_objs);

	vs_cont = vs_obj->vso_cont;
	hint = vs_cont->vsc_hc[CONT_STREAM_AGG];
	D_ASSERT(hint != NULL);

	for (i = 0; i < merge_cnt; i++) {
		rsrvd = vs_list_pop_one(&vs_obj->vso_alloc_list, VS_AGG_BLKS_MAX);
		if (rsrvd != NULL) {
			d_list_add_tail(&rsrvd->vre_link, &f_list);
			merge_blks += rsrvd->vre_blk_cnt;
		}

		if (rsrvd == NULL || merge_blks >= VS_AGG_BLKS_MAX)
			break;
	}

	/* Nothing can be merged */
	if (merge_blks == 0)
		return 0;

	/* Reserve blocks for coalesced extent */
	cur_ts = daos_getutime();
	rc = vea_reserve(vs_pool->vsp_vsi, merge_blks, hint, &r_list);
	if (rc != 0) {
		fprintf(stderr, "failed to reserve %u blks for aggregation\n", merge_blks);
		return rc;
	}
	vs_counter_inc(&vs_pool->vsp_cntr[VS_OP_RESERVE], cur_ts);

	rsrvd = d_list_entry(r_list.prev, struct vea_resrvd_ext, vre_link);
	D_ASSERT(rsrvd->vre_blk_cnt == merge_blks);
	D_ALLOC_PTR(dup);
	if (dup == NULL) {
		fprintf(stderr, "failed to alloc dup ext\n");
		rc = -DER_NOMEM;
		goto error;
	}
	D_INIT_LIST_HEAD(&dup->vre_link);
	dup->vre_blk_off = rsrvd->vre_blk_off;
	dup->vre_blk_cnt = rsrvd->vre_blk_cnt;
	d_list_add(&dup->vre_link, &a_list);

	cur_ts = daos_getutime();
	rc = umem_tx_begin(&vs_pool->vsp_umm, &vs_pool->vsp_txd);
	D_ASSERT(rc == 0);

	/* Free old allocated extents */
	d_list_for_each_entry_safe(rsrvd, tmp, &f_list, vre_link) {
		d_list_del_init(&rsrvd->vre_link);
		rc = vea_free(vs_pool->vsp_vsi, rsrvd->vre_blk_off, rsrvd->vre_blk_cnt);
		D_ASSERT(rc == 0);
		D_FREE(rsrvd);
	}

	/* Publish coalesced extent */
	rc = vea_tx_publish(vs_pool->vsp_vsi, hint, &r_list);
	D_ASSERT(rc == 0);

	rc = umem_tx_commit(&vs_pool->vsp_umm);
	D_ASSERT(rc == 0);
	vs_counter_inc(&vs_pool->vsp_cntr[VS_OP_MERGE], cur_ts);

	vs_list_insert(&a_list, 1, &vs_obj->vso_alloc_list);
	return rc;
error:
	vea_cancel(vs_pool->vsp_vsi, hint, &r_list);
	d_list_for_each_entry_safe(rsrvd, tmp, &f_list, vre_link) {
		d_list_del_init(&rsrvd->vre_link);
		D_FREE(rsrvd);
	}
	return rc;
}

static int
vs_aggregate(struct vea_stress_pool *vs_pool, unsigned int io_percent)
{
	if (!vs_list_empty(&vs_pool->vsp_punched_list) &&
	    (io_percent <= 50 || (rand() % 2 == 0)))
		return vs_reclaim(vs_pool);

	return vs_coalesce(vs_pool);
}

static unsigned int
get_io_percent(struct vea_stress_pool *vs_pool)
{
	unsigned int io_percent;

	if (vs_pool->vsp_free_blks > (vs_pool->vsp_tot_blks * 2 / 3))
		io_percent = 70;
	else if (vs_pool->vsp_free_blks > (vs_pool->vsp_tot_blks / 2))
		io_percent = 50;
	else if (vs_pool->vsp_free_blks > (vs_pool->vsp_tot_blks / 3))
		io_percent = 30;
	else
		io_percent = 10;

	return io_percent;
}

static int
vs_run_one(struct vea_stress_pool *vs_pool)
{
	unsigned int	io_percent;

	if (need_punch(vs_pool))
		return vs_punch(vs_pool);

	io_percent = get_io_percent(vs_pool);
	if ((rand() % 100) < io_percent)
		return vs_update(vs_pool);
	else
		return vs_aggregate(vs_pool, io_percent);
}

#define DF_12U64	"%-12" PRIu64

static bool
vs_stop_run(struct vea_stress_pool *vs_pool, int rc)
{
	static uint64_t	last_print_ts;
	uint64_t	now = daos_wallclock_secs(), heap_bytes;
	struct vea_stat	stat;
	unsigned int	duration = 0;
	bool		stop;
	int		ret;

	duration = (now > start_ts) ? now - start_ts : 0;

	if (duration > test_duration || rc) {
		fprintf(stdout, "Used %u seconds, rc:%d\n", duration, rc);
		stop = true;
	} else {
		stop = false;
	}

	if (!stop && (last_print_ts + stats_intvl > now))
		return stop;

	fprintf(stdout, "\n== frag info ("DF_U64" seconds elapsed since last report)\n",
		last_print_ts ? now - last_print_ts : 0);
	last_print_ts = now;

	ret = umempobj_get_heapusage(vs_pool->vsp_umm.umm_pool, &heap_bytes);
	if (ret) {
		fprintf(stderr, "failed to get heap usage\n");
		return stop;
	}

	fprintf(stdout, "total blks:"DF_12U64" free blks:"DF_12U64" allocated blks:"DF_12U64" "
		"heap_bytes:"DF_U64"\n", vs_pool->vsp_tot_blks, vs_pool->vsp_free_blks,
		vs_pool->vsp_alloc_blks, heap_bytes);

	ret = vea_query(vs_pool->vsp_vsi, NULL, &stat);
	if (ret) {
		fprintf(stderr, "vea_query failed:%d\n", rc);
		return stop;
	}

	fprintf(stdout, "free_blks:["DF_12U64","DF_12U64"] frags_l:"DF_12U64" frags_s:"DF_12U64" "
		"frags_a:"DF_12U64" r_hint:"DF_12U64" r_large:"DF_12U64" r_small:"DF_12U64"\n",
		stat.vs_free_persistent, stat.vs_free_transient, stat.vs_frags_large,
		stat.vs_frags_small, stat.vs_frags_aging, stat.vs_resrv_hint, stat.vs_resrv_large,
		stat.vs_resrv_small);

	return stop;
}

static int
vs_stress_run(struct vea_stress_pool *vs_pool)
{
	int	rc = 0;

	while (!vs_stop_run(vs_pool, rc))
		rc = vs_run_one(vs_pool);

	fprintf(stdout, "\n");
	return rc;
}

static inline size_t
vs_root_size(void)
{
	return sizeof(struct vea_space_df) +
		sizeof(struct vea_hint_df) * CONT_STREAM_CNT * cont_per_pool;
}

static inline unsigned int
vs_arg_size(void)
{
	return sizeof(struct vea_stress_pool) +
		sizeof(struct vea_stress_cont) * cont_per_pool;
}

static void
vs_teardown_objs(struct vea_stress_cont *vs_cont)
{
	struct vea_stress_obj	*vs_obj;
	unsigned int		 i;

	if (vs_cont->vsc_objs == NULL)
		return;

	for (i = 0; i < obj_per_cont; i++) {
		vs_obj = &vs_cont->vsc_objs[i];

		vs_list_teardown(&vs_obj->vso_alloc_list);
		vs_obj->vso_alloc_blks = 0;
	}
	D_FREE(vs_cont->vsc_objs);
}

static void
vs_teardown_conts(struct vea_stress_pool *vs_pool)
{
	struct vea_stress_cont	*vs_cont;
	int i, j;

	for (i = 0; i < cont_per_pool; i++) {
		vs_cont = &vs_pool->vsp_conts[i];

		for (j = 0; j < CONT_STREAM_CNT; j++) {
			if (vs_cont->vsc_hc[j] != NULL) {
				vea_hint_unload(vs_cont->vsc_hc[j]);
				vs_cont->vsc_hc[j] = NULL;
			}
		}
		vs_teardown_objs(vs_cont);
	}
}

static int
vs_setup_conts(struct vea_stress_pool *vs_pool, void *addr)
{
	struct vea_stress_cont	*vs_cont;
	struct vea_stress_obj	*vs_obj;
	int i, j, k, rc;

	for (i = 0; i < cont_per_pool; i++) {
		vs_cont = &vs_pool->vsp_conts[i];

		for (j = 0; j < CONT_STREAM_CNT; j++) {
			vs_cont->vsc_hd[j] = addr;
			addr += sizeof(struct vea_hint_df);

			vs_cont->vsc_hd[j]->vhd_off = 0;
			vs_cont->vsc_hd[j]->vhd_seq = 0;
			rc = vea_hint_load(vs_cont->vsc_hd[j], &vs_cont->vsc_hc[j]);
			if (rc) {
				fprintf(stderr, "failed to load hint\n");
				return -1;
			}
		}

		D_ALLOC(vs_cont->vsc_objs, sizeof(*vs_obj) * obj_per_cont);
		if (vs_cont->vsc_objs == NULL) {
			fprintf(stderr, "failed to allocate objs\n");
			return -1;
		}

		for (k = 0; k < obj_per_cont; k++) {
			vs_obj = &vs_cont->vsc_objs[k];

			vs_obj->vso_cont = vs_cont;
			D_INIT_LIST_HEAD(&vs_obj->vso_link);
			vs_list_init(&vs_obj->vso_alloc_list);
			vs_obj->vso_alloc_blks = 0;
		}
	}

	return 0;
}

static void
vs_teardown_pool(struct vea_stress_pool *vs_pool)
{
	vs_teardown_conts(vs_pool);
	vs_list_teardown(&vs_pool->vsp_punched_list);

	if (vs_pool->vsp_vsi != NULL)
		vea_unload(vs_pool->vsp_vsi);

	if (vs_pool->vsp_umm.umm_pool != NULL)
		umempobj_close(vs_pool->vsp_umm.umm_pool);

	umem_fini_txd(&vs_pool->vsp_txd);
	D_FREE(vs_pool);
}

static struct vea_stress_pool *
vs_setup_pool(void)
{
	struct vea_stress_pool	*vs_pool;
	struct umem_attr	 uma = { 0 };
	void			*root_addr;
	struct vea_unmap_context unmap_ctxt = { 0 };
	struct vea_attr		 attr;
	struct vea_stat		 stat;
	uint64_t		 load_time;
	int			 rc;

	D_ALLOC(vs_pool, vs_arg_size());
	if (vs_pool == NULL) {
		fprintf(stderr, "failed to allocate vs_pool\n");
		return NULL;
	}
	D_INIT_LIST_HEAD(&vs_pool->vsp_objs);
	vs_list_init(&vs_pool->vsp_punched_list);

	rc = umem_init_txd(&vs_pool->vsp_txd);
	if (rc) {
		fprintf(stderr, "failed to init txd\n");
		goto error;
	}

	uma.uma_id = UMEM_CLASS_PMEM;
	if (loading_test) {
		uma.uma_pool = umempobj_open(pool_file, "vea_stress",
					     UMEMPOBJ_ENABLE_STATS, NULL);
		if (uma.uma_pool == NULL) {
			fprintf(stderr, "failed to open pobj pool\n");
			goto error;
		}
	} else {
		unlink(pool_file);
		uma.uma_pool = umempobj_create(pool_file, "vea_stress",
				    UMEMPOBJ_ENABLE_STATS, heap_size, 0666, NULL);
		if (uma.uma_pool == NULL) {
			fprintf(stderr, "failed to create pobj pool\n");
			goto error;
		}
	}

	root_addr = umempobj_get_rootptr(uma.uma_pool, vs_root_size());
	if (root_addr == NULL) {
		fprintf(stderr, "failed to get pobj pool root\n");
		goto error;
	}

	rc = umem_class_init(&uma, &vs_pool->vsp_umm);
	if (rc) {
		fprintf(stderr, "failed to initialize umm\n");
		goto error;
	}
	uma.uma_pool = NULL;

	vs_pool->vsp_vsd = root_addr;
	root_addr += sizeof(struct vea_space_df);

	if (!loading_test) {
		rc = vea_format(&vs_pool->vsp_umm, &vs_pool->vsp_txd, vs_pool->vsp_vsd,
				VS_BLK_SIZE, 1, /* hdr blks */ pool_capacity, NULL, NULL, false);
		if (rc) {
			fprintf(stderr, "failed to format\n");
			goto error;
		}
	}

	load_time = daos_wallclock_secs();
	rc = vea_load(&vs_pool->vsp_umm, &vs_pool->vsp_txd, vs_pool->vsp_vsd, &unmap_ctxt,
		      NULL, &vs_pool->vsp_vsi);
	if (rc) {
		fprintf(stderr, "failed to load\n");
		goto error;
	}
	load_time = daos_wallclock_secs() - load_time;

	rc = vea_query(vs_pool->vsp_vsi, &attr, &stat);
	if (rc) {
		fprintf(stderr, "failed to query\n");
		goto error;
	}

	vs_pool->vsp_tot_blks = attr.va_tot_blks;
	vs_pool->vsp_free_blks = attr.va_free_blks;
	D_ASSERT(vs_pool->vsp_tot_blks >= vs_pool->vsp_free_blks);
	vs_pool->vsp_alloc_blks = vs_pool->vsp_tot_blks - vs_pool->vsp_free_blks;
	fprintf(stdout, "Loaded pool tot_blks:"DF_U64", free_blks:"DF_U64" in "DF_U64" seconds\n",
		vs_pool->vsp_tot_blks, vs_pool->vsp_free_blks, load_time);

	rc = vs_setup_conts(vs_pool, root_addr);
	if (rc) {
		fprintf(stderr, "failed to setup conts\n");
		goto error;
	}

	return vs_pool;
error:
	if (uma.uma_pool != NULL)
		umempobj_close(uma.uma_pool);
	vs_teardown_pool(vs_pool);
	return NULL;
}

static void
vs_fini(void)
{
	daos_debug_fini();
}

static int
vs_init(void)
{
	int	rc;

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc != 0) {
		fprintf(stderr, "failed to init debug\n");
		return rc;
	}

	rc = dbtree_class_register(DBTREE_CLASS_IV,
				   BTR_FEAT_UINT_KEY | BTR_FEAT_DIRECT_KEY,
				   &dbtree_iv_ops);
	if (rc != 0 && rc != -DER_EXIST) {
		fprintf(stderr, "failed to register DBTREE_CLASS_IV\n");
		vs_fini();
		return rc;
	}

	rc = dbtree_class_register(DBTREE_CLASS_IFV, BTR_FEAT_UINT_KEY | BTR_FEAT_DIRECT_KEY,
				   &dbtree_ifv_ops);
	if (rc != 0 && rc != -DER_EXIST) {
		fprintf(stderr, "failed to register DBTREE_CLASS_IFV\n");
		vs_fini();
		return rc;
	}
	return rc;
}

const char vs_stress_options[] =
"Available options are:\n"
"-C <capacity>		pool capacity\n"
"-c <cont_nr>		container nr\n"
"-d <duration>		test duration in seconds\n"
"-f <pool_file>		pmemobj pool filename\n"
"-H <heap_size>		allocator heap size\n"
"-l <load>		test loading existing pool\n"
"-o <obj_nr>		per container object nr\n"
"-s <rand_seed>		rand seed\n"
"-h			help message\n";

static void
print_usage(void)
{
	fprintf(stdout, "vea_stress [options]\n");
	fprintf(stdout, "%s\n", vs_stress_options);
}

static inline uint64_t
val_unit(uint64_t val, char unit)
{
	switch (unit) {
	default:
		return val;
	case 'k':
	case 'K':
		return (val << 10);
	case 'm':
	case 'M':
		return (val << 20);
	case 'g':
	case 'G':
		return (val << 30);
	case 't':
	case 'T':
		return (val << 40);
	}
}

static inline char *
vs_op2str(unsigned int op)
{
	switch (op) {
	case VS_OP_RESERVE:
		return "reserve";
	case VS_OP_PUBLISH:
		return "tx_publish";
	case VS_OP_FREE:
		return "tx_free";
	case VS_OP_MERGE:
		return "tx_merge";
	default:
		break;
	}
	return "Unknown";
}

int main(int argc, char **argv)
{
	static struct option long_ops[] = {
		{ "capacity",	required_argument,	NULL,	'C' },
		{ "cont_nr",	required_argument,	NULL,	'c' },
		{ "duration",	required_argument,	NULL,	'd' },
		{ "file",	required_argument,	NULL,	'f' },
		{ "heap",	required_argument,	NULL,	'H' },
		{ "load",	no_argument,		NULL,	'l' },
		{ "obj_nr",	required_argument,	NULL,	'o' },
		{ "seed",	required_argument,	NULL,	's' },
		{ "help",	no_argument,		NULL,	'h' },
		{ NULL,		0,			NULL,	0   },
	};
	struct vea_stress_pool	*vs_pool;
	char			*endp;
	int			 i, rc;

	rand_seed = (unsigned int)(time(NULL) & 0xFFFFFFFFUL);
	memset(pool_file, 0, sizeof(pool_file));
	while ((rc = getopt_long(argc, argv, "C:c:d:f:H:lo:s:h", long_ops, NULL)) != -1) {
		switch (rc) {
		case 'C':
			pool_capacity = strtoul(optarg, &endp, 0);
			pool_capacity = val_unit(pool_capacity, *endp);
			break;
		case 'c':
			cont_per_pool = atol(optarg);
			break;
		case 'd':
			test_duration = atol(optarg);
			break;
		case 'f':
			strncpy(pool_file, optarg, PATH_MAX - 1);
			break;
		case 'H':
			heap_size = strtoul(optarg, &endp, 0);
			heap_size = val_unit(heap_size, *endp);
			break;
		case 'l':
			loading_test = true;
			break;
		case 'o':
			obj_per_cont = atol(optarg);
			break;
		case 's':
			rand_seed = atol(optarg);
			break;
		case 'h':
			print_usage();
			return 0;
		default:
			fprintf(stderr, "unknown option %c\n", rc);
			print_usage();
			return -1;
		}
	}

	if (strlen(pool_file) == 0)
		strncpy(pool_file, "/mnt/daos/vea_stress_pool", sizeof(pool_file));

	fprintf(stdout, "Start VEA stress test\n");
	fprintf(stdout, "pool_file  : %s\n", pool_file);
	fprintf(stdout, "capacity   : "DF_U64" bytes\n", pool_capacity);
	fprintf(stdout, "heap_size  : "DF_U64" bytes\n", heap_size);
	fprintf(stdout, "cont_nr    : %u\n", cont_per_pool);
	fprintf(stdout, "obj_nr     : %u\n", obj_per_cont);
	fprintf(stdout, "duration   : %u secs\n", test_duration);
	fprintf(stdout, "rand_seed  : %u\n\n", rand_seed);

	rc = vs_init();
	if (rc)
		return rc;

	fprintf(stdout, "Setup pool and containers\n");
	vs_pool = vs_setup_pool();
	if (vs_pool == NULL) {
		rc = -1;
		goto fini;
	}

	if (loading_test)
		goto teardown;

	srand(rand_seed);
	start_ts = daos_wallclock_secs();
	fprintf(stdout, "VEA stress test started (timestamp: "DF_U64")\n", start_ts);
	rc = vs_stress_run(vs_pool);
	if (rc)
		fprintf(stderr, "VEA stress test failed\n");
	else
		fprintf(stdout, "VEA stress test succeeded\n");

	fprintf(stdout, "\n");
	fprintf(stdout, "%-11s %-12s %-12s %-10s %-10s %-10s\n",
		"Operation", "Samples", "Time(us)", "Min(us)", "Max(us)", "Avg(us)");
	for (i = 0; i < VS_OP_MAX; i++) {
		struct vs_perf_cntr *cntr = &vs_pool->vsp_cntr[i];

		fprintf(stdout, "%-11s "DF_12U64" "DF_12U64" %-10u %-10u %-10u\n",
			vs_op2str(i), cntr->vpc_count, cntr->vpc_tot, cntr->vpc_min, cntr->vpc_max,
			cntr->vpc_count ? (unsigned int)(cntr->vpc_tot / cntr->vpc_count) : 0);
	}

teardown:
	vs_teardown_pool(vs_pool);
fini:
	vs_fini();
	return rc;
}
