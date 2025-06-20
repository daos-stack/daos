/*
 * (C) Copyright 2023-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "crt_perf.h"

#include <mercury_proc.h> /* for hg_proc_save_ptr() */
#include <unistd.h>
#include <getopt.h>

/****************/
/* Local Macros */
/****************/

#ifndef CART_VERSION
#define CART_VERSION "0.0.0"
#endif

#define NDIGITS               2
#define NWIDTH                27

#define CRT_PERF_GROUP_ID     "crt_perf"

#define CRT_PERF_BUF_SIZE_MAX (1 << 24)
#define CRT_PERF_BUF_COUNT    (64)

#define CRT_PERF_OPTS_DEFAULTS                                                                     \
	((struct crt_perf_opts){.comm         = NULL,                                              \
				.domain       = NULL,                                              \
				.protocol     = NULL,                                              \
				.hostname     = NULL,                                              \
				.port         = NULL,                                              \
				.attach_path  = NULL,                                              \
				.msg_size_max = 0,                                                 \
				.buf_size_min = 0,                                                 \
				.buf_size_max = CRT_PERF_BUF_SIZE_MAX,                             \
				.context_max  = 1,                                                 \
				.request_max  = 1,                                                 \
				.buf_count    = CRT_PERF_BUF_COUNT,                                \
				.loop         = 1,                                                 \
				.busy_wait    = false,                                             \
				.bidir        = false,                                             \
				.verify       = false,                                             \
				.mbps         = false})

/************************************/
/* Local Type and Struct Definition */
/************************************/

/********************/
/* Local Prototypes */
/********************/

static void
crt_perf_parse_options(int argc, char *argv[], struct crt_perf_opts *opts);

static void
crt_perf_free_options(struct crt_perf_opts *opts);

static size_t
crt_perf_parse_size(const char *str);

static void
crt_perf_usage(const char *execname);

static int
crt_perf_context_init(const struct crt_perf_info *perf_info, int context_id,
		      struct crt_perf_context_info *info);

static void
crt_perf_context_cleanup(struct crt_perf_context_info *info);

static int
crt_perf_group_save(const struct crt_perf_info *info);

static int
crt_perf_group_attach(struct crt_perf_info *info);

static int
crt_perf_bulk_buf_alloc(struct crt_perf_context_info *info, size_t bulk_handle_max,
			size_t buf_count, size_t buf_size_max, crt_bulk_perm_t bulk_perm,
			bool init_data);

static void
crt_perf_bulk_buf_free(struct crt_perf_context_info *info);

static void
crt_perf_init_data(void *buf, size_t buf_size);

static int
crt_perf_proc_iovec(crt_proc_t proc, void *data);

static int
crt_perf_proc_tags(crt_proc_t proc, void *data);

static int
crt_perf_proc_bulk_init_info(crt_proc_t proc, void *data);

static int
crt_perf_proc_bulk_info(crt_proc_t proc, void *data);

static int
crt_perf_tags_out(crt_rpc_t *rpc, void *arg);

static void
crt_perf_rpc_rate_cb(crt_rpc_t *rpc);

static void
crt_perf_done_cb(crt_rpc_t *rpc);

static void
crt_perf_tags_cb(crt_rpc_t *rpc);

static void
crt_perf_bulk_init_cb(crt_rpc_t *rpc);

static void
crt_perf_bulk_push_cb(crt_rpc_t *rpc);

static void
crt_perf_bulk_pull_cb(crt_rpc_t *rpc);

static void
crt_perf_bulk_common(crt_rpc_t *rpc, crt_bulk_op_t op);

static int
crt_perf_bulk_transfer_cb(const struct crt_bulk_cb_info *bulk_cb_info);

static int
crt_perf_send_rpc_wait(crt_context_t context, crt_endpoint_t *target_ep, crt_opcode_t opc,
		       int (*out_cb)(crt_rpc_t *, void *), void *out_arg);

/*******************/
/* Local Variables */
/*******************/

static const char   *crt_perf_short_options = "hc:d:p:H:P:l:bC:Z:y:z:w:x:BvMf:";

static struct option crt_perf_long_options[] = {
    {"help", no_argument, NULL, 'h'},
    {"comm", required_argument, NULL, 'c'},
    {"domain", required_argument, NULL, 'd'},
    {"protocol", required_argument, NULL, 'p'},
    {"hostname", required_argument, NULL, 'H'},
    {"port", required_argument, NULL, 'P'},
    {"loop", required_argument, NULL, 'l'},
    {"busy", no_argument, NULL, 'b'},
    {"contexts", required_argument, NULL, 'C'},
    {"msg_size", required_argument, NULL, 'Z'},
    {"buf_size_min", required_argument, NULL, 'y'},
    {"buf_size_max", required_argument, NULL, 'z'},
    {"buf_count", required_argument, NULL, 'w'},
    {"requests", required_argument, NULL, 'x'},
    {"bidirectional", no_argument, NULL, 'B'},
    {"verify", no_argument, NULL, 'v'},
    {"millionbps", no_argument, NULL, 'M'},
    {"hostfile", required_argument, NULL, 'f'},
};

/* TODO keep global until we can retrieve user data from cart context */
static struct crt_perf_info *perf_info_g;

/* clang-format off */
static struct crt_req_format crt_perf_no_arg = {
    .crf_proc_in  = NULL,
	.crf_proc_out = NULL,
	.crf_size_in  = 0,
	.crf_size_out = 0
};

static struct crt_req_format crt_perf_rate = {
    .crf_proc_in  = crt_perf_proc_iovec,
	.crf_proc_out = NULL,
	.crf_size_in  = sizeof(struct iovec),
	.crf_size_out = 0
};

static struct crt_req_format crt_perf_rate_bidir = {
    .crf_proc_in  = crt_perf_proc_iovec,
	.crf_proc_out = crt_perf_proc_iovec,
	.crf_size_in  = sizeof(struct iovec),
	.crf_size_out = sizeof(struct iovec)
};

static struct crt_req_format crt_perf_tags = {
    .crf_proc_in  = NULL,
	.crf_proc_out = crt_perf_proc_tags,
	.crf_size_in  = 0,
	.crf_size_out = sizeof(uint32_t)
};

static struct crt_req_format crt_perf_bulk_init = {
    .crf_proc_in  = crt_perf_proc_bulk_init_info,
	.crf_proc_out = NULL,
	.crf_size_in  = sizeof(struct crt_perf_bulk_init_info),
	.crf_size_out = 0
};

