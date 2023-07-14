/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * ds_pool: Pool Server Utilities
 */
#define D_LOGFAC	DD_FAC(pool)

#include <daos_srv/pool.h>
#include <daos_srv/bio.h>
#include <daos_srv/smd.h>

#include <daos/pool_map.h>
#include "rpc.h"
#include "srv_internal.h"

/* Build a rank list of targets with certain status. */
int
map_ranks_init(const struct pool_map *map, unsigned int status, d_rank_list_t *ranks)
{
	struct pool_domain     *domains = NULL;
	int			nnodes;
	int			n = 0;
	int			i;
	d_rank_t	       *rs;

	nnodes = pool_map_find_nodes((struct pool_map *)map,
				      PO_COMP_ID_ALL, &domains);
	if (nnodes == 0) {
		D_ERROR("no nodes in pool map\n");
		return -DER_IO;
	}

	for (i = 0; i < nnodes; i++) {
		if (status & domains[i].do_comp.co_status)
			n++;
	}

	if (n == 0) {
		ranks->rl_nr = 0;
		ranks->rl_ranks = NULL;
		return 0;
	}

	D_ALLOC_ARRAY(rs, n);
	if (rs == NULL)
		return -DER_NOMEM;

	ranks->rl_nr = n;
	ranks->rl_ranks = rs;

	n = 0;
	for (i = 0; i < nnodes; i++) {
		if (status & domains[i].do_comp.co_status) {
			D_ASSERT(n < ranks->rl_nr);
			ranks->rl_ranks[n] = domains[i].do_comp.co_rank;
			n++;
		}
	}
	D_ASSERTF(n == ranks->rl_nr, "%d != %u\n", n, ranks->rl_nr);

	return 0;
}

void
map_ranks_fini(d_rank_list_t *ranks)
{
	if (ranks->rl_ranks != NULL) {
		D_FREE(ranks->rl_ranks);
		ranks->rl_nr = 0;
	} else {
		D_ASSERT(ranks->rl_nr == 0);
	}
}

/**
 * Is \a rank considered up in \a map? Note that when \a rank does not exist in
 * \a map, false is returned.
 */
bool
ds_pool_map_rank_up(struct pool_map *map, d_rank_t rank)
{
	struct pool_domain     *node;
	int			rc;

	rc = pool_map_find_nodes(map, rank, &node);
	if (rc == 0)
		return false;
	D_ASSERTF(rc == 1, "%d\n", rc);

	return node->do_comp.co_status & POOL_GROUP_MAP_STATUS;
}

int
ds_pool_bcast_create(crt_context_t ctx, struct ds_pool *pool,
		     enum daos_module_id module, crt_opcode_t opcode,
		     uint32_t version, crt_rpc_t **rpc, crt_bulk_t bulk_hdl,
		     d_rank_list_t *excluded_list)
{
	d_rank_list_t	excluded;
	crt_opcode_t		opc;
	int			rc;

	ABT_rwlock_rdlock(pool->sp_lock);
	rc = map_ranks_init(pool->sp_map, PO_COMP_ST_DOWN | PO_COMP_ST_DOWNOUT, &excluded);
	ABT_rwlock_unlock(pool->sp_lock);
	if (rc != 0) {
		D_ERROR(DF_UUID": failed to create rank list: %d\n",
			DP_UUID(pool->sp_uuid), rc);
		return rc;
	}

	if (excluded_list != NULL && excluded_list->rl_nr > 0) {
		rc = daos_rank_list_merge(&excluded, excluded_list);
		if (rc != 0) {
			D_ERROR(DF_UUID": failed to merge rank list: %d\n",
				DP_UUID(pool->sp_uuid), rc);
			D_GOTO(out, rc);
		}
	}

	opc = DAOS_RPC_OPCODE(opcode, module, version);
	rc = crt_corpc_req_create(ctx, pool->sp_group,
			  excluded.rl_nr == 0 ? NULL : &excluded,
			  opc, bulk_hdl/* co_bulk_hdl */, NULL /* priv */,
			  0 /* flags */, crt_tree_topo(CRT_TREE_KNOMIAL, 32),
			  rpc);

out:
	map_ranks_fini(&excluded);
	return rc;
}

static int
bulk_cb(const struct crt_bulk_cb_info *cb_info)
{
	ABT_eventual *eventual = cb_info->bci_arg;

	ABT_eventual_set(*eventual, (void *)&cb_info->bci_rc,
			 sizeof(cb_info->bci_rc));
	return 0;
}

/*
 * Transfer the pool map buffer to "remote_bulk". If the remote bulk buffer is
 * too small, then return -DER_TRUNC and set "required_buf_size" to the local
 * pool map buffer size.
 */
