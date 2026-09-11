/*
 * (C) Copyright 2023-2024 Intel Corporation.
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "crt_perf.h"

#define CRT_PERF_HAS_DAOS_AGENT 1
#ifdef CRT_PERF_HAS_DAOS_AGENT
#include <daos/agent.h>
#include <daos/mgmt.h>
#endif

#include <mercury_proc.h> /* for hg_proc_save_ptr() */
#include <unistd.h>
#include <getopt.h>

/****************/
/* Local Macros */
/****************/

#ifndef CART_VERSION
#define CART_VERSION "0.0.0"
#endif

#define NDIGITS                2
#define NWIDTH                 27

#define CRT_PERF_GROUP_ID      "crt_perf"

/* matches crt_rpc.h */
#define CRT_OPC_PERF_BASE      0xF5000000UL
#define CRT_PROTO_PERF_VERSION 1
#define CRT_PERF_DAOS_ID(x)    CRT_PROTO_OPC(CRT_OPC_PERF_BASE, CRT_PROTO_PERF_VERSION, x)

#define CRT_PERF_BASE_OPC      0x010000000
#define CRT_PERF_RPC_VERSION   1
#define CRT_PERF_ID(x)         CRT_PROTO_OPC(CRT_PERF_BASE_OPC, CRT_PERF_RPC_VERSION, x)

#define CRT_PERF_BUF_SIZE_MAX  (1 << 24)
#define CRT_PERF_BUF_COUNT     (64)

#define DAOS_RESERVED_CTXS     2 /* Number of contexts reserved by DAOS for SWIM/RDB */

#define CRT_PERF_OPTS_DEFAULTS                                                                     \
	((struct crt_perf_opts){                                                                   \
	    .comm          = NULL,                                                                 \
	    .domain        = NULL,                                                                 \
	    .protocol      = NULL,                                                                 \
	    .hostname      = NULL,                                                                 \
	    .port          = NULL,                                                                 \
	    .auth_key      = NULL,                                                                 \
	    .attach_path   = NULL,                                                                 \
	    .group_id      = NULL,                                                                 \
	    .msg_size_max  = 0,                                                                    \
	    .buf_size_min  = 0,                                                                    \
	    .buf_size_max  = CRT_PERF_BUF_SIZE_MAX,                                                \
	    .context_max   = 1,                                                                    \
	    .request_max   = 1,                                                                    \
	    .buf_count     = CRT_PERF_BUF_COUNT,                                                   \
	    .loop          = 1,                                                                    \
	    .busy_wait     = false,                                                                \
	    .bidir         = false,                                                                \
	    .force_reg     = false,                                                                \
	    .verify        = false,                                                                \
	    .mbps          = false,                                                                \
	    .daos_agent    = false,                                                                \
	    .progress_cond = false,                                                                \
	    .daos_tse      = false,                                                                \
	})

/************************************/
/* Local Type and Struct Definition */
/************************************/

struct crt_perf_provider_info {
	char provider_str[128]; /* Provider string */
	char port_str[16];      /* Port string */
};

struct crt_perf_bulk_init_info {
	crt_bulk_t bulk;             /* Bulk handle */
	uint32_t   bulk_op;          /* Bulk operation */
	uint32_t   global_handle_id; /* Global bulk handle ID (unique across all server targets) */
	uint32_t   request_max;      /* Max number of requests/RPCs in flight */
	uint32_t   buf_count;        /* Number of buffers */
	uint32_t   size_max;         /* Max buffer size */
	uint32_t   comm_size;        /* Number of ranks in the communicator */
	uint32_t   target_rank;      /* Server rank for bulk transfer */
	uint32_t   target_max;       /* Total number of server targets */
	uint8_t    verify;           /* Verify data */
};

struct crt_perf_bulk_info {
	crt_bulk_t bulk;      /* Bulk handle */
	uint32_t   handle_id; /* Source handle ID */
	uint32_t   size;      /* Transfer size*/
};

struct crt_perf_daos_rate_in {
	d_iov_t iov;    /* IOV */
	uint8_t verify; /* Verify data */
};

struct crt_perf_daos_rate_out {
	int32_t rc;  /* Return code */
	int32_t idx; /* Index */
	int32_t val; /* Value */
};

enum crt_perf_daos_rpc_id {
	CRT_PERF_DAOS_RATE,
};

enum crt_perf_rpc_id {
	CRT_PERF_RATE,
	CRT_PERF_DONE,
	CRT_PERF_TAGS,
	CRT_PERF_BW_INIT,
	CRT_PERF_BW_READ,
	CRT_PERF_BW_WRITE
};

struct crt_perf_rpc {
	crt_endpoint_t endpoint; /* Destination endpoint */
	crt_rpc_t     *rpc;      /* RPC request */
};

struct crt_perf_request {
	int32_t expected_count;         /* Expected count */
	int32_t complete_count;         /* Complete count */
	int     rc;                     /* Callback return code */
	bool    done;                   /* Request completed */
	int (*cb)(crt_rpc_t *, void *); /* Callback */
	void *arg;                      /* Callback arg */
};

struct crt_perf_lat_task_args {
	const struct crt_perf_info   *perf_info;    /* Pointer to perf info */
	struct crt_perf_context_info *info;         /* Pointer to context info */
	size_t                        buf_size;     /* Buffer size */
	struct crt_perf_rpc          *request;      /* RPC request */
	struct crt_perf_request      *request_args; /* Request args */
};

struct crt_perf_tse_task {
	struct crt_perf_lat_task_args task_args; /* TSE task args */
	tse_task_t                   *task;      /* TSE task */
};

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

#ifdef CRT_PERF_HAS_DAOS_AGENT
static void
crt_perf_agent_init_options(struct dc_mgmt_sys_info *mgmt_sys_info,
			    crt_init_options_t      *crt_init_options);
#endif

static int
crt_perf_crt_init_options(const struct crt_perf_info    *info,
			  struct crt_perf_provider_info *provider_info,
			  crt_init_options_t            *crt_init_options);

static int
crt_perf_context_init(const struct crt_perf_info *perf_info, int context_id,
		      struct crt_perf_context_info *info);

static void
crt_perf_context_cleanup(struct crt_perf_context_info *info);

static int
crt_perf_group_save(const struct crt_perf_info *info);

static int
crt_perf_group_attach(crt_group_id_t group_id, struct crt_perf_info *info);

#ifdef CRT_PERF_HAS_DAOS_AGENT
static int
crt_perf_group_attach_agent(crt_group_id_t group_id, struct crt_perf_info *info,
			    const Mgmt__GetAttachInfoResp *attach_info_resp);
#endif

static int
crt_perf_bulk_buf_alloc(struct crt_perf_context_info *info, crt_bulk_perm_t bulk_perm,
			bool init_data, bool bulk_create);

static void
crt_perf_bulk_buf_free(struct crt_perf_context_info *info);

static void
crt_perf_init_data(void *buf, size_t buf_size);

static int
crt_perf_verify_data(const void *buf, size_t buf_size);

static int
crt_perf_proc_daos_rate_in(crt_proc_t proc, void *data);

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

static int
crt_perf_rpc_verify(crt_rpc_t *rpc, void *arg);

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

static void
crt_perf_print_lat(const struct crt_perf_info *perf_info, const struct crt_perf_context_info *info,
		   size_t buf_size, struct timespec t);

static void
crt_perf_print_bw(const struct crt_perf_info *perf_info, const struct crt_perf_context_info *info,
		  size_t buf_size, struct timespec t);

