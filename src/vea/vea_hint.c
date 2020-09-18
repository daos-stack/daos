/**
 * (C) Copyright 2018-2020 Intel Corporation.
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
 * provided in Contract No. B620873.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
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

int
hint_cancel(struct vea_hint_context *hint, uint64_t off, uint64_t seq_min,
	    uint64_t seq_max)
{
	if (hint == NULL)
		return 0;

	D_ASSERT(hint->vhc_pd != NULL);
	if (hint->vhc_pd->vhd_seq > seq_max) {
		/*
		 * Subsequent reserve was already published, abort cancel.
		 * It will result in un-allocated hole for the I/O stream.
		 */
		return 0;
	} else if (hint->vhc_pd->vhd_seq > seq_min) {
		D_ERROR("unexpected persistent hint "DF_U64" > "DF_U64"\n",
			hint->vhc_pd->vhd_seq, seq_min);
		return -DER_INVAL;
	}

	if (hint->vhc_seq == seq_max) {
		/* This is the last reserve, revert the hint offset. */
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

	D_ERROR("unexpected transient hint "DF_U64" ["DF_U64", "DF_U64"]\n",
		hint->vhc_seq, seq_min, seq_max);

	return -DER_INVAL;
}

int
hint_tx_publish(struct umem_instance *umm, struct vea_hint_context *hint,
		uint64_t off, uint64_t seq_min, uint64_t seq_max)
{
	int	rc;

	D_ASSERT(pmemobj_tx_stage() == TX_STAGE_WORK ||
		 umm->umm_id == UMEM_CLASS_VMEM);

	if (hint == NULL)
		return 0;

	D_ASSERT(hint->vhc_pd != NULL);
	if (hint->vhc_pd->vhd_seq > seq_max) {
		/* Subsequent reserve is already published */
		return 0;
	} else if (hint->vhc_pd->vhd_seq < seq_min) {
		rc = umem_tx_add_ptr(umm, hint->vhc_pd, sizeof(*hint->vhc_pd));
		if (rc != 0)
			return rc;

		hint->vhc_pd->vhd_off = off;
		hint->vhc_pd->vhd_seq = seq_max;
		return 0;
	}

	D_ERROR("unexpected persistent hint "DF_U64", ["DF_U64", "DF_U64"]\n",
		hint->vhc_pd->vhd_seq, seq_min, seq_max);

	return -DER_INVAL;
}
