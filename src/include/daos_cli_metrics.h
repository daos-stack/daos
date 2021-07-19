/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * DAOS CLIENT METRICS API
 */

#ifndef __DAOS_CLI_METRICS_H__
#define __DAOS_CLI_METRICS_H__

#if defined(__cplusplus)
extern "C" {
#endif

/** DAOS Metrics Major and Minor Version */
#define DAOS_CLI_METRICS_MAJOR_VERSION	0x1
#define DAOS_CLI_METRICS_MINOR_VERSION	0x0

/** Basic Counter Metric type */
typedef	unsigned long	dm_cntr_t;

/** rpc counters */
#ifdef  RPC_SIMPLE_METRICS
typedef dm_rpc_cntr_t dm_cntr_t;
#else
typedef struct dm_rpc_cntr {
	/** Count of rpc initiated so far */
	dm_cntr_t initiated;
	/** Count of successfully completed rpc calls */
	dm_cntr_t success;
	/** Count of failed rpc calls */
	dm_cntr_t failure;
} dm_rpc_cntr_t;
#endif

/** rpc counter types */
enum dm_rpc_cntr_type {
	DM_POOL_RPC	= 0x001,
	DM_CONT_RPC	= 0x002,
	DM_OBJ_RPC	= 0x004,
};

/** rpc counters associated with DAOS Pool  */
typedef struct {
	/** Counter for all pool related rpc calls */
	dm_rpc_cntr_t pool_op_cnt;
	/** Counter for pool connect calls */
	dm_rpc_cntr_t pool_connect_cnt;
	/** Counter for pool disconnect calls */
	dm_rpc_cntr_t pool_disconnect_cnt;
	/** Counter for pool attribute related calls */
	dm_rpc_cntr_t pool_attr_cnt; 
	/** Counter for pool query calls */
	dm_rpc_cntr_t pool_query_cnt;
} dm_pool_rpc_cntrs_t;

/** rpc counters associated with DAOS Container  */
typedef struct {
	/** Counter for all container related rpc calls */
	dm_rpc_cntr_t cont_op_cnt;
	/** Counter for container create rpc calls */
	dm_rpc_cntr_t cont_create_cnt;
	/** Counter for container destroy rpc calls */
	dm_rpc_cntr_t cont_destroy_cnt;
	/** Counter for container open rpc calls */
	dm_rpc_cntr_t cont_open_cnt;
	/** Counter for container close rpc calls */
	dm_rpc_cntr_t cont_close_cnt;
	/** Counter for container snapshot create rpc calls */
	dm_rpc_cntr_t cont_snapshot_cnt;
	/** Counter for container snapshot destroy rpc calls */
	dm_rpc_cntr_t cont_snapdel_cnt;
	/** Counter for container acl, attribute, prop rpc calls */
	dm_rpc_cntr_t cont_attr_cnt;
	/** Counter for container query rpc calls */
	dm_rpc_cntr_t cont_query_cnt;
	/** Counter for container oidalloc rpc calls */
	dm_rpc_cntr_t cont_oidalloc_cnt;
	/** Counter for container aggregate rpc calls */
	dm_rpc_cntr_t cont_aggregate_cnt;
} dm_cont_rpc_cntrs_t;

/** rpc counters associated with DAOS Objects  */
typedef struct {
	/** Counter for all object related rpc calls */
	dm_rpc_cntr_t obj_op_cnt;
	/** Counter for object update rpc calls */
	dm_rpc_cntr_t obj_update_cnt;
	/** Counter for object fetch rpc calls */
	dm_rpc_cntr_t obj_fetch_cnt;
	/** Counter for object punch rpc calls */
	dm_rpc_cntr_t obj_punch_cnt;
	/** Counter for dkey punch rpc calls */
	dm_rpc_cntr_t dkey_punch_cnt;
	/** Counter for akey punch rpc calls */
	dm_rpc_cntr_t akey_punch_cnt;
	/** Counter for object list rpc calls */
	dm_rpc_cntr_t obj_enum_cnt;
	/** Counter for dkey enumerate rpc calls */
	dm_rpc_cntr_t dkey_enum_cnt;
	/** Counter for akey enumerate rpc calls */
	dm_rpc_cntr_t akey_enum_cnt;
	/** Counter for recx enumerate rpc calls */
	dm_rpc_cntr_t recx_enum_cnt;
	/** Counter for obj sync rpc calls */
	dm_rpc_cntr_t obj_sync_cnt;
} dm_obj_rpc_cntrs_t;

/** rpc counters for Pool, Container and Object calls */
typedef struct {
	dm_pool_rpc_cntrs_t d_pool_cntrs;
	dm_cont_rpc_cntrs_t d_cont_cntrs;
	dm_obj_rpc_cntrs_t  d_obj_cntrs;
} dm_rpc_cntrs_t;

/** Metric type for stats */
typedef struct {
	unsigned long value;
	unsigned long min;
	unsigned long max;
	unsigned long sum;
	unsigned long sum_of_squares;
} dm_stats_t;

/** daos i/o stats */
typedef struct {
	dm_stats_t dm_obj_update;
	dm_stats_t dm_obj_fetch;
} dm_iostats_t;

/** Types of i/o stats */
enum dm_io_type {
	DM_OBJ_UPDATE	= 0x001,
	DM_OBJ_FETCH	= 0x002,
};

/** Metrics for i/o distribution */
/**
 * Distribution ids for fetch/update rpc calls based on size.
 * Field DM_IO_X_Y indicates calls with size greater than X and less than
 * or equal to Y
 */
enum {
	DM_IO_0_1K = 0,
	DM_IO_1K_2K = 1,
	DM_IO_2K_4K,
	DM_IO_4K_8K,
	DM_IO_8K_16K,
	DM_IO_16K_32K,
	DM_IO_32K_64K,
	DM_IO_64K_128K,
	DM_IO_128K_256K,
	DM_IO_256K_512K,
	DM_IO_512K_1M,
	DM_IO_1M_2M,
	DM_IO_2M_4M,
	DM_IO_4M_INF,
	DM_IO_BYSIZE_COUNT,
};

/** Distribution of i/o rpc calls based on size */
typedef struct {
	unsigned long	total_call_cnt;
	dm_cntr_t	buckets[DM_IO_BYSIZE_COUNT];
} dm_iocall_dist_byprot_t;

/** Distribution ids for update rpc calls based on protection/replication type */
enum {
	/** No Protection */
	DM_IO_NO_PROT = 0,
	/** Replication */
	DM_IO_RP,
	/** Erasure coding and partial update of a stripe */
	DM_IO_EC_P,
	/** Erasure coding and update of full stripe */
	DM_IO_EC_F,
	DM_IO_BYPROTO_COUNT,
};

/** Distribution of i/o rpc calls based on protection type */
typedef struct {
	unsigned long	total_call_cnt;
	dm_cntr_t	buckets[DM_IO_BYPROTO_COUNT];
} dm_iocall_dist_bysize_t;

/**
 * Returns the daos metrics version in use by the daos library.
 * The library is compatible if the major version number returned matches
 * DAOS_METRICS_MAJOR_VERSION and minor version is greater than or equal to
 * DAOS_METRICS_MINOR_VERSION.
 *
 * \param[out]	major	Major version number
 * \param[out]	minor	Minor version number
 *
 * \return   		0 if metrics is enabled, 1 if metrics is disabled.
 */
int
daos_metrics_get_version(int *major, int *minor);

/**
 * Returns the daos client rpc counters of the specified type. Refer enum dm_rpc_cntr_type
 * for the types supported. 
 *
 * \param[in]		type	Type of the rpc counters requested. It can be a
 * 				union of enums listed in dm_rpc_cntr_type.
 * \param[in,out]	cntrs	Struct containing the counters. Only sub structures
 * 				of the matching type will be populated. 
 *
 * \return   		0	 if metrics is enabled, 1 if metrics is disabled, -ve on failure.
 */
int
daos_metrics_get_rpccntrs(int type, dm_rpc_cntrs_t *cntrs);

/**
 * Returns the daos client stats metric of the specified io_type. Refer enum dm_io_type for the
 * io types supported. 
 *
 * \param[in]		io_type	I/O type for which the stats is requested. It can be a union of
 * 			enums listed in dm_io_type.
 * \param[in,out]	stats	Struct containing the stats. Only sub structures of the matching
 * 				type will be populated. 
 *
 * \return   		0	 if metrics is enabled, 1 if metrics is disabled, -ve on failure.
 */
int
daos_metrics_get_iostats(int io_type, dm_iostats_t *stats);

/**
 * Returns the daos client i/o rpc call count distribution based on the size of data
 * transferred for the specified io_type. Refer enum dm_io_type for the io types supported.
 *
 * \param[in]		io_type	I/O type for which the distribution info is requested.
 * 				It can be a union of enums listed in dm_io_type.
 * \param[in,out]	dist	Struct containing the distribution info. Only sub structures
 * 				of the matching type will be populated. 
 *
 * \return   		0	 if metrics is enabled, 1 if metrics is disabled, -ve on failure.
 */
int
daos_metrics_get_io_distbysize(int io_type, dm_iocall_dist_bysize_t *dist);

/**
 * Returns the daos client obj update rpc call count distribution based on the protection type.
 *
 * \param[in,out]	dist	Struct containing the distribution info.
 *
 * \return   		0	if metrics is enabled, 1 if metrics is disabled, -ve on failure.
 */
int
daos_metrics_get_objupdate_distbyprot(dm_iocall_dist_byprot_t *dist);


/**
 * Dump the metrics to the file pointed to by the file descriptor fd.
 *
 * \return   		0	if metrics is enabled, 1 if metrics is disabled, -ve on failure.
 */
int
daos_metrics_dump(int fd);

#if defined(__cplusplus)
}
#endif

#endif /* __DAOS_CLI_METRICS_H__ */