static void
crt_perf_request_complete(const struct crt_cb_info *cb_info);

static int
crt_perf_is_request_complete(void *arg);

static int
crt_perf_request_wait(const struct crt_perf_info *perf_info, struct crt_perf_context_info *info,
		      int64_t timeout, struct crt_perf_request *args);

static int
crt_perf_send_rpc_wait(crt_context_t context, crt_endpoint_t *target_ep, crt_opcode_t opc,
		       int (*out_cb)(crt_rpc_t *, void *), void *out_arg);

static int
crt_perf_lat_req_send(const struct crt_perf_info *perf_info, struct crt_perf_context_info *info,
		      size_t buf_size, unsigned int j, struct crt_perf_request *args);

static int
crt_perf_lat_req_send_tse(const struct crt_perf_info *perf_info, struct crt_perf_context_info *info,
			  size_t buf_size, unsigned int j, struct crt_perf_request *args);

static int
crt_perf_run_lat_task(tse_task_t *task);

/*******************/
/* Local Variables */
/*******************/

static const char   *crt_perf_short_options = "hc:d:p:H:P:k:l:bC:Z:y:z:w:x:BRvMf:g:aDT";

static struct option crt_perf_long_options[] = {{"help", no_argument, NULL, 'h'},
						{"comm", required_argument, NULL, 'c'},
						{"domain", required_argument, NULL, 'd'},
						{"protocol", required_argument, NULL, 'p'},
						{"hostname", required_argument, NULL, 'H'},
						{"port", required_argument, NULL, 'P'},
						{"auth_key", required_argument, NULL, 'k'},
						{"loop", required_argument, NULL, 'l'},
						{"busy", no_argument, NULL, 'b'},
						{"contexts", required_argument, NULL, 'C'},
						{"msg_size", required_argument, NULL, 'Z'},
						{"buf_size_min", required_argument, NULL, 'y'},
						{"buf_size_max", required_argument, NULL, 'z'},
						{"buf_count", required_argument, NULL, 'w'},
						{"requests", required_argument, NULL, 'x'},
						{"bidirectional", no_argument, NULL, 'B'},
						{"force_reg", no_argument, NULL, 'R'},
						{"verify", no_argument, NULL, 'v'},
						{"millionbps", no_argument, NULL, 'M'},
						{"hostfile", required_argument, NULL, 'f'},
						{"group_name", required_argument, NULL, 'g'},
						{"daos_agent", no_argument, NULL, 'a'},
						{"progress_cond", no_argument, NULL, 'D'},
						{"daos_tse", no_argument, NULL, 'T'},
						{NULL, 0, NULL, 0}};

/* TODO keep global until we can retrieve user data from cart context */
static struct crt_perf_info       *perf_info_g;

static struct crt_req_format       crt_perf_daos_rate = {.crf_proc_in  = crt_perf_proc_daos_rate_in,
							 .crf_proc_out = NULL,
							 .crf_size_in =
							     sizeof(struct crt_perf_daos_rate_in),
							 .crf_size_out = 0};

static struct crt_proto_rpc_format crt_perf_daos_rpcs[] = {
    {.prf_req_fmt = &crt_perf_daos_rate, .prf_hdlr = NULL, .prf_co_ops = NULL, .prf_flags = 0}};

static struct crt_req_format crt_perf_null = {
    .crf_proc_in = NULL, .crf_proc_out = NULL, .crf_size_in = 0, .crf_size_out = 0};

static struct crt_req_format crt_perf_rate = {.crf_proc_in  = crt_perf_proc_iovec,
					      .crf_proc_out = NULL,
					      .crf_size_in  = sizeof(struct iovec),
					      .crf_size_out = 0};

static struct crt_req_format crt_perf_rate_bidir = {.crf_proc_in  = crt_perf_proc_iovec,
						    .crf_proc_out = crt_perf_proc_iovec,
						    .crf_size_in  = sizeof(struct iovec),
						    .crf_size_out = sizeof(struct iovec)};

static struct crt_req_format crt_perf_tags = {.crf_proc_in  = NULL,
					      .crf_proc_out = crt_perf_proc_tags,
					      .crf_size_in  = 0,
					      .crf_size_out = sizeof(uint32_t)};

static struct crt_req_format crt_perf_bulk_init = {.crf_proc_in  = crt_perf_proc_bulk_init_info,
						   .crf_proc_out = NULL,
						   .crf_size_in =
						       sizeof(struct crt_perf_bulk_init_info),
						   .crf_size_out = 0};

static struct crt_req_format crt_perf_bulk_bw = {.crf_proc_in  = crt_perf_proc_bulk_info,
						 .crf_proc_out = NULL,
						 .crf_size_in  = sizeof(struct crt_perf_bulk_info),
						 .crf_size_out = 0};

static struct crt_proto_rpc_format crt_perf_rpcs[] = {{.prf_req_fmt = &crt_perf_rate,
						       .prf_hdlr    = crt_perf_rpc_rate_cb,
						       .prf_co_ops  = NULL,
						       .prf_flags   = 0},
						      {.prf_req_fmt = &crt_perf_null,
						       .prf_hdlr    = crt_perf_done_cb,
						       .prf_co_ops  = NULL,
						       .prf_flags   = 0},
						      {.prf_req_fmt = &crt_perf_tags,
						       .prf_hdlr    = crt_perf_tags_cb,
						       .prf_co_ops  = NULL,
						       .prf_flags   = 0},
						      {.prf_req_fmt = &crt_perf_bulk_init,
						       .prf_hdlr    = crt_perf_bulk_init_cb,
						       .prf_co_ops  = NULL,
						       .prf_flags   = 0},
						      {.prf_req_fmt = &crt_perf_bulk_bw,
						       .prf_hdlr    = crt_perf_bulk_push_cb,
						       .prf_co_ops  = NULL,
						       .prf_flags   = 0},
						      {.prf_req_fmt = &crt_perf_bulk_bw,
						       .prf_hdlr    = crt_perf_bulk_pull_cb,
						       .prf_co_ops  = NULL,
						       .prf_flags   = 0}};

static struct crt_proto_format     crt_perf_daos_protocol = {.cpf_name = "perf",
							     .cpf_ver  = CRT_PROTO_PERF_VERSION,
							     .cpf_count =
								 ARRAY_SIZE(crt_perf_daos_rpcs),
							     .cpf_prf  = crt_perf_daos_rpcs,
							     .cpf_base = CRT_OPC_PERF_BASE};

static struct crt_proto_format     crt_perf_protocol = {.cpf_name  = "perf_crt",
							.cpf_ver   = CRT_PERF_RPC_VERSION,
							.cpf_count = ARRAY_SIZE(crt_perf_rpcs),
							.cpf_prf   = crt_perf_rpcs,
							.cpf_base  = CRT_PERF_BASE_OPC};

