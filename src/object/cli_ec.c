/**
 * (C) Copyright 2016-2019 Intel Corporation.
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
/**
 * DAOS client erasure-coded object handling.
 *
 * src/object/cli_ec.c
 */
#define D_LOGFAC	DD_FAC(object)

#include <daos/common.h>
#include <daos_task.h>
#include <daos_types.h>
#include "obj_rpc.h"
#include "obj_internal.h"

/* EC struct used to save state during encoding and to drive resource recovery.
 */
struct ec_params {
	daos_iod_t		*iods;	/* Replaces iod array in update.
					 * NULL except head of list
					 */
	d_sg_list_t		*sgls;	/* Replaces sgl array in update.
					 * NULL except head
					 */
	unsigned int		nr;	/* number of records in iods and sgls
					 * (same as update_t)
					 */
	daos_iod_t		niod;	/* replacement IOD for an input IOD
					 * that includes full stripe.
					 */
	d_sg_list_t		nsgl;	/* replacement SGL for an input IOD that
					 * includes full stripe.
					 */
	struct obj_ec_parity	p_segs;	/* Structure containing array of
					 * pointers to parity extents.
					 */
	struct ec_params        *next;	/* Pointer to next entry in list. */
};

/* Determines weather a given IOD contains a recx that is at least a full
 * stripe's worth of data.
 */
static bool
ec_has_full_stripe(daos_iod_t *iod, struct daos_oclass_attr *oca,
		   uint64_t *tgt_set)
{
	unsigned int	ss = oca->u.ec.e_k * oca->u.ec.e_len;
	unsigned int	i;

	for (i = 0; i < iod->iod_nr; i++) {
		if (iod->iod_type == DAOS_IOD_ARRAY) {
			int start = iod->iod_recxs[i].rx_idx * iod->iod_size;
			int length = iod->iod_recxs[i].rx_nr * iod->iod_size;

			if (length < ss) {
				continue;
			} else if (length - (start % ss) >= ss) {
				*tgt_set = ~0UL;
				return true;
			}
		} else if (iod->iod_type == DAOS_IOD_SINGLE &&
			   iod->iod_size >= ss) {
			*tgt_set = ~0UL;
			return true;
		}
	}
	return false;
}

/* Initialize a param structure for an IOD--SGL pair. */
static void
ec_init_params(struct ec_params *params, daos_iod_t *iod, d_sg_list_t *sgl)
{
	memset(params, 0, sizeof(struct ec_params));
	params->niod		= *iod;
	params->nsgl		= *sgl;
}

/* The head of the params list contains the replacement IOD and SGL arrays.
 * These are used only when stripes have been encoded for the update.
 *
 * Called for head of list only (for the first IOD in the input that contains
 * a full stripe.
 */
static int
ec_set_head_params(struct ec_params *head, daos_obj_update_t *args,
		   unsigned int cnt)
{
	unsigned int i;

	D_ALLOC_ARRAY(head->iods, args->nr);
	if (head->iods == NULL)
		return -DER_NOMEM;
	D_ALLOC_ARRAY(head->sgls, args->nr);
	if (head->sgls == NULL) {
		D_FREE(head->iods);
		return -DER_NOMEM;
	}
	for (i = 0; i < cnt; i++) {
		head->iods[i] = args->iods[i];
		head->sgls[i] = args->sgls[i];
		head->nr++;
	}
	return 0;
}

/* Moves the SGL "cursors" to the start of a full stripe */
static void
ec_move_sgl_cursors(d_sg_list_t *sgl, size_t size, unsigned int *sg_idx,
		 size_t *sg_off)
{
	if (size < sgl->sg_iovs[*sg_idx].iov_len - *sg_off) {
		*sg_off += size;
	} else {
		size_t buf_len = sgl->sg_iovs[*sg_idx].iov_len - *sg_off;

		for (*sg_off = 0; *sg_idx < sgl->sg_nr; (*sg_idx)++) {
			if (buf_len + sgl->sg_iovs[*sg_idx].iov_len > size) {
				*sg_off = size - buf_len;
				break;
			}
			buf_len += sgl->sg_iovs[*sg_idx].iov_len;
		}
	}
}

/* Allocates a stripe's worth of parity cells. */
static int
ec_allocate_parity(struct obj_ec_parity *par, unsigned int len, unsigned int p,
		unsigned int prior_cnt)
{
	unsigned char	**nbuf;
	unsigned int	i;
	int		rc = 0;

	D_REALLOC_ARRAY(nbuf, par->p_bufs, (prior_cnt + p));
	if (nbuf == NULL)
		return -DER_NOMEM;
	par->p_bufs = nbuf;

	for (i = prior_cnt; i < prior_cnt + p; i++) {
		D_ALLOC(par->p_bufs[i], len);
		if (par->p_bufs[i] == NULL)
			return -DER_NOMEM;
		par->p_nr++;
	}
	return rc;
}

/* Encode all of the full stripes contained within the recx at recx_idx.
 */
