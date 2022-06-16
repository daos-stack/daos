/**
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC       DD_FAC(server)

#include <execinfo.h>
#include <abt.h>
#include <daos/common.h>
#include <daos_errno.h>
#include <daos_srv/vos.h>
#include <gurt/telemetry_producer.h>
#include "srv_internal.h"

/*
 * CPU weights for each type of ULTs, the ULT consuming more CPU in a schedule
 * cycle has larger weights.
 */
static unsigned int req_weights[SCHED_REQ_MAX] = {
	2,	/* SCHED_REQ_UPDATE */
	1,	/* SCHED_REQ_FETCH */
	4,	/* SCHED_REQ_GC */
	3,	/* SCHED_REQ_SCRUB */
	2,	/* SCHED_REQ_MIGRATE */
};

struct stats_cycle {
	/* Kicked off weights in a schedule cycle */
	uint64_t	sc_kicked_wts[SCHED_REQ_MAX];
};

#define SW_CYCLE_MAX	5000

struct stats_window {
	/* All schedule cycles in the stats window */
	struct stats_cycle	sw_cycles[SW_CYCLE_MAX];
	/* Last schedule cycle */
	struct stats_cycle	sw_last_cycle;
	/* Per type kicked off weights in the stats window */
	uint64_t		sw_kicked_wts[SCHED_REQ_MAX];
	/* Total kicked off weights in the stats window */
	uint64_t		sw_kicked_wts_tot;
	/* To be updated array index of 'sw_cycles' */
	unsigned int		sw_cursor;
	/* Array size of 'sw_cycles' */
	unsigned int		sw_count;
	/* Generation used on making kicking off decision */
	uint8_t			sw_gen;
};

/*
 * Assume the CPU is under utilized (not enough workload generated for certain DAOS
 * pool) when the total kicked weights are less than the SW_MIN_WEIGHTS within a
 * stats window.
 */
#define SW_MIN_WEIGHTS	2500 /* req_weights[SCHED_REQ_FETCH] * SW_CYCLE_MAX / 2 */

static inline void
sw_cycle_update(struct stats_window *sw, unsigned int req_type)
{
	struct stats_cycle	*cur = &sw->sw_last_cycle;

	D_ASSERT(req_type < SCHED_REQ_MAX);
	cur->sc_kicked_wts[req_type] += req_weights[req_type];
}

static inline void
increase_kicked_wts(struct stats_window *sw, struct stats_cycle *sc,
		    struct stats_cycle *last_cycle, unsigned int req_type)
{
	sc->sc_kicked_wts[req_type]	= last_cycle->sc_kicked_wts[req_type];
	sw->sw_kicked_wts[req_type]	+= last_cycle->sc_kicked_wts[req_type];
	sw->sw_kicked_wts_tot		+= last_cycle->sc_kicked_wts[req_type];

	last_cycle->sc_kicked_wts[req_type] = 0;
}

static void
sw_window_update(struct stats_window *sw)
{
	struct stats_cycle	*last_cycle = &sw->sw_last_cycle;
	struct stats_cycle	*sc = &sw->sw_cycles[sw->sw_cursor];
	int			 i;

	if (likely(sw->sw_count == SW_CYCLE_MAX)) {
		for (i = SCHED_REQ_UPDATE; i < SCHED_REQ_MAX; i++) {
			/* Replace the weights in the entry to be updated */
			D_ASSERT(sw->sw_kicked_wts[i] >= sc->sc_kicked_wts[i]);
			D_ASSERT(sw->sw_kicked_wts_tot >= sw->sw_kicked_wts[i]);
			sw->sw_kicked_wts[i]	-= sc->sc_kicked_wts[i];
			sw->sw_kicked_wts_tot	-= sc->sc_kicked_wts[i];

			increase_kicked_wts(sw, sc, last_cycle, i);
		}
		goto done;
	}

	for (i = SCHED_REQ_UPDATE; i < SCHED_REQ_MAX; i++)
		increase_kicked_wts(sw, sc, last_cycle, i);

	sw->sw_count++;
done:
	if (sw->sw_cursor == (SW_CYCLE_MAX - 1))
		sw->sw_cursor = 0;
	else
		sw->sw_cursor++;

	if (sw->sw_gen == UINT8_MAX)
		sw->sw_gen = 0;
	else
		sw->sw_gen++;
}

struct sched_req_info {
	d_list_t		sri_req_list;
	/* Total request count in 'sri_req_list' */
	uint32_t		sri_req_cnt;
	/* How many requests are kicked in current cycle */
	uint32_t		sri_req_kicked;
	/* Limit of kicked requests in current cycle */
	uint32_t		sri_req_limit;
};

struct sched_pool_info {
	/* Link to 'sched_info->si_pool_hash' */
	d_list_t		spi_hash_link;
	uuid_t			spi_pool_id;
	struct sched_req_info	spi_req_array[SCHED_REQ_MAX];
	/* When space pressure info acquired, in msecs */
	uint64_t		spi_space_ts;
	/* When pool is running into space pressure, in msecs */
	uint64_t		spi_pressure_ts;
	int			spi_space_pressure;
	int			spi_gc_ults;
	int			spi_gc_sleeping;
	int			spi_ref;
	uint32_t		spi_req_cnt;
	struct stats_window	spi_stats_window;
};

struct sched_request {
	/*
	 * IO request links to 'sched_info->si_fifo_list', other types of
	 * request link to each 'sched_req_info->sri_req_list' respectively.
	 * When request is not used, it's in 'sched_info->si_idle_list'.
	 */
	d_list_t		 sr_link;
	struct sched_req_attr	 sr_attr;
	void			*sr_func;
	void			*sr_arg;
	ABT_thread		 sr_ult;
	struct sched_pool_info	*sr_pool_info;
	/* Wakeup time for the sleeping request, in milli seconds */
	uint64_t		 sr_wakeup_time;
	/* When the request is enqueued, in msecs */
	uint64_t		 sr_enqueue_ts;
	unsigned int		 sr_abort:1,
				 /* sr_ult is sched_request-owned */
				 sr_owned:1;
};

bool		sched_prio_disabled;
unsigned int	sched_relax_intvl = SCHED_RELAX_INTVL_DEFAULT;
unsigned int	sched_relax_mode;
unsigned int	sched_unit_runtime_max = 32; /* ms */
bool		sched_watchdog_all;

enum {
	/* All requests for various pools are processed in FIFO */
	SCHED_POLICY_FIFO	= 0,
	/*
	 * All requests are processed in RR based on certain ID (Client ID,
	 * Pool ID, Container ID, JobID, UID, etc.)
	 */
	SCHED_POLICY_ID_RR,
	/*
	 * Request priority is based on certain ID (Client ID, Pool ID,
	 * Container ID, JobID, UID, etc.)
	 */
	SCHED_POLICY_ID_PRIO,
	SCHED_POLICY_MAX
};

static int	sched_policy;

/*
 * Time threshold for giving IO up throttling. If space pressure stays in the
 * highest level for enough long time, we assume that no more space can be
 * reclaimed and choose to give up IO throttling, so that ENOSPACE error could
 * be returned to client earlier.
 *
 * To make time for aggregation reclaiming overwriteen space, this threshold
 * should be longer than the DAOS_AGG_THRESHOLD.
 */
#define SCHED_DELAY_THRESH	40000	/* msecs */

/* Maximum milli-seconds a ULT can be delayed */
static unsigned int max_delay_msecs[SCHED_REQ_MAX] = {
	12000,	/* SCHED_REQ_UPDATE */
	2000,	/* SCHED_REQ_FETCH */
	20000,	/* SCHED_REQ_GC */
	20000,	/* SCHED_REQ_SCRUB */
	12000,	/* SCHED_REQ_MIGRATE */
};

/* Maximum QD for different type of ULTs */
static unsigned int max_qds[SCHED_REQ_MAX] = {
	64000,	/* SCHED_REQ_UPDATE */
	32000,	/* SCHED_REQ_FETCH */
	1024,	/* SCHED_REQ_GC */
	1024,	/* SCHED_REQ_SCRUB */
	64000,	/* SCHED_REQ_MIGRATE */
};

struct pressure_ratio {
	unsigned int	pr_free;	/* free space ratio */
	unsigned int	pr_gc_ratio;	/* CPU percentage for GC & Aggregation */
	unsigned int	pr_delay;	/* update being delayed in msec */
	unsigned int	pr_pressure;	/* index in pressure_gauge */
};

static struct pressure_ratio pressure_gauge[] = {
	{	/* free space > 40%, no space pressure */
		.pr_free	= 40,
		.pr_gc_ratio	= 10,
		.pr_delay	= 0,
		.pr_pressure	= SCHED_SPACE_PRESS_NONE,
	},
	{	/* free space > 30% */
		.pr_free	= 30,
		.pr_gc_ratio	= 20,
		.pr_delay	= 4000, /* msecs */
		.pr_pressure	= 1,
	},
	{	/* free space > 20% */
		.pr_free	= 20,
		.pr_gc_ratio	= 30,
		.pr_delay	= 6000, /* msecs */
		.pr_pressure	= 2,
	},
	{	/* free space > 10% */
		.pr_free	= 10,
		.pr_gc_ratio	= 40,
		.pr_delay	= 8000, /* msecs */
		.pr_pressure	= 3,
	},
	{	/* free space > 5% */
		.pr_free	= 5,
		.pr_gc_ratio	= 50,
		.pr_delay	= 10000, /* msecs */
		.pr_pressure	= 4,
	},
	{	/* free space <= 5% */
		.pr_free	= 0,
		.pr_gc_ratio	= 60,
		.pr_delay	= 12000, /* msecs */
		.pr_pressure	= 5,
	},
};