static struct crt_req_format crt_perf_bulk_bw = {
    .crf_proc_in  = crt_perf_proc_bulk_info,
	.crf_proc_out = NULL,
	.crf_size_in  = sizeof(struct crt_perf_bulk_info),
	.crf_size_out = 0
};

static struct crt_proto_rpc_format crt_perf_rpcs[] = {
    {
        .prf_req_fmt = &crt_perf_rate,
		.prf_hdlr    = crt_perf_rpc_rate_cb,
		.prf_co_ops  = NULL,
		.prf_flags   = 0
    },
    {
        .prf_req_fmt = &crt_perf_no_arg,
		.prf_hdlr    = crt_perf_done_cb,
		.prf_co_ops  = NULL,
		.prf_flags   = 0
    },
    {
        .prf_req_fmt = &crt_perf_tags,
		.prf_hdlr    = crt_perf_tags_cb,
		.prf_co_ops  = NULL,
		.prf_flags   = 0
    },
	{
        .prf_req_fmt = &crt_perf_bulk_init,
		.prf_hdlr    = crt_perf_bulk_init_cb,
		.prf_co_ops  = NULL,
		.prf_flags   = 0
    },
	{
        .prf_req_fmt = &crt_perf_bulk_bw,
		.prf_hdlr    = crt_perf_bulk_push_cb,
		.prf_co_ops  = NULL,
		.prf_flags   = 0
    },
	{
        .prf_req_fmt = &crt_perf_bulk_bw,
		.prf_hdlr    = crt_perf_bulk_pull_cb,
		.prf_co_ops  = NULL,
		.prf_flags   = 0
    }
};

static struct crt_proto_format crt_perf_protocol = {
    .cpf_name  = "crt_perf_protocol",
	.cpf_ver   = CRT_PERF_RPC_VERSION,
	.cpf_count = ARRAY_SIZE(crt_perf_rpcs),
	.cpf_prf   = &crt_perf_rpcs[0],
	.cpf_base  = CRT_PERF_BASE_OPC
};
/* clang-format on */

static void
crt_perf_parse_options(int argc, char *argv[], struct crt_perf_opts *opts)
{
	int opt;

	while ((opt = getopt_long(argc, argv, crt_perf_short_options, crt_perf_long_options,
				  NULL)) != -1) {
		switch (opt) {
		case 'c': /* comm */
			opts->comm = strdup(optarg);
			break;

		case 'd': /* domain */
			opts->domain = strdup(optarg);
			break;

		case 'p': /* protocol */
			opts->protocol = strdup(optarg);
			break;

		case 'H': /* hostname */
			opts->hostname = strdup(optarg);
			break;

		case 'P': /* port */
			opts->port = strdup(optarg);
			break;

		case 'l': /* loop */
			opts->loop = atoi(optarg);
			break;

		case 'b': /* busy wait */
			opts->busy_wait = true;
			break;

		case 'C': /* context max */
			opts->context_max = (size_t)atoi(optarg);
			break;

		case 'Z': /* msg size */
			opts->msg_size_max = crt_perf_parse_size(optarg);
			break;

		case 'y': /* min buffer size */
			opts->buf_size_min = crt_perf_parse_size(optarg);
			break;

		case 'z': /* max buffer size */
			opts->buf_size_max = crt_perf_parse_size(optarg);
			break;

		case 'w': /* buffer count */
			opts->buf_count = (size_t)atol(optarg);
			break;

		case 'x': /* request max */
			opts->request_max = (size_t)atoi(optarg);
			break;

		case 'B': /* bidirectional */
			opts->bidir = true;
			break;

		case 'v': /* verify */
			opts->verify = true;
			break;

		case 'M': /* OSU-style output MB/s */
			opts->mbps = true;
			break;

		case 'f': /* hostfile */
			opts->attach_path = strdup(optarg);
			break;

		case 'h':
		default:
			crt_perf_usage(argv[0]);
			exit(EXIT_FAILURE);
		}
	}

	if ((argc - optind) > 1) {
		crt_perf_usage(argv[0]);
		exit(EXIT_FAILURE);
	}
}

static void
crt_perf_free_options(struct crt_perf_opts *opts)
{
	if (opts->comm != NULL) {
		free(opts->comm);
		opts->comm = NULL;
	}
	if (opts->domain != NULL) {
		free(opts->domain);
		opts->domain = NULL;
	}
	if (opts->protocol != NULL) {
		free(opts->protocol);
		opts->protocol = NULL;
	}
	if (opts->hostname != NULL) {
		free(opts->hostname);
		opts->hostname = NULL;
	}
	if (opts->port != NULL) {
		free(opts->port);
		opts->port = NULL;
	}
	if (opts->attach_path != NULL) {
		free(opts->attach_path);
		opts->attach_path = NULL;
	}
}

static size_t
crt_perf_parse_size(const char *str)
{
	size_t size;
	char   prefix;

	if (sscanf(str, "%zu%c", &size, &prefix) == 2) {
		switch (prefix) {
		case 'k':
			size *= 1024;
			break;
		case 'm':
			size *= (1024 * 1024);
			break;
		case 'g':
			size *= (1024 * 1024 * 1024);
			break;
		default:
			break;
		}
		return size;
	} else if (sscanf(str, "%zu", &size) == 1)
		return size;
	else
		return 0;
}

static void
crt_perf_usage(const char *execname)
{
	printf("usage: %s [OPTIONS]\n", execname);
	printf("    OPTIONS\n");
	printf("    -h, --help           Print a usage message and exit\n");
	printf("    -c, --comm           Select NA plugin\n"
	       "                         NA plugins: ofi, ucx, etc\n");
	printf("    -d, --domain         Select NA OFI domain\n");
	printf("    -p, --protocol       Select plugin protocol\n"
	       "                         Available protocols: tcp, verbs, etc\n");
	printf("    -H, --hostname       Select hostname / IP address to use\n"
	       "                         Default: any\n");
	printf("    -P, --port           Select port to use\n"
	       "                         Default: any\n");
	printf("    -l, --loop           Number of loops (default: 1)\n");
	printf("    -b, --busy           Busy wait\n");
	printf("    -C, --contexts       Number of contexts (default: 1)\n");
	printf("    -Z, --msg_size       Unexpected/expected msg size if different than default\n");
	printf("    -y  --buf_size_min   Min buffer size (in bytes)\n");
	printf("    -z, --buf_size_max   Max buffer size (in bytes)\n");
	printf("    -w  --buf_count      Number of buffers used\n");
	printf("    -x, --requests       Max number of in-flight requests\n");
	printf("    -B, --bidirectional  Bidirectional communication\n");
	printf("    -v, --verify         Verify data\n");
	printf("    -M, --mbps           Output in MB/s instead of MiB/s\n");
	printf("    -f, --hostfile       Specify attach info path\n");
}