static int
ec_array_encode(struct ec_params *params, daos_obj_id_t oid, daos_iod_t *iod,
		d_sg_list_t *sgl, struct daos_oclass_attr *oca,
		int recx_idx, unsigned int *sg_idx, size_t *sg_off)
{
	uint64_t	 s_cur;
	unsigned int	 len = oca->u.ec.e_len;
	unsigned int	 k = oca->u.ec.e_k;
	unsigned int	 p = oca->u.ec.e_p;
	daos_recx_t     *this_recx = &iod->iod_recxs[recx_idx];
	uint64_t	 recx_start_offset = this_recx->rx_idx * iod->iod_size;
	uint64_t	 recx_end_offset = (this_recx->rx_nr * iod->iod_size) +
					   recx_start_offset;
	unsigned int	 i;
	int		 rc = 0;

	/* s_cur is the index (in bytes) into the recx where a full stripe
	 * begins.
	 */
	s_cur = recx_start_offset + (recx_start_offset % (len * k));
	if (s_cur != recx_start_offset)
		/* if the start of stripe is not at beginning of recx, move
		 * the sgl index to where the stripe begins).
		 */
		ec_move_sgl_cursors(sgl, recx_start_offset % (len * k), sg_idx,
				    sg_off);

	for ( ; s_cur + len*k <= recx_end_offset; s_cur += len*k) {
		daos_recx_t *nrecx;

		rc = ec_allocate_parity(&(params->p_segs), len, p,
					params->niod.iod_nr);
		if (rc != 0)
			return rc;
		rc = obj_encode_full_stripe(oid, sgl, sg_idx, sg_off,
					    &(params->p_segs),
					    params->niod.iod_nr);
		if (rc != 0)
			return rc;
		/* Parity is prepended to the recx array, so we have to add
		 * them here for each encoded stripe.
		 */
		D_REALLOC_ARRAY(nrecx, (params->niod.iod_recxs),
				(params->niod.iod_nr+p));
		if (nrecx == NULL)
			return -DER_NOMEM;
		params->niod.iod_recxs = nrecx;
		for (i = 0; i < p; i++) {
			params->niod.iod_recxs[params->niod.iod_nr].rx_idx =
			PARITY_INDICATOR | (s_cur+i*len)/params->niod.iod_size;
			params->niod.iod_recxs[params->niod.iod_nr++].rx_nr =
				len / params->niod.iod_size;
		}
	}
	return rc;
}

/* Updates the params instance for a IOD -- SGL pair.
 * The parity recxs have already been added, this function appends the
 * original recx entries.
 * The parity cells are placed first in the SGL, followed by the
 * input entries.
 */
static int
ec_update_params(struct ec_params *params, daos_iod_t *iod, d_sg_list_t *sgl,
		 unsigned int len)
{
	daos_recx_t	*nrecx;
	unsigned int	 i;
	int		 rc = 0;

	D_REALLOC_ARRAY(nrecx, (params->niod.iod_recxs),
			(params->niod.iod_nr + iod->iod_nr));
	if (nrecx == NULL)
		return -DER_NOMEM;
	params->niod.iod_recxs = nrecx;
	for (i = 0; i < iod->iod_nr; i++)
		params->niod.iod_recxs[params->niod.iod_nr++] =
			iod->iod_recxs[i];

	D_ALLOC_ARRAY(params->nsgl.sg_iovs, (params->p_segs.p_nr + sgl->sg_nr));
	if (params->nsgl.sg_iovs == NULL)
		return -DER_NOMEM;
	for (i = 0; i < params->p_segs.p_nr; i++) {
		params->nsgl.sg_iovs[i].iov_buf = params->p_segs.p_bufs[i];
		params->nsgl.sg_iovs[i].iov_buf_len = len;
		params->nsgl.sg_iovs[i].iov_len = len;
		params->nsgl.sg_nr++;
	}
	for (i = 0; i < sgl->sg_nr; i++)
		params->nsgl.sg_iovs[params->nsgl.sg_nr++] = sgl->sg_iovs[i];

	return rc;
}

/* Recover EC allocated memory */
static void
ec_free_params(struct ec_params *head)
{
	D_FREE(head->iods);
	D_FREE(head->sgls);
	while (head != NULL) {
		int i;
		struct ec_params *current = head;

		D_FREE(current->niod.iod_recxs);
		D_FREE(current->nsgl.sg_iovs);
		for (i = 0; i < current->p_segs.p_nr; i++)
			D_FREE(current->p_segs.p_bufs[i]);
		D_FREE(current->p_segs.p_bufs);
		head = current->next;
		D_FREE(current);
	}
}


/* Call-back that recovers EC allocated memory  */
static int
ec_free_params_cb(tse_task_t *task, void *data)
{
	struct ec_params *head = *((struct ec_params **)data);
	int		  rc = task->dt_result;

	ec_free_params(head);
	return rc;
}

/* Identifies the applicable subset of forwarding targets for non-full-stripe
 * EC updates.
 */