static inline unsigned int
pool2req_cnt(struct sched_pool_info *pool_info, unsigned int type)
{
	D_ASSERT(type < SCHED_REQ_MAX);
	return pool_info->spi_req_array[type].sri_req_cnt;
}

static inline d_list_t *
pool2req_list(struct sched_pool_info *pool_info, unsigned int type)
{
	D_ASSERT(type < SCHED_REQ_MAX);
	return &pool_info->spi_req_array[type].sri_req_list;
}

static inline struct sched_pool_info *
sched_rlink2spi(d_list_t *rlink)
{
	return container_of(rlink, struct sched_pool_info, spi_hash_link);
}

static bool
spi_key_cmp(struct d_hash_table *htable, d_list_t *rlink,
	    const void *key, unsigned int len)
{
	struct sched_pool_info	*spi = sched_rlink2spi(rlink);

	D_ASSERT(len == sizeof(uuid_t));
	return uuid_compare(*(uuid_t *)key, spi->spi_pool_id) == 0;
}

static uint32_t
spi_key_hash(struct d_hash_table *htable, const void *key, unsigned int len)
{
	D_ASSERT(len == sizeof(uuid_t));
	return *((const uint32_t *)key);
}

static void
spi_rec_addref(struct d_hash_table *htable, d_list_t *rlink)
{
	struct sched_pool_info	*spi = sched_rlink2spi(rlink);

	spi->spi_ref++;
}

static bool
spi_rec_decref(struct d_hash_table *htable, d_list_t *rlink)
{
	struct sched_pool_info	*spi = sched_rlink2spi(rlink);

	D_ASSERT(spi->spi_ref > 0);
	spi->spi_ref--;

	return spi->spi_ref == 0;
}

static inline bool
is_spi_inuse(struct sched_pool_info *spi)
{
	return spi->spi_req_cnt != 0 || spi->spi_gc_ults != 0;
}

static void
spi_rec_free(struct d_hash_table *htable, d_list_t *rlink)
{
	struct sched_pool_info	*spi = sched_rlink2spi(rlink);
	unsigned int		 type;

	/*
	 * If server shutdown before disconnecting pools, pool cache
	 * isn't cleared, so spi_gc_ults could be non-zero here.
	 *
	 * See pool_tls_fini(), it should be fixed by local cont/pool
	 * close/disconnect on shutdown. Once that's fixed, following
	 * two assertions could be changed to D_ASSERT(!is_spi_inuse()).
	 */
	D_ASSERTF(spi->spi_req_cnt == 0, "req_cnt:%u\n", spi->spi_req_cnt);
	D_ASSERTF(spi->spi_gc_sleeping == 0, "gc_sleeping:%d\n",
		  spi->spi_gc_sleeping);

	for (type = SCHED_REQ_UPDATE; type < SCHED_REQ_MAX; type++) {
		D_ASSERTF(pool2req_cnt(spi, type) == 0, "type:%u cnt:%u\n",
			  type, pool2req_cnt(spi, type));
		D_ASSERT(d_list_empty(pool2req_list(spi, type)));
	}

	D_FREE(spi);
}

static d_hash_table_ops_t sched_pool_hash_ops = {
	.hop_key_cmp	= spi_key_cmp,
	.hop_key_hash	= spi_key_hash,
	.hop_rec_addref	= spi_rec_addref,
	.hop_rec_decref	= spi_rec_decref,
	.hop_rec_free	= spi_rec_free,
};

/*
 * d_hash_table_traverse() does not support item deletion in traverse
 * callback, so the stale 'spi' (pool was destroyed) will be added into
 * a purge list in traverse callback and being deleted later.
 */
struct purge_item {
	d_list_t	pi_link;
	uuid_t		pi_pool_id;
};

static void
prune_purge_list(struct dss_xstream *dx)
{
	struct sched_info	*info = &dx->dx_sched_info;
	struct sched_pool_info	*spi;
	struct purge_item	*pi, *tmp;
	d_list_t		*rlink;
	bool			 deleted;

	d_list_for_each_entry_safe(pi, tmp, &info->si_purge_list, pi_link) {
		rlink = d_hash_rec_find(info->si_pool_hash, pi->pi_pool_id,
					sizeof(uuid_t));
		if (rlink == NULL)
			goto next;

		spi = sched_rlink2spi(rlink);
		D_ASSERT(spi->spi_ref > 1);
		d_hash_rec_decref(info->si_pool_hash, rlink);
		if (!is_spi_inuse(spi)) {
			deleted = d_hash_rec_delete(info->si_pool_hash,
						    pi->pi_pool_id,
						    sizeof(uuid_t));
			if (!deleted)
				D_ERROR("Purge "DF_UUID" failed.\n",
					DP_UUID(pi->pi_pool_id));
		} else {
			unsigned int type;

			D_ERROR("Pool "DF_UUID", req_cnt:%u, gc_ults:%d\n",
				DP_UUID(pi->pi_pool_id), spi->spi_req_cnt,
				spi->spi_gc_ults);

			for (type = SCHED_REQ_UPDATE; type < SCHED_REQ_MAX;
			     type++) {
				if (pool2req_cnt(spi, type) != 0)
					D_ERROR("type:%u, req_cnt:%u\n", type,
						pool2req_cnt(spi, type));
			}
		}
next:
		d_list_del_init(&pi->pi_link);
		D_FREE(pi);
	}
}

static void
add_purge_list(struct dss_xstream *dx, struct sched_pool_info *spi)
{
	struct sched_info	*info = &dx->dx_sched_info;
	struct purge_item	*pi;

	D_CDEBUG(!is_spi_inuse(spi), DB_TRACE, DLOG_ERR,
		 "vos pool:"DF_UUID" is destroyed. req_cnt:%u, gc_ults:%u\n",
		 DP_UUID(spi->spi_pool_id), spi->spi_req_cnt, spi->spi_gc_ults);

	/* Don't purge the spi when it's still inuse */
	if (is_spi_inuse(spi))
		return;

	d_list_for_each_entry(pi, &info->si_purge_list, pi_link) {
		/* Already in purge list */
		if (uuid_compare(pi->pi_pool_id, spi->spi_pool_id) == 0)
			return;
	}

	D_ALLOC_PTR(pi);
	if (pi == NULL) {
		D_ERROR("Alloc purge item failed.\n");
		return;
	}
	D_INIT_LIST_HEAD(&pi->pi_link);
	uuid_copy(pi->pi_pool_id, spi->spi_pool_id);
	d_list_add_tail(&pi->pi_link, &info->si_purge_list);
}

static void
sched_info_fini(struct dss_xstream *dx)
{
	struct sched_info	*info = &dx->dx_sched_info;
	struct sched_request	*req, *tmp;

	D_ASSERT(info->si_req_cnt == 0);
	D_ASSERT(d_list_empty(&info->si_sleep_list));
	D_ASSERT(d_list_empty(&info->si_fifo_list));

	prune_purge_list(dx);

	if (info->si_pool_hash) {
		d_hash_table_destroy(info->si_pool_hash, true);
		info->si_pool_hash = NULL;
	}

	d_list_for_each_entry_safe(req, tmp, &info->si_idle_list,
				   sr_link) {
		d_list_del_init(&req->sr_link);
		D_FREE(req);
	}
}

static int
prealloc_requests(struct sched_info *info, int cnt)
{
	struct sched_request	*req;
	int			 i;

	for (i = 0; i < cnt; i++) {
		D_ALLOC_PTR(req);
		if (req == NULL) {
			D_ERROR("Alloc req failed.\n");
			return -DER_NOMEM;
		}
		D_INIT_LIST_HEAD(&req->sr_link);
		req->sr_ult = ABT_THREAD_NULL;
		d_list_add_tail(&req->sr_link, &info->si_idle_list);
	}
	return 0;
}

#define SCHED_PREALLOC_INIT_CNT		8192
#define SCHED_PREALLOC_BATCH_CNT	1024

static void
sched_metrics_init(struct dss_xstream *dx)
{
	struct sched_info	*info = &dx->dx_sched_info;
	struct sched_stats	*stats = &info->si_stats;
	int			 rc;

	stats->ss_busy_ts = info->si_cur_ts;
	stats->ss_watchdog_ts = 0;
	stats->ss_last_unit = NULL;

	rc = d_tm_add_metric(&stats->ss_total_time, D_TM_COUNTER, "Total running time", "ms",
			     "sched/total_time/xs_%u", dx->dx_xs_id);
	if (rc)
		D_WARN("Failed to create total_time telemetry: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&stats->ss_relax_time, D_TM_COUNTER, "Total relaxing time", "ms",
			     "sched/relax_time/xs_%u", dx->dx_xs_id);
	if (rc)
		D_WARN("Failed to create relax_time telemetry: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&stats->ss_wq_len, D_TM_GAUGE, "Wait queue length", "req",
			     "sched/wait_queue/xs_%u", dx->dx_xs_id);
	if (rc)
		D_WARN("Failed to create wait_queue telemetry: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&stats->ss_sq_len, D_TM_GAUGE, "Sleep queue length", "req",
			     "sched/sleep_queue/xs_%u", dx->dx_xs_id);
	if (rc)
		D_WARN("Failed to create sleep_queue telemetry: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&stats->ss_cycle_duration, D_TM_STATS_GAUGE, "Schedule cycle duration",
			     "ms", "sched/cycle_duration/xs_%u", dx->dx_xs_id);
	if (rc)
		D_WARN("Failed to create cycle_duration telemetry: "DF_RC"\n", DP_RC(rc));

	rc = d_tm_add_metric(&stats->ss_cycle_size, D_TM_STATS_GAUGE, "Schedule cycle size",
			     "ULT", "sched/cycle_size/xs_%u", dx->dx_xs_id);
	if (rc)
		D_WARN("Failed to create cycle_size telemetry: "DF_RC"\n", DP_RC(rc));
}

