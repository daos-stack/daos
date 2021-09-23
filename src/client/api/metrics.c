/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(client)

#include <common.h>
#include <daos_metrics.h>
#include <daos/pool.h>
#include <daos/container.h>
#include <daos/object.h>

/** Tunable to enable/disable metrics */
int is_metrics_enabled;

/** Linked list for TLS metric data */
static D_LIST_HEAD(metrics_list);
static pthread_mutex_t metrics_tls_lock = PTHREAD_MUTEX_INITIALIZER;

/** Thread specific tls data pointer */
__thread dc_metrics_tls_data_t *metrics_tls;
pthread_key_t metrics_tls_key;

/** Holds the aggregated values of metrics from terminated threads */
static dc_metrics_tls_data_t metrics_tls_agg;

#define	RPDIST_ENUM_IDX_CNT	129
#define	ECDIST_ENUM_IDX_CNT	18
static int rpdist_enum_idx[RPDIST_ENUM_IDX_CNT];
static int ecdist_enum_idx[ECDIST_ENUM_IDX_CNT];

static void dc_metrics_tls_cleanup(void *value);

int
dc_metrics_tls_init()
{
	int rc = 0;

	rc = pthread_key_create(&metrics_tls_key, dc_metrics_tls_cleanup);
	if (rc)
		return -DER_NOMEM;

	metrics_tls_agg.update_stat.st_min = ULONG_MAX;
	metrics_tls_agg.fetch_stat.st_min = ULONG_MAX;
	D_MUTEX_LOCK(&metrics_tls_lock);
	d_list_add(&metrics_tls_agg.list, &metrics_list);
	D_MUTEX_UNLOCK(&metrics_tls_lock);
	return 0;
}

int
dc_metrics_tls_alloc()
{
	if (metrics_tls != NULL)
		return 0;

	D_ALLOC_PTR(metrics_tls);
	if (metrics_tls == NULL)
		return -DER_NOMEM;

	metrics_tls->update_stat.st_min = ULONG_MAX;
	metrics_tls->fetch_stat.st_min = ULONG_MAX;
	D_MUTEX_LOCK(&metrics_tls_lock);
	d_list_add(&metrics_tls->list, &metrics_list);
	pthread_setspecific(metrics_tls_key, metrics_tls);
	D_MUTEX_UNLOCK(&metrics_tls_lock);
	return 0;
}

static void
dc_metrics_reset_tls_data()
{
	dc_metrics_tls_data_t *entry;

	D_MUTEX_LOCK(&metrics_tls_lock);
	d_list_for_each_entry(entry, &metrics_list, list) {
		memset(&entry->update_stat, 0, sizeof(entry->update_stat));
		entry->update_stat.st_min = ULONG_MAX;
		memset(&entry->fetch_stat, 0, sizeof(entry->fetch_stat));
		entry->fetch_stat.st_min = ULONG_MAX;
		memset(&entry->idsz, 0, sizeof(entry->idsz));
		memset(&entry->udrp, 0, sizeof(entry->udrp));
		memset(&entry->udec, 0, sizeof(entry->udec));
	}
	D_MUTEX_UNLOCK(&metrics_tls_lock);
}

static inline void
dc_metrics_aggr_stats(daos_metrics_stat_t *dest, daos_metrics_stat_t *src)
{
	dest->st_value += src->st_value;
	if (dest->st_min > src->st_min)
		dest->st_min = src->st_min;
	if (dest->st_max < src->st_max)
		dest->st_max = src->st_max;
	dest->st_sum += src->st_sum;
	dest->st_sum_of_squares += src->st_sum_of_squares;
}

static void
dc_metrics_tls_aggr(dc_metrics_tls_data_t *value)
{
	int i;

	/** Update stats */
	dc_metrics_aggr_stats(&metrics_tls_agg.update_stat, &value->update_stat);

	/** Fetch stats */
	dc_metrics_aggr_stats(&metrics_tls_agg.fetch_stat, &value->fetch_stat);

	/** io distribution by size */
	for (i = 0; i < DAOS_METRICS_DIST_IO_BKT_COUNT; i++) {
		metrics_tls_agg.idsz[i].ids_updatecnt += value->idsz[i].ids_updatecnt;
		metrics_tls_agg.idsz[i].ids_fetchcnt  += value->idsz[i].ids_fetchcnt;
	}

	/** update distribution for RP based protection type */
	for (i = 0; i < DAOS_METRICS_DIST_RP_BKT_COUNT; i++) {
		metrics_tls_agg.udrp[i].udrp_updatecnt += value->udrp[i].udrp_updatecnt;
		metrics_tls_agg.udrp[i].udrp_updatesz += value->udrp[i].udrp_updatesz;
	}

	/** update distribution for EC based protection type */
	for (i = 0; i < DAOS_METRICS_DIST_EC_BKT_COUNT; i++) {
		metrics_tls_agg.udec[i].udec_full_updatecnt += value->udec[i].udec_full_updatecnt;
		metrics_tls_agg.udec[i].udec_full_updatesz += value->udec[i].udec_full_updatesz;
		metrics_tls_agg.udec[i].udec_part_updatecnt += value->udec[i].udec_part_updatecnt;
		metrics_tls_agg.udec[i].udec_part_updatesz += value->udec[i].udec_part_updatesz;
	}
}

