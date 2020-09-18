/**
 * (C) Copyright 2016-2020 Intel Corporation.
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

#define D_LOGFAC       DD_FAC(server)

#include <abt.h>
#include <daos/common.h>
#include <daos_errno.h>
#include <daos_srv/vos.h>
#include "srv_internal.h"

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
	unsigned int		 sr_abort:1;
};

static bool	sched_prio_disabled;

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

#define SCHED_DELAY_THRESH	20000	/* msecs */

static unsigned int max_delay_msecs[SCHED_REQ_MAX] = {
	20000,	/* SCHED_REQ_UPDATE */
	1000,	/* SCHED_REQ_FETCH */
	500,	/* SCHED_REQ_GC */
	20000,	/* SCHED_REQ_MIGRATE */
};

static unsigned int max_qds[SCHED_REQ_MAX] = {
	64000,	/* SCHED_REQ_UPDATE */
	32000,	/* SCHED_REQ_FETCH */
	1024,	/* SCHED_REQ_GC */
	64000,	/* SCHED_REQ_MIGRATE */
};

static unsigned int req_throttle[SCHED_REQ_MAX] = {
	0,	/* SCHED_REQ_UPDATE */
	0,	/* SCHED_REQ_FETCH */
	30,	/* SCHED_REQ_GC */
	30,	/* SCHED_REQ_REBUILD */
};

/*
 * Throttle certain type of requests to N percent of IO requests
 * in a cycle. IO requests can't be throttled.
 */
int
sched_set_throttle(unsigned int type, unsigned int percent)
{
	if (percent >= 100) {
		D_ERROR("Invalid throttle number: %d\n", percent);
		return -DER_INVAL;
	}

	if (type >= SCHED_REQ_MAX) {
		D_ERROR("Invalid request type: %d\n", type);
		return -DER_INVAL;
	}

	if (type == SCHED_REQ_UPDATE || type == SCHED_REQ_FETCH) {
		D_ERROR("Can't throttle IO requests");
		return -DER_INVAL;
	}

	req_throttle[type] = percent;
	return 0;
}

struct pressure_ratio {
	unsigned int	pr_free;	/* free space ratio */
	unsigned int	pr_throttle;	/* update throttle ratio */
	unsigned int	pr_delay;	/* update being delayed in msec */
	unsigned int	pr_pressure;	/* index in pressure_gauge */
};

