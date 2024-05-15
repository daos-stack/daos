/**
 * (C) Copyright 2017-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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

	D_ASSERT(g_nreps < g_nranks);

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
	uint32_t		rank;
	uint32_t		notleaders = 0;
	uint32_t		notreplicas = 0;
	const d_rank_t		NO_RANK = 0xFFFFFF;
	d_rank_t		ldr_rank = NO_RANK;
	uint64_t		term = 0;
	int			rc = 0;
	int			rc_svc;

	for (rank = 0; rank < nranks ; rank++) {
		bool			hint_isvalid;
		struct rsvc_hint	h;
		bool			resp_isvalid = true;
		bool			found_leader = (ldr_rank != NO_RANK);

		rc_svc = rdbt_ping_rank(group, rank, &h);
		hint_isvalid = (h.sh_flags & RSVC_HINT_VALID);

		if ((rc_svc == -DER_NOTLEADER) && !hint_isvalid) {
			resp_isvalid = (rank < nreplicas);
			if (!resp_isvalid)
				goto resp_valid_check;
			notleaders++;
		} else if (rc_svc == -DER_NOTLEADER) {
			resp_isvalid = (rank < nreplicas);
			if (!resp_isvalid)
				goto resp_valid_check;
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
			}
		} else if (rc_svc == -DER_NOTREPLICA) {
			resp_isvalid = (rank >= nreplicas);
			if (!resp_isvalid)
				goto resp_valid_check;
			notreplicas++;
		} else if (!hint_isvalid) {
			/* Leader reply without a hint */
			resp_isvalid = ((rc_svc == 0) && (rank < nreplicas));
			if (!resp_isvalid)
				goto resp_valid_check;
			if (found_leader) {
				if (rank != ldr_rank) {
					printf("WARN: rank=%u replied as leader"
					       " vs. found leader (rank=%u, "
					       "term="DF_U64")\n", rank,
					       ldr_rank, term);
					ldr_rank = rank;
				}
			} else {
				ldr_rank = rank;
				/* unknown term */
			}
		} else {
			/* Leader reply with a hint (does it happen)? */
			resp_isvalid = ((rc_svc == 0) && (rank < nreplicas));
			if (!resp_isvalid)
				goto resp_valid_check;
			if (found_leader) {
				/* reject if h.sh_term lower? */
				if (rank != ldr_rank) {
					printf("WARN: rank=%u replied as leader"
					       " term="DF_U64" vs. found leader"
					       " (rank=%u, term="DF_U64")\n",
					       rank, h.sh_term, ldr_rank, term);
					ldr_rank = rank;
					term = h.sh_term;
				}
			} else {
				ldr_rank = rank;
				term = h.sh_term;
			}
		}

resp_valid_check:
		if (!resp_isvalid) {
			printf("ERR: rank %u invalid reply: rc="DF_RC", "
			       "hint is %s valid (rank=%u, term="DF_U64")\n",
			       rank, DP_RC(rc_svc), hint_isvalid ? "" : "NOT",
			       h.sh_rank, h.sh_term);
			rc = -1;
			break;
		}
		rc = 0;
	}

	if ((rc == 0) && (ldr_rank != NO_RANK)) {
		printf("INFO: found leader rank=%u, term="DF_U64
		       ", non-leaders: %u, non-replicas: %u\n",
		       ldr_rank, term, notleaders, notreplicas);
		*leaderp = ldr_rank;
		*termp = term;
	} else if (ldr_rank == NO_RANK) {
		printf("ERR: no leader found!\n");
		return -1;
	}

	return rc;
}

