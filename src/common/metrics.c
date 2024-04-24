/**
 * (C) Copyright 2016-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * It implements thread-local storage (TLS) for DAOS.
 */
#include <pthread.h>
#include <daos/tls.h>
#include <daos/metrics.h>

struct metrics_list {
	struct daos_module_metrics *mm_metrics;
	d_list_t                    mm_list;
	uint32_t                    mm_id;
};

/* Track list of loaded modules */
D_LIST_HEAD(metrics_mod_list);
pthread_mutex_t metrics_mod_list_lock = PTHREAD_MUTEX_INITIALIZER;

int
daos_metrics_init(enum daos_module_tag tag, uint32_t id, struct daos_module_metrics *metrics)
{
	struct metrics_list *ml;

	D_ALLOC_PTR(ml);
	if (ml == NULL)
		return -DER_NOMEM;
	ml->mm_metrics = metrics;
	ml->mm_id      = id;
	D_MUTEX_LOCK(&metrics_mod_list_lock);
	d_list_add_tail(&ml->mm_list, &metrics_mod_list);
	D_MUTEX_UNLOCK(&metrics_mod_list_lock);

	return 0;
}

void
daos_metrics_fini(void)
{
	struct metrics_list *ml;
	struct metrics_list *tmp;

	D_MUTEX_LOCK(&metrics_mod_list_lock);
	d_list_for_each_entry_safe(ml, tmp, &metrics_mod_list, mm_list) {
		d_list_del_init(&ml->mm_list);
		D_FREE(ml);
	}
	D_MUTEX_UNLOCK(&metrics_mod_list_lock);
}

void
daos_module_fini_metrics(enum dss_module_tag tag, void **metrics)
{
	struct metrics_list *ml;

	D_MUTEX_LOCK(&metrics_mod_list_lock);
	d_list_for_each_entry(ml, &metrics_mod_list, mm_list) {
		struct daos_module_metrics *met = ml->mm_metrics;

		if (met == NULL)
			continue;
		if ((met->dmm_tags & tag) == 0)
			continue;
		if (met->dmm_fini == NULL)
			continue;
		if (metrics[ml->mm_id] == NULL)
			continue;

		met->dmm_fini(metrics[ml->mm_id]);
	}
	D_MUTEX_UNLOCK(&metrics_mod_list_lock);
}

int
daos_module_init_metrics(enum dss_module_tag tag, void **metrics, const char *path, int tgt_id)
{
	struct metrics_list *ml;

	D_MUTEX_LOCK(&metrics_mod_list_lock);
	d_list_for_each_entry(ml, &metrics_mod_list, mm_list) {
		struct daos_module_metrics *met = ml->mm_metrics;

		if (met == NULL)
			continue;
		if ((met->dmm_tags & tag) == 0)
			continue;
		if (met->dmm_init == NULL)
			continue;

		metrics[ml->mm_id] = met->dmm_init(path, tgt_id);
		if (metrics[ml->mm_id] == NULL) {
			D_ERROR("failed to allocate per-pool metrics for module %u\n", ml->mm_id);
			D_MUTEX_UNLOCK(&metrics_mod_list_lock);
			daos_module_fini_metrics(tag, metrics);
			return -DER_NOMEM;
		}
	}
	D_MUTEX_UNLOCK(&metrics_mod_list_lock);

	return 0;
}

/**
 * Query all modules for the number of per-pool metrics they create.
 *
 * \return Total number of metrics for all modules
 */
int
daos_module_nr_pool_metrics(void)
{
	struct metrics_list *ml;
	int                  total = 0;

	d_list_for_each_entry(ml, &metrics_mod_list, mm_list) {
		struct daos_module_metrics *met = ml->mm_metrics;

		if (met == NULL)
			continue;
		if (met->dmm_nr_metrics == NULL)
			continue;
		if (!(met->dmm_tags & DAOS_CLI_TAG))
			continue;

		total += met->dmm_nr_metrics();
	}

	return total;
}