static struct pressure_ratio pressure_gauge[] = {
	{	/* free space > 40%, no space pressure */
		.pr_free	= 40,
		.pr_throttle	= 100,
		.pr_delay	= 0,
		.pr_pressure	= SCHED_SPACE_PRESS_NONE,
	},
	{	/* free space > 30% */
		.pr_free	= 30,
		.pr_throttle	= 70,
		.pr_delay	= 2000, /* msecs */
		.pr_pressure	= 1,
	},
	{	/* free space > 20% */
		.pr_free	= 20,
		.pr_throttle	= 40,
		.pr_delay	= 4000, /* msecs */
		.pr_pressure	= 2,
	},
	{	/* free space > 10% */
		.pr_free	= 10,
		.pr_throttle	= 20,
		.pr_delay	= 8000, /* msecs */
		.pr_pressure	= 3,
	},
	{	/* free space > 5% */
		.pr_free	= 5,
		.pr_throttle	= 10,
		.pr_delay	= 12000, /* msecs */
		.pr_pressure	= 4,
	},
	{	/* free space <= 5% */
		.pr_free	= 0,
		.pr_throttle	= 5,
		.pr_delay	= 20000, /* msecs */
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

static void
spi_rec_free(struct d_hash_table *htable, d_list_t *rlink)
{
	struct sched_pool_info	*spi = sched_rlink2spi(rlink);
	unsigned int		 type;

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
		if (spi->spi_req_cnt == 0) {
			deleted = d_hash_rec_delete(info->si_pool_hash,
						    pi->pi_pool_id,
						    sizeof(uuid_t));
			if (!deleted)
				D_ERROR("XS(%d): Purge "DF_UUID" failed.\n",
					dx->dx_xs_id, DP_UUID(pi->pi_pool_id));
		} else {
			unsigned int type;

			D_ERROR("XS(%d): Pool "DF_UUID", req_cnt:%u\n",
				dx->dx_xs_id, DP_UUID(pi->pi_pool_id),
				spi->spi_req_cnt);

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

	D_CDEBUG(spi->spi_req_cnt == 0, DB_TRACE, DLOG_ERR,
		 "XS(%d): vos pool:"DF_UUID" is destroyed. req_cnt:%u\n",
		 dx->dx_xs_id, DP_UUID(spi->spi_pool_id), spi->spi_req_cnt);

	/* Don't purge the spi when there is queued request */
	if (spi->spi_req_cnt != 0)
		return;

	d_list_for_each_entry(pi, &info->si_purge_list, pi_link) {
		/* Already in purge list */
		if (uuid_compare(pi->pi_pool_id, spi->spi_pool_id) == 0)
			return;
	}

	D_ALLOC_PTR(pi);
	if (pi == NULL) {
		D_ERROR("XS(%d): Alloc purge item failed.\n", dx->dx_xs_id);
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

static int
sched_info_init(struct dss_xstream *dx)
{
	struct sched_info	*info = &dx->dx_sched_info;
	int			 rc;

	info->si_cur_ts = daos_getntime_coarse() / NSEC_PER_MSEC;
	D_INIT_LIST_HEAD(&info->si_idle_list);
	D_INIT_LIST_HEAD(&info->si_sleep_list);
	D_INIT_LIST_HEAD(&info->si_fifo_list);
	D_INIT_LIST_HEAD(&info->si_purge_list);
	info->si_req_cnt = 0;
	info->si_stop = 0;

	rc = d_hash_table_create(D_HASH_FT_NOLOCK, 4,
				 NULL, &sched_pool_hash_ops,
				 &info->si_pool_hash);
	if (rc) {
		D_ERROR("XS(%d): Create sched pool hash failed. "DF_RC".\n",
			dx->dx_xs_id, DP_RC(rc));
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
	void (*func)(void *), void *arg, ABT_thread ult)
{
	struct sched_info	*info = &dx->dx_sched_info;
	struct sched_pool_info	*spi;
	struct sched_request	*req;
	int			 rc;

	spi = cur_pool_info(info, attr->sra_pool_id);
	if (spi == NULL) {
		D_ERROR("XS(%d): get pool info "DF_UUID" failed.\n",
			dx->dx_xs_id, DP_UUID(attr->sra_pool_id));
		return NULL;
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

static int
req_kickoff_internal(struct dss_xstream *dx, struct sched_req_attr *attr,
		     void (*func)(void *), void *arg)
{
	ABT_pool	abt_pool;
	int		rc;

	D_ASSERT(attr && func && arg);
	switch (attr->sra_type) {
	case SCHED_REQ_UPDATE:
	case SCHED_REQ_FETCH:
		abt_pool = dx->dx_pools[DSS_POOL_IO];
		break;
	case SCHED_REQ_GC:
		abt_pool = dx->dx_pools[DSS_POOL_GC];
		break;
	case SCHED_REQ_MIGRATE:
		abt_pool = dx->dx_pools[DSS_POOL_REBUILD];
		break;
	default:
		D_ASSERTF(0, "Invalid req type: %u\n", attr->sra_type);
		break;
	}

	rc = ABT_thread_create(abt_pool, func, arg, ABT_THREAD_ATTR_NULL, NULL);

	return dss_abterr2der(rc);
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
	uint64_t		 scm_left;
	struct pressure_ratio	*pr;
	int			 orig_pressure, rc;

	D_ASSERT(spi->spi_space_ts <= info->si_cur_ts);
	/* Use cached space presure info */
	if ((spi->spi_space_ts + SCHED_SPACE_AGE_MAX) > info->si_cur_ts)
		goto out;

	rc = vos_pool_query_space(spi->spi_pool_id, &vps);
	if (rc == -DER_NONEXIST) {	/* vos pool is destroyed */
		add_purge_list(dx, spi);
		goto out;
	} else if (rc) {
		D_ERROR("XS(%d): query pool:"DF_UUID" space failed. "DF_RC"\n",
			dx->dx_xs_id, DP_UUID(spi->spi_pool_id), DP_RC(rc));
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

	orig_pressure = spi->spi_space_pressure;
	for (pr = &pressure_gauge[0]; pr->pr_free != 0; pr++) {
		if (scm_left > (SCM_TOTAL(&vps) * pr->pr_free / 100))
			break;
	}
	spi->spi_space_pressure = pr->pr_pressure;

	if (spi->spi_space_pressure != SCHED_SPACE_PRESS_NONE &&
	    spi->spi_space_pressure != orig_pressure) {
		D_INFO("XS(%d): pool:"DF_UUID" is under %d presure, "
		       "SCM: tot["DF_U64"], sys["DF_U64"], free["DF_U64"] "
		       "NVMe: tot["DF_U64"], sys["DF_U64"], free["DF_U64"]\n",
		       dx->dx_xs_id, DP_UUID(spi->spi_pool_id),
		       spi->spi_space_pressure, SCM_TOTAL(&vps),
		       SCM_SYS(&vps), SCM_FREE(&vps), NVME_TOTAL(&vps),
		       NVME_SYS(&vps), NVME_FREE(&vps));

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
process_req_list(struct dss_xstream *dx, d_list_t *list)
{
	struct sched_request	*req, *tmp;
	int			 rc;

	d_list_for_each_entry_safe(req, tmp, list, sr_link) {
		rc = process_req(dx, req);
		if (rc)
			break;
	}
}

static inline void
reset_req_limit(struct dss_xstream *dx, struct sched_pool_info *spi,
		unsigned int req_type, unsigned int limit)
{
	unsigned int	tot = pool2req_cnt(spi, req_type);

	D_ASSERT(limit <= tot);
	if (tot - limit > max_qds[req_type]) {
		D_CRIT("XS(%d) Too large QD: %u/%u/%u for req:%d\n",
		       dx->dx_xs_id, tot, max_qds[req_type], limit, req_type);
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

static inline unsigned int
throttle_update(unsigned int u_max, struct pressure_ratio *pr)
{
	if (u_max == 0)
		return 0;

	/* Severe space pressure */
	if (pr->pr_free == 0)
		return u_max * pr->pr_throttle / 100;

	/* Keep IO flow moving when there are only few inflight updates */
	if ((u_max * pr->pr_throttle / 100) == 0)
		return 1;

	return u_max * pr->pr_throttle / 100;
}

static int
process_pool_cb(d_list_t *rlink, void *arg)
{
	struct dss_xstream	*dx = (struct dss_xstream *)arg;
	struct sched_info	*info = &dx->dx_sched_info;
	struct sched_pool_info	*spi;
	unsigned int		 u_max, f_max, io_max, gc_max, mig_max;
	unsigned int		 gc_thr, mig_thr;
	struct pressure_ratio	*pr;
	int			 press;

	spi = sched_rlink2spi(rlink);

	gc_thr	= req_throttle[SCHED_REQ_GC];
	mig_thr	= req_throttle[SCHED_REQ_MIGRATE];
	D_ASSERT(gc_thr < 100 && mig_thr < 100);

	u_max	= pool2req_cnt(spi, SCHED_REQ_UPDATE);
	f_max	= pool2req_cnt(spi, SCHED_REQ_FETCH);
	io_max	= u_max + f_max;

	gc_max	= pool2req_cnt(spi, SCHED_REQ_GC);
	mig_max	= pool2req_cnt(spi, SCHED_REQ_MIGRATE);

	press = check_space_pressure(dx, spi);

	if (press == SCHED_SPACE_PRESS_NONE) {
		/* Throttle GC & aggregation */
		if (io_max && gc_max && gc_thr)
			gc_max = min(gc_max, io_max * gc_thr / 100);
		goto out;
	}

	pr = &pressure_gauge[press];
	D_ASSERT(pr->pr_throttle < 100);

	if (pr->pr_free != 0) {	/* Light space pressure */
		/* Throttle updates when there is space to be reclaimed */
		if (is_gc_pending(spi)) {
			u_max	= throttle_update(u_max, pr);
			io_max	= u_max + f_max;
		}
	} else {		/* Severe space pressure */
		/*
		 * If space pressure stays in highest level for a while, we
		 * can assume that no available space could be reclaimed, so
		 * throttling can be stopped and ENOSPACE could be returned
		 * to client sooner.
		 */
		if (is_pressure_recent(info, spi)) {
			u_max	= throttle_update(u_max, pr);
			/*
			 * Delay all rebuild and reintegration requests for
			 * this moment, since we can't tell if they are for
			 * update or fetch.
			 */
			mig_max	= 0;
		}
	}

out:
	/* Throttle rebuild and reintegration */
	if (mig_max && io_max && mig_thr) {
		mig_thr = max(1, io_max * mig_thr / 100);
		mig_max = min(mig_max, mig_thr);
	}

	reset_req_limit(dx, spi, SCHED_REQ_UPDATE, u_max);
	reset_req_limit(dx, spi, SCHED_REQ_FETCH, f_max);
	reset_req_limit(dx, spi, SCHED_REQ_GC, gc_max);
	reset_req_limit(dx, spi, SCHED_REQ_MIGRATE, mig_max);

	process_req_list(dx, pool2req_list(spi, SCHED_REQ_GC));
	process_req_list(dx, pool2req_list(spi, SCHED_REQ_MIGRATE));

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

	process_req_list(dx, &info->si_fifo_list);
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

	if (info->si_req_cnt == 0) {
		D_ASSERT(d_list_empty(&info->si_fifo_list));
		return;
	}

	prune_purge_list(dx);
	rc = d_hash_table_traverse(info->si_pool_hash, process_pool_cb, dx);
	if (rc)
		D_ERROR("XS(%d) traverse pool hash error. "DF_RC"\n",
			dx->dx_xs_id, DP_RC(rc));

	D_ASSERT(policy_ops[sched_policy].process_io != NULL);
	policy_ops[sched_policy].process_io(dx);
}

static inline bool
should_enqueue_req(struct dss_xstream *dx, struct sched_req_attr *attr)
{
	struct sched_info	*info = &dx->dx_sched_info;

	if (sched_prio_disabled || info->si_stop)
		return false;

	D_ASSERT(attr->sra_type == SCHED_REQ_GC ||
		 attr->sra_type == SCHED_REQ_UPDATE ||
		 attr->sra_type == SCHED_REQ_FETCH ||
		 attr->sra_type == SCHED_REQ_MIGRATE);

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

	D_ASSERT(attr->sra_type < SCHED_REQ_MAX);
	req = req_get(dx, attr, func, arg, ABT_THREAD_NULL);
	if (req == NULL) {
		D_ERROR("XS(%d): get req failed.\n", dx->dx_xs_id);
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
gc_sleep_counting(struct sched_request *req, int sleep)
{
	struct sched_pool_info	*spi = req->sr_pool_info;

	D_ASSERT(spi != NULL);
	if (req->sr_attr.sra_type != SCHED_REQ_GC)
		return;

	spi->spi_gc_sleeping += sleep;

	D_ASSERT(spi->spi_gc_sleeping >= 0);
	D_ASSERTF(spi->spi_gc_sleeping <= spi->spi_gc_ults,
		  "gc:%d, sleeping:%d\n", spi->spi_gc_ults,
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

	gc_sleep_counting(req, 1);

	ABT_self_suspend();
}

void
sched_req_wakeup(struct sched_request *req)
{
	D_ASSERT(req != NULL);
	/* The request is not in sleep */
	if (req->sr_wakeup_time == 0)
		return;

	D_ASSERT(!d_list_empty(&req->sr_link));
	d_list_del_init(&req->sr_link);
	req->sr_wakeup_time = 0;

	gc_sleep_counting(req, -1);

	D_ASSERT(req->sr_ult != ABT_THREAD_NULL);
	ABT_thread_resume(req->sr_ult);
}

void
sched_req_wait(struct sched_request *req, bool abort)
{
	D_ASSERT(req != NULL);
	if (abort) {
		req->sr_abort = 1;
		sched_req_wakeup(req);
	}
	D_ASSERT(req->sr_ult != ABT_THREAD_NULL);
	ABT_thread_join(req->sr_ult);
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

	/* Update current ts stored in sched_info */
	info->si_cur_ts = daos_getntime_coarse() / NSEC_PER_MSEC;

	d_list_for_each_entry_safe(req, tmp, &info->si_sleep_list, sr_link) {
		D_ASSERT(req->sr_wakeup_time > 0);
		if (!info->si_stop && req->sr_wakeup_time > info->si_cur_ts)
			break;

		if (!should_enqueue_req(dx, &req->sr_attr)) {
			sched_req_wakeup(req);
		} else {
			d_list_del_init(&req->sr_link);
			req->sr_wakeup_time = 0;
			gc_sleep_counting(req, -1);
			D_ASSERT(req->sr_ult != ABT_THREAD_NULL);
			req_enqueue(dx, req);
		}
	}
}

struct sched_request *
sched_req_get(struct sched_req_attr *attr, ABT_thread ult)
{
	struct dss_xstream	*dx = dss_current_xstream();
	struct sched_request	*req;
	int			 rc;

	D_ASSERT(attr->sra_type == SCHED_REQ_GC ||
		 attr->sra_type == SCHED_REQ_UPDATE ||
		 attr->sra_type == SCHED_REQ_FETCH ||
		 attr->sra_type == SCHED_REQ_MIGRATE);

	if (ult == ABT_THREAD_NULL) {
		ABT_thread	self;

		rc = ABT_thread_self(&self);
		if (rc) {
			D_ERROR("Failed to get self thread. "DF_RC"\n",
				DP_RC(dss_abterr2der(rc)));
			return NULL;
		}
		ult = self;
	}

	req = req_get(dx, attr, NULL, NULL, ult);
	if (req != NULL && attr->sra_type == SCHED_REQ_GC)
		req->sr_pool_info->spi_gc_ults++;

	return req;
}

void
sched_req_put(struct sched_request *req)
{
	struct dss_xstream	*dx = dss_current_xstream();
	struct sched_info	*info = &dx->dx_sched_info;

	D_ASSERT(req != NULL && req->sr_ult != ABT_THREAD_NULL);
	D_ASSERT(d_list_empty(&req->sr_link));
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

	D_PRINT("XS(%d): comm:%d main:%d. age_net:%u, age_nvme:%u, "
		"new_cycle:%d cycle_started:%d total_ults:%u [%u, %u, %u]\n",
		dx->dx_xs_id, dx->dx_comm, dx->dx_main_xs, cycle->sc_age_net,
		cycle->sc_age_nvme, cycle->sc_new_cycle,
		cycle->sc_cycle_started, cycle->sc_ults_tot,
		cycle->sc_ults_cnt[DSS_POOL_IO],
		cycle->sc_ults_cnt[DSS_POOL_REBUILD],
		cycle->sc_ults_cnt[DSS_POOL_GC]);
#endif
}

#define SCHED_AGE_NET_MAX		512
#define SCHED_AGE_NVME_MAX		512

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
	struct dss_xstream	*dx = data->sd_dx;
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
		D_ERROR("XS(%d) failed to pop network poll ULT: %d\n",
			dx->dx_xs_id, ret);
		return ABT_UNIT_NULL;
	}

	return unit;
}

static bool
need_nvme_poll(struct sched_cycle *cycle)
{
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

	if (!need_nvme_poll(cycle))
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
		D_ERROR("XS(%d) failed to pop NVMe poll ULT: %d\n",
			dx->dx_xs_id, ret);
		return ABT_UNIT_NULL;
	}

	return unit;
}

static ABT_unit
sched_pop_one(struct sched_data *data, ABT_pool pool, int pool_idx)
{
	struct dss_xstream	*dx = data->sd_dx;
	struct sched_cycle	*cycle = &data->sd_cycle;
	ABT_unit		 unit;
	int			 ret;

	D_ASSERT(cycle->sc_ults_tot >= cycle->sc_ults_cnt[pool_idx]);
	if (cycle->sc_ults_cnt[pool_idx] == 0)
		return ABT_UNIT_NULL;

	ret = ABT_pool_pop(pool, &unit);
	if (ret != ABT_SUCCESS) {
		D_ERROR("XS(%d) failed to pop ULT for ABT pool(%d): %d\n",
			dx->dx_xs_id, pool_idx, ret);
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
		D_DEBUG(DB_TRACE, "XS(%d) popped NULL unit for ABT pool(%d)\n",
			dx->dx_xs_id, pool_idx);

	cycle->sc_age_net++;
	cycle->sc_age_nvme++;
	cycle->sc_ults_cnt[pool_idx] -= 1;
	cycle->sc_ults_tot -= 1;

	return unit;
}

static void
sched_start_cycle(struct sched_data *data, ABT_pool *pools)
{
	struct dss_xstream	*dx = data->sd_dx;
	struct sched_cycle	*cycle = &data->sd_cycle;
	size_t			 cnt;
	int			 i, ret;

	D_ASSERT(cycle->sc_new_cycle == 1);
	D_ASSERT(cycle->sc_cycle_started == 0);
	D_ASSERT(cycle->sc_ults_tot == 0);

	cycle->sc_new_cycle = 0;
	cycle->sc_cycle_started = 1;

	wakeup_all(dx);
	process_all(dx);

	/* Get number of ULTS for each ABT pool */
	for (i = DSS_POOL_IO; i < DSS_POOL_CNT; i++) {
		D_ASSERT(cycle->sc_ults_cnt[i] == 0);

		ret = ABT_pool_get_size(pools[i], &cnt);
		if (ret != ABT_SUCCESS) {
			D_ERROR("XS(%d) get ABT pool(%d) size error: %d\n",
				dx->dx_xs_id, i, ret);
			cnt = 0;
		}

		cycle->sc_ults_cnt[i] = cnt;
		cycle->sc_ults_tot += cycle->sc_ults_cnt[i];
	}
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
	int			 i, ret;

	ABT_sched_get_data(sched, (void **)&data);
	cycle = &data->sd_cycle;
	dx = data->sd_dx;

	ret = ABT_sched_get_pools(sched, DSS_POOL_CNT, 0, pools);
	if (ret != ABT_SUCCESS) {
		D_ERROR("XS(%d) get ABT pools error: %d\n",
			dx->dx_xs_id, ret);
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

		/* Try to pick a ULT from other ABT pools */
		for (i = DSS_POOL_IO; i < DSS_POOL_CNT; i++) {
			pool = pools[i];
			unit = sched_pop_one(data, pool, i);
			if (unit != ABT_UNIT_NULL)
				goto execute;
		}

		/*
		 * Nothing to be executed? Could be idle helper XS or poll ULT
		 * hasn't started yet.
		 */
		goto check_event;
execute:
		D_ASSERT(pool != ABT_POOL_NULL);
		ABT_xstream_run_unit(unit, pool);
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
				D_DEBUG(DB_TRACE, "XS(%d) stop scheduler\n",
					dx->dx_xs_id);
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
