/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <time.h>

#include "layout_test_helpers.h"
#include "../../pool/rpc.h"
#include "../../pool/srv_pool_map.h"

void
pool_map_and_pl_map_init(struct test_ctx *ctx)
{
	struct pool_component *comps;
	struct pool_component *comp;
	struct pool_buf       *buf;
	int                    nr_nodes = ctx->nodes;
	int                    nr_ranks = ctx->nodes * ctx->ranks_per_node;
	int                    nr_tgts  = ctx->nodes * ctx->ranks_per_node * ctx->targets_per_rank;
	int                    nr       = nr_nodes + nr_ranks + nr_tgts;
	int                    i;
	int                    rc;

	D_ALLOC_ARRAY(comps, nr);
	D_ASSERT(comps != NULL);
	ctx->setup_layout_test_mem += sizeof(*comps) * nr;
	comp = comps;

	/* Nodes (fault domains) */
	for (i = 0; i < nr_nodes; i++, comp++) {
		comp->co_type   = PO_COMP_TP_NODE;
		comp->co_status = PO_COMP_ST_UPIN;
		comp->co_id     = i;
		comp->co_rank   = i;
		comp->co_ver    = 1;
		comp->co_nr     = ctx->ranks_per_node;
	}

	/* Ranks */
	for (i = 0; i < nr_ranks; i++, comp++) {
		comp->co_type   = PO_COMP_TP_RANK;
		comp->co_status = PO_COMP_ST_UPIN;
		comp->co_id     = i;
		comp->co_rank   = i;
		comp->co_ver    = 1;
		comp->co_nr     = ctx->targets_per_rank;
	}

	/* Targets */
	for (i = 0; i < nr_tgts; i++, comp++) {
		comp->co_type   = PO_COMP_TP_TARGET;
		comp->co_status = PO_COMP_ST_UPIN;
		comp->co_id     = i;
		comp->co_rank   = i / ctx->targets_per_rank;
		comp->co_index  = i % ctx->targets_per_rank;
		comp->co_ver    = 1;
		comp->co_nr     = 1;
	}

	buf = pool_buf_alloc(nr);
	D_ASSERT(buf != NULL);
	ctx->setup_daos_api_mem += pool_buf_size(nr);
	rc = pool_buf_attach(buf, comps, nr);
	assert_success(rc);
	D_FREE(comps);

	rc = pool_map_create(buf, 1, &ctx->pool_map);
	assert_success(rc);
	D_FREE(buf);

	rc = pl_map_update(ctx->uuid, ctx->pool_map, true, PL_TYPE_JUMP_MAP);
	assert_success(rc);

	ctx->pl_map = pl_map_find(ctx->uuid, DAOS_OBJ_NIL);
	D_ASSERT(ctx->pl_map != NULL);
	ctx->setup_daos_api_mem += sizeof(*ctx->pl_map);
}

/*
 * Rebuild the placement map under ctx->uuid from the current pool map,
 * release the stale ctx->pl_map reference, and re-acquire the fresh one
 * from the hash table.  Must be called after every pool map mutation so
 * that ctx->pl_map always reflects the latest topology.
 */
static int
refresh_pl_map(struct test_ctx *ctx)
{
	int rc;

	rc = pl_map_update(ctx->uuid, ctx->pool_map, false, PL_TYPE_JUMP_MAP);
	if (rc != 0) {
		D_ERROR("pl_map_update failed rc=%d\n", rc);
		return rc;
	}
	pl_map_decref(ctx->pl_map);
	ctx->pl_map = pl_map_find(ctx->uuid, DAOS_OBJ_NIL);
	D_ASSERT(ctx->pl_map != NULL);
	return 0;
}

/*
 * Rebuild ctx->pool_map from its serialized buffer so map-internal cached
 * values (like in_ver/fseq minima) are recomputed as in a fresh initialize.
 */
