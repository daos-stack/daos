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

	return node->do_comp.co_status & POOL_GROUP_MAP_STATES;
}

int
ds_pool_bcast_create(crt_context_t ctx, struct ds_pool *pool,
		     enum daos_module_id module, crt_opcode_t opcode,
		     uint32_t version, crt_rpc_t **rpc, crt_bulk_t bulk_hdl,
		     d_rank_list_t *excluded_list, void *priv)
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
			  opc, bulk_hdl/* co_bulk_hdl */, priv,
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

static void
rank_list_del_at(d_rank_list_t *list, int index)
{
	D_ASSERTF(0 <= index && index < list->rl_nr, "index=%d rl_nr=%u\n", index, list->rl_nr);
	memmove(&list->rl_ranks[index], &list->rl_ranks[index + 1],
		(list->rl_nr - index - 1) * sizeof(list->rl_ranks[0]));
	list->rl_nr--;
}

/*
 * Ephermal "reconfiguration domain" used by ds_pool_plan_svc_reconfs to track
 * aspects of domains that include at least one engine in POOL_SVC_MAP_STATES.
 *
 * The rcd_n_replicas field is the number of replicas in this domain.
 *
 * The rcd_n_engines field is the number of POOL_SVC_MAP_STATES engines.
 *
 * The number of vacant engines is therefore rcd_n_engines - rcd_n_replicas. We
 * always have 0 <= rcd_n_replicas <= rcd_n_engines and rcd_n_engines > 0.
 */
struct reconf_domain {
	struct pool_domain *rcd_domain;
	int                 rcd_n_replicas;
	int                 rcd_n_engines;
};

/*
 * Ephemeral "reconfiguration map" used by ds_pool_plan_svc_reconfs to track
 * aspects of the pool map and the replicas.
 *
 * The rcm_domains field points to a shuffle of all domains that include at
 * least one engine in POOL_SVC_MAP_STATES.
 *
 * The rcm_domains_n_engines_max field stores the maximum of the rcd_n_engines
 * field across rcm_domains.
 *
 * The rcm_replicas field points to a rank list of all replicas underneath
 * rcm_domains.
 */
struct reconf_map {
	struct reconf_domain *rcm_domains;
	int                   rcm_domains_len;
	int                   rcm_domains_n_engines_max;
	d_rank_list_t        *rcm_replicas;
};

/*
 * Given map and replicas, initialize rmap_out, and append all undesired
 * replicas to to_remove.
 */
static int
init_reconf_map(struct pool_map *map, d_rank_list_t *replicas, d_rank_t self,
		struct reconf_map *rmap_out, d_rank_list_t *to_remove)
{
	struct reconf_map   rmap = {0};
	struct pool_domain *domains;
	int                 domains_len;
	d_rank_list_t      *replicas_left = NULL;
	int                 i;
	int                 rc;

	domains_len = pool_map_find_domain(map, PO_COMP_TP_NODE, PO_COMP_ID_ALL, &domains);
	D_ASSERTF(domains_len > 0, "pool_map_find_domain: %d\n", domains_len);

	D_ALLOC_ARRAY(rmap.rcm_domains, domains_len);
	if (rmap.rcm_domains == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}

	rmap.rcm_replicas = d_rank_list_alloc(0);
	if (rmap.rcm_replicas == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}

	/*
	 * Use a duplicate so that we can delete the replica rank from it
	 * whenever we find a replica. This can speed up future iteration of
	 * the following loop, and leave us with a list of replicas that are
	 * outside of the pool map.
	 */
	rc = d_rank_list_dup(&replicas_left, replicas);
	if (rc != 0)
		goto out;

	/*
	 * Go through all PO_COMP_TP_NODE domains and their engines in the pool
	 * map, in order to populate rmap and to_remove.
	 */
	for (i = 0; i < domains_len; i++) {
		struct pool_domain *domain = &domains[i];
		int                 j;
		int                 n_engines  = 0;
		int                 n_replicas = 0;

		for (j = 0; j < domain->do_comp.co_nr; j++) {
			struct pool_domain *engine = &domain->do_children[j];
			bool                is_desired;
			int                 k;
			d_rank_list_t      *list;

			is_desired = engine->do_comp.co_status & POOL_SVC_MAP_STATES;
			if (is_desired)
				n_engines++;

			if (!d_rank_list_find(replicas_left, engine->do_comp.co_rank, &k))
				continue;

			rank_list_del_at(replicas_left, k);
			if (is_desired) {
				list = rmap.rcm_replicas;
				n_replicas++;
			} else {
				list = to_remove;
				if (engine->do_comp.co_rank == self) {
					D_ERROR("self undesired: state=%x\n",
						engine->do_comp.co_status);
					rc = -DER_INVAL;
					goto out;
				}
			}
			rc = d_rank_list_append(list, engine->do_comp.co_rank);
			if (rc != 0)
				goto out;
		}

		/* If a domain has no desired engine, we won't consider it. */
		if (n_engines == 0)
			continue;

		/* Add this domain to rmap. */
		rmap.rcm_domains[rmap.rcm_domains_len].rcd_domain     = domain;
		rmap.rcm_domains[rmap.rcm_domains_len].rcd_n_engines  = n_engines;
		rmap.rcm_domains[rmap.rcm_domains_len].rcd_n_replicas = n_replicas;
		rmap.rcm_domains_len++;
		if (n_engines > rmap.rcm_domains_n_engines_max)
			rmap.rcm_domains_n_engines_max = n_engines;
	}

