/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "layout_test_helpers.h"

int pool_map_init(struct test_ctx *ctx)
{
	struct pool_buf		*buf;
	struct pool_component   *comps;
	struct pool_component   *comp;

	int			 total_ranks;
	int			 total_targets;
	int			 total_components;
	int			 i;
	int			 rc;

	total_ranks = ctx->nodes * ctx->ranks_per_node;
	total_targets = total_ranks * ctx->targets_per_rank;
	total_components = ctx->nodes + total_ranks + total_targets;

	buf = pool_buf_alloc(total_components);
	if (buf == NULL)
		return -DER_NOMEM;

	D_ALLOC_ARRAY(comps, total_components);
	if (comps == NULL) {
		D_FREE(buf);
		return -DER_NOMEM;
	}

	comp = &comps[0];
	
	for (i = 0; i < ctx->nodes; i++, comp++) {
		comp->co_type   = PO_COMP_TP_NODE;
		comp->co_status = PO_COMP_ST_UPIN;
		comp->co_id     = i;
		comp->co_rank   = 0;
		comp->co_ver    = 1;
		comp->co_nr     = ctx->ranks_per_node;
	}

	for (i = 0; i < total_ranks; i++, comp++) {
		comp->co_type   = PO_COMP_TP_RANK;
		comp->co_status = PO_COMP_ST_UPIN;
		comp->co_id     = i;
		comp->co_rank   = i;
		comp->co_ver    = 1;
		comp->co_nr     = ctx->targets_per_rank;
	}

	for (i = 0; i < total_targets; i++, comp++) {
		comp->co_type   = PO_COMP_TP_TARGET;
		comp->co_status = PO_COMP_ST_UPIN;
		comp->co_id     = i;
		comp->co_rank   = i / ctx->targets_per_rank;
		comp->co_index  = i % ctx->targets_per_rank;
		comp->co_ver    = 1;
		comp->co_nr     = 1;
	}

	rc = pool_buf_attach(buf, comps, total_components);
	if (rc != 0) {
		printf("pool_buf_attach failed rc=%d\n", rc);
		D_FREE(comps);
		D_FREE(buf);
		return rc;
	}

	rc = pool_map_create(buf, 1, &ctx->pool_map);
	if (rc != 0) {
		printf("pool_map_create failed rc=%d\n", rc);
		D_FREE(comps);
		D_FREE(buf);
		return rc;
	}
	D_FREE(comps);
	D_FREE(buf);
	return 0;
}

int placement_map_init(struct test_ctx *ctx)
{
	int rc;
	struct pl_map_init_attr  mia;

	if (ctx->pool_map == NULL) {
		printf("pool_map is NULL\n");
		return -DER_INVAL;
	}
	mia.ia_type            = PL_TYPE_JUMP_MAP;
	mia.ia_jump_map.domain = PO_COMP_TP_NODE;
	rc = pl_map_create(ctx->pool_map, &mia, &ctx->pl_map);
	if (rc != 0) {
		printf("pl_map_create failed rc=%d\n", rc);
		return rc;
	}
	return 0;
}

int generate_oids(struct test_ctx *ctx, struct test_oid *oids)
{
	int i;
	printf("Generating %d OIDs with class %u...\n", ctx->num_oids, ctx->oclass);
	for (i = 0; i < ctx->num_oids; i++) {
		oids[i].oid.hi = 0;
		oids[i].oid.lo = ((uint64_t)rand() << 32) | i;
		daos_obj_set_oid_by_class(&oids[i].oid, 0, ctx->oclass, 0);
	}

	return 0;
}

int capture_layouts(struct test_ctx *ctx,
		struct test_oid *oids,
		struct oid_layout *layouts)
{
	struct pl_obj_layout	*layout;
	int			 rc;
	int			 i;
	int			 j;
	struct daos_obj_md	 md = {0};