static int
crt_perf_context_init(const struct crt_perf_info *perf_info, int context_id,
		      struct crt_perf_context_info *info)
{
	int ctx_idx;
	int rc;

	rc = crt_context_create(&info->context);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not create context");

	rc = crt_context_idx(info->context, &ctx_idx);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not query context index");

	CRT_PERF_CHECK_ERROR(context_id != ctx_idx, error, rc, -DER_MISMATCH,
			     "context_id %d != ctx_idx %d", context_id, ctx_idx);
	info->context_id = context_id;

	info->requests = calloc(perf_info->opts.request_max, sizeof(*info->requests));
	CRT_PERF_CHECK_ERROR(info->requests == NULL, error, rc, -DER_NOMEM,
			     "Could not allocate request array of size %zu",
			     perf_info->opts.request_max);

	return 0;

error:
	crt_perf_context_cleanup(info);
	return rc;
}

static void
crt_perf_context_cleanup(struct crt_perf_context_info *info)
{
	if (info->remote_bulk_handles != NULL) {
		size_t i;
		for (i = 0; i < info->bulk_handle_max; i++)
			crt_bulk_free(info->remote_bulk_handles[i]);
		free(info->remote_bulk_handles);
	}

	crt_perf_bulk_buf_free(info);

	if (info->context != NULL) {
		(void)crt_context_destroy(info->context, 1);
		info->context = NULL;
	}

	if (info->requests != NULL) {
		free(info->requests);
		info->requests = NULL;
	}
	if (info->rpc_buf != NULL) {
		free(info->rpc_buf);
		info->rpc_buf = NULL;
	}
}

static int
crt_perf_group_save(const struct crt_perf_info *info)
{
	char *uri_list = NULL;
	int   rc;

	if (info->mpi_info.size > 1) {
		char  uri_name[128];
		char *uri;
		int   rank;

		rc = crt_self_uri_get(0, &uri);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not get self uri");
		memset(uri_name, '\0', sizeof(uri_name));
		strcpy(uri_name, uri);
		free(uri);

		uri_list = malloc(sizeof(uri_name) * info->mpi_info.size);
		CRT_PERF_CHECK_ERROR(uri_list == NULL, error, rc, -DER_NOMEM,
				     "could not allocate array of size %zu",
				     sizeof(uri_name) * info->mpi_info.size);

		rc = crt_perf_mpi_allgather(&info->mpi_info, uri_name, sizeof(uri_name), uri_list,
					    sizeof(uri_name));
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not allgather uris");

		for (rank = 0; rank < info->mpi_info.size; rank++) {
			char *rank_uri = uri_list + rank * sizeof(uri_name);

			if (rank == info->mpi_info.rank)
				continue; /* our rank is already added */

			rc = crt_group_primary_rank_add(info->context_info[0].context, NULL, rank,
							rank_uri);
			CRT_PERF_CHECK_D_ERROR(error, rc, "could not add rank %d %s", rank,
					       rank_uri);
		}
	}

	if (info->mpi_info.rank == 0) {
		rc = crt_group_config_save(NULL, true);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not save group config");
	}

	free(uri_list);

	return 0;

error:
	free(uri_list);

	return rc;
}

static int
crt_perf_group_attach(struct crt_perf_info *info)
{
	int rc;

	rc = crt_group_attach(CRT_PERF_GROUP_ID, &info->ep_group);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not attach to group %s", CRT_PERF_GROUP_ID);

	rc = crt_group_size(info->ep_group, &info->ep_ranks);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not query group size");
	CRT_PERF_CHECK_ERROR(info->ep_ranks == 0, error, rc, -DER_INVAL, "ep ranks cannot be zero");

	if (info->mpi_info.rank == 0) {
		crt_endpoint_t target_ep = {.ep_grp = info->ep_group, .ep_rank = 0, .ep_tag = 0};
		rc = crt_perf_send_rpc_wait(info->context_info[0].context, &target_ep,
					    CRT_PERF_ID(CRT_PERF_TAGS), crt_perf_tags_out,
					    &info->ep_tags);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not query tags");
		CRT_PERF_CHECK_ERROR(info->ep_tags == 0, error, rc, -DER_INVAL,
				     "ep tags cannot be zero");

		printf("# %" PRIu32 " target rank(s) read - %" PRIu32 " tag(s)\n", info->ep_ranks,
		       info->ep_tags);
	}
	if (info->mpi_info.size > 1) {
		rc = crt_perf_mpi_bcast(&info->mpi_info, &info->ep_tags, sizeof(info->ep_tags), 0);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not bcast ep_tags");
	}

	return 0;

error:
	return rc;
}

static int
crt_perf_bulk_buf_alloc(struct crt_perf_context_info *info, size_t bulk_handle_max,
			size_t buf_count, size_t buf_size_max, crt_bulk_perm_t bulk_perm,
			bool init_data)
{
	size_t page_size = sysconf(_SC_PAGE_SIZE);
	int    rc;
	size_t i;

	info->bulk_handle_max = bulk_handle_max;
	info->buf_count       = buf_count;
	info->buf_size_max    = buf_size_max;

	info->bulk_bufs = (void **)calloc(bulk_handle_max, sizeof(*info->bulk_bufs));
	CRT_PERF_CHECK_ERROR(info->bulk_bufs == NULL, error, rc, -DER_NOMEM, "malloc(%zu) failed",
			     bulk_handle_max * sizeof(*info->bulk_bufs));

	info->local_bulk_handles =
	    (crt_bulk_t *)calloc(bulk_handle_max, sizeof(*info->local_bulk_handles));
	CRT_PERF_CHECK_ERROR(info->local_bulk_handles == NULL, error, rc, -DER_NOMEM,
			     "calloc(%zu) failed",
			     bulk_handle_max * sizeof(*info->local_bulk_handles));

	for (i = 0; i < bulk_handle_max; i++) {
		size_t  alloc_size = buf_size_max * buf_count;
		d_iov_t iov = {.iov_buf = NULL, .iov_buf_len = alloc_size, .iov_len = alloc_size};
		d_sg_list_t sgl = {.sg_nr = 1, .sg_nr_out = 0, .sg_iovs = &iov};

		/* Prepare buf */
		info->bulk_bufs[i] = aligned_alloc(page_size, alloc_size);
		CRT_PERF_CHECK_ERROR(info->bulk_bufs[i] == NULL, error, rc, -DER_NOMEM,
				     "aligned_alloc(%zu, %zu) failed", page_size, buf_size_max);
		iov.iov_buf = info->bulk_bufs[i];

		/* Initialize data */
		if (init_data)
			crt_perf_init_data(info->bulk_bufs[i], alloc_size);

		rc = crt_bulk_create(info->context, &sgl, bulk_perm, &info->local_bulk_handles[i]);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not create bulk handle");
	}

	return 0;

error:
	if (info->local_bulk_handles != NULL) {
		for (i = 0; i < bulk_handle_max; i++)
			(void)crt_bulk_free(info->local_bulk_handles[i]);
		free(info->local_bulk_handles);
		info->local_bulk_handles = NULL;
	}

	if (info->bulk_bufs != NULL) {
		for (i = 0; i < bulk_handle_max; i++)
			free(info->bulk_bufs[i]);
		free(info->bulk_bufs);
		info->bulk_bufs = NULL;
	}

	return rc;
}

