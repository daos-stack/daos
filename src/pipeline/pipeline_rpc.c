/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define D_LOGFAC	DD_FAC(pipeline)

#include <daos/rpc.h>
#include <daos/pipeline.h>
#include <daos/common.h>
#include "pipeline_rpc.h"
#include <mercury_proc.h>

static int
pipeline_t_proc_consts(crt_proc_t proc, crt_proc_op_t proc_op,
		       size_t num_constants, d_iov_t **constants)
{
	int		rc = 0;
	size_t		i;
	d_iov_t		*constant;

	if (DECODING(proc_op))
	{
		D_ALLOC(*constants, num_constants * sizeof(d_iov_t));
	}
	for (i = 0; i < num_constants; i++)
	{
		constant = &((*constants)[i]);

		rc = crt_proc_d_iov_t(proc, proc_op, constant);
		if (unlikely(rc))
		{
			D_GOTO(exit, rc);
		}
	}

exit:
	return rc;
}

static int
pipeline_t_proc_parts(crt_proc_t proc, crt_proc_op_t proc_op,
		      uint32_t num_parts, daos_filter_part_t ***parts)
{
	int			rc = 0;
	uint32_t		i;
	daos_filter_part_t	*part;

	if (DECODING(proc_op))
	{
		D_ALLOC(*parts, num_parts * sizeof(daos_filter_part_t *));
	}
	for (i = 0; i < num_parts; i++)
	{
		if (DECODING(proc_op))
		{
			D_ALLOC((*parts)[i], sizeof(daos_filter_part_t));
		}
		part = (*parts)[i];

		rc = crt_proc_d_iov_t(proc, proc_op, &part->part_type);
		if (unlikely(rc))
		{
			D_GOTO(exit, rc);
		}
		rc = crt_proc_d_iov_t(proc, proc_op, &part->data_type);
		if (unlikely(rc))
		{
			D_GOTO(exit, rc);
		}
		rc = crt_proc_uint32_t(proc, proc_op, &part->num_operands);
		if (unlikely(rc))
		{
			D_GOTO(exit, rc);
		}
		rc = crt_proc_d_iov_t(proc, proc_op, &part->akey);
		if (unlikely(rc))
		{
			D_GOTO(exit, rc);
		}
		rc = crt_proc_uint64_t(proc, proc_op, &part->num_constants);
		if (unlikely(rc))
		{
			D_GOTO(exit, rc);
		}
		rc = pipeline_t_proc_consts(proc, proc_op, part->num_constants,
					    &part->constant);
		if (unlikely(rc))
		{
			D_GOTO(exit, rc);
		}
		rc = crt_proc_uint64_t(proc, proc_op, &part->data_offset);
		if (unlikely(rc))
		{
			D_GOTO(exit, rc);
		}
		rc = crt_proc_uint64_t(proc, proc_op, &part->data_len);
		if (unlikely(rc))
		{
			D_GOTO(exit, rc);
		}
	}

exit:
	return rc;
}

static int
pipeline_t_proc_filters(crt_proc_t proc, crt_proc_op_t proc_op,
			uint32_t num_filters, daos_filter_t ***filters)
{
	int		rc = 0;
	uint32_t	i;
	daos_filter_t	*filter;

	if (DECODING(proc_op))
	{
		D_ALLOC(*filters, num_filters * sizeof(daos_filter_t *));
	}
	for (i = 0; i < num_filters; i++)
	{
		if (DECODING(proc_op))
		{
			D_ALLOC((*filters)[i], sizeof(daos_filter_t));
		}
		filter = (*filters)[i];

		rc = crt_proc_d_iov_t(proc, proc_op, &filter->filter_type);
		if (unlikely(rc))
		{
			D_GOTO(exit, rc);
		}
		rc = crt_proc_uint32_t(proc, proc_op, &filter->num_parts);
		if (unlikely(rc))
		{
			D_GOTO(exit, rc);
		}
		rc = pipeline_t_proc_parts(proc, proc_op, filter->num_parts,
					   &filter->parts);
		if (unlikely(rc))
		{
			D_GOTO(exit, rc);
		}
	}

exit:
	return rc;
}

static int
crt_proc_daos_pipeline_t(crt_proc_t proc, crt_proc_op_t proc_op,
			 daos_pipeline_t *pipe)
{
	int	rc = 0;

	rc = crt_proc_uint64_t(proc, proc_op, &pipe->version);
	if (unlikely(rc))
	{
		D_GOTO(exit, rc);
	}
	rc = crt_proc_uint32_t(proc, proc_op, &pipe->num_filters);
	if (unlikely(rc))
	{
		D_GOTO(exit, rc);
	}
	rc = pipeline_t_proc_filters(proc, proc_op, pipe->num_filters,
				     &pipe->filters);
	if (unlikely(rc))
	{
		D_GOTO(exit, rc);
	}
	rc = crt_proc_uint32_t(proc, proc_op, &pipe->num_aggr_filters);
	if (unlikely(rc))
	{
		D_GOTO(exit, rc);
	}
	rc = pipeline_t_proc_filters(proc, proc_op, pipe->num_aggr_filters,
				     &pipe->aggr_filters);
	if (unlikely(rc))
	{
		D_GOTO(exit, rc);
	}

exit:
	return rc;
}


CRT_RPC_DEFINE(pipeline_run, DAOS_ISEQ_PIPELINE_RUN, DAOS_OSEQ_PIPELINE_RUN)

#define X(a, b, c, d, e, f)	\
{				\
	.prf_flags   = b,	\
	.prf_req_fmt = c,	\
	.prf_hdlr    = NULL,	\
	.prf_co_ops  = NULL,	\
},
static struct crt_proto_rpc_format pipeline_proto_rpc_fmt[] = {
	PIPELINE_PROTO_CLI_RPC_LIST
};
#undef X

struct crt_proto_format pipeline_proto_fmt = {
	.cpf_name  = "daos-pipeline",
	.cpf_ver   = DAOS_PIPELINE_VERSION,
	.cpf_count = ARRAY_SIZE(pipeline_proto_rpc_fmt),
	.cpf_prf   = pipeline_proto_rpc_fmt,
	.cpf_base  = DAOS_RPC_OPCODE(0, DAOS_PIPELINE_MODULE, 0)
};
