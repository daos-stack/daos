/**
 * (C) Copyright 2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * pl_debug - Interactive placement debugging utility
 *
 * Usage: pl_debug -n <nodes> -r <ranks_per_node> -t <targets_per_rank>
 *
 * Constructs an in-memory pool map and jump placement map from the given
 * topology parameters.  NODE is used as the fault domain.
 *
 * Interactive commands:
 *   obj_class <name|id>           - Set current object class
 *   gen_layout id=<number> [mode=<pre_rebuild|current|post_rebuild>] [ver=<number>]
 *   set_down rank=<n>|node=<n>    - Set rank/node status to DOWN
 *   set_downout rank=<n>|node=<n> - Set rank/node status to DOWNOUT
 *   set_up rank=<n>|node=<n>      - Set rank/node status to UP
 *   set_upin rank=<n>|node=<n>    - Set rank/node status to UPIN
 *   help                          - Show this help text
 *   quit / exit                   - Exit the tool
 *
 * Smoke-test example:
 *   $ pl_debug -n 4 -r 2 -t 8
 *   pl_debug> obj_class OC_EC_4P1GX
 *   pl_debug> gen_layout id=42
 *   pl_debug> gen_layout id=42 mode=current ver=3
 *   pl_debug> set_down rank=0
 *   pl_debug> gen_layout id=42 mode=post_rebuild
 *   pl_debug> quit
 */

#define D_LOGFAC DD_FAC(tests)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <inttypes.h>

#include <daos/common.h>
#include <daos/placement.h>
#include <daos.h>
#include <daos/object.h>
#include <daos/pool_map.h>

#include "place_obj_common.h"

/* Internal pool-server APIs used to apply target state changes */
#include "../../pool/rpc.h"
#include "../../pool/srv_pool_map.h"
/* layout_gen_mode enum (PRE_REBUILD / CURRENT / POST_REBUILD) */
#include "../pl_map.h"

/* Layout version used by all placement tests */
#define PLD_LAYOUT_VERSION 2

/* Sentinel for the optional second map_update_opc argument: pass this to skip */
#define PLD_NO_OPC (-1)

/* ------------------------------------------------------------------ */
/* Global state                                                         */
/* ------------------------------------------------------------------ */

static struct pool_map   *g_po_map;
static struct pl_map     *g_pl_map;
static daos_oclass_id_t   g_obj_class = OC_RP_3GX;

/* ------------------------------------------------------------------ */
/* Help                                                                 */
/* ------------------------------------------------------------------ */

static void
print_help(void)
{
	printf("Commands:\n"
	       "  obj_class <name|id>           Set current object class\n"
	       "  gen_layout id=<number> [mode=<m>] [ver=<number>]\n"
	       "                                Generate layout (OID lo=<number>)\n"
	       "                                mode: pre_rebuild|0, current|1, post_rebuild|2\n"
	       "                                      (default: pre_rebuild)\n"
	       "                                ver:  pool map version (default: current)\n"
	       "  set_down rank=<n>|node=<n>    Set rank/node to DOWN\n"
	       "  set_downout rank=<n>|node=<n> Set rank/node to DOWNOUT\n"
	       "  set_up rank=<n>|node=<n>      Set rank/node to UP\n"
	       "  set_upin rank=<n>|node=<n>    Set rank/node to UPIN\n"
	       "  help                          Show this help\n"
	       "  quit / exit                   Exit pl_debug\n");
}

/* ------------------------------------------------------------------ */
/* Placement map refresh                                                */
/* ------------------------------------------------------------------ */

static void
refresh_pl_map(void)
{
	struct pl_map_init_attr mia = {0};

	if (g_pl_map != NULL) {
		pl_map_decref(g_pl_map);
		g_pl_map = NULL;
	}
	mia.ia_type        = PL_TYPE_JUMP_MAP;
	mia.ia_ring.domain = PO_COMP_TP_NODE;
	if (pl_map_create(g_po_map, &mia, &g_pl_map) != 0)
		fprintf(stderr, "pl_map_create failed; placement unavailable\n");
}

/* ------------------------------------------------------------------ */
/* Target-list helpers                                                  */
/* ------------------------------------------------------------------ */