	/*
	 * Hypothetically, if there are replicas that are not found in the pool
	 * map, put them in to_remove.
	 */
	for (i = 0; i < replicas_left->rl_nr; i++) {
		rc = d_rank_list_append(to_remove, replicas_left->rl_ranks[i]);
		if (rc != 0)
			goto out;
	}

	/* Shuffle rmap.rcm_domains for randomness in replica placement. */
	for (i = 0; i < rmap.rcm_domains_len; i++) {
		int j = i + d_rand() % (rmap.rcm_domains_len - i);

		D_ASSERTF(i <= j && j < rmap.rcm_domains_len, "i=%d j=%d len=%d\n", i, j,
			  rmap.rcm_domains_len);
		if (j != i) {
			struct reconf_domain t = rmap.rcm_domains[i];

			rmap.rcm_domains[i] = rmap.rcm_domains[j];
			rmap.rcm_domains[j] = t;
		}
	}

	rc = 0;
out:
	d_rank_list_free(replicas_left);
	if (rc == 0) {
		*rmap_out = rmap;
	} else {
		d_rank_list_free(rmap.rcm_replicas);
		D_FREE(rmap.rcm_domains);
	}
	return rc;
}

static void
fini_reconf_map(struct reconf_map *rmap)
{
	d_rank_list_free(rmap->rcm_replicas);
	D_FREE(rmap->rcm_domains);
}

/* Find in rdomain a random engine that is not in replicas. */
static d_rank_t
find_vacancy_in_domain(struct reconf_domain *rdomain, d_rank_list_t *replicas)
{
	int n = rdomain->rcd_n_engines - rdomain->rcd_n_replicas;
	int i;

	D_ASSERTF(n >= 0, "invalid n: %d: rcd_n_engines=%d rcd_n_replicas=%d\n", n,
		  rdomain->rcd_n_engines, rdomain->rcd_n_replicas);
	if (n == 0)
		return CRT_NO_RANK;

	for (i = 0; i < rdomain->rcd_domain->do_comp.co_nr; i++) {
		struct pool_domain *engine = &rdomain->rcd_domain->do_children[i];

		if ((engine->do_comp.co_status & POOL_SVC_MAP_STATES) &&
		    !d_rank_list_find(replicas, engine->do_comp.co_rank, NULL /* idx */)) {
			/* Pick this vacant engine with a probability of 1/n. */
			if (d_rand() % n == 0)
				return engine->do_comp.co_rank;
			n--;
		}
	}

	return CRT_NO_RANK;
}

/* Find in rdomain a random engine that is in replicas but not self. */
static d_rank_t
find_replica_in_domain(struct reconf_domain *rdomain, d_rank_list_t *replicas, d_rank_t self)
{
	int n = rdomain->rcd_n_replicas;
	int i;

	/* If ourself is in this domain, decrement n. */
	for (i = 0; i < rdomain->rcd_domain->do_comp.co_nr; i++) {
		struct pool_domain *engine = &rdomain->rcd_domain->do_children[i];

		if (engine->do_comp.co_rank == self) {
			n--;
			break;
		}
	}

	D_ASSERTF(n >= 0, "invalid n: %d: rcd_n_engines=%d rcd_n_replicas=%d rl_nr=%d self=%u\n", n,
		  rdomain->rcd_n_engines, rdomain->rcd_n_replicas, replicas->rl_nr, self);
	if (n == 0)
		return CRT_NO_RANK;

	for (i = 0; i < rdomain->rcd_domain->do_comp.co_nr; i++) {
		struct pool_domain *engine = &rdomain->rcd_domain->do_children[i];

		if ((engine->do_comp.co_status & POOL_SVC_MAP_STATES) &&
		    engine->do_comp.co_rank != self &&
		    d_rank_list_find(replicas, engine->do_comp.co_rank, NULL /* idx */)) {
			if (d_rand() % n == 0)
				return engine->do_comp.co_rank;
			n--;
		}
	}

	return CRT_NO_RANK;
}

/*
 * Find engines for at most n replicas, and append their ranks to to_add.
 * Return the number of ranks appended or an error. If not zero, the
 * domains_n_engines_max parameter overrides
 * rmap->rcm_domains_n_engines_max.
 */