int
ds_pool_transfer_map_buf(struct pool_buf *map_buf, uint32_t map_version,
			 crt_rpc_t *rpc, crt_bulk_t remote_bulk,
			 uint32_t *required_buf_size)
{
	size_t			map_buf_size;
	daos_size_t		remote_bulk_size;
	d_iov_t			map_iov;
	d_sg_list_t		map_sgl;
	crt_bulk_t		bulk;
	struct crt_bulk_desc	map_desc;
	crt_bulk_opid_t		map_opid;
	ABT_eventual		eventual;
	int		       *status;
	int			rc;

	map_buf_size = pool_buf_size(map_buf->pb_nr);

	/* Check if the client bulk buffer is large enough. */
	rc = crt_bulk_get_len(remote_bulk, &remote_bulk_size);
	if (rc != 0)
		goto out;
	if (remote_bulk_size < map_buf_size) {
		*required_buf_size = map_buf_size;
		rc = -DER_TRUNC;
		goto out;
	}

	d_iov_set(&map_iov, map_buf, map_buf_size);
	map_sgl.sg_nr = 1;
	map_sgl.sg_nr_out = 0;
	map_sgl.sg_iovs = &map_iov;

	rc = crt_bulk_create(rpc->cr_ctx, &map_sgl, CRT_BULK_RO, &bulk);
	if (rc != 0)
		goto out;

	/* Prepare "map_desc" for crt_bulk_transfer(). */
	map_desc.bd_rpc = rpc;
	map_desc.bd_bulk_op = CRT_BULK_PUT;
	map_desc.bd_remote_hdl = remote_bulk;
	map_desc.bd_remote_off = 0;
	map_desc.bd_local_hdl = bulk;
	map_desc.bd_local_off = 0;
	map_desc.bd_len = map_iov.iov_len;

	rc = ABT_eventual_create(sizeof(*status), &eventual);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto out_bulk;
	}

	rc = crt_bulk_transfer(&map_desc, bulk_cb, &eventual, &map_opid);
	if (rc != 0)
		goto out_eventual;

	rc = ABT_eventual_wait(eventual, (void **)&status);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto out_eventual;
	}

	rc = *status;

out_eventual:
	ABT_eventual_free(&eventual);
out_bulk:
	crt_bulk_free(bulk);
out:
	return rc;
}

/* Move a rank that is not exception from the end of src to the end of dst. */
static int
move_rank_except_for(d_rank_t exception, d_rank_list_t *src, d_rank_list_t *dst)
{
	int	i;
	int	rc;

	/* Choose the last rank that is not exception in src. */
	if (src->rl_nr == 0)
		return -DER_NONEXIST;
	i = src->rl_nr - 1;
	if (src->rl_ranks[i] == exception)
		i--;
	if (i < 0)
		return -DER_NONEXIST;
	D_ASSERT(src->rl_ranks[i] != exception);

	/* Add it to dst first, as this may return an error. */
	rc = d_rank_list_append(dst, src->rl_ranks[i]);
	if (rc != 0)
		return rc;

	/* Remove it from src. */
	if (i < src->rl_nr - 1)
		src->rl_ranks[i] = src->rl_ranks[src->rl_nr - 1];
	src->rl_nr--;

	return 0;
}

#if 0 /* unit tests for move_rank_except_for */
void
ds_pool_test_move_rank_except_for(void)
{
	d_rank_list_t	src;
	d_rank_list_t	dst;
	int		rc;

	{
		src.rl_ranks = NULL;
		src.rl_nr = 0;
		dst.rl_ranks = NULL;
		dst.rl_nr = 0;
		rc = move_rank_except_for(CRT_NO_RANK, &src, &dst);
		D_ASSERT(rc == -DER_NONEXIST);
	}

	{
		d_rank_t src_ranks[] = {0};

		src.rl_ranks = src_ranks;
		src.rl_nr = 1;
		dst.rl_ranks = NULL;
		dst.rl_nr = 0;
		rc = move_rank_except_for(0, &src, &dst);
		D_ASSERT(rc == -DER_NONEXIST);
	}

	{
		d_rank_t src_ranks[] = {2};

		src.rl_ranks = src_ranks;
		src.rl_nr = 1;
		dst.rl_ranks = NULL;
		dst.rl_nr = 0;
		rc = move_rank_except_for(CRT_NO_RANK, &src, &dst);
		D_ASSERT(rc == 0);
		D_ASSERT(src.rl_nr == 0);
		D_ASSERT(dst.rl_nr == 1);
		D_ASSERT(dst.rl_ranks[0] == 2);
		D_FREE(dst.rl_ranks);
	}

	{
		d_rank_t src_ranks[] = {2, 5};

		src.rl_ranks = src_ranks;
		src.rl_nr = 2;
		dst.rl_ranks = NULL;
		dst.rl_nr = 0;
		rc = move_rank_except_for(5, &src, &dst);
		D_ASSERT(rc == 0);
		D_ASSERT(src.rl_nr == 1);
		D_ASSERT(src.rl_ranks[0] == 5);
		D_ASSERT(dst.rl_nr == 1);
		D_ASSERT(dst.rl_ranks[0] == 2);
		D_FREE(dst.rl_ranks);
	}
}
#endif

/*
 * Compute the PS reconfiguration objective, that is, the number of replicas we
 * want to achieve.
 */
static int
compute_svc_reconf_objective(int svc_rf, d_rank_list_t *replicas)
{
	/*
	 * If the PS RF is unknown, we choose the greater one between the
	 * default PS RF and the one implied by the current number of replicas.
	 */
	if (svc_rf < 0) {
		svc_rf = ds_pool_svc_rf_from_nreplicas(replicas->rl_nr);
		if (svc_rf < DAOS_PROP_PO_SVC_REDUN_FAC_DEFAULT)
			svc_rf = DAOS_PROP_PO_SVC_REDUN_FAC_DEFAULT;
	}

	return ds_pool_svc_rf_to_nreplicas(svc_rf);
}