static void
crt_perf_bulk_buf_free(struct crt_perf_context_info *info)
{
	size_t i;

	if (info->local_bulk_handles != NULL) {
		for (i = 0; i < info->bulk_handle_max; i++)
			(void)crt_bulk_free(info->local_bulk_handles[i]);
		free(info->local_bulk_handles);
		info->local_bulk_handles = NULL;
	}

	if (info->bulk_bufs != NULL) {
		for (i = 0; i < info->bulk_handle_max; i++)
			free(info->bulk_bufs[i]);
		free(info->bulk_bufs);
		info->bulk_bufs = NULL;
	}
}

static void
crt_perf_init_data(void *buf, size_t buf_size)
{
	char  *buf_ptr = (char *)buf;
	size_t i;

	for (i = 0; i < buf_size; i++)
		buf_ptr[i] = (char)i;
}

static int
crt_perf_proc_iovec(crt_proc_t proc, void *data)
{
	struct iovec *iov = (struct iovec *)data;
	crt_proc_op_t proc_op;
	uint32_t      len = (uint32_t)iov->iov_len;
	int           rc;

	CRT_PERF_CHECK_ERROR(proc == NULL || iov == NULL, error, rc, -DER_INVAL, "NULL arguments");

	rc = crt_proc_get_op(proc, &proc_op);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not get proc op");

	if (FREEING(proc_op)) {
		iov->iov_base = NULL;
		iov->iov_len  = 0;
		return 0;
	}

	rc = crt_proc_uint32_t(proc, proc_op, &len);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not proc len");

	if (len == 0)
		return 0;

	if (DECODING(proc_op)) {
		iov->iov_len = (size_t)len;
		/**
		 * Don't allocate/memcpy like we do for others.
		 * Just point at memory in request buffer instead.
		 */
		iov->iov_base = hg_proc_save_ptr(proc, iov->iov_len);
		CRT_PERF_CHECK_ERROR(iov->iov_base == NULL, error, rc, -DER_INVAL,
				     "could not proc save ptr");
	} else { /* ENCODING(proc_op) */
		rc = crt_proc_memcpy(proc, proc_op, iov->iov_base, iov->iov_len);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not proc memcpy");
	}

	return 0;

error:
	return rc;
}

static int
crt_perf_proc_tags(crt_proc_t proc, void *data)
{
	uint32_t     *tags = (uint32_t *)data;
	crt_proc_op_t proc_op;
	int           rc;

	CRT_PERF_CHECK_ERROR(proc == NULL || tags == NULL, error, rc, -DER_INVAL, "NULL arguments");

	rc = crt_proc_get_op(proc, &proc_op);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not get proc op");

	if (FREEING(proc_op))
		return 0;

	rc = crt_proc_uint32_t(proc, proc_op, tags);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not proc tags");

	return 0;

error:
	return rc;
}

static int
crt_perf_proc_bulk_init_info(crt_proc_t proc, void *data)
{
	struct crt_perf_bulk_init_info *info = (struct crt_perf_bulk_init_info *)data;
	crt_proc_op_t                   proc_op;
	int                             rc;

	CRT_PERF_CHECK_ERROR(proc == NULL || data == NULL, error, rc, -DER_INVAL, "NULL arguments");

	rc = crt_proc_get_op(proc, &proc_op);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not get proc op");

	rc = crt_proc_crt_bulk_t(proc, proc_op, &info->bulk);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not proc bulk");

	rc = crt_proc_uint32_t(proc, proc_op, &info->bulk_op);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not proc bulk op");

	rc = crt_proc_uint32_t(proc, proc_op, &info->handle_id);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not proc handle id");

	rc = crt_proc_uint32_t(proc, proc_op, &info->request_max);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not proc request max");

	rc = crt_proc_uint32_t(proc, proc_op, &info->buf_count);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not proc buf count");

	rc = crt_proc_uint32_t(proc, proc_op, &info->size_max);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not proc size max");

	rc = crt_proc_uint32_t(proc, proc_op, &info->comm_size);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not proc comm size");

	rc = crt_proc_uint32_t(proc, proc_op, &info->target_rank);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not proc target rank");

	rc = crt_proc_uint32_t(proc, proc_op, &info->target_max);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not proc target max");

	return 0;

error:
	return rc;
}

static int
crt_perf_proc_bulk_info(crt_proc_t proc, void *data)
{
	struct crt_perf_bulk_info *info = (struct crt_perf_bulk_info *)data;
	crt_proc_op_t              proc_op;
	int                        rc;

	CRT_PERF_CHECK_ERROR(proc == NULL || data == NULL, error, rc, -DER_INVAL, "NULL arguments");

	rc = crt_proc_get_op(proc, &proc_op);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not get proc op");

	rc = crt_proc_uint32_t(proc, proc_op, &info->handle_id);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not proc handle id");

	rc = crt_proc_uint32_t(proc, proc_op, &info->size);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not proc size");

	return 0;

error:
	return rc;
}

static int
crt_perf_tags_out(crt_rpc_t *rpc, void *arg)
{
	uint32_t *tags;
	int       rc;

	tags = crt_reply_get(rpc);
	CRT_PERF_CHECK_ERROR(tags == NULL, error, rc, -DER_INVAL,
			     "could not retrieve rpc response");

	*(uint32_t *)arg = *tags;

	return 0;

error:
	return rc;
}