static int
sched_info_init(struct dss_xstream *dx)
{
	struct sched_info	*info = &dx->dx_sched_info;
	int			 rc;

	info->si_cur_ts = daos_getmtime_coarse();
	info->si_cur_seq = 0;
	D_INIT_LIST_HEAD(&info->si_idle_list);
	D_INIT_LIST_HEAD(&info->si_sleep_list);
	D_INIT_LIST_HEAD(&info->si_fifo_list);
	D_INIT_LIST_HEAD(&info->si_purge_list);
	info->si_req_cnt = 0;
	info->si_sleep_cnt = 0;
	info->si_wait_cnt = 0;
	info->si_stop = 0;
	sched_metrics_init(dx);

	rc = d_hash_table_create(D_HASH_FT_NOLOCK, 4,
				 NULL, &sched_pool_hash_ops,
				 &info->si_pool_hash);
	if (rc) {
		D_ERROR("Create sched pool hash failed. "DF_RC".\n", DP_RC(rc));
		return rc;
	}

	rc = prealloc_requests(info, SCHED_PREALLOC_INIT_CNT);
	if (rc)
		sched_info_fini(dx);

	return rc;
}

static struct sched_pool_info *
cur_pool_info(struct sched_info *info, uuid_t pool_uuid)
{
	struct sched_pool_info	*spi;
	d_list_t		*rlink, *list;
	unsigned int		 type;
	int			 rc;

	D_ASSERT(info->si_pool_hash != NULL);
	rlink = d_hash_rec_find(info->si_pool_hash, pool_uuid, sizeof(uuid_t));
	if (rlink != NULL) {
		spi = sched_rlink2spi(rlink);
		D_ASSERT(spi->spi_ref > 1);
		d_hash_rec_decref(info->si_pool_hash, rlink);

		return spi;
	}

	D_ALLOC_PTR(spi);
	if (spi == NULL) {
		D_ERROR("Failed to allocate spi\n");
		return NULL;
	}
	D_INIT_LIST_HEAD(&spi->spi_hash_link);
	uuid_copy(spi->spi_pool_id, pool_uuid);

	for (type = SCHED_REQ_UPDATE; type < SCHED_REQ_MAX; type++) {
		list = pool2req_list(spi, type);
		D_INIT_LIST_HEAD(list);
	}

	rc = d_hash_rec_insert(info->si_pool_hash, pool_uuid, sizeof(uuid_t),
			       &spi->spi_hash_link, false);
	if (rc)
		D_ERROR("Failed to insert pool hash. "DF_RC"\n", DP_RC(rc));

	D_ASSERT(spi->spi_ref == 1);
	return spi;
}


static struct sched_request *
req_get(struct dss_xstream *dx, struct sched_req_attr *attr,
	void (*func)(void *), void *arg, ABT_thread ult, bool owned)
{
	struct sched_info	*info = &dx->dx_sched_info;
	struct sched_pool_info	*spi;
	struct sched_request	*req;
	int			 rc;

	if (attr->sra_type == SCHED_REQ_ANONYM) {
		spi = NULL;
	} else {
		spi = cur_pool_info(info, attr->sra_pool_id);
		if (spi == NULL) {
			D_ERROR("Get pool info "DF_UUID" failed.\n",
				DP_UUID(attr->sra_pool_id));
			return NULL;
		}
	}

	if (d_list_empty(&info->si_idle_list)) {
		rc = prealloc_requests(info, SCHED_PREALLOC_BATCH_CNT);
		if (rc)
			return NULL;
	}

	req = d_list_entry(info->si_idle_list.next, struct sched_request,
			   sr_link);
	d_list_del_init(&req->sr_link);

	req->sr_attr	= *attr;
	req->sr_func	= func;
	req->sr_arg	= arg;
	req->sr_ult	= ult;
	req->sr_abort	= 0;
	req->sr_owned	= (owned ? 1 : 0);
	req->sr_pool_info = spi;

	return req;
}

static void
req_put(struct dss_xstream *dx, struct sched_request *req)
{
	struct sched_info	*info = &dx->dx_sched_info;

	D_ASSERT(d_list_empty(&req->sr_link));
	/* Don't put into idle list when the request is tracked by caller */
	if (req->sr_ult == ABT_THREAD_NULL)
		d_list_add_tail(&req->sr_link, &info->si_idle_list);
}

static inline int
req_kickoff_internal(struct dss_xstream *dx, struct sched_req_attr *attr,
		     void (*func)(void *), void *arg)
{
	D_ASSERT(attr && func && arg);
	D_ASSERT(attr->sra_type < SCHED_REQ_TYPE_MAX);

	return sched_create_thread(dx, func, arg, ABT_THREAD_ATTR_NULL, NULL,
				   attr->sra_flags & SCHED_REQ_FL_PERIODIC ?
					DSS_ULT_FL_PERIODIC : 0);
}

static int
req_kickoff(struct dss_xstream *dx, struct sched_request *req)
{
	struct sched_info	*info = &dx->dx_sched_info;
	struct sched_pool_info	*spi = req->sr_pool_info;
	struct sched_req_info	*sri;
	int			 rc;

	if (req->sr_ult != ABT_THREAD_NULL) {
		rc = ABT_thread_resume(req->sr_ult);
		rc = dss_abterr2der(rc);
	} else {
		rc = req_kickoff_internal(dx, &req->sr_attr, req->sr_func,
					  req->sr_arg);
	}

	D_ASSERT(spi != NULL);
	D_ASSERT(req->sr_attr.sra_type < SCHED_REQ_MAX);
	sri = &spi->spi_req_array[req->sr_attr.sra_type];

	D_ASSERT(sri->sri_req_cnt > 0);
	sri->sri_req_cnt--;
	D_ASSERT(spi->spi_req_cnt > 0);
	spi->spi_req_cnt--;
	D_ASSERT(info->si_req_cnt > 0);
	info->si_req_cnt--;
	sw_cycle_update(&spi->spi_stats_window, req->sr_attr.sra_type);

	d_list_del_init(&req->sr_link);
	req_put(dx, req);

	return rc;
}

#define SCHED_SPACE_AGE_MAX	2000	/* 2000 msecs */

static int
check_space_pressure(struct dss_xstream *dx, struct sched_pool_info *spi)
{
	struct sched_info	*info = &dx->dx_sched_info;
	struct vos_pool_space	 vps = { 0 };
	uint64_t		 scm_left, nvme_left;
	struct pressure_ratio	*pr;
	int			 orig_pressure, rc;

	D_ASSERT(spi->spi_space_ts <= info->si_cur_ts);
	/* TLS is destroyed on dss_srv_handler ULT exiting */
	if (info->si_stop)
		goto out;

	/* Use cached space presure info */
	if ((spi->spi_space_ts + SCHED_SPACE_AGE_MAX) > info->si_cur_ts)
		goto out;

	rc = vos_pool_query_space(spi->spi_pool_id, &vps);
	if (rc == -DER_NONEXIST) {	/* vos pool is destroyed */
		add_purge_list(dx, spi);
		goto out;
	} else if (rc) {
		D_ERROR("Query pool:"DF_UUID" space failed. "DF_RC"\n",
			DP_UUID(spi->spi_pool_id), DP_RC(rc));
		goto out;
	}
	spi->spi_space_ts = info->si_cur_ts;

	D_ASSERT(SCM_SYS(&vps) < SCM_TOTAL(&vps));
	/* NVME_TOTAL and NVME_SYS could be both zero */
	D_ASSERT(NVME_SYS(&vps) <= NVME_TOTAL(&vps));

	if (SCM_FREE(&vps) > SCM_SYS(&vps))
		scm_left = SCM_FREE(&vps) - SCM_SYS(&vps);
	else
		scm_left = 0;

	if (NVME_TOTAL(&vps) == 0)      /* NVMe not enabled */
		nvme_left = UINT64_MAX;
	else if (NVME_FREE(&vps) > NVME_SYS(&vps))
		nvme_left = NVME_FREE(&vps) - NVME_SYS(&vps);
	else
		nvme_left = 0;

	orig_pressure = spi->spi_space_pressure;
	for (pr = &pressure_gauge[0]; pr->pr_free != 0; pr++) {
		if (scm_left > (SCM_TOTAL(&vps) * pr->pr_free / 100) &&
		    nvme_left > (NVME_TOTAL(&vps) * pr->pr_free / 100))
			break;
	}
	spi->spi_space_pressure = pr->pr_pressure;

	if (spi->spi_space_pressure != SCHED_SPACE_PRESS_NONE &&
	    spi->spi_space_pressure != orig_pressure) {
		D_INFO("Pool:"DF_UUID" is under %d pressure, "
		       "SCM: tot["DF_U64"], sys["DF_U64"], free["DF_U64"] "
		       "NVMe: tot["DF_U64"], sys["DF_U64"], free["DF_U64"]\n",
		       DP_UUID(spi->spi_pool_id), spi->spi_space_pressure,
		       SCM_TOTAL(&vps), SCM_SYS(&vps), SCM_FREE(&vps),
		       NVME_TOTAL(&vps), NVME_SYS(&vps), NVME_FREE(&vps));

		spi->spi_pressure_ts = info->si_cur_ts;
	}
out:
	return spi->spi_space_pressure;
}

