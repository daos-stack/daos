/**
 * (C) Copyright 2017 Intel Corporation.
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
#define D_LOGFAC	DD_FAC(rdb)

#include <getopt.h>
#include <daos/rpc.h>
#include "rpc.h"

const char	       *default_group;
const d_rank_t	default_rank;
const uint32_t		default_nreplicas = 1;

crt_context_t context;

typedef int (*command_hdlr_t)(int, char *[]);

static int
help_hdlr(int argc, char *argv[])
{
	printf("\
usage: rdbt COMMAND [OPTIONS]\n\
commands:\n\
  init	init a replica\n\
  fini	finalize a replica\n\
  test	invoke tests on a replica\n\
  help	print this message and exit\n");
	printf("\
init options:\n\
  --group=GROUP	server group \n\
  --rank=RANK	rank to initialize (0)\n\
  --replicas=N	number of replicas (1)\n\
  --uuid=UUID	rdb UUID\n");
	printf("\
fini options:\n\
  --group=GROUP	server group \n\
  --rank=RANK	rank to initialize (0)\n");
	printf("\
test options:\n\
  --group=GROUP	server group \n\
  --rank=RANK	rank to invoke tests on (0)\n\
  --update	update (otherwise verify)\n");
	return 0;
}

static void
rpc_cb(const struct crt_cb_info *cb_info)
{
	int *rc = cb_info->cci_arg;

	*rc = cb_info->cci_rc;
}

static crt_rpc_t *
create_rpc(crt_opcode_t opc, crt_group_t *group, d_rank_t rank)
{
	crt_opcode_t	opcode = DAOS_RPC_OPCODE(opc, DAOS_RDBT_MODULE,
						 DAOS_RDBT_VERSION);
	crt_endpoint_t	ep;
	crt_rpc_t      *rpc;
	int		rc;

	ep.ep_grp = group;
	ep.ep_rank = rank;
	ep.ep_tag = daos_rpc_tag(DAOS_REQ_RDB, 0);
	rc = crt_req_create(context, &ep, opcode, &rpc);
	D_ASSERTF(rc == 0, "%d\n", rc);
	return rpc;
}

static int
invoke_rpc(crt_rpc_t *rpc)
{
	const int	rpc_rc_uninitialized = 20170502;
	int		rpc_rc = rpc_rc_uninitialized;
	int		rc;

	crt_req_addref(rpc);
	rc = crt_req_send(rpc, rpc_cb, &rpc_rc);
	D_ASSERTF(rc == 0, "%d\n", rc);
	/* Sloppy... */
	while (rpc_rc == rpc_rc_uninitialized)
		crt_progress(context, 0);
	return rpc_rc;
}

static void
destroy_rpc(crt_rpc_t *rpc)
{
	crt_req_decref(rpc);
}

static int
rdbt_init(crt_group_t *group, d_rank_t rank, uuid_t uuid, uint32_t nreplicas)
{
	crt_rpc_t	       *rpc;
	struct rdbt_init_in    *in;
	struct rdbt_init_out   *out;
	int			rc;

	rpc = create_rpc(RDBT_INIT, group, rank);
	in = crt_req_get(rpc);
	uuid_copy(in->tii_uuid, uuid);
	in->tii_nreplicas = nreplicas;
	rc = invoke_rpc(rpc);
	D_ASSERTF(rc == 0, "%d\n", rc);
	out = crt_reply_get(rpc);
	rc = out->tio_rc;
	destroy_rpc(rpc);
	return rc;
}

static int
rdbt_fini(crt_group_t *group, d_rank_t rank)
{
	crt_rpc_t	       *rpc;
	struct rdbt_fini_out   *out;
	int			rc;

	rpc = create_rpc(RDBT_FINI, group, rank);
	rc = invoke_rpc(rpc);
	D_ASSERTF(rc == 0, "%d\n", rc);
	out = crt_reply_get(rpc);
	rc = out->tfo_rc;
	destroy_rpc(rpc);
	return rc;
}

static int
rdbt_test(crt_group_t *group, d_rank_t rank, int update)
{
	crt_rpc_t	       *rpc;
	struct rdbt_test_in    *in;
	struct rdbt_test_out   *out;
	int			rc;

	rpc = create_rpc(RDBT_TEST, group, rank);
	in = crt_req_get(rpc);
	in->tti_update = update;
	rc = invoke_rpc(rpc);
	D_ASSERTF(rc == 0, "%d\n", rc);
	out = crt_reply_get(rpc);
	rc = out->tto_rc;
	destroy_rpc(rpc);
	return rc;
}

