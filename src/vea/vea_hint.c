/**
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos/common.h>
#include "vea_internal.h"

void
hint_get(struct vea_hint_context *hint, uint64_t *off)
{
	if (hint != NULL) {
		D_ASSERT(off != NULL);
		*off = hint->vhc_off;
	}
}

void
hint_update(struct vea_hint_context *hint, uint64_t off, uint64_t *seq)
{
	if (hint != NULL) {
		D_ASSERT(seq != NULL);
		hint->vhc_off = off;
		hint->vhc_seq++;
		*seq = hint->vhc_seq;
	}
}

static inline bool
is_rsrv_interleaved(uint64_t seq_min, uint64_t seq_max, unsigned int seq_cnt)
{
	unsigned int diff = seq_max - seq_min + 1;

	D_ASSERTF(diff >= seq_cnt, "["DF_U64", "DF_U64"] %u\n",
		  seq_min, seq_max, seq_cnt);
	return diff > seq_cnt;
}

int
hint_cancel(struct vea_hint_context *hint, uint64_t off, uint64_t seq_min,
	    uint64_t seq_max, unsigned int seq_cnt)
{
	if (hint == NULL)
		return 0;

	D_ASSERT(hint->vhc_pd != NULL);
	if (hint->vhc_seq == seq_max &&
	    !is_rsrv_interleaved(seq_min, seq_max, seq_cnt)) {
		/*
		 * This is the last reserve, and no interleaved reserve, revert
		 * the hint offset to the first offset with min sequence.
		 */
		hint->vhc_off = off;
		return 0;
	} else if (hint->vhc_seq > seq_max) {
		/*
		 * Subsequent reserve detected, abort hint cancel. It could
		 * result in un-allocated holes on out of order hint cancels,
		 * not a big deal.
		 */
		return 0;
	}

	D_ERROR("unexpected transient hint "DF_U64" ["DF_U64", "DF_U64"] %u\n",
		hint->vhc_seq, seq_min, seq_max, seq_cnt);

	return -DER_INVAL;
}

int
hint_tx_publish(struct umem_instance *umm, struct vea_hint_context *hint,
		uint64_t off, uint64_t seq_min, uint64_t seq_max,
		unsigned int seq_cnt)
{
	int	rc;

	D_ASSERT(umem_tx_inprogress(umm) ||
		 umm->umm_id == UMEM_CLASS_VMEM);

	if (hint == NULL)
		return 0;

	D_ASSERT(hint->vhc_pd != NULL);

	if (hint->vhc_pd->vhd_seq == seq_min ||
	    hint->vhc_pd->vhd_seq == seq_max)
		goto error;

	if (hint->vhc_pd->vhd_seq > seq_max) {
		/* Subsequent reserve is already published */
		return 0;
	} else if (hint->vhc_pd->vhd_seq < seq_min ||
		   is_rsrv_interleaved(seq_min, seq_max, seq_cnt)) {
		rc = umem_tx_add_ptr(umm, hint->vhc_pd, sizeof(*hint->vhc_pd));
		if (rc != 0)
			return rc;

		hint->vhc_pd->vhd_off = off;
		hint->vhc_pd->vhd_seq = seq_max;
		return 0;
	}
error:
	D_ERROR("unexpected persistent hint "DF_U64" ["DF_U64", "DF_U64"] %u\n",
		hint->vhc_pd->vhd_seq, seq_min, seq_max, seq_cnt);

	return -DER_INVAL;
}