static void
crt_perf_rpc_rate_cb(crt_rpc_t *rpc)
{
	const struct crt_perf_opts *opts = &perf_info_g->opts;
	struct iovec               *in_iov;
	int                         rc;

	/* Get input struct */
	in_iov = crt_req_get(rpc);
	CRT_PERF_CHECK_ERROR(in_iov == NULL, error, rc, -DER_INVAL,
			     "could not retrieve rpc request");

	if (opts->verify) {
		rc = crt_perf_verify_data(in_iov->iov_base, in_iov->iov_len);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not verify data");
	}

	/* Send response back */
	if (opts->bidir) {
		struct iovec *out_iov = (struct iovec *)crt_reply_get(rpc);
		CRT_PERF_CHECK_ERROR(out_iov == NULL, error, rc, -DER_INVAL,
				     "could not retrieve rpc response");

		out_iov->iov_base = in_iov->iov_base;
		out_iov->iov_len  = in_iov->iov_len;
	}

	rc = crt_reply_send(rpc);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not send response");

	return;

error:
	return;
}

static void
crt_perf_done_cb(crt_rpc_t *rpc)
{
	struct crt_perf_context_info *info;
	int                           ctx_idx;
	int                           rc;

	rc = crt_context_idx(rpc->cr_ctx, &ctx_idx);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not query context index");

	info = (struct crt_perf_context_info *)&perf_info_g->context_info[ctx_idx];

	/* Set done for context data */
	info->done = true;

	/* Send response back */
	rc = crt_reply_send(rpc);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not send response");

	return;

error:
	return;
}

static void
crt_perf_tags_cb(crt_rpc_t *rpc)
{
	uint32_t *tags_p;
	int       rc;

	tags_p = (uint32_t *)crt_reply_get(rpc);
	CRT_PERF_CHECK_ERROR(tags_p == NULL, error, rc, -DER_INVAL,
			     "could not retrieve rpc response");
	*tags_p = (uint32_t)perf_info_g->opts.context_max;

	/* Send response back */
	rc = crt_reply_send(rpc);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not send response");

	return;

error:
	return;
}

static void
crt_perf_bulk_init_cb(crt_rpc_t *rpc)
{
	struct crt_perf_context_info   *info;
	struct crt_perf_bulk_init_info *bulk_info;
	int                             ctx_idx;
	int                             rc;

	rc = crt_context_idx(rpc->cr_ctx, &ctx_idx);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not query context index");

	info = (struct crt_perf_context_info *)&perf_info_g->context_info[ctx_idx];

	/* Get input struct */
	bulk_info = crt_req_get(rpc);
	CRT_PERF_CHECK_ERROR(bulk_info == NULL, error, rc, -DER_INVAL,
			     "could not retrieve rpc request");

	if (info->bulk_bufs == NULL) {
		crt_bulk_perm_t bulk_perm =
		    (bulk_info->bulk_op == CRT_BULK_GET) ? CRT_BULK_WO : CRT_BULK_RO;
		size_t bulk_handle_max =
		    (bulk_info->request_max * bulk_info->comm_size) / bulk_info->target_max;

		if (((bulk_info->request_max * bulk_info->comm_size) % bulk_info->target_max) >
		    bulk_info->target_rank)
			bulk_handle_max++;

		D_INFO("(%d,%" PRIu32 ") number of handles is %zu\n", info->context_id,
		       bulk_info->target_rank, bulk_handle_max);

		rc = crt_perf_bulk_buf_alloc(info, bulk_handle_max, bulk_info->buf_count,
					     bulk_info->size_max, bulk_perm,
					     bulk_info->bulk_op == CRT_BULK_PUT);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not allocate bulk buffers");

		info->remote_bulk_handles =
		    (crt_bulk_t *)calloc(bulk_handle_max, sizeof(*info->remote_bulk_handles));
		CRT_PERF_CHECK_ERROR(info->remote_bulk_handles == NULL, error, rc, -DER_NOMEM,
				     "malloc(%zu) failed",
				     bulk_handle_max * sizeof(*info->remote_bulk_handles));

		info->bulk_requests = (struct crt_perf_request *)calloc(
		    bulk_handle_max, sizeof(*info->bulk_requests));
		CRT_PERF_CHECK_ERROR(info->bulk_requests == NULL, error, rc, -DER_NOMEM,
				     "malloc(%zu) failed",
				     bulk_handle_max * sizeof(*info->bulk_requests));
	}

	CRT_PERF_CHECK_ERROR(bulk_info->handle_id >= info->bulk_handle_max, error, rc,
			     -DER_OVERFLOW, "(%d,%" PRIu32 ") Handle ID is %" PRIu32 " >= %zu",
			     info->context_id, bulk_info->target_rank, bulk_info->handle_id,
			     info->bulk_handle_max);
	info->remote_bulk_handles[bulk_info->handle_id] = bulk_info->bulk;
	crt_bulk_addref(bulk_info->bulk);

	/* Send response back */
	rc = crt_reply_send(rpc);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not send response");

	return;

error:
	return;
}

static void
crt_perf_bulk_push_cb(crt_rpc_t *rpc)
{
	crt_perf_bulk_common(rpc, CRT_BULK_PUT);
}

static void
crt_perf_bulk_pull_cb(crt_rpc_t *rpc)
{
	crt_perf_bulk_common(rpc, CRT_BULK_GET);
}

static void
crt_perf_bulk_common(crt_rpc_t *rpc, crt_bulk_op_t op)
{
	struct crt_perf_context_info *info;
	struct crt_perf_bulk_info    *bulk_info;
	size_t                        i;
	int                           ctx_idx;
	int                           rc;

	rc = crt_context_idx(rpc->cr_ctx, &ctx_idx);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not query context index");

	info = (struct crt_perf_context_info *)&perf_info_g->context_info[ctx_idx];

	/* Get input struct */
	bulk_info = crt_req_get(rpc);
	CRT_PERF_CHECK_ERROR(bulk_info == NULL, error, rc, -DER_INVAL,
			     "could not retrieve rpc request");

	/* Keep RPC refcount */
	crt_req_addref(rpc);

	/* Initialize request */
	info->bulk_requests[bulk_info->handle_id] =
	    (struct crt_perf_request){.expected_count = (int32_t)info->buf_count,
				      .complete_count = 0,
				      .rc             = 0,
				      .done           = false,
				      .cb             = NULL,
				      .arg            = info};

	/* Post bulk push */
	for (i = 0; i < info->buf_count; i++) {
		struct crt_bulk_desc bulk_desc = {
		    .bd_rpc        = rpc,
		    .bd_bulk_op    = op,
		    .bd_remote_hdl = info->remote_bulk_handles[bulk_info->handle_id],
		    .bd_remote_off = i * info->buf_size_max,
		    .bd_local_hdl  = info->local_bulk_handles[bulk_info->handle_id],
		    .bd_local_off  = i * info->buf_size_max,
		    .bd_len        = bulk_info->size};

		rc = crt_bulk_transfer(&bulk_desc, crt_perf_bulk_transfer_cb,
				       &info->bulk_requests[bulk_info->handle_id], NULL);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not issue bulk transfer");
	}

	return;

error:
	return;
}