static int
process_req(struct dss_xstream *dx, struct sched_request *req)
{
	struct sched_info	*info = &dx->dx_sched_info;
	struct sched_pool_info	*spi = req->sr_pool_info;
	struct sched_req_info	*sri;
	unsigned int		 req_type = req->sr_attr.sra_type;
	unsigned int		 delay_msecs;

	D_ASSERT(spi != NULL);
	D_ASSERT(req_type < SCHED_REQ_MAX);

	sri = &spi->spi_req_array[req_type];

	/* Kickoff all requests on shutdown */
	if (info->si_stop)
		goto kickoff;

	if (sri->sri_req_kicked < sri->sri_req_limit)
		goto kickoff;

	if (req->sr_attr.sra_flags & SCHED_REQ_FL_NO_DELAY)
		goto kickoff;

	if (req_type == SCHED_REQ_UPDATE) {
		struct pressure_ratio *pr;

		pr = &pressure_gauge[spi->spi_space_pressure];
		delay_msecs = pr->pr_delay;
	} else {
		delay_msecs = max_delay_msecs[req_type];
	}

	/* Request expired */
	D_ASSERT(info->si_cur_ts >= req->sr_enqueue_ts);
	if ((info->si_cur_ts - req->sr_enqueue_ts) > delay_msecs)
		goto kickoff;

	/* Remaining requests are not expired */
	return 1;
kickoff:
	sri->sri_req_kicked++;
	req_kickoff(dx, req);
	return 0;
}

static inline void
process_req_list(struct dss_xstream *dx, d_list_t *list, bool stop_early)
{
	struct sched_request	*req, *tmp;
	int			 rc;

	d_list_for_each_entry_safe(req, tmp, list, sr_link) {
		rc = process_req(dx, req);
		if (rc && stop_early)
			break;
	}
}

static inline void
set_req_limit(struct dss_xstream *dx, struct sched_pool_info *spi,
	      unsigned int req_type, unsigned int limit)
{
	unsigned int	tot = pool2req_cnt(spi, req_type);

	D_ASSERT(limit <= tot);
	if (tot - limit > max_qds[req_type]) {
		D_CRIT("Too large QD: %u/%u/%u for req:%d\n",
		       tot, max_qds[req_type], limit, req_type);
		limit = tot - max_qds[req_type];
	}
	spi->spi_req_array[req_type].sri_req_limit = limit;
	spi->spi_req_array[req_type].sri_req_kicked = 0;
}

/* Are space reclaiming ULTs busy/pending on reclaiming space? */
static inline bool
is_gc_pending(struct sched_pool_info *spi)
{
	D_ASSERT(spi->spi_gc_ults >= spi->spi_gc_sleeping);
	return spi->spi_gc_ults && (spi->spi_gc_ults > spi->spi_gc_sleeping);
}

/* Just run into this space pressure situation recently? */
static inline bool
is_pressure_recent(struct sched_info *info, struct sched_pool_info *spi)
{
	D_ASSERT(info->si_cur_ts >= spi->spi_pressure_ts);
	return (info->si_cur_ts - spi->spi_pressure_ts) < SCHED_DELAY_THRESH;
}

static inline uint64_t
apportion_wts(uint64_t avail_wts, uint32_t *kick, unsigned int req_type)
{
	uint64_t	pending_wts, kick_cnt;

	if (kick[req_type] == 0)
		return avail_wts;

	if (avail_wts == 0) {
		kick[req_type] = 0;
		return 0;
	}

	pending_wts = (uint64_t)kick[req_type] * req_weights[req_type];
	if (avail_wts <= pending_wts) {
		kick_cnt = avail_wts / req_weights[req_type];
		if (kick_cnt < kick[req_type])
			kick[req_type] = kick_cnt;
		return 0;
	}

	return avail_wts - pending_wts;
}

/*
 * The goal is to subtract 'delta' weights from 'cur_wts' to make it satisfy:
 * (cur_wts - delta) = (tot_wts - delta) * ratio
 *
 * That concludes: delta = (cur_wts - tot_wts * ratio) / (1 - ratio)
 */
static inline uint64_t
calc_avail_wts(uint64_t cur_wts, uint64_t goal_wts, uint64_t kicked_wts, unsigned int ratio)
{
	uint64_t	avail_wts;

	D_ASSERT(cur_wts > goal_wts);
	avail_wts = (cur_wts - goal_wts) * 100 / (100 - ratio);

	if (cur_wts <= avail_wts)
		return 0;

	avail_wts = cur_wts - avail_wts;
	return (avail_wts <= kicked_wts) ? 0 : avail_wts - kicked_wts;
}

/*
 * When the pool is under space pressure, GC ULTs could be throttled if it
 * exceeded the ratio defined for current pressure level, otherwise, other
 * ULTs could be throttled.
 */
static void
throttle_io(struct sched_info *info, struct sched_pool_info *spi, uint32_t *kick,
	    struct pressure_ratio *pr)
{
	struct stats_window	*sw = &spi->spi_stats_window;
	uint64_t		*kicked_wts, tot_wts, gc_wts, gc_wts_max, avail_wts;
	unsigned int		 req_type;

	kicked_wts = &sw->sw_kicked_wts[0];

	/* No onging I/O and rebuild, full-speed GC without any throttling */
	if (kicked_wts[SCHED_REQ_UPDATE] == 0 && kicked_wts[SCHED_REQ_FETCH] == 0 &&
	    kicked_wts[SCHED_REQ_MIGRATE] == 0 && kick[SCHED_REQ_UPDATE] == 0 &&
	    kick[SCHED_REQ_FETCH] == 0 && kick[SCHED_REQ_MIGRATE] == 0)
		return;

	gc_wts = kicked_wts[SCHED_REQ_GC];
	gc_wts += (uint64_t)kick[SCHED_REQ_GC] * req_weights[SCHED_REQ_GC];

	tot_wts = sw->sw_kicked_wts_tot;
	for (req_type = SCHED_REQ_UPDATE; req_type < SCHED_REQ_MAX; req_type++)
		tot_wts += (uint64_t)kick[req_type] * req_weights[req_type];

	/* CPU is under utilized, no throttling on any ULTs */
	if (tot_wts < SW_MIN_WEIGHTS)
		return;

	gc_wts_max = tot_wts * pr->pr_gc_ratio / 100;
	avail_wts = (uint64_t)kick[SCHED_REQ_SCRUB] * req_weights[SCHED_REQ_SCRUB];
	if (gc_wts > gc_wts_max) {
		if (kick[SCHED_REQ_GC] == 0)
			goto done;

		gc_wts = calc_avail_wts(gc_wts, gc_wts_max, kicked_wts[SCHED_REQ_GC],
					pr->pr_gc_ratio);
		apportion_wts(gc_wts, kick, SCHED_REQ_GC);
	} else if (gc_wts < gc_wts_max) {
		avail_wts = 0;
		/*
		 * If space pressure isn't at highest level (pr->pr_free != 0), throttling
		 * will be skipped when all GC/Aggregation ULTs are in sleep.
		 */
		if (pr->pr_free != 0 && !is_gc_pending(spi))
			goto done;
		/*
		 * If space pressure stays in same level for a while, we just assume that
		 * no available space can be reclaimed, so throttling will be skipped and
		 * ENOSPACE could be returned to client sooner.
		 */
		if (!is_pressure_recent(info, spi))
			goto done;

		D_ASSERT(sw->sw_kicked_wts_tot >= kicked_wts[SCHED_REQ_GC]);
		avail_wts = calc_avail_wts(tot_wts - gc_wts, tot_wts - gc_wts_max,
					   sw->sw_kicked_wts_tot - kicked_wts[SCHED_REQ_GC],
					   100 - pr->pr_gc_ratio);

		/* Satisfy rebuild/reintegration ULTs first when 'sw_gen' is odd */
		if (sw->sw_gen & 0x1) {
			avail_wts = apportion_wts(avail_wts, kick, SCHED_REQ_MIGRATE);
			avail_wts = apportion_wts(avail_wts, kick, SCHED_REQ_UPDATE);
		} else {
			avail_wts = apportion_wts(avail_wts, kick, SCHED_REQ_UPDATE);
			avail_wts = apportion_wts(avail_wts, kick, SCHED_REQ_MIGRATE);
		}
	}
done:
	/* Schedule SCRUB ULT when there are available weights or on every 256 cycles */
	if (sw->sw_gen != 0)
		apportion_wts(avail_wts, kick, SCHED_REQ_SCRUB);
}

/* Rebuild/Reintegration takes 30% CPU when there is no space pressure */
#define REBUILD_RATIO	30

/*
 * When there is no space pressure, all IO requests will be kicked off immediately,
 * internal sys ULTs will be throttled.
 */