/* ping all ranks and expect to find a particular leader rank */
static int
wait_for_this_leader(crt_group_t *grp, uint32_t nranks, uint32_t nreplicas,
		     d_rank_t expect_ldr, uint64_t expect_term_min,
		     uint64_t *out_termp)
{
	int			rc = 0;
	int			try;
	d_rank_t		found_ldr;
	uint64_t		found_term;
	const unsigned int	SLEEP_SEC = 2;
	const unsigned int	TRY_LIMIT = 6;

	for (try = 0; try < TRY_LIMIT; try++) {
		sleep(SLEEP_SEC);
		rc = rdbt_find_leader(grp, nranks, nreplicas, &found_ldr,
				      &found_term);
		if (rc == 0)
			break;
		printf("try %u/%u: no leader found yet, rc: "DF_RC"\n",
		       (try+1), TRY_LIMIT, DP_RC(rc));
	}
	if (rc == 0) {
		if (found_ldr != expect_ldr) {
			fprintf(stderr, "ERR: leader %u (expected %u)\n",
					found_ldr, expect_ldr);
			return -1;
		}

		if (found_term < expect_term_min) {
			fprintf(stderr, "ERR: term "DF_U64" < "DF_U64"\n",
					found_term, expect_term_min);
			return -1;
		}
	} else {
		fprintf(stderr, "FAIL: find leader after add replica\n");
		return rc;
	}

	printf("INFO: leader=%u, term="DF_U64"\n", found_ldr, found_term);
	if (out_termp)
		*out_termp = found_term;
	return 0;
}

/* ping all ranks and find the same or a different leader */
static int
wait_for_any_leader(crt_group_t *grp, uint32_t nranks, uint32_t nreplicas,
		    uint64_t expect_term_min, d_rank_t *out_ldrp,
		    uint64_t *out_termp)
{
	int			rc = 0;
	int			try;
	d_rank_t		found_ldr;
	uint64_t		found_term;
	const unsigned int	SLEEP_SEC = 2;
	const unsigned int	TRY_LIMIT = 6;

	for (try = 0; try < TRY_LIMIT; try++) {
		sleep(SLEEP_SEC);
		rc = rdbt_find_leader(grp, nranks, nreplicas, &found_ldr,
				      &found_term);
		if ((rc == 0) && (found_term >= expect_term_min))
			break;

		printf("try %u/%u: term >= "DF_U64" not found, rc: "DF_RC"\n",
		       (try+1), TRY_LIMIT, expect_term_min, DP_RC(rc));
	}
	if ((rc == 0) && (found_term < expect_term_min)) {
		fprintf(stderr, "ERR: term "DF_U64" < "DF_U64"\n",
				found_term, expect_term_min);
		return -1;
	} else if (rc != 0) {
		fprintf(stderr, "FAIL: find leader after add replica\n");
		return rc;
	}

	if (out_ldrp)
		*out_ldrp = found_ldr;
	if (out_termp)
		*out_termp = found_term;
	printf("INFO: leader=%u, term="DF_U64"\n", found_ldr, found_term);
	return 0;
}

static int
rdbt_start_election(crt_group_t *grp, d_rank_t rank)
{
	crt_rpc_t			*rpc;
	struct rdbt_start_election_out	*out;
	int				 rc;

	rpc = create_rpc(RDBT_START_ELECTION, grp, rank);
	rc = invoke_rpc(rpc);
	D_ASSERTF(rc == 0, "%d\n", rc);
	out = crt_reply_get(rpc);
	rc = out->rtse_rc;
	destroy_rpc(rpc);
	return rc;
}

static int
rdbt_add_replica_rank(crt_group_t *grp, d_rank_t ldr_rank, d_rank_t new_rank,
		      struct rsvc_hint *hintp)
{
	crt_rpc_t			*rpc;
	struct rdbt_replicas_add_in	*in;
	struct rdbt_replicas_add_out	*out;
	d_rank_list_t			*replicas_to_add;
	int				 rc;

