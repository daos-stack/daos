/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __CACHE_ENGINE_H_
#define __CACHE_ENGINE_H_

struct ocf_thread_priv;
struct ocf_request;

#define LOOKUP_HIT 5
#define LOOKUP_MISS 6
#define LOOKUP_REMAPPED 8

typedef enum {
	/* modes inherited from user API */
	ocf_req_cache_mode_wt = ocf_cache_mode_wt,
	ocf_req_cache_mode_wb = ocf_cache_mode_wb,
	ocf_req_cache_mode_wa = ocf_cache_mode_wa,
	ocf_req_cache_mode_pt = ocf_cache_mode_pt,
	ocf_req_cache_mode_wi = ocf_cache_mode_wi,
	ocf_req_cache_mode_wo = ocf_cache_mode_wo,

	/* internal modes */
	ocf_req_cache_mode_fast,
		/*!< Fast path */
	ocf_req_cache_mode_d2c,
		/*!< Direct to Core - pass through to core without
				touching cacheline metadata */

	ocf_req_cache_mode_max,
} ocf_req_cache_mode_t;

struct ocf_io_if {
	int (*read)(struct ocf_request *req);

	int (*write)(struct ocf_request *req);

	const char *name;
};

void ocf_resolve_effective_cache_mode(ocf_cache_t cache,
		ocf_core_t core, struct ocf_request *req);

const struct ocf_io_if *ocf_get_io_if(ocf_req_cache_mode_t cache_mode);

static inline const char *ocf_get_io_iface_name(ocf_cache_mode_t cache_mode)
{
	const struct ocf_io_if *iface = ocf_get_io_if(cache_mode);

	return iface ? iface->name : "Unknown";
}

static inline bool ocf_cache_mode_is_valid(ocf_cache_mode_t mode)
{
	return mode >= ocf_cache_mode_wt && mode < ocf_cache_mode_max;
}

static inline bool ocf_req_cache_mode_has_lazy_write(ocf_req_cache_mode_t mode)
{
	return ocf_cache_mode_is_valid((ocf_cache_mode_t)mode) &&
			ocf_mngt_cache_mode_has_lazy_write(
					(ocf_cache_mode_t)mode);
}

bool ocf_fallback_pt_is_on(ocf_cache_t cache);

struct ocf_request *ocf_engine_pop_req(struct ocf_queue *q);

int ocf_engine_hndl_req(struct ocf_request *req);

#define OCF_FAST_PATH_YES	7
#define OCF_FAST_PATH_NO	13

int ocf_engine_hndl_fast_req(struct ocf_request *req);

void ocf_engine_hndl_discard_req(struct ocf_request *req);

void ocf_engine_hndl_ops_req(struct ocf_request *req);

#endif