static int
crt_perf_bulk_transfer_cb(const struct crt_bulk_cb_info *bulk_cb_info)
{
	const struct crt_perf_opts *opts         = &perf_info_g->opts;
	struct crt_perf_request    *bulk_request = (struct crt_perf_request *)bulk_cb_info->bci_arg;
	int                         rc;

	CRT_PERF_CHECK_ERROR(bulk_cb_info->bci_rc != 0, done, rc, bulk_cb_info->bci_rc,
			     "bulk transfer failed");

done:
	if ((++bulk_request->complete_count) == bulk_request->expected_count) {
		bulk_request->done = true;

		if (bulk_cb_info->bci_bulk_desc->bd_bulk_op == CRT_BULK_GET && opts->verify) {
			const struct crt_perf_context_info *info =
			    (const struct crt_perf_context_info *)bulk_request->arg;
			d_iov_t     iov = {.iov_buf = NULL, .iov_buf_len = 0, .iov_len = 0};
			d_sg_list_t sgl = {.sg_nr = 1, .sg_nr_out = 0, .sg_iovs = &iov};
			size_t      i;

			rc = crt_bulk_access(bulk_cb_info->bci_bulk_desc->bd_local_hdl, &sgl);
			CRT_PERF_CHECK_D_ERROR(error, rc, "could not access bulk handle");

			CRT_PERF_CHECK_ERROR(sgl.sg_nr_out != 1, error, rc, -DER_INVAL,
					     "sgl.sg_nr_out=%" PRIu32, sgl.sg_nr_out);
			CRT_PERF_CHECK_ERROR(
			    sgl.sg_iovs[0].iov_len != info->buf_size_max * info->buf_count, error,
			    rc, -DER_INVAL, "sgl.sg_iovs[0].iov_len=%zu", sgl.sg_iovs[0].iov_len);

			for (i = 0; i < info->buf_count; i++) {
				char *buf_p =
				    (char *)sgl.sg_iovs[0].iov_buf + info->buf_size_max * i;
				rc = crt_perf_verify_data(buf_p,
							  bulk_cb_info->bci_bulk_desc->bd_len);
				CRT_PERF_CHECK_D_ERROR(error, rc, "could not verify data");
			}
		}

		(void)crt_reply_send(bulk_cb_info->bci_bulk_desc->bd_rpc);
		crt_req_decref(bulk_cb_info->bci_bulk_desc->bd_rpc);
	}

	return 0;

error:
	(void)crt_reply_send(bulk_cb_info->bci_bulk_desc->bd_rpc);
	crt_req_decref(bulk_cb_info->bci_bulk_desc->bd_rpc);

	return rc;
}

static int
crt_perf_send_rpc_wait(crt_context_t context, crt_endpoint_t *target_ep, crt_opcode_t opc,
		       int (*out_cb)(crt_rpc_t *, void *), void *out_arg)
{
	struct crt_perf_request args = {.expected_count = 1,
					.complete_count = 0,
					.rc             = 0,
					.done           = false,
					.cb             = out_cb,
					.arg            = out_arg};
	crt_rpc_t              *request;
	int                     rc;

	rc = crt_req_create(context, target_ep, opc, &request);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not create request");

	rc = crt_req_send(request, crt_perf_request_complete, &args);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not send request to %" PRIu32 ":%" PRIu32,
			       target_ep->ep_rank, target_ep->ep_tag);

	while (!args.done) {
		rc = crt_progress(context, CRT_PERF_TIMEOUT);
		if (rc == -DER_TIMEDOUT)
			continue;
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not make progress");
	}

	return 0;

error:
	return rc;
}

int
crt_perf_init(int argc, char *argv[], bool listen, struct crt_perf_info *info)
{
	struct crt_init_options crt_init_options;
	const char             *attach_info_path = NULL;
	uint32_t                crt_init_flags   = 0;
	size_t                  i;
	int                     rc;

	/* Clear all info and set defaults */
	memset(info, 0, sizeof(*info));
	info->opts = CRT_PERF_OPTS_DEFAULTS;

	rc = d_log_init();
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not init log");

	/* Init MPI (if available) */
	rc = crt_perf_mpi_init(&info->mpi_info);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not initialize MPI");

	/* Parse user options */
	crt_perf_parse_options(argc, argv, &info->opts);

	memset(&crt_init_options, 0, sizeof(crt_init_options));
	crt_init_options.cio_provider      = info->opts.protocol;
	crt_init_options.cio_interface     = info->opts.hostname;
	crt_init_options.cio_domain        = info->opts.domain;
	crt_init_options.cio_port          = info->opts.port;
	crt_init_options.cio_progress_busy = info->opts.busy_wait;
	if (info->opts.msg_size_max) {
		crt_init_options.cio_max_expected_size   = info->opts.msg_size_max;
		crt_init_options.cio_max_unexpected_size = info->opts.msg_size_max;
		crt_init_options.cio_use_expected_size   = true;
		crt_init_options.cio_use_unexpected_size = true;
	}
	crt_init_options.cio_thread_mode_single = true;
	if (info->mpi_info.rank == 0) {
		if (info->opts.busy_wait)
			printf("# Initializing CRT in busy wait mode\n");
	}

	if (listen)
		crt_init_flags |= CRT_FLAG_BIT_SERVER | CRT_FLAG_BIT_AUTO_SWIM_DISABLE;
	rc = crt_init_opt(listen ? CRT_PERF_GROUP_ID : NULL, crt_init_flags, &crt_init_options);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not init CART");

	if (attach_info_path) {
		rc = crt_group_config_path_set(attach_info_path);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not set attach info path to %s",
				       attach_info_path);
	}

	if (listen) {
		rc = crt_rank_self_set(info->mpi_info.rank, 1);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not set self rank to %d",
				       info->mpi_info.rank);
	}

	if (info->opts.bidir) {
		crt_perf_rpcs[0].prf_req_fmt = &crt_perf_rate_bidir;
	}

	rc = crt_proto_register(&crt_perf_protocol);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not register protocol");

	info->context_info = (struct crt_perf_context_info *)calloc(info->opts.context_max,
								    sizeof(*info->context_info));
	CRT_PERF_CHECK_ERROR(info->context_info == NULL, error, rc, -DER_NOMEM,
			     "could not allocate context info array for %zu contexts",
			     info->opts.context_max);

	for (i = 0; i < info->opts.context_max; i++) {
		rc = crt_perf_context_init(info, i, &info->context_info[i]);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not init context info");
	}

	if (listen) {
		rc = crt_perf_group_save(info);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not save group info");
	} else {
		rc = crt_perf_group_attach(info);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not attach to server group");
	}

	perf_info_g = info;

	return 0;