static int
add_replicas(int n, struct reconf_map *rmap, int domains_n_engines_max, d_rank_list_t *to_add)
{
	int n_appended = 0;
	int i;

	D_ASSERTF(n > 0, "invalid n: %d\n", n);
	D_ASSERTF(0 <= domains_n_engines_max &&
		      domains_n_engines_max <= rmap->rcm_domains_n_engines_max,
		  "invalid domains_n_engines_max: %d: rcm_domains_n_engines_max=%d\n",
		  domains_n_engines_max, rmap->rcm_domains_n_engines_max);

	if (domains_n_engines_max == 0)
		domains_n_engines_max = rmap->rcm_domains_n_engines_max;

	/* We start from domains with least replicas. */
	for (i = 0; i < domains_n_engines_max; i++) {
		int j;

		/* For each domain with i replicas and more than i engines... */
		for (j = 0; j < rmap->rcm_domains_len; j++) {
			struct reconf_domain *rdomain = &rmap->rcm_domains[j];
			d_rank_t              rank;
			int                   rc;

			if (rdomain->rcd_n_replicas != i ||
			    rdomain->rcd_n_replicas == rdomain->rcd_n_engines)
				continue;

			/* This domain has at least one vacant engine. */
			rank = find_vacancy_in_domain(rdomain, rmap->rcm_replicas);
			D_ASSERT(rank != CRT_NO_RANK);

			rc = d_rank_list_append(to_add, rank);
			if (rc != 0)
				return rc;
			rc = d_rank_list_append(rmap->rcm_replicas, rank);
			if (rc != 0)
				return rc;

			rdomain->rcd_n_replicas++;
			n_appended++;
			if (n_appended == n)
				return n;
		}
	}

	return n_appended;
}

static int
remove_replica_in_domain(struct reconf_domain *rdomain, d_rank_list_t *replicas, d_rank_t self,
			 d_rank_list_t *to_remove)
{
	d_rank_t rank;
	int      k;
	bool     found;
	int      rc;

	rank = find_replica_in_domain(rdomain, replicas, self);
	if (rank == CRT_NO_RANK)
		return -DER_NONEXIST;

	rc = d_rank_list_append(to_remove, rank);
	if (rc != 0)
		return rc;

	found = d_rank_list_find(replicas, rank, &k);
	D_ASSERT(found);
	rank_list_del_at(replicas, k);

	rdomain->rcd_n_replicas--;
	return 0;
}

/*
 * Find at most n replicas and append their ranks to to_remove. Return the
 * number of ranks appended or an error.
 */
static int
remove_replicas(int n, struct reconf_map *rmap, d_rank_t self, d_rank_list_t *to_remove)
{
	int n_appended = 0;
	int i;

	D_ASSERTF(n > 0, "invalid n: %d\n", n);

	/*
	 * We start from domains with most replicas, so that the subsequent
	 * balance_replicas call will produce less reconfigurations. The
	 * algorithm here could perhaps be improved by maintaining an
	 * rcd_n_replicas-sorted array of domains that each has at least one
	 * replica.
	 */
	for (i = rmap->rcm_domains_n_engines_max; i > 0; i--) {
		int j;

		/* For each domain with i replicas... */
		for (j = 0; j < rmap->rcm_domains_len; j++) {
			struct reconf_domain *rdomain = &rmap->rcm_domains[j];
			int                   rc;

			if (rdomain->rcd_n_replicas != i)
				continue;

			rc = remove_replica_in_domain(rdomain, rmap->rcm_replicas, self, to_remove);
			if (rc == -DER_NONEXIST)
				continue;
			else if (rc != 0)
				return rc;

			n_appended++;
			if (n_appended == n)
				return n;
		}
	}

	return n_appended;
}

/*
 * Move replicas so that if there is a domain with i replicas, then all domains
 * with less than i - 1 replicas are full.
 */
static int
balance_replicas(struct reconf_map *rmap, d_rank_t self, d_rank_list_t *to_add,
		 d_rank_list_t *to_remove)
{
	int i;

	/*
	 * We start from domains with most replicas. Since moving a replica
	 * from a domain with only one replica does not make sense (see
	 * below), we stop when i == 1. The algorithm here could perhaps be
	 * improved in a similar manner as remove_replicas.
	 */
	for (i = rmap->rcm_domains_n_engines_max; i > 1; i--) {
		int j;

		/* For each domain with i replicas... */
		for (j = 0; j < rmap->rcm_domains_len; j++) {
			struct reconf_domain *rdomain = &rmap->rcm_domains[j];
			int                   rc;

			if (rdomain->rcd_n_replicas != i)
				continue;

			/*
			 * Try to add a replica with add_replicas, such that
			 * the domain containing the new replica will have at
			 * most i - 1 replicas. Since i > 1, we know that
			 * i - 1 > 0.
			 */
			rc = add_replicas(1, rmap, i - 1 /* domains_n_engines_max */, to_add);
			if (rc < 0)
				return rc;
			else if (rc == 0)
				continue;

			/*
			 * Remove a replica from the current domain. Since
			 * i > 1, there must be at least one replica we can
			 * remove.
			 */
			rc = remove_replica_in_domain(rdomain, rmap->rcm_replicas, self, to_remove);
			D_ASSERT(rc != -DER_NONEXIST);
			if (rc != 0)
				return rc;
		}
	}

	return 0;
}

