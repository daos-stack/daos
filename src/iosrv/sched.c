/**
 * (C) Copyright 2016-2019 Intel Corporation.
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
#include "srv_internal.h"

/*
 * A schedule cycle consists of three stages:
 * 1. Starting with a network poll ULT, number of ULTs to be executed in this
 *    cycle is queried by ABT_pool_get_size() for each non-poll ABT pool;
 * 2. Executing all other ULTs which not for hardware polling;
 * 3. Ending with a NVMe poll ULT;
 *
 * Extra network & NVMe poll ULTs could be scheduled in executing stage
 * according to network/NVMe poll age and request/IO statistics.
 */
struct sched_cycle {
	uint32_t	sc_ults_cnt[DSS_POOL_CNT];
	uint32_t	sc_ults_tot;
	/*
	 * bound[0]: Minimum network/NVMe poll age, scheduler always try to
	 * execute few ULTs (if there is any) before next poll.
	 *
	 * bound[1]: Maximum network/NVMe poll age, scheduler will do an extra
	 * poll if it's not polled after executing cerntain amout of ULTs.
	 */
	uint32_t	sc_age_net_bound[2];
	uint32_t	sc_age_nvme_bound[2];
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

static unsigned int sched_throttle[DSS_POOL_CNT] = {
	0,	/* DSS_POOL_NET_POLL */
	0,	/* DSS_POOL_NVME_POLL */
	0,	/* DSS_POOL_IO */
	30,	/* DSS_POOL_REBUILD */
	0,	/* DSS_POOL_AGGREGATE */
	0,	/* DSS_POOL_GC */
};

int
sched_set_throttle(int pool_idx, unsigned int percent)
{
	if (percent >= 100) {
		D_ERROR("Invalid throttle number: %d\n", percent);
		return -DER_INVAL;
	}

	if (pool_idx >= DSS_POOL_CNT) {
		D_ERROR("Invalid pool idx: %d\n", pool_idx);
		return -DER_INVAL;
	}

	if (pool_idx == DSS_POOL_NET_POLL || pool_idx == DSS_POOL_NVME_POLL) {
		D_ERROR("Can't throttle network or NVMe poll\n");
		return -DER_INVAL;
	}

	sched_throttle[pool_idx] = percent;
	return 0;
}

/* #define SCHED_DEBUG */
static void
sched_dump_data(struct sched_data *data)
{
#ifdef SCHED_DEBUG
	struct dss_xstream	*dx = data->sd_dx;
	struct sched_cycle	*cycle = &data->sd_cycle;

	D_PRINT("XS(%d): comm:%d main:%d. age_net:%u, [%u, %u], "
		"age_nvme:%u, [%u, %u] new_cycle:%d cycle_started:%d "
		"total_ults:%u [%u, %u, %u, %u]\n", dx->dx_xs_id,
		dx->dx_comm, dx->dx_main_xs, cycle->sc_age_net,
		cycle->sc_age_net_bound[0], cycle->sc_age_net_bound[1],
		cycle->sc_age_nvme, cycle->sc_age_nvme_bound[0],
		cycle->sc_age_nvme_bound[1], cycle->sc_new_cycle,
		cycle->sc_cycle_started, cycle->sc_ults_tot,
		cycle->sc_ults_cnt[DSS_POOL_IO],
		cycle->sc_ults_cnt[DSS_POOL_REBUILD],
		cycle->sc_ults_cnt[DSS_POOL_AGGREGATE],
		cycle->sc_ults_cnt[DSS_POOL_GC]);
#endif
}

#define SCHED_AGE_NET_MIN		32
#define SCHED_AGE_NET_MAX		512
#define SCHED_AGE_NVME_MIN		32
#define SCHED_AGE_NVME_MAX		512

static int
sched_init(ABT_sched sched, ABT_sched_config config)
{
	struct sched_data	*data;
	struct sched_cycle	*cycle;
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

	cycle = &data->sd_cycle;
	cycle->sc_age_net_bound[0] = SCHED_AGE_NET_MIN;
	cycle->sc_age_net_bound[1] = SCHED_AGE_NET_MAX;
	cycle->sc_age_nvme_bound[0] = SCHED_AGE_NVME_MIN;
	cycle->sc_age_nvme_bound[1] = SCHED_AGE_NVME_MAX;

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
	if (cycle->sc_age_net > cycle->sc_age_net_bound[1])
		return true;

	/* TODO: Take network request statistics into account */

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

	/* XXX Need to figure out why it can pop a NULL unit */
	if (unit == ABT_UNIT_NULL)
		D_ERROR("XS(%d) poped NULL unit for ABT pool(%d)\n",
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
	uint32_t		 limit = 0;
	bool			 space_pressure = false;
	int			 i, ret;

	D_ASSERT(cycle->sc_new_cycle == 1);
	D_ASSERT(cycle->sc_cycle_started == 0);
	D_ASSERT(cycle->sc_ults_tot == 0);

	cycle->sc_new_cycle = 0;
	cycle->sc_cycle_started = 1;

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

		if (sched_throttle[i] > 0 && cycle->sc_ults_cnt[i] > 1)
			limit = 1;
	}

	/* No throttling for helper xstream so far */
	if (!dx->dx_main_xs)
		return;

	if (!limit && !space_pressure)
		return;

	/*
	 * Throttle the ABT pools which have throttle setting.
	 * TODO: If it's under space pressure, throttle IO pool.
	 */
	for (i = DSS_POOL_IO; i < DSS_POOL_CNT; i++) {
		unsigned int	throttle = sched_throttle[i];
		int		diff;

		if (sched_throttle[i] == 0)
			continue;

		/*
		 * No ULTs from other ABT pools in current cycle, or too few
		 * ULTs in current cycle.
		 */
		if (cycle->sc_ults_cnt[i] == cycle->sc_ults_tot ||
		    cycle->sc_ults_tot <= cycle->sc_age_net_bound[0])
			continue;

		D_ASSERT(throttle < 100);
		limit = max(cycle->sc_ults_tot * throttle / 100, 1);
		diff = cycle->sc_ults_cnt[i] - limit;

		D_ASSERT(cycle->sc_ults_tot > diff);
		if (diff > 0 &&
		    (cycle->sc_ults_tot - diff) > cycle->sc_age_net_bound[0]) {
			cycle->sc_ults_cnt[i] -= diff;
			cycle->sc_ults_tot -= diff;
		}
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
		D_ASSERT(pool != ABT_UNIT_NULL);
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

	/* Create argobots pools */
	rc = sched_create_pools(dx);
	if (rc != ABT_SUCCESS)
		goto out;

	/* Create a scheduler config */
	rc = ABT_sched_config_create(&config, event_freq, 512, dx_ptr, dx,
				     ABT_sched_config_var_end);
	if (rc != ABT_SUCCESS)
		goto out;

	rc = ABT_sched_create(&sched_def, DSS_POOL_CNT, dx->dx_pools, config,
			      &dx->dx_sched);
	ABT_sched_config_free(&config);
out:
	if (rc)
		sched_free_pools(dx);
	return dss_abterr2der(rc);
}