/*
 * Find n ranks with states in nodes but not in blacklist_0 or blacklist_1, and
 * append them to list. Return the number of ranks appended or an error.
 */
static int
find_ranks(int n, pool_comp_state_t states, struct pool_domain *nodes, int nnodes,
	   d_rank_list_t *blacklist_0, d_rank_list_t *blacklist_1, d_rank_list_t *list)
{
	int	n_appended = 0;
	int	i;
	int	rc;

	if (n == 0)
		return 0;

	for (i = 0; i < nnodes; i++) {
		if (!(nodes[i].do_comp.co_status & states))
			continue;
		if (d_rank_list_find(blacklist_0, nodes[i].do_comp.co_rank, NULL /* idx */))
			continue;
		if (d_rank_list_find(blacklist_1, nodes[i].do_comp.co_rank, NULL /* idx */))
			continue;
		rc = d_rank_list_append(list, nodes[i].do_comp.co_rank);
		if (rc != 0)
			return rc;
		n_appended++;
		if (n_appended == n)
			break;
	}

	return n_appended;
}

/**
 * Plan a round of pool service (PS) reconfigurations based on the PS
 * redundancy factor (RF), the pool map, and the current PS membership. The
 * caller is responsible for freeing \a to_add_out and \a to_remove_out with
 * d_rank_list_free.
 *
 * We desire replicas in UP or UPIN states.
 *
 * If removals are necessary, we only append desired replicas to \a
 * to_remove_out after all undesired replicas have already been appended to the
 * same list.
 *
 * [Compatibility] If \a svc_rf is negative, we try to maintain
 * DAOS_PROP_PO_SVC_REDUN_FAC_DEFAULT or the redundancy of the current
 * membership, whichever is higher---a compatibility policy for earlier pool
 * layout versions that do not store PS RFs.
 *
 * \param[in]	svc_rf		PS redundancy factor
 * \param[in]	map		pool map
 * \param[in]	replicas	current PS membership
 * \param[in]	self		self rank
 * \param[out]	to_add_out	PS replicas to add
 * \param[out]	to_remove_out	PS replicas to remove
 */
int
ds_pool_plan_svc_reconfs(int svc_rf, struct pool_map *map, d_rank_list_t *replicas, d_rank_t self,
			 d_rank_list_t **to_add_out, d_rank_list_t **to_remove_out)
{
	const pool_comp_state_t	 desired_states = PO_COMP_ST_UP | PO_COMP_ST_UPIN;
	struct pool_domain	*nodes = NULL;
	int			 nnodes;
	int			 objective;
	d_rank_list_t		*desired = NULL;
	d_rank_list_t		*undesired = NULL;
	d_rank_list_t		*to_add = NULL;
	d_rank_list_t		*to_remove = NULL;
	int			 i;
	int			 rc;

	nnodes = pool_map_find_nodes(map, PO_COMP_ID_ALL, &nodes);
	D_ASSERTF(nnodes > 0, "pool_map_find_nodes: %d\n", nnodes);

	objective = compute_svc_reconf_objective(svc_rf, replicas);

	desired = d_rank_list_alloc(0);
	undesired = d_rank_list_alloc(0);
	to_add = d_rank_list_alloc(0);
	to_remove = d_rank_list_alloc(0);
	if (desired == NULL || undesired == NULL || to_add == NULL || to_remove == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}

	/* Classify replicas into desired and undesired. */
	for (i = 0; i < replicas->rl_nr; i++) {
		d_rank_list_t  *list;
		int		j;

		for (j = 0; j < nnodes; j++)
			if (nodes[j].do_comp.co_rank == replicas->rl_ranks[i])
				break;
		if (j == nnodes) /* not found (hypothetical) */
			list = undesired;
		else if (nodes[j].do_comp.co_status & desired_states)
			list = desired;
		else
			list = undesired;
		rc = d_rank_list_append(list, replicas->rl_ranks[i]);
		if (rc != 0)
			goto out;
	}

	D_DEBUG(DB_MD, "desired=%u undesired=%u objective=%d\n", desired->rl_nr, undesired->rl_nr,
		objective);

	/*
	 * If we have too many replicas, remove undesired ones (if any) before
	 * desired ones.
	 */
	while (desired->rl_nr + undesired->rl_nr > objective) {
		rc = move_rank_except_for(self, undesired, to_remove);
		if (rc == -DER_NONEXIST)
			break;
		else if (rc != 0)
			goto out;
	}
	while (desired->rl_nr + undesired->rl_nr > objective) {
		rc = move_rank_except_for(self, desired, to_remove);
		D_ASSERT(rc != -DER_NONEXIST);
		if (rc != 0)
			goto out;
	}

	/* If necessary, add more replicas towards the objective. */
	if (desired->rl_nr + undesired->rl_nr < objective) {
		rc = find_ranks(objective - desired->rl_nr - undesired->rl_nr, desired_states,
				nodes, nnodes, desired, undesired, to_add);
		if (rc < 0)
			goto out;
		/* Copy the new ones to desired. */
		for (i = 0; i < to_add->rl_nr; i++) {
			rc = d_rank_list_append(desired, to_add->rl_ranks[i]);
			if (rc != 0)
				goto out;
		}
	}

	/*
	 * If there are undesired ones, try to replace as many of them as
	 * possible.
	 */
	if (undesired->rl_nr > 0) {
		int n;

		rc = find_ranks(undesired->rl_nr, desired_states, nodes, nnodes, desired, undesired,
				to_add);
		if (rc < 0)
			goto out;
		n = rc;
		/* Copy the n replacements to desired. */
		for (i = 0; i < n; i++) {
			rc = d_rank_list_append(desired, to_add->rl_ranks[i]);
			if (rc != 0)
				goto out;
		}
		/* Move n replicas from undesired to to_remove. */
		for (i = 0; i < n; i++) {
			rc = move_rank_except_for(self, undesired, to_remove);
			D_ASSERT(rc != -DER_NONEXIST);
			if (rc != 0)
				goto out;
		}
	}

	rc = 0;
out:
	if (rc == 0) {
		*to_add_out = to_add;
		*to_remove_out = to_remove;
	} else {
		d_rank_list_free(to_remove);
		d_rank_list_free(to_add);
	}
	d_rank_list_free(undesired);
	d_rank_list_free(desired);
	return rc;
}

