/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef DAOS_DDB_SPDK_H
#define DAOS_DDB_SPDK_H

#include <daos_srv/bio.h>

struct ddbs_sync_info {
	struct bio_blob_hdr	*dsi_hdr;
	uuid_t			 dsi_dev_id;
	uint64_t		 dsi_cluster_size;
	uint64_t		 dsi_cluster_nr;
};

typedef void (*ddbs_sync_cb)(struct ddbs_sync_info *dsi, void *cb_arg);

int ddbs_for_each_bio_blob_hdr(const char *nvme_json, ddbs_sync_cb cb, void *cb_arg);

#endif /* DAOS_DDB_SPDK_H */