static int
reinitialize_pool_map(struct test_ctx *ctx)
{
	struct pool_buf *buf     = NULL;
	struct pool_map *new_map = NULL;
	uint32_t         ver;
	int              rc;

	ver = pool_map_get_version(ctx->pool_map);
	rc  = pool_buf_extract(ctx->pool_map, &buf);
	if (rc != 0) {
		D_ERROR("pool_buf_extract failed rc=%d\n", rc);
		return rc;
	}

	rc = pool_map_create(buf, ver, &new_map);
	pool_buf_free(buf);
	if (rc != 0) {
		D_ERROR("pool_map_create failed rc=%d\n", rc);
		return rc;
	}

	pool_map_decref(ctx->pool_map);
	ctx->pool_map = new_map;
	return 0;
}

void
generate_oids(struct test_ctx *ctx)
{
	int  i;
	char ename[MAX_OBJ_CLASS_NAME_LEN];
	daos_oclass_id2name(ctx->oclass, ename);
	printf("Generating %d OIDs with class %s...\n", ctx->num_oids, ename);
	for (i = 0; i < ctx->num_oids; i++) {
		ctx->oids[i].obj_md.omd_id.hi    = 0;
		ctx->oids[i].obj_md.omd_id.lo    = ((uint64_t)rand() << 32) | i;
		ctx->oids[i].obj_md.omd_ver      = pool_map_get_version(ctx->pool_map);
		ctx->oids[i].obj_md.omd_pda      = 1;
		ctx->oids[i].obj_md.omd_pdom_lvl = PO_COMP_TP_ROOT;
		daos_obj_set_oid_by_class(&ctx->oids[i].obj_md.omd_id, 0, ctx->oclass, 0);
	}
}

int
capture_layouts(struct test_ctx *ctx, struct pl_obj_layout **layouts, double *ms_place_out)
{
	int      rc;
	int      i;
	uint32_t ver;

	ver = pool_map_get_version(ctx->pool_map);

	*ms_place_out = 0.0;
	for (i = 0; i < ctx->num_oids; i++) {
		struct timespec _t0, _t1;
		ctx->oids[i].obj_md.omd_ver = ver;
		clock_gettime(CLOCK_MONOTONIC, &_t0);
		rc = pl_obj_place(ctx->pl_map, PLT_LAYOUT_VERSION, &ctx->oids[i].obj_md, 0, NULL,
				  &layouts[i]);
		clock_gettime(CLOCK_MONOTONIC, &_t1);
		*ms_place_out +=
		    (_t1.tv_sec - _t0.tv_sec) * 1e3 + (_t1.tv_nsec - _t0.tv_nsec) / 1e6;
		if (rc != 0 || layouts[i] == NULL) {
			fprintf(stderr, "ERROR: placement failed for oid %d rc=%d\n", i, rc);
			return (rc != 0) ? rc : -DER_INVAL;
		}
	}
	return 0;
}

void
free_layouts(struct pl_obj_layout **layouts, int num_oids)
{
	int i;

	if (layouts == NULL)
		return;
	for (i = 0; i < num_oids; i++) {
		if (layouts[i] != NULL)
			pl_obj_layout_free(layouts[i]);
	}
	D_FREE(layouts);
}

void
free_diffs(struct shard_diff *diff, int num_oids)
{
	int i;

	if (diff == NULL)
		return;
	for (i = 0; i < num_oids; i++) {
		D_FREE(diff[i].shard_ids);
		D_FREE(diff[i].spare_tgts);
	}
	D_FREE(diff);
}