static void
ec_init_tgt_set(daos_iod_t *iods, unsigned int nr,
		struct daos_oclass_attr *oca, uint64_t *tgt_set)
{
	unsigned int    len = oca->u.ec.e_len;
	unsigned int    k = oca->u.ec.e_k;
	unsigned int    p = oca->u.ec.e_p;
	uint64_t	par_only = (1UL << p) - 1;
	uint64_t	full = (1UL <<  (k+p)) - 1;
	unsigned int	i, j;

	for (i = 0; i < p; i++)
		*tgt_set |= 1UL << i;
	for (i = 0; i < nr; i++) {
		if (iods->iod_type != DAOS_IOD_ARRAY)
			continue;
		for (j = 0; j < iods[i].iod_nr; j++) {
			uint64_t ext_idx;
			uint64_t rs = iods[i].iod_recxs[j].rx_idx *
						iods[i].iod_size;
			uint64_t re = iods[i].iod_recxs[j].rx_nr *
						iods[i].iod_size + rs - 1;

			/* No partial-parity updates, so this function won't be
			 * called if parity is present.
			 */
			D_ASSERT(!(PARITY_INDICATOR & rs));
			for (ext_idx = rs; ext_idx <= re; ext_idx += len) {
				unsigned int cell = ext_idx/len;

				*tgt_set |= 1UL << (cell+p);
				if (*tgt_set == full) {
					*tgt_set = 0;
					return;
				}
			}
		}
	}
	/* In case it's a single value, set the tgt_set to send to all.
	 * These go everywhere for now.
	 */
	if (*tgt_set == par_only)
		*tgt_set = 0;
}

/* Iterates over the IODs in the update, encoding all full stripes contained
 * within each recx.
 */
int
ec_obj_update_encode(tse_task_t *task, daos_obj_id_t oid,
		     struct daos_oclass_attr *oca, uint64_t *tgt_set)
{
	daos_obj_update_t	*args = dc_task_get_args(task);
	struct ec_params	*head = NULL;
	struct ec_params	*current = NULL;
	unsigned int		 i, j;
	int			 rc = 0;

	for (i = 0; i < args->nr; i++) {
		d_sg_list_t	*sgl = &args->sgls[i];
		daos_iod_t	*iod = &args->iods[i];

		if (ec_has_full_stripe(iod, oca, tgt_set)) {
			struct ec_params *params;

			D_ALLOC_PTR(params);
			if (params == NULL) {
				rc = -DER_NOMEM;
				break;
			}
			ec_init_params(params, iod, sgl);
			if (head == NULL) {
				head = params;
				current = head;
				rc = ec_set_head_params(head, args, i);
				if (rc != 0)
					break;
			} else {
				current->next = params;
				current = params;
			}
			if (args->iods[i].iod_type == DAOS_IOD_ARRAY) {
				unsigned int sg_idx = 0;
				size_t sg_off = 0;

				for (j = 0; j < iod->iod_nr; j++) {
					rc = ec_array_encode(params, oid, iod,
							     sgl, oca, j,
							     &sg_idx, &sg_off);
					if (rc != 0)
						break;
				}
				rc = ec_update_params(params, iod, sgl,
						      oca->u.ec.e_len);
				head->iods[i] = params->niod;
				head->sgls[i] = params->nsgl;
				D_ASSERT(head->nr == i);
				head->nr++;
			} else {
				D_ASSERT(iod->iod_type ==
					 DAOS_IOD_SINGLE && iod->iod_size >=
					 oca->u.ec.e_len * oca->u.ec.e_k);
				/* Encode single value */
			}
		} else if (head != NULL && &(head->sgls[i]) != NULL) {
			/* Add sgls[i] and iods[i] to head. Since we're
			 * adding ec parity (head != NULL) and thus need to
			 * replace the arrays in the update struct.
			 */
			head->iods[i] = *iod;
			head->sgls[i] = *sgl;
			D_ASSERT(head->nr == i);
			head->nr++;
		}
	}

	if (*tgt_set != 0) {
		/* tgt_set == 0 means send to all forwarding targets
		 * from leader. If it's not zero here, it means that
		 * ec_object_update encoded a full stripe. Hence
		 * the update should go to all targets.
		 */
		*tgt_set = 0;
	} else {
		/* Called for updates with no full stripes.
		 * Builds a bit map only if forwarding targets are
		 * a proper subset. Sets tgt_set to zero if all targets
		 * are addressed.
		 */
		ec_init_tgt_set(args->iods, args->nr, oca, tgt_set);
	}

	if (rc != 0 && head != NULL) {
		ec_free_params(head);
	} else if (head != NULL) {
		args->iods = head->iods;
		args->sgls = head->sgls;
		tse_task_register_comp_cb(task, ec_free_params_cb, &head,
					  sizeof(head));
	}
	return rc;
}

bool
ec_mult_data_targets(uint32_t fw_cnt, daos_obj_id_t oid)
{
        struct daos_oclass_attr *oca = daos_oclass_attr_find(oid);

        if (oca->ca_resil == DAOS_RES_EC && fw_cnt > oca->u.ec.e_p)
                return true;
        return false;
}
