/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(ddb)

#include <string.h>

#include <daos_srv/bio.h>
#include <daos_srv/smd.h>
#include <daos_srv/vos.h>
#include <vos_internal.h>

#include "ddb_common.h"
#include "ddb_parse.h"
#include "ddb_spdk_reinit_wa.h"
#include "ddb_wal.h"

int
dw_open(const char *path, const char *db_path, struct ddb_ctx *ctx, struct bio_meta_context **mc)
{
	struct vos_file_parts   *vf;
	struct bio_meta_context *bio_mc = NULL;
	enum bio_mc_flags        flags  = 0;
	bool                     can_proceed;
	int                      rc;

	D_ALLOC_PTR(vf);
	if (vf == NULL) {
		D_ERROR("Unable to allocate memory for pool path\n");
		return -DER_NOMEM;
	}

	rc = ddb_parse_vos_file_parts(path, db_path, vf);
	if (!SUCCESS(rc)) {
		D_ERROR("Unable to parse VOS pool path '%s' and DB path '%s'\n", path,
			db_path ? db_path : "(null)");
		goto out_vf;
	}

	rc = dwa_can_proceed(ctx, vf->vf_db_path, &can_proceed);
	if (!SUCCESS(rc))
		goto out_vf;
	if (!can_proceed)
		D_GOTO(out_vf, rc = -DER_NO_SERVICE);

	rc = vos_self_init(vf->vf_db_path, true, vf->vf_target_idx);
	if (!SUCCESS(rc)) {
		D_ERROR("Failed to initialize VOS with DB path '%s': " DF_RC "\n", vf->vf_db_path,
			DP_RC(rc));
		goto out_vf;
	}

	if (!bio_nvme_configured(SMD_DEV_TYPE_META)) {
		D_ERROR("DB path '%s' is not configured in MD-on-SSD mode\n", vf->vf_db_path);
		D_GOTO(out_fini, rc = -DER_NOTSUPPORTED);
	}

	if (strncmp(vf->vf_vos_file_name, "rdb", 3) == 0)
		flags |= BIO_MC_FL_RDB;

	/*
	 * The blobs under inspection may be damaged: bio_mc_open() asserts on a meta or WAL header
	 * whose checksum is valid but whose content is not (e.g. a checkpoint ID beyond the
	 * committed ID), so the headers are verified first, without opening a session.
	 */
	rc = bio_mc_hdr_verify(vos_xsctxt_get(), vf->vf_pool_uuid, flags);
	if (!SUCCESS(rc)) {
		D_ERROR("Invalid meta/WAL headers for pool " DF_UUID " target %u: " DF_RC "\n",
			DP_UUID(vf->vf_pool_uuid), vf->vf_target_idx, DP_RC(rc));
		goto out_fini;
	}

	/*
	 * bio_mc_open() opens the meta blob, loads and validates the meta and WAL headers and opens
	 * the WAL (and data) blobs. The WAL replay is a separate step performed by VOS when a pool
	 * is opened, so the WAL content is left untouched here.
	 */
	rc = bio_mc_open(vos_xsctxt_get(), vf->vf_pool_uuid, flags, &bio_mc);
	if (!SUCCESS(rc)) {
		D_ERROR("Failed to open the meta/WAL blobs of pool " DF_UUID " target %u: " DF_RC
			"\n",
			DP_UUID(vf->vf_pool_uuid), vf->vf_target_idx, DP_RC(rc));
		goto out_fini;
	}
	if (bio_mc == NULL) {
		/* bio_mc_open() only leaves the meta context unset without MD-on-SSD */
		D_ERROR("No meta context for pool " DF_UUID " target %u: DB path '%s' is not "
			"configured in MD-on-SSD mode\n",
			DP_UUID(vf->vf_pool_uuid), vf->vf_target_idx, vf->vf_db_path);
		D_GOTO(out_fini, rc = -DER_NOTSUPPORTED);
	}

	*mc = bio_mc;
	D_FREE(vf);
	return 0;

out_fini:
	vos_self_fini();
out_vf:
	D_FREE(vf);
	return rc;
}

int
dw_close(struct bio_meta_context *mc)
{
	int rc;

	rc = bio_mc_close(mc);
	vos_self_fini();

	return rc;
}
