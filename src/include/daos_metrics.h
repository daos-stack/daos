/*
 * (C) Copyright 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * DAOS CLIENT METRICS API
 */

#ifndef __DAOS_METRICS_H__
#define __DAOS_METRICS_H__

#include <stdio.h>

#if defined(__cplusplus)
extern "C" {
#endif

/** DAOS Metrics Major Version */
#define DAOS_METRICS_MAJOR_VERSION	0x1
/** DAOS Metrics Minor Version */
#define DAOS_METRICS_MINOR_VERSION	0x0

/** counters */
typedef struct {
	/** Inprogress */
	unsigned long mc_inflight;
	/** Successfully completed */
	unsigned long mc_success;
	/** Completed with failure */
	unsigned long mc_failure;
} daos_metrics_cntr_t;

/** Counter groups */
enum daos_metrics_cntr_grp {
	DAOS_METRICS_POOL_RPC_CNTR = 1,
	DAOS_METRICS_CONT_RPC_CNTR = 2,
	DAOS_METRICS_OBJ_RPC_CNTR  = 3,
};

/** RPC counters associated with DAOS Pool */
typedef struct {
	/** Counter for pool connect calls */
	daos_metrics_cntr_t prc_connect_cnt;
	/** Counter for pool disconnect calls */
	daos_metrics_cntr_t prc_disconnect_cnt;
	/** Counter for pool attribute related calls */
	daos_metrics_cntr_t prc_attr_cnt;
	/** Counter for pool query calls */
	daos_metrics_cntr_t prc_query_cnt;
} daos_metrics_pool_rpc_cntrs_t;

/** RPC counters associated with DAOS Container  */
typedef struct {
	/** Counter for container create rpc calls */
	daos_metrics_cntr_t crc_create_cnt;
	/** Counter for container destroy rpc calls */
	daos_metrics_cntr_t crc_destroy_cnt;
	/** Counter for container open rpc calls */
	daos_metrics_cntr_t crc_open_cnt;
	/** Counter for container close rpc calls */
	daos_metrics_cntr_t crc_close_cnt;
	/** Counter for container snapshot create rpc calls */
	daos_metrics_cntr_t crc_snapshot_cnt;
	/** Counter for container snapshot list rpc calls */
	daos_metrics_cntr_t crc_snaplist_cnt;
	/** Counter for container snapshot destroy rpc calls */
	daos_metrics_cntr_t crc_snapdel_cnt;
	/** Counter for container attribute rpc calls */
	daos_metrics_cntr_t crc_attr_cnt;
	/** Counter for container acl rpc calls */
	daos_metrics_cntr_t crc_acl_cnt;
	/** Counter for container prop rpc calls */
	daos_metrics_cntr_t crc_prop_cnt;
	/** Counter for container query rpc calls */
	daos_metrics_cntr_t crc_query_cnt;
	/** Counter for container oidalloc rpc calls */
	daos_metrics_cntr_t crc_oidalloc_cnt;
	/** Counter for container aggregate rpc calls */
	daos_metrics_cntr_t crc_aggregate_cnt;
} daos_metrics_cont_rpc_cntrs_t;

/** RPC counters associated with DAOS Objects  */
typedef struct {
	/** Counter for object update rpc calls */
	daos_metrics_cntr_t orc_update_cnt;
	/** Counter for object fetch rpc calls */
	daos_metrics_cntr_t orc_fetch_cnt;
	/** Counter for object punch rpc calls */
	daos_metrics_cntr_t orc_obj_punch_cnt;
	/** Counter for dkey punch rpc calls */
	daos_metrics_cntr_t orc_dkey_punch_cnt;
	/** Counter for akey punch rpc calls */
	daos_metrics_cntr_t orc_akey_punch_cnt;
	/** Counter for object list rpc calls */
	daos_metrics_cntr_t orc_obj_enum_cnt;
	/** Counter for dkey enumerate rpc calls */
	daos_metrics_cntr_t orc_dkey_enum_cnt;
	/** Counter for akey enumerate rpc calls */
	daos_metrics_cntr_t orc_akey_enum_cnt;
	/** Counter for recx enumerate rpc calls */
	daos_metrics_cntr_t orc_recx_enum_cnt;
	/** Counter for obj sync rpc calls */
	daos_metrics_cntr_t orc_sync_cnt;
	/** Counter for obj query key rpc calls */
	daos_metrics_cntr_t orc_querykey_cnt;
	/** Counter for obj compound(tx) rpc calls */
	daos_metrics_cntr_t orc_cpd_cnt;
} daos_metrics_obj_rpc_cntrs_t;

/** Structure to be used to obtain the daos client counters metrics */
typedef struct {
	/** Counter metric group */
	enum daos_metrics_cntr_grp mc_grp;
	unsigned int		    resrv;
	union {
		/** mc_grp == DAOS_METRICS_POOL_RPC_CNTR **/
		daos_metrics_pool_rpc_cntrs_t arc_pool_cntrs;
		/** mc_grp == DAOS_METRICS_CONT_RPC_CNTR **/
		daos_metrics_cont_rpc_cntrs_t arc_cont_cntrs;
		/** mc_grp == DAOS_METRICS_OBJ_RPC_CNTR **/
		daos_metrics_obj_rpc_cntrs_t  arc_obj_cntrs;
	} u;
} daos_metrics_ucntrs_t;

/** Stats metrics groups */
enum daos_metrics_stats_grp {
	DAOS_METRICS_OBJ_UPDATE_STATS,
	DAOS_METRICS_OBJ_FETCH_STATS,
};

/** Stats metric */
typedef struct {
	unsigned long st_value;
	unsigned long st_min;
	unsigned long st_max;
	unsigned long st_sum;
	unsigned long st_sum_of_squares;
} daos_metrics_stat_t;

/** Structure to be used to obtain the daos client stats metrics */
typedef struct {
	/** Stats metric group */
	enum daos_metrics_stats_grp ms_grp;
	unsigned int resrv;
	union {
		/**
		 * ms_grp == DAOS_METRICS_OBJ_UPDATE_STATS
		 * i/o stat for object update ops
		 */
		daos_metrics_stat_t st_obj_update;
		/**
		 * ms_grp == DAOS_METRICS_OBJ_UPDATE_STATS
		 * i/o stat for object fetch ops
		 */
		daos_metrics_stat_t st_obj_fetch;
	} u;
} daos_metrics_ustats_t;

/* Metrics for i/o distribution */

/**
 * Distribution ids for fetch/update rpc calls based on size.
 * Field DM_IO_X_Y indicates calls with size greater than or equal to X
 * and less than Y.
 */
enum {
	DAOS_METRICS_DIST_IO_0_1K = 0,
	DAOS_METRICS_DIST_IO_1K_2K,
	DAOS_METRICS_DIST_IO_2K_4K,
	DAOS_METRICS_DIST_IO_4K_8K,
	DAOS_METRICS_DIST_IO_8K_16K,
	DAOS_METRICS_DIST_IO_16K_32K,
	DAOS_METRICS_DIST_IO_32K_64K,
	DAOS_METRICS_DIST_IO_64K_128K,
	DAOS_METRICS_DIST_IO_128K_256K,
	DAOS_METRICS_DIST_IO_256K_512K,
	DAOS_METRICS_DIST_IO_512K_1M,
	DAOS_METRICS_DIST_IO_1M_2M,
	DAOS_METRICS_DIST_IO_2M_4M,
	DAOS_METRICS_DIST_IO_4M_INF,
	DAOS_METRICS_DIST_IO_BKT_COUNT,
};

/** Distribution of i/o rpc calls based on size */
typedef struct {
	/** Count of update rpc calls */
	unsigned long	ids_updatecnt;
	/** Count of fetch rpc calls */
	unsigned long	ids_fetchcnt;
} daos_metrics_iodist_sz_t;

/** Distribution ids for RP based update rpc calls. */
enum {
	DAOS_METRICS_DIST_NORP = 0,
	DAOS_METRICS_DIST_RP2,
	DAOS_METRICS_DIST_RP3,
	DAOS_METRICS_DIST_RP4,
	DAOS_METRICS_DIST_RP6,
	DAOS_METRICS_DIST_RP8,
	DAOS_METRICS_DIST_RP12,
	DAOS_METRICS_DIST_RP16,
	DAOS_METRICS_DIST_RP24,
	DAOS_METRICS_DIST_RP32,
	DAOS_METRICS_DIST_RP48,
	DAOS_METRICS_DIST_RP64,
	DAOS_METRICS_DIST_RP128,
	/** User defined EC settings */
	DAOS_METRICS_DIST_RPU,
	DAOS_METRICS_DIST_RP_BKT_COUNT,
};

/** Distribution stats for RP based update calls */
typedef struct {
	/** Count of update calls */
	unsigned long udrp_updatecnt;
	/** Total bytes transferred as part of update */
	unsigned long udrp_updatesz;
} daos_metrics_updist_rp_t;

/** Distribution ids for EC based update rpc calls. */
enum {
	DAOS_METRICS_DIST_EC2P1 = 0,
	DAOS_METRICS_DIST_EC2P2,
	DAOS_METRICS_DIST_EC4P1,
	DAOS_METRICS_DIST_EC4P2,
	DAOS_METRICS_DIST_EC8P1,
	DAOS_METRICS_DIST_EC8P2,
	DAOS_METRICS_DIST_EC16P1,
	DAOS_METRICS_DIST_EC16P2,
	/** User defined EC settings */
	DAOS_METRICS_DIST_ECU,
	DAOS_METRICS_DIST_EC_BKT_COUNT,
};

/** Distribution stats for EC based update calls */
typedef struct {
	/** Count of update calls that do not require EC aggregation */
	unsigned long udec_full_updatecnt;
	/** Total bytes transferred as part of full update */
	unsigned long udec_full_updatesz;
	/** Count of update that requires EC aggregation */
	unsigned long udec_part_updatecnt;
	/** Total bytes transferred as part of partial update */
	unsigned long udec_part_updatesz;
} daos_metrics_updist_ec_t;

/** Distribution metrics groups */
enum daos_metrics_dist_grp {
	DAOS_METRICS_IO_DIST_SZ,
	DAOS_METRICS_UP_DIST_RP,
	DAOS_METRICS_UP_DIST_EC,
};

/** DAOS client distribution metrics */
typedef struct {
	/** Distribution metric group id */
	enum daos_metrics_dist_grp md_grp;
	unsigned int resrv;
	union {
		/** md_grp == DAOS_METRICS_IO_DIST_SZ */
		daos_metrics_iodist_sz_t md_iosz[DAOS_METRICS_DIST_IO_BKT_COUNT];
		/** md_grp == DAOS_METRICS_UP_DIST_RP */
		daos_metrics_updist_rp_t md_uprp[DAOS_METRICS_DIST_RP_BKT_COUNT];
		/** md_grp == DAOS_METRICS_UP_DIST_EC */
		daos_metrics_updist_ec_t md_upec[DAOS_METRICS_DIST_EC_BKT_COUNT];
	} u;
} daos_metrics_udists_t;

/**
 * Returns the daos metrics version in use by the daos library.
 * The library is compatible if the major version number returned matches
 * DAOS_METRICS_MAJOR_VERSION and minor version is greater than or equal to
 * DAOS_METRICS_MINOR_VERSION.
 *
 * \param[out]	major	Major version number
 * \param[out]	minor	Minor version number
 *
 * \return		0 if metrics is enabled
 *			1 if metrics is disabled.
 */
int
daos_metrics_get_version(int *major, int *minor);

/**
 * Allocates internal buffer to hold the counter metrics. This buffer is used as
 * argument to daos_metrics_get_cntrs().
 *
 * \param[out]	cntrs	Return pointer for the internal buffer.
 *
 * \return		0 success, -ve on failure.
 */
int
daos_metrics_alloc_cntrsbuf(daos_metrics_ucntrs_t **cntrs);

/**
 * Returns the daos client counters metrics for a given type.
 *
 * \param[in]	mc_grp	Group id of the counter metric group to be populated.
 *
 * \param[in,out]
 *		cntrs	Pointer to structure to be populated based on the type.
 *			For max compatibility, the structure pointed to by cntrs should be
 *			allocated using	daos_metrics_alloc_cntrsbuf().
 *
 * \return		0 success
 *			1 if metrics is disabled
 *			-ve on failure.
 */
int
daos_metrics_get_cntrs(enum daos_metrics_cntr_grp mc_grp, daos_metrics_ucntrs_t *cntrs);

/**
 * Frees the internal buffer pointed to by \a cntrs.
 *
 * \param[in]	cntrs	metrics pointer passed to the function
 *			daos_metrics_alloc_cntrsbuf().
 *
 * \return		0 success, -ve on failure.
 */
int
daos_metrics_free_cntrsbuf(daos_metrics_ucntrs_t *cntrs);

/**
 * Allocates internal buffer to hold the stats metrics. This buffer is used as
 * argument to daos_metrics_get_stats().
 *
 * \param[out]	stats	Return pointer for the internal buffer.
 *
 * \return		0 success, -ve on failure.
 */
int
daos_metrics_alloc_statsbuf(daos_metrics_ustats_t **stats);

/**
 * Returns the daos client stats metrics for a given type.
 *
 * \param[in]	ms_grp	Group id  of the stats metric group to be populated.
 *
 * \param[in,out]
 *		stats	Pointer to structure to be populated based on the type.
 *			For max compatibility, the structure pointed to by dist should be
 *			allocated using	daos_metrics_alloc_statsbuf().
 *
 * \return		0 success
 *			1 if metrics is disabled
 *			-ve on failure.
 */
int
daos_metrics_get_stats(enum daos_metrics_stats_grp ms_grp, daos_metrics_ustats_t *stats);

/**
 * Frees the internal buffer pointed to by \a stats.
 *
 * \param[in]	stats	metrics pointer passed to the function
 *			daos_metrics_alloc_statsbuf().
 *
 * \return		0 success, -ve on failure.
 */
int
daos_metrics_free_statsbuf(daos_metrics_ustats_t *stats);

/**
 * Allocates internal buffer to hold the distribution metrics. This buffer is used as
 * argument to daos_metrics_get_dist().
 *
 * \param[out]	dist	Return pointer for the internal buffer.
 *
 * \return		0 success, -ve on failure.
 */
int
daos_metrics_alloc_distbuf(daos_metrics_udists_t **dist);

/**
 * Returns the daos client distribution metrics for a given type.
 *
 * \param[in]	md_grp	Group id of the distribution metric group to be populated.
 *
 * \param[in,out]
 *		dist	Pointer to structure to be populated based on the type.
 *			For max compatibility, the structure pointed to by dist should be
 *			allocated using	daos_metrics_alloc_distbuf().
 *
 * \return		0 success
 *			1 if metrics is disabled
 *			-ve on failure.
 */
int
daos_metrics_get_dist(enum daos_metrics_dist_grp md_grp, daos_metrics_udists_t *dist);

/**
 * Frees the internal buffer pointed to by \a dist.
 *
 * \param[in]	dist	metrics pointer passed to the function daos_metrics_alloc_distbuf().
 *
 * \return		0 success,
 *			-ve on failure.
 */
int
daos_metrics_free_distbuf(daos_metrics_udists_t *dist);

/**
 * Clears/Resets the all internal metrics data associated with the client.
 * This routine is not fully automic and hence should be called at safe points.
 *
 * \return		0 if success and metrics is enabled on client
 *			1 if metrics is disabled on client
 *			-ve on failure
 */
int
daos_metrics_reset();

/**
 * Dump the metrics to the file pointed to by the file pointer \a fp.
 *
 * \param[in]	fp	file pointer.
 *
 * \return		0 if metrics is enabled
 *			1 if metrics is disabled
 *			-ve on failure.
 */
int
daos_metrics_dump(FILE *fp);

#if defined(__cplusplus)
}
#endif

#endif /* __DAOS_METRICS_H__ */