static void
throttle_sys(struct stats_window *sw, uint32_t *kick, struct pressure_ratio *pr)
{
	uint64_t	*kicked_wts, io_wts, tot_wts, avail_wts;
	unsigned int	 io_ratio;

	kicked_wts = &sw->sw_kicked_wts[0];

	io_wts = kicked_wts[SCHED_REQ_UPDATE] + kicked_wts[SCHED_REQ_FETCH];
	io_wts += (uint64_t)kick[SCHED_REQ_UPDATE] * req_weights[SCHED_REQ_UPDATE];
	io_wts += (uint64_t)kick[SCHED_REQ_FETCH] * req_weights[SCHED_REQ_FETCH];

	/* No recent IO and pending IO, no throttling on sys ULTs */
	if (io_wts == 0)
		return;

	if (kicked_wts[SCHED_REQ_MIGRATE] != 0 || kick[SCHED_REQ_MIGRATE] != 0)
		io_ratio = 100 - REBUILD_RATIO;
	else
		io_ratio = 100 - pr->pr_gc_ratio;

	/* Calculate the target total weights based on IO weights and IO ratio */
	tot_wts = io_wts * 100 / io_ratio;
	if (tot_wts < SW_MIN_WEIGHTS)
		tot_wts = SW_MIN_WEIGHTS;

	if (tot_wts <= sw->sw_kicked_wts_tot) {
		kick[SCHED_REQ_GC] = 0;
		kick[SCHED_REQ_MIGRATE] = 0;
		kick[SCHED_REQ_SCRUB] = 0;
		return;
	}

	avail_wts = tot_wts - sw->sw_kicked_wts_tot;
	/* Satisfy rebuild/reintegration ULTs first when 'sw_gen' is odd */
	if (sw->sw_gen & 0x1) {
		avail_wts = apportion_wts(avail_wts, kick, SCHED_REQ_MIGRATE);
		avail_wts = apportion_wts(avail_wts, kick, SCHED_REQ_GC);
	} else {
		avail_wts = apportion_wts(avail_wts, kick, SCHED_REQ_GC);
		avail_wts = apportion_wts(avail_wts, kick, SCHED_REQ_MIGRATE);
	}

	/* Schedule SCRUB ULT when there are available weights or on every 256 cycles */
	if (sw->sw_gen != 0)
		apportion_wts(avail_wts, kick, SCHED_REQ_SCRUB);
}

static int
process_pool_cb(d_list_t *rlink, void *arg)
{
	struct dss_xstream	*dx = (struct dss_xstream *)arg;
	struct sched_info	*info = &dx->dx_sched_info;
	struct sched_pool_info	*spi;
	uint32_t		 kick[SCHED_REQ_MAX];
	struct pressure_ratio	*pr;
	int			 press, i;

	spi = sched_rlink2spi(rlink);

	/* Update stats window no matter if any pending ULT or not */
	sw_window_update(&spi->spi_stats_window);
	if (spi->spi_req_cnt == 0)
		return 0;

	for (i = SCHED_REQ_UPDATE; i < SCHED_REQ_MAX; i++)
		kick[i] = pool2req_cnt(spi, i);

	press = check_space_pressure(dx, spi);
	pr = &pressure_gauge[press];

	if (press == SCHED_SPACE_PRESS_NONE)
		throttle_sys(&spi->spi_stats_window, &kick[SCHED_REQ_UPDATE], pr);
	else
		throttle_io(info, spi, &kick[SCHED_REQ_UPDATE], pr);

	for (i = SCHED_REQ_UPDATE; i < SCHED_REQ_MAX; i++)
		set_req_limit(dx, spi, i, kick[i]);

	process_req_list(dx, pool2req_list(spi, SCHED_REQ_GC), true);
	process_req_list(dx, pool2req_list(spi, SCHED_REQ_SCRUB), true);
	process_req_list(dx, pool2req_list(spi, SCHED_REQ_MIGRATE), true);

	return 0;
}

static void
policy_fifo_enqueue(struct dss_xstream *dx, struct sched_request *req,
		    void *prio_data)
{
	struct sched_info	*info = &dx->dx_sched_info;

	d_list_add_tail(&req->sr_link, &info->si_fifo_list);
}

static void
policy_fifo_process(struct dss_xstream *dx)
{
	struct sched_info	*info = &dx->dx_sched_info;

	process_req_list(dx, &info->si_fifo_list, false);
}

struct sched_policy_ops {
	void (*enqueue_io)(struct dss_xstream *dx, struct sched_request *req,
			   void *prio_data);
	void (*process_io)(struct dss_xstream *dx);
};

static struct sched_policy_ops	policy_ops[SCHED_POLICY_MAX] = {
	{	/* SCHED_POLICY_FIFO */
		.enqueue_io = policy_fifo_enqueue,
		.process_io = policy_fifo_process,
	},
	{	/* SCHED_POLICY_ID_RR */
		.enqueue_io = NULL,
		.process_io = NULL,
	},
	{	/* SCHED_POLICY_ID_PRIO */
		.enqueue_io = NULL,
		.process_io = NULL,
	}
};

static void
process_all(struct dss_xstream *dx)
{
	struct sched_info	*info = &dx->dx_sched_info;
	int			 rc;

	prune_purge_list(dx);
	rc = d_hash_table_traverse(info->si_pool_hash, process_pool_cb, dx);
	if (rc)
		D_ERROR("Traverse pool hash error. "DF_RC"\n", DP_RC(rc));

	D_ASSERT(policy_ops[sched_policy].process_io != NULL);
	policy_ops[sched_policy].process_io(dx);
}

static inline bool
should_enqueue_req(struct dss_xstream *dx, struct sched_req_attr *attr)
{
	struct sched_info	*info = &dx->dx_sched_info;

	if (sched_prio_disabled || info->si_stop)
		return false;

	D_ASSERT(attr->sra_type < SCHED_REQ_TYPE_MAX);
	if (attr->sra_type == SCHED_REQ_ANONYM)
		return false;

	/* For VOS xstream only */
	return dx->dx_main_xs;
}

static void
req_enqueue(struct dss_xstream *dx, struct sched_request *req)
{
	struct sched_info	*info = &dx->dx_sched_info;
	struct sched_pool_info	*spi = req->sr_pool_info;
	struct sched_req_attr	*attr = &req->sr_attr;
	struct sched_req_info	*sri;

	D_ASSERT(spi != NULL);
	sri = &spi->spi_req_array[attr->sra_type];

	D_ASSERT(d_list_empty(&req->sr_link));
	if (attr->sra_type == SCHED_REQ_UPDATE ||
	    attr->sra_type == SCHED_REQ_FETCH) {
		D_ASSERT(policy_ops[sched_policy].enqueue_io != NULL);
		policy_ops[sched_policy].enqueue_io(dx, req, NULL);
	} else {
		d_list_add_tail(&req->sr_link, &sri->sri_req_list);
	}
	req->sr_enqueue_ts = info->si_cur_ts;

	sri->sri_req_cnt++;
	spi->spi_req_cnt++;
	info->si_req_cnt++;
}

int
sched_req_enqueue(struct dss_xstream *dx, struct sched_req_attr *attr,
		  void (*func)(void *), void *arg)
{
	struct sched_request	*req;

	if (!should_enqueue_req(dx, attr))
		return req_kickoff_internal(dx, attr, func, arg);

	/*
	 * TODO: A RPC flow control mechanism could be introduced to avoid RPC timeout when the
	 * server is congested:
	 *
	 * Estimate how long it would take to process the incoming request based on the server
	 * resource availability (request queue length, space pressure, DMA buffer usage, etc)
	 * and the average per request processing time, then compare the estimated time with the
	 * RPC timeout to see if the request should be early replied with hint data for retry.
	 *
	 * That requires wire format and client changes.
	 */
	D_ASSERT(attr->sra_type < SCHED_REQ_MAX);
	req = req_get(dx, attr, func, arg, ABT_THREAD_NULL, false);
	if (req == NULL) {
		D_ERROR("Get req failed.\n");
		return -DER_NOMEM;
	}
	req_enqueue(dx, req);

	return 0;
}

void
sched_req_yield(struct sched_request *req)
{
	struct dss_xstream	*dx = dss_current_xstream();

	D_ASSERT(req != NULL);
	if (!should_enqueue_req(dx, &req->sr_attr)) {
		ABT_thread_yield();
		return;
	}

	D_ASSERT(req->sr_ult != ABT_THREAD_NULL);
	req_enqueue(dx, req);

	ABT_self_suspend();
}

static inline void
sleep_counting(struct dss_xstream *dx, struct sched_request *req, int sleep)
{
	struct sched_info	*info = &dx->dx_sched_info;
	struct sched_pool_info	*spi = req->sr_pool_info;

	info->si_sleep_cnt += sleep;
	D_ASSERT(info->si_sleep_cnt >= 0);

	if (req->sr_attr.sra_type == SCHED_REQ_ANONYM)
		return;

	D_ASSERT(spi != NULL);
	if (req->sr_attr.sra_type != SCHED_REQ_GC)
		return;

	spi->spi_gc_sleeping += sleep;

	D_ASSERTF(spi->spi_gc_sleeping >= 0 &&
		  spi->spi_gc_sleeping <= spi->spi_gc_ults,
		  "Pool:"DF_UUID", gc_ults:%d, sleeping:%d\n",
		  DP_UUID(spi->spi_pool_id), spi->spi_gc_ults,
		  spi->spi_gc_sleeping);
}

void
sched_req_sleep(struct sched_request *req, uint32_t msecs)
{
	struct dss_xstream	*dx = dss_current_xstream();
	struct sched_info	*info = &dx->dx_sched_info;
	struct sched_request	*tmp;

	D_ASSERT(req != NULL);
	if (msecs == 0 || info->si_stop || req->sr_abort) {
		sched_req_yield(req);
		return;
	}

	D_ASSERT(req->sr_ult != ABT_THREAD_NULL);
	req->sr_wakeup_time = info->si_cur_ts + msecs;

	D_ASSERT(d_list_empty(&req->sr_link));
	/* Sleep list is sorted in wakeup time ascending order */
	d_list_for_each_entry_reverse(tmp, &info->si_sleep_list, sr_link) {
		if (req->sr_wakeup_time >= tmp->sr_wakeup_time) {
			d_list_add(&req->sr_link, &tmp->sr_link);
			break;
		}
	}
	if (d_list_empty(&req->sr_link))
		d_list_add(&req->sr_link, &info->si_sleep_list);

	sleep_counting(dx, req, 1);

	ABT_self_suspend();
}

