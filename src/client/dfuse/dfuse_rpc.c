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

#include "dfuse_common.h"
#include "dfuse_fs.h"

#define DFUSE_PROTO_SIGNON_BASE 0x02000000
#define DFUSE_PROTO_SIGNON_VERSION 3
#define DFUSE_PROTO_WRITE_BASE 0x01000000
#define DFUSE_PROTO_WRITE_VERSION 5
#define DFUSE_PROTO_IO_BASE 0x03000000
#define DFUSE_PROTO_IO_VERSION 2

/*
 * Re-use the CMF_UUID type when using a GAH as they are both 128 bit types
 * but define CMF_GAH here so it's clearer in the code what is happening.
 */
#define CMF_GAH CMF_UUID

int
crt_proc_struct_ios_name(crt_proc_t proc, void *arg)
{
	struct ios_name *data = arg;

	return crt_proc_memcpy(proc, data, sizeof(*data));
}

int
crt_proc_struct_ios_gah(crt_proc_t proc, void *arg)
{
	struct ios_gah *data = arg;

	return crt_proc_memcpy(proc, data, sizeof(*data));
}

int
dfuse_proc_stat(crt_proc_t proc, void *arg)
{
	struct stat *data = arg;

	return crt_proc_memcpy(proc, data, sizeof(*data));
}

struct crt_msg_field CMF_DFUSE_NAME = {
	.cmf_size = sizeof(struct ios_name),
	.cmf_proc = crt_proc_struct_ios_name,
};

struct crt_msg_field CMF_DFUSE_STAT = {
	.cmf_size = sizeof(struct stat),
	.cmf_proc = dfuse_proc_stat,
};

struct crt_msg_field *gah_string_in[] = {
	&CMF_GAH,	/* gah */
	&CMF_DFUSE_NAME,	/* name */
};

struct crt_msg_field *imigrate_in[] = {
	&CMF_GAH,	/* gah of parent */
	&CMF_DFUSE_NAME,	/* name */
	&CMF_INT,	/* inode */
};

struct crt_msg_field *string_out[] = {
	&CMF_STRING,
	&CMF_INT,
	&CMF_INT,
};

struct crt_msg_field *entry_out[] = {
	&CMF_GAH,		/* gah */
	&CMF_DFUSE_STAT,	/* struct stat */
	&CMF_INT,		/* rc */
	&CMF_INT,		/* err */
};

struct crt_msg_field *create_out[] = {
	&CMF_GAH,		/* gah */
	&CMF_GAH,		/* inode gah */
	&CMF_DFUSE_STAT,	/* struct stat */
	&CMF_INT,		/* rc */
	&CMF_INT,		/* err */
};

struct crt_msg_field *two_string_in[] = {
	&CMF_GAH,
	&CMF_DFUSE_NAME,
	&CMF_STRING,
};

struct crt_msg_field *create_in[] = {
	&CMF_GAH,		/* gah */
	&CMF_DFUSE_NAME,	/* name */
	&CMF_INT,		/* mode */
	&CMF_INT,		/* flags */
};

struct crt_msg_field *rename_in[] = {
	&CMF_GAH,		/* old parent */
	&CMF_GAH,		/* new parent */
	&CMF_DFUSE_NAME,	/* old name */
	&CMF_DFUSE_NAME,	/* new name */
	&CMF_INT,		/* flags */
};

struct crt_msg_field *open_in[] = {
	&CMF_GAH,	/* gah */
	&CMF_INT,	/* flags */
};

struct crt_msg_field *unlink_in[] = {
	&CMF_DFUSE_NAME,	/* name */
	&CMF_GAH,		/* gah */
	&CMF_INT,		/* flags */
};

struct crt_msg_field *attr_out[] = {
	&CMF_DFUSE_STAT,	/* stat */
	&CMF_INT,		/* rc */
	&CMF_INT,		/* err */
};