static void
crt_perf_parse_options(int argc, char *argv[], struct crt_perf_opts *opts)
{
	int opt;

	if (argc < 2) {
		crt_perf_usage(argv[0]);
		exit(EXIT_FAILURE);
	}

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

		case 'k': /* auth key */
			opts->auth_key = strdup(optarg);
			break;

		case 'l': /* loop */
			opts->loop = atoi(optarg);
			if (opts->loop <= 0) {
				D_ERROR("invalid loop value %s\n", optarg);
				goto error;
			}
			break;

		case 'b': /* busy wait */
			opts->busy_wait = true;
			break;

		case 'C': /* context max */
			if (atoi(optarg) <= 0) {
				D_ERROR("invalid context value %s\n", optarg);
				goto error;
			}
			opts->context_max = (size_t)atoi(optarg);
			break;

		case 'Z': /* msg size */
			opts->msg_size_max = crt_perf_parse_size(optarg);
			if (opts->msg_size_max == 0) {
				D_ERROR("invalid msg size value %s\n", optarg);
				goto error;
			}
			break;

		case 'y': /* min buffer size */
			opts->buf_size_min = crt_perf_parse_size(optarg);
			break;

		case 'z': /* max buffer size */
			opts->buf_size_max = crt_perf_parse_size(optarg);
			break;

		case 'w': /* buffer count */
			opts->buf_count = (size_t)atol(optarg);
			if (opts->buf_count == 0) {
				D_ERROR("invalid buffer count value %s\n", optarg);
				goto error;
			}
			break;

		case 'x': /* request max */
			if (atoi(optarg) <= 0) {
				D_ERROR("invalid max request in-flight value %s\n", optarg);
				goto error;
			}
			opts->request_max = (size_t)atoi(optarg);
			break;

		case 'B': /* bidirectional */
			opts->bidir = true;
			break;

		case 'R': /* force registration */
			opts->force_reg = true;
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

		case 'g': /* group name */
			opts->group_id = strdup(optarg);
			break;

		case 'a': /* daos agent */
			opts->daos_agent = true;
			break;

		case 'D': /* use crt_progress_cond */
			opts->progress_cond = true;
			break;

		case 'T': /* use DAOS TSE */
			opts->daos_tse = true;
			break;

		case 'h':
		default:
			crt_perf_usage(argv[0]);
			exit(EXIT_FAILURE);
		}
	}

	if ((argc - optind) > 1)
		goto error;

	return;

error:
	crt_perf_usage(argv[0]);
	crt_perf_free_options(opts);
	exit(EXIT_FAILURE);
}

static void
crt_perf_free_options(struct crt_perf_opts *opts)
{
	D_FREE(opts->comm);
	D_FREE(opts->domain);
	D_FREE(opts->protocol);
	D_FREE(opts->hostname);
	D_FREE(opts->port);
	D_FREE(opts->auth_key);
	D_FREE(opts->attach_path);
	D_FREE(opts->group_id);
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
	printf("    -c, --comm           Select transport plugin\n"
	       "                         Available plugins: ofi, ucx, etc\n");
	printf("    -d, --domain         Select domain / device to use\n");
	printf("    -p, --protocol       Select plugin protocol provider\n"
	       "                         Available providers: tcp, verbs, etc\n");
	printf("    -H, --hostname       Select hostname / IP address / interface to use\n"
	       "                         Default: any\n");
	printf("    -P, --port           Select port to use\n"
	       "                         Default: any\n");
	printf("    -k, --auth_key       Authentication key\n");
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
	printf("    -g, --group_id       Specify group ID (default:%s)\n", CRT_PERF_GROUP_ID);
	printf("    -a, --daos_agent     Use DAOS agent (crt_rate client only)\n");
	printf("    -T, --daos_tse       Use DAOS TSE (crt_rate client only)\n");
	printf("    -D, --progress_cond  Use crt_progress_cond to wait for requests\n");
}

#ifdef CRT_PERF_HAS_DAOS_AGENT
static void
crt_perf_agent_init_options(struct dc_mgmt_sys_info *mgmt_sys_info,
			    crt_init_options_t      *crt_init_options)
{
	crt_init_options->cio_provider    = mgmt_sys_info->provider;
	crt_init_options->cio_interface   = mgmt_sys_info->interface;
	crt_init_options->cio_domain      = mgmt_sys_info->domain;
	crt_init_options->cio_crt_timeout = mgmt_sys_info->crt_timeout;
}
#endif

static int
crt_perf_crt_init_options(const struct crt_perf_info    *info,
			  struct crt_perf_provider_info *provider_info,
			  crt_init_options_t            *crt_init_options)
{
	int rc;

	/* If protocol contains '+', it means it has provider specified as well, so parse them */
	if (info->opts.protocol == NULL)
		crt_init_options->cio_provider = info->opts.comm;
	else if (info->opts.comm == NULL && strchr(info->opts.protocol, '+') != NULL)
		crt_init_options->cio_provider = info->opts.protocol;
	else if (info->opts.comm != NULL) {
		snprintf(provider_info->provider_str, sizeof(provider_info->provider_str), "%s+%s",
			 info->opts.comm, info->opts.protocol);
		crt_init_options->cio_provider = provider_info->provider_str;
	} else
		crt_init_options->cio_provider = info->opts.protocol;

	crt_init_options->cio_interface = info->opts.hostname;
	crt_init_options->cio_domain    = info->opts.domain;
	if (info->opts.port != NULL) {
		/* Adjust port based on number of contexts per rank */
		int port;
		port = atoi(info->opts.port);
		CRT_PERF_CHECK_ERROR(port <= 0, error, rc, -DER_INVAL, "invalid port %s",
				     info->opts.port);
		port += info->opts.context_max * info->mpi_info.rank;
		CRT_PERF_CHECK_ERROR(port > 65535, error, rc, -DER_INVAL, "port %d out of range",
				     port);
		snprintf(provider_info->port_str, sizeof(provider_info->port_str), "%d", port);
		crt_init_options->cio_port = provider_info->port_str;
	}
	crt_init_options->cio_auth_key      = info->opts.auth_key;
	crt_init_options->cio_progress_busy = info->opts.busy_wait;
	if (info->opts.msg_size_max) {
		crt_init_options->cio_max_expected_size   = info->opts.msg_size_max;
		crt_init_options->cio_max_unexpected_size = info->opts.msg_size_max;
		crt_init_options->cio_use_expected_size   = true;
		crt_init_options->cio_use_unexpected_size = true;
	}
	crt_init_options->cio_thread_mode_single = true;

	return 0;

error:
	return rc;
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

	D_ALLOC_ARRAY(info->requests, perf_info->opts.request_max);
	CRT_PERF_CHECK_ERROR(info->requests == NULL, error, rc, -DER_NOMEM,
			     "D_ALLOC_ARRAY(%zu) failed", perf_info->opts.request_max);

	if (perf_info->opts.daos_tse) {
		D_ALLOC_ARRAY(info->tse_tasks, perf_info->opts.request_max);
		CRT_PERF_CHECK_ERROR(info->tse_tasks == NULL, error, rc, -DER_NOMEM,
				     "D_ALLOC_ARRAY(%zu) failed", perf_info->opts.request_max);

		rc = tse_sched_init(&info->tse_sched, NULL, NULL);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not init tse_sched");
	}

	return 0;

error:
	crt_perf_context_cleanup(info);
	return rc;
}