static int
init_hdlr(int argc, char *argv[])
{
	struct option	options[] = {
		{"group",	required_argument,	NULL,	'g'},
		{"rank",	required_argument,	NULL,	'r'},
		{"replicas",	required_argument,	NULL,	'R'},
		{"uuid",	required_argument,	NULL,	'u'},
		{NULL,		0,			NULL,	0}
	};
	const char     *group_id = default_group;
	d_rank_t	rank = default_rank;
	uint32_t	nreplicas = default_nreplicas;
	uuid_t		uuid;
	crt_group_t    *group;
	int		rc;

	while ((rc = getopt_long(argc, argv, "", options, NULL)) != -1) {
		switch (rc) {
		case 'g':
			group_id = optarg;
			break;
		case 'r':
			rank = atoi(optarg);
			break;
		case 'R':
			nreplicas = atoi(optarg);
			break;
		case 'u':
			rc = uuid_parse(optarg, uuid);
			if (rc != 0) {
				fprintf(stderr, "invalid uuid `%s'\n", optarg);
				return 2;
			}
			break;
		default:
			return 2;
		}
	}

	rc = crt_group_attach((char *)group_id, &group);
	if (rc != 0)
		return rc;

	return rdbt_init(group, rank, uuid, nreplicas);
}

static int
fini_hdlr(int argc, char *argv[])
{
	struct option	options[] = {
		{"group",	required_argument,	NULL,	'g'},
		{"rank",	required_argument,	NULL,	'r'},
		{NULL,		0,			NULL,	0}
	};
	const char     *group_id = default_group;
	d_rank_t	rank = default_rank;
	crt_group_t    *group;
	int		rc;

	while ((rc = getopt_long(argc, argv, "", options, NULL)) != -1) {
		switch (rc) {
		case 'g':
			group_id = optarg;
			break;
		case 'r':
			rank = atoi(optarg);
			break;
		default:
			return 2;
		}
	}

	rc = crt_group_attach((char *)group_id, &group);
	if (rc != 0)
		return rc;

	return rdbt_fini(group, rank);
}

static int
test_hdlr(int argc, char *argv[])
{
	struct option	options[] = {
		{"group",	required_argument,	NULL,	'g'},
		{"rank",	required_argument,	NULL,	'r'},
		{"update",	no_argument,		NULL,	'U'},
		{NULL,		0,			NULL,	0}
	};
	const char     *group_id = default_group;
	d_rank_t	rank = default_rank;
	int		update = 0;
	crt_group_t    *group;
	int		rc;

	while ((rc = getopt_long(argc, argv, "", options, NULL)) != -1) {
		switch (rc) {
		case 'g':
			group_id = optarg;
			break;
		case 'r':
			rank = atoi(optarg);
			break;
		case 'U':
			update = 1;
			break;
		default:
			return 2;
		}
	}

	rc = crt_group_attach((char *)group_id, &group);
	if (rc != 0)
		return rc;

	return rdbt_test(group, rank, update);
}

int
main(int argc, char *argv[])
{
	command_hdlr_t	hdlr = NULL;
	int		rc;

	if (argc == 1 || strcmp(argv[1], "help") == 0)
		hdlr = help_hdlr;
	else if (strcmp(argv[1], "init") == 0)
		hdlr = init_hdlr;
	else if (strcmp(argv[1], "fini") == 0)
		hdlr = fini_hdlr;
	else if (strcmp(argv[1], "test") == 0)
		hdlr = test_hdlr;

	if (hdlr == NULL || hdlr == help_hdlr) {
		help_hdlr(argc, argv);
		return hdlr == NULL ? 2 : 0;
	}

	rc = crt_init_opt(NULL, 0, daos_crt_init_opt_get(false, 1));
	D_ASSERTF(rc == 0, "%d\n", rc);
	rc = crt_context_create(&context);
	D_ASSERTF(rc == 0, "%d\n", rc);
	rc = daos_rpc_register(&rdbt_proto_fmt, RDBT_PROTO_CLI_COUNT,
				NULL, DAOS_RDBT_MODULE);
	D_ASSERTF(rc == 0, "%d\n", rc);

	rc = hdlr(argc, argv);

	crt_context_destroy(context, 1 /* force */);
	crt_finalize();
	if (rc < 0) {
		return 1;
	} else if (rc > 0) {
		help_hdlr(argc, argv);
		return 2;
	}
	return 0;
}
