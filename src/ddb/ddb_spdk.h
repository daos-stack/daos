/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef DAOS_DDB_SPDK_H
#define DAOS_DDB_SPDK_H

#include <daos_srv/bio.h>

typedef int (*ddbs_sync_cb)(struct bio_blob_hdr *hdr, void *cb_arg);

int ddbs_for_each_bio_blob_hdr(char *nvme_json, ddbs_sync_cb cb, void *cb_arg);

#endif /* DAOS_DDB_SPDK_H */