static void
dc_metrics_tls_cleanup(void *value)
{
	if (metrics_tls == NULL)
		return;

	D_MUTEX_LOCK(&metrics_tls_lock);
	d_list_del(&metrics_tls->list);
	dc_metrics_tls_aggr((dc_metrics_tls_data_t *)value);
	D_MUTEX_UNLOCK(&metrics_tls_lock);
	D_FREE(metrics_tls);
	metrics_tls = NULL;
}


int
dc_metrics_init()
{
	int rc, i;
	bool val = false;

	d_getenv_bool("DAOS_CLI_METRICS_DISABLE", &val);
	if (val)
		is_metrics_enabled = 0;
	else
		is_metrics_enabled = 1;

	if (is_metrics_enabled == 0)
		return 0;

	rc = dc_pool_metrics_init();
	if (rc != 0) {
		D_ERROR("Failed to initialize metrics for pool: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}
	rc = dc_cont_metrics_init();
	if (rc != 0) {
		D_ERROR("Failed to initialize metrics for container: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_cont, rc);
	}
	rc = dc_obj_metrics_init();
	if (rc != 0) {
		D_ERROR("Failed to initialize metrics for object: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_obj, rc);
	}
	rc = dc_metrics_tls_init();
	if (rc != 0) {
		D_ERROR("Failed to initialize metrics TLS: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out_tls, rc);
	}

	for (i = 0; i < RPDIST_ENUM_IDX_CNT; i++)
		rpdist_enum_idx[i] = DAOS_METRICS_DIST_RPU;

	rpdist_enum_idx[1] = DAOS_METRICS_DIST_NORP;
	rpdist_enum_idx[2] = DAOS_METRICS_DIST_RP2;
	rpdist_enum_idx[3] = DAOS_METRICS_DIST_RP3;
	rpdist_enum_idx[4] = DAOS_METRICS_DIST_RP4;
	rpdist_enum_idx[6] = DAOS_METRICS_DIST_RP6;
	rpdist_enum_idx[8] = DAOS_METRICS_DIST_RP8;
	rpdist_enum_idx[12] = DAOS_METRICS_DIST_RP12;
	rpdist_enum_idx[16] = DAOS_METRICS_DIST_RP16;
	rpdist_enum_idx[32] = DAOS_METRICS_DIST_RP32;
	rpdist_enum_idx[48] = DAOS_METRICS_DIST_RP48;
	rpdist_enum_idx[64] = DAOS_METRICS_DIST_RP64;
	rpdist_enum_idx[128] = DAOS_METRICS_DIST_RP128;

	for (i = 0; i < ECDIST_ENUM_IDX_CNT; i++)
		ecdist_enum_idx[i] = DAOS_METRICS_DIST_ECU;
	ecdist_enum_idx[2] = DAOS_METRICS_DIST_EC2P1;
	ecdist_enum_idx[3] = DAOS_METRICS_DIST_EC2P2;
	ecdist_enum_idx[4] = DAOS_METRICS_DIST_EC4P1;
	ecdist_enum_idx[5] = DAOS_METRICS_DIST_EC4P2;
	ecdist_enum_idx[8] = DAOS_METRICS_DIST_EC8P1;
	ecdist_enum_idx[9] = DAOS_METRICS_DIST_EC8P2;
	ecdist_enum_idx[16] = DAOS_METRICS_DIST_EC16P1;
	ecdist_enum_idx[17] = DAOS_METRICS_DIST_EC16P2;

	D_GOTO(out, rc);
out_tls:
	dc_obj_metrics_fini();
out_obj:
	dc_cont_metrics_fini();
out_cont:
	dc_pool_metrics_fini();
out:
	return rc;
}

void
dc_metrics_fini()
{
	bool val = false;

	if (is_metrics_enabled == 0)
		return;

	d_getenv_bool("DAOS_CLI_METRICS_DUMP", &val);
	if (val)
		daos_metrics_dump(stderr);

	dc_pool_metrics_fini();
	dc_cont_metrics_fini();
	dc_obj_metrics_fini();
	if (metrics_tls)
		dc_metrics_tls_cleanup(metrics_tls);
}

static void
dc_metrics_update_iostats(int is_update, size_t size)
{
	dc_metrics_tls_data_t *ptr;
	daos_metrics_stat_t *iostat;

	ptr = metrics_tls;
	if (is_update)
		iostat = &ptr->update_stat;
	else
		iostat = &ptr->fetch_stat;
	iostat->st_value++;
	if (iostat->st_min > size)
		iostat->st_min = size;
	if (iostat->st_max < size)
		iostat->st_max = size;
	iostat->st_sum += size;
	iostat->st_sum_of_squares += size*size;
}

static void
dc_metrics_update_iodist(int is_update, size_t size, struct daos_oclass_attr *ptype,
				int is_part_stripe)
{
	int idx, num, dcells, kcells;
	dc_metrics_tls_data_t *ptr = metrics_tls;
	size_t bsize = 4*1024*1024;

	idx = DAOS_METRICS_DIST_IO_BKT_COUNT-1;
	while ((size < bsize) && (idx > 0)) {
		idx--;
		bsize >>= 1;
	}
	if (is_update) {
		ptr->idsz[idx].ids_updatecnt++;
	} else {
		ptr->idsz[idx].ids_fetchcnt++;
	}

	if (is_update) {
		if (ptype->ca_resil == DAOS_RES_REPL) {
			num = ptype->u.rp.r_num;
			if (num < RPDIST_ENUM_IDX_CNT) {
				ptr->udrp[rpdist_enum_idx[num]].udrp_updatecnt++;
				ptr->udrp[rpdist_enum_idx[num]].udrp_updatesz += size;
			} else {
				ptr->udrp[DAOS_METRICS_DIST_RPU].udrp_updatecnt++;
				ptr->udrp[DAOS_METRICS_DIST_RPU].udrp_updatesz += size;
			}
		} else if (ptype->ca_resil == DAOS_RES_EC) {
			dcells = ptype->u.ec.e_k;
			kcells = ptype->u.ec.e_p;

			idx = DAOS_METRICS_DIST_ECU;
			if ((dcells < ECDIST_ENUM_IDX_CNT) && (kcells < 3))
				idx = ecdist_enum_idx[dcells];
			if (idx != DAOS_METRICS_DIST_ECU)
				idx += (kcells - 1);
			if (is_part_stripe) {
				ptr->udec[idx].udec_part_updatecnt++;
				ptr->udec[idx].udec_part_updatesz += size;
			} else {
				ptr->udec[idx].udec_full_updatecnt++;
				ptr->udec[idx].udec_full_updatesz += size;
			}
		}
	}
}

int
dc_metrics_update_ioinfo(int is_update, size_t size, struct daos_oclass_attr *ptype,
				int is_part_stripe)
{
	int rc = 0;

	if (is_metrics_enabled == 0)
		D_GOTO(out, rc);

	if (metrics_tls == NULL) {
		rc = dc_metrics_tls_alloc();
		if (rc != 0)
			D_GOTO(out, rc);
	}
	dc_metrics_update_iostats(is_update, size);
	dc_metrics_update_iodist(is_update, size, ptype, is_part_stripe);

out:
	return rc;
}

int
daos_metrics_get_version(int *major, int *minor)
{
	if (is_metrics_enabled == 0)
		return 1;
	*major = DAOS_METRICS_MAJOR_VERSION;
	*minor = DAOS_METRICS_MINOR_VERSION;
	return 0;
}

int
daos_metrics_alloc_cntrsbuf(daos_metrics_ucntrs_t **cntrs)
{
	int rc = 0;

	D_ALLOC_PTR(*cntrs);
	if (*cntrs == NULL) {
		D_ERROR("Failed to allocate memory\n");
		rc = -DER_NOMEM;
	}
	return rc;
}

int
daos_metrics_free_cntrsbuf(daos_metrics_ucntrs_t *cntrs)
{
	free(cntrs);
	return 0;
}

int
daos_metrics_get_cntrs(enum daos_metrics_cntr_grp mc_grp, daos_metrics_ucntrs_t *cntrs)
{
	int rc = 0;

	if (is_metrics_enabled == 0)
		D_GOTO(out, rc = 0);

	switch (mc_grp) {
	case DAOS_METRICS_POOL_RPC_CNTR:
		rc = dc_pool_metrics_get_rpccntrs(&cntrs->u.arc_pool_cntrs);
		if (rc != 0)
			D_ERROR("Failed to obtain pool rpc counters, rc = %d\n", rc);
		else
			cntrs->mc_grp = DAOS_METRICS_POOL_RPC_CNTR;
		break;
	case DAOS_METRICS_CONT_RPC_CNTR:
		rc = dc_cont_metrics_get_rpccntrs(&cntrs->u.arc_cont_cntrs);
		if (rc != 0)
			D_ERROR("Failed to obtain container rpc counters, rc = %d\n", rc);
		else
			cntrs->mc_grp = DAOS_METRICS_CONT_RPC_CNTR;
		break;
	case DAOS_METRICS_OBJ_RPC_CNTR:
		rc = dc_obj_metrics_get_rpccntrs(&cntrs->u.arc_obj_cntrs);
		if (rc != 0)
			D_ERROR("Failed to obtain object rpc objects, rc = %d\n", rc);
		else
			cntrs->mc_grp = DAOS_METRICS_OBJ_RPC_CNTR;
		break;
	default:
		D_ERROR("Invalid argument mc_grp = %d\n", mc_grp);
		rc = -DER_INVAL;
	}
out:
	return rc;
}

int
daos_metrics_alloc_statsbuf(daos_metrics_ustats_t **stats)
{
	int rc = 0;

	D_ALLOC_PTR(*stats);
	if (*stats == NULL) {
		D_ERROR("Failed to allocate memory\n");
		rc = -DER_NOMEM;
	}
	return rc;
}

int
daos_metrics_free_statsbuf(daos_metrics_ustats_t *stats)
{
	free(stats);
	return 0;
}

int
daos_metrics_get_stats(enum daos_metrics_stats_grp ms_grp, daos_metrics_ustats_t *stats)
{
	int rc = 0;
	dc_metrics_tls_data_t *entry;

	if (is_metrics_enabled == 0)
		D_GOTO(out, rc = 0);

	if ((ms_grp != DAOS_METRICS_OBJ_UPDATE_STATS) && (ms_grp != DAOS_METRICS_OBJ_FETCH_STATS)) {
		D_ERROR("Invalid argument ms_grp = %d\n", ms_grp);
		D_GOTO(out, rc = -DER_INVAL);
	}

	D_MUTEX_LOCK(&metrics_tls_lock);
	memset(stats, 0, sizeof(*stats));
	if (ms_grp == DAOS_METRICS_OBJ_UPDATE_STATS) {
		stats->ms_grp = DAOS_METRICS_OBJ_UPDATE_STATS;
		stats->u.st_obj_fetch.st_min = ULONG_MAX;
	} else if (ms_grp == DAOS_METRICS_OBJ_FETCH_STATS) {
		stats->ms_grp = DAOS_METRICS_OBJ_FETCH_STATS;
		stats->u.st_obj_update.st_min = ULONG_MAX;
	}
	d_list_for_each_entry(entry, &metrics_list, list) {
		if (ms_grp == DAOS_METRICS_OBJ_UPDATE_STATS) {
			stats->u.st_obj_update.st_value += entry->update_stat.st_value;
			if (stats->u.st_obj_update.st_min > entry->update_stat.st_min)
				stats->u.st_obj_update.st_min = entry->update_stat.st_min;
			if (stats->u.st_obj_update.st_max < entry->update_stat.st_max)
				stats->u.st_obj_update.st_max = entry->update_stat.st_max;
			stats->u.st_obj_update.st_sum += entry->update_stat.st_sum;
			stats->u.st_obj_update.st_sum_of_squares +=
				entry->update_stat.st_sum_of_squares;
		} else if (ms_grp == DAOS_METRICS_OBJ_FETCH_STATS) {
			stats->u.st_obj_fetch.st_value += entry->fetch_stat.st_value;
			if (stats->u.st_obj_fetch.st_min > entry->fetch_stat.st_min)
				stats->u.st_obj_fetch.st_min = entry->fetch_stat.st_min;
			if (stats->u.st_obj_fetch.st_max < entry->fetch_stat.st_max)
				stats->u.st_obj_fetch.st_max = entry->fetch_stat.st_max;
			stats->u.st_obj_fetch.st_sum += entry->fetch_stat.st_sum;
			stats->u.st_obj_fetch.st_sum_of_squares +=
				entry->fetch_stat.st_sum_of_squares;
		}
	}
	D_MUTEX_UNLOCK(&metrics_tls_lock);
	if (stats->u.st_obj_update.st_min == ULONG_MAX)
		stats->u.st_obj_update.st_min = 0;
	if (stats->u.st_obj_fetch.st_min == ULONG_MAX)
		stats->u.st_obj_fetch.st_min = 0;
out:
	return rc;
}

int
daos_metrics_alloc_distbuf(daos_metrics_udists_t **dists)
{
	int rc = 0;

	D_ALLOC_PTR(*dists);
	if (*dists == NULL) {
		D_ERROR("Failed to allocate memory\n");
		rc = -DER_NOMEM;
	}
	return rc;
}

int
daos_metrics_free_distbuf(daos_metrics_udists_t *dists)
{
	free(dists);
	return 0;
}

int
daos_metrics_get_dist(enum daos_metrics_dist_grp md_grp, daos_metrics_udists_t *dist)
{
	int rc = 0, i;
	dc_metrics_tls_data_t *entry;

	if (is_metrics_enabled == 0)
		D_GOTO(out, rc = 0);

	memset(dist, 0, sizeof(*dist));
	D_MUTEX_LOCK(&metrics_tls_lock);
	switch (md_grp) {
	case DAOS_METRICS_IO_DIST_SZ:
		dist->md_grp = DAOS_METRICS_IO_DIST_SZ;
		d_list_for_each_entry(entry, &metrics_list, list) {
			for (i = 0; i < DAOS_METRICS_DIST_IO_BKT_COUNT; i++) {
				dist->u.md_iosz[i].ids_updatecnt += entry->idsz[i].ids_updatecnt;
				dist->u.md_iosz[i].ids_fetchcnt += entry->idsz[i].ids_fetchcnt;
			}
		}
		break;
	case DAOS_METRICS_UP_DIST_RP:
		dist->md_grp = DAOS_METRICS_UP_DIST_RP;
		d_list_for_each_entry(entry, &metrics_list, list) {
			for (i = 0; i < DAOS_METRICS_DIST_RP_BKT_COUNT; i++) {
				dist->u.md_uprp[i].udrp_updatecnt += entry->udrp[i].udrp_updatecnt;
				dist->u.md_uprp[i].udrp_updatesz += entry->udrp[i].udrp_updatesz;
			}
		}
		break;
	case DAOS_METRICS_UP_DIST_EC:
		dist->md_grp = DAOS_METRICS_UP_DIST_EC;
		d_list_for_each_entry(entry, &metrics_list, list) {
			for (i = 0; i < DAOS_METRICS_DIST_EC_BKT_COUNT; i++) {
				dist->u.md_upec[i].udec_full_updatecnt +=
					entry->udec[i].udec_full_updatecnt;
				dist->u.md_upec[i].udec_full_updatesz +=
					entry->udec[i].udec_full_updatesz;
				dist->u.md_upec[i].udec_part_updatecnt +=
					entry->udec[i].udec_part_updatecnt;
				dist->u.md_upec[i].udec_part_updatesz +=
					entry->udec[i].udec_part_updatesz;
			}
		}
		break;
	default:
		D_ERROR("Invalid argument md_grp = %d\n", md_grp);
		rc = -DER_INVAL;
	}
	D_MUTEX_UNLOCK(&metrics_tls_lock);
out:
	return rc;
}

static int
dump_pool_rpccntrs(FILE *fp)
{
	int rc;
	daos_metrics_ucntrs_t *cntrs = NULL;
	daos_metrics_pool_rpc_cntrs_t *pcntrs;

	rc = daos_metrics_alloc_cntrsbuf(&cntrs);
	if (rc != 0) {
		D_ERROR("Failed to dump pool rpc counters rc = %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = daos_metrics_get_cntrs(DAOS_METRICS_POOL_RPC_CNTR, cntrs);
	if (rc != 0) {
		D_ERROR("Failed to dump pool rpc counters rc = %d\n", rc);
		D_GOTO(alloc_out, rc);
	}

	pcntrs = &cntrs->u.arc_pool_cntrs;

	fprintf(fp, "********************  Dumping Pool RPC Counters *********************\n");
	fprintf(fp, "%-16s\t%12s\t%12s\t%12s\n", "Name", "Inflight", "Success", "Failure");
	fprintf(fp, "%-16s\t%12lu\t%12lu\t%12lu\n", "pool connect",
			pcntrs->prc_connect_cnt.mc_inflight,
			pcntrs->prc_connect_cnt.mc_success, pcntrs->prc_connect_cnt.mc_failure);
	fprintf(fp, "%-16s\t%12lu\t%12lu\t%12lu\n", "pool disconnect",
			pcntrs->prc_disconnect_cnt.mc_inflight,
			pcntrs->prc_disconnect_cnt.mc_success,
			pcntrs->prc_disconnect_cnt.mc_failure);
	fprintf(fp, "%-16s\t%12lu\t%12lu\t%12lu\n", "pool attr(get/set)",
			pcntrs->prc_attr_cnt.mc_inflight,
			pcntrs->prc_attr_cnt.mc_success, pcntrs->prc_attr_cnt.mc_failure);
	fprintf(fp, "%-16s\t%12lu\t%12lu\t%12lu\n", "pool query", pcntrs->prc_query_cnt.mc_inflight,
			pcntrs->prc_query_cnt.mc_success, pcntrs->prc_query_cnt.mc_failure);
	fflush(fp);
alloc_out:
	daos_metrics_free_cntrsbuf(cntrs);
out:
	return rc;
}

static int
dump_cont_rpccntrs(FILE *fp)
{
	int rc;
	daos_metrics_ucntrs_t *cntrs;
	daos_metrics_cont_rpc_cntrs_t *ccntrs;

	rc = daos_metrics_alloc_cntrsbuf(&cntrs);
	if (rc != 0) {
		D_ERROR("Failed to dump cont rpc counters rc = %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = daos_metrics_get_cntrs(DAOS_METRICS_CONT_RPC_CNTR, cntrs);
	if (rc != 0) {
		D_ERROR("Failed to dump cont rpc counters rc = %d\n", rc);
		D_GOTO(alloc_out, rc);
	}

	ccntrs = &cntrs->u.arc_cont_cntrs;

	fprintf(fp, "******************  Dumping Container RPC Counters *******************\n");
	fprintf(fp, "%-16s\t%12s\t%12s\t%12s\n", "Name", "Inflight", "Success", "Failure");
	fprintf(fp, "%-16s\t%12lu\t%12lu\t%12lu\n", "cont create",
			ccntrs->crc_create_cnt.mc_inflight,
			ccntrs->crc_create_cnt.mc_success, ccntrs->crc_create_cnt.mc_failure);
	fprintf(fp, "%-16s\t%12lu\t%12lu\t%12lu\n", "cont destroy",
			ccntrs->crc_destroy_cnt.mc_inflight, ccntrs->crc_destroy_cnt.mc_success,
			ccntrs->crc_destroy_cnt.mc_failure);
	fprintf(fp, "%-16s\t%12lu\t%12lu\t%12lu\n", "cont open", ccntrs->crc_open_cnt.mc_inflight,
			ccntrs->crc_open_cnt.mc_success, ccntrs->crc_open_cnt.mc_failure);
	fprintf(fp, "%-16s\t%12lu\t%12lu\t%12lu\n", "cont close",
			ccntrs->crc_close_cnt.mc_inflight, ccntrs->crc_close_cnt.mc_success,
			ccntrs->crc_close_cnt.mc_failure);
	fprintf(fp, "%-16s\t%12lu\t%12lu\t%12lu\n", "cont snapshot",
			ccntrs->crc_snapshot_cnt.mc_inflight, ccntrs->crc_snapshot_cnt.mc_success,
			ccntrs->crc_snapshot_cnt.mc_failure);
	fprintf(fp, "%-16s\t%12lu\t%12lu\t%12lu\n", "cont snaplist",
			ccntrs->crc_snaplist_cnt.mc_inflight, ccntrs->crc_snaplist_cnt.mc_success,
			ccntrs->crc_snaplist_cnt.mc_failure);
	fprintf(fp, "%-16s\t%12lu\t%12lu\t%12lu\n", "cont snapdestroy",
			ccntrs->crc_snapdel_cnt.mc_inflight, ccntrs->crc_snapdel_cnt.mc_success,
			ccntrs->crc_snapdel_cnt.mc_failure);
	fprintf(fp, "%-16s\t%12lu\t%12lu\t%12lu\n", "cont attr", ccntrs->crc_attr_cnt.mc_inflight,
			ccntrs->crc_attr_cnt.mc_success, ccntrs->crc_attr_cnt.mc_failure);
	fprintf(fp, "%-16s\t%12lu\t%12lu\t%12lu\n", "cont acl", ccntrs->crc_acl_cnt.mc_inflight,
			ccntrs->crc_acl_cnt.mc_success, ccntrs->crc_acl_cnt.mc_failure);
	fprintf(fp, "%-16s\t%12lu\t%12lu\t%12lu\n", "cont prop", ccntrs->crc_prop_cnt.mc_inflight,
			ccntrs->crc_prop_cnt.mc_success, ccntrs->crc_prop_cnt.mc_failure);
	fprintf(fp, "%-16s\t%12lu\t%12lu\t%12lu\n", "cont query",
			ccntrs->crc_query_cnt.mc_inflight, ccntrs->crc_query_cnt.mc_success,
			ccntrs->crc_query_cnt.mc_failure);
	fprintf(fp, "%-16s\t%12lu\t%12lu\t%12lu\n", "cont oidalloc",
			ccntrs->crc_oidalloc_cnt.mc_inflight, ccntrs->crc_oidalloc_cnt.mc_success,
			ccntrs->crc_oidalloc_cnt.mc_failure);
	fprintf(fp, "%-16s\t%12lu\t%12lu\t%12lu\n", "cont aggregate",
			ccntrs->crc_aggregate_cnt.mc_inflight,
			ccntrs->crc_aggregate_cnt.mc_success, ccntrs->crc_aggregate_cnt.mc_failure);
	fflush(fp);
alloc_out:
	daos_metrics_free_cntrsbuf(cntrs);
out:
	return rc;
}

static int
dump_obj_rpccntrs(FILE *fp)
{
	int rc;
	daos_metrics_ucntrs_t *cntrs;
	daos_metrics_obj_rpc_cntrs_t *ocntrs;

	rc = daos_metrics_alloc_cntrsbuf(&cntrs);
	if (rc != 0) {
		D_ERROR("Failed to dump obj rpc counters rc = %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = daos_metrics_get_cntrs(DAOS_METRICS_OBJ_RPC_CNTR, cntrs);
	if (rc != 0) {
		D_ERROR("Failed to dump obj rpc counters rc = %d\n", rc);
		D_GOTO(alloc_out, rc);
	}

	ocntrs = &cntrs->u.arc_obj_cntrs;

	fprintf(fp, "*******************  Dumping Object RPC Counters ********************\n");
	fprintf(fp, "%-16s\t%12s\t%12s\t%12s\n", "Name", "Inflight", "Success", "Failure");
	fprintf(fp, "%-16s\t%12lu\t%12lu\t%12lu\n", "obj update",
			ocntrs->orc_update_cnt.mc_inflight,
			ocntrs->orc_update_cnt.mc_success, ocntrs->orc_update_cnt.mc_failure);
	fprintf(fp, "%-16s\t%12lu\t%12lu\t%12lu\n", "obj fetch",
			ocntrs->orc_fetch_cnt.mc_inflight,
			ocntrs->orc_fetch_cnt.mc_success, ocntrs->orc_fetch_cnt.mc_failure);
	fprintf(fp, "%-16s\t%12lu\t%12lu\t%12lu\n", "obj enum dkey",
			ocntrs->orc_dkey_enum_cnt.mc_inflight,
			ocntrs->orc_dkey_enum_cnt.mc_success, ocntrs->orc_dkey_enum_cnt.mc_failure);
	fprintf(fp, "%-16s\t%12lu\t%12lu\t%12lu\n", "obj enum akey",
			ocntrs->orc_akey_enum_cnt.mc_inflight,
			ocntrs->orc_akey_enum_cnt.mc_success, ocntrs->orc_akey_enum_cnt.mc_failure);
	fprintf(fp, "%-16s\t%12lu\t%12lu\t%12lu\n", "obj enum recx",
			ocntrs->orc_recx_enum_cnt.mc_inflight, ocntrs->orc_recx_enum_cnt.mc_success,
			ocntrs->orc_recx_enum_cnt.mc_failure);
	fprintf(fp, "%-16s\t%12lu\t%12lu\t%12lu\n", "obj enum obj",
			ocntrs->orc_obj_enum_cnt.mc_inflight,
			ocntrs->orc_obj_enum_cnt.mc_success, ocntrs->orc_obj_enum_cnt.mc_failure);
	fprintf(fp, "%-16s\t%12lu\t%12lu\t%12lu\n", "obj punch obj",
			ocntrs->orc_obj_punch_cnt.mc_inflight, ocntrs->orc_obj_punch_cnt.mc_success,
			ocntrs->orc_obj_punch_cnt.mc_failure);
	fprintf(fp, "%-16s\t%12lu\t%12lu\t%12lu\n", "obj punch dkeys",
			ocntrs->orc_dkey_punch_cnt.mc_inflight,
			ocntrs->orc_dkey_punch_cnt.mc_success,
			ocntrs->orc_dkey_punch_cnt.mc_failure);
	fprintf(fp, "%-16s\t%12lu\t%12lu\t%12lu\n", "obj punch akeys",
			ocntrs->orc_akey_punch_cnt.mc_inflight,
			ocntrs->orc_akey_punch_cnt.mc_success,
			ocntrs->orc_akey_punch_cnt.mc_failure);
	fprintf(fp, "%-16s\t%12lu\t%12lu\t%12lu\n", "obj query keys",
			ocntrs->orc_querykey_cnt.mc_inflight, ocntrs->orc_querykey_cnt.mc_success,
			ocntrs->orc_querykey_cnt.mc_failure);
	fprintf(fp, "%-16s\t%12lu\t%12lu\t%12lu\n", "obj sync", ocntrs->orc_sync_cnt.mc_inflight,
			ocntrs->orc_sync_cnt.mc_success, ocntrs->orc_sync_cnt.mc_failure);
	fprintf(fp, "%-16s\t%12lu\t%12lu\t%12lu\n", "obj cpd", ocntrs->orc_cpd_cnt.mc_inflight,
			ocntrs->orc_cpd_cnt.mc_success, ocntrs->orc_cpd_cnt.mc_failure);
	fflush(fp);
alloc_out:
	daos_metrics_free_cntrsbuf(cntrs);
out:
	return rc;
}

static int
dump_obj_stats(FILE *fp)
{
	int rc;
	daos_metrics_ustats_t *stats;

	rc = daos_metrics_alloc_statsbuf(&stats);
	if (rc != 0) {
		D_ERROR("Failed to dump iostats rc = %d\n", rc);
		D_GOTO(out, rc);
	}

	fprintf(fp, "***************  Dumping Object IO Stats ***************************\n");
	fprintf(fp, "%-10s\t%12s\t%16s\t%20s\t%12s\t%12s\n", "Name", "Count", "Sum Size",
			"Sum of Sqrs Size", "Min", "Max");

	rc = daos_metrics_get_stats(DAOS_METRICS_OBJ_UPDATE_STATS, stats);
	if (rc != 0) {
		D_ERROR("Failed to dump iostats rc = %d\n", rc);
		D_GOTO(alloc_out, rc);
	}
	fprintf(fp, "%-10s\t%12lu\t%16lu\t%20lu\t%12lu\t%12lu\n", "update",
			stats->u.st_obj_update.st_value, stats->u.st_obj_update.st_sum,
			stats->u.st_obj_update.st_sum_of_squares, stats->u.st_obj_update.st_min,
			stats->u.st_obj_update.st_max);

	rc = daos_metrics_get_stats(DAOS_METRICS_OBJ_FETCH_STATS, stats);
	if (rc != 0) {
		D_ERROR("Failed to dump iostats rc = %d\n", rc);
		D_GOTO(alloc_out, rc);
	}
	fprintf(fp, "%-10s\t%12lu\t%16lu\t%20lu\t%12lu\t%12lu\n", "fetch",
			stats->u.st_obj_fetch.st_value, stats->u.st_obj_fetch.st_sum,
			stats->u.st_obj_fetch.st_sum_of_squares, stats->u.st_obj_fetch.st_min,
			stats->u.st_obj_fetch.st_max);
	fflush(fp);
alloc_out:
	daos_metrics_free_statsbuf(stats);
out:
	return rc;
}

static const char * const distname_bysize[] = {
	"IO_0_1K",
	"IO_1K_2K",
	"IO_2K_4K",
	"IO_4K_8K",
	"IO_8K_16K",
	"IO_16K_32K",
	"IO_32K_64K",
	"IO_64K_128K",
	"IO_128K_256K",
	"IO_256K_512K",
	"IO_512K_1M",
	"IO_1M_2M",
	"IO_2M_4M",
	"IO_4M_INF",
};

static int
dump_obj_iodist_bysize(FILE *fp)
{
	int rc, i;
	daos_metrics_udists_t *dist;

	rc = daos_metrics_alloc_distbuf(&dist);
	if (rc != 0) {
		D_ERROR("Failed to dump io distribution by size rc = %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = daos_metrics_get_dist(DAOS_METRICS_IO_DIST_SZ, dist);
	if (rc != 0) {
		D_ERROR("Failed to dump io distribution by size, rc = %d\n", rc);
		D_GOTO(alloc_out, rc);
	}

	fprintf(fp, "***************  Dumping i/o Distribution by Size ******************\n");
	fprintf(fp, "%-16s\t%12s\t%12s\n", "Name", "update cnt", "fetch cnt");
	for (i = 0; i < DAOS_METRICS_DIST_IO_BKT_COUNT; i++) {
		fprintf(fp, "%-16s\t%12lu\t%12lu\n", distname_bysize[i],
				dist->u.md_iosz[i].ids_updatecnt,
				dist->u.md_iosz[i].ids_fetchcnt);
	}
	fflush(fp);
alloc_out:
	daos_metrics_free_distbuf(dist);
out:
	return rc;
}

const char * const distname_rp[] = {
	"NO_RP",
	"RP2",
	"RP3",
	"RP4",
	"RP6",
	"RP8",
	"RP12",
	"RP16",
	"RP24",
	"RP32",
	"RP48",
	"RP64",
	"RP128",
	"RPU",
};

const char * const distname_ec[] = {
	"IO_EC2P1",
	"IO_EC2P2",
	"IO_EC4P1",
	"IO_EC4P2",
	"IO_EC8P1",
	"IO_EC8P2",
	"IO_EC16P1",
	"IO_EC16P2",
	"IO_ECU",
};

static int
dump_obj_updist_rp(FILE *fp)
{
	int rc, i;
	daos_metrics_udists_t *dist;

	rc = daos_metrics_alloc_distbuf(&dist);
	if (rc != 0) {
		D_ERROR("Failed to dump io distribution by size rc = %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = daos_metrics_get_dist(DAOS_METRICS_UP_DIST_RP, dist);
	if (rc != 0) {
		D_ERROR("Failed to dump io distribution by size, rc = %d\n", rc);
		D_GOTO(alloc_out, rc);
	}

	fprintf(fp, "**********  Dumping update call Distribution for RP ***************\n");
	fprintf(fp, "%-16s\t%12s\t%12s\n", "Name", "update cnt", "size");
	for (i = 0; i < DAOS_METRICS_DIST_RP_BKT_COUNT; i++) {
		fprintf(fp, "%-16s\t%12lu\t%12lu\n", distname_rp[i],
				dist->u.md_uprp[i].udrp_updatecnt,
				dist->u.md_uprp[i].udrp_updatesz);
	}
	fflush(fp);
alloc_out:
	daos_metrics_free_distbuf(dist);
out:
	return rc;
}

static int
dump_obj_updist_ec(FILE *fp)
{
	int rc, i;
	daos_metrics_udists_t *dist;

	rc = daos_metrics_alloc_distbuf(&dist);
	if (rc != 0) {
		D_ERROR("Failed to dump io distribution by size rc = %d\n", rc);
		D_GOTO(out, rc);
	}

	rc = daos_metrics_get_dist(DAOS_METRICS_UP_DIST_EC, dist);
	if (rc != 0) {
		D_ERROR("Failed to dump io distribution by size, rc = %d\n", rc);
		D_GOTO(alloc_out, rc);
	}

	fprintf(fp, "**********  Dumping update call Distribution for EC ***************\n");
	fprintf(fp, "%-16s\t%12s\t%12s\t%12s\t%12s\n", "Name", "fstripe/sng cnt", "size",
			"pstripe cnt", "size");
	for (i = 0; i < DAOS_METRICS_DIST_EC_BKT_COUNT; i++) {
		fprintf(fp, "%-16s\t%12lu\t%12lu\t%12lu\t%12lu\n", distname_ec[i],
				dist->u.md_upec[i].udec_full_updatecnt,
				dist->u.md_upec[i].udec_full_updatesz,
				dist->u.md_upec[i].udec_part_updatecnt,
				dist->u.md_upec[i].udec_part_updatesz);
	}
	fflush(fp);
alloc_out:
	daos_metrics_free_distbuf(dist);
out:
	return rc;
}

int
daos_metrics_dump(FILE *fp)
{
	int rc = 0;

	if (is_metrics_enabled == 0)
		D_GOTO(out, rc = 1);
	rc = dump_pool_rpccntrs(fp);
	if (rc != 0)
		D_GOTO(out, rc);
	rc = dump_cont_rpccntrs(fp);
	if (rc != 0)
		D_GOTO(out, rc);
	rc = dump_obj_rpccntrs(fp);
	if (rc != 0)
		D_GOTO(out, rc);
	rc = dump_obj_stats(fp);
	if (rc != 0)
		D_GOTO(out, rc);
	rc = dump_obj_iodist_bysize(fp);
	if (rc != 0)
		D_GOTO(out, rc);
	rc = dump_obj_updist_rp(fp);
	if (rc != 0)
		D_GOTO(out, rc);
	rc = dump_obj_updist_ec(fp);
	if (rc != 0)
		D_GOTO(out, rc);
out:
	return rc;
}

int
daos_metrics_reset()
{
	int rc = 0;

	if (is_metrics_enabled == 0)
		D_GOTO(out, rc = 1);
	rc = dc_pool_metrics_reset();
	if (rc != 0)
		D_GOTO(out, rc);
	rc = dc_cont_metrics_reset();
	if (rc != 0)
		D_GOTO(out, rc);
	rc = dc_obj_metrics_reset();
	if (rc != 0)
		D_GOTO(out, rc);

	dc_metrics_reset_tls_data();
out:
	return rc;
}