struct crt_msg_field *iov_pair[] = {
	&CMF_IOVEC,
	&CMF_INT,
	&CMF_INT
};

struct crt_msg_field *gah_pair[] = {
	&CMF_GAH,
	&CMF_INT,
	&CMF_INT
};

struct crt_msg_field *readdir_in[] = {
	&CMF_GAH,
	&CMF_BULK,
	&CMF_UINT64,
};

struct crt_msg_field *readdir_out[] = {
	&CMF_IOVEC,
	&CMF_INT,
	&CMF_INT,
	&CMF_INT,
	&CMF_INT,
};

CRT_GEN_PROC_FUNC(dfuse_xtvec, DFUSE_STRUCT_XTVEC);

CRT_RPC_DEFINE(dfuse_readx, DFUSE_RPC_READX_IN, DFUSE_RPC_READX_OUT);
CRT_RPC_DEFINE(dfuse_writex, DFUSE_RPC_WRITEX_IN, DFUSE_RPC_WRITEX_OUT);

struct crt_msg_field *status_out[] = {
	&CMF_INT,
	&CMF_INT,
};

struct crt_msg_field *gah_in[] = {
	&CMF_GAH,
};

struct crt_msg_field *writex_in[] = {
	&CMF_GAH,
	&CMF_IOVEC,
	&CMF_UINT64,
	&CMF_UINT64,
	&CMF_UINT64,
	&CMF_UINT64,
	&CMF_BULK,
	&CMF_BULK,
};

struct crt_msg_field *writex_out[] = {
	&CMF_UINT64,
	&CMF_INT,
	&CMF_INT,
	&CMF_UINT64,
	&CMF_UINT64,
};

struct crt_msg_field *setattr_in[] = {
	&CMF_GAH,		/* gah */
	&CMF_DFUSE_STAT,	/* struct stat */
	&CMF_UINT32,		/* to_set */
};

#define X(a, b, c)					\
	static struct crt_req_format DFUSE_CRF_##a =	\
		DEFINE_CRT_REQ_FMT(b, c);

DFUSE_RPCS_LIST

#undef X

#define X(a, b, c)					\
	{						\
		.prf_flags = CRT_RPC_FEAT_NO_TIMEOUT,	\
		.prf_req_fmt = &DFUSE_CRF_##a,	\
	},

static struct crt_proto_rpc_format dfuse_write_rpc_types[] = {
	DFUSE_RPCS_LIST
};

#undef X

static struct crt_proto_rpc_format dfuse_io_rpc_types[] = {
	{
		.prf_req_fmt = &CQF_dfuse_readx,
		.prf_flags = CRT_RPC_FEAT_NO_TIMEOUT,
	},
	{
		.prf_req_fmt = &CQF_dfuse_writex,
		.prf_flags = CRT_RPC_FEAT_NO_TIMEOUT,
	}
};

static struct crt_proto_format dfuse_write_registry = {
	.cpf_name = "DFUSE_METADATA",
	.cpf_ver = DFUSE_PROTO_WRITE_VERSION,
	.cpf_count = ARRAY_SIZE(dfuse_write_rpc_types),
	.cpf_prf = dfuse_write_rpc_types,
	.cpf_base = DFUSE_PROTO_WRITE_BASE,
};

static struct crt_proto_format dfuse_io_registry = {
	.cpf_name = "DFUSE_IO",
	.cpf_ver = DFUSE_PROTO_IO_VERSION,
	.cpf_count = ARRAY_SIZE(dfuse_io_rpc_types),
	.cpf_prf = dfuse_io_rpc_types,
	.cpf_base = DFUSE_PROTO_IO_BASE,
};

/* Bulk register a RPC type
 *
 * If there is a failure then register what is possible, and return
 * the first error that occurred.
 *
 * On the origin side the handlers array can be NULL, as no RPCs are expected
 * to be received.
 * On the target side proto can be NULL, as no RPCs are sent, so the opcodes
 * are not requried.
 */