/**
 * Perform two layout comparisons after an operation:
 *
 * Check 1 – Initial vs final (bounded drift):
 *   Compare pre_layout against post_layout directly.  The number of shards
 *   whose target differs within each group must not exceed max_diff.  This
 *   verifies that the operation did not displace more shards per group than
 *   the fault-tolerance budget allows.
 *
 * Check 2 – Diff applied to original must equal final exactly:
 *   Apply every rebuild diff entry (shard_ids / spare_tgts) to a temporary
 *   copy of pre_layout.  The resulting merged layout is then compared
 *   against post_layout and must match target-for-target with zero
 *   mismatches.  This verifies that the diff fully and precisely describes
 *   all shard movements.
 *
 * @param ctx         Test context (num_oids, oclass)
 * @param pre_layout  Layout captured before the operation (not modified)
 * @param diff        Per-OID rebuild diff from set_tgt_status_and_find_diff()
 * @param post_layout Layout captured after the final status change
 * @param max_diff    Maximum allowed shard target differences per group
 *                    (Check 1 only; Check 2 requires an exact match)
 */
int
compare_layout(struct test_ctx *ctx, struct pl_obj_layout **pre_layout, struct shard_diff *diff,
	       struct pl_obj_layout **post_layout, int max_diff, enum operation_type op_type)
{
	int i;
	int j;
	int k;
	int total_shards    = 0;
	int moved_shards    = 0;
	int reint_drift_cnt = 0; /* Check 1 excess-drift groups for OP_REINT */

	/* ------------------------------------------------------------------ *
	 * Check 1: Compare initial vs final layout.                           *
	 * Per-group shard diff count must not exceed max_diff.                *
	 * ------------------------------------------------------------------ */
	for (i = 0; i < ctx->num_oids; i++) {
		for (j = 0; j < (int)pre_layout[i]->ol_grp_nr; j++) {
			int shard_diff_cnt = 0;

			for (k = 0; k < (int)pre_layout[i]->ol_grp_size; k++) {
				struct pl_obj_shard *s1;
				struct pl_obj_shard *s2;

				s1 = &pre_layout[i]->ol_shards[j * pre_layout[i]->ol_grp_size + k];
				s2 =
				    &post_layout[i]->ol_shards[j * post_layout[i]->ol_grp_size + k];

				if (s1->po_target != s2->po_target) {
					shard_diff_cnt++;
				}
			}

			if (shard_diff_cnt > max_diff) {
				if (op_type == OP_REINT) {
					reint_drift_cnt++;
				} else {
					fprintf(stderr,
						"  ERROR: OID %d grp %d: %d/%d shards differ "
						"(max allowed: %d)\n",
						i, j, shard_diff_cnt, pre_layout[i]->ol_grp_size,
						max_diff);
					return -DER_IO;
				}
			}
		}
	}

	if (reint_drift_cnt > 0)
		printf("  WARNING: The shards moved are more than %d allowed shard(s) movement "
		       "during reintegration.\n"
		       "           This is expected when the order of reintegrations is not in "
		       "LIFO order\n",
		       max_diff);

	/* ------------------------------------------------------------------ *
	 * Check 2: Apply diff to original, result must equal final exactly.  *
	 * ------------------------------------------------------------------ */
	for (i = 0; i < ctx->num_oids; i++) {
		total_shards += pre_layout[i]->ol_nr;
		moved_shards += diff[i].nr;
	}

	printf("  Moved: %d/%d shards (%.1f%%)\n", moved_shards, total_shards,
	       total_shards > 0 ? 100.0 * moved_shards / total_shards : 0.0);