static void
req_wakeup_internal(struct dss_xstream *dx, struct sched_request *req)
{
	D_ASSERT(req != NULL);
	/* The request is not in sleep */
	if (req->sr_wakeup_time == 0)
		return;

	D_ASSERT(!d_list_empty(&req->sr_link));
	d_list_del_init(&req->sr_link);
	req->sr_wakeup_time = 0;

	sleep_counting(dx, req, -1);

	D_ASSERT(req->sr_ult != ABT_THREAD_NULL);
	ABT_thread_resume(req->sr_ult);
}

void
sched_req_wakeup(struct sched_request *req)
{
	struct dss_xstream *dx = dss_current_xstream();

	return req_wakeup_internal(dx, req);
}

void
sched_req_wait(struct sched_request *req, bool abort)
{
	int	rc;

	D_ASSERT(req != NULL);
	if (abort) {
		req->sr_abort = 1;
		sched_req_wakeup(req);
	}
	D_ASSERT(req->sr_ult != ABT_THREAD_NULL);
	rc = ABT_thread_join(req->sr_ult);
	D_ASSERTF(rc == ABT_SUCCESS, "ABT_thread_join: %d\n", rc);
}

inline bool
sched_req_is_aborted(struct sched_request *req)
{
	D_ASSERT(req != NULL);
	return req->sr_abort != 0;
}

int
sched_req_space_check(struct sched_request *req)
{
	struct dss_xstream	*dx = dss_current_xstream();

	D_ASSERT(req != NULL && req->sr_pool_info != NULL);
	return check_space_pressure(dx, req->sr_pool_info);
}

static void
wakeup_all(struct dss_xstream *dx)
{
	struct sched_info	*info = &dx->dx_sched_info;
	struct sched_request	*req, *tmp;

	d_list_for_each_entry_safe(req, tmp, &info->si_sleep_list, sr_link) {
		D_ASSERT(req->sr_wakeup_time > 0);
		if (!info->si_stop && req->sr_wakeup_time > info->si_cur_ts)
			break;

		if (!should_enqueue_req(dx, &req->sr_attr)) {
			req_wakeup_internal(dx, req);
		} else {
			d_list_del_init(&req->sr_link);
			req->sr_wakeup_time = 0;
			sleep_counting(dx, req, -1);
			D_ASSERT(req->sr_ult != ABT_THREAD_NULL);
			req_enqueue(dx, req);
		}
	}
}

struct sched_request *
sched_req_get(struct sched_req_attr *attr, ABT_thread ult)
{
	struct dss_xstream	*dx = dss_current_xstream();
	bool			 owned;
	struct sched_request	*req;
	int			 rc;

	D_ASSERT(attr->sra_type < SCHED_REQ_TYPE_MAX);

	if (ult == ABT_THREAD_NULL) {
		ABT_thread	self;

		rc = ABT_thread_self(&self);
		if (rc != ABT_SUCCESS) {
			D_ERROR("Failed to get self thread: %d\n", rc);
			return NULL;
		}
		ult = self;
		owned = false;
	} else {
		ABT_bool unnamed;

		/*
		 * Since Argobots prohibits freeing unnamed ULTs, don't own
		 * them.
		 */
		rc = ABT_thread_is_unnamed(ult, &unnamed);
		if (rc != ABT_SUCCESS) {
			D_ERROR("Failed to get thread type: %d\n", rc);
			return NULL;
		}
		if (unnamed == ABT_TRUE) {
			D_ERROR("Unnamed threads are not supported\n");
			return NULL;
		}
		owned = true;
	}

	req = req_get(dx, attr, NULL, NULL, ult, owned);
	if (req != NULL && attr->sra_type == SCHED_REQ_GC)
		req->sr_pool_info->spi_gc_ults++;

	return req;
}

void
sched_req_put(struct sched_request *req)
{
	struct dss_xstream	*dx = dss_current_xstream();
	struct sched_info	*info = &dx->dx_sched_info;
	int			 rc;

	D_ASSERT(req != NULL && req->sr_ult != ABT_THREAD_NULL);
	D_ASSERT(d_list_empty(&req->sr_link));
	if (req->sr_owned) {
		/* We are responsible for freeing a req-owned ULT. */
		rc = ABT_thread_free(&req->sr_ult);
		D_ASSERTF(rc == ABT_SUCCESS, "%d\n", rc);
	} else {
		req->sr_ult = ABT_THREAD_NULL;
	}
	d_list_add_tail(&req->sr_link, &info->si_idle_list);

	if (req->sr_attr.sra_type == SCHED_REQ_GC) {
		D_ASSERT(req->sr_pool_info->spi_gc_ults > 0);
		req->sr_pool_info->spi_gc_ults--;
	}
}

void
sched_stop(struct dss_xstream *dx)
{
	struct sched_info	*info = &dx->dx_sched_info;

	info->si_stop = 1;
	wakeup_all(dx);
	process_all(dx);
}

void
sched_cond_wait(ABT_cond cond, ABT_mutex mutex)
{
	struct dss_xstream	*dx = dss_current_xstream();
	struct sched_info	*info = &dx->dx_sched_info;

	info->si_wait_cnt += 1;
	ABT_cond_wait(cond, mutex);
	D_ASSERT(info->si_wait_cnt > 0);
	info->si_wait_cnt -= 1;
}

uint64_t
sched_cur_msec(void)
{
	struct dss_xstream	*dx = dss_current_xstream();
	struct sched_info	*info = &dx->dx_sched_info;

	return info->si_cur_ts;
}

uint64_t
sched_cur_seq(void)
{
	struct dss_xstream	*dx = dss_current_xstream();
	struct sched_info	*info = &dx->dx_sched_info;

	return info->si_cur_seq;
}

struct sched_request *
sched_create_ult(struct sched_req_attr *attr, void (*func)(void *), void *arg, size_t stack_size)
{
	struct dss_xstream	*dx = dss_current_xstream();
	struct sched_request	*req;
	ABT_thread		 ult = ABT_THREAD_NULL;
	int			 rc;

	req = req_get(dx, attr, NULL, NULL, ult, true);
	if (req == NULL)
		return NULL;

	/* The ULT must be created on the caller xstream */
	rc = dss_ult_create(func, arg, DSS_XS_SELF, 0, stack_size, &ult);
	if (rc) {
		D_ERROR("Failed to create ULT: "DF_RC"\n", DP_RC(rc));
		req_put(dx, req);
		return NULL;
	}
	D_ASSERT(ult != ABT_THREAD_NULL);

	req->sr_ult = ult;
	if (attr->sra_type == SCHED_REQ_GC)
		req->sr_pool_info->spi_gc_ults++;

	return req;
}

/*
 * A schedule cycle consists of three stages:
 * 1. Starting with a network poll ULT, number of ULTs to be executed in this
 *    cycle is queried by ABT_pool_get_size() for each non-poll ABT pool;
 * 2. Executing all other ULTs which not for hardware polling;
 * 3. Ending with a NVMe poll ULT;
 *
 * Extra network & NVMe poll ULTs could be scheduled in executing stage
 * according to network/NVMe poll age;
 */
struct sched_cycle {
	uint32_t	sc_ults_cnt[DSS_POOL_CNT];
	uint32_t	sc_ults_tot;
	uint32_t	sc_age_net;
	uint32_t	sc_age_nvme;
	unsigned int	sc_new_cycle:1,
			sc_cycle_started:1;
};

struct sched_data {
	struct sched_cycle	 sd_cycle;
	struct dss_xstream	*sd_dx;
	uint32_t		 sd_event_freq;
};

/* #define SCHED_DEBUG */
static void
sched_dump_data(struct sched_data *data)
{
#ifdef SCHED_DEBUG
	struct dss_xstream	*dx = data->sd_dx;
	struct sched_cycle	*cycle = &data->sd_cycle;

	D_PRINT("comm:%d main:%d. age_net:%u, age_nvme:%u, "
		"new_cycle:%d cycle_started:%d total_ults:%u\n",
		dx->dx_comm, dx->dx_main_xs, cycle->sc_age_net,
		cycle->sc_age_nvme, cycle->sc_new_cycle,
		cycle->sc_cycle_started, cycle->sc_ults_tot);
#endif
}

#define SCHED_AGE_NET_MAX		32
#define SCHED_AGE_NVME_MAX		64

static int
sched_init(ABT_sched sched, ABT_sched_config config)
{
	struct sched_data	*data;
	int			 ret;

	D_ALLOC_PTR(data);
	if (data == NULL)
		return ABT_ERR_MEM;

	ret = ABT_sched_config_read(config, 2, &data->sd_event_freq,
				    &data->sd_dx);
	if (ret != ABT_SUCCESS) {
		D_ERROR("Failed to read ABT sched config: %d\n", ret);
		D_FREE(data);
		return ret;
	}

	ret = ABT_sched_set_data(sched, (void *)data);
	return ret;
}

static bool
need_net_poll(struct sched_cycle *cycle)
{
	/* Need net poll to start new cycle */
	if (!cycle->sc_cycle_started) {
		D_ASSERT(cycle->sc_ults_tot == 0);
		return true;
	}

	/* Need a nvme poll to end current cycle */
	if (cycle->sc_ults_tot == 0)
		return false;

	/*
	 * Need extra net poll when too many ULTs are processed in
	 * current cycle.
	 */
	if (cycle->sc_age_net > SCHED_AGE_NET_MAX)
		return true;

	return false;
}