	printf("Capturing layouts for %d OIDs...\n", ctx->num_oids);
	for (i = 0; i < ctx->num_oids; i++) {
		md.omd_id  = oids[i].oid;
		md.omd_ver = 0;
		md.omd_pda = 1;
		layout = NULL;
		rc = pl_obj_place(ctx->pl_map, PLT_LAYOUT_VERSION, &md, 0, NULL, &layout);
		if (rc != 0 || layout == NULL) {
			printf("placement failed for oid %d rc=%d\n", i, rc);
			layouts[i].oid = oids[i].oid;
			layouts[i].shard_nr = 0;
			D_FREE(layout);
			continue;
		}

		layouts[i].oid = oids[i].oid;
		layouts[i].shard_nr = layout->ol_nr;

		for (j = 0; j < layout->ol_nr; j++) {
			layouts[i].targets[j] = layout->ol_shards[j].po_target;
		}
		pl_obj_layout_free(layout);
	}

	return 0;
}


int validate_configuration(uint32_t nodes, uint32_t ranks, uint32_t targets,
                              const char *object_class_str, struct operation *operations, int operation_count)
{
	if (nodes == 0 || ranks == 0 || targets == 0) {
		printf("ERROR: Nodes, ranks, and targets must be greater than 0\n");
		return -DER_INVAL;
	}

    uint32_t total_ranks = nodes * ranks;
    uint32_t total_targets = total_ranks * targets;
    uint32_t required_nodes, required_targets, grp, nr_grps = 0;

    struct daos_oclass_attr *oc;

    int cid = daos_oclass_name2id(object_class_str);
    if (cid == DAOS_OC_UNKNOWN) {
        fprintf(stderr, "Unknown object class: %s\n", object_class_str);
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
            grp = oc->ca_grp_nr;
            break;
        case DAOS_RES_EC:
            required_nodes = oc->u.ec.e_k + oc->u.ec.e_p;
            grp = oc->ca_grp_nr;
            break;
        default:
            printf("Unsupported oclass type\n");
            return -DER_INVAL;
    }
    
    if (nodes < required_nodes) {
        fprintf(stderr, "\nERROR: Insufficient nodes for %s class\n", object_class_str);
        fprintf(stderr, "Required : %u\n", required_nodes);
        fprintf(stderr, "Provided : %u\n", nodes);
        return -DER_INVAL;
    }

    required_targets = required_nodes * grp;
    if (total_targets < required_targets) {
        fprintf(stderr, "\nWARNING: only %u targets available, "
                "%u logical shards may be created\n",
                total_targets,
                required_targets);
    }
    if (operation_count == 0) {
                fprintf(stderr, "No valid operations parsed\n");
                return -DER_INVAL;
    }

    for (int i = 0; i < operation_count; i++) {
        if (operations[i].type == OP_EXCLUDE_RANK || operations[i].type == OP_REINTEGRATE_RANK) {
            uint32_t rank_id;
            if (sscanf(operations[i].args, "%u", &rank_id) != 1) {
                fprintf(stderr, "ERROR: Invalid rank ID in operation: %s\n", operations[i].args);
                return -DER_INVAL;
            }
            if (rank_id >= total_ranks) {
                fprintf(stderr, "ERROR: Rank ID %u out of range (total ranks: %u)\n", rank_id, total_ranks);
                return -DER_INVAL;
            }
        }
        if (operations[i].type == OP_EXCLUDE_NODE) {
            uint32_t node_id;
            if (sscanf(operations[i].args, "%u", &node_id) != 1) {
                fprintf(stderr, "ERROR: Invalid node ID in operation: %s\n", operations[i].args);
                return -DER_INVAL;
            }
            if (node_id >= nodes) {
                fprintf(stderr, "ERROR: Node ID %u out of range (total nodes: %u)\n", node_id, nodes);
                return -DER_INVAL;
            }
        }
        if (operations[i].type == OP_ADD_NODE) {
            uint32_t new_nodes;
            if (sscanf(operations[i].args, "%u", &new_nodes) != 1) {
                fprintf(stderr, "ERROR: Invalid node count in operation: %s\n", operations[i].args);
                return -DER_INVAL;
            }
            if (new_nodes == 0) {
                fprintf(stderr, "ERROR: Node count must be greater than 0 in operation: %s\n", operations[i].args);
                return -DER_INVAL;
            }
        }
    }

    return 0;
}

void
cleanup(struct test_ctx *ctx)
{
	if (ctx->pool_map != NULL)
		pool_map_decref(ctx->pool_map);

	pl_fini();
}