	for (i = 0; i < ctx->num_oids; i++) {
		struct pl_obj_shard *merged;
		int                  nr = pre_layout[i]->ol_nr;

		/* Temporary copy of pre_layout so we do not mutate the original. */
		D_ALLOC_ARRAY(merged, nr);
		if (merged == NULL) {
			fprintf(stderr, "  ERROR: failed to allocate merged layout\n");
			return -DER_NOMEM;
		}
		memcpy(merged, pre_layout[i]->ol_shards, nr * sizeof(struct pl_obj_shard));

		/* Apply diff entries to the copy. */
		for (j = 0; j < diff[i].nr; j++) {
			uint32_t shard_idx = diff[i].shard_ids[j];
			uint32_t spare_tgt = diff[i].spare_tgts[j];

			if (shard_idx < (uint32_t)nr)
				merged[shard_idx].po_target = spare_tgt;
		}

		/* Merged layout must match post_layout with zero mismatches. */
		for (j = 0; j < (int)pre_layout[i]->ol_grp_nr; j++) {
			for (k = 0; k < (int)pre_layout[i]->ol_grp_size; k++) {
				int                  idx = j * pre_layout[i]->ol_grp_size + k;
				struct pl_obj_shard *s1  = &merged[idx];
				struct pl_obj_shard *s2  = &post_layout[i]->ol_shards[idx];

				if (s1->po_target != s2->po_target) {
					fprintf(stderr,
						"  ERROR: OID %d grp %d shard %d: "
						"merged target %u != post target %u\n",
						i, j, k, s1->po_target, s2->po_target);
					D_FREE(merged);
					return -DER_IO;
				}
			}
		}

		D_FREE(merged);
	}
	printf("  Validation succeeded\n");
	return 0;
}

int
validate_configuration(int64_t nodes, int64_t ranks_per_node, int64_t tgts_per_rank,
		       int64_t obj_count, const char *object_class_str,
		       struct operation *operations, int operation_count)
{
	struct daos_oclass_attr *oc;
	int64_t                  total_ranks;
	int64_t                  total_targets;
	int64_t                  required_nodes;
	int64_t                  required_targets;
	int64_t                  grp;
	uint32_t                 nr_grps = 0;
	int                      cid;
	int                      i;
	int                      j;

	if (nodes <= 0 || ranks_per_node <= 0 || tgts_per_rank <= 0 || obj_count <= 0) {
		fprintf(
		    stderr,
		    "ERROR: nodes, ranks, targets, and object count must be positive integers\n");
		return -DER_INVAL;
	}

	total_ranks   = nodes * ranks_per_node;
	total_targets = total_ranks * tgts_per_rank;

	cid = daos_oclass_name2id(object_class_str);
	if (cid == DAOS_OC_UNKNOWN) {
		fprintf(stderr, "ERROR: unknown object class: %s\n", object_class_str);
		return -DER_INVAL;
	}

	oc = daos_oclass_id2attr(cid, &nr_grps);
	if (!oc) {
		D_DEBUG(DB_PL, "Unknown object class %u\n", (unsigned int)cid);
		return -DER_INVAL;
	}

	switch (oc->ca_resil) {
	case DAOS_RES_REPL:
		required_nodes = oc->u.rp.r_num;
		grp            = oc->ca_grp_nr;
		break;
	case DAOS_RES_EC:
		required_nodes = oc->u.ec.e_k + oc->u.ec.e_p;
		grp            = oc->ca_grp_nr;
		break;
	default:
		fprintf(stderr, "ERROR: unsupported oclass resilience type\n");
		return -DER_INVAL;
	}

	if (nodes < required_nodes) {
		fprintf(stderr,
			"ERROR: The given object class requires minimum %lld number of "
			"fault domains/nodes\n",
			(long long)required_nodes);
		return -DER_INVAL;
	}

	/*
	 * For GX classes ca_grp_nr == DAOS_OBJ_GRP_MAX (65535); the actual
	 * group count is resolved at placement time from the available targets.
	 * The minimum viable pool only needs one group, i.e. required_nodes
	 * targets.  Use that lower bound instead of the sentinel value.
	 */
	required_targets = required_nodes * (grp == DAOS_OBJ_GRP_MAX ? 1 : grp);
	if (total_targets < required_targets) {
		fprintf(stderr,
			"ERROR: insufficient targets available: required %lld, provided %lld\n",
			(long long)required_targets, (long long)total_targets);
		return -DER_INVAL;
	}

	if (operation_count == 0) {
		fprintf(stderr, "ERROR: no operations specified (use --operation)\n");
		return -DER_INVAL;
	}

