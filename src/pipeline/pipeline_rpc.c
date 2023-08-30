/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC DD_FAC(pipeline)

#include <daos/rpc.h>
#include <daos/pipeline.h>
#include <daos/common.h>
#include "pipeline_rpc.h"
#include <mercury_proc.h>

static int
crt_proc_daos_key_desc_t(crt_proc_t proc, crt_proc_op_t proc_op, daos_key_desc_t *key)
{
	int rc;

	rc = crt_proc_uint64_t(proc, proc_op, &key->kd_key_len);
	if (unlikely(rc))
		return rc;

	rc = crt_proc_uint32_t(proc, proc_op, &key->kd_val_type);
	if (unlikely(rc))
		return rc;

	return 0;
}

static int
crt_proc_d_sg_list_t(crt_proc_t proc, crt_proc_op_t proc_op, d_sg_list_t *p)
{
	int i;
	int rc;

	if (FREEING(proc_op)) {
		/* NB: don't need free in crt_proc_d_iov_t() */
		D_FREE(p->sg_iovs);
		return 0;
	}

	rc = crt_proc_uint32_t(proc, proc_op, &p->sg_nr);
	if (unlikely(rc))
		return rc;

	rc = crt_proc_uint32_t(proc, proc_op, &p->sg_nr_out);
	if (unlikely(rc))
		return rc;

	if (p->sg_nr == 0)
		return 0;

	switch (proc_op) {
	case CRT_PROC_DECODE:
		D_ALLOC_ARRAY(p->sg_iovs, p->sg_nr);
		if (p->sg_iovs == NULL)
			return -DER_NOMEM;
		/* fall through to fill sg_iovs */
	case CRT_PROC_ENCODE:
		for (i = 0; i < p->sg_nr; i++) {
			rc = crt_proc_d_iov_t(proc, proc_op, &p->sg_iovs[i]);
			if (unlikely(rc)) {
				if (DECODING(proc_op))
					D_FREE(p->sg_iovs);
				return rc;
			}
		}
		break;
	default:
		return -DER_INVAL;
	}

	return rc;
}

static int
crt_proc_daos_unit_oid_t(crt_proc_t proc, crt_proc_op_t proc_op, daos_unit_oid_t *p)
{
	return crt_proc_memcpy(proc, proc_op, p, sizeof(*p));
}

static int
crt_proc_daos_epoch_range_t(crt_proc_t proc, crt_proc_op_t proc_op, daos_epoch_range_t *erange)
{
	int rc;

	rc = crt_proc_uint64_t(proc, proc_op, &erange->epr_lo);
	if (unlikely(rc))
		return rc;

	rc = crt_proc_uint64_t(proc, proc_op, &erange->epr_hi);
	if (unlikely(rc))
		return rc;

	return 0;
}

static int
pipeline_t_proc_consts(crt_proc_t proc, crt_proc_op_t proc_op, size_t num_constants,
		       d_iov_t **constants)
{
	int      rc = 0;
	size_t   i;
	d_iov_t *constant;

	if (DECODING(proc_op)) {
		D_ALLOC_ARRAY(*constants, num_constants);
		if (*constants == NULL)
			D_GOTO(exit, rc = -DER_NOMEM);
	}
	for (i = 0; i < num_constants; i++) {
		constant = &((*constants)[i]);

		rc       = crt_proc_d_iov_t(proc, proc_op, constant);
		if (unlikely(rc)) {
			if (DECODING(proc_op))
				D_GOTO(exit_free, rc);
			D_GOTO(exit, rc);
		}
	}

	if (FREEING(proc_op)) {
exit_free:
		D_FREE(*constants);
	}
exit:
	return rc;
}