static int
build_rank_tgt_list(uint32_t rank, struct pool_target_id_list *tgts)
{
	d_rank_t      rank_id = rank;
	d_rank_list_t rank_list;
	int           rc;

	rank_list.rl_ranks = &rank_id;
	rank_list.rl_nr    = 1;
	memset(tgts, 0, sizeof(*tgts));

	rc = pool_map_find_targets_on_ranks(g_po_map, &rank_list, tgts);
	if (rc <= 0) {
		fprintf(stderr, "No targets found for rank %u (rc=%d)\n", rank, rc);
		return rc == 0 ? -DER_NONEXIST : rc;
	}
	return 0;
}

static int
build_node_tgt_list(uint32_t node_id, struct pool_target_id_list *tgts)
{
	struct pool_domain *node_dom;
	int                 i, j, rc;

	memset(tgts, 0, sizeof(*tgts));
	rc = pool_map_find_domain(g_po_map, PO_COMP_TP_NODE, node_id, &node_dom);
	if (rc != 1) {
		fprintf(stderr, "Node %u not found\n", node_id);
		return -DER_NONEXIST;
	}

	for (i = 0; i < node_dom->do_child_nr; i++) {
		struct pool_domain *rank_dom = &node_dom->do_children[i];

		for (j = 0; j < rank_dom->do_target_nr; j++) {
			struct pool_target_id id = {0};

			id.pti_id = rank_dom->do_targets[j].ta_comp.co_id;
			rc = pool_target_id_list_append(tgts, &id);
			if (rc != 0) {
				pool_target_id_list_free(tgts);
				return rc;
			}
		}
	}

	if (tgts->pti_number == 0) {
		fprintf(stderr, "No targets found for node %u\n", node_id);
		return -DER_NONEXIST;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* Pool-map update helper                                               */
/* ------------------------------------------------------------------ */

/*
 * Apply opc (a map_update_opc value) to every target in tgts via
 * ds_pool_map_tgts_update().  If opc_second != PLD_NO_OPC, apply it as a
 * second step afterwards (used for two-phase transitions such as DOWN →
 * DOWNOUT or UP → UPIN).
 */
static int
do_update(struct pool_target_id_list *tgts, int opc, int opc_second)
{
	uuid_t zero_uuid = {0};
	int    rc;

	rc = ds_pool_map_tgts_update(zero_uuid, g_po_map, tgts, opc,
				     false, NULL, true);
	if (rc != 0) {
		fprintf(stderr, "ds_pool_map_tgts_update opc=%d failed: %d\n",
			opc, rc);
		return rc;
	}
	if (opc_second != PLD_NO_OPC) {
		rc = ds_pool_map_tgts_update(zero_uuid, g_po_map, tgts, opc_second,
					     false, NULL, true);
		if (rc != 0) {
			fprintf(stderr,
				"ds_pool_map_tgts_update opc=%d failed: %d\n",
				opc_second, rc);
			return rc;
		}
	}
	return 0;
}

/* Human-readable names for layout_gen_mode values */
static const char *layout_gen_mode_names[] = {"pre_rebuild", "current", "post_rebuild"};

/* ------------------------------------------------------------------ */
/* Command implementations                                              */
/* ------------------------------------------------------------------ */

static void
cmd_obj_class(const char *arg)
{
	daos_oclass_id_t cid;
	char             name[64] = {0};
	char            *endp;

	if (arg == NULL || *arg == '\0') {
		fprintf(stderr, "Usage: obj_class <name|id>\n");
		return;
	}

	/* Try numeric input first */
	cid = (daos_oclass_id_t)strtoul(arg, &endp, 0);
	if (*endp != '\0') {
		/* Not a number – treat as class name */
		cid = daos_oclass_name2id(arg);
		if (cid == OC_UNKNOWN) {
			fprintf(stderr, "Unknown object class: %s\n", arg);
			return;
		}
	}

	g_obj_class = cid;
	daos_oclass_id2name(cid, name);
	printf("Object class set to: %s (id=%u)\n", name, (unsigned int)cid);
}

static void
cmd_gen_layout(const char *arg)
{
	daos_obj_id_t        oid  = {0};
	struct pl_obj_layout *layout = NULL;
	struct daos_obj_md    md  = {0};
	uint64_t              lo_val;
	char                  name[64] = {0};
	char                  arg_copy[1024];
	char                 *tok, *save;
	bool                  id_found = false;
	enum layout_gen_mode  mode = PRE_REBUILD;
	uint32_t              ver = 0;   /* 0 means "use current map version" */
	int                   grp, sz, index, rc;

	if (arg == NULL || *arg == '\0') {
		fprintf(stderr, "Usage: gen_layout id=<number> [mode=<pre_rebuild|current|post_rebuild>] [ver=<number>]\n");
		return;
	}

	if (g_pl_map == NULL) {
		fprintf(stderr, "Placement map unavailable\n");
		return;
	}

	/* Tokenise a copy of the argument string on whitespace */
	snprintf(arg_copy, sizeof(arg_copy), "%s", arg);
	for (tok = strtok_r(arg_copy, " \t", &save); tok != NULL;
	     tok = strtok_r(NULL, " \t", &save)) {
		if (strncmp(tok, "id=", 3) == 0) {
			char *endp;

			lo_val = strtoull(tok + 3, &endp, 0);
			if (*endp != '\0') {
				fprintf(stderr, "Invalid id value: %s\n", tok + 3);
				return;
			}
			id_found = true;
		} else if (strncmp(tok, "mode=", 5) == 0) {
			const char *val = tok + 5;
			char       *endp;
			long        num;

			/* Accept numeric values */
			num = strtol(val, &endp, 0);
			if (*endp == '\0') {
				if (num < PRE_REBUILD || num > POST_REBUILD) {
					fprintf(stderr,
						"Invalid mode value %ld; valid: 0 (pre_rebuild), 1 (current), 2 (post_rebuild)\n",
						num);
					return;
				}
				mode = (enum layout_gen_mode)num;
			} else if (strcasecmp(val, "pre_rebuild") == 0) {
				mode = PRE_REBUILD;
			} else if (strcasecmp(val, "current") == 0) {
				mode = CURRENT;
			} else if (strcasecmp(val, "post_rebuild") == 0) {
				mode = POST_REBUILD;
			} else {
				fprintf(stderr,
					"Unknown mode '%s'; valid: pre_rebuild, current, post_rebuild\n",
					val);
				return;
			}
		} else if (strncmp(tok, "ver=", 4) == 0) {
			char         *endp;
			unsigned long v;

			v = strtoul(tok + 4, &endp, 0);
			if (*endp != '\0' || v > UINT32_MAX) {
				fprintf(stderr, "Invalid ver value: %s\n", tok + 4);
				return;
			}
			ver = (uint32_t)v;
		} else {
			fprintf(stderr, "Unknown gen_layout option: '%s'\n", tok);
			fprintf(stderr, "Usage: gen_layout id=<number> [mode=<pre_rebuild|current|post_rebuild>] [ver=<number>]\n");
			return;
		}
	}

	if (!id_found) {
		fprintf(stderr, "Usage: gen_layout id=<number> [mode=<pre_rebuild|current|post_rebuild>] [ver=<number>]\n");
		return;
	}

	oid.lo = lo_val;
	oid.hi = 0;

	rc = daos_obj_set_oid_by_class(&oid, 0, g_obj_class, 0);
	if (rc != 0) {
		fprintf(stderr, "daos_obj_set_oid_by_class failed: %d\n", rc);
		return;
	}

	md.omd_id  = oid;
	md.omd_ver = (ver != 0) ? ver : pool_map_get_version(g_pl_map->pl_poolmap);
	md.omd_pda = 0;

	rc = pl_obj_place(g_pl_map, PLD_LAYOUT_VERSION, &md, (unsigned int)mode,
			  NULL, &layout);
	if (rc != 0) {
		fprintf(stderr, "pl_obj_place failed: %d\n", rc);
		return;
	}

	daos_oclass_id2name(g_obj_class, name);
	printf("Layout for OID lo=%" PRIu64 " class=%s (id=%u) mode=%s ver=%u:\n",
	       lo_val, name, (unsigned int)g_obj_class,
	       layout_gen_mode_names[mode], md.omd_ver);
	printf("  groups=%u  group_size=%u  total_shards=%u\n",
	       layout->ol_grp_nr, layout->ol_grp_size, layout->ol_nr);

	for (grp = 0; grp < layout->ol_grp_nr; ++grp) {
		printf("  [group %d]\n", grp);
		for (sz = 0; sz < layout->ol_grp_size; ++sz) {
			struct pl_obj_shard shard;

			index = grp * layout->ol_grp_size + sz;
			shard = layout->ol_shards[index];
			printf("    shard %2d: target_id=%4d  rank=%4u"
			       "  tgt_idx=%2u  fseq=%u%s\n",
			       shard.po_shard,
			       shard.po_target,
			       shard.po_rank,
			       shard.po_index,
			       shard.po_fseq,
			       shard.po_rebuilding ? "  [rebuilding]" : "");
		}
	}

	pl_obj_layout_free(layout);
}

/*
 * Parse "rank=<n>" or "node=<n>", build the target list, apply
 * opc (a map_update_opc value) to transition targets to the desired state,
 * then optionally apply opc_second for a two-phase transition (e.g. DOWN →
 * DOWNOUT or UP → UPIN).  Pass PLD_NO_OPC for opc_second to skip it.
 */
static void
cmd_set_state(const char *subcmd, const char *arg, int opc, int opc_second)
{
	struct pool_target_id_list tgts = {0};
	unsigned long              id;
	char                      *endp;
	int                        rc;

	if (arg == NULL) {
		fprintf(stderr, "Usage: %s rank=<n>|node=<n>\n", subcmd);
		return;
	}

	if (strncmp(arg, "rank=", 5) == 0) {
		id = strtoul(arg + 5, &endp, 0);
		if (*endp != '\0') {
			fprintf(stderr, "Invalid rank number: %s\n", arg + 5);
			return;
		}
		rc = build_rank_tgt_list((uint32_t)id, &tgts);
	} else if (strncmp(arg, "node=", 5) == 0) {
		id = strtoul(arg + 5, &endp, 0);
		if (*endp != '\0') {
			fprintf(stderr, "Invalid node number: %s\n", arg + 5);
			return;
		}
		rc = build_node_tgt_list((uint32_t)id, &tgts);
	} else {
		fprintf(stderr, "Usage: %s rank=<n>|node=<n>\n", subcmd);
		return;
	}

	if (rc != 0) {
		fprintf(stderr, "%s: failed to build target list: %d\n",
			subcmd, rc);
		return;
	}

	rc = do_update(&tgts, opc, opc_second);
	pool_target_id_list_free(&tgts);
	if (rc != 0) {
		fprintf(stderr, "%s: pool map update failed: %d\n", subcmd, rc);
		return;
	}

	refresh_pl_map();
	printf("Pool map updated (version=%u). Placement map refreshed.\n",
	       pool_map_get_version(g_po_map));
}

/* ------------------------------------------------------------------ */
/* REPL                                                                 */
/* ------------------------------------------------------------------ */

static void
run_repl(void)
{
	char  line[1024];
	char *cmd, *arg, *nl;

	printf("pl_debug interactive shell. Type 'help' for commands.\n");

	for (;;) {
		printf("pl_debug> ");
		fflush(stdout);

		if (fgets(line, sizeof(line), stdin) == NULL)
			break;

		/* Strip trailing newline */
		nl = strchr(line, '\n');
		if (nl != NULL)
			*nl = '\0';

		/* Skip leading whitespace */
		cmd = line;
		while (*cmd == ' ' || *cmd == '\t')
			cmd++;

		/* Skip blank lines and comments */
		if (*cmd == '\0' || *cmd == '#')
			continue;

		/* Split at the first whitespace boundary */
		arg = cmd;
		while (*arg != '\0' && *arg != ' ' && *arg != '\t')
			arg++;
		if (*arg != '\0') {
			*arg = '\0';
			arg++;
			while (*arg == ' ' || *arg == '\t')
				arg++;
		} else {
			arg = NULL;
		}

		if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
			break;
		} else if (strcmp(cmd, "help") == 0) {
			print_help();
		} else if (strcmp(cmd, "obj_class") == 0) {
			cmd_obj_class(arg);
		} else if (strcmp(cmd, "gen_layout") == 0) {
			cmd_gen_layout(arg);
		} else if (strcmp(cmd, "set_down") == 0) {
			/* UP/UPIN → DOWN */
			cmd_set_state("set_down", arg, MAP_EXCLUDE, PLD_NO_OPC);
		} else if (strcmp(cmd, "set_downout") == 0) {
			/* UP/UPIN → DOWN → DOWNOUT */
			cmd_set_state("set_downout", arg,
				      MAP_EXCLUDE, MAP_EXCLUDE_OUT);
		} else if (strcmp(cmd, "set_up") == 0) {
			/* DOWN/DOWNOUT → UP */
			cmd_set_state("set_up", arg, MAP_REINT, PLD_NO_OPC);
		} else if (strcmp(cmd, "set_upin") == 0) {
			/* DOWN/DOWNOUT → UP → UPIN */
			cmd_set_state("set_upin", arg, MAP_REINT, MAP_ADD_IN);
		} else {
			fprintf(stderr,
				"Unknown command: '%s'  (type 'help')\n", cmd);
		}
	}

	printf("Exiting pl_debug.\n");
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

static void
usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s -n <nodes> -r <ranks_per_node> -t <targets_per_rank>\n"
		"Options:\n"
		"  -n <number>  Number of nodes\n"
		"  -r <number>  Number of ranks per node\n"
		"  -t <number>  Number of targets per rank\n",
		prog);
}