error:
	return rc;
}

void
crt_perf_cleanup(struct crt_perf_info *info)
{
	size_t i;

	if (info->ep_group != NULL) {
		(void)crt_group_detach(info->ep_group);
		info->ep_group = NULL;
	}

	if (info->context_info != NULL) {
		for (i = 0; i < info->opts.context_max; i++)
			crt_perf_context_cleanup(&info->context_info[i]);
		free(info->context_info);
		info->context_info = NULL;
	}

	(void)crt_finalize();

	crt_perf_free_options(&info->opts);

	crt_perf_mpi_finalize(&info->mpi_info);

	d_log_fini();

	perf_info_g = NULL;
}

void
crt_perf_rpc_set_req(const struct crt_perf_info *perf_info, struct crt_perf_context_info *info)
{
	size_t comm_rank = (size_t)perf_info->mpi_info.rank,
	       comm_size = (size_t)perf_info->mpi_info.size;
	size_t i;

	for (i = 0; i < perf_info->opts.request_max; i++) {
		crt_endpoint_t *endpoint          = &info->requests[i].endpoint;
		size_t          request_global_id = comm_rank + i * comm_size;

		*endpoint = (crt_endpoint_t){.ep_grp  = perf_info->ep_group,
					     .ep_rank = (request_global_id / perf_info->ep_tags) %
							perf_info->ep_ranks,
					     .ep_tag = (request_global_id % perf_info->ep_tags)};

		D_INFO("Sending to %d:%d\n", endpoint->ep_rank, endpoint->ep_tag);
	}
}

int
crt_perf_rpc_buf_init(const struct crt_perf_info *perf_info, struct crt_perf_context_info *info)
{
	const struct crt_perf_opts *opts      = &perf_info->opts;
	size_t                      page_size = sysconf(_SC_PAGE_SIZE);
	int                         rc;

	/* Prepare buf */
	info->rpc_buf = aligned_alloc(page_size, opts->buf_size_max);
	CRT_PERF_CHECK_ERROR(info->rpc_buf == NULL, error, rc, -DER_NOMEM,
			     "aligned_alloc(%zu, %zu) failed", page_size, opts->buf_size_max);

	/* Init data */
	crt_perf_init_data(info->rpc_buf, opts->buf_size_max);

	return 0;

error:
	if (info->rpc_buf != NULL) {
		free(info->rpc_buf);
		info->rpc_buf = NULL;
	}

	return rc;
}

int
crt_perf_bulk_buf_init(const struct crt_perf_info *perf_info, struct crt_perf_context_info *info,
		       crt_bulk_op_t bulk_op)
{
	const struct crt_perf_opts *opts      = &perf_info->opts;
	size_t                      comm_rank = (size_t)perf_info->mpi_info.rank,
	       comm_size                      = (size_t)perf_info->mpi_info.size;
	crt_bulk_perm_t         bulk_perm = (bulk_op == CRT_BULK_GET) ? CRT_BULK_RO : CRT_BULK_WO;
	struct crt_perf_request args      = {.expected_count = opts->request_max,
					     .complete_count = 0,
					     .rc             = 0,
					     .done           = false,
					     .cb             = NULL,
					     .arg            = NULL};
	int                     rc;
	size_t                  i;

	rc = crt_perf_bulk_buf_alloc(info, opts->request_max, opts->buf_count, opts->buf_size_max,
				     bulk_perm, bulk_op == CRT_BULK_GET);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not allocate bulk buffers");

	for (i = 0; i < opts->request_max; i++) {
		struct crt_perf_rpc *request          = &info->requests[i];
		size_t               handle_global_id = comm_rank + i * comm_size,
		       target_max                     = perf_info->ep_ranks * perf_info->ep_tags,
		       target_rank                    = handle_global_id % target_max;
		// struct crt_perf_bulk_init_info bulk_info =
		struct crt_perf_bulk_init_info *bulk_init_info;

		rc = crt_req_create(info->context, &request->endpoint,
				    CRT_PERF_ID(CRT_PERF_BW_INIT), &request->rpc);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not create request");

		bulk_init_info  = crt_req_get(request->rpc);
		*bulk_init_info = (struct crt_perf_bulk_init_info){
		    .bulk        = info->local_bulk_handles[i],
		    .bulk_op     = (uint32_t)bulk_op,
		    .handle_id   = (uint32_t)(handle_global_id / target_max),
		    .buf_count   = (uint32_t)opts->buf_count,
		    .size_max    = (uint32_t)opts->buf_size_max,
		    .request_max = (uint32_t)opts->request_max,
		    .comm_size   = (uint32_t)comm_size,
		    .target_rank = (uint32_t)target_rank,
		    .target_max  = (uint32_t)target_max};

		D_INFO("(%zu) handle_id %" PRIu32 " (%zu) to %zu\n", comm_rank,
		       bulk_init_info->handle_id, handle_global_id, target_rank);

		rc = crt_req_send(request->rpc, crt_perf_request_complete, &args);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not send request to %" PRIu32 ":%" PRIu32,
				       request->endpoint.ep_rank, request->endpoint.ep_tag);
	}

	rc = crt_perf_request_wait(perf_info, info, CRT_PERF_TIMEOUT, &args);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not wait for requests");

	return 0;

error:
	crt_perf_bulk_buf_free(info);

	return rc;
}

