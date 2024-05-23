/**
 * (C) Copyright 2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef DAOS_CLI_CSUM_H
#define DAOS_CLI_CSUM_H

#include <daos/checksum.h>
#include <daos/cont_props.h>
#include "obj_internal.h"

int dc_obj_csum_update(struct daos_csummer *csummer, struct cont_props props, daos_obj_id_t param,
		       daos_key_t *dkey, daos_iod_t *iods, d_sg_list_t *sgls, const uint32_t iod_nr,
		       struct dcs_layout *layout, struct dcs_csum_info **dkey_csum,
		       struct dcs_iod_csums **iod_csums);

int dc_obj_csum_fetch(struct daos_csummer *csummer, daos_key_t *dkey, daos_iod_t *iods,
		      d_sg_list_t *sgls, const uint32_t iod_nr, struct dcs_layout *layout,
		      struct dcs_csum_info **dkey_csum, struct dcs_iod_csums **iod_csums);

/*
 * used to flatten all of the information needed for verifying checksums after fetch from server
 */
struct dc_csum_veriry_args {
	struct daos_csummer     *csummer;
	d_sg_list_t             *sgls;
	daos_iod_t              *iods;

	struct dcs_iod_csums    *iods_csums;
	daos_iom_t              *maps;
	daos_key_t              *dkey;

	uint64_t                *sizes;
	daos_unit_oid_t          oid;
	uint32_t                 iod_nr;
	uint64_t                 maps_nr;

	struct obj_io_desc      *oiods;
	struct obj_reasb_req    *reasb_req;

	struct dc_object        *obj;
	uint64_t                 dkey_hash;
	uint64_t                *shard_offs;

	struct daos_oclass_attr *oc_attr;
	d_iov_t                 *iov_csum;
	uint32_t                 shard;
	uint32_t                 shard_idx;
};

int
dc_rw_cb_csum_verify(struct dc_csum_veriry_args *args);

#endif /* DAOS_CLI_CSUM_H */