int
main(int argc, char *argv[])
{
	int n = 0, r = 0, t = 0;
	int opt, rc;

	while ((opt = getopt(argc, argv, "n:r:t:h")) != -1) {
		switch (opt) {
		case 'n':
			n = atoi(optarg);
			break;
		case 'r':
			r = atoi(optarg);
			break;
		case 't':
			t = atoi(optarg);
			break;
		case 'h':
		default:
			usage(argv[0]);
			return 0;
		}
	}

	if (n <= 0 || r <= 0 || t <= 0) {
		fprintf(stderr,
			"Error: -n, -r, and -t must all be positive integers.\n");
		usage(argv[0]);
		return 1;
	}

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc != 0) {
		fprintf(stderr, "daos_debug_init failed: %d\n", rc);
		goto out;
	}

	rc = obj_class_init();
	if (rc != 0) {
		fprintf(stderr, "obj_class_init failed: %d\n", rc);
		goto out_debug;
	}

	rc = pl_init();
	if (rc != 0) {
		fprintf(stderr, "pl_init failed: %d\n", rc);
		goto out_obj_class;
	}

	/*
	 * Build pool map:
	 *   gen_pool_and_placement_map(num_pds, fdoms_per_pd,
	 *                              nodes_per_domain, vos_per_target, ...)
	 * With num_pds=1, fdoms_per_pd=n, nodes_per_domain=r,
	 * vos_per_target=t we get:
	 *   n nodes  ×  r ranks/node  ×  t targets/rank
	 * NODE is used as the fault domain.
	 */
	gen_pool_and_placement_map(1, n, r, t, PL_TYPE_JUMP_MAP,
				   PO_COMP_TP_NODE,
				   &g_po_map, &g_pl_map);
	if (g_po_map == NULL || g_pl_map == NULL) {
		fprintf(stderr, "Failed to create pool/placement map\n");
		rc = -DER_NOMEM;
		goto out_pl;
	}

	printf("Pool map: %d nodes × %d ranks/node × %d targets/rank"
	       " = %d total targets\n", n, r, t, n * r * t);
	printf("Fault domain: NODE  |  Map version: %u\n",
	       pool_map_get_version(g_po_map));

	run_repl();

	free_pool_and_placement_map(g_po_map, g_pl_map);

out_pl:
	pl_fini();
out_obj_class:
	obj_class_fini();
out_debug:
	daos_debug_fini();
out:
	return rc != 0 ? 1 : 0;
}