static int
pipeline_t_proc_parts(crt_proc_t proc, crt_proc_op_t proc_op, uint32_t num_parts,
		      daos_filter_part_t ***parts)
{
	int                 rc = 0;
	uint32_t            i  = 0;
	uint32_t            j;
	daos_filter_part_t *part;

	if (DECODING(proc_op)) {
		D_ALLOC_ARRAY(*parts, num_parts);
		if (*parts == NULL)
			D_GOTO(exit, rc = -DER_NOMEM);
	}
	while (i < num_parts) {
		if (DECODING(proc_op)) {
			D_ALLOC((*parts)[i], sizeof(daos_filter_part_t));
			if ((*parts)[i] == NULL)
				D_GOTO(exit_free, rc = -DER_NOMEM);
		}
		part = (*parts)[i++];

		rc   = crt_proc_d_iov_t(proc, proc_op, &part->part_type);
		if (unlikely(rc)) {
			if (DECODING(proc_op))
				D_GOTO(exit_free, rc);
			D_GOTO(exit, rc);
		}
		rc = crt_proc_d_iov_t(proc, proc_op, &part->data_type);
		if (unlikely(rc)) {
			if (DECODING(proc_op))
				D_GOTO(exit_free, rc);
			D_GOTO(exit, rc);
		}
		rc = crt_proc_uint32_t(proc, proc_op, &part->num_operands);
		if (unlikely(rc)) {
			if (DECODING(proc_op))
				D_GOTO(exit_free, rc);
			D_GOTO(exit, rc);
		}
		rc = crt_proc_d_iov_t(proc, proc_op, &part->akey);
		if (unlikely(rc)) {
			if (DECODING(proc_op))
				D_GOTO(exit_free, rc);
			D_GOTO(exit, rc);
		}
		rc = crt_proc_uint64_t(proc, proc_op, &part->num_constants);
		if (unlikely(rc)) {
			if (DECODING(proc_op))
				D_GOTO(exit_free, rc);
			D_GOTO(exit, rc);
		}
		rc = pipeline_t_proc_consts(proc, proc_op, part->num_constants, &part->constant);
		if (unlikely(rc)) {
			if (DECODING(proc_op))
				D_GOTO(exit_free, rc);
			D_GOTO(exit, rc);
		}
		rc = crt_proc_uint64_t(proc, proc_op, &part->data_offset);
		if (unlikely(rc)) {
			if (DECODING(proc_op))
				D_GOTO(exit_free, rc);
			D_GOTO(exit, rc);
		}
		rc = crt_proc_uint64_t(proc, proc_op, &part->data_len);
		if (unlikely(rc)) {
			if (DECODING(proc_op))
				D_GOTO(exit_free, rc);
			D_GOTO(exit, rc);
		}
	}

	if (FREEING(proc_op)) {
exit_free:
		for (j = 0; j < i; j++)
			D_FREE((*parts)[j]);
		D_FREE(*parts);
	}
exit:
	return rc;
}

static int
pipeline_t_proc_filters(crt_proc_t proc, crt_proc_op_t proc_op, uint32_t num_filters,
			daos_filter_t ***filters)
{
	int            rc = 0;
	uint32_t       i  = 0;
	uint32_t       j;
	daos_filter_t *filter;

	if (DECODING(proc_op)) {
		D_ALLOC_ARRAY(*filters, num_filters);
		if (*filters == NULL)
			D_GOTO(exit, rc = -DER_NOMEM);
	}
	while (i < num_filters) {
		if (DECODING(proc_op)) {
			D_ALLOC((*filters)[i], sizeof(daos_filter_t));
			if ((*filters)[i] == NULL)
				D_GOTO(exit_free, rc = -DER_NOMEM);
		}
		filter = (*filters)[i++];

		rc     = crt_proc_d_iov_t(proc, proc_op, &filter->filter_type);
		if (unlikely(rc)) {
			if (DECODING(proc_op))
				D_GOTO(exit_free, rc);
			D_GOTO(exit, rc);
		}
		rc = crt_proc_uint32_t(proc, proc_op, &filter->num_parts);
		if (unlikely(rc)) {
			if (DECODING(proc_op))
				D_GOTO(exit_free, rc);
			D_GOTO(exit, rc);
		}
		rc = pipeline_t_proc_parts(proc, proc_op, filter->num_parts, &filter->parts);
		if (unlikely(rc)) {
			if (DECODING(proc_op))
				D_GOTO(exit_free, rc);
			D_GOTO(exit, rc);
		}
	}

	if (FREEING(proc_op)) {
exit_free:
		for (j = 0; j < i; j++)
			D_FREE((*filters)[j]);
		D_FREE(*filters);
	}
exit:
	return rc;
}

static int
crt_proc_daos_pipeline_t(crt_proc_t proc, crt_proc_op_t proc_op, daos_pipeline_t *pipe)
{
	int rc = 0;

	rc     = crt_proc_uint64_t(proc, proc_op, &pipe->version);
	if (unlikely(rc))
		D_GOTO(exit, rc);

	rc = crt_proc_uint32_t(proc, proc_op, &pipe->num_filters);
	if (unlikely(rc))
		D_GOTO(exit, rc);

	rc = pipeline_t_proc_filters(proc, proc_op, pipe->num_filters, &pipe->filters);
	if (unlikely(rc))
		D_GOTO(exit, rc);

	rc = crt_proc_uint32_t(proc, proc_op, &pipe->num_aggr_filters);
	if (unlikely(rc))
		D_GOTO(exit, rc);

	rc = pipeline_t_proc_filters(proc, proc_op, pipe->num_aggr_filters, &pipe->aggr_filters);
	if (unlikely(rc))
		D_GOTO(exit, rc);

exit:
	return rc;
}

