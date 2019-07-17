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
 "
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This file is part of daos_sr
 *
 * src/object/srv_ec.c
 */
#define D_LOGFAC	DD_FAC(object)

#include <stddef.h>
#include <stdio.h>
#include <daos/rpc.h>
#include <daos_types.h>
#include "obj_rpc.h"
#include "obj_internal.h"

/* Determines if entire update affects just this target. If so, no IOD
 * modifications or special bulk-transfer handling are needed for this
 * update request.
 */
static bool
ec_is_one_cell(daos_iod_t *iod, struct daos_oclass_attr *oca,
	       unsigned int tgt_idx)
{
	unsigned int    len = oca->u.ec.e_len;
	unsigned int    k = oca->u.ec.e_k;
	int		rc;
	unsigned int	j;

	for (j = 0; j < iod->iod_nr; j++) {
		daos_recx_t     *this_recx = &iod->iod_recxs[j];
		uint64_t         recx_start_offset = this_recx->rx_idx *
						     iod->iod_size;
		uint64_t         recx_end_offset =
					(this_recx->rx_nr * iod->iod_size) +
					recx_start_offset;

		if (recx_start_offset & PARITY_INDICATOR) {
			rc = false;
			break;
		} else if (recx_start_offset/len == recx_end_offset/len && 
			(recx_start_offset % (len * k)) / len == tgt_idx) {
			rc = true;
		} else {
			rc = false;
			break;
		}
	}
	return rc;
}

/* Removes the specified recx entry from an IOD's iod_recx array.
 */
static void
ec_del_recx(daos_iod_t *iod, unsigned int idx)
{
	int j;

	for (j = idx; j < iod->iod_nr - 1; j++)
		iod->iod_recxs[j] = iod->iod_recxs[j+1];
	iod->iod_nr--;
}

/* Process IOD array on data target. Keeps extents that are addressed
 * to this target.
 */
int
ec_data_target(unsigned int dtgt_idx, unsigned int nr, daos_iod_t *iods,
	       struct daos_oclass_attr *oca, long **skip_list)
{
	unsigned long	ss = oca->u.ec.e_len * oca->u.ec.e_k;
	unsigned int	i, j, idx;
	int		rc = 0;

	for (i = 0; i < nr; i++) {
		daos_iod_t	*iod = &iods[i];
		int		 sl_idx = 0;

		if (iod->iod_type == DAOS_IOD_SINGLE ||
			ec_is_one_cell(iod, oca, dtgt_idx))
			continue;
		D_ALLOC_ARRAY(skip_list[i], 3 * iod->iod_nr + 1);
		if (skip_list[i] == NULL)
			D_GOTO(out, rc = -1);
		unsigned int loop_bound = iod->iod_nr;
		for (idx = 0, j = 0; j < loop_bound; j++) {
			daos_recx_t	*this_recx = &iod->iod_recxs[idx];
			unsigned long	so =
				(this_recx->rx_idx * iod->iod_size) % ss;
			unsigned int	cell = so / oca->u.ec.e_len;
			unsigned long	recx_size = iod->iod_size *
						 this_recx->rx_nr;

			if ( iod->iod_recxs[idx].rx_idx & PARITY_INDICATOR) {
				skip_list[i][sl_idx++] = -(long)oca->u.ec.e_len;
				ec_del_recx(iod, idx);;
				continue;
			}
			if (cell == dtgt_idx) {
				uint32_t new_len = (cell+1) *
							oca->u.ec.e_len - so;

				this_recx->rx_nr = new_len / iod->iod_size;
				skip_list[i][sl_idx++] = new_len;
				skip_list[i][sl_idx++] =
					-(long)(recx_size - new_len);
			} else if ((dtgt_idx +1) * oca->u.ec.e_len < so) {
				/* this recx doesn't map to this target
				 * so we need to remove the recx */
				ec_del_recx(iod, idx);
				skip_list[i][sl_idx++] =
					-(long)(this_recx->rx_nr *
						iod->iod_size);
				continue;
			} else {
				int cell_start = dtgt_idx *
							  oca->u.ec.e_len - so;

				if (cell_start > recx_size) {
					/* this recx doesn't map to this target
					 * so we need to remove the recx */
					ec_del_recx(iod, idx);
					skip_list[i][sl_idx++] =
					      this_recx->rx_nr * iod->iod_size;
					continue;
				}
				skip_list[i][sl_idx++] = -cell_start;
				this_recx->rx_idx += cell_start / iod->iod_size;
				if (cell_start + oca->u.ec.e_len < recx_size) {
					this_recx->rx_nr =
						oca->u.ec.e_len / iod->iod_size;
					skip_list[i][sl_idx++] =
						oca->u.ec.e_len;
					skip_list[i][sl_idx++] = -(recx_size -
						(cell_start + oca->u.ec.e_len));
				} else {
					this_recx->rx_nr = (recx_size -
						cell_start) / iod->iod_size;
					skip_list[i][sl_idx++] = recx_size -
								 cell_start;
				}
			}
			idx++;
		}
	}
out:
	return rc;
}

/* Determines if parity exists for the specified stripe */
static bool
ec_has_parity(daos_recx_t *recxs, uint64_t stripe, uint32_t pss,
	      uint32_t iod_size)
{
	unsigned int j;

	for (j = 0; recxs[j].rx_idx & PARITY_INDICATOR; j++) {
		uint64_t p_stripe = (~PARITY_INDICATOR &
					(recxs[j].rx_idx * iod_size)) / pss;

		if (p_stripe == stripe)
			return true;
	}
	return false;
}