void
crt_perf_print_header_lat(const struct crt_perf_info         *perf_info,
			  const struct crt_perf_context_info *info, const char *benchmark)
{
	const struct crt_perf_opts *opts = &perf_info->opts;

	printf("# CRT %s v" CART_VERSION "\n", benchmark);
	printf("# %d client process(es)\n", perf_info->mpi_info.size);
	printf("# Loop %d times from size %zu to %zu byte(s) with %zu request(s) "
	       "in-flight\n",
	       opts->loop, opts->buf_size_min, opts->buf_size_max, opts->request_max);
	if (opts->request_max * (size_t)perf_info->mpi_info.size <
	    (size_t)(perf_info->ep_ranks * perf_info->ep_tags))
		printf("# WARNING number of requests in flight less than number of "
		       "targets\n");
	if (opts->verify)
		printf("# WARNING verifying data, output will be slower\n");
	printf("%-*s%*s%*s\n", 10, "# Size", NWIDTH, "Avg time (us)", NWIDTH, "Avg rate (RPC/s)");
	fflush(stdout);
}

void
crt_perf_print_lat(const struct crt_perf_info *perf_info, const struct crt_perf_context_info *info,
		   size_t buf_size, struct timespec t)
{
	const struct crt_perf_opts *opts = &perf_info->opts;
	double                      rpc_time;
	size_t                      loop = (size_t)opts->loop, request_max = opts->request_max,
	       dir           = (size_t)(opts->bidir ? 2 : 1),
	       mpi_comm_size = (size_t)perf_info->mpi_info.size;

	rpc_time = d_time2s(t) * 1e6 / (double)(loop * request_max * dir * mpi_comm_size);

	printf("%-*zu%*.*f%*lu\n", 10, buf_size, NWIDTH, NDIGITS, rpc_time, NWIDTH,
	       (long unsigned int)(1e6 / rpc_time));
}

void
crt_perf_print_header_bw(const struct crt_perf_info         *perf_info,
			 const struct crt_perf_context_info *info, const char *benchmark)
{
	const struct crt_perf_opts *opts = &perf_info->opts;

	printf("# CRT %s v" CART_VERSION "\n", benchmark);
	printf("# %d client process(es)\n", perf_info->mpi_info.size);
	printf("# Loop %d times from size %zu to %zu byte(s) with %zu request(s) "
	       "in-flight\n# - %zu bulk transfer(s) per request\n",
	       opts->loop, opts->buf_size_min, opts->buf_size_max, opts->request_max,
	       opts->buf_count);
	if (opts->verify)
		printf("# WARNING verifying data, output will be slower\n");
	if (opts->mbps)
		printf("%-*s%*s%*s\n", 10, "# Size", NWIDTH, "Bandwidth (MB/s)", NWIDTH,
		       "Time (us)");
	else
		printf("%-*s%*s%*s\n", 10, "# Size", NWIDTH, "Bandwidth (MiB/s)", NWIDTH,
		       "Time (us)");
	fflush(stdout);
}

void
crt_perf_print_bw(const struct crt_perf_info *perf_info, const struct crt_perf_context_info *info,
		  size_t buf_size, struct timespec t)
{
	const struct crt_perf_opts *opts = &perf_info->opts;
	size_t loop = (size_t)opts->loop, mpi_comm_size = (size_t)perf_info->mpi_info.size,
	       request_max = opts->request_max, buf_count = opts->buf_count;
	double avg_time, avg_bw;

	avg_time = d_time2s(t) * 1e6 / (double)(loop * request_max * mpi_comm_size * buf_count);
	avg_bw = (double)(buf_size * loop * request_max * mpi_comm_size * buf_count) / d_time2s(t);

	if (opts->mbps)
		avg_bw /= 1e6; /* MB/s, matches OSU benchmarks */
	else
		avg_bw /= (1024 * 1024); /* MiB/s */

	printf("%-*zu%*.*f%*.*f\n", 10, buf_size, NWIDTH, NDIGITS, avg_bw, NWIDTH, NDIGITS,
	       avg_time);
}

int
crt_perf_verify_data(const void *buf, size_t buf_size)
{
	const char *buf_ptr = (const char *)buf;
	size_t      i;
	int         rc;

	for (i = 0; i < buf_size; i++) {
		CRT_PERF_CHECK_ERROR(buf_ptr[i] != (char)i - 1, error, rc, -DER_INVAL,
				     "Error detected in bulk transfer, buf[%zu] = %d, "
				     "was expecting %d!",
				     i, buf_ptr[i], (char)i);
	}

	return 0;

error:
	return rc;
}

void
crt_perf_request_complete(const struct crt_cb_info *cb_info)
{
	struct crt_perf_request *info = (struct crt_perf_request *)cb_info->cci_arg;

	CRT_PERF_CHECK_ERROR(cb_info->cci_rc != 0, out, info->rc, cb_info->cci_rc,
			     "callback failed");

	if (info->cb) {
		info->rc = info->cb(cb_info->cci_rpc, info->arg);
	}

out:
	if ((++info->complete_count) == info->expected_count)
		info->done = true;
}

int
crt_perf_request_wait(const struct crt_perf_info *perf_info, struct crt_perf_context_info *info,
		      int64_t timeout, struct crt_perf_request *args)
{
	const struct crt_perf_opts *opts     = &perf_info->opts;
	bool                        aborting = false;
	int                         rc;

	while (!args->done) {
		rc = crt_progress(info->context, timeout);
		if (rc == -DER_TIMEDOUT) {
			unsigned int i;

			DL_WARN(rc, "RPC request timed out");

			if (aborting)
				continue;
			for (i = 0; i < opts->request_max; i++) {
				rc = crt_req_abort(info->requests[i].rpc);
				CRT_PERF_CHECK_D_ERROR(error, rc, "could not abort request");
			}
			aborting = true;
		} else
			CRT_PERF_CHECK_D_ERROR(error, rc, "could not make progress");
	}
	if (aborting)
		return -DER_TIMEDOUT;

	return 0;

error:
	return rc;
}

int
crt_perf_send_done(const struct crt_perf_info *perf_info, struct crt_perf_context_info *info)
{
	uint32_t ep_rank, ep_tag;
	int      rc;

	for (ep_rank = 0; ep_rank < perf_info->ep_ranks; ep_rank++) {
		for (ep_tag = 0; ep_tag < perf_info->ep_tags; ep_tag++) {
			crt_endpoint_t target_ep = {
			    .ep_grp = perf_info->ep_group, .ep_rank = ep_rank, .ep_tag = ep_tag};

			rc = crt_perf_send_rpc_wait(info->context, &target_ep,
						    CRT_PERF_ID(CRT_PERF_DONE), NULL, NULL);
			CRT_PERF_CHECK_D_ERROR(error, rc, "could not send rpc");
		}
	}

	return 0;

error:
	return rc;
}
