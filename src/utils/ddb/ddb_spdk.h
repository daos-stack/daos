/**
 * (C) Copyright 2022 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef DAOS_DDB_SPDK_H
#define DAOS_DDB_SPDK_H

#include <daos_srv/bio.h>

struct ddbs_sync_info {
	union {
		/* SMD_DEV_TYPE_DATA */
		struct bio_blob_hdr *dsi_hdr;
		/* SMD_DEV_TYPE_META */
		struct meta_header  *dsi_meta_hdr;
		/* SMD_DEV_TYPE_WAL */
		struct wal_header   *dsi_wal_hdr;
	};
	enum smd_dev_type        st;
	uuid_t			 dsi_dev_id;
	uint64_t                 dsi_blob_id;
	uint64_t		 dsi_cluster_size;
	uint64_t		 dsi_cluster_nr;
};

typedef void (*ddbs_sync_cb)(struct ddbs_sync_info *dsi, void *cb_arg);

int ddbs_for_each_bio_blob_hdr(const char *nvme_json, ddbs_sync_cb cb, void *cb_arg);

#endif /* DAOS_DDB_SPDK_H */