#if 0 /* unit tests for ds_pool_plan_svc_reconfs */
static bool
testu_rank_sets_identical(d_rank_list_t *x, d_rank_t *y_ranks, int y_ranks_len)
{
	/* y_ranks may point to a static initializer; do not change it. */
	d_rank_list_sort(x);
	if (x->rl_nr != y_ranks_len)
		return false;
	if (memcmp(x->rl_ranks, y_ranks, x->rl_nr) != 0)
		return false;
	return true;
}

static bool
testu_rank_sets_belong(d_rank_list_t *x, d_rank_t *y_ranks, int y_ranks_len)
{
	int i;

	for (i = 0; i < x->rl_nr; i++) {
		d_rank_t	rank = x->rl_ranks[i];
		int		j;

		for (j = 0; j < y_ranks_len; j++)
			if (y_ranks[j] == rank)
				break;
		if (j == y_ranks_len)
			return false;
	}
	return true;
}

static struct pool_map *
testu_create_pool_map(d_rank_t *ranks, int n_ranks, d_rank_t *down_ranks, int n_down_ranks)
{
	d_rank_list_t		ranks_list = {
		.rl_ranks	= ranks,
		.rl_nr		= n_ranks
	};
	struct pool_buf	       *map_buf;
	struct pool_map	       *map;
	uint32_t	       *domains;
	int			n_domains = 3 + n_ranks;
	int			i;
	int			rc;

	/* Not using domains properly at the moment. See FD_TREE_TUNPLE_LEN. */
	D_ALLOC_ARRAY(domains, n_domains);
	D_ASSERT(domains != NULL);
	domains[0] = 1;
	domains[1] = 0;
	domains[2] = n_ranks;
	for (i = 0; i < n_ranks; i++)
		domains[3 + i] = i;

	rc = gen_pool_buf(NULL /* map */, &map_buf, 1 /* map_version */, n_domains,
			  n_ranks, n_ranks * 1 /* ntargets */, domains, &ranks_list,
			  1 /* dss_tgt_nr */);
	D_ASSERT(rc == 0);

	rc = pool_map_create(map_buf, 1, &map);
	D_ASSERT(rc == 0);

	for (i = 0; i < n_down_ranks; i++) {
		struct pool_domain *d;

		d = pool_map_find_node_by_rank(map, down_ranks[i]);
		D_ASSERT(d != NULL);
		d->do_comp.co_status = PO_COMP_ST_DOWN;
	}

	pool_buf_free(map_buf);
	D_FREE(domains);
	return map;
}

static void
testu_plan_svc_reconfs(int svc_rf, d_rank_t ranks[], int n_ranks, d_rank_t down_ranks[],
		       int n_down_ranks, d_rank_t replicas_ranks[], int n_replicas_ranks,
		       d_rank_t self, d_rank_list_t **to_add, d_rank_list_t **to_remove)
{
	struct pool_map	       *map;
	d_rank_list_t		replicas_list;
	int			rc;

	map = testu_create_pool_map(ranks, n_ranks, down_ranks, n_down_ranks);

	replicas_list.rl_ranks = replicas_ranks;
	replicas_list.rl_nr = n_replicas_ranks;

	rc = ds_pool_plan_svc_reconfs(svc_rf, map, &replicas_list, self, to_add, to_remove);
	D_ASSERT(rc == 0);

	pool_map_decref(map);
}