static void
crt_perf_context_cleanup(struct crt_perf_context_info *info)
{
	if (info->tse_tasks != NULL) {
		tse_sched_complete(&info->tse_sched, 0, true);

		D_FREE(info->tse_tasks);
		info->tse_tasks = NULL;
	}
	if (info->remote_bulk_handles != NULL) {
		size_t i;
		for (i = 0; i < info->handle_max; i++)
			crt_bulk_free(info->remote_bulk_handles[i]);
		free(info->remote_bulk_handles);
		info->remote_bulk_handles = NULL;
	}
	D_FREE(info->remote_bulk_handle_ids);
	D_FREE(info->bulk_requests);

	crt_perf_bulk_buf_free(info);

	if (info->context != NULL) {
		(void)crt_context_destroy(info->context, 1);
		info->context = NULL;
	}

	D_FREE(info->requests);
	D_FREE(info->rpc_buf);
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
crt_perf_group_attach(crt_group_id_t group_id, struct crt_perf_info *info)
{
	int rc;

	rc = crt_group_attach(group_id, &info->ep_group);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not attach to group %s", group_id);

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

		printf("# %" PRIu32 " target rank(s) read - %" PRIu32 " tag(s) / rank\n",
		       info->ep_ranks, info->ep_tags);
	}
	if (info->mpi_info.size > 1) {
		rc = crt_perf_mpi_bcast(&info->mpi_info, &info->ep_tags, sizeof(info->ep_tags), 0);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not bcast ep_tags");
	}

	return 0;

error:
	return rc;
}

#ifdef CRT_PERF_HAS_DAOS_AGENT
static int
crt_perf_group_attach_agent(crt_group_id_t group_id, struct crt_perf_info *info,
			    const Mgmt__GetAttachInfoResp *attach_info_resp)
{
	uint32_t num_min_ctxs = 0;
	int      rc, i;

	rc = crt_group_view_create(group_id, &info->ep_group);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not create group view");

	for (i = 0; i < attach_info_resp->n_rank_uris; i++) {
		Mgmt__GetAttachInfoResp__RankUri *rank_uri = attach_info_resp->rank_uris[i];

		rc = crt_group_primary_rank_add(info->context_info[0].context, info->ep_group,
						rank_uri->rank, rank_uri->uri);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not add rank %d %s", rank_uri->rank,
				       rank_uri->uri);

		/* Check if the number of contexts is consistent across all ranks, if not use the
		 * minimum */
		if (num_min_ctxs > 0 && rank_uri->num_ctxs > num_min_ctxs)
			D_WARN("rank %d: num_ctxs %d > expected %d", rank_uri->rank,
			       rank_uri->num_ctxs, num_min_ctxs);
		else
			num_min_ctxs = rank_uri->num_ctxs;
	}

	info->ep_ranks = attach_info_resp->n_rank_uris;
	CRT_PERF_CHECK_ERROR(num_min_ctxs <= DAOS_RESERVED_CTXS, error, rc, -DER_INVAL,
			     "num_ctxs %d must be greater than reserved contexts %d", num_min_ctxs,
			     DAOS_RESERVED_CTXS);
	info->ep_tags = num_min_ctxs - DAOS_RESERVED_CTXS;

	if (info->mpi_info.rank == 0)
		printf("# %" PRIu32 " target rank(s) read - %" PRIu32 " tag(s) / rank\n",
		       info->ep_ranks, info->ep_tags);

	return 0;

error:
	return rc;
}
#endif

static int
crt_perf_bulk_buf_alloc(struct crt_perf_context_info *info, crt_bulk_perm_t bulk_perm,
			bool init_data, bool bulk_create)
{
	size_t page_size = sysconf(_SC_PAGE_SIZE);
	int    rc;
	size_t i;

	D_ALLOC_ARRAY(info->bulk_bufs, info->handle_max);
	CRT_PERF_CHECK_ERROR(info->bulk_bufs == NULL, error, rc, -DER_NOMEM,
			     "D_ALLOC_ARRAY(%zu) failed", info->handle_max);

	D_ALLOC_ARRAY(info->local_bulk_handles, info->handle_max);
	CRT_PERF_CHECK_ERROR(info->local_bulk_handles == NULL, error, rc, -DER_NOMEM,
			     "D_ALLOC_ARRAY(%zu) failed", info->handle_max);

	for (i = 0; i < info->handle_max; i++) {
		size_t  alloc_size = info->buf_size_max * info->buf_count;
		d_iov_t iov = {.iov_buf = NULL, .iov_buf_len = alloc_size, .iov_len = alloc_size};
		d_sg_list_t sgl = {.sg_nr = 1, .sg_nr_out = 0, .sg_iovs = &iov};

		/* Prepare buf */
		D_ALIGNED_ALLOC(info->bulk_bufs[i], page_size, alloc_size);
		CRT_PERF_CHECK_ERROR(info->bulk_bufs[i] == NULL, error, rc, -DER_NOMEM,
				     "D_ALIGNED_ALLOC(%zu, %zu) failed", page_size,
				     info->buf_size_max);
		iov.iov_buf = info->bulk_bufs[i];

		/* Initialize data */
		if (init_data) {
			size_t j;
			for (j = 0; j < info->buf_count; j++) {
				char *buf_p = (char *)iov.iov_buf + j * info->buf_size_max;
				crt_perf_init_data(buf_p, info->buf_size_max);
			}
		}

		if (bulk_create) {
			rc = crt_bulk_create(info->context, &sgl, bulk_perm,
					     &info->local_bulk_handles[i]);
			CRT_PERF_CHECK_D_ERROR(error, rc, "could not create bulk handle");
		}
	}

	return 0;

error:
	crt_perf_bulk_buf_free(info);

	return rc;
}

static void
crt_perf_bulk_buf_free(struct crt_perf_context_info *info)
{
	size_t i;

	if (info->local_bulk_handles != NULL) {
		for (i = 0; i < info->handle_max; i++)
			(void)crt_bulk_free(info->local_bulk_handles[i]);
		free(info->local_bulk_handles);
		info->local_bulk_handles = NULL;
	}

	if (info->bulk_bufs != NULL) {
		for (i = 0; i < info->handle_max; i++)
			free(info->bulk_bufs[i]);
		free(info->bulk_bufs);
		info->bulk_bufs = NULL;
	}
}

static void
crt_perf_init_data(void *buf, size_t buf_size)
{
	int   *buf_ptr = (int *)buf;
	size_t i;

	/* Skip first integer (used for checking rank) */
	for (i = 1; i < buf_size / sizeof(int); i++)
		buf_ptr[i] = (int)i;
}

static int
crt_perf_verify_data(const void *buf, size_t buf_size)
{
	const int *buf_ptr = (const int *)buf;
	size_t     i;
	int        rc;

	/* Skip first integer (used for checking rank) */
	for (i = 1; i < buf_size / sizeof(int); i++)
		CRT_PERF_CHECK_ERROR(buf_ptr[i] != (int)i, error, rc, -DER_INVAL,
				     "Error detected in bulk transfer, buf[%zu] = %d, "
				     "was expecting %d!",
				     i, buf_ptr[i], (int)i);

	return 0;

error:
	return rc;
}

