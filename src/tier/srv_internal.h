/**
 * (C) Copyright 2016 Intel Corporation.
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
/*
 * dsms: Internal Declarations
 *
 * This file contains all declarations that are only used by dcts but do not
 * belong to the more specific headers.  All external
 * variables and functions must have a "tier_" prefix, however, even if they
 * are only used by dsms.
 **/

#ifndef __DCTS_INTERNAL_H__
#define __DCTS_INTERNAL_H__
#include <daos_types.h>
#include <daos_srv/vos_types.h>

extern char *colder_grp;
extern uuid_t colder_id;
extern daos_handle_t colder_poh;
extern bool colder_conn_flg;

extern char *warmer_grp;
extern uuid_t warmer_id;
extern daos_handle_t warmer_poh;
extern bool warmer_conn_flg;


struct daos_bld_iod_ctx {
	daos_key_t		dkey;
	daos_unit_oid_t		oid;
	daos_handle_t		coh;
	unsigned int		nr;
	struct daos_list_head   recs;
};

/* tier_ping.c */

/* ping test handler, more of a self-teaching widget */
void ds_tier_ping_handler(crt_rpc_t *rpc);


/* Tier Management Functions
 * Used to setup and debug inter-tier connections
 */
void ds_tier_cross_conn_handler(crt_rpc_t *rpc);
void ds_tier_upstream_handler(crt_rpc_t *rpc);
void ds_tier_register_cold_handler(crt_rpc_t *rpc);
void ds_tier_hdl_bcast_handler(crt_rpc_t *rpc);

/*May be redundant with tier register cold, check with john*/
void ds_tier_fetch_handler(crt_rpc_t *rpc);

typedef int (tier_enum_cbfn_t)(void *, vos_iter_entry_t *);

/* srv_enum.c */
/* tier_enum_params - enumeration parameter block
 *
 * Following VOS, this enumerates by caling a calback function for each
 * item to be enumerated.  In addition, callback functions can be  setup
 * for each item level {object, dkey, akey}, called either before or after
 * enumerating the next level. These allow the caller to mark boundaries or
 * perform operations on subsets of enumerated items.For instance, a caller
 * could enumerate all records in a VOS pool and use the pre/post callbacks to
 * group the records by the dkey they belong to. They can be NULL if not needed.
 * If the caller wishes to enumerate objects, dkeys, or akeys,  either the
 * pre-desend or post-decend callback function for that object should be
 * used as the enumeration callback.
 */

struct tier_enum_params {
	/* the type of thing to enumerate */
	vos_iter_type_t    dep_type;

	/* a caller-supplied context for callback functions */
	void		  *dep_cbctx;

	/* epoch to look in */
	daos_epoch_t	  dep_ev;

	/* object pre-decend and post-decend functions */
	int              (*dep_obj_pre)(void *ctx, vos_iter_entry_t *ie);
	int              (*dep_obj_post)(void *ctx, vos_iter_entry_t *ie);
	/* dkey level pre-decend and post-decend functions */
	int		 (*dep_dkey_pre)(void *ctx, vos_iter_entry_t *ie);
	int		 (*dep_dkey_post)(void *ctx, vos_iter_entry_t *ie);
	/* akey-level pre & post decend functions */
	int		 (*dep_akey_pre)(void *ctx, vos_iter_entry_t *ie);
	int		 (*dep_akey_post)(void *ctx, vos_iter_entry_t *ie);
	/* recx callback function */
	int		 (*dep_recx_cbfn)(void *ctx, vos_iter_entry_t *ie);
};

/**
 * tier_safecb() - performs callbacks thru pointer in a safe manner
 */
static inline int
tier_safecb(tier_enum_cbfn_t *fn, void *ctx, vos_iter_entry_t *ie)
{
	int rc = 0;

	if (fn != NULL)
		rc = (*fn)(ctx, ie);
	return rc;
}

/* Utility functions for fetch's use of tier_enum() */
int ds_tier_enum(daos_handle_t coh, struct tier_enum_params *params);

/* Range-check an epoch */
static inline int
tier_rangein(daos_epoch_range_t *r, daos_epoch_t t)
{
	int rc = 1;

	if ((t < r->epr_lo) || (r->epr_hi < t))
		rc = 0;
	return rc;
}

/* type-copy helpers */
static inline void
tier_cp_iov(daos_iov_t *dst, daos_iov_t *src)
{
	dst->iov_buf     = src->iov_buf;
	dst->iov_buf_len = src->iov_buf_len;
	dst->iov_len     = src->iov_len;
}

static inline void
tier_cp_recx(daos_recx_t *dst, daos_recx_t *src)
{
	dst->rx_idx   = src->rx_idx;
	dst->rx_nr    = src->rx_nr;
}

/* checksum handling */
static inline void
tier_cp_cksum(daos_csum_buf_t *dst, daos_csum_buf_t *src)
{
	dst->cs_type    = src->cs_type;
	dst->cs_len     = src->cs_len;
	dst->cs_buf_len = src->cs_buf_len;
	dst->cs_csum    = src->cs_csum;
}

static inline void
tier_csum(daos_csum_buf_t *dst, void *src, daos_size_t len)
{
	daos_csum_set(dst, NULL, 0);
}

static inline void
tier_cp_oid(daos_unit_oid_t *p1, daos_unit_oid_t *p2)
{
	p1->id_pub    = p2->id_pub;
	p1->id_shard  = p2->id_shard;
	p1->id_pad_32 = p2->id_pad_32;
}

static inline void
tier_cp_vec_iod(daos_iod_t *pd, daos_iod_t *ps)
{
	pd->iod_name = ps->iod_name;
	tier_cp_cksum(&pd->iod_kcsum, &ps->iod_kcsum);
	pd->iod_type = ps->iod_type;
	pd->iod_size = ps->iod_size;
	pd->iod_nr = ps->iod_nr;

	pd->iod_recxs = ps->iod_recxs;
	pd->iod_csums = ps->iod_csums;
	pd->iod_eprs  = ps->iod_eprs;
}

#endif /*__DCTS_INTERNAL_H__*/