	rpc = create_rpc(RDBT_REPLICAS_ADD, grp, ldr_rank);
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

static int
rdbt_remove_replica_rank(crt_group_t *group, d_rank_t ldr_rank,
			 d_rank_t rem_rank, struct rsvc_hint *hintp)
{
	crt_rpc_t			*rpc;
	struct rdbt_replicas_remove_in	*in;
	struct rdbt_replicas_remove_out	*out;
	d_rank_list_t			*replicas_to_remove;
	int				 rc;

	rpc = create_rpc(RDBT_REPLICAS_REMOVE, group, ldr_rank);
	in = crt_req_get(rpc);
	replicas_to_remove = d_rank_list_alloc(1);
	replicas_to_remove->rl_ranks[0] = rem_rank;
	in->rtmi_ranks = replicas_to_remove;
	rc = invoke_rpc(rpc);
	if (rem_rank != ldr_rank)
		D_ASSERTF(rc == 0, "%d\n", rc);
	out = crt_reply_get(rpc);
	rc = out->rtmo_rc;
	*hintp = out->rtmo_hint;
	if (out->rtmo_failed != NULL)
		fprintf(stderr, "ERR: removing replica %u (reply rank %u)\n",
				rem_rank, out->rtmo_failed->rl_ranks[0]);
	destroy_rpc(rpc);
	return rc;
}
/* Use this after tests that have added a replica.
 * Go from cur_nreplicas back to original number (cur_nreplicas-1).
 */
static int
restore_initial_replicas(crt_group_t *grp, uint32_t nranks,
			 uint32_t cur_nreplicas)
{
	int			rc;
	d_rank_t		cur_ldr_rank;
	d_rank_t		interim_ldr_rank;
	d_rank_t		final_ldr_rank;
	d_rank_t		remove_rank = cur_nreplicas - 1;
	uint64_t		term;
	uint64_t		new_term;
	struct rsvc_hint	h;

	printf("\n==== BEGIN RESTORE nreplicas (%u->%u)\n", cur_nreplicas,
	       (cur_nreplicas - 1));
	rc = rdbt_find_leader(grp, nranks, cur_nreplicas, &cur_ldr_rank, &term);
	if (rc) {
		fprintf(stderr, "ERR: RDB find leader failed\n");
		return rc;
	}

	/* If needed elect new leader (rank 0) before removing the rank */
	interim_ldr_rank = cur_ldr_rank;
	if (cur_ldr_rank == remove_rank) {
		rc = rdbt_start_election(grp, 0);
		if (rc != 0) {
			fprintf(stderr, "FAIL: start election from rank 0\n");
			return rc;
		}

		printf("INFO: rank 0 called for election. Sleep some\n");
		sleep(5);

		rc = wait_for_this_leader(grp, nranks, cur_nreplicas, 0,
					  (term+1), &new_term);
		if (rc != 0) {
			fprintf(stderr, "FAIL: wait for rank 0 to be leader: "
					DF_RC"\n", DP_RC(rc));
			return rc;
		}
		term = new_term;
		interim_ldr_rank = 0;
	}
	printf("INFO: rank %u is the interim leader, term "DF_U64"\n",
	       interim_ldr_rank, term);

	/* Remove the added replica rank */
	rc = rdbt_remove_replica_rank(grp, interim_ldr_rank, remove_rank, &h);
	if (rc != 0) {
		fprintf(stderr, "ERR: failed to remove rank %u: "DF_RC"\n",
				remove_rank, DP_RC(rc));
		return rc;
	}
	printf("INFO: removed rank %u\n", remove_rank);

	/* Should end up with same leader, term but test OK if it changes */
	cur_nreplicas--;
	rc = wait_for_any_leader(grp, nranks, cur_nreplicas, term,
				 &final_ldr_rank, &new_term);
	if (rc != 0) {
		fprintf(stderr, "FAIL: wait for a leader: "DF_RC"\n",
				DP_RC(rc));
		return rc;
	}

	printf("==== END RESTORE nreplicas (%u) leader %u term "DF_U64"\n",
	       cur_nreplicas, final_ldr_rank, new_term);

	return 0;
}

static int
dictate(crt_group_t *grp, d_rank_t rank, d_rank_t chosen_rank, d_rank_list_t *replicas)
{
	crt_rpc_t		*rpc;
	struct rdbt_dictate_in	*in;
	struct rdbt_dictate_out	*out;
	int			 rc;

	rpc = create_rpc(RDBT_DICTATE, grp, rank);
	in = crt_req_get(rpc);
	in->rti_ranks = replicas;
	in->rti_rank = chosen_rank;
	rc = invoke_rpc(rpc);
	D_ASSERTF(rc == 0, "%d\n", rc);
	out = crt_reply_get(rpc);
	rc = out->rto_rc;
	destroy_rpc(rpc);
	return rc;
}

/**** init command functions ****/

static int
rdbt_init(crt_group_t *grp, d_rank_t rank, uuid_t uuid, uint32_t nreplicas)
{
	crt_rpc_t	       *rpc;
	struct rdbt_init_in    *in;
	struct rdbt_init_out   *out;
	int			rc;

	rpc = create_rpc(RDBT_INIT, grp, rank);
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
rdbt_create_rank(crt_group_t *grp, d_rank_t rank, struct rsvc_hint *hintp)
{
	crt_rpc_t	       *rpc;
	struct rdbt_create_out *out;
	int			rc;

	rpc = create_rpc(RDBT_CREATE, grp, rank);
	rc = invoke_rpc(rpc);
	D_ASSERTF(rc == 0, "%d\n", rc);
	out = crt_reply_get(rpc);
	rc = out->tco_rc;
	*hintp = out->tco_hint;
	destroy_rpc(rpc);
	return rc;
}

static int
rdbt_create_multi(crt_group_t *grp, uint32_t nranks, uint32_t nreplicas)
{
	int			rc;
	d_rank_t		junk_rank = nranks + 1000;
	d_rank_t		ldr_rank = junk_rank;
	uint64_t		term;
	struct rsvc_hint	h;

	rc = rdbt_find_leader(grp, nranks, nreplicas, &ldr_rank, &term);
	if (rc) {
		fprintf(stderr, "ERR: RDB find leader failed\n");
		return rc;
	}
	printf("Discovered leader %u, term="DF_U64"\n", ldr_rank, term);

	printf("===== Create RDB KV stores on leader %u\n", ldr_rank);
	rc = rdbt_create_rank(grp, ldr_rank, &h);
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
rdbt_test_rank(crt_group_t *grp, d_rank_t rank, int update,
	       enum rdbt_membership_op memb_op, uint64_t user_key,
	       uint64_t user_val_in, uint64_t *user_val_outp,
	       struct rsvc_hint *hintp)
{
	crt_rpc_t	       *rpc;
	struct rdbt_test_in    *in;
	struct rdbt_test_out   *out;
	int			rc;

	rpc = create_rpc(RDBT_TEST, grp, rank);
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
testm_update_lookup(crt_group_t *grp, uint32_t nranks, uint32_t nreplicas,
		    uint64_t key, uint64_t val)
{
	int			rc;
	d_rank_t		ldr_rank;
	uint64_t		term;
	uint64_t		val_out = 0;
	struct rsvc_hint	h;
	const int		NO_UPDATE = 0;
	const int		UPDATE = 1;

	printf("\n==== TEST: RDB update then lookup from discovered leader\n");

	rc = rdbt_find_leader(grp, nranks, nreplicas, &ldr_rank, &term);
	if (rc) {
		fprintf(stderr, "ERR: RDB find leader failed\n");
		return rc;
	}
	printf("INFO: RDB discovered leader rank %u, term="DF_U64"\n",
	       ldr_rank, term);

	rc = rdbt_test_rank(grp, ldr_rank, UPDATE, RDBT_MEMBER_NOOP, key, val,
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
	rc = rdbt_test_rank(grp, ldr_rank, NO_UPDATE, RDBT_MEMBER_NOOP,
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
testm_update_lookup_all(crt_group_t *grp, uint32_t nranks, uint32_t nreplicas,
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

	printf("\n==== TEST: RDB update then lookup on all replicas\n");

	rc = rdbt_find_leader(grp, nranks, nreplicas, &ldr_rank, &term);
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
		rc = rdbt_test_rank(grp, ldr_rank, UPDATE, RDBT_MEMBER_NOOP,
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

#if 0
		/* Make test_rank become leader, call election, expect to win */
		rc = rdbt_start_election(grp, test_rank);
		if (rc != 0) {
			fprintf(stderr, "FAIL: start election from rank %u\n",
				test_rank);
			return rc;
		}

		printf("INFO: rank %u called for election. Sleep some\n",
		       test_rank);
		sleep(5);

		rc = wait_for_this_leader(grp, nranks, nreplicas, test_rank,
					  (term+1), NULL /* &term */);
		if (rc != 0) {
			fprintf(stderr, "FAIL: wait for leader %u term >= "
					DF_U64" after election: "DF_RC"\n",
					test_rank, (term+1), DP_RC(rc));
			return rc;
		}
		ldr_rank = test_rank;
		printf("INFO: replica rank %u is now leader\n", test_rank);
#endif

		/* Verify data on the test_rank */
		val_out = 0;
		rc = rdbt_test_rank(grp, test_rank, NO_UPDATE,
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

#if 0
static int
testm_add_leader(crt_group_t *grp, uint32_t nranks, uint32_t nreplicas,
		 d_rank_t new_rank, uint64_t key, uint64_t val)
{
	int			rc;
	d_rank_t		ldr_rank;
	d_rank_list_t		*stop_ranks;
	uint64_t		term;
	uint64_t		new_term;
	uint64_t		val_out = 0;
	struct rsvc_hint	h;
	uint32_t		new_nreplicas;
	const int		NO_UPDATE = 0;
	const int		UPDATE = 1;

	printf("\n==== TEST: RDB update, add leader replica, lookup from "
	       "new leader\n");

	stop_ranks = d_rank_list_alloc(nreplicas);
	if (stop_ranks == NULL)
		return -1;

	rc = rdbt_find_leader(grp, nranks, nreplicas, &ldr_rank, &term);
	if (rc) {
		fprintf(stderr, "ERR: RDB find leader failed\n");
		goto out_ranks;
	}
	printf("INFO: RDB discovered leader rank %u, term="DF_U64"\n",
	       ldr_rank, term);

	rc = rdbt_test_rank(grp, ldr_rank, UPDATE, RDBT_MEMBER_NOOP, key, val,
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
	rc = rdbt_add_replica_rank(grp, ldr_rank, new_rank, &h);
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
	rc = wait_for_this_leader(grp, nranks, new_nreplicas, ldr_rank, term,
				  &new_term);
	if (rc != 0) {
		fprintf(stderr, "FAIL: wait for leader %u term "DF_U64
				" after add replica: "DF_RC"\n", ldr_rank,
				term, DP_RC(rc));
		goto out_ranks;
	}
	term = new_term;
	printf("INFO: rank %u still leader term "DF_U64" after adding %u\n",
	       ldr_rank, term, new_rank);

	/* Make the added replica the leader, having it call for election */
	rc = rdbt_start_election(grp, new_rank);
	if (rc != 0) {
		fprintf(stderr, "FAIL: start election from new rank %u\n",
				new_rank);
		goto out_ranks;
	}
	printf("INFO: new rank %u called for election. Sleep some\n",
	       new_rank);
	sleep(5);

	rc = wait_for_this_leader(grp, nranks, new_nreplicas, new_rank,
				  (term+1), &new_term);
	if (rc != 0) {
		fprintf(stderr, "FAIL: wait for new leader %u term >= "DF_U64
				" after stop replicas: "DF_RC"\n", new_rank,
				(term+1), DP_RC(rc));
		goto out_ranks;
	}
	term = new_term;
	printf("INFO: new replica rank %u is leader term "DF_U64"\n",
	       new_rank, term);

	/* Lookup user key/value from new leader */
	val_out = 0;
	rc = rdbt_test_rank(grp, new_rank, NO_UPDATE, RDBT_MEMBER_NOOP,
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
#endif

static int
testm_add_follower(crt_group_t *grp, uint32_t nranks, uint32_t nreplicas,
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

	printf("\n==== TEST: RDB update, add follower replica, lookup from "
	       "original leader\n");

	rc = rdbt_find_leader(grp, nranks, nreplicas, &ldr_rank, &term);
	if (rc) {
		fprintf(stderr, "ERR: RDB find leader failed\n");
		return rc;
	}
	printf("INFO: RDB discovered leader rank %u, term="DF_U64"\n",
	       ldr_rank, term);

	rc = rdbt_test_rank(grp, ldr_rank, UPDATE, RDBT_MEMBER_NOOP, key, val,
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
	rc = rdbt_add_replica_rank(grp, ldr_rank, new_rank, &h);
	if (rc) {
		fprintf(stderr, "FAIL: add replica rank %u RPC to leader %u: "
				DF_RC", hint:(r=%u, t="DF_U64"\n", new_rank,
				ldr_rank, DP_RC(rc), h.sh_rank, h.sh_term);
		return rc;
	}

	new_nreplicas = nreplicas + 1;
	rc = wait_for_this_leader(grp, nranks, new_nreplicas, ldr_rank, term,
				  NULL /* &term */);
	if (rc != 0) {
		fprintf(stderr, "FAIL: waiting for leader after add replica: "
				DF_RC"\n", DP_RC(rc));
		return rc;
	}

	/* Lookup user key/value from unchanged leader */
	val_out = 0;
	rc = rdbt_test_rank(grp, ldr_rank, NO_UPDATE, RDBT_MEMBER_NOOP,
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
testm_disruptive_membership(crt_group_t *grp, uint32_t nranks,
			    uint32_t nreplicas, uint64_t key, uint64_t val,
			    enum rdbt_membership_op memb_op)
{
	int			rc;
	d_rank_t		orig_ldr_rank;
	d_rank_t		next_ldr_rank;
	uint64_t		term;
	uint64_t		orig_val = val;
	uint64_t		val_out = 0;
	struct rsvc_hint	h;
	const int		UPDATE = 1;
	const int		NO_UPDATE = 0;

	D_ASSERTF((memb_op != RDBT_MEMBER_NOOP),
		  "memb_op should be RESIGN or CAMPAIGN\n");
	printf("\n==== TEST: RDB fail update due to %s\n",
	       rdbt_membership_opname(memb_op));

	rc = rdbt_find_leader(grp, nranks, nreplicas, &orig_ldr_rank, &term);
	if (rc) {
		fprintf(stderr, "ERR: RDB find leader failed\n");
		return rc;
	}
	printf("INFO: RDB discovered leader rank %u, term="DF_U64"\n",
	       orig_ldr_rank, term);

	/* Negative test, expect update to fail */
	val++;
	rc = rdbt_test_rank(grp, orig_ldr_rank, UPDATE, memb_op, key, val,
			    &val_out, &h);
	if (rc == 0) {
		fprintf(stderr, "FAIL: update RDB should have failed in RPC to "
				"leader %u: "DF_RC", hint:(r=%u, t="DF_U64"\n",
				orig_ldr_rank, DP_RC(rc), h.sh_rank, h.sh_term);
		return rc;
	}

	if (memb_op == RDBT_MEMBER_RESIGN) {
		/* Another replica or original leader could win Raft election */
		rc = wait_for_any_leader(grp, nranks, nreplicas, (term+1),
					 &next_ldr_rank, NULL);
	} else /* RDBT_MEMBER_CAMPAIGN */ {
		rc = wait_for_this_leader(grp, nranks, nreplicas, orig_ldr_rank,
					  (term+1), NULL /* out_termp */);
		next_ldr_rank = orig_ldr_rank;
	}
	if (rc != 0) {
		fprintf(stderr, "ERR: wait for leader failed\n");
		return rc;
	}

	/* Make sure the update did not happen */
	val_out = 0;
	rc = rdbt_test_rank(grp, next_ldr_rank, NO_UPDATE, RDBT_MEMBER_NOOP,
			    key, val, &val_out, &h);
	if (rc != 0) {
		fprintf(stderr, "FAIL: lookup RPC to rank %u: "DF_RC"\n",
				next_ldr_rank, DP_RC(rc));
		return rc;
	}
	if ((val_out == val) || (val_out != orig_val)) {
		fprintf(stderr, "FAIL: lookup val="DF_U64". Expect "DF_U64"\n",
				val_out, orig_val);
		return -1;
	}

	printf("====== PASS: update/lookup fail with %s: RPC to initial "
	       "leader %u\n", rdbt_membership_opname(memb_op), orig_ldr_rank);

	return 0;
}

static int
testm_dictate_internal(crt_group_t *grp, uint32_t nranks, uint32_t nreplicas, uint64_t key,
		       uint64_t val, d_rank_t chosen_rank, d_rank_t exec_rank)
{
	d_rank_list_t	       *ranks;
	d_rank_t		ldr_rank;
	d_rank_t		rank;
	struct rsvc_hint	h;
	int			i;
	int			rc;

	printf("INFO: chosen_rank=%u exec_rank=%u\n", chosen_rank, exec_rank);

	ranks = d_rank_list_alloc(nreplicas);
	if (ranks == NULL)
		return -DER_NOMEM;

	rc = dictate(grp, exec_rank, chosen_rank, ranks);
	d_rank_list_free(ranks);
	if (rc) {
		fprintf(stderr, "FAIL: failed to dictate: "DF_RC"\n", DP_RC(rc));
		return rc;
	}
	ldr_rank = chosen_rank;

	printf("INFO: waiting for rank %u\n", ldr_rank);
	for (i = 0; i < 20; i++) {
		rc = rdbt_ping_rank(grp, ldr_rank, &h);
		if (rc != -DER_NOTLEADER)
			break;
		sleep(1);
	}
	if (rc != 0) {
		fprintf(stderr, "FAIL: no leader after dictating: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	printf("INFO: restoring original replicas\n");
	for (rank = 0; rank < nreplicas; rank++) {
		if (rank == ldr_rank)
			continue;
		rc = rdbt_add_replica_rank(grp, ldr_rank, rank, &h);
		if (rc) {
			fprintf(stderr, "FAIL: add back replica rank %u RPC to leader %u: "
				DF_RC", hint:(r=%u, t="DF_U64"\n", rank, ldr_rank,
				DP_RC(rc), h.sh_rank, h.sh_term);
			return rc;
		}
	}

	printf("INFO: sleeping 10 s for the restored replicas to catch up\n");
	sleep(10);

	printf("INFO: lookup all replicas\n");
	for (rank = 0; rank < nreplicas; rank++) {
		const int	NO_UPDATE = 0;
		uint64_t	val_out = 0;

		rc = rdbt_test_rank(grp, rank, NO_UPDATE, RDBT_MEMBER_NOOP,
				    key, val, &val_out, &h);
		if (rc) {
			fprintf(stderr, "FAIL: lookup RDB failed via RPC to leader "
				"rank %u: "DF_RC", hint:(r=%u, t="DF_U64"\n",
				ldr_rank, DP_RC(rc), h.sh_rank, h.sh_term);
			return rc;
		}
		if (val_out != val) {
			fprintf(stderr, "FAIL: lookup val="DF_U64" expect "DF_U64"\n", val_out,
				val);
			return -1;
		}
	}

	return 0;
}

static int
testm_dictate(crt_group_t *grp, uint32_t nranks, uint32_t nreplicas, uint64_t key, uint64_t val)
{
	int			rc;
	d_rank_t		ldr_rank;
	uint64_t		term;
	uint64_t		val_out = 0;
	const int		UPDATE = 1;
	struct rsvc_hint	h;

	printf("\n==== TEST: RDB update, destroy majority, dictate, and lookup\n");

	rc = rdbt_find_leader(grp, nranks, nreplicas, &ldr_rank, &term);
	if (rc) {
		fprintf(stderr, "ERR: RDB find leader failed\n");
		return rc;
	}
	printf("INFO: RDB discovered leader rank %u, term="DF_U64"\n",
	       ldr_rank, term);

	rc = rdbt_test_rank(grp, ldr_rank, UPDATE, RDBT_MEMBER_NOOP, key, val,
			    &val_out, &h);
	if (rc) {
		fprintf(stderr, "FAIL: update RDB failed via RPC to leader "
				"rank %u: "DF_RC", hint:(r=%u, t="DF_U64"\n",
				ldr_rank, DP_RC(rc), h.sh_rank, h.sh_term);
		return rc;
	}
	if (val_out != val) {
		fprintf(stderr, "FAIL: update val="DF_U64" expect "DF_U64"\n", val_out, val);
		return -1;
	}

	rc = testm_dictate_internal(grp, nranks, nreplicas, key, val,
				    ldr_rank /* chosen_rank */,
				    (ldr_rank + 1) % nreplicas /* exec_rank */);
	if (rc)
		return rc;

	rc = rdbt_find_leader(grp, nranks, nreplicas, &ldr_rank, &term);
	if (rc) {
		fprintf(stderr, "ERR: RDB find leader failed\n");
		return rc;
	}
	printf("INFO: RDB discovered leader rank %u, term="DF_U64"\n",
	       ldr_rank, term);

	rc = testm_dictate_internal(grp, nranks, nreplicas, key, val,
				    (ldr_rank + 1) % nreplicas /* chosen_rank */,
				    ldr_rank /* exec_rank */);
	if (rc)
		return rc;

	printf("====== PASS: dictate\n");

	return 0;
}

static int
rdbt_test_multi(crt_group_t *grp, uint32_t nranks, uint32_t nreplicas)
{
	int			rc;
	d_rank_t		new_rank;
	uint64_t		key = RDBT_KEY;
	uint64_t		val;

	/* Update user key,val and lookup / verify (same leader and members) */
	val = 32;
	rc = testm_update_lookup(grp, nranks, nreplicas, key, val);
	if (rc != 0)
		return rc;

	/* For each replica, update k,v then verify it */
	val *= 2;
	rc = testm_update_lookup_all(grp, nranks, nreplicas, key, val);
	if (rc != 0)
		return rc;

	new_rank = nreplicas;	/* replica ranks consecutive from 0 */
	val *= 2;
	rc = testm_add_follower(grp, nranks, nreplicas, new_rank, key, val);
	if (rc != 0)
		return rc;
	nreplicas++;

	rc = restore_initial_replicas(grp, nranks, nreplicas);
	if (rc != 0)
		return rc;
	nreplicas--;

	/*
	 * Disabled because it's hard to make the new replica a leader without
	 * some leadership transfer mechanism.
	 */
#if 0
	/* Update k,v, add new member (become leader), lookup/verify */
	new_rank = nreplicas;	/* make replica ranks consecutive from 0 */
	val *= 2;
	rc = testm_add_leader(grp, nranks, nreplicas, new_rank, key, val);
	if (rc != 0)
		return rc;
	nreplicas++;

	rc = restore_initial_replicas(grp, nranks, nreplicas);
	if (rc != 0)
		return rc;
	nreplicas--;
#endif

	/* TODO? disruptive membership test RDBT_MEMBER_CAMPAIGN */

	/* Resign in middle of update, fail transaction (new leader, term)
	 * val is from last successful update test.
	 */
	rc = testm_disruptive_membership(grp, nranks, nreplicas, key, val,
					 RDBT_MEMBER_RESIGN);
	if (rc != 0)
		return rc;

	val *= 2;
	rc = testm_dictate(grp, nranks, nreplicas, key, val);
	if (rc != 0)
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
rdbt_destroy_rank(crt_group_t *grp, d_rank_t rank, struct rsvc_hint *hintp)
{
	crt_rpc_t		*rpc;
	struct rdbt_destroy_out	*out;
	int			 rc;

	rpc = create_rpc(RDBT_DESTROY, grp, rank);
	rc = invoke_rpc(rpc);
	D_ASSERTF(rc == 0, "%d\n", rc);
	out = crt_reply_get(rpc);
	rc = out->tdo_rc;
	*hintp = out->tdo_hint;
	destroy_rpc(rpc);
	return rc;
}

static int
rdbt_destroy_multi(crt_group_t *grp, uint32_t nranks, uint32_t nreplicas)
{
	int			rc;
	d_rank_t		junk_rank = nranks + 1000;
	d_rank_t		ldr_rank = junk_rank;
	uint64_t		term;
	struct rsvc_hint	h;

	rc = rdbt_find_leader(grp, nranks, nreplicas, &ldr_rank, &term);
	if (rc) {
		fprintf(stderr, "ERR: RDB find leader failed\n");
		return rc;
	}
	printf("Discovered leader %u, term="DF_U64"\n", ldr_rank, term);

	printf("===== Destroy RDB KV stores on leader %u\n", ldr_rank);
	rc = rdbt_destroy_rank(grp, ldr_rank, &h);
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
rdbt_fini_rank(crt_group_t *grp, d_rank_t rank)
{
	crt_rpc_t	       *rpc;
	struct rdbt_fini_out   *out;
	int			rc;

	rpc = create_rpc(RDBT_FINI, grp, rank);
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
