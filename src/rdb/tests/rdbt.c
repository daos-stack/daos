/**
 * (C) Copyright 2017-2020 Intel Corporation.
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
#include <daos.h>
#include <daos/mgmt.h>
#include <daos/rpc.h>
#include <daos/rsvc.h>
#include "rpc.h"

const d_rank_t		default_rank;
const uint64_t		RDBT_KEY = 0xDA05DA05DA05DA05;


static const char	       *group_id;
static uint32_t			g_nranks = 1;
static uint32_t			g_nreps = 1;
static struct dc_mgmt_sys       *sys;

crt_context_t context;

typedef int (*command_hdlr_t)(int, char *[]);

static int
help_hdlr(int argc, char *argv[])
{
	printf("\
usage: rdbt COMMAND [OPTIONS]\n\
commands:\n\
  init		init a replica\n\
  create	create KV stores (on discovered leader)\n\
  test		invoke tests on a specified replica rank\n\
  test-multi	invoke tests (on discovered leader)\n\
  destroy	destroy KV stores (on discovered leader)\n\
  fini		finalize a replica\n\
  help		print this message and exit\n");
	printf("\
init options:\n\
  --group=GROUP	server group \n\
  --rank=RANK	rank to initialize (0)\n\
  --replicas=N	number of replicas (1)\n\
  --uuid=UUID	rdb UUID\n");
	printf("\
create, test-multi, destroy options:\n\
  --group=GROUP	server group \n\
  --replicas=N	number of replicas (1)\n\
  --nranks=R	number of server ranks (1)\n");
	printf("\
test options:\n\
  --group=GROUP	server group \n\
  --rank=RANK	rank to invoke tests on (0)\n\
  --update	update (otherwise verify)\n");
	printf("\
fini options:\n\
  --group=GROUP	server group \n\
  --rank=RANK	rank to finalize (0)\n");
	return 0;
}

/**** Common utility functions for multiple tests. */