/* Process IOD array on parity target. Keeps parity extents that are addressed
 * to this target. Also retains all data extents that do not have parity for
 * their stripe.
 */
int
ec_parity_target(unsigned int ptgt_idx, unsigned int nr, daos_iod_t *iods,
	         struct daos_oclass_attr *oca, long **skip_list)
{
	unsigned long	ss = oca->u.ec.e_len * oca->u.ec.e_k;
	uint32_t	pss = oca->u.ec.e_len * oca->u.ec.e_p;
	unsigned int	i, j, idx;
	int		rc = 0;

	for (i = 0; i < nr; i++) {
		daos_iod_t	*iod = &iods[i];
		int		 sl_idx = 0;

		if (iod->iod_type == DAOS_IOD_SINGLE)
			continue;
		D_ALLOC_ARRAY(skip_list[i], iod->iod_nr + 1);
		if (skip_list[i] == NULL)
			D_GOTO(out, rc = -1);
		unsigned int loop_bound = iod->iod_nr;
		for (idx = 0, j = 0; j < loop_bound; j++) {
			daos_recx_t	*this_recx = &iod->iod_recxs[idx];

			if (iod->iod_recxs[idx].rx_idx & PARITY_INDICATOR) {
				uint64_t	p_address = ~PARITY_INDICATOR &
					this_recx->rx_idx * iod->iod_size;
				unsigned int	so = p_address % pss;
				unsigned int	pcell = so / oca->u.ec.e_len;

				if (pcell == ptgt_idx) {
					skip_list[i][sl_idx++] =
						oca->u.ec.e_len;
				} else {
					ec_del_recx(iod, idx);;
					skip_list[i][sl_idx++] =
						-(long)oca->u.ec.e_len;
					continue;
				}
			} else {
				uint64_t stripe = (this_recx->rx_idx *
						iod->iod_size) / ss;

				if (ec_has_parity(iod->iod_recxs, stripe, pss,
						  iod->iod_size)) {
					ec_del_recx(iod, idx);
					skip_list[i][sl_idx++] =
						-(this_recx->rx_nr *
						  iod->iod_size);
				} else {
					skip_list[i][sl_idx++] =
						(this_recx->rx_nr *
						 iod->iod_size);
				}
			}
			idx++;
		}
	}
out:
	return rc;
}

/* Make a copy of the iod array submitted in the RPC. Needed on the leader,
 * since the original IOD contents must be forwarded to the slave targets.
 */
int
ec_copy_iods(daos_iod_t **out_iod, daos_iod_t *in_iod, int nr)
{
	int i, rc = 0;
	D_ASSERT(*out_iod == NULL);

	D_ALLOC_ARRAY(*out_iod, nr);
	if (*out_iod == NULL)
		D_GOTO(out, rc = -1);
	for (i = 0; i < nr; i++) {
		(*out_iod)[i] = in_iod[i];
		D_ALLOC_ARRAY((*out_iod)[i].iod_recxs, (*out_iod)[i].iod_nr);
		if((*out_iod)[i].iod_recxs == NULL) {
			int j;
			for (j =0; j < i; j++)
				D_FREE((*out_iod)[j].iod_recxs);
			D_FREE(out_iod);
			D_GOTO(out, rc = -1);
		}
		memcpy((*out_iod)[i].iod_recxs, in_iod[i].iod_recxs,
		       in_iod[i].iod_nr * sizeof(daos_recx_t));
	}
out:
	return rc;
}

#ifdef UNIT_TEST
daos_oclass_attr_t oca =
	{
		.ca_schema		= DAOS_OS_SINGLE,
		.ca_resil		= DAOS_RES_EC,
		.ca_grp_nr		= 1,
		.u.ec			= {
			.e_k		= 2,
			.e_p		= 2,
			.e_len		= 1 << 15,
		},
	};

int main(int argc, char* argv[])
{
	long* skip_list[1];
        daos_handle_t oh = {0L};
        daos_handle_t th = {1L};
        daos_key_t dkey;
        dkey.iov_buf = "42";
        dkey.iov_buf_len = 3L;
        dkey.iov_len = 2L;

	skip_list[0] = NULL;
        //daos_recx_t recx[1] = {{(1 << 18) + (1 << 15) + 16384, (1 << 18) - (1 << 16)}};
        daos_recx_t recx[2] = {{PARITY_INDICATOR, 32768L}, {0, 1 << 16}};
		//{(unsigned long)PARITY_INDICATOR | 32768L, 32768L}, {0, 1 << 18}};

        daos_iod_t iod = { dkey,
			   .iod_kcsum = { NULL, 0, 0, 0, 0, 0},
                           DAOS_IOD_ARRAY, 1, 2, recx,
                           NULL, NULL};
	int i;
	for (i = 0; i < iod.iod_nr; i++)
	D_INFO("recxs[%d].rx_idx: %lu, , recxs[%d].rx_nr: %lu\n", 
	       i, iod.iod_recxs[i].rx_idx,i, iod.iod_recxs[i].rx_nr);

	D_INFO("\nstart\n");
	int ret = ec_data_target(1, 1, &iod, &oca, skip_list);
	for (i = 0; skip_list[0][i]; i++)
		D_INFO("%d -> %ld\n", i, skip_list[0][i]);
	D_INFO("iod.iod_nr == %u\n", iod.iod_nr);
	for (i = 0; i < iod.iod_nr; i++)
	D_INFO("recxs[%d].rx_idx: %lu, , recxs[%d].rx_nr: %lu\n", 
	       i, iod.iod_recxs[i].rx_idx,i, iod.iod_recxs[i].rx_nr);

}
#endif