static ABT_unit
sched_pop_net_poll(struct sched_data *data, ABT_pool pool)
{
	struct sched_cycle	*cycle = &data->sd_cycle;
	ABT_unit		 unit;
	int			 ret;

	if (!need_net_poll(cycle))
		return ABT_UNIT_NULL;

	cycle->sc_age_net = 0;
	cycle->sc_age_nvme++;
	if (cycle->sc_ults_tot == 0) {
		D_ASSERT(!cycle->sc_cycle_started);
		cycle->sc_new_cycle = 1;
	}

	/*
	 * No matter if current xstream has comm(Cart) context attached or
	 * not, there is always a server handler ULT in DSS_POOL_NET_POLL.
	 * (see dss_srv_handler()).
	 */
	ret = ABT_pool_pop(pool, &unit);
	if (ret != ABT_SUCCESS) {
		D_ERROR("Failed to pop network poll ULT: %d\n", ret);
		return ABT_UNIT_NULL;
	}

	return unit;
}

static bool
need_nvme_poll(struct dss_xstream *dx, struct sched_cycle *cycle)
{
	struct sched_info	*info = &dx->dx_sched_info;
	struct dss_module_info	*dmi;

	/* Need net poll to start new cycle */
	if (!cycle->sc_cycle_started) {
		D_ASSERT(cycle->sc_ults_tot == 0);
		return false;
	}

	/* Need nvme poll to end current cycle */
	if (cycle->sc_ults_tot == 0)
		return true;

	/*
	 * Need extra NVMe poll when too many ULTs are processed in
	 * current cycle.
	 */
	if (cycle->sc_age_nvme > SCHED_AGE_NVME_MAX)
		return true;

	/* TLS is destroyed on dss_srv_handler ULT exiting */
	if (info->si_stop)
		return false;

	dmi = dss_get_module_info();
	D_ASSERT(dmi != NULL);
	return bio_need_nvme_poll(dmi->dmi_nvme_ctxt);
}

static ABT_unit
sched_pop_nvme_poll(struct sched_data *data, ABT_pool pool)
{
	struct dss_xstream	*dx = data->sd_dx;
	struct sched_cycle	*cycle = &data->sd_cycle;
	ABT_unit		 unit;
	int			 ret;

	if (!need_nvme_poll(dx, cycle))
		return ABT_UNIT_NULL;

	D_ASSERT(cycle->sc_cycle_started);
	cycle->sc_age_nvme = 0;
	cycle->sc_age_net++;
	if (cycle->sc_ults_tot == 0)
		cycle->sc_cycle_started = 0;

	/* Only main xstream (VOS xstream) has NVMe poll ULT */
	if (!dx->dx_main_xs)
		return ABT_UNIT_NULL;

	ret = ABT_pool_pop(pool, &unit);
	if (ret != ABT_SUCCESS) {
		D_ERROR("Failed to pop NVMe poll ULT: %d\n", ret);
		return ABT_UNIT_NULL;
	}

	return unit;
}

static ABT_unit
sched_pop_one(struct sched_data *data, ABT_pool pool, int pool_idx)
{
	struct sched_cycle	*cycle = &data->sd_cycle;
	ABT_unit		 unit;
	int			 ret;

	D_ASSERT(cycle->sc_ults_tot >= cycle->sc_ults_cnt[pool_idx]);
	if (cycle->sc_ults_cnt[pool_idx] == 0)
		return ABT_UNIT_NULL;

	ret = ABT_pool_pop(pool, &unit);
	if (ret != ABT_SUCCESS) {
		D_ERROR("Failed to pop ULT for ABT pool(%d): %d\n", pool_idx, ret);
		return ABT_UNIT_NULL;
	}

	/*
	 * When ABT_thread_join() is called to wait for a target ULT to
	 * terminate, the target ULT could be removed from ABT pool by the
	 * ABT_thread_join(), so the ABT pool could become empty when our
	 * scheduler back to control.
	 *
	 * This usually happen on pool destroy or server shutdown where
	 * ABT_thread_join() is called.
	 */
	if (unit == ABT_UNIT_NULL)
		D_DEBUG(DB_TRACE, "Popped NULL unit for ABT pool(%d)\n", pool_idx);

	cycle->sc_age_net++;
	cycle->sc_age_nvme++;
	cycle->sc_ults_cnt[pool_idx] -= 1;
	cycle->sc_ults_tot -= 1;

	return unit;
}

#define SCHED_IDLE_THRESH	8000UL	/* msecs */

/*
 * Try to relax CPU for a short period when the xstream is idle. The relaxing
 * period can't be too long, otherwise, potential external events like:
 * incoming network requests, new ULTs created by other xstream (from the
 * collective call or offloading call) could be delayed too much.
 *
 * There are also some periodical internal events from BIO, like hotplug
 * poller, health/io stats collecting, blobstore state transition, etc. It's
 * not easy to accurately predict the next occurrence of those events.
 */
static void
sched_try_relax(struct dss_xstream *dx, ABT_pool *pools, uint32_t running)
{
	struct sched_info	*info = &dx->dx_sched_info;
	unsigned int		 sleep_time = sched_relax_intvl;
	size_t			 blocked;
	int			 ret;

	dx->dx_timeout = 0;

	if (info->si_stop)
		return;

	/*
	 * There are running ULTs in current schedule cycle.
	 *
	 * NB. DRPC listener ULT is currently always running (it waits on
	 * drpc_progress(), so the DRPC listener xstream will never sleep
	 * in this function.
	 */
	if (running != 0)
		return;

	/* There are queued requests to be processed */
	if (info->si_req_cnt != 0)
		return;

	ret = ABT_pool_get_total_size(pools[DSS_POOL_GENERIC], &blocked);
	if (ret != ABT_SUCCESS) {
		D_ERROR("Get ABT pool(%d) total size error: %d\n",
			DSS_POOL_GENERIC, ret);
		return;
	}

	/*
	 * Unlike sleeping ULTs, the ULTs blocked on sched_cond_wait() could
	 * be woken up by other xstream (or even main thread), so that the
	 * 'blocked' could have been decreased by waking up xstream, but the
	 * 'si_wait_cnt' isn't decreased accordingly by current xstream yet.
	 */
	D_ASSERTF(info->si_sleep_cnt <= blocked,
		  "sleep:%d > blocked:%zd, wait:%d\n",
		  info->si_sleep_cnt, blocked, info->si_wait_cnt);

	/*
	 * Only start relaxing when all blocked ULTs are either sleeping
	 * ULT or long wait ULT.
	 */
	if (blocked > info->si_sleep_cnt + info->si_wait_cnt)
		return;

	/*
	 * System is currently idle, but we only start relaxing when there is
	 * no external events for a short period of SCHED_IDLE_THRESH.
	 */
	D_ASSERT(info->si_cur_ts >= info->si_stats.ss_busy_ts);
	if (info->si_cur_ts - info->si_stats.ss_busy_ts < SCHED_IDLE_THRESH)
		return;

	/* Adjust sleep time according to the first sleeping ULT */
	if (info->si_sleep_cnt > 0) {
		struct sched_request	*req;

		D_ASSERT(!d_list_empty(&info->si_sleep_list));
		req = d_list_entry(info->si_sleep_list.next,
				   struct sched_request, sr_link);

		/* sched_start_cycle() has already been called for info->si_cur_ts */
		D_ASSERT(req->sr_wakeup_time > info->si_cur_ts);
		if (sleep_time > req->sr_wakeup_time - info->si_cur_ts)
			sleep_time = req->sr_wakeup_time - info->si_cur_ts;
	}
	D_ASSERT(sleep_time > 0 && sleep_time <= SCHED_RELAX_INTVL_MAX);

	/*
	 * Wait on external network request if the xstream has Cart context,
	 * otherwise, sleep for a while.
	 */
	if (sched_relax_mode != SCHED_RELAX_MODE_SLEEP && dx->dx_progress_started) {
		/* convert to micro-seconds */
		dx->dx_timeout = sleep_time * 1000;
	} else {
		ret = usleep(sleep_time * 1000);
		if (ret)
			D_ERROR("Sleep error: %s\n", strerror(errno));
	}

	/* Rough stats, interruption isn't taken into account */
	d_tm_inc_counter(info->si_stats.ss_relax_time, sleep_time);
}

static void
sched_start_cycle(struct sched_data *data, ABT_pool *pools)
{
	struct dss_xstream	*dx = data->sd_dx;
	struct sched_info	*info = &dx->dx_sched_info;
	struct sched_cycle	*cycle = &data->sd_cycle;
	size_t			 cnt;
	int			 ret;
	uint64_t		 cur_ts, duration;

	D_ASSERT(cycle->sc_new_cycle == 1);
	D_ASSERT(cycle->sc_cycle_started == 0);
	D_ASSERT(cycle->sc_ults_tot == 0);

	cycle->sc_new_cycle = 0;
	cycle->sc_cycle_started = 1;

	/* Update current ts stored in sched_info */
	cur_ts = daos_getmtime_coarse();
	if (cur_ts < info->si_cur_ts) {
		D_WARN("Backwards time: cur_ts:"DF_U64", si_cur_ts:"DF_U64"\n",
		       cur_ts, info->si_cur_ts);
		cur_ts = info->si_cur_ts;
	}
	duration = cur_ts - info->si_cur_ts;
	info->si_cur_ts = cur_ts;

	wakeup_all(dx);
	process_all(dx);

	/* Get number of ULTS in generic ABT pool */
	D_ASSERT(cycle->sc_ults_cnt[DSS_POOL_GENERIC] == 0);
	ret = ABT_pool_get_size(pools[DSS_POOL_GENERIC], &cnt);
	if (ret != ABT_SUCCESS) {
		D_ERROR("Get ABT pool(%d) size error: %d\n",
			DSS_POOL_GENERIC, ret);
		cnt = 0;
	}
	cycle->sc_ults_cnt[DSS_POOL_GENERIC] = cnt;
	cycle->sc_ults_tot += cycle->sc_ults_cnt[DSS_POOL_GENERIC];

	if (sched_relax_mode != SCHED_RELAX_MODE_DISABLED)
		sched_try_relax(dx, pools, cycle->sc_ults_tot);

	d_tm_inc_counter(info->si_stats.ss_total_time, duration);
	d_tm_set_gauge(info->si_stats.ss_wq_len, info->si_req_cnt);
	d_tm_set_gauge(info->si_stats.ss_sq_len, info->si_sleep_cnt);
	if (cycle->sc_ults_tot) {
		d_tm_set_gauge(info->si_stats.ss_cycle_duration, duration);
		d_tm_set_gauge(info->si_stats.ss_cycle_size, cycle->sc_ults_tot);
	}
}