static int
crt_perf_proc_daos_rate_in(crt_proc_t proc, void *data)
{
	struct crt_perf_daos_rate_in *ptr = (struct crt_perf_daos_rate_in *)data;
	crt_proc_op_t                 proc_op;
	int                           rc;

	CRT_PERF_CHECK_ERROR(proc == NULL || ptr == NULL, error, rc, -DER_INVAL, "NULL arguments");

	rc = crt_proc_get_op(proc, &proc_op);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not get proc op");

	rc = crt_proc_d_iov_t(proc, proc_op, &ptr->iov);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not proc iov");

	rc = crt_proc_uint8_t(proc, proc_op, &ptr->verify);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not proc verify");

	return 0;

error:
	return rc;
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

	rc = crt_proc_uint32_t(proc, proc_op, &info->global_handle_id);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not proc global handle id");

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

	rc = crt_proc_uint8_t(proc, proc_op, &info->verify);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not proc verify");

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

	/* only used when forcing registration on every loop */
	rc = crt_proc_crt_bulk_t(proc, proc_op, &info->bulk);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not proc bulk");

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

static int
crt_perf_rpc_verify(crt_rpc_t *rpc, void *arg)
{
	struct iovec *out_iov;
	int           rc;

	(void)arg;

	out_iov = crt_reply_get(rpc);
	CRT_PERF_CHECK_ERROR(out_iov == NULL, error, rc, -DER_INVAL,
			     "could not retrieve rpc response");

	rc = crt_perf_verify_data(out_iov->iov_base, out_iov->iov_len);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not verify data");

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

	info = &perf_info_g->context_info[ctx_idx];

	/* Set done for context data */
	info->done = true;

	/* Free up resources */
	if (info->remote_bulk_handles != NULL) {
		size_t i;
		for (i = 0; i < info->handle_max; i++)
			crt_bulk_free(info->remote_bulk_handles[i]);
		free(info->remote_bulk_handles);
		info->remote_bulk_handles = NULL;
	}
	D_FREE(info->remote_bulk_handle_ids);
	D_FREE(info->bulk_requests);

	crt_perf_bulk_buf_free(info);

	D_FREE(info->rpc_buf);

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
	uint32_t                        handle_id;
	int                             rc;

	rc = crt_context_idx(rpc->cr_ctx, &ctx_idx);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not query context index");

	info = &perf_info_g->context_info[ctx_idx];

	/* Get input struct */
	bulk_info = crt_req_get(rpc);
	CRT_PERF_CHECK_ERROR(bulk_info == NULL, error, rc, -DER_INVAL,
			     "could not retrieve rpc request");

	if (info->bulk_bufs == NULL) {
		crt_bulk_perm_t bulk_perm =
		    (bulk_info->bulk_op == CRT_BULK_GET) ? CRT_BULK_WO : CRT_BULK_RO;

		info->verify = bulk_info->verify;
		info->handle_max =
		    (bulk_info->request_max * bulk_info->comm_size) / bulk_info->target_max;
		if (((bulk_info->request_max * bulk_info->comm_size) % bulk_info->target_max) >
		    bulk_info->target_rank)
			info->handle_max++;

		D_INFO("(%d,%" PRIu32 ") number of handles is %zu\n", info->context_id,
		       bulk_info->target_rank, info->handle_max);

		info->buf_count    = bulk_info->buf_count;
		info->buf_size_max = bulk_info->size_max;

		rc = crt_perf_bulk_buf_alloc(info, bulk_perm, bulk_info->bulk_op == CRT_BULK_PUT,
					     true);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not allocate bulk buffers");

		D_ALLOC_ARRAY(info->remote_bulk_handles, info->handle_max);
		CRT_PERF_CHECK_ERROR(info->remote_bulk_handles == NULL, error, rc, -DER_NOMEM,
				     "D_ALLOC_ARRAY(%zu) failed", info->handle_max);

		D_ALLOC_ARRAY(info->remote_bulk_handle_ids, info->handle_max);
		CRT_PERF_CHECK_ERROR(info->remote_bulk_handle_ids == NULL, error, rc, -DER_NOMEM,
				     "D_ALLOC_ARRAY(%zu) failed", info->handle_max);

		D_ALLOC_ARRAY(info->bulk_requests, info->handle_max);
		CRT_PERF_CHECK_ERROR(info->bulk_requests == NULL, error, rc, -DER_NOMEM,
				     "D_ALLOC_ARRAY(%zu) failed", info->handle_max);
	}

	handle_id = bulk_info->global_handle_id / bulk_info->target_max;
	CRT_PERF_CHECK_ERROR(handle_id >= info->handle_max, error, rc, -DER_OVERFLOW,
			     "(%d,%" PRIu32 ") Handle ID is %" PRIu32 " >= %zu", info->context_id,
			     bulk_info->target_rank, handle_id, info->handle_max);

	info->remote_bulk_handle_ids[handle_id] = bulk_info->global_handle_id;
	info->remote_bulk_handles[handle_id]    = bulk_info->bulk;
	if (bulk_info->bulk != CRT_BULK_NULL)
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
	crt_bulk_t                    remote_hdl;
	int                           rc;

	rc = crt_context_idx(rpc->cr_ctx, &ctx_idx);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not query context index");

	info = &perf_info_g->context_info[ctx_idx];

	/* Get input struct */
	bulk_info = crt_req_get(rpc);
	CRT_PERF_CHECK_ERROR(bulk_info == NULL, error, rc, -DER_INVAL,
			     "could not retrieve rpc request");
	remote_hdl = bulk_info->bulk != CRT_BULK_NULL
			 ? bulk_info->bulk
			 : info->remote_bulk_handles[bulk_info->handle_id];

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
		    .bd_remote_hdl = remote_hdl,
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
	struct crt_perf_request *bulk_request = (struct crt_perf_request *)bulk_cb_info->bci_arg;
	int                      rc;

	CRT_PERF_CHECK_ERROR(bulk_cb_info->bci_rc != 0, done, rc, bulk_cb_info->bci_rc,
			     "bulk transfer failed");

done:
	if ((++bulk_request->complete_count) == bulk_request->expected_count) {
		const struct crt_perf_context_info *info =
		    (const struct crt_perf_context_info *)bulk_request->arg;
		bulk_request->done = true;

		if (info->verify && bulk_cb_info->bci_bulk_desc->bd_bulk_op == CRT_BULK_GET) {
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

static void
crt_perf_print_lat(const struct crt_perf_info *perf_info, const struct crt_perf_context_info *info,
		   size_t buf_size, struct timespec t)
{
	const struct crt_perf_opts *opts = &perf_info->opts;
	double                      rpc_time;
	size_t                      loop = (size_t)opts->loop, request_max = opts->request_max,
				    dir           = (size_t)(opts->bidir ? 2 : 1),
				    mpi_comm_size = (size_t)perf_info->mpi_info.size;

	rpc_time = d_time2s(t) * 1e6 / (double)(loop * request_max * dir * mpi_comm_size);

	printf("%-*zu%*lu%*.*f\n", 10, buf_size, NWIDTH, (long unsigned int)(1e6 / rpc_time),
	       NWIDTH, NDIGITS, rpc_time);
}

static void
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

static void
crt_perf_request_complete(const struct crt_cb_info *cb_info)
{
	struct crt_perf_request *info = (struct crt_perf_request *)cb_info->cci_arg;

	CRT_PERF_CHECK_ERROR(cb_info->cci_rc != 0, out, info->rc, cb_info->cci_rc,
			     "callback failed");

	if (info->cb)
		info->rc = info->cb(cb_info->cci_rpc, info->arg);

out:
	if ((++info->complete_count) == info->expected_count)
		info->done = true;
}

static int
crt_perf_is_request_complete(void *arg)
{
	struct crt_perf_request *info = (struct crt_perf_request *)arg;

	return info->done;
}

static int
crt_perf_request_wait(const struct crt_perf_info *perf_info, struct crt_perf_context_info *info,
		      int64_t timeout, struct crt_perf_request *args)
{
	const struct crt_perf_opts *opts     = &perf_info->opts;
	bool                        aborting = false;
	int                         rc;

	if (opts->daos_tse)
		tse_sched_progress(&info->tse_sched);

	if (opts->progress_cond) {
		rc = crt_progress_cond(info->context, timeout, crt_perf_is_request_complete, args);
		if (rc == -DER_TIMEDOUT) {
			DL_WARN(rc, "RPC request timed out");
			return -DER_TIMEDOUT;
		} else
			CRT_PERF_CHECK_D_ERROR(error, rc, "could not make progress");
	} else {
		while (!args->done) {
			rc = crt_progress(info->context, timeout);
			if (rc == -DER_TIMEDOUT) {
				unsigned int i;

				DL_WARN(rc, "RPC request timed out");

				if (aborting)
					continue;
				for (i = 0; i < opts->request_max; i++) {
					rc = crt_req_abort(info->requests[i].rpc);
					CRT_PERF_CHECK_D_ERROR(error, rc,
							       "could not abort request");
				}
				aborting = true;
			} else
				CRT_PERF_CHECK_D_ERROR(error, rc, "could not make progress");
		}
		if (aborting)
			return -DER_TIMEDOUT;
	}

	return 0;

error:
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

static int
crt_perf_lat_req_send(const struct crt_perf_info *perf_info, struct crt_perf_context_info *info,
		      size_t buf_size, unsigned int j, struct crt_perf_request *args)
{
	const struct crt_perf_opts *opts = &perf_info->opts;
	const crt_opcode_t          opc =
	    opts->daos_agent ? CRT_PERF_DAOS_ID(CRT_PERF_DAOS_RATE) : CRT_PERF_ID(CRT_PERF_RATE);
	struct crt_perf_rpc *request = &info->requests[j];
	void                *in_req;
	int                  rc;

	rc = crt_req_create(info->context, &request->endpoint, opc, &request->rpc);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not create request");

	in_req = crt_req_get(request->rpc);
	if (opts->daos_agent)
		*(struct crt_perf_daos_rate_in *)in_req =
		    (struct crt_perf_daos_rate_in){.iov    = (d_iov_t){.iov_buf     = info->rpc_buf,
								       .iov_buf_len = buf_size,
								       .iov_len     = buf_size},
						   .verify = (uint8_t)opts->verify};
	else
		*(struct iovec *)in_req =
		    (struct iovec){.iov_base = info->rpc_buf, .iov_len = buf_size};

	rc = crt_req_send(request->rpc, crt_perf_request_complete, args);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not send request to %" PRIu32 ":%" PRIu32,
			       request->endpoint.ep_rank, request->endpoint.ep_tag);

	return 0;

error:
	return rc;
}

static int
crt_perf_lat_req_send_tse(const struct crt_perf_info *perf_info, struct crt_perf_context_info *info,
			  size_t buf_size, unsigned int j, struct crt_perf_request *args)
{
	struct crt_perf_lat_task_args *task_args = &info->tse_tasks[j].task_args;
	*task_args = (struct crt_perf_lat_task_args){.perf_info    = perf_info,
						     .info         = info,
						     .request      = &info->requests[j],
						     .buf_size     = buf_size,
						     .request_args = args};
	int rc;

	rc = tse_task_create(crt_perf_run_lat_task, &info->tse_sched, task_args,
			     &info->tse_tasks[j].task);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not create task");

	rc = tse_task_schedule(info->tse_tasks[j].task, true);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not schedule task");

	return 0;

error:
	return rc;
}

static int
crt_perf_run_lat_task(tse_task_t *task)
{
	struct crt_perf_lat_task_args *task_args =
	    (struct crt_perf_lat_task_args *)tse_task_get_priv(task);
	const struct crt_perf_opts *opts    = &task_args->perf_info->opts;
	struct crt_perf_rpc        *request = task_args->request;
	const crt_opcode_t          opc =
	    opts->daos_agent ? CRT_PERF_DAOS_ID(CRT_PERF_DAOS_RATE) : CRT_PERF_ID(CRT_PERF_RATE);
	void *in_req;
	int   rc;

	rc = crt_req_create(task_args->info->context, &request->endpoint, opc, &request->rpc);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not create request");

	in_req = crt_req_get(request->rpc);
	if (opts->daos_agent)
		*(struct crt_perf_daos_rate_in *)in_req = (struct crt_perf_daos_rate_in){
		    .iov    = (d_iov_t){.iov_buf     = task_args->info->rpc_buf,
					.iov_buf_len = task_args->buf_size,
					.iov_len     = task_args->buf_size},
		    .verify = (uint8_t)opts->verify};
	else
		*(struct iovec *)in_req = (struct iovec){.iov_base = task_args->info->rpc_buf,
							 .iov_len  = task_args->buf_size};

	rc = crt_req_send(request->rpc, crt_perf_request_complete, task_args->request_args);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not send request to %" PRIu32 ":%" PRIu32,
			       request->endpoint.ep_rank, request->endpoint.ep_tag);

	tse_task_complete(task, 0);

	return 0;

error:
	tse_task_complete(task, rc);

	return rc;
}

int
crt_perf_init(int argc, char *argv[], bool listen, struct crt_perf_info *info)
{
	struct crt_init_options       crt_init_options;
	struct crt_perf_provider_info provider_info;
#ifdef CRT_PERF_HAS_DAOS_AGENT
	struct dc_mgmt_sys_info  mgmt_sys_info    = {0};
	Mgmt__GetAttachInfoResp *attach_info_resp = NULL;
#endif
	crt_group_id_t group_id = NULL;
	uint32_t crt_init_flags = listen ? CRT_FLAG_BIT_SERVER | CRT_FLAG_BIT_AUTO_SWIM_DISABLE : 0;
	size_t   i;
	int      rc;

	/* Clear all info and set defaults */
	memset(info, 0, sizeof(*info));
	info->opts = CRT_PERF_OPTS_DEFAULTS;

	/* Parse user options */
	crt_perf_parse_options(argc, argv, &info->opts);

	rc = d_log_init();
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not init log");

	/* Init MPI (if available) */
	rc = crt_perf_mpi_init(&info->mpi_info);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not initialize MPI");

	memset(&crt_init_options, 0, sizeof(crt_init_options));
	if (!listen && info->opts.daos_agent) {
#ifdef CRT_PERF_HAS_DAOS_AGENT
		group_id = info->opts.group_id ? info->opts.group_id : "daos_server";

		rc = dc_agent_init();
		CRT_PERF_CHECK_D_ERROR(error, rc, "failed to initialize DAOS agent (%d)", rc);

		/* Query the agent for the CaRT network configuration parameters */
		rc = dc_get_attach_info(group_id, true, &mgmt_sys_info, &attach_info_resp);
		CRT_PERF_CHECK_D_ERROR(error, rc, "dc_get_attach_info() failed (%d)\n", rc);

		crt_perf_agent_init_options(&mgmt_sys_info, &crt_init_options);
#else
		CRT_PERF_CHECK_ERROR(true, error, rc, -DER_INVAL,
				     "DAOS agent support not compiled in");
#endif
	} else {
		group_id = info->opts.group_id ? info->opts.group_id : CRT_PERF_GROUP_ID;

		rc = crt_perf_crt_init_options(info, &provider_info, &crt_init_options);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not init crt options (%d)", rc);
	}

	if (info->mpi_info.rank == 0 && info->opts.busy_wait)
		printf("# Initializing CRT in busy wait mode\n");
	if (info->mpi_info.rank == 0)
		printf("# CRT transport using info string: \"%s://%s%s%s%s%s\"\n",
		       crt_init_options.cio_provider,
		       crt_init_options.cio_domain ? crt_init_options.cio_domain : "",
		       crt_init_options.cio_domain ? "/" : "",
		       crt_init_options.cio_interface ? crt_init_options.cio_interface : "",
		       crt_init_options.cio_port ? ":" : "",
		       crt_init_options.cio_port ? crt_init_options.cio_port : "");

	rc = crt_init_opt(listen ? group_id : NULL, crt_init_flags, &crt_init_options);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not init CART");

	if (info->opts.attach_path != NULL) {
		if (info->mpi_info.rank == 0)
			printf("# Using attach path: %s\n", info->opts.attach_path);
		rc = crt_group_config_path_set(info->opts.attach_path);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not set attach path to %s",
				       info->opts.attach_path);
	}

	if (listen) {
		rc = crt_rank_self_set(info->mpi_info.rank, 1);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not set self rank to %d",
				       info->mpi_info.rank);
	}

	if (info->opts.bidir)
		crt_perf_rpcs[0].prf_req_fmt = &crt_perf_rate_bidir;

	if (!listen && info->opts.daos_agent) {
		rc = crt_proto_register(&crt_perf_daos_protocol);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not register protocol");
	} else {
		rc = crt_proto_register(&crt_perf_protocol);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not register protocol");
	}

	D_ALLOC_ARRAY(info->context_info, info->opts.context_max);
	CRT_PERF_CHECK_ERROR(info->context_info == NULL, error, rc, -DER_NOMEM,
			     "D_ALLOC_ARRAY(%zu) failed", info->opts.context_max);

	for (i = 0; i < info->opts.context_max; i++) {
		rc = crt_perf_context_init(info, i, &info->context_info[i]);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not init context info");
	}

	if (listen) {
		rc = crt_perf_group_save(info);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not save group info");
	} else if (info->opts.daos_agent) {
#ifdef CRT_PERF_HAS_DAOS_AGENT
		rc = crt_perf_group_attach_agent(group_id, info, attach_info_resp);
		CRT_PERF_CHECK_D_ERROR(
		    error, rc, "could not attach to server group (%s) using agent", group_id);
#else
		CRT_PERF_CHECK_ERROR(true, error, rc, -DER_INVAL,
				     "DAOS agent support not compiled in");
#endif
	} else {
		rc = crt_perf_group_attach(group_id, info);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not attach to server group (%s)",
				       group_id);
	}

#ifdef CRT_PERF_HAS_DAOS_AGENT
	if (info->opts.daos_agent) {
		dc_put_attach_info(&mgmt_sys_info, attach_info_resp);
		dc_agent_fini();
	}
#endif

	perf_info_g = info;

	return 0;

error:
#ifdef CRT_PERF_HAS_DAOS_AGENT
	if (info->opts.daos_agent) {
		dc_put_attach_info(&mgmt_sys_info, attach_info_resp);
		dc_agent_fini();
	}
#endif
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
		uint32_t        tag_offset = perf_info->opts.daos_agent ? DAOS_RESERVED_CTXS : 0;

		*endpoint = (crt_endpoint_t){
		    .ep_grp  = perf_info->ep_group,
		    .ep_rank = (request_global_id / perf_info->ep_tags) % perf_info->ep_ranks,
		    .ep_tag  = (request_global_id % perf_info->ep_tags) + tag_offset};

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
	D_ALIGNED_ALLOC(info->rpc_buf, page_size, opts->buf_size_max);
	CRT_PERF_CHECK_ERROR(info->rpc_buf == NULL, error, rc, -DER_NOMEM,
			     "D_ALIGNED_ALLOC(%zu, %zu) failed", page_size, opts->buf_size_max);

	/* Init data */
	crt_perf_init_data(info->rpc_buf, opts->buf_size_max);

	return 0;

error:
	D_FREE(info->rpc_buf);

	return rc;
}

