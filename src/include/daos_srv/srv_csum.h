/**
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_IOSRV_CHECKSUM_H__
#define __DAOS_IOSRV_CHECKSUM_H__

#include <daos_srv/bio.h>
#include <daos_srv/pool.h>
#include <daos/checksum.h>

/**
 * Process the bsgl and create new checksums or use the stored
 * checksums for the bsgl as needed and appropriate. The result is the iod will
 * have checksums appropriate for the extents and data they represent
 *
 * @param iod[in]			I/O Descriptor that will receive the
 *					csums
 * @param csummer[in]			csummer object for calculating and csum
 *					logic
 * @param bsgl[in]			bio scatter gather list with the data
 * @param biov_csums[in]			list csum info for each \bsgl
 * @param biov_csums_used[in/out]	track the number of csums used
 * @return
 */
int
ds_csum_add2iod(daos_iod_t *iod, struct daos_csummer *csummer,
		struct bio_sglist *bsgl, struct dcs_csum_info *biov_csums,
		size_t *biov_csums_used, struct dcs_iod_csums *iod_csums);


/**
 * The following declarations are for checksum scrubbing functions. The function
 * types provide an interface for injecting dependencies into the
 * scrubber (srv_pool_scrub.c) from the schedule/ult management
 * so that it can be more easily tested without depending on the entire daos
 * engine to be running or waiting for schedules to run.
 */

struct cont_scrub {
	struct daos_csummer	*scs_cont_csummer;
	daos_handle_t		 scs_cont_hdl;
	uuid_t			 scs_cont_uuid;
};

/*
 * Because the scrubber operates at the pool level, it will need a way to
 * get some info for each container within the pool as it's scrubbed.
 */
typedef int(*ds_get_cont_fn_t)(uuid_t pool_uuid, uuid_t cont_uuid, void *arg,
			    struct cont_scrub *cont);

/*
 * handler for scrubber progress. will get called after each checksum is
 * calculated.
 */
typedef int (*ds_progress_handler_t)(void *);
/* handler for when corruption is discovered */
typedef int (*ds_corruption_handler_t)(void *);
/* Inject schedule sleeps or yields */
typedef int (*ds_sleep_fn_t)(void *, uint32_t msec);
typedef int (*ds_yield_fn_t)(void *);


/* For the pool target to start and stop the scrubbing ult */
int ds_start_scrubbing_ult(struct ds_pool_child *child);
void ds_stop_scrubbing_ult(struct ds_pool_child *child);

enum scrub_status {
	SCRUB_STATUS_UNKNOWN = 0,
	SCRUB_STATUS_RUNNING = 1,
	SCRUB_STATUS_NOT_RUNNING = 2,
};

/* Scrub the pool */
struct scrub_ctx {
	/**
	 * Pool
	 **/
	uuid_t			 sc_pool_uuid;
	daos_handle_t		 sc_vos_pool_hdl;
	struct ds_pool		*sc_pool; /* Used to get properties */
	struct timespec		 sc_pool_start_scrub;
	int			 sc_pool_last_csum_calcs;
	int			 sc_pool_csum_calcs;

	/**
	 * Container
	 **/
	/* callback function that will provide the csummer for the container */
	ds_get_cont_fn_t	 sc_cont_lookup_fn;
	struct cont_scrub	 sc_cont;

	/** Number of msec between checksum calculations */
	daos_size_t		 sc_msec_between_calcs;

	/**
	 * Object
	 */
	daos_unit_oid_t		 sc_cur_oid;
	daos_key_t		 sc_dkey;
	struct dcs_csum_info	*sc_csum_to_verify;
	daos_epoch_t		 sc_epoch;
	daos_iod_t		 sc_iod;

	/* Current vos object iterator */
	daos_handle_t		 sc_vos_iter_handle;

	/* Schedule controlling function pointers and arg */
	uint32_t		 sc_credits_left;
	ds_sleep_fn_t		 sc_sleep_fn;
	ds_yield_fn_t		 sc_yield_fn;
	void			*sc_sched_arg;

	enum scrub_status	 sc_status;
};

/*
 * It is expected that the pool uuid/handle and any functional dependencies are
 * set in the scrubbing context. The container/object info should not be set.
 * This function will iterate over all of the containers in the pool and if
 * checksums are enabled on the pool, each object in the container will be
 * scrubbed.
 */
int ds_scrub_pool(struct scrub_ctx *ctx);

/*
 * Based on the schedule type, calculate the number of msec to wait between
 * checksum calculations.
 */
uint64_t
ds_scrub_wait_between_msec(uint32_t sched, struct timespec start_time,
			   uint64_t last_csum_calcs, uint64_t freq_seconds);

/*
 * Based on the schedule type, number of checksums already calculated, credits
 * consumed, might need yield or sleep for a certain amount of time.
 */
void
ds_scrub_sched_control(struct scrub_ctx *ctx);

#endif