static inline bool
watchdog_enabled(struct dss_xstream *dx)
{
	if (sched_unit_runtime_max == 0)
		return false;

	return dx->dx_xs_id == 0 || (sched_watchdog_all && dx->dx_main_xs);
}

int
sched_exec_time(uint64_t *msecs, const char *ult_name)
{
	struct dss_xstream	*dx = dss_current_xstream();
	struct sched_info	*info = &dx->dx_sched_info;
	uint64_t		 cur;

	if (!watchdog_enabled(dx))
		return -DER_NOSYS;

	cur = daos_getmtime_coarse();
	if (cur < info->si_ult_start) {
		D_WARN("cur:"DF_U64" < start:"DF_U64"\n", cur, info->si_ult_start);
		*msecs = 0;
		return 0;
	}

	*msecs = cur - info->si_ult_start;
	if (*msecs > sched_unit_runtime_max && ult_name != NULL)
		D_WARN("ULT:%s executed "DF_U64" msecs\n", ult_name, *msecs);
	return 0;
}

static void
sched_watchdog_prep(struct dss_xstream *dx, ABT_unit unit)
{
	struct sched_info	*info = &dx->dx_sched_info;
	ABT_thread		 thread;
	void			 (*thread_func)(void *);
	int			 rc;

	if (!watchdog_enabled(dx))
		return;

	info->si_ult_start = daos_getmtime_coarse();
	rc = ABT_unit_get_thread(unit, &thread);
	D_ASSERT(rc == ABT_SUCCESS);
	rc = ABT_thread_get_thread_func(thread, &thread_func);
	D_ASSERT(rc == ABT_SUCCESS);
	info->si_ult_func = thread_func;
}

static void
sched_watchdog_post(struct dss_xstream *dx)
{
	struct sched_info	*info = &dx->dx_sched_info;
	uint64_t		 cur;
	unsigned int		 elapsed;
	char			**strings;

	/* A ULT is just scheduled, increase schedule seq */
	info->si_cur_seq++;

	if (!watchdog_enabled(dx))
		return;

	cur = daos_getmtime_coarse();
	if (cur < info->si_ult_start) {
		D_WARN("Backwards time, cur:"DF_U64", start:"DF_U64"\n",
		       cur, info->si_ult_start);
		return;
	}

	elapsed = cur - info->si_ult_start;
	if (elapsed <= sched_unit_runtime_max)
		return;

	/* Throttle printing a bit */
	D_ASSERTF(cur >= info->si_stats.ss_watchdog_ts,
		  "cur:"DF_U64" < watchdog_ts:"DF_U64"\n",
		  cur, info->si_stats.ss_watchdog_ts);

	if (info->si_stats.ss_last_unit == info->si_ult_func &&
	    (cur - info->si_stats.ss_watchdog_ts) <= 2000)
		return;

	info->si_stats.ss_last_unit = info->si_ult_func;
	info->si_stats.ss_watchdog_ts = cur;

	strings = backtrace_symbols(&info->si_ult_func, 1);
	D_ERROR("WATCHDOG: Thread %p took %u ms. symbol:%s\n",
		info->si_ult_func, elapsed, strings != NULL ? strings[0] : NULL);

	free(strings);
}

static void
sched_run(ABT_sched sched)
{
	struct sched_data	*data;
	struct sched_cycle	*cycle;
	struct dss_xstream	*dx;
	ABT_pool		 pools[DSS_POOL_CNT];
	ABT_pool		 pool;
	ABT_unit		 unit;
	uint32_t		 work_count = 0;
	int			 ret;

	ABT_sched_get_data(sched, (void **)&data);
	cycle = &data->sd_cycle;
	dx = data->sd_dx;

	ret = ABT_sched_get_pools(sched, DSS_POOL_CNT, 0, pools);
	if (ret != ABT_SUCCESS) {
		D_ERROR("Get ABT pools error: %d\n", ret);
		return;
	}

	while (1) {
		/* Try to pick network poll ULT */
		pool = pools[DSS_POOL_NET_POLL];
		unit = sched_pop_net_poll(data, pool);
		if (unit != ABT_UNIT_NULL)
			goto execute;

		/* Try to pick NVMe poll ULT */
		pool = pools[DSS_POOL_NVME_POLL];
		unit = sched_pop_nvme_poll(data, pool);
		if (unit != ABT_UNIT_NULL)
			goto execute;

		if (cycle->sc_ults_tot == 0)
			goto start_cycle;

		/* Try to pick a ULT from generic ABT pool */
		pool = pools[DSS_POOL_GENERIC];
		unit = sched_pop_one(data, pool, DSS_POOL_GENERIC);
		if (unit != ABT_UNIT_NULL)
			goto execute;

		/*
		 * Nothing to be executed? Could be idle helper XS or poll ULT
		 * hasn't started yet.
		 */
		goto check_event;
execute:
		D_ASSERT(pool != ABT_POOL_NULL);
		sched_watchdog_prep(dx, unit);

		ABT_xstream_run_unit(unit, pool);

		sched_watchdog_post(dx);
start_cycle:
		if (cycle->sc_new_cycle) {
			sched_start_cycle(data, pools);
			sched_dump_data(data);
		}
check_event:
		if (++work_count >= data->sd_event_freq) {
			ABT_bool stop;

			ABT_sched_has_to_stop(sched, &stop);
			if (stop == ABT_TRUE) {
				D_DEBUG(DB_TRACE, "Stop scheduler\n");
				break;
			}
			work_count = 0;
			ABT_xstream_check_events(sched);
		}
	}
}

static int
sched_free(ABT_sched sched)
{
	struct sched_data	*data;

	ABT_sched_get_data(sched, (void **)&data);
	D_FREE(data);

	return ABT_SUCCESS;
}

static void
sched_free_pools(struct dss_xstream *dx)
{
	int	i;

	for (i = 0; i < DSS_POOL_CNT; i++) {
		if (dx->dx_pools[i] != ABT_POOL_NULL) {
			ABT_pool_free(&dx->dx_pools[i]);
			dx->dx_pools[i] = ABT_POOL_NULL;
		}
	}
}

static int
sched_create_pools(struct dss_xstream *dx)
{
	int	i, rc;

	for (i = 0; i < DSS_POOL_CNT; i++) {
		/*
		 * All pools should be created with ABT_POOL_ACCESS_MPSC to
		 * allow in-pool ULTs creating new ULTs for other xstreams.
		 *
		 * Set 'automatic' as ABT_TRUE, so the pools will be freed
		 * automatically.
		 */
		D_ASSERT(dx->dx_pools[i] == ABT_POOL_NULL);
		rc = ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPSC,
					   ABT_TRUE, &dx->dx_pools[i]);
		if (rc != ABT_SUCCESS)
			return rc;
	}
	return ABT_SUCCESS;
}

void
dss_sched_fini(struct dss_xstream *dx)
{
	D_ASSERT(dx->dx_sched != ABT_SCHED_NULL);
	/* Pools will be automatically freed by ABT_sched_free() */
	ABT_sched_free(&dx->dx_sched);
	sched_info_fini(dx);
}

int
dss_sched_init(struct dss_xstream *dx)
{
	ABT_sched_config	config;
	ABT_sched_config_var	event_freq = {
		.idx	= 0,
		.type	= ABT_SCHED_CONFIG_INT
	};
	ABT_sched_config_var	dx_ptr = {
		.idx	= 1,
		.type	= ABT_SCHED_CONFIG_PTR
	};
	ABT_sched_def		sched_def = {
		.type	= ABT_SCHED_TYPE_ULT,
		.init	= sched_init,
		.run	= sched_run,
		.free	= sched_free,
		.get_migr_pool = NULL
	};
	int			rc;

	rc = sched_info_init(dx);
	if (rc)
		return rc;

	/* Create argobots pools */
	rc = sched_create_pools(dx);
	if (rc != ABT_SUCCESS)
		goto err_sched_info;

	/* Create a scheduler config */
	rc = ABT_sched_config_create(&config, event_freq, 512, dx_ptr, dx,
				     ABT_sched_config_var_end);
	if (rc != ABT_SUCCESS)
		goto err_pools;

	rc = ABT_sched_create(&sched_def, DSS_POOL_CNT, dx->dx_pools, config,
			      &dx->dx_sched);
	ABT_sched_config_free(&config);

	if (rc == ABT_SUCCESS)
		return 0;
err_pools:
	sched_free_pools(dx);
err_sched_info:
	sched_info_fini(dx);
	return dss_abterr2der(rc);
}