static int
crt_proc_daos_pipeline_iods_t(crt_proc_t proc, crt_proc_op_t proc_op, daos_pipeline_iods_t *iods)
{
	int      rc = 0;
	uint32_t i  = 0;
	uint32_t j;

	rc = crt_proc_uint32_t(proc, proc_op, &iods->nr);
	if (unlikely(rc))
		D_GOTO(exit, rc);

	if (DECODING(proc_op)) {
		D_ALLOC_ARRAY(iods->iods, iods->nr);
		if (iods->iods == NULL)
			D_GOTO(exit, rc = -DER_NOMEM);
	}

	while (i < iods->nr) {
		rc = crt_proc_d_iov_t(proc, proc_op, &iods->iods[i].iod_name);
		if (unlikely(rc)) {
			if (DECODING(proc_op))
				D_GOTO(exit_free, rc);
			D_GOTO(exit, rc);
		}
		rc = crt_proc_memcpy(proc, proc_op, &iods->iods[i].iod_type,
				     sizeof(iods->iods[i].iod_type));
		if (unlikely(rc)) {
			if (DECODING(proc_op))
				D_GOTO(exit_free, rc);
			D_GOTO(exit, rc);
		}
		rc = crt_proc_uint64_t(proc, proc_op, &iods->iods[i].iod_size);
		if (unlikely(rc)) {
			if (DECODING(proc_op))
				D_GOTO(exit_free, rc);
			D_GOTO(exit, rc);
		}
		rc = crt_proc_uint64_t(proc, proc_op, &iods->iods[i].iod_flags);
		if (unlikely(rc)) {
			if (DECODING(proc_op))
				D_GOTO(exit_free, rc);
			D_GOTO(exit, rc);
		}
		rc = crt_proc_uint32_t(proc, proc_op, &iods->iods[i].iod_nr);
		if (unlikely(rc)) {
			if (DECODING(proc_op))
				D_GOTO(exit_free, rc);
			D_GOTO(exit, rc);
		}
		if (iods->iods[i].iod_type == DAOS_IOD_ARRAY && iods->iods[i].iod_nr > 0) {
			if (DECODING(proc_op)) {
				D_ALLOC_ARRAY(iods->iods[i].iod_recxs, iods->iods[i].iod_nr);
				if (iods->iods[i].iod_recxs == NULL)
					D_GOTO(exit_free, rc = -DER_NOMEM);
			}
			rc = crt_proc_memcpy(proc, proc_op, iods->iods[i].iod_recxs,
					     iods->iods[i].iod_nr *
						 sizeof(*iods->iods[i].iod_recxs));
			if (unlikely(rc)) {
				if (DECODING(proc_op)) {
					i++;
					D_GOTO(exit_free, rc);
				}
				D_GOTO(exit, rc);
			}
		}
		i++;
	}

	if (FREEING(proc_op)) {
exit_free:
		for (j = 0; j < i; j++)
			D_FREE(iods->iods[j].iod_recxs);
		D_FREE(iods->iods);
	}
exit:
	return rc;
}

static int
crt_proc_daos_pipeline_stats_t(crt_proc_t proc, crt_proc_op_t proc_op, daos_pipeline_stats_t *stats)
{
	int rc;

	rc = crt_proc_uint64_t(proc, proc_op, &stats->nr_objs);
	if (unlikely(rc))
		return rc;

	rc = crt_proc_uint64_t(proc, proc_op, &stats->nr_dkeys);
	if (unlikely(rc))
		return rc;

	rc = crt_proc_uint64_t(proc, proc_op, &stats->nr_akeys);
	if (unlikely(rc))
		return rc;

	return 0;
}

CRT_RPC_DEFINE(pipeline_run, DAOS_ISEQ_PIPELINE_RUN, DAOS_OSEQ_PIPELINE_RUN)

#define X(a, b, c, d, e, f)                                                                        \
	{                                                                                          \
	    .prf_flags   = b,                                                                      \
	    .prf_req_fmt = c,                                                                      \
	    .prf_hdlr    = NULL,                                                                   \
	    .prf_co_ops  = NULL,                                                                   \
	},
static struct crt_proto_rpc_format pipeline_proto_rpc_fmt[] = {PIPELINE_PROTO_CLI_RPC_LIST};
#undef X

struct crt_proto_format pipeline_proto_fmt = {.cpf_name  = "daos-pipeline",
					      .cpf_ver   = DAOS_PIPELINE_VERSION,
					      .cpf_count = ARRAY_SIZE(pipeline_proto_rpc_fmt),
					      .cpf_prf   = pipeline_proto_rpc_fmt,
					      .cpf_base =
						  DAOS_RPC_OPCODE(0, DAOS_PIPELINE_MODULE, 0)};