	for (i = 0; i < operation_count; i++) {
		struct operation *op = &operations[i];

		if (op->nr_args == 0) {
			fprintf(stderr, "ERROR: operation %d has no arguments\n", i);
			return -DER_INVAL;
		}

		/*
		 * For OP_ADD, node IDs must be assigned sequentially: the first
		 * arg must equal the current node count, the second arg must be
		 * current + 1, and so on.  Gaps or duplicates are rejected.
		 * After validation, update the running topology counts so that
		 * subsequent operations in the list can reference the new nodes,
		 * ranks, and targets.
		 */
		if (op->type == OP_ADD) {
			for (j = 0; j < op->nr_args; j++) {
				if ((int64_t)op->args[j] != nodes + j) {
					fprintf(stderr,
						"ERROR: op %d: node ID %d is not the next "
						"sequential node (expected %lld)\n",
						i, op->args[j], (long long)(nodes + j));
					return -DER_INVAL;
				}
			}
			nodes += op->nr_args;
			total_ranks += (int64_t)op->nr_args * ranks_per_node;
			total_targets += (int64_t)op->nr_args * ranks_per_node * tgts_per_rank;
			continue;
		}

		/* Validate each argument ID is within range for the given mode */
		for (j = 0; j < op->nr_args; j++) {
			int id = op->args[j];

			if (id < 0) {
				fprintf(stderr, "ERROR: operation %d: negative ID %d\n", i, id);
				return -DER_INVAL;
			}

			switch (op->component) {
			case RANK:
				if ((int64_t)id >= total_ranks) {
					fprintf(stderr,
						"ERROR: op %d: rank %d out of range (total: %ld)\n",
						i, id, total_ranks);
					return -DER_INVAL;
				}
				break;
			case NODE:
				if ((int64_t)id >= nodes) {
					fprintf(stderr,
						"ERROR: op %d: node %d out of range (total: %ld)\n",
						i, id, nodes);
					return -DER_INVAL;
				}
				break;
			case TARGET:
				if ((int64_t)id >= total_targets) {
					fprintf(
					    stderr,
					    "ERROR: op %d: target %d out of range (total: %ld)\n",
					    i, id, total_targets);
					return -DER_INVAL;
				}
				break;
			default:
				fprintf(stderr, "ERROR: op %d: invalid mode %d\n", i,
					op->component);
				return -DER_INVAL;
			}
		}
	}

	return 0;
}

void
cleanup(struct test_ctx *ctx)
{
	if (ctx->pl_map != NULL) {
		pl_map_decref(ctx->pl_map);
		ctx->pl_map = NULL;
	}
	if (ctx->pool_map != NULL)
		pool_map_decref(ctx->pool_map);

	pl_fini();
}

/**
 * Build a flat list of all target IDs for a given operation.
 *
 * Handles all three component types:
 *   TARGET – args are raw target IDs; copied directly into the output list.
 *   RANK   – each arg is a rank ID; all targets of that rank are merged in.
 *   NODE   – each arg is a node ID; all targets of that node are merged in.
 *
 * @param ctx   Test context
 * @param op    Operation describing component type and ID arguments
 * @param tgts  Output target ID list (caller must free with pool_target_id_list_free)
 *
 * @return 0 on success, negative error on failure
 */
int
fetch_targets(struct test_ctx *ctx, struct operation *op, struct pool_target_id_list *tgts)
{
	int a;
	int rc = 0;

	if (op->component == TARGET) {
		rc = pool_target_id_list_alloc(op->nr_args, tgts);
		if (rc != 0)
			return rc;
		for (a = 0; a < op->nr_args; a++)
			tgts->pti_ids[a].pti_id = op->args[a];
	} else {
		pool_comp_type_t comp_type =
		    (op->component == RANK) ? PO_COMP_TP_RANK : PO_COMP_TP_NODE;

		for (a = 0; a < op->nr_args; a++) {
			struct pool_target_id_list tmp    = {0};
			struct pool_domain        *domain = NULL;
			int                        t;

			rc = pool_map_find_domain(ctx->pool_map, comp_type, op->args[a], &domain);
			if (rc != 1) {
				printf("ERROR: Cannot find domain type=%d id=%d (rc=%d)\n",
				       comp_type, op->args[a], rc);
				return -DER_NONEXIST;
			}

			D_ALLOC_ARRAY(tmp.pti_ids, domain->do_target_nr);
			if (tmp.pti_ids == NULL)
				return -DER_NOMEM;

			tmp.pti_number = domain->do_target_nr;
			for (t = 0; t < domain->do_target_nr; t++)
				tmp.pti_ids[t].pti_id = domain->do_targets[t].ta_comp.co_id;

			rc = pool_target_id_list_merge(tgts, &tmp);
			pool_target_id_list_free(&tmp);
			if (rc != 0)
				return rc;
		}
	}

	return 0;
}