static int
multi_tests_common_parse(int argc, char *argv[])
{
	struct option		options[] = {
		{"group",	required_argument,	NULL,	'g'},
		{"nranks",	required_argument,	NULL,	'n'},
		{"replicas",	required_argument,	NULL,	'R'},
		{NULL,		0,			NULL,	0}
	};
	int			rc;

	while ((rc = getopt_long(argc, argv, "", options, NULL)) != -1) {
		switch (rc) {
		case 'g':
			group_id = optarg;
			break;
		case 'n':
			g_nranks = atoi(optarg);
			break;
		case 'R':
			g_nreps = atoi(optarg);
			break;
		default:
			return 2;
		}
	}

	D_ASSERTF((g_nreps < g_nranks), "g_nreps=%u <= g_nranks=%u\n",
		  g_nreps, g_nranks);

	return dc_mgmt_sys_attach(group_id, &sys);
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
rdbt_ping_rank(crt_group_t *group, d_rank_t rank, struct rsvc_hint *hintp)
{
	crt_rpc_t	       *rpc;
	struct rdbt_ping_out   *out;
	int			rc;

	rpc = create_rpc(RDBT_PING, group, rank);
	rc = invoke_rpc(rpc);
	D_ASSERTF(rc == 0, "%d\n", rc);
	out = crt_reply_get(rpc);
	rc = out->tpo_rc;
	*hintp = out->tpo_hint;
	destroy_rpc(rpc);
	return rc;
}

static int
rdbt_find_leader(crt_group_t *group, uint32_t nranks, uint32_t nreplicas,
		      d_rank_t *leaderp, uint64_t *termp)
{
	uint32_t		 rank;
	uint32_t		 notleaders = 0;
	uint32_t		 notreplicas = 0;
	d_rank_t		 ldr_rank;
	uint64_t		 term = 0;
	bool			 found_leader = false;
	int			 rc;
	int			 rc_svc;

	for (rank = 0; rank < nranks ; rank++) {
		bool hint_isvalid;
		struct rsvc_hint	h;
		bool			resp_isvalid = true;

		rc_svc = rdbt_ping_rank(group, rank, &h);
		hint_isvalid = (h.sh_flags & RSVC_HINT_VALID);

		if ((rc_svc == -DER_NOTLEADER) && !hint_isvalid) {
			resp_isvalid = (rank < nreplicas);
			if (!resp_isvalid)
				break;
			notleaders++;
		} else if (rc_svc == -DER_NOTLEADER) {
			resp_isvalid = (rank < nreplicas);
			if (!resp_isvalid)
				break;
			notleaders++;
			if (found_leader) {
				/* update leader rank and term if applicable */
				if (h.sh_term == term) {
					if (h.sh_rank != ldr_rank)
						printf("WARN: NL rank %u term "
						       DF_U64" bad leader=%u "
						       "!= leader=%u\n", rank,
						       h.sh_term, h.sh_rank,
						       ldr_rank);
				} else if (h.sh_term > term) {
					ldr_rank = h.sh_rank;
					term = h.sh_term;
				} else {
					printf("WARN: NL rank %u has stale ldr "
					       "rank=%u, term="DF_U64"\n",
					       rank, h.sh_rank, h.sh_term);
				}
			} else {
				ldr_rank = h.sh_rank;
				term = h.sh_term;
				found_leader = true;
			}
		} else if (rc_svc == -DER_NOTREPLICA) {
			resp_isvalid = (rank >= nreplicas);
			if (!resp_isvalid)
				break;
			notreplicas++;
		} else if (!hint_isvalid) {
			/* Leader reply without a hint */
			resp_isvalid = ((rc_svc == 0) && (rank < nreplicas));
			if (!resp_isvalid)
				break;
			if (found_leader) {
				if (rank != ldr_rank) {
					printf("WARN: rank=%u leader reply, vs."
					       "leader (rank=%u, term="
					       DF_U64")\n", rank, ldr_rank,
					       term);
					ldr_rank = rank;
				}
			} else {
				ldr_rank = rank;
				/* unknown term */
				found_leader = true;
			}
		} else {
			/* Leader reply with a hint (does it happen)? */
			resp_isvalid = ((rc_svc == 0) && (rank < nreplicas));
			if (!resp_isvalid)
				break;
			if (found_leader) {
				/* reject if h.sh_term lower? */
				if (rank != ldr_rank) {
					printf("WARN: rank=%u leader reply "
					       "term="DF_U64" vs. "
					       "leader (rank=%u, term="
					       DF_U64")\n", rank, h.sh_term,
					       ldr_rank, term);
					ldr_rank = rank;
					term = h.sh_term;
				}
			} else {
				found_leader = true;
				ldr_rank = rank;
				term = h.sh_term;
			}
		}

		if (!resp_isvalid) {
			printf("ERR: rank %u invalid reply: rc="DF_RC", "
			       "hint is %s valid (rank=%u, term="DF_U64")\n",
			       rank, DP_RC(rc_svc), resp_isvalid ? "" : "NOT",
			       h.sh_rank, h.sh_term);
			rc = -1;
			break;
		}
		rc = 0;
	}

	if ((rc == 0) && found_leader) {
		printf("INFO: found leader rank=%u, term="DF_U64
		       ", non-leaders: %u, non-replicas: %u\n",
		       ldr_rank, term, notleaders, notreplicas);
		*leaderp = ldr_rank;
		*termp = term;
	} else if (!found_leader) {
		printf("ERR: no leader found!\n");
		return -1;
	}

	return rc;
}

static int
wait_for_leader(crt_group_t *group, uint32_t nranks, uint32_t nreplicas,
		d_rank_t expect_ldr, uint64_t expect_term_min)
{
	int			rc;
	int			try;
	d_rank_t		new_ldr;
	uint64_t		new_term;
	const unsigned int	SLEEP_SEC = 2;
	const unsigned int	TRY_LIMIT = 6;

	for (try = 0; try < TRY_LIMIT; try++) {
		sleep(SLEEP_SEC);
		rc = rdbt_find_leader(group, nranks, nreplicas, &new_ldr,
				      &new_term);
		if (rc == 0)
			break;
		printf("try %u/%u: no leader found yet, rc: "DF_RC"\n",
		       (try+1), TRY_LIMIT, DP_RC(rc));
	}
	if (rc == 0) {
		if (new_ldr != expect_ldr) {
			fprintf(stderr, "ERR: leader %u (expected %u)\n",
					new_ldr, expect_ldr);
			return -1;
		}

		if (new_term < expect_term_min) {
			fprintf(stderr, "ERR: term "DF_U64" < expected "
					DF_U64"\n", new_term, expect_term_min);
			return -1;
		}
	} else {
		fprintf(stderr, "FAIL: find leader after add replica\n");
		return rc;
	}

	printf("INFO: leader=%u, term="DF_U64"\n", new_ldr, new_term);
	return 0;
}

static int
wait_for_new_leader(crt_group_t *group, uint32_t nranks, uint32_t nreplicas,
		    d_rank_t orig_ldr, uint64_t expect_term_min,
		    d_rank_t *new_ldrp)
{
	int			rc;
	int			try;
	d_rank_t		new_ldr;
	uint64_t		new_term;
	const unsigned int	SLEEP_SEC = 2;
	const unsigned int	TRY_LIMIT = 6;

	for (try = 0; try < TRY_LIMIT; try++) {
		sleep(SLEEP_SEC);
		rc = rdbt_find_leader(group, nranks, nreplicas, &new_ldr,
				      &new_term);
		if ((rc == 0) && (new_ldr != orig_ldr) &&
		    (new_term >= expect_term_min))
			break;

		printf("try %u/%u: new leader not found yet, rc: "
		       DF_RC"\n", (try+1), TRY_LIMIT, DP_RC(rc));
	}
	if (rc == 0) {
		if (new_ldr == orig_ldr) {
			fprintf(stderr, "ERR: same leader %u (expected new)\n",
					new_ldr);
			return -1;
		}

		if (new_term < expect_term_min) {
			fprintf(stderr, "ERR: term "DF_U64" < expected "
					DF_U64"\n", new_term, expect_term_min);
			return -1;
		}
	} else {
		fprintf(stderr, "FAIL: find leader after add replica\n");
		return rc;
	}

	*new_ldrp = new_ldr;
	printf("INFO: leader=%u, term="DF_U64"\n", new_ldr, new_term);
	return 0;
}

static int
rdbt_start_election(crt_group_t *group, d_rank_t rank)
{
	crt_rpc_t			*rpc;
	struct rdbt_start_election_out	*out;
	int				 rc;

	rpc = create_rpc(RDBT_START_ELECTION, group, rank);
	rc = invoke_rpc(rpc);
	D_ASSERTF(rc == 0, "%d\n", rc);
	out = crt_reply_get(rpc);
	rc = out->rtse_rc;
	destroy_rpc(rpc);
	return rc;
}

static int
rdbt_add_replica_rank(crt_group_t *group, d_rank_t ldr_rank, d_rank_t new_rank,
		      struct rsvc_hint *hintp)
{
	crt_rpc_t			*rpc;
	struct rdbt_replicas_add_in	*in;
	struct rdbt_replicas_add_out	*out;
	d_rank_list_t			*replicas_to_add;
	int				 rc;

	rpc = create_rpc(RDBT_REPLICAS_ADD, group, ldr_rank);
	in = crt_req_get(rpc);
	replicas_to_add = d_rank_list_alloc(1);
	replicas_to_add->rl_ranks[0] = new_rank;
	in->rtmi_ranks = replicas_to_add;
	rc = invoke_rpc(rpc);
	D_ASSERTF(rc == 0, "%d\n", rc);
	out = crt_reply_get(rpc);
	rc = out->rtmo_rc;
	*hintp = out->rtmo_hint;
	if (out->rtmo_failed != NULL)
		fprintf(stderr, "ERR: adding replica %u (reply rank %u)\n",
				new_rank, out->rtmo_failed->rl_ranks[0]);
	destroy_rpc(rpc);
	return rc;
}

/**** init command functions ****/

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
init_hdlr(int argc, char *argv[])
{
	struct option		 options[] = {
		{"group",	required_argument,	NULL,	'g'},
		{"rank",	required_argument,	NULL,	'r'},
		{"replicas",	required_argument,	NULL,	'R'},
		{"uuid",	required_argument,	NULL,	'u'},
		{NULL,		0,			NULL,	0}
	};
	d_rank_t		 rank = default_rank;
	uuid_t			 uuid;
	int			 rc;

	while ((rc = getopt_long(argc, argv, "", options, NULL)) != -1) {
		switch (rc) {
		case 'g':
			group_id = optarg;
			break;
		case 'r':
			rank = atoi(optarg);
			break;
		case 'R':
			g_nreps = atoi(optarg);
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

	rc = dc_mgmt_sys_attach(group_id, &sys);
	if (rc != 0)
		return rc;

	return rdbt_init(sys->sy_group, rank, uuid, g_nreps);
}

/**** create command functions ****/

/* Send RDBT_CREATE RPC to service leader after init, create KV stores */
static int
rdbt_create_rank(crt_group_t *group, d_rank_t rank, struct rsvc_hint *hintp)
{
	crt_rpc_t	       *rpc;
	struct rdbt_create_out *out;
	int			rc;

	rpc = create_rpc(RDBT_CREATE, group, rank);
	rc = invoke_rpc(rpc);
	D_ASSERTF(rc == 0, "%d\n", rc);
	out = crt_reply_get(rpc);
	rc = out->tco_rc;
	*hintp = out->tco_hint;
	destroy_rpc(rpc);
	return rc;
}

static int
rdbt_create_multi(crt_group_t *group, uint32_t nranks, uint32_t nreplicas)
{
	int			rc;
	d_rank_t		junk_rank = nranks + 1000;
	d_rank_t		ldr_rank = junk_rank;
	uint64_t		term;
	struct rsvc_hint	h;

	rc = rdbt_find_leader(group, nranks, nreplicas, &ldr_rank, &term);
	if (rc) {
		fprintf(stderr, "ERR: RDB find leader failed\n");
		return rc;
	}
	printf("Discovered leader %u, term="DF_U64"\n", ldr_rank, term);

	printf("===== Create RDB KV stores on leader %u\n", ldr_rank);
	rc = rdbt_create_rank(group, ldr_rank, &h);
	if (rc) {
		fprintf(stderr, "ERR: create RDB KV stores failed RPC to "
				"leader %u: "DF_RC", hint:(r=%u, t="DF_U64"\n",
				ldr_rank, DP_RC(rc), h.sh_rank, h.sh_term);
		return rc;
	}
	printf("Created RDB KV stores, via RPC to leader %u\n", ldr_rank);

	return rc;
}

static int
create_hdlr(int argc, char *argv[])
{
	int			 rc;

	rc = multi_tests_common_parse(argc, argv);
	if (rc != 0)
		return rc;

	return rdbt_create_multi(sys->sy_group, g_nranks, g_nreps);
}

/**** test command functions ****/

static int
rdbt_test_rank(crt_group_t *group, d_rank_t rank, int update,
	       enum rdbt_membership_op memb_op, uint64_t user_key,
	       uint64_t user_val_in, uint64_t *user_val_outp,
	       struct rsvc_hint *hintp)
{
	crt_rpc_t	       *rpc;
	struct rdbt_test_in    *in;
	struct rdbt_test_out   *out;
	int			rc;

	rpc = create_rpc(RDBT_TEST, group, rank);
	in = crt_req_get(rpc);
	in->tti_update = update;
	in->tti_memb_op = memb_op;
	in->tti_key = user_key;
	in->tti_val = user_val_in;
	rc = invoke_rpc(rpc);
	D_ASSERTF(rc == 0, "%d\n", rc);
	out = crt_reply_get(rpc);
	rc = out->tto_rc;
	if (user_val_outp)
		*user_val_outp = out->tto_val;
	if (hintp)
		*hintp = out->tto_hint;
	destroy_rpc(rpc);
	return rc;
}

static int
test_hdlr(int argc, char *argv[])
{
	struct option		options[] = {
		{"group",	required_argument,	NULL,	'g'},
		{"rank",	required_argument,	NULL,	'r'},
		{"update",	no_argument,		NULL,	'U'},
		{NULL,		0,			NULL,	0}
	};
	d_rank_t		rank = default_rank;
	int			update = 0;
	const uint64_t		key = RDBT_KEY;
	const uint64_t		val_in = 987654321;
	uint64_t		val_out = 0;
	int			rc;

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

	rc = dc_mgmt_sys_attach(group_id, &sys);
	if (rc != 0)
		return rc;

	rc = rdbt_test_rank(sys->sy_group, rank, update, RDBT_MEMBER_NOOP,
			    key, val_in, &val_out, NULL /*hintp */);
	if (rc != 0)
		return rc;

	/* make sure to run test with update=true first */
	if (val_out != val_in) {
		fprintf(stderr, "ERR: val_out="DF_U64" expected "DF_U64"\n",
				val_out, val_in);
		return -1;
	}

	return 0;
}

/**** test-multi command functions ****/

static int
testm_update_lookup(crt_group_t *group, uint32_t nranks, uint32_t nreplicas,
		    uint64_t key, uint64_t val)
{
	int			rc;
	d_rank_t		ldr_rank;
	uint64_t		term;
	uint64_t		val_out = 0;
	struct rsvc_hint	h;
	const int		NO_UPDATE = 0;
	const int		UPDATE = 1;

	printf("==== TEST: RDB update then lookup from discovered leader\n");

	rc = rdbt_find_leader(group, nranks, nreplicas, &ldr_rank, &term);
	if (rc) {
		fprintf(stderr, "ERR: RDB find leader failed\n");
		return rc;
	}
	printf("INFO: RDB discovered leader rank %u, term="DF_U64"\n",
	       ldr_rank, term);

	rc = rdbt_test_rank(group, ldr_rank, UPDATE, RDBT_MEMBER_NOOP, key, val,
			    &val_out, &h);
	if (rc) {
		fprintf(stderr, "FAIL: update RDB failed via RPC to leader "
				"rank %u: "DF_RC", hint:(r=%u, t="DF_U64"\n",
				ldr_rank, DP_RC(rc), h.sh_rank, h.sh_term);
		return rc;
	}
	if (val_out != val) {
		fprintf(stderr, "FAIL: update val="DF_U64" expect "DF_U64"\n",
				val_out, val);
		return -1;
	}

	val_out = 0;
	rc = rdbt_test_rank(group, ldr_rank, NO_UPDATE, RDBT_MEMBER_NOOP,
			    key, val, &val_out, &h);
	if (rc) {
		fprintf(stderr, "FAIL: lookup RDB failed via RPC to leader "
			       "rank %u: "DF_RC", hint:(r=%u, t="DF_U64"\n",
			       ldr_rank, DP_RC(rc), h.sh_rank, h.sh_term);
		return rc;
	}
	if (val_out != val) {
		fprintf(stderr, "FAIL: lookup val="DF_U64" expect "DF_U64"\n",
				val_out, val);
		return -1;
	}
	printf("====== PASS: update/lookup: RDB via RPC to leader rank %u "
	       "(K=0x%"PRIx64", V="DF_U64")\n", ldr_rank, key, val_out);

	return 0;
}

static int
testm_update_lookup_all(crt_group_t *group, uint32_t nranks, uint32_t nreplicas,
			uint64_t key, uint64_t val)
{
	int			rc;
	d_rank_t		ldr_rank;
	d_rank_t		orig_ldr_rank;
	d_rank_t		test_rank;
	uint64_t		term;
	uint64_t		val_out = 0;
	struct rsvc_hint	h;
	const int		NO_UPDATE = 0;
	const int		UPDATE = 1;

	printf("==== TEST: RDB update then lookup on all replicas\n");

	rc = rdbt_find_leader(group, nranks, nreplicas, &ldr_rank, &term);
	if (rc) {
		fprintf(stderr, "ERR: RDB find leader failed\n");
		return rc;
	}
	printf("INFO: RDB discovered leader rank %u, term="DF_U64"\n",
	       ldr_rank, term);
	orig_ldr_rank = ldr_rank;

	for (test_rank = 0; test_rank < nreplicas; test_rank++) {
		if (test_rank == orig_ldr_rank)
			continue;

		/* First, have current leader update the value */
		val++;
		val_out = 0;
		rc = rdbt_test_rank(group, ldr_rank, UPDATE, RDBT_MEMBER_NOOP,
				    key, val, &val_out, &h);
		if (rc) {
			fprintf(stderr, "FAIL: update RDB failed via RPC to "
					"leader %u: "DF_RC", hint:(r=%u, "
					"t="DF_U64"\n", ldr_rank, DP_RC(rc),
					h.sh_rank, h.sh_term);
			return rc;
		}
		if (val_out != val) {
			fprintf(stderr, "FAIL: update val="DF_U64" expect "
					DF_U64"\n", val_out, val);
			return -1;
		}

		/* Make test_rank become leader, call election, expect to win */
		rc = rdbt_start_election(group, test_rank);
		if (rc != 0) {
			fprintf(stderr, "FAIL: start election from rank %u\n",
				test_rank);
			return rc;
		}

		printf("INFO: rank %u called for election. Sleep some\n",
		       test_rank);
		sleep(5);

		rc = wait_for_leader(group, nranks, nreplicas, test_rank,
				     (term+1));
		if (rc != 0) {
			fprintf(stderr, "FAIL: wait for leader %u term >= "
					DF_U64" after election: "DF_RC"\n",
					test_rank, (term+1), DP_RC(rc));
			return rc;
		}
		ldr_rank = test_rank;
		printf("INFO: replica rank %u is now leader\n", test_rank);

		/* Verify data on the rank now that it is leader */
		val_out = 0;
		rc = rdbt_test_rank(group, ldr_rank, NO_UPDATE,
				    RDBT_MEMBER_NOOP, key, val, &val_out, &h);
		if (rc != 0) {
			fprintf(stderr, "FAIL: lookup RDB failed via RPC to "
					"leader %u: "DF_RC", hint:(r=%u, "
					"t="DF_U64"\n", test_rank, DP_RC(rc),
					h.sh_rank, h.sh_term);
			return rc;
		}
		if (val_out != val) {
			fprintf(stderr, "FAIL: lookup val="DF_U64" expect "
					DF_U64"\n", val_out, val);
			return -1;
		}
		printf("INFO: update/lookup all replicas (rank %u): "
			"(K=0x%"PRIx64", V="DF_U64")\n", test_rank, key, val);
	}

	printf("====== PASS: update/lookup all replicas\n");

	return 0;
}

static int
testm_add_leader(crt_group_t *group, uint32_t nranks, uint32_t nreplicas,
		 d_rank_t new_rank, uint64_t key, uint64_t val)
{
	int			rc;
	d_rank_t		ldr_rank;
	d_rank_list_t		*stop_ranks;
	uint64_t		term;
	uint64_t		val_out = 0;
	struct rsvc_hint	h;
	uint32_t		new_nreplicas;
	const int		NO_UPDATE = 0;
	const int		UPDATE = 1;

	printf("==== TEST: RDB update, add leader replica, lookup from "
	       "new leader\n");

	stop_ranks = d_rank_list_alloc(nreplicas);
	if (stop_ranks == NULL)
		return -1;

	rc = rdbt_find_leader(group, nranks, nreplicas, &ldr_rank, &term);
	if (rc) {
		fprintf(stderr, "ERR: RDB find leader failed\n");
		goto out_ranks;
	}
	printf("INFO: RDB discovered leader rank %u, term="DF_U64"\n",
	       ldr_rank, term);

	rc = rdbt_test_rank(group, ldr_rank, UPDATE, RDBT_MEMBER_NOOP, key, val,
			    &val_out, &h);
	if (rc) {
		fprintf(stderr, "FAIL: update RDB failed via RPC to leader "
			       "rank %u: "DF_RC", hint:(r=%u, t="DF_U64"\n",
			       ldr_rank, DP_RC(rc), h.sh_rank, h.sh_term);
		goto out_ranks;
	}
	if (val_out != val) {
		fprintf(stderr, "FAIL: update val="DF_U64" expect "DF_U64"\n",
				val_out, val);
		rc = -1;
		goto out_ranks;
	}

	/* Add new replica, wait and confirm existing leader and same term */
	rc = rdbt_add_replica_rank(group, ldr_rank, new_rank, &h);
	if (rc) {
		fprintf(stderr, "FAIL: add replica rank %u RPC to leader %u: "
				DF_RC", hint:(r=%u, t="DF_U64"\n", new_rank,
				ldr_rank, DP_RC(rc), h.sh_rank, h.sh_term);
		goto out_ranks;
	}
	printf("INFO: added new replica rank %u. Sleep some\n", new_rank);

	/* Sleep a few seconds to allow added replica to catch up */
	sleep(5);

	new_nreplicas = nreplicas + 1;
#if 0
	rc = wait_for_leader(group, nranks, new_nreplicas, ldr_rank, term);
	if (rc != 0) {
		fprintf(stderr, "FAIL: waiting for leader after add replica: "
				DF_RC"\n", DP_RC(rc));
		goto out_ranks;
	}
	printf("INFO: rank %u remains leader after adding replica rank %u\n",
	       ldr_rank, new_rank);
#endif

	rc = rdbt_start_election(group, new_rank);
	if (rc != 0) {
		fprintf(stderr, "FAIL: start election from new rank %u\n",
				new_rank);
		goto out_ranks;
	}
	printf("INFO: new rank %u called for election. Sleep some\n",
	       new_rank);
	sleep(5);

	rc = wait_for_leader(group, nranks, new_nreplicas, new_rank /* ldr */,
			     (term+1));
	if (rc != 0) {
		fprintf(stderr, "FAIL: wait for new leader %u term >= "DF_U64
				"after stop replicas: "DF_RC"\n", new_rank,
				(term+1), DP_RC(rc));
		goto out_ranks;
	}
	printf("INFO: new replica rank %u is now leader\n", new_rank);

	/* Lookup user key/value from new leader */
	val_out = 0;
	rc = rdbt_test_rank(group, new_rank, NO_UPDATE, RDBT_MEMBER_NOOP,
			    key, val, &val_out, &h);
	if (rc) {
		fprintf(stderr, "FAIL: lookup RDB failed via RPC to new leader "
			       "rank %u: "DF_RC", hint:(r=%u, t="DF_U64"\n",
			       new_rank, DP_RC(rc), h.sh_rank, h.sh_term);
		goto out_ranks;
	}
	if (val_out != val) {
		fprintf(stderr, "FAIL: lookup val="DF_U64" expect "DF_U64"\n",
				val_out, val);
		rc = -1;
		goto out_ranks;
	}

	printf("====== PASS: RDB via RPC to new replica/leader rank %u "
	       "(K=0x%"PRIx64", V="DF_U64")\n", new_rank, key, val_out);

out_ranks:
	d_rank_list_free(stop_ranks);
	return rc;
}

static int
testm_add_follower(crt_group_t *group, uint32_t nranks, uint32_t nreplicas,
		   d_rank_t new_rank, uint64_t key, uint64_t val)
{
	int			rc;
	d_rank_t		ldr_rank;
	uint64_t		term;
	uint64_t		val_out = 0;
	struct rsvc_hint	h;
	uint32_t		new_nreplicas;
	const int		NO_UPDATE = 0;
	const int		UPDATE = 1;

	printf("==== TEST: RDB update, add follower replica, lookup from "
	       "original leader\n");

	rc = rdbt_find_leader(group, nranks, nreplicas, &ldr_rank, &term);
	if (rc) {
		fprintf(stderr, "ERR: RDB find leader failed\n");
		return rc;
	}
	printf("INFO: RDB discovered leader rank %u, term="DF_U64"\n",
	       ldr_rank, term);

	rc = rdbt_test_rank(group, ldr_rank, UPDATE, RDBT_MEMBER_NOOP, key, val,
			    &val_out, &h);
	if (rc) {
		fprintf(stderr, "FAIL: update RDB failed via RPC to leader "
				"rank %u: "DF_RC", hint:(r=%u, t="DF_U64"\n",
				ldr_rank, DP_RC(rc), h.sh_rank, h.sh_term);
		return rc;
	}
	if (val_out != val) {
		fprintf(stderr, "FAIL: update val="DF_U64" expect "DF_U64"\n",
				val_out, val);
		return -1;
	}

	/* Add new replica, wait and confirm existing leader and same term */
	rc = rdbt_add_replica_rank(group, ldr_rank, new_rank, &h);
	if (rc) {
		fprintf(stderr, "FAIL: add replica rank %u RPC to leader %u: "
				DF_RC", hint:(r=%u, t="DF_U64"\n", new_rank,
				ldr_rank, DP_RC(rc), h.sh_rank, h.sh_term);
		return rc;
	}

	new_nreplicas = nreplicas + 1;
	rc = wait_for_leader(group, nranks, new_nreplicas, ldr_rank, term);
	if (rc != 0) {
		fprintf(stderr, "FAIL: waiting for leader after add replica: "
				DF_RC"\n", DP_RC(rc));
		return rc;
	}

	/* Lookup user key/value from unchanged leader */
	val_out = 0;
	rc = rdbt_test_rank(group, ldr_rank, NO_UPDATE, RDBT_MEMBER_NOOP,
			    key, val, &val_out, &h);
	if (rc) {
		fprintf(stderr, "FAIL: lookup RDB failed via RPC to leader "
			       "rank %u: "DF_RC", hint:(r=%u, t="DF_U64"\n",
			       ldr_rank, DP_RC(rc), h.sh_rank, h.sh_term);
		return rc;
	}
	if (val_out != val) {
		fprintf(stderr, "FAIL: lookup val="DF_U64" expect "DF_U64"\n",
				val_out, val);
		return -1;
	}
	printf("====== PASS: update/lookup: RDB via RPC to leader rank %u "
	       "(K=0x%"PRIx64", V="DF_U64")\n", ldr_rank, key, val_out);

	return 0;
}

static int
testm_disruptive_membership(crt_group_t *group, uint32_t nranks,
			    uint32_t nreplicas, uint64_t key, uint64_t val,
			    enum rdbt_membership_op memb_op)
{
	int			rc;
	d_rank_t		ldr_rank;
	d_rank_t		new_ldr_rank;
	uint64_t		term;
	uint64_t		val_out = 0;
	struct rsvc_hint	h;
	const int		UPDATE = 1;

	D_ASSERTF((memb_op != RDBT_MEMBER_NOOP),
		  "memb_op should be RESIGN or CAMPAIGN\n");
	printf("==== TEST: RDB fail update due to %s\n",
	       rdbt_membership_opname(memb_op));

	rc = rdbt_find_leader(group, nranks, nreplicas, &ldr_rank, &term);
	if (rc) {
		fprintf(stderr, "ERR: RDB find leader failed\n");
		return rc;
	}
	printf("INFO: RDB discovered leader rank %u, term="DF_U64"\n",
	       ldr_rank, term);

	/* Negative test, expect update to fail */
	rc = rdbt_test_rank(group, ldr_rank, UPDATE, memb_op, key, val,
			    &val_out, &h);
	if (rc == 0) {
		fprintf(stderr, "FAIL: update RDB should have failed in RPC to "
				"leader %u: "DF_RC", hint:(r=%u, t="DF_U64"\n",
				ldr_rank, DP_RC(rc), h.sh_rank, h.sh_term);
		return rc;
	}
	if (val_out == val) {
		fprintf(stderr, "FAIL: lookup val="DF_U64". Expect != val("
				DF_U64")\n", val_out, val);
		return -1;
	}

	if (memb_op == RDBT_MEMBER_RESIGN) {
		rc = wait_for_new_leader(group, nranks, nreplicas, ldr_rank,
					 (term+1), &new_ldr_rank);
	} else /* RDBT_MEMBER_CAMPAIGN */ {
		rc = wait_for_leader(group, nranks, nreplicas, ldr_rank,
				     (term+1));
		new_ldr_rank = ldr_rank;
	}
	if (rc != 0) {
		fprintf(stderr, "ERR: wait for leader failed\n");
		return rc;
	}

	printf("====== PASS: update/lookup fail with %s: RPC to initial "
	       "leader %u\n", rdbt_membership_opname(memb_op), ldr_rank);

	return 0;
}

static int
rdbt_test_multi(crt_group_t *group, uint32_t nranks, uint32_t nreplicas)
{
	int			rc;
	d_rank_t		new_rank;
	uint64_t		key = RDBT_KEY;
	uint64_t		val;

	/* Update user key,val and lookup / verify (same leader and members) */
	val = 32;
	rc = testm_update_lookup(group, nranks, nreplicas, key, val);
	if (rc != 0)
		return rc;

	/* For each replica, update k,v then verify it */
	val *= 2;
	rc = testm_update_lookup_all(group, nranks, nreplicas, key, val);
	if (rc != 0)
		return rc;

	new_rank = nreplicas;	/* replica ranks consecutive from 0 */
	val *= 2;
	rc = testm_add_follower(group, nranks, nreplicas, new_rank, key, val);
	if (rc != 0)
		return rc;
	nreplicas++;

	/* Update k,v, add new member (become leader), lookup/verify */
	new_rank = nreplicas;	/* make replica ranks consecutive from 0 */
	val *= 2;
	rc = testm_add_leader(group, nranks, nreplicas, new_rank, key, val);
	if (rc != 0)
		return rc;
	nreplicas++;

#if 0
	/* Call election in middle of update, fail transaction (new term) */
	val *= 2;
	rc = testm_disruptive_membership(group, nranks, nreplicas, key, val,
					 RDBT_MEMBER_CAMPAIGN);
	if (rc != 0)
		return rc;
#endif

	/* Resign in middle of update, fail transaction (new leader, term) */
	val *= 2;
	rc = testm_disruptive_membership(group, nranks, nreplicas, key, val,
					 RDBT_MEMBER_RESIGN);
	if (rc!= 0)
		return rc;
	return 0;
}

static int
test_multi_hdlr(int argc, char *argv[])
{
	int			 rc;

	rc = multi_tests_common_parse(argc, argv);
	if (rc != 0)
		return rc;

	return rdbt_test_multi(sys->sy_group, g_nranks, g_nreps);
}

/**** destroy command functions ****/

static int
rdbt_destroy_rank(crt_group_t *group, d_rank_t rank, struct rsvc_hint *hintp)
{
	crt_rpc_t		*rpc;
	struct rdbt_destroy_out	*out;
	int			 rc;

	rpc = create_rpc(RDBT_DESTROY, group, rank);
	rc = invoke_rpc(rpc);
	D_ASSERTF(rc == 0, "%d\n", rc);
	out = crt_reply_get(rpc);
	rc = out->tdo_rc;
	*hintp = out->tdo_hint;
	destroy_rpc(rpc);
	return rc;
}

static int
rdbt_destroy_multi(crt_group_t *group, uint32_t nranks, uint32_t nreplicas)
{
	int			rc;
	d_rank_t		junk_rank = nranks + 1000;
	d_rank_t		ldr_rank = junk_rank;
	uint64_t		term;
	struct rsvc_hint	h;

	rc = rdbt_find_leader(group, nranks, nreplicas, &ldr_rank, &term);
	if (rc) {
		fprintf(stderr, "ERR: RDB find leader failed\n");
		return rc;
	}
	printf("Discovered leader %u, term="DF_U64"\n", ldr_rank, term);

	printf("===== Destroy RDB KV stores on leader %u\n", ldr_rank);
	rc = rdbt_destroy_rank(group, ldr_rank, &h);
	if (rc) {
		fprintf(stderr, "ERR: destroy RDB KV stores failed RPC to rank "
				"%u: "DF_RC", hint:(r=%u, t="DF_U64"\n",
				ldr_rank, DP_RC(rc), h.sh_rank, h.sh_term);
		return rc;
	}
	printf("Destroyed RDB KV stores, via RPC to leader %u\n", ldr_rank);

	return rc;
}

static int
destroy_hdlr(int argc, char *argv[])
{
	int			 rc;

	rc = multi_tests_common_parse(argc, argv);
	if (rc != 0)
		return rc;

	return rdbt_destroy_multi(sys->sy_group, g_nranks, g_nreps);
}

/**** fini command functions ****/

static int
rdbt_fini_rank(crt_group_t *group, d_rank_t rank)
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
fini_hdlr(int argc, char *argv[])
{
	struct option		 options[] = {
		{"group",	required_argument,	NULL,	'g'},
		{"rank",	required_argument,	NULL,	'r'},
		{NULL,		0,			NULL,	0}
	};
	d_rank_t		 rank = default_rank;
	int			 rc;

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

	rc = dc_mgmt_sys_attach(group_id, &sys);
	if (rc != 0)
		return rc;

	return rdbt_fini_rank(sys->sy_group, rank);
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
	else if (strcmp(argv[1], "create") == 0)
		hdlr = create_hdlr;
	else if (strcmp(argv[1], "test") == 0)
		hdlr = test_hdlr;
	else if (strcmp(argv[1], "test-multi") == 0)
		hdlr = test_multi_hdlr;
	else if (strcmp(argv[1], "destroy") == 0)
		hdlr = destroy_hdlr;
	else if (strcmp(argv[1], "fini") == 0)
		hdlr = fini_hdlr;

	if (hdlr == NULL || hdlr == help_hdlr) {
		help_hdlr(argc, argv);
		return hdlr == NULL ? 2 : 0;
	}

	rc = daos_init();
	D_ASSERTF(rc == 0, "%d\n", rc);

	rc = crt_context_create(&context);
	D_ASSERTF(rc == 0, "%d\n", rc);
	rc = daos_rpc_register(&rdbt_proto_fmt, RDBT_PROTO_CLI_COUNT,
				NULL, DAOS_RDBT_MODULE);
	D_ASSERTF(rc == 0, "%d\n", rc);

	rc = hdlr(argc, argv);

	crt_context_destroy(context, 1 /* force */);
	daos_fini();
	if (rc < 0) {
		return 1;
	} else if (rc > 0) {
		help_hdlr(argc, argv);
		return 2;
	}

	return 0;
}