static int
dfuse_core_register(struct crt_proto_format *reg,
		    struct crt_proto_format **proto,
		    crt_rpc_cb_t handlers[])
{
	int rc;
	int i;

	if (handlers)
		for (i = 0; i < reg->cpf_count; i++)
			reg->cpf_prf[i].prf_hdlr = handlers[i];

	rc = crt_proto_register(reg);

	if (proto && rc == -DER_SUCCESS)
		*proto = reg;

	return rc;
}

int
dfuse_io_register(struct crt_proto_format **proto,
		crt_rpc_cb_t handlers[])
{
	return dfuse_core_register(&dfuse_io_registry, proto, handlers);
}

struct sq_cb {
	struct dfuse_tracker	tracker;
	uint32_t		write_version;
	int			write_rc;
	uint32_t		io_version;
	int			io_rc;
};

static void
dfuse_write_query_cb(struct crt_proto_query_cb_info *cb_info)
{
	struct sq_cb *cbi = cb_info->pq_arg;

	cbi->write_rc = cb_info->pq_rc;
	if (cbi->write_rc == -DER_SUCCESS) {
		cbi->write_version = cb_info->pq_ver;
	}

	dfuse_tracker_signal(&cbi->tracker);
}

static void
dfuse_io_query_cb(struct crt_proto_query_cb_info *cb_info)
{
	struct sq_cb *cbi = cb_info->pq_arg;

	cbi->io_rc = cb_info->pq_rc;
	if (cbi->io_rc == -DER_SUCCESS) {
		cbi->io_version = cb_info->pq_ver;
	}

	dfuse_tracker_signal(&cbi->tracker);
}

/* Query the server side protocols in use, for now we only support one
 * version so check that with the server and ensure it's what we expect.
 *
 * Query both the protocols at the same time, by sending both requests
 * and then waiting for them both to complete.
 */
int
dfuse_client_register(crt_endpoint_t *tgt_ep,
		      struct crt_proto_format **write,
		      struct crt_proto_format **io)
{
	uint32_t write_ver = DFUSE_PROTO_WRITE_VERSION;
	uint32_t io_ver = DFUSE_PROTO_IO_VERSION;
	struct sq_cb cbi = {0};
	int rc;

	dfuse_tracker_init(&cbi.tracker, 2);

	rc = crt_proto_query(tgt_ep, DFUSE_PROTO_WRITE_BASE,
			     &write_ver, 1, dfuse_write_query_cb, &cbi);
	if (rc != -DER_SUCCESS) {
		dfuse_tracker_signal(&cbi.tracker);
		dfuse_tracker_signal(&cbi.tracker);
		dfuse_tracker_wait(&cbi.tracker);
		return rc;
	}

	rc = crt_proto_query(tgt_ep, DFUSE_PROTO_IO_BASE,
			     &io_ver, 1, dfuse_io_query_cb, &cbi);
	if (rc != -DER_SUCCESS) {
		dfuse_tracker_signal(&cbi.tracker);
		dfuse_tracker_wait(&cbi.tracker);
		return rc;
	}

	dfuse_tracker_wait(&cbi.tracker);

	if (cbi.write_rc != -DER_SUCCESS) {
		return cbi.write_rc;
	}

	if (cbi.io_rc != -DER_SUCCESS) {
		return cbi.io_rc;
	}

	if (cbi.write_version != DFUSE_PROTO_WRITE_VERSION) {
		return -DER_INVAL;
	}

	if (cbi.io_version != DFUSE_PROTO_IO_VERSION) {
		return -DER_INVAL;
	}

	rc = crt_proto_register(&dfuse_write_registry);
	if (rc != -DER_SUCCESS) {
		return rc;
	}

	rc = crt_proto_register(&dfuse_io_registry);
	if (rc != -DER_SUCCESS) {
		return rc;
	}

	*write	= &dfuse_write_registry;
	*io	= &dfuse_io_registry;

	return -DER_SUCCESS;
}