int
crt_perf_bulk_buf_init(const struct crt_perf_info *perf_info, struct crt_perf_context_info *info,
		       crt_bulk_op_t bulk_op)
{
	const struct crt_perf_opts *opts      = &perf_info->opts;
	size_t                      comm_rank = (size_t)perf_info->mpi_info.rank,
				    comm_size = (size_t)perf_info->mpi_info.size;
	crt_bulk_perm_t         bulk_perm = (bulk_op == CRT_BULK_GET) ? CRT_BULK_RO : CRT_BULK_WO;
	struct crt_perf_request args      = {.expected_count = opts->request_max,
					     .complete_count = 0,
					     .rc             = 0,
					     .done           = false,
					     .cb             = NULL,
					     .arg            = NULL};
	int                     rc;
	size_t                  i;

	/* Clients keep request_max bulk handles */
	info->handle_max   = opts->request_max;
	info->buf_count    = opts->buf_count;
	info->buf_size_max = opts->buf_size_max;

	rc = crt_perf_bulk_buf_alloc(info, bulk_perm, bulk_op == CRT_BULK_GET, !opts->force_reg);
	CRT_PERF_CHECK_D_ERROR(error, rc, "could not allocate bulk buffers");

	for (i = 0; i < opts->request_max; i++) {
		struct crt_perf_rpc            *request          = &info->requests[i];
		size_t                          handle_global_id = comm_rank + i * comm_size,
						target_max = perf_info->ep_ranks * perf_info->ep_tags,
						target_rank = handle_global_id % target_max;
		struct crt_perf_bulk_init_info *bulk_init_info;

		rc = crt_req_create(info->context, &request->endpoint,
				    CRT_PERF_ID(CRT_PERF_BW_INIT), &request->rpc);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not create request");

		bulk_init_info  = crt_req_get(request->rpc);
		*bulk_init_info = (struct crt_perf_bulk_init_info){
		    .bulk    = (!opts->force_reg) ? info->local_bulk_handles[i] : CRT_BULK_NULL,
		    .bulk_op = (uint32_t)bulk_op,
		    .global_handle_id = (uint32_t)handle_global_id,
		    .buf_count        = (uint32_t)opts->buf_count,
		    .size_max         = (uint32_t)opts->buf_size_max,
		    .request_max      = (uint32_t)opts->request_max,
		    .comm_size        = (uint32_t)comm_size,
		    .target_rank      = (uint32_t)target_rank,
		    .target_max       = (uint32_t)target_max,
		    .verify           = (uint8_t)opts->verify};

		D_INFO("(%zu) global handle ID %zu to %zu\n", comm_rank, handle_global_id,
		       target_rank);

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
		printf("# WARNING number of requests in flight (%zu) less than number of "
		       "targets (%zu)\n",
		       opts->request_max * (size_t)perf_info->mpi_info.size,
		       (size_t)(perf_info->ep_ranks * perf_info->ep_tags));
	if (opts->verify)
		printf("# WARNING verifying data, output will be slower\n");
	printf("%-*s%*s%*s\n", 10, "# Size", NWIDTH, "\"Rate (ops/s)\"", NWIDTH, "\"Time (us)\"");
	fflush(stdout);
}

int
crt_perf_run_lat(const struct crt_perf_info *perf_info, struct crt_perf_context_info *info,
		 size_t buf_size, size_t skip)
{
	const struct crt_perf_opts *opts = &perf_info->opts;
	int (*lat_req_send)(const struct crt_perf_info *, struct crt_perf_context_info *, size_t,
			    unsigned int, struct crt_perf_request *) =
	    opts->daos_tse ? crt_perf_lat_req_send_tse : crt_perf_lat_req_send;
	struct timespec t1, t2;
	size_t          i;
	int             rc;

	/* Warm up for RPC */
	for (i = 0; i < skip + (size_t)opts->loop; i++) {
		struct crt_perf_request args = {
		    .expected_count = (int32_t)opts->request_max,
		    .complete_count = 0,
		    .rc             = 0,
		    .done           = false,
		    .cb             = (opts->verify && opts->bidir) ? crt_perf_rpc_verify : NULL,
		    .arg            = NULL};
		unsigned int j;

		if (i == skip) {
			if (perf_info->mpi_info.size > 1)
				crt_perf_mpi_barrier(&perf_info->mpi_info);
			d_gettime(&t1);
		}

		for (j = 0; j < opts->request_max; j++) {
			rc = lat_req_send(perf_info, info, buf_size, j, &args);
			CRT_PERF_CHECK_D_ERROR(error, rc, "could not send request");
		}

		rc = crt_perf_request_wait(perf_info, info, CRT_PERF_TIMEOUT, &args);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not wait for requests");

		if (opts->verify && opts->daos_agent) {
			for (j = 0; j < opts->request_max; j++) {
				struct crt_perf_rpc           *request = &info->requests[j];
				struct crt_perf_daos_rate_out *out_resp =
				    crt_reply_get(request->rpc);
				CRT_PERF_CHECK_D_ERROR(error, rc, "could not get reply struct");

				CRT_PERF_CHECK_ERROR(
				    out_resp->rc != 0, error, rc, -DER_INVAL,
				    "Error detected in bulk transfer, buf[%d] = %d, "
				    "was expecting %d!",
				    out_resp->idx, out_resp->val, out_resp->idx);
			}
		}
	}

	if (perf_info->mpi_info.size > 1)
		crt_perf_mpi_barrier(&perf_info->mpi_info);

	d_gettime(&t2);

	if (perf_info->mpi_info.rank == 0)
		crt_perf_print_lat(perf_info, info, buf_size, d_timediff(&t1, &t2));

	return 0;

error:
	return rc;
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

int
crt_perf_run_bw(const struct crt_perf_info *perf_info, struct crt_perf_context_info *info,
		size_t buf_size, size_t skip, crt_bulk_op_t bulk_op)
{
	struct timespec             t1, t2;
	size_t                      i;
	int                         rc;
	const struct crt_perf_opts *opts = &perf_info->opts;
	enum crt_perf_rpc_id        rpc_id =
	    (bulk_op == CRT_BULK_GET) ? CRT_PERF_BW_WRITE : CRT_PERF_BW_READ;

	/* Warm up for RPC */
	for (i = 0; i < skip + (size_t)opts->loop; i++) {
		struct crt_perf_request args = {.expected_count = (int32_t)opts->request_max,
						.complete_count = 0,
						.rc             = 0,
						.done           = false,
						.cb             = NULL,
						.arg            = NULL};
		unsigned int            j;

		if (i == skip) {
			if (perf_info->mpi_info.size > 1)
				crt_perf_mpi_barrier(&perf_info->mpi_info);
			d_gettime(&t1);
		}

		if (perf_info->opts.force_reg) {
			crt_bulk_perm_t bulk_perm =
			    (bulk_op == CRT_BULK_GET) ? CRT_BULK_RO : CRT_BULK_WO;
			size_t alloc_size = info->buf_size_max * info->buf_count;

			for (j = 0; j < opts->request_max; j++) {
				d_iov_t     iov = {.iov_buf     = info->bulk_bufs[j],
						   .iov_buf_len = alloc_size,
						   .iov_len     = alloc_size};
				d_sg_list_t sgl = {.sg_nr = 1, .sg_nr_out = 0, .sg_iovs = &iov};

				rc = crt_bulk_create(info->context, &sgl, bulk_perm,
						     &info->local_bulk_handles[j]);
				CRT_PERF_CHECK_D_ERROR(error, rc, "could not register bulk buffer");
			}
		}

		for (j = 0; j < opts->request_max; j++) {
			struct crt_perf_rpc       *request = &info->requests[j];
			struct crt_perf_bulk_info *in_struct;

			rc = crt_req_create(info->context, &request->endpoint, CRT_PERF_ID(rpc_id),
					    &request->rpc);
			CRT_PERF_CHECK_D_ERROR(error, rc, "could not create request");

			in_struct  = crt_req_get(request->rpc);
			*in_struct = (struct crt_perf_bulk_info){
			    .bulk      = (perf_info->opts.force_reg) ? info->local_bulk_handles[j]
								     : CRT_BULK_NULL,
			    .handle_id = (uint32_t)((perf_info->mpi_info.rank +
						     j * perf_info->mpi_info.size) /
						    (perf_info->ep_ranks * perf_info->ep_tags)),
			    .size      = (uint32_t)buf_size};

			rc = crt_req_send(request->rpc, crt_perf_request_complete, &args);
			CRT_PERF_CHECK_D_ERROR(error, rc,
					       "could not send request to %" PRIu32 ":%" PRIu32,
					       request->endpoint.ep_rank, request->endpoint.ep_tag);
		}

		rc = crt_perf_request_wait(perf_info, info, CRT_PERF_TIMEOUT, &args);
		CRT_PERF_CHECK_D_ERROR(error, rc, "could not wait for requests");

		if (perf_info->opts.verify && bulk_op == CRT_BULK_PUT) {
			for (j = 0; j < opts->request_max; j++) {
				size_t k;
				for (k = 0; k < info->buf_count; k++) {
					char *buf_p =
					    (char *)info->bulk_bufs[j] + info->buf_size_max * k;
					rc = crt_perf_verify_data(buf_p, buf_size);
					CRT_PERF_CHECK_D_ERROR(error, rc, "could not verify data");
				}
			}
		}

		if (perf_info->opts.force_reg) {
			for (j = 0; j < opts->request_max; j++) {
				rc = crt_bulk_free(info->local_bulk_handles[j]);
				CRT_PERF_CHECK_D_ERROR(error, rc, "could not destroy bulk handle");
				info->local_bulk_handles[j] = CRT_BULK_NULL;
			}
		}
	}

	if (perf_info->mpi_info.size > 1)
		crt_perf_mpi_barrier(&perf_info->mpi_info);

	d_gettime(&t2);

	if (perf_info->mpi_info.rank == 0)
		crt_perf_print_bw(perf_info, info, buf_size, d_timediff(&t1, &t2));

	return 0;

error:
	return rc;
}

int
crt_perf_send_done(const struct crt_perf_info *perf_info, struct crt_perf_context_info *info)
{
	uint32_t ep_rank, ep_tag;
	int      rc;

	/* When using the DAOS agent, do not send termination/done RPC to the server */
	if (perf_info->opts.daos_agent) {
		D_INFO("Skipping done RPC (DAOS agent)\n");
		return 0;
	}

	for (ep_rank = 0; ep_rank < perf_info->ep_ranks; ep_rank++) {
		for (ep_tag = 0; ep_tag < perf_info->ep_tags; ep_tag++) {
			crt_endpoint_t target_ep = {
			    .ep_grp = perf_info->ep_group, .ep_rank = ep_rank, .ep_tag = ep_tag};

			D_INFO("Sending done RPC to %d:%d\n", ep_rank, ep_tag);
			rc = crt_perf_send_rpc_wait(info->context, &target_ep,
						    CRT_PERF_ID(CRT_PERF_DONE), NULL, NULL);
			CRT_PERF_CHECK_D_ERROR(error, rc, "could not send rpc");
		}
	}

	return 0;

error:
	return rc;
}