/**
 * Apply a two-stage component status change for the given target list and
 * collect the rebuild diff between the two stages.
 *
 * Stage 1 sets the intermediate status (e.g. DOWN for exclude), then calls
 * pl_obj_find_rebuild() for every OID to discover which shards need to move
 * and where their spare targets are.  Stage 2 advances to the final status
 * (e.g. DOWNOUT for exclude) and refreshes the placement map.
 *
 * Two-stage mapping:
 *   OP_EXCLUDE : DOWN  -> DOWNOUT
 *   OP_DRAIN   : DRAIN -> DOWNOUT
 *   OP_REINT   : UP    -> UPIN
 *   OP_ADD     : (NEW already set by add_node) -> UPIN
 *
 * @param ctx             Test context (pool_map, pl_map, uuid, num_oids)
 * @param tgts            List of target IDs to update
 * @param op_type         Operation type (OP_EXCLUDE, OP_REINT, OP_DRAIN, OP_ADD)
 * @param diff            Output array (length ctx->num_oids); each entry receives
 *                        the shard indices and spare target IDs for that OID
 * @param ms_rebuild_out  Accumulated wall-clock time (ms) spent in pl_obj_find_rebuild()
 */
int
set_tgt_status_and_find_diff(struct test_ctx *ctx, struct pool_target_id_list *tgts,
			     enum operation_type op_type, struct shard_diff *diff,
			     double *ms_rebuild_out)
{
	struct timespec t0, t1;
	uint32_t       *spare_tgts = NULL;
	uint32_t       *shard_ids  = NULL;
	uint32_t        buf_size   = MAX_SHARDS;
	uint32_t        ver;
	int             opc1;
	int             opc2;
	int             rc;
	int             i;

	/* Map operation type to the two sequential map opcodes */
	switch (op_type) {
	case OP_EXCLUDE:
		opc1 = MAP_EXCLUDE;
		opc2 = MAP_EXCLUDE_OUT;
		break;
	case OP_DRAIN:
		opc1 = MAP_DRAIN;
		opc2 = MAP_EXCLUDE_OUT;
		break;
	case OP_REINT:
		opc1 = MAP_REINT;
		opc2 = MAP_ADD_IN;
		break;
	case OP_ADD:
		/*
		 * update_one_dom is called by ds_pool_map_tgts_update() only when
		 * rc > 0 (i.e., the target status actually changed) or exclude_rank == true.
		 * For MAP_ADD_IN, targets already-UPIN return rc=0 (nothing to do),
		 * so update_one_dom is skipped for those — but for targets transitioning UP→UPIN
		 * it returns rc=1 and update_one_dom is reached.
		 * Since MAP_ADD_IN has no case in update_one_dom (falls through to default: break),
		 * the domain is never promoted to UPIN.  So, use MAP_EXTEND + MAP_FINISH_REBUILD
		 * instead, which does promote the domain to UPIN.
		 */
		opc1 = MAP_EXTEND;
		opc2 = MAP_FINISH_REBUILD;
		break;
	default:
		D_ERROR("Invalid operation type %d\n", op_type);
		return -DER_INVAL;
	}

	/* Stage 1: advance targets to the intermediate status */
	rc = ds_pool_map_tgts_update(NULL, ctx->pool_map, tgts, opc1, false, NULL, false);
	if (rc != 0) {
		D_ERROR("ds_pool_map_tgts_update (stage 1) failed rc=%d\n", rc);
		return rc;
	}
	pool_map_update_failed_cnt(ctx->pool_map);
	ver = pool_map_get_version(ctx->pool_map);
	rc  = reinitialize_pool_map(ctx);
	if (rc != 0)
		return rc;
	rc = refresh_pl_map(ctx);
	if (rc != 0)
		return rc;

	/*
	 * Identify which shards need to be rebuilt/moved and their
	 * spare targets at the intermediate state.  Scratch buffers start
	 * at MAX_SHARDS entries; if pl_obj_find_rebuild returns -DER_REC2BIG
	 * they are grown by 64 and the call is retried.  The grown buffers
	 * are reused across OIDs to avoid repeated allocation.
	 */
	D_ALLOC_ARRAY(spare_tgts, buf_size);
	if (spare_tgts == NULL)
		return -DER_NOMEM;
	D_ALLOC_ARRAY(shard_ids, buf_size);
	if (shard_ids == NULL) {
		D_FREE(spare_tgts);
		return -DER_NOMEM;
	}

	*ms_rebuild_out = 0.0;
	for (i = 0; i < ctx->num_oids; i++) {
		int j;

		diff[i].nr                  = 0;
		diff[i].shard_ids           = NULL;
		diff[i].spare_tgts          = NULL;
		ctx->oids[i].obj_md.omd_ver = ver;

retry:
		clock_gettime(CLOCK_MONOTONIC, &t0);
		diff[i].nr =
		    pl_obj_find_rebuild(ctx->pl_map, PLT_LAYOUT_VERSION, &ctx->oids[i].obj_md, NULL,
					ver, spare_tgts, shard_ids, buf_size);
		clock_gettime(CLOCK_MONOTONIC, &t1);
		*ms_rebuild_out += (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;

		if (diff[i].nr == -DER_REC2BIG) {
			/*
			 * Output buffer too small; grow both arrays by 64 and retry.
			 */
			uint32_t  new_size = buf_size + 64;
			uint32_t *new_spare;
			uint32_t *new_shard;

			D_ALLOC_ARRAY(new_spare, new_size);
			D_ALLOC_ARRAY(new_shard, new_size);
			if (new_spare == NULL || new_shard == NULL) {
				D_FREE(new_spare);
				D_FREE(new_shard);
				D_FREE(spare_tgts);
				D_FREE(shard_ids);
				return -DER_NOMEM;
			}
			D_FREE(spare_tgts);
			D_FREE(shard_ids);
			spare_tgts = new_spare;
			shard_ids  = new_shard;
			buf_size   = new_size;
			goto retry;
		}

		if (diff[i].nr < 0) {
			D_ERROR("pl_obj_find_rebuild failed for OID %d rc=%d\n", i, diff[i].nr);
			D_FREE(spare_tgts);
			D_FREE(shard_ids);
			return diff[i].nr;
		}

		if (diff[i].nr > 0) {
			D_ALLOC_ARRAY(diff[i].shard_ids, diff[i].nr);
			if (diff[i].shard_ids == NULL) {
				D_FREE(spare_tgts);
				D_FREE(shard_ids);
				return -DER_NOMEM;
			}
			D_ALLOC_ARRAY(diff[i].spare_tgts, diff[i].nr);
			if (diff[i].spare_tgts == NULL) {
				D_FREE(diff[i].shard_ids);
				D_FREE(spare_tgts);
				D_FREE(shard_ids);
				return -DER_NOMEM;
			}
			for (j = 0; j < diff[i].nr; j++) {
				diff[i].shard_ids[j]  = shard_ids[j];
				diff[i].spare_tgts[j] = spare_tgts[j];
			}
		}
	}
	D_FREE(spare_tgts);
	D_FREE(shard_ids);

	/* Stage 2: advance targets to the final status */
	rc = ds_pool_map_tgts_update(NULL, ctx->pool_map, tgts, opc2, false, NULL, false);
	if (rc != 0) {
		D_ERROR("ds_pool_map_tgts_update (stage 2) failed rc=%d\n", rc);
		return rc;
	}
	pool_map_update_failed_cnt(ctx->pool_map);
	rc = reinitialize_pool_map(ctx);
	if (rc != 0)
		return rc;
	rc = refresh_pl_map(ctx);
	if (rc != 0)
		return rc;
	return 0;
}

/**
 * Add a new node to the pool map.
 *
 * Allocates one node, ranks_per_node ranks, and
 * ranks_per_node * targets_per_rank targets, all with PO_COMP_ST_NEW status.
 * IDs are assigned sequentially after the current pool map population.
 * The pool version is bumped.
 *
 * @param ctx      Test context (pool_map, ranks_per_node, targets_per_rank)
 * @param node_id  ID to assign to the new node
 *
 * @return 0 on success, negative DAOS error code on failure
 */
int
add_node(struct test_ctx *ctx, uint32_t node_id)
{
	struct pool_component *comps;
	struct pool_component *comp;
	struct pool_buf       *buf;
	uint32_t               ver;
	uint32_t               base_rank;
	uint32_t               base_tgt;
	int                    nr_ranks;
	int                    nr_tgts;
	int                    nr_comps;
	int                    r;
	int                    t;
	int                    rc;

	nr_ranks = ctx->ranks_per_node;
	nr_tgts  = ctx->ranks_per_node * ctx->targets_per_rank;
	nr_comps = 1 + nr_ranks + nr_tgts;

	/* New IDs continue from the current pool map population */
	base_rank = node_id * ctx->ranks_per_node;
	base_tgt  = pool_map_find_target(ctx->pool_map, PO_COMP_ID_ALL, NULL);
	ver       = pool_map_get_version(ctx->pool_map) + 1;

	D_ALLOC_ARRAY(comps, nr_comps);
	if (comps == NULL)
		return -DER_NOMEM;

	comp = comps;

	/* Node */
	comp->co_type   = PO_COMP_TP_NODE;
	comp->co_status = PO_COMP_ST_NEW;
	comp->co_id     = node_id;
	comp->co_rank   = 0;
	comp->co_ver    = ver;
	comp->co_fseq   = 1;
	comp->co_nr     = nr_ranks;
	comp++;

	/* Ranks (one per ranks_per_node slot) */
	for (r = 0; r < nr_ranks; r++, comp++) {
		comp->co_type   = PO_COMP_TP_RANK;
		comp->co_status = PO_COMP_ST_NEW;
		comp->co_id     = base_rank + r;
		comp->co_rank   = base_rank + r;
		comp->co_ver    = ver;
		comp->co_fseq   = 1;
		comp->co_nr     = ctx->targets_per_rank;
	}

	/* Targets */
	for (t = 0; t < nr_tgts; t++, comp++) {
		comp->co_type   = PO_COMP_TP_TARGET;
		comp->co_status = PO_COMP_ST_NEW;
		comp->co_id     = base_tgt + t;
		comp->co_rank   = base_rank + t / ctx->targets_per_rank;
		comp->co_index  = t % ctx->targets_per_rank;
		comp->co_ver    = ver;
		comp->co_fseq   = 1;
		comp->co_nr     = 1;
	}

	buf = pool_buf_alloc(nr_comps);
	if (buf == NULL) {
		D_FREE(comps);
		return -DER_NOMEM;
	}

	rc = pool_buf_attach(buf, comps, nr_comps);
	D_FREE(comps);
	if (rc != 0) {
		D_FREE(buf);
		return rc;
	}

	rc = pool_map_extend(ctx->pool_map, ver, buf);
	D_FREE(buf);
	return rc;
}