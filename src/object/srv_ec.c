
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

static bool
ec_is_one_cell(daos_iod_t *iod, struct daos_oclass_attr *oca,
	       unsigned int tgt_idx)
{
	unsigned int    len = oca->u.ec.e_len;
	unsigned int    k = oca->u.ec.e_k;
	int		rc = 0;
	unsigned int	j;

	D_INFO("Entering ec_is_one_cell\n");
	for (j = 0; j < iod->iod_nr; j++) {
		D_INFO("Entered loop in ec_is_one_cell; iod->iod_nr == %u\n",
		       iod->iod_nr);
		daos_recx_t     *this_recx = &iod->iod_recxs[j];
		D_INFO("Dereferenced recx: %u in ec_is_one_cell\n", j);

		uint64_t         recx_start_offset = this_recx->rx_idx *
						     iod->iod_size;
		uint64_t         recx_end_offset =
					(this_recx->rx_nr * iod->iod_size) +
					recx_start_offset;

		D_INFO("Processing recx: %u in ec_is_one_cell\n", j);
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

static void
ec_del_recx(daos_iod_t *iod, unsigned int idx)
{
	int j;

	D_INFO("deleting recx at %u\n", idx);
	for (j = idx; j < iod->iod_nr - 1; j++)
		iod->iod_recxs[j] = iod->iod_recxs[j+1];
	iod->iod_nr--;
}

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

		D_INFO("Processing IOD: %u for data target: %u of type %d\n",
		       i, dtgt_idx, iod->iod_type);
		if (iod->iod_type == DAOS_IOD_SINGLE ||
			ec_is_one_cell(iod, oca, dtgt_idx))
			continue;
		D_ALLOC_ARRAY(skip_list[i], 3 * iod->iod_nr + 1);
		unsigned int loop_bound = iod->iod_nr;
		D_INFO("loop_bound: %u\n", loop_bound);
		for (idx = 0, j = 0; j < loop_bound; j++) {
			D_INFO("idx: %u, j: %u\n", idx, j);
			daos_recx_t	*this_recx = &iod->iod_recxs[idx];
			unsigned long	so =
				(this_recx->rx_idx * iod->iod_size) % ss;
			unsigned int	cell = so / oca->u.ec.e_len;
			unsigned long	recx_size = iod->iod_size *
						 this_recx->rx_nr;

			D_INFO("Processing recx: %u, size %lu, starts at %lu\n",
			       j, recx_size, this_recx->rx_idx);
			if ( iod->iod_recxs[idx].rx_idx & PARITY_INDICATOR) {
				D_INFO("processing parity recx at start %lu\n",
				       this_recx->rx_idx & ~PARITY_INDICATOR);
				skip_list[i][sl_idx++] = -(long)oca->u.ec.e_len;
				D_INFO("Deleting parity recx\n");
				ec_del_recx(iod, idx);;
				continue;
			}

			D_INFO("cell: %u, target: %u\n", cell, dtgt_idx);
			if (cell == dtgt_idx) {
				uint32_t new_len = (cell+1) *
							oca->u.ec.e_len - so;
				D_INFO("new_len: %u\n", new_len);

				this_recx->rx_nr = new_len / iod->iod_size;
				skip_list[i][sl_idx++] = new_len;
				skip_list[i][sl_idx++] =
					-(long)(recx_size - new_len);
			} else if ((dtgt_idx +1) * oca->u.ec.e_len < so) {
				D_INFO("cell ends at %lu), offset is %lu\n",
				       (dtgt_idx +1) * oca->u.ec.e_len, so);
				/* this recx doesn't map to this target
				 * so we need to remove the recx */
				ec_del_recx(iod, idx);
				skip_list[i][sl_idx++] =
					-(long)(this_recx->rx_nr *
						iod->iod_size);
				continue;
			} else {
				int cell_start = dtgt_idx  *
							  oca->u.ec.e_len - so;

				D_INFO("cell_start: %d\n", cell_start);

				if (cell_start > recx_size) {
					/* this recx doesn't map to this target
					 * so we need to remove the recx */
					ec_del_recx(iod, idx);
					skip_list[i][sl_idx++] =
					      this_recx->rx_nr * iod->iod_size;
					continue;
				}
				D_INFO("setting skip_list[0][%u] to %d\n", idx,
				       -cell_start); 
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
	return rc;
}

static bool
ec_has_parity(daos_recx_t *recxs, uint64_t stripe, uint32_t pss,
	      uint32_t iod_size)
{
	unsigned int j;

	D_INFO("checking for parity, stripe: %lu\n", stripe);

	for (j = 0; recxs[j].rx_idx & PARITY_INDICATOR; j++) {
		uint64_t p_stripe = (~PARITY_INDICATOR &
					(recxs[j].rx_idx * iod_size)) / pss;

		D_INFO("p_stripe: %lu\n", p_stripe);
		if (p_stripe == stripe)
			return true;
	}
	return false;
}

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

		D_INFO("Processing IOD: %u in parity target\n", i);
		if (iod->iod_type == DAOS_IOD_SINGLE) {
			D_INFO("Single Value at parity target\n");
			continue;
		}
		D_ALLOC_ARRAY(skip_list[i], iod->iod_nr + 1);
		unsigned int loop_bound = iod->iod_nr;
		for (idx = 0, j = 0; j < loop_bound; j++) {
			daos_recx_t	*this_recx = &iod->iod_recxs[idx];

			if (iod->iod_recxs[idx].rx_idx & PARITY_INDICATOR) {
				uint64_t	p_address = ~PARITY_INDICATOR &
					this_recx->rx_idx * iod->iod_size;
				unsigned int	so = p_address % pss;
				unsigned int	pcell = so / oca->u.ec.e_len;

				if (pcell == ptgt_idx) {
					D_INFO("keeping parity cell\n");
					skip_list[i][sl_idx++] =
						oca->u.ec.e_len;
				} else {
					D_INFO("skipping parity cell\n");
					ec_del_recx(iod, idx);;
					skip_list[i][sl_idx++] =
						-(long)oca->u.ec.e_len;
					continue;
				}
			} else {
				D_INFO("processing nonparity recx at start: %lu,
				       length: %lu\n",
				       this_recx->rx_idx, this_recx->rx_nr);
				uint64_t stripe = (this_recx->rx_idx *
						iod->iod_size) / ss;

				if (ec_has_parity(iod->iod_recxs, stripe, pss,
						  iod->iod_size)) {
					ec_del_recx(iod, idx);
					skip_list[i][sl_idx++] =
						-(this_recx->rx_nr *
						  iod->iod_size);
				} else {
					D_INFO("no parity\n");
					skip_list[i][sl_idx++] =
						(this_recx->rx_nr *
						 iod->iod_size);
				}
			}
			idx++;
		}
	}
	return rc;
}


int
ec_update_bulk_transfer(crt_rpc_t *rpc, bool bulk_bind,
		        crt_bulk_t *remote_bulks, daos_handle_t ioh,
			long **skip_list, int sgl_nr)
{
	struct ds_bulk_async_args arg = { 0 };
	crt_bulk_opid_t		bulk_opid;
	crt_bulk_op_t		bulk_op = CRT_BULK_GET;
	crt_bulk_perm_t		bulk_perm = CRT_BULK_RW;
	int			i, rc, *status, ret;

	rc = ABT_eventual_create(sizeof(*status), &arg.eventual);
	if (rc != 0)
		return dss_abterr2der(rc);


	for (i = 0; i < sgl_nr; i++) {
		d_sg_list_t		*sgl, tmp_sgl;
		struct crt_bulk_desc	 bulk_desc;
		crt_bulk_t		 local_bulk_hdl;
		struct bio_sglist	*bsgl;
		daos_size_t		 offset = 0;
		unsigned int		 idx = 0;
		unsigned int		 sl_idx = 0;

		if (remote_bulks[i] == NULL)
			 continue;


		D_ASSERT(!daos_handle_is_inval(ioh));
		bsgl = vos_iod_sgl_at(ioh, i);
		D_ASSERT(bsgl != NULL);

		sgl = &tmp_sgl;
		rc = bio_sgl_convert(bsgl, sgl);
		if (rc)
			break;

		if (daos_io_bypass & IOBP_SRV_BULK) {
			/* this mode will bypass network bulk transfer and
			 * only copy data from/to dummy buffer. This is for
			 * performance evaluation on low bandwidth network.
			 */
			bulk_bypass(sgl, bulk_op);
			goto next;
		}

		while (idx < sgl->sg_nr_out) {
			d_sg_list_t	sgl_sent;
			daos_size_t	length = 0;
			unsigned int	start = idx;

			sgl_sent.sg_iovs = &sgl->sg_iovs[start];
			if (skip_list[i] == NULL) { 
				/* Find the end of the non-empty record */
				while (sgl->sg_iovs[idx].iov_buf != NULL &&
					idx < sgl->sg_nr_out)
					length += sgl->sg_iovs[idx++].iov_len;
			} else {
				while (skip_list[i][sl_idx] < 0) 
					offset += -skip_list[i][sl_idx++];
				length += sgl->sg_iovs[idx++].iov_len;
				D_ASSERT(skip_list[i][sl_idx] == length);
				sl_idx++;
			}
			sgl_sent.sg_nr = idx - start;
			sgl_sent.sg_nr_out = idx - start;

			rc = crt_bulk_create(rpc->cr_ctx, &sgl_sent,
					     bulk_perm, &local_bulk_hdl);
			if (rc != 0) {
				D_ERROR("crt_bulk_create %d error (%d).\n",
					i, rc);
				break;
			}

			crt_req_addref(rpc);

			bulk_desc.bd_rpc	= rpc;
			bulk_desc.bd_bulk_op	= bulk_op;
			bulk_desc.bd_remote_hdl	= remote_bulks[i];
			bulk_desc.bd_local_hdl	= local_bulk_hdl;
			bulk_desc.bd_len	= length;
			bulk_desc.bd_remote_off	= offset;
			bulk_desc.bd_local_off	= 0;

			arg.bulks_inflight++;
			if (bulk_bind)
				rc = crt_bulk_bind_transfer(&bulk_desc,
					bulk_complete_cb, &arg, &bulk_opid);
			else
				rc = crt_bulk_transfer(&bulk_desc,
					bulk_complete_cb, &arg, &bulk_opid);
			if (rc < 0) {
				D_ERROR("crt_bulk_transfer %d error (%d).\n",
					i, rc);
				arg.bulks_inflight--;
				crt_bulk_free(local_bulk_hdl);
				crt_req_decref(rpc);
				break;
			}
			offset += length;
		}
next:
		daos_sgl_fini(sgl, false);
		if (rc)
			break;
		D_FREE(skip_list[i]);
	}

	if (arg.bulks_inflight == 0)
		ABT_eventual_set(arg.eventual, &rc, sizeof(rc));

	ret = ABT_eventual_wait(arg.eventual, (void **)&status);
	if (rc == 0)
		rc = ret ? dss_abterr2der(ret) : *status;

	ABT_eventual_free(&arg.eventual);
	return rc;
}

#ifdef UNIT_TEST
daos_oclass_attr_t oca =
	{
		.ca_schema		= DAOS_OS_SINGLE,
		.ca_resil		= DAOS_RES_EC,
		.ca_grp_nr		= 1,
		.u.ec			= {
			.e_k		= 8,
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
        daos_recx_t recx[1] = {{(1 << 18) + (1 << 15) + 16384, (1 << 18) - (1 << 16)}};
        //daos_recx_t recx[3] = {{PARITY_INDICATOR, 32768L},
	//	{(unsigned long)PARITY_INDICATOR | 32768L, 32768L}, {0, 1 << 18}};

        daos_iod_t iod = { dkey,
			   .iod_kcsum = { 0, 0, 0, NULL},
                           DAOS_IOD_ARRAY, 1, 1, recx,
                           NULL, NULL};
	int i;
	for (i = 0; i < iod.iod_nr; i++)
	D_INFO("recxs[%d].rx_idx: %lu, , recxs[%d].rx_nr: %lu\n", 
	       i, iod.iod_recxs[i].rx_idx,i, iod.iod_recxs[i].rx_nr);

	D_INFO("\nstart\n");
	int ret = ec_parity_target(1, 1, &iod, &oca, skip_list);
	for (i = 0; skip_list[0][i]; i++)
		D_INFO("%d -> %ld\n", i, skip_list[0][i]);
	D_INFO("iod.iod_nr == %u\n", iod.iod_nr);
	for (i = 0; i < iod.iod_nr; i++)
	D_INFO("recxs[%d].rx_idx: %lu, , recxs[%d].rx_nr: %lu\n", 
	       i, iod.iod_recxs[i].rx_idx,i, iod.iod_recxs[i].rx_nr);

}
#endif