/**
 * Plan a round of pool service (PS) reconfigurations based on the PS
 * redundancy factor (RF), the pool map, and the current PS membership. The
 * caller is responsible for freeing \a to_add_out and \a to_remove_out with
 * d_rank_list_free.
 *
 * We desire replicas in POOL_SVC_MAP_STATES. The \a self replica must be in a
 * desired state in \a map, or this function will return -DER_INVAL. All
 * undesired replicas, if any, will be appended to \a to_remove, so that no
 * replica is outside the pool group.
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
 * \param[in]	replicas	current PS membership (may be empty if \a svc_rf >= 0)
 * \param[in]	self		self replica rank (may be CRT_NO_RANK if we're not a replica)
 * \param[in]	filter_only	only filter out replicas not in POOL_SVC_MAP_STATES
 * \param[out]	to_add_out	PS replicas to add
 * \param[out]	to_remove_out	PS replicas to remove
 */
int
ds_pool_plan_svc_reconfs(int svc_rf, struct pool_map *map, d_rank_list_t *replicas, d_rank_t self,
			 bool filter_only, d_rank_list_t **to_add_out,
			 d_rank_list_t **to_remove_out)
{
	int			 objective;
	d_rank_list_t		*to_add = NULL;
	d_rank_list_t		*to_remove = NULL;
	struct reconf_map	 rmap;
	int			 n;
	int			 rc;

	objective = compute_svc_reconf_objective(svc_rf, replicas);

	to_add = d_rank_list_alloc(0);
	to_remove = d_rank_list_alloc(0);
	if (to_add == NULL || to_remove == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}

	rc = init_reconf_map(map, replicas, self, &rmap, to_remove);
	if (rc != 0)
		goto out;

	D_DEBUG(DB_MD, "domains=%d n_engines_max=%d replicas=%u remove=%u filter_only=%d\n",
		rmap.rcm_domains_len, rmap.rcm_domains_n_engines_max, rmap.rcm_replicas->rl_nr,
		to_remove->rl_nr, filter_only);

	if (filter_only) {
		rc = 0;
		goto out_rmap;
	}

	n = rmap.rcm_replicas->rl_nr - objective;
	if (n < 0)
		rc = add_replicas(-n, &rmap, 0 /* domains_n_engines_max */, to_add);
	else if (n > 0)
		rc = remove_replicas(n, &rmap, self, to_remove);
	if (rc < 0)
		goto out_rmap;

	rc = balance_replicas(&rmap, self, to_add, to_remove);

out_rmap:
	fini_reconf_map(&rmap);
out:
	if (rc == 0) {
		*to_add_out = to_add;
		*to_remove_out = to_remove;
	} else {
		d_rank_list_free(to_remove);
		d_rank_list_free(to_add);
	}
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

/* Add all ranks in y to x. Just append; no dup check. */
static d_rank_list_t *
testu_rank_sets_add(d_rank_t *x_ranks, int x_ranks_len, d_rank_list_t *y)
{
	d_rank_list_t *z;
	int i;
	int rc;

	z = d_rank_list_alloc(0);
	D_ASSERT(z != NULL);

	for (i = 0; i < x_ranks_len; i++) {
		rc = d_rank_list_append(z, x_ranks[i]);
		D_ASSERT(rc == 0);
	}

	for (i = 0; i < y->rl_nr; i++) {
		rc = d_rank_list_append(z, y->rl_ranks[i]);
		D_ASSERT(rc == 0);
	}

	return z;
}

/* Subtract all ranks in y from x. */
static d_rank_list_t *
testu_rank_sets_subtract(d_rank_t *x_ranks, int x_ranks_len, d_rank_list_t *y)
{
	d_rank_list_t *z;
	int i;
	int rc;

	z = d_rank_list_alloc(0);
	D_ASSERT(z != NULL);

	for (i = 0; i < x_ranks_len; i++) {
		rc = d_rank_list_append(z, x_ranks[i]);
		D_ASSERT(rc == 0);
	}

	for (i = 0; i < y->rl_nr; i++) {
		int j;

		if (d_rank_list_find(z, y->rl_ranks[i], &j))
			rank_list_del_at(z, j);
	}

	return z;
}

static int
testu_cmp_ints(const void *a, const void *b)
{
	return *(int *)a - *(int *)b;
}

static bool
testu_rank_set_has_dist(d_rank_list_t *ranks, int *dist, int dist_len)
{
	int *counts;
	int i;

	D_ALLOC_ARRAY(counts, dist_len);
	D_ASSERT(counts != NULL);
	for (i = 0; i < ranks->rl_nr; i++) {
		int domain = ranks->rl_ranks[i] / 10;

		D_ASSERT(0 <= domain && domain < dist_len);
		counts[domain]++;
	}

	qsort(dist, dist_len, sizeof(int), testu_cmp_ints);
	qsort(counts, dist_len, sizeof(int), testu_cmp_ints);
	return memcmp(counts, dist, dist_len * sizeof(int)) == 0;
}

static void
testu_create_domain_buf(d_rank_t *ranks, int n_ranks, uint32_t **domain_buf_out,
			int *domain_buf_len_out)
{
	int       n_domains          = ranks[n_ranks - 1] / 10 + 1;
	int       n_ranks_per_domain = ranks[n_ranks - 1] % 10 + 1;
	uint32_t *domain_buf;
	int       domain_buf_len;
	uint32_t *p;
	int       i;

	D_ASSERT(n_domains * n_ranks_per_domain == n_ranks);

	domain_buf_len = 3 /* root */ + 3 * n_domains + n_ranks;
	D_ALLOC_ARRAY(domain_buf, domain_buf_len);
	D_ASSERT(domain_buf != NULL);

	/* The root. */
	p = domain_buf;
	D_ASSERT(domain_buf <= p && p + 3 <= domain_buf + domain_buf_len);
	p[0] = 2;
	p[1] = 0;
	p[2] = n_domains;
	p += 3;

	/* The domains. */
	for (i = 0; i < n_domains; i++) {
		D_ASSERT(domain_buf <= p && p + 3 <= domain_buf + domain_buf_len);
		p[0] = 1;
		p[1] = i;
		p[2] = n_ranks_per_domain;
		p += 3;
	}

	/* The ranks. */
	for (i = 0; i < n_ranks; i++) {
		D_ASSERT(domain_buf <= p && p + 1 <= domain_buf + domain_buf_len);
		p[0] = ranks[i];
		p++;
	}

	for (i = 0; i < domain_buf_len; i++)
		D_INFO("domain_buf[%d]: %u\n", i, domain_buf[i]);

	*domain_buf_out     = domain_buf;
	*domain_buf_len_out = domain_buf_len;
}

static struct pool_map *
testu_create_pool_map(d_rank_t *ranks, int n_ranks, d_rank_t *down_ranks, int n_down_ranks)
{
	struct pool_buf	       *map_buf;
	struct pool_map	       *map;
	uint32_t	       *domain_buf;
	int			domain_buf_len;
	int			i;
	int			rc;

	testu_create_domain_buf(ranks, n_ranks, &domain_buf, &domain_buf_len);

	rc = gen_pool_buf(NULL /* map */, &map_buf, 1 /* map_version */, domain_buf_len,
			  n_ranks, n_ranks * 1 /* ntargets */, domain_buf, 1 /* dss_tgt_nr */);
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
	D_FREE(domain_buf);
	return map;
}

static void
testu_plan_svc_reconfs(int svc_rf, d_rank_t ranks[], int n_ranks, d_rank_t down_ranks[],
		       int n_down_ranks, d_rank_t replicas_ranks[], int n_replicas_ranks,
		       d_rank_t self, int expected_rc, d_rank_list_t **to_add,
		       d_rank_list_t **to_remove)
{
	struct pool_map	       *map;
	d_rank_list_t		replicas_list;
	int			rc;

	map = testu_create_pool_map(ranks, n_ranks, down_ranks, n_down_ranks);

	replicas_list.rl_ranks = replicas_ranks;
	replicas_list.rl_nr = n_replicas_ranks;

	rc = ds_pool_plan_svc_reconfs(svc_rf, map, &replicas_list, self, false, to_add, to_remove);
	D_ASSERTF(rc == expected_rc, "rc=%d expected_rc=%d\n", rc, expected_rc);

	pool_map_decref(map);
}

void
ds_pool_test_plan_svc_reconfs(void)
{
	d_rank_list_t	       *to_add;
	d_rank_list_t	       *to_remove;

#define call_testu_plan_svc_reconfs(expected_rc)						\
	testu_plan_svc_reconfs(svc_rf, ranks, ARRAY_SIZE(ranks), down_ranks,			\
			       ARRAY_SIZE(down_ranks), replicas_ranks,				\
			       ARRAY_SIZE(replicas_ranks), self, expected_rc, &to_add,		\
			       &to_remove);

#define call_d_rank_list_free									\
	d_rank_list_free(to_add);								\
	d_rank_list_free(to_remove);

	/*
	 * We encode domains into ranks: rank / 10 = domain. For example, rank
	 * 1 is in domain 0, rank 11 is in domain 1, and rank 20 is in domain
	 * 2. See testu_rank_to_domain. Hence, each domain can have at most 10
	 * ranks.
	 * 
	 * The ranks arrays below must be monotically increasing.
	 */

	/* A PS is created one replica per domain. */
	{
		int		svc_rf = 2;
		d_rank_t	ranks[] = {
			 0,  1,
			10, 11,
			20, 21,
			30, 31,
			40, 41,
			50, 51
		};
		d_rank_t	down_ranks[] = {};
		d_rank_t	replicas_ranks[] = {};
		d_rank_t	self = CRT_NO_RANK;
		int		expected_dist[] = {0, 1, 1, 1, 1, 1};

		call_testu_plan_svc_reconfs(0)

		D_ASSERT(to_add->rl_nr == 5);
		D_ASSERT(to_remove->rl_nr == 0);

		D_ASSERT(testu_rank_set_has_dist(to_add, expected_dist, ARRAY_SIZE(expected_dist)));

		call_d_rank_list_free
	}

	/* A PS is created multiple replicas per domain. */
	{
		int		svc_rf = 2;
		d_rank_t	ranks[] = {
			 0,  1,  2,
			10, 11, 12,
			20, 21, 22
		};
		d_rank_t	down_ranks[] = {};
		d_rank_t	replicas_ranks[] = {};
		d_rank_t	self = CRT_NO_RANK;
		int		expected_dist[] = {1, 2, 2};

		call_testu_plan_svc_reconfs(0)

		D_ASSERT(to_add->rl_nr == 5);
		D_ASSERT(to_remove->rl_nr == 0);

		D_ASSERT(testu_rank_set_has_dist(to_add, expected_dist, ARRAY_SIZE(expected_dist)));

		call_d_rank_list_free
	}

	/* A happy PS does not want any changes. */
	{
		int		svc_rf = 2;
		d_rank_t	ranks[] = {
			 0,  1,
			10, 11,
			20, 21,
			30, 31,
			40, 41,
			50, 51
		};
		d_rank_t	down_ranks[] = {};
		d_rank_t	replicas_ranks[] = {0, 10, 20, 30, 40};
		d_rank_t	self = 0;

		call_testu_plan_svc_reconfs(0)

		D_ASSERT(to_add->rl_nr == 0);
		D_ASSERT(to_remove->rl_nr == 0);

		call_d_rank_list_free
	}

	/* The PS leader itself must not be undesired. */
	{
		int		svc_rf = 1;
		d_rank_t	ranks[] = {0, 1, 2};
		d_rank_t	down_ranks[] = {0};
		d_rank_t	replicas_ranks[] = {0, 1, 2};
		d_rank_t	self = 0;

		call_testu_plan_svc_reconfs(-DER_INVAL)
	}

	/* One lonely replica. */
	{
		int		svc_rf = 0;
		d_rank_t	ranks[] = {0};
		d_rank_t	down_ranks[] = {};
		d_rank_t	replicas_ranks[] = {0};
		d_rank_t	self = 0;

		call_testu_plan_svc_reconfs(0)

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
		d_rank_t	self = 0;

		call_testu_plan_svc_reconfs(0)

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
		d_rank_t	self = 0;

		call_testu_plan_svc_reconfs(0)

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
		d_rank_t	self = 0;

		call_testu_plan_svc_reconfs(0)

		D_ASSERT(testu_rank_sets_identical(to_add, expected_to_add,
						   ARRAY_SIZE(expected_to_add)));
		D_ASSERT(to_remove->rl_nr == 0);

		call_d_rank_list_free
	}

	/* A PS successfully achieves the RF, picking the best distribution. */
	{
		int		svc_rf = 1;
		d_rank_t	ranks[] = {
			 0,  1,
			10, 11,
			20, 21
		};
		d_rank_t	down_ranks[] = {};
		d_rank_t	replicas_ranks[] = {0};
		d_rank_t	self = 0;
		int		expected_dist[] = {1, 1, 1};
		d_rank_list_t  *new_replicas_ranks;

		call_testu_plan_svc_reconfs(0)

		new_replicas_ranks = testu_rank_sets_add(replicas_ranks, ARRAY_SIZE(replicas_ranks),
							 to_add);
		D_ASSERT(testu_rank_set_has_dist(new_replicas_ranks, expected_dist,
						 ARRAY_SIZE(expected_dist)));
		d_rank_list_free(new_replicas_ranks);
		D_ASSERT(to_remove->rl_nr == 0);

		call_d_rank_list_free
	}

	/* A PS removes the down rank even when there's no replacement. */
	{
		int		svc_rf = 1;
		d_rank_t	ranks[] = {0, 1, 2};
		d_rank_t	down_ranks[] = {2};
		d_rank_t	replicas_ranks[] = {0, 1, 2};
		d_rank_t	self = 0;
		d_rank_t	expected_to_remove[] = {2};

		call_testu_plan_svc_reconfs(0)

		D_ASSERT(to_add->rl_nr == 0);
		D_ASSERT(testu_rank_sets_identical(to_remove, expected_to_remove,
						   ARRAY_SIZE(expected_to_remove)));

		call_d_rank_list_free
	}

	/* A PS replaces one down rank. */
	{
		int		svc_rf = 1;
		d_rank_t	ranks[] = {
			 0,  1,
			10, 11,
			20, 21
		};
		d_rank_t	down_ranks[] = {21};
		d_rank_t	replicas_ranks[] = {0, 10, 21};
		d_rank_t	self = 0;
		d_rank_t	expected_to_add[] = {20};
		d_rank_t	expected_to_remove[] = {21};

		call_testu_plan_svc_reconfs(0)

		D_ASSERT(testu_rank_sets_identical(to_add, expected_to_add,
						ARRAY_SIZE(expected_to_add)));
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
		d_rank_t	self = 0;
		d_rank_t	expected_to_add[] = {1, 2, 3};

		call_testu_plan_svc_reconfs(0)

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
		d_rank_t	self = 0;
		d_rank_t	expected_to_remove[] = {1, 2};

		call_testu_plan_svc_reconfs(0)

		D_ASSERT(to_add->rl_nr == 0);
		D_ASSERT(testu_rank_sets_identical(to_remove, expected_to_remove,
						   ARRAY_SIZE(expected_to_remove)));

		call_d_rank_list_free
	}

	/* A PS removes down ranks while growing. */
	{
		int		svc_rf = 2;
		d_rank_t	ranks[] = {0, 1, 2, 3, 4, 5};
		d_rank_t	down_ranks[] = {2};
		d_rank_t	replicas_ranks[] = {0, 1, 2};
		d_rank_t	self = 0;
		d_rank_t	expected_to_add[] = {3, 4, 5};
		d_rank_t	expected_to_remove[] = {2};

		call_testu_plan_svc_reconfs(0)

		D_ASSERT(testu_rank_sets_identical(to_add, expected_to_add,
						   ARRAY_SIZE(expected_to_add)));
		D_ASSERT(testu_rank_sets_identical(to_remove, expected_to_remove,
						   ARRAY_SIZE(expected_to_remove)));

		call_d_rank_list_free
	}

	/* A PS removes undesired ranks first. */
	{
		int		svc_rf = 2;
		d_rank_t	ranks[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
		d_rank_t	down_ranks[] = {1, 2, 3};
		d_rank_t	replicas_ranks[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
		d_rank_t	self = 0;
		d_rank_t	expected_to_remove_candidates[] = {1, 2, 3, 4, 5, 6, 7, 8};
		d_rank_list_t	tmp;

		call_testu_plan_svc_reconfs(0)

		D_ASSERT(to_add->rl_nr == 0);
		D_ASSERT(to_remove->rl_nr == 4);
		tmp.rl_ranks = to_remove->rl_ranks;
		tmp.rl_nr = 3;
		D_ASSERT(testu_rank_sets_identical(&tmp, down_ranks, ARRAY_SIZE(down_ranks)));
		D_ASSERT(testu_rank_sets_belong(to_remove, expected_to_remove_candidates,
						ARRAY_SIZE(expected_to_remove_candidates)));

		call_d_rank_list_free
	}

	/* A PS removes from crowded domains first. */
	{
		int		svc_rf = 1;
		d_rank_t	ranks[] = {
			 0,  1,
			10, 11,
			20, 21
		};
		d_rank_t	down_ranks[] = {};
		d_rank_t	replicas_ranks[] = {0, 1, 10, 20, 21};
		d_rank_t	self = 0;
		int		expected_dist[] = {1, 1, 1};
		d_rank_list_t  *new_replicas_ranks;

		call_testu_plan_svc_reconfs(0)

		D_ASSERT(to_add->rl_nr == 0);
		D_ASSERT(to_remove->rl_nr == 2);
		new_replicas_ranks = testu_rank_sets_subtract(replicas_ranks,
							      ARRAY_SIZE(replicas_ranks),
							      to_remove);
		D_ASSERT(testu_rank_set_has_dist(new_replicas_ranks, expected_dist,
						 ARRAY_SIZE(expected_dist)));
		d_rank_list_free(new_replicas_ranks);

		call_d_rank_list_free
	}

	/* A shrink that is too complicated to comment on. */
	{
		int		svc_rf = 3;
		d_rank_t	ranks[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
		d_rank_t	down_ranks[] = {1, 3, 5, 7};
		d_rank_t	replicas_ranks[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
		d_rank_t	self = 0;
		d_rank_t	expected_to_add[] = {9};
		d_rank_t	expected_to_remove[] = {1, 3, 5, 7};

		call_testu_plan_svc_reconfs(0)

		D_ASSERT(testu_rank_sets_identical(to_add, expected_to_add,
						   ARRAY_SIZE(expected_to_add)));
		D_ASSERT(testu_rank_sets_identical(to_remove, expected_to_remove,
						   ARRAY_SIZE(expected_to_remove)));

		call_d_rank_list_free
	}

	/* A PS moves a replica out of a crowded domain. */
	{
		int		svc_rf = 1;
		d_rank_t	ranks[] = {
			 0,  1,
			10, 11,
			20, 21
		};
		d_rank_t	down_ranks[] = {};
		d_rank_t	replicas_ranks[] = {0, 1, 10};
		d_rank_t	self = 0;
		d_rank_t	expected_to_add_candidates[] = {20, 21};
		d_rank_t	expected_to_remove_candidates[] = {0, 1};

		call_testu_plan_svc_reconfs(0)

		D_ASSERT(to_add->rl_nr == 1);
		D_ASSERT(testu_rank_sets_belong(to_add, expected_to_add_candidates,
						ARRAY_SIZE(expected_to_add_candidates)));
		D_ASSERT(to_remove->rl_nr == 1);
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
		D_ERROR(DF_UUID": Pool child not found\n", DP_UUID(pool_id));
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

/*
 * 0: All ds_pool_child are started/stopped
 * 1: Some ds_pool_child are still in starting/stopping
 * 2: Some ds_pool_child need be started/stopped
 */
static int
check_pool_child_state(unsigned int tgt_id, d_list_t *pool_list, uint32_t expected)
{
	struct smd_pool_info	*pool_info;
	uint32_t		 state, inprogress;
	int			 rc = 0;

	D_ASSERT(expected == POOL_CHILD_NEW || expected == POOL_CHILD_STARTED);
	inprogress = (expected == POOL_CHILD_NEW) ? POOL_CHILD_STOPPING : POOL_CHILD_STARTING;

	d_list_for_each_entry(pool_info, pool_list, spi_link) {
		state = ds_pool_child_state(pool_info->spi_id, tgt_id);

		if (state == expected)
			continue;
		else if (state == inprogress)
			rc = 1;
		else
			return 2;
	}

	return rc;
}

static void
manage_target(bool start)
{
	struct smd_pool_info	*pool_info, *tmp;
	d_list_t		 pool_list;
	int			 pool_cnt, rc;

	D_INIT_LIST_HEAD(&pool_list);
	rc = smd_pool_list(&pool_list, &pool_cnt);
	if (rc) {
		DL_ERROR(rc, "Failed to list pools.");
		return;
	}

	d_list_for_each_entry(pool_info, &pool_list, spi_link) {
		if (start)
			rc = ds_pool_child_start(pool_info->spi_id, true);
		else
			rc = ds_pool_child_stop(pool_info->spi_id);

		if (rc < 0) {
			DL_ERROR(rc, DF_UUID": Failed to %s pool child.",
				 DP_UUID(pool_info->spi_id), start ? "start" : "stop");
			break;
		}
	}

	d_list_for_each_entry_safe(pool_info, tmp, &pool_list, spi_link) {
		d_list_del(&pool_info->spi_link);
		smd_pool_free_info(pool_info);
	}
}

static void
setup_target(void *arg)
{
	manage_target(true);
}

static void
teardown_target(void *arg)
{
	manage_target(false);
}

static int
manage_targets(int *tgt_ids, int tgt_cnt, d_list_t *pool_list, bool start)
{
	uint32_t	expected = start ? POOL_CHILD_STARTED : POOL_CHILD_NEW;
	int		i, rc, ret = 0;

	for (i = 0; i < tgt_cnt; i++) {
		rc = check_pool_child_state(tgt_ids[i], pool_list, expected);

		if (ret < rc)
			ret = rc;

		/* All pool child state are in (or transiting to) expected state */
		if (rc < 2)
			continue;

		rc = dss_ult_create(start ? setup_target : teardown_target, NULL, DSS_XS_VOS,
				    tgt_ids[i], 0, NULL);
		if (rc) {
			DL_ERROR(rc, "Failed to create ULT.");
			ret = rc;
			break;
		}
	}

	return ret;
}

static int
nvme_reaction(int *tgt_ids, int tgt_cnt, bool reint)
{
	struct smd_pool_info	*pool_info, *tmp;
	d_list_t		 pool_list;
	d_rank_t		 pl_rank;
	int			 pool_cnt, ret, rc, i;

	D_ASSERT(tgt_cnt > 0);
	D_ASSERT(tgt_ids != NULL);

	for (i = 0; i < tgt_cnt; i++) {
		if (tgt_ids[i] == BIO_SYS_TGT_ID) {
			if (reint) {
				D_ERROR("Auto reint sys target isn't supported.\n");
				return -DER_NOTSUPPORTED;
			}

			D_ERROR("SYS target SSD is failed, kill the engine...\n");
			rc = kill(getpid(), SIGKILL);
			if (rc != 0)
				D_ERROR("failed to raise SIGKILL: %d\n", errno);
			return 1;
		}
		D_ASSERT(tgt_ids[i] >= 0 && tgt_ids[i] < BIO_MAX_VOS_TGT_CNT);
	}

	D_INIT_LIST_HEAD(&pool_list);
	rc = smd_pool_list(&pool_list, &pool_cnt);
	if (rc) {
		D_ERROR("Failed to list pools: "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	if (reint) {
		rc = manage_targets(tgt_ids, tgt_cnt, &pool_list, true);
		if (rc) {
			if (rc < 0)
				DL_ERROR(rc, "Setup targets failed.");
			else
				D_DEBUG(DB_MGMT, "Setup targets is in-progress.\n");
			goto done;
		}
	}

	d_list_for_each_entry(pool_info, &pool_list, spi_link) {
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
	}

	if (!rc && !reint) {
		rc = manage_targets(tgt_ids, tgt_cnt, &pool_list, false);
		if (rc < 0)
			DL_ERROR(rc, "Teardown targets failed.");
		else if (rc > 0)
			D_DEBUG(DB_MGMT, "Teardown targets is in-progress.\n");
	}
done:
	d_list_for_each_entry_safe(pool_info, tmp, &pool_list, spi_link) {
		d_list_del(&pool_info->spi_link);
		smd_pool_free_info(pool_info);
	}

	D_DEBUG(DB_MGMT, "NVMe reaction done. tgt_cnt:%d, rc:%d\n", tgt_cnt, rc);
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

struct bio_reaction_ops nvme_reaction_ops = {
	.faulty_reaction	= nvme_faulty_reaction,
	.reint_reaction		= nvme_reint_reaction,
};