void
ds_pool_test_plan_svc_reconfs(void)
{
	d_rank_t		self = 0;
	d_rank_list_t	       *to_add;
	d_rank_list_t	       *to_remove;

#define call_testu_plan_svc_reconfs								\
	testu_plan_svc_reconfs(svc_rf, ranks, ARRAY_SIZE(ranks), down_ranks,			\
			       ARRAY_SIZE(down_ranks), replicas_ranks,				\
			       ARRAY_SIZE(replicas_ranks), self, &to_add, &to_remove);

#define call_d_rank_list_free									\
	d_rank_list_free(to_add);								\
	d_rank_list_free(to_remove);

	/* A happy PS does not want any changes. */
	{
		int		svc_rf = 2;
		d_rank_t	ranks[] = {0, 1, 2, 3, 4, 5, 6, 7};
		d_rank_t	down_ranks[] = {};
		d_rank_t	replicas_ranks[] = {0, 1, 2, 3, 4};

		call_testu_plan_svc_reconfs

		D_ASSERT(to_add->rl_nr == 0);
		D_ASSERT(to_remove->rl_nr == 0);

		call_d_rank_list_free
	}

	/* One lonely replica. */
	{
		int		svc_rf = 0;
		d_rank_t	ranks[] = {0};
		d_rank_t	down_ranks[] = {};
		d_rank_t	replicas_ranks[] = {0};

		call_testu_plan_svc_reconfs

		D_ASSERT(to_add->rl_nr == 0);
		D_ASSERT(to_remove->rl_nr == 0);

		call_d_rank_list_free
	}

	/* A PS tries to achieve the RF. */
	{
		int		svc_rf = 1;
		d_rank_t	ranks[] = {0, 1};
		d_rank_t	down_ranks[] = {};
		d_rank_t	replicas_ranks[] = {0};
		d_rank_t	expected_to_add[] = {1};

		call_testu_plan_svc_reconfs

		D_ASSERT(testu_rank_sets_identical(to_add, expected_to_add,
						   ARRAY_SIZE(expected_to_add)));
		D_ASSERT(to_remove->rl_nr == 0);

		call_d_rank_list_free
	}

	/* A PS resists the temptation of down ranks. */
	{
		int		svc_rf = 1;
		d_rank_t	ranks[] = {0, 1, 2};
		d_rank_t	down_ranks[] = {2};
		d_rank_t	replicas_ranks[] = {0};
		d_rank_t	expected_to_add[] = {1};

		call_testu_plan_svc_reconfs

		D_ASSERT(testu_rank_sets_identical(to_add, expected_to_add,
						   ARRAY_SIZE(expected_to_add)));
		D_ASSERT(to_remove->rl_nr == 0);

		call_d_rank_list_free
	}

	/* A PS successfully achieves the RF. */
	{
		int		svc_rf = 1;
		d_rank_t	ranks[] = {0, 1, 2};
		d_rank_t	down_ranks[] = {};
		d_rank_t	replicas_ranks[] = {0};
		d_rank_t	expected_to_add[] = {1, 2};

		call_testu_plan_svc_reconfs

		D_ASSERT(testu_rank_sets_identical(to_add, expected_to_add,
						   ARRAY_SIZE(expected_to_add)));
		D_ASSERT(to_remove->rl_nr == 0);

		call_d_rank_list_free
	}

	/* A PS holds its ground when there's no replacement. */
	{
		int		svc_rf = 1;
		d_rank_t	ranks[] = {0, 1, 2};
		d_rank_t	down_ranks[] = {2};
		d_rank_t	replicas_ranks[] = {0, 1, 2};

		call_testu_plan_svc_reconfs

		D_ASSERT(to_add->rl_nr == 0);
		D_ASSERT(to_remove->rl_nr == 0);

		call_d_rank_list_free
	}

	/* A PS replaces one down rank. */
	{
		int		svc_rf = 1;
		d_rank_t	ranks[] = {0, 1, 2, 3, 4};
		d_rank_t	down_ranks[] = {2};
		d_rank_t	replicas_ranks[] = {0, 1, 2};
		d_rank_t	expected_to_add_candidates[] = {3, 4};
		d_rank_t	expected_to_remove[] = {2};

		call_testu_plan_svc_reconfs

		D_ASSERT(to_add->rl_nr == 1);
		D_ASSERT(testu_rank_sets_belong(to_add, expected_to_add_candidates,
						ARRAY_SIZE(expected_to_add_candidates)));
		D_ASSERT(testu_rank_sets_identical(to_remove, expected_to_remove,
						   ARRAY_SIZE(expected_to_remove)));

		call_d_rank_list_free
	}

	/*
	 * When the administrator dreams of an unrealistic RF, the PS tries as
	 * hard as it can.
	 */
	{
		int		svc_rf = 2;
		d_rank_t	ranks[] = {0, 1, 2, 3};
		d_rank_t	down_ranks[] = {};
		d_rank_t	replicas_ranks[] = {0};
		d_rank_t	expected_to_add[] = {1, 2, 3};

		call_testu_plan_svc_reconfs

		D_ASSERT(testu_rank_sets_identical(to_add, expected_to_add,
						   ARRAY_SIZE(expected_to_add)));
		D_ASSERT(to_remove->rl_nr == 0);

		call_d_rank_list_free
	}

	/* A PS shrinks due to the RF. */
	{
		int		svc_rf = 0;
		d_rank_t	ranks[] = {0, 1, 2};
		d_rank_t	down_ranks[] = {};
		d_rank_t	replicas_ranks[] = {0, 1, 2};
		d_rank_t	expected_to_remove[] = {1, 2};

		call_testu_plan_svc_reconfs

		D_ASSERT(to_add->rl_nr == 0);
		D_ASSERT(testu_rank_sets_identical(to_remove, expected_to_remove,
						   ARRAY_SIZE(expected_to_remove)));

		call_d_rank_list_free
	}

	/* A PS keeps down ranks while growing. */
	{
		int		svc_rf = 2;
		d_rank_t	ranks[] = {0, 1, 2, 3, 4};
		d_rank_t	down_ranks[] = {2};
		d_rank_t	replicas_ranks[] = {0, 1, 2};
		d_rank_t	expected_to_add[] = {3, 4};

		call_testu_plan_svc_reconfs

		D_ASSERT(testu_rank_sets_identical(to_add, expected_to_add,
						   ARRAY_SIZE(expected_to_add)));
		D_ASSERT(to_remove->rl_nr == 0);

		call_d_rank_list_free
	}

	/* A PS removes undesired ranks first. */
	{
		int		svc_rf = 2;
		d_rank_t	ranks[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
		d_rank_t	down_ranks[] = {1, 2, 3};
		d_rank_t	replicas_ranks[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
		d_rank_t	expected_to_remove_candidates[] = {1, 2, 3, 4, 5, 6, 7, 8};
		d_rank_list_t	tmp;

		call_testu_plan_svc_reconfs

		D_ASSERT(to_add->rl_nr == 0);
		D_ASSERT(to_remove->rl_nr == 4);
		tmp.rl_ranks = to_remove->rl_ranks;
		tmp.rl_nr = 3;
		D_ASSERT(testu_rank_sets_identical(&tmp, down_ranks, ARRAY_SIZE(down_ranks)));
		D_ASSERT(testu_rank_sets_belong(to_remove, expected_to_remove_candidates,
						ARRAY_SIZE(expected_to_remove_candidates)));

		call_d_rank_list_free
	}

	/* A shrink that is too complicated to comment on. */
	{
		int		svc_rf = 3;
		d_rank_t	ranks[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
		d_rank_t	down_ranks[] = {1, 3, 5, 7};
		d_rank_t	replicas_ranks[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
		d_rank_t	expected_to_add[] = {9};
		d_rank_t	expected_to_remove_candidates[] = {1, 3, 5, 7};

		call_testu_plan_svc_reconfs

		D_ASSERT(testu_rank_sets_identical(to_add, expected_to_add,
						   ARRAY_SIZE(expected_to_add)));
		D_ASSERT(to_remove->rl_nr == 3);
		D_ASSERT(testu_rank_sets_belong(to_remove, expected_to_remove_candidates,
						ARRAY_SIZE(expected_to_remove_candidates)));

		call_d_rank_list_free
	}

#undef call_d_rank_list_free
#undef call_testu_plan_svc_reconfs
}
#endif

/** The caller are responsible for freeing the ranks */
int ds_pool_get_ranks(const uuid_t pool_uuid, int status,
		      d_rank_list_t *ranks)
{
	struct ds_pool	*pool;
	int		rc;

	rc = ds_pool_lookup(pool_uuid, &pool);
	if (rc != 0) {
		D_DEBUG(DB_MD, "Lookup "DF_UUID": %d\n", DP_UUID(pool_uuid), rc);
		return 0;
	}

	/* This may not be the pool leader node, so down targets
	 * may not be updated, then the following collective RPC
	 * might be timeout. XXX
	 */
	ABT_rwlock_rdlock(pool->sp_lock);
	if (pool->sp_map == NULL) {
		rc = 0;
		goto out_lock;
	}
	rc = map_ranks_init(pool->sp_map, status, ranks);
out_lock:
	ABT_rwlock_unlock(pool->sp_lock);
	if (rc != 0)
		D_ERROR(DF_UUID": failed to create rank list: %d\n",
			DP_UUID(pool->sp_uuid), rc);

	ds_pool_put(pool);
	return rc;
}

/* Get failed target index on the current node */
int ds_pool_get_tgt_idx_by_state(const uuid_t pool_uuid, unsigned int status, int **tgts,
				 unsigned int *tgts_cnt)
{
	struct ds_pool		*pool;
	struct pool_target	**pool_tgts = NULL;
	d_rank_t		myrank;
	int			rc;

	*tgts_cnt = 0;
	rc = ds_pool_lookup(pool_uuid, &pool);
	if (pool == NULL || pool->sp_map == NULL) {
		D_DEBUG(DB_MD, "pool look "DF_UUID": %d\n", DP_UUID(pool_uuid), rc);
		D_GOTO(output, rc = 0);
	}

	/* Check if we need excluded the failure targets, NB:
	 * since the ranks in the pool map are ranks of primary
	 * group, so we have to use primary group here.
	 */
	rc = crt_group_rank(NULL, &myrank);
	if (rc) {
		D_ERROR("Can not get rank "DF_RC"\n", DP_RC(rc));
		D_GOTO(output, rc);
	}

	rc = pool_map_find_by_rank_status(pool->sp_map, &pool_tgts, tgts_cnt, status,
					  myrank);
	if (*tgts_cnt != 0) {
		int i;

		D_ALLOC(*tgts, *tgts_cnt * sizeof(int));
		if (*tgts == NULL) {
			*tgts_cnt = 0;
			D_GOTO(output, rc = -DER_NOMEM);
		}
		for (i = 0; i < *tgts_cnt; i++)
			(*tgts)[i] = pool_tgts[i]->ta_comp.co_index;
	}

output:
	if (pool)
		ds_pool_put(pool);
	if (pool_tgts)
		D_FREE(pool_tgts);
	return rc;
}

int
ds_pool_get_failed_tgt_idx(const uuid_t pool_uuid, int **failed_tgts, unsigned int *failed_tgts_cnt)
{
	unsigned int status;

	status = PO_COMP_ST_DOWN | PO_COMP_ST_DOWNOUT;
	return ds_pool_get_tgt_idx_by_state(pool_uuid, status, failed_tgts, failed_tgts_cnt);
}

/* See nvme_reaction() for return values */
static int
check_pool_targets(uuid_t pool_id, int *tgt_ids, int tgt_cnt, bool reint,
		   d_rank_t *pl_rank)
{
	struct ds_pool_child	*pool_child;
	struct ds_pool		*pool;
	struct pool_target	*target = NULL;
	d_rank_t		 rank = dss_self_rank();
	int			 nr_downout, nr_down, nr_upin, nr_up;
	int			 i, nr, rc = 0;

	/* Get pool map to check the target status */
	pool_child = ds_pool_child_lookup(pool_id);
	if (pool_child == NULL) {
		D_ERROR(DF_UUID": Pool cache not found\n", DP_UUID(pool_id));
		/*
		 * The SMD pool info could be inconsistent with global pool
		 * info when pool creation/destroy partially succeed or fail.
		 * For example: If a pool destroy happened after a blobstore
		 * is torndown for faulty SSD, the blob and SMD info for the
		 * affected pool targets will be left behind.
		 *
		 * SSD faulty/reint reaction should tolerate such kind of
		 * inconsistency, otherwise, state transition for the SSD
		 * won't be able to moving forward.
		 */
		return 0;
	}
	pool = pool_child->spc_pool;
	D_ASSERT(pool != NULL);

	nr_downout = nr_down = nr_upin = nr_up = 0;
	ABT_rwlock_rdlock(pool->sp_lock);
	for (i = 0; i < tgt_cnt; i++) {
		nr = pool_map_find_target_by_rank_idx(pool->sp_map, rank,
						      tgt_ids[i], &target);
		if (nr != 1) {
			D_ERROR(DF_UUID": Failed to get rank:%u, idx:%d\n",
				DP_UUID(pool_id), rank, tgt_ids[i]);
			rc = -DER_NONEXIST;
			break;
		}

		D_ASSERT(target != NULL);
		switch (target->ta_comp.co_status) {
		case PO_COMP_ST_DOWNOUT:
			nr_downout++;
			break;
		case PO_COMP_ST_DOWN:
			nr_down++;
			break;
		case PO_COMP_ST_UPIN:
			nr_upin++;
			break;
		case PO_COMP_ST_UP:
			nr_up++;
			break;
		default:
			break;
		}
	}

	if (pool->sp_iv_ns != NULL) {
		*pl_rank = pool->sp_iv_ns->iv_master_rank;
	} else {
		*pl_rank = -1;
		D_ERROR(DF_UUID": Pool IV NS isn't initialized\n",
			DP_UUID(pool_id));
	}

	ABT_rwlock_unlock(pool->sp_lock);
	ds_pool_child_put(pool_child);

	if (rc)
		return rc;

	if (reint) {
		if (nr_upin + nr_up == tgt_cnt)
			return 0;
	} else {
		if (nr_downout + nr_down == tgt_cnt)
			return 0;
	}

	return (*pl_rank == -1) ? -DER_UNINIT : 1;
}

struct update_targets_arg {
	uuid_t		 uta_pool_id;
	d_rank_t	 uta_pl_rank;
	d_rank_t	*uta_ranks;
	int		*uta_tgts;
	int		 uta_nr;
	bool		 uta_reint;
};

static void
free_update_targets_arg(struct update_targets_arg *uta)
{
	D_ASSERT(uta != NULL);
	if (uta->uta_ranks != NULL)
		D_FREE(uta->uta_ranks);
	if (uta->uta_tgts != NULL)
		D_FREE(uta->uta_tgts);
	D_FREE(uta);
}

static struct update_targets_arg *
alloc_update_targets_arg(uuid_t pool_id, int *tgt_ids, int tgt_cnt, bool reint,
			 d_rank_t pl_rank)
{
	struct update_targets_arg	*uta;
	d_rank_t			 rank;
	int				 i;

	D_ASSERT(tgt_cnt > 0);
	D_ASSERT(tgt_ids != NULL);

	D_ALLOC_PTR(uta);
	if (uta == NULL)
		return NULL;

	D_ALLOC_ARRAY(uta->uta_ranks, tgt_cnt);
	if (uta->uta_ranks == NULL)
		goto free;

	D_ALLOC_ARRAY(uta->uta_tgts, tgt_cnt);
	if (uta->uta_tgts == NULL)
		goto free;

	uuid_copy(uta->uta_pool_id, pool_id);
	uta->uta_reint = reint;
	uta->uta_pl_rank = pl_rank;
	uta->uta_nr = tgt_cnt;
	rank = dss_self_rank();
	for (i = 0; i < uta->uta_nr; i++) {
		uta->uta_ranks[i] = rank;
		uta->uta_tgts[i] = tgt_ids[i];
	}

	return uta;
free:
	free_update_targets_arg(uta);
	return NULL;
}

static void
update_targets_ult(void *arg)
{
	struct update_targets_arg	*uta = arg;
	struct d_tgt_list		 tgt_list;
	d_rank_list_t			 svc;
	int				 rc;

	svc.rl_ranks = &uta->uta_pl_rank;
	svc.rl_nr = 1;

	tgt_list.tl_nr = uta->uta_nr;
	tgt_list.tl_ranks = uta->uta_ranks;
	tgt_list.tl_tgts = uta->uta_tgts;

	if (uta->uta_reint)
		rc = dsc_pool_tgt_reint(uta->uta_pool_id, NULL /* grp */,
					&svc, &tgt_list);
	else
		rc = dsc_pool_tgt_exclude(uta->uta_pool_id, NULL /* grp */,
					  &svc, &tgt_list);
	if (rc)
		D_ERROR(DF_UUID": %s targets failed. "DF_RC"\n",
			DP_UUID(uta->uta_pool_id),
			uta->uta_reint ? "Reint" : "Exclude",
			DP_RC(rc));

	free_update_targets_arg(uta);
}

/*
 * The NVMe faulty reaction is called from bio_nvme_poll() which is on
 * progress (hardware poll) ULT, and it will call into client stack to
 * exclude pool targets, blocking calls could be made in this code path,
 * so we have to perform the faulty reactions asynchronously in a new ULT
 * to avoid blocking the hardware poll.
 */
static int
update_pool_targets(uuid_t pool_id, int *tgt_ids, int tgt_cnt, bool reint,
		    d_rank_t pl_rank)
{
	struct update_targets_arg	*uta;
	int				 rc;

	uta = alloc_update_targets_arg(pool_id, tgt_ids, tgt_cnt, reint,
				       pl_rank);
	if (uta == NULL)
		return -DER_NOMEM;

	rc = dss_ult_create(update_targets_ult, uta, DSS_XS_SELF, 0, 0, NULL);
	if (rc) {
		D_ERROR(DF_UUID": Failed to start targets updating ULT. %d\n",
			DP_UUID(pool_id), rc);
		free_update_targets_arg(uta);
	}

	return rc;
}

static int
nvme_reaction(int *tgt_ids, int tgt_cnt, bool reint)
{
	struct smd_pool_info	*pool_info, *tmp;
	d_list_t		 pool_list;
	d_rank_t		 pl_rank;
	int			 pool_cnt, ret, rc;

	D_ASSERT(tgt_cnt > 0);
	D_ASSERT(tgt_ids != NULL);

	D_INIT_LIST_HEAD(&pool_list);
	rc = smd_pool_list(&pool_list, &pool_cnt);
	if (rc) {
		D_ERROR("Failed to list pools: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	d_list_for_each_entry_safe(pool_info, tmp, &pool_list, spi_link) {
		ret = check_pool_targets(pool_info->spi_id, tgt_ids, tgt_cnt,
					 reint, &pl_rank);
		switch (ret) {
		case 0:
			/*
			 * All affected targets are in expected state, it's safe
			 * to transit BIO BS state to now.
			 */
			D_DEBUG(DB_MGMT, DF_UUID": Targets are all in %s\n",
				DP_UUID(pool_info->spi_id),
				reint ? "UP/UPIN" : "DOWN/DOWNOUT");
			break;
		case 1:
			/*
			 * Some affected targets are not in expected state,
			 * need to send exclude/reint RPC.
			 */
			D_DEBUG(DB_MGMT, DF_UUID": Trigger targets %s.\n",
				DP_UUID(pool_info->spi_id),
				reint ? "reint" : "exclude");
			rc = update_pool_targets(pool_info->spi_id, tgt_ids,
						 tgt_cnt, reint, pl_rank);
			if (rc == 0)
				rc = 1;
			break;
		default:
			/* Errors */
			D_ERROR(DF_UUID": Check targets status failed: %d\n",
				DP_UUID(pool_info->spi_id), ret);
			if (rc >= 0)
				rc = ret;
			break;
		}

		d_list_del(&pool_info->spi_link);
		smd_pool_free_info(pool_info);
	}

	D_DEBUG(DB_MGMT, "Faulty reaction done. tgt_cnt:%d, rc:%d\n",
		tgt_cnt, rc);
	return rc;
}

static int
nvme_faulty_reaction(int *tgt_ids, int tgt_cnt)
{
	return nvme_reaction(tgt_ids, tgt_cnt, false);
}

static int
nvme_reint_reaction(int *tgt_ids, int tgt_cnt)
{
	return nvme_reaction(tgt_ids, tgt_cnt, true);
}

static int
nvme_bio_error(int media_err_type, int tgt_id)
{
	int rc;

	rc = ds_notify_bio_error(media_err_type, tgt_id);

	return rc;
}

struct bio_reaction_ops nvme_reaction_ops = {
	.faulty_reaction	= nvme_faulty_reaction,
	.reint_reaction		= nvme_reint_reaction,
	.ioerr_reaction		= nvme_bio_error,
};
