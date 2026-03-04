/**
 * (C) Copyright 2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * pl_debug - Interactive placement debugging utility
 *
 * Usage: pl_debug -n <nodes> -r <ranks_per_node> -t <targets_per_rank>
 *                [-h|--help]
 *
 * Constructs an in-memory pool map and jump placement map from the given
 * topology parameters.  NODE is used as the fault domain.
 *
 * Interactive commands:
 *   obj_class <name|id>           - Set current object class
 *   print_obj_class <hint>         - Print object classes matching hint
 *                                     hint: all | EC | EC(k+p) | RP | RP_<r> | shard
 *   gen_oid id=<number> [class=<name|id>] [type=<EC_8P2|RP_3|...> grp=<number|X>]
 *                                 - Set current OID (required before gen_layout/diff_layout)
 *   gen_layout [mode=<pre_rebuild|current|post_rebuild>] [ver=<number>] [output=<file>]
 *                                 - Generate layout for current OID
 *   diff_layout [ver=<number>]    - Show rebuild shards for current OID
 *   set_down rank=<n>|node=<n>    - Set rank/node status to DOWN
 *   set_downout rank=<n>|node=<n> - Set rank/node status to DOWNOUT
 *   set_up rank=<n>|node=<n>      - Set rank/node status to UP
 *   set_upin rank=<n>|node=<n>    - Set rank/node status to UPIN
 *   query rank=<n>|node=<n>       - Print status of all sub-components and targets
 *   help                          - Show this help text
 *   quit / exit                   - Exit the tool
 *
 * Smoke-test example:
 *   $ pl_debug -n 4 -r 2 -t 8
 *   pl_debug> obj_class OC_EC_4P1GX
 *   pl_debug> gen_oid id=42
 *   pl_debug> gen_layout
 *   pl_debug> gen_layout mode=current ver=3
 *   pl_debug> set_down rank=0
 *   pl_debug> gen_layout mode=post_rebuild
 *   pl_debug> diff_layout
 *   pl_debug> quit
 */

#define D_LOGFAC DD_FAC(tests)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <time.h>

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

/* Buffer size for daos_oclass_names_list (covers all currently registered classes) */
#define PLD_OCLASS_LIST_BUF_SIZE (64 << 10)

/* ------------------------------------------------------------------ */
/* Global state                                                         */
/* ------------------------------------------------------------------ */

static struct pool_map   *g_po_map;
static struct pl_map     *g_pl_map;
static daos_oclass_id_t   g_obj_class = OC_UNKNOWN;

/* Set by gen_oid; required before gen_layout / diff_layout */
static daos_obj_id_t        g_oid       = {0};
static bool                 g_oid_set   = false;
static daos_oclass_id_t     g_oid_class = OC_UNKNOWN;

/* Last layout from gen_layout; overwritten on each gen_layout call */
static struct pl_obj_layout *g_layout   = NULL;

/* Forward declaration – defined after cmd_set_state */
static const char *fmt_comp_flags(uint32_t flags, char *buf, size_t bufsz);

/* ------------------------------------------------------------------ */
/* Help                                                                 */
/* ------------------------------------------------------------------ */

static void
print_help(void)
{
	printf("Commands:\n"
	       "  obj_class <name|id>           Set current object class\n"
	       "  print_obj_class <hint>        List classes matching hint\n"
	       "                                hint: all | EC | EC(k+p) | RP | RP_<r> | shard\n"
	       "                                  EC: all EC classes; EC(8+2): 8+2 EC classes\n"
	       "                                  RP: all RP classes; RP_3: 3-way RP classes\n"
	       "                                  shard: S1/S2/.../SX classes\n"
	       "                                  all: every registered class\n"
	       "  gen_oid id=<number> [class=<name|id>] [type=<EC_8P2|RP_3|...> grp=<number|X>]\n"
	       "                                Set current OID (required before gen_layout/diff_layout)\n"
	       "                                class: object class name or numeric id\n"
	       "                                  e.g. class=OC_EC_4P1GX  or  class=256\n"
	       "                                type+grp: alternative way to specify class\n"
	       "                                  e.g. type=EC_8P2 grp=2  -> EC_8P2G2\n"
	       "                                       type=RP_3 grp=X    -> RP_3GX\n"
	       "  gen_layout [mode=<m>] [ver=<number>] [output=<file>]\n"
	       "                                Generate layout for current OID\n"
	       "                                mode: pre_rebuild|0, current|1, post_rebuild|2\n"
	       "                                      (default: pre_rebuild)\n"
	       "                                ver:  pool map version (default: latest)\n"
	       "                                output: write layout to <file> instead of stdout\n"
	       "                                Result is stored; overwritten on each call\n"
	       "  diff_layout [ver=<number>]    Show shards that need rebuild (pl_obj_find_rebuild)\n"
	       "                                ver: pool map version (default: latest)\n"
	       "                                Outputs shard ID, target ID, rank, status,\n"
	       "                                and DOWN2UP flag for each rebuild shard\n"
	       "  set_down rank=<n>|node=<n>    Set rank/node to DOWN\n"
	       "  set_downout rank=<n>|node=<n> Set rank/node to DOWNOUT\n"
	       "  set_up rank=<n>|node=<n>      Set rank/node to UP\n"
	       "  set_upin rank=<n>|node=<n>    Set rank/node to UPIN\n"
	       "  query rank=<n>|node=<n>       Print status of all sub-components and targets\n"
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
				     true, NULL, true);
	if (rc != 0) {
		fprintf(stderr, "ds_pool_map_tgts_update opc=%d failed: %d\n",
			opc, rc);
		return rc;
	}
	if (opc_second != PLD_NO_OPC) {
		rc = ds_pool_map_tgts_update(zero_uuid, g_po_map, tgts, opc_second,
					     true, NULL, true);
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

/*
 * Returns true if 'oc_name' is a single-replica shard class (S1, S2, ... SX).
 * These names have the form: 'S' followed by a decimal digit or 'X'.
 */
static bool
is_shard_oclass(const char *oc_name)
{
	return oc_name[0] == 'S' && oc_name[1] != '\0' &&
	       (oc_name[1] == 'X' || (oc_name[1] >= '0' && oc_name[1] <= '9'));
}

/*
 * print_obj_class <hint>
 *
 * Print the object classes that match the given hint.  The hint is
 * mandatory.  Supported hints:
 *
 *   all       - every registered class
 *   EC        - all Erasure Coding classes (EC_*)
 *   EC(k+p)   - EC classes with k data + p parity cells (e.g. EC(8+2))
 *   RP        - all Replication classes (RP_*)
 *   RP_<r>    - replication classes with r replicas (e.g. RP_3)
 *   shard     - single-replica shard classes (S1, S2, ... SX)
 */
static void
cmd_print_obj_class(const char *arg)
{
	char             prefix[64];
	char            *buf;
	char            *save;
	char            *tok;
	long             k, p, rval;
	char            *endp;
	bool             ec_specific = false;
	bool             rp_specific = false;
	daos_oclass_id_t cid;

	if (arg == NULL || *arg == '\0') {
		fprintf(stderr,
			"Usage: print_obj_class <hint>\n"
			"  hint: all | EC | EC(k+p) | RP | RP_<r> | shard\n"
			"  Examples:\n"
			"    print_obj_class EC         all EC classes\n"
			"    print_obj_class EC(8+2)    8+2 EC classes (EC_8P2*)\n"
			"    print_obj_class RP         all replication classes\n"
			"    print_obj_class RP_3       3-way replication classes\n"
			"    print_obj_class shard      single-replica S1/S2/.../SX\n"
			"    print_obj_class all        every registered class\n");
		return;
	}

	/* Pre-parse the hint and build a prefix string for specific filters */
	prefix[0] = '\0';
	if (strcasecmp(arg, "all") == 0 || strcasecmp(arg, "EC") == 0 ||
	    strcasecmp(arg, "RP") == 0 || strcasecmp(arg, "shard") == 0) {
		/* nothing to pre-parse */
	} else if (strncasecmp(arg, "EC(", 3) == 0) {
		const char *hp = arg + 3;

		k = strtol(hp, &endp, 10);
		if (endp == hp || *endp != '+')
			goto bad_hint;
		hp = endp + 1;
		p = strtol(hp, &endp, 10);
		if (endp == hp || *endp != ')' || endp[1] != '\0')
			goto bad_hint;
		if (k <= 0 || p <= 0)
			goto bad_hint;
		snprintf(prefix, sizeof(prefix), "EC_%ldP%ldG", k, p);
		ec_specific = true;
	} else if (strncasecmp(arg, "RP_", 3) == 0) {
		const char *hp = arg + 3;

		rval = strtol(hp, &endp, 10);
		if (endp == hp || *endp != '\0' || rval <= 0)
			goto bad_hint;
		snprintf(prefix, sizeof(prefix), "RP_%ldG", rval);
		rp_specific = true;
	} else {
		goto bad_hint;
	}

	buf = malloc(PLD_OCLASS_LIST_BUF_SIZE);
	if (buf == NULL) {
		fprintf(stderr, "Out of memory\n");
		return;
	}

	if (daos_oclass_names_list(PLD_OCLASS_LIST_BUF_SIZE, buf) < 0) {
		fprintf(stderr, "daos_oclass_names_list failed\n");
		free(buf);
		return;
	}

	printf("Object classes matching '%s':\n", arg);
	for (tok = strtok_r(buf, ", ", &save); tok != NULL;
	     tok = strtok_r(NULL, ", ", &save)) {
		bool match;

		if (strcasecmp(arg, "all") == 0) {
			match = true;
		} else if (strcasecmp(arg, "EC") == 0) {
			match = strncmp(tok, "EC_", 3) == 0;
		} else if (ec_specific) {
			match = strncmp(tok, prefix, strlen(prefix)) == 0;
		} else if (strcasecmp(arg, "RP") == 0) {
			match = strncmp(tok, "RP_", 3) == 0;
		} else if (rp_specific) {
			match = strncmp(tok, prefix, strlen(prefix)) == 0;
		} else {
			/* shard */
			match = is_shard_oclass(tok);
		}

		if (match) {
			cid = daos_oclass_name2id(tok);
			printf("  %-24s (id=%u)\n", tok, (unsigned int)cid);
		}
	}

	free(buf);
	return;

bad_hint:
	fprintf(stderr,
		"Unknown or invalid hint '%s'\n"
		"  Valid hints: all | EC | EC(k+p) | RP | RP_<r> | shard\n"
		"  Examples: EC, EC(8+2), RP, RP_3, shard, all\n", arg);
}

/*
 * gen_oid id=<number> [class=<name|id>] [type=<...> grp=<...>]
 *
 * Sets the current OID (g_oid / g_oid_class) that gen_layout and diff_layout
 * will operate on.  The object class is resolved in priority order:
 *   1. class=<name|id>        (direct class name or numeric id)
 *   2. type=<...> grp=<...>   (composite name, e.g. EC_8P2G2)
 *   3. g_obj_class            (set by the obj_class command)
 */
static void
cmd_gen_oid(const char *arg)
{
	daos_obj_id_t    oid      = {0};
	uint64_t         lo_val;
	char             arg_copy[1024];
	char            *tok, *save;
	bool             id_found = false;
	char             class_str[64] = {0}; /* class= value, e.g. "OC_EC_4P1GX" or "256" */
	char             type_str[64] = {0};
	char             grp_str[16]  = {0};
	daos_oclass_id_t use_class;
	char             name[64] = {0};
	char            *endp;
	unsigned long    num;
	int              rc;

#define GEN_OID_USAGE \
	"Usage: gen_oid id=<number> [class=<name|id>]\n" \
	"               [type=<EC_8P2|RP_3|...> grp=<number|X>]\n"

	if (arg == NULL || *arg == '\0') {
		fprintf(stderr, GEN_OID_USAGE);
		return;
	}

	snprintf(arg_copy, sizeof(arg_copy), "%s", arg);
	for (tok = strtok_r(arg_copy, " \t", &save); tok != NULL;
	     tok = strtok_r(NULL, " \t", &save)) {
		if (strncmp(tok, "id=", 3) == 0) {
			lo_val = strtoull(tok + 3, &endp, 0);
			if (*endp != '\0') {
				fprintf(stderr, "Invalid id value: %s\n",
					tok + 3);
				return;
			}
			id_found = true;
		} else if (strncmp(tok, "class=", 6) == 0) {
			const char *val = tok + 6;

			if (strlen(val) >= sizeof(class_str)) {
				fprintf(stderr, "class= value too long: %s\n",
					val);
				return;
			}
			snprintf(class_str, sizeof(class_str), "%s", val);
		} else if (strncmp(tok, "type=", 5) == 0) {
			const char *val = tok + 5;

			if (strlen(val) >= sizeof(type_str)) {
				fprintf(stderr, "type= value too long: %s\n",
					val);
				return;
			}
			snprintf(type_str, sizeof(type_str), "%s", val);
		} else if (strncmp(tok, "grp=", 4) == 0) {
			const char *val = tok + 4;

			if (strcasecmp(val, "X") == 0) {
				snprintf(grp_str, sizeof(grp_str), "X");
			} else {
				long v = strtol(val, &endp, 10);

				if (*endp != '\0' || v <= 0) {
					fprintf(stderr,
						"Invalid grp value '%s'; expected a positive number or X\n",
						val);
					return;
				}
				snprintf(grp_str, sizeof(grp_str), "%u",
					 (unsigned int)v);
			}
		} else {
			fprintf(stderr, "Unknown gen_oid option: '%s'\n", tok);
			fprintf(stderr, GEN_OID_USAGE);
			return;
		}
	}

	if (!id_found) {
		fprintf(stderr, GEN_OID_USAGE);
		return;
	}

	/* Resolve object class: class= > type=+grp= > g_obj_class */
	if (class_str[0] != '\0') {
		/* class= is mutually exclusive with type=/grp= */
		if (type_str[0] != '\0' || grp_str[0] != '\0') {
			fprintf(stderr,
				"class= and type=/grp= are mutually exclusive\n");
			return;
		}
		/* Accept numeric id or class name */
		errno = 0;
		num = strtoul(class_str, &endp, 0);

		if (*endp == '\0') {
			if (errno == ERANGE || num > UINT32_MAX) {
				fprintf(stderr,
					"class= numeric value out of range: %s\n",
					class_str);
				return;
			}
			use_class = (daos_oclass_id_t)num;
		} else {
			use_class = daos_oclass_name2id(class_str);
			if (use_class == OC_UNKNOWN) {
				fprintf(stderr,
					"Unknown object class '%s'\n"
					"  Use 'print_obj_class all' to list valid classes\n",
					class_str);
				return;
			}
		}
	} else if (type_str[0] != '\0' && grp_str[0] != '\0') {
		char class_name[sizeof(type_str) + sizeof(grp_str) + 2]; /* 'G' + NUL */

		snprintf(class_name, sizeof(class_name), "%sG%s", type_str,
			 grp_str);
		use_class = daos_oclass_name2id(class_name);
		if (use_class == OC_UNKNOWN) {
			fprintf(stderr,
				"Unknown object class '%s' (from type=%s grp=%s)\n"
				"  Use 'print_obj_class EC' or 'print_obj_class RP' to list valid classes\n",
				class_name, type_str, grp_str);
			return;
		}
	} else if (type_str[0] != '\0' || grp_str[0] != '\0') {
		fprintf(stderr,
			"Both type= and grp= must be specified together\n"
			"  e.g. type=EC_8P2 grp=2  or  type=RP_3 grp=X\n");
		return;
	} else {
		if (g_obj_class == OC_UNKNOWN) {
			fprintf(stderr,
				"No object class set; run 'obj_class <name|id>' first\n"
				"  (or use class=<name|id>, or type= and grp= to specify)\n");
			return;
		}
		use_class = g_obj_class;
	}

	oid.lo = lo_val;
	oid.hi = 0;
	rc = daos_obj_set_oid_by_class(&oid, 0, use_class, 0);
	if (rc != 0) {
		fprintf(stderr, "daos_obj_set_oid_by_class failed: %d\n", rc);
		return;
	}

	g_oid       = oid;
	g_oid_class = use_class;
	g_oid_set   = true;

	daos_oclass_id2name(use_class, name);
	printf("OID set: lo=%" PRIu64 " hi=0x%" PRIx64 " class=%s (id=%u)\n",
	       lo_val, oid.hi, name, (unsigned int)use_class);
#undef GEN_OID_USAGE
}

/*
 * gen_layout [mode=<pre_rebuild|current|post_rebuild>] [ver=<number>] [output=<file>]
 *
 * Generates the placement layout for the current OID (set by gen_oid).
 * The result is stored in g_layout, overwriting any previous layout.
 * If output=<file> is given, the layout is written to that file instead
 * of stdout.
 */
static void
cmd_gen_layout(const char *arg)
{
	struct pl_obj_layout *layout = NULL;
	struct daos_obj_md    md     = {0};
	char                  name[64] = {0};
	char                  arg_copy[1024];
	char                 *tok, *save;
	enum layout_gen_mode  mode = PRE_REBUILD;
	uint32_t              ver  = 0; /* 0 → use latest map version */
	const char           *out_path = NULL;
	FILE                 *out = stdout;
	int                   grp, sz, index, rc;
	struct timespec       ts_start, ts_end;

#define GEN_LAYOUT_USAGE \
	"Usage: gen_layout [mode=<pre_rebuild|current|post_rebuild>] [ver=<number>] [output=<file>]\n"

	if (g_pl_map == NULL) {
		fprintf(stderr, "Placement map unavailable\n");
		return;
	}

	if (!g_oid_set) {
		fprintf(stderr,
			"No OID set; run 'gen_oid id=<number>' first\n");
		return;
	}

	if (arg != NULL && *arg != '\0') {
		snprintf(arg_copy, sizeof(arg_copy), "%s", arg);
		for (tok = strtok_r(arg_copy, " \t", &save); tok != NULL;
		     tok = strtok_r(NULL, " \t", &save)) {
			if (strncmp(tok, "mode=", 5) == 0) {
				const char *val = tok + 5;
				char       *endp;
				long        num;

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
					fprintf(stderr,
						"Invalid ver value: %s\n",
						tok + 4);
					return;
				}
				ver = (uint32_t)v;
			} else if (strncmp(tok, "output=", 7) == 0) {
				out_path = tok + 7;
			} else {
				fprintf(stderr,
					"Unknown gen_layout option: '%s'\n",
					tok);
				fprintf(stderr, GEN_LAYOUT_USAGE);
				return;
			}
		}
	}

	if (out_path != NULL) {
		out = fopen(out_path, "w");
		if (out == NULL) {
			fprintf(stderr, "Cannot open output file '%s': %s\n",
				out_path, strerror(errno));
			return;
		}
	}

	md.omd_id  = g_oid;
	md.omd_ver = (ver != 0) ? ver : pool_map_get_version(g_pl_map->pl_poolmap);
	md.omd_pda = 0;

	clock_gettime(CLOCK_MONOTONIC, &ts_start);
	rc = pl_obj_place(g_pl_map, PLD_LAYOUT_VERSION, &md,
			  (mode == PRE_REBUILD) ? DAOS_OO_RO : DAOS_OO_RW,
			  NULL, &layout);
	clock_gettime(CLOCK_MONOTONIC, &ts_end);
	if (rc != 0) {
		fprintf(stderr, "pl_obj_place failed: %d\n", rc);
		if (out != stdout)
			fclose(out);
		return;
	}

	/* Overwrite previous layout */
	if (g_layout != NULL)
		pl_obj_layout_free(g_layout);
	g_layout = layout;

	daos_oclass_id2name(g_oid_class, name);
	fprintf(out, "Layout for OID lo=%" PRIu64 " class=%s (id=%u) mode=%s ver=%u:\n",
		g_oid.lo, name, (unsigned int)g_oid_class,
		layout_gen_mode_names[mode], md.omd_ver);
	fprintf(out, "  groups=%u  group_size=%u  total_shards=%u\n",
		layout->ol_grp_nr, layout->ol_grp_size, layout->ol_nr);

	for (grp = 0; grp < layout->ol_grp_nr; ++grp) {
		fprintf(out, "  [group %d]\n", grp);
		for (sz = 0; sz < layout->ol_grp_size; ++sz) {
			struct pl_obj_shard shard;

			index = grp * layout->ol_grp_size + sz;
			shard = layout->ol_shards[index];
			fprintf(out, "    shard %2d: target_id=%4d  rank=%4u"
				"  tgt_idx=%2u  fseq=%u%s\n",
				shard.po_shard,
				shard.po_target,
				shard.po_rank,
				shard.po_index,
				shard.po_fseq,
				shard.po_rebuilding ? "  [rebuilding]" : "");
		}
	}
	fprintf(out, "  pl_obj_place time: %lld us\n",
		((long long)(ts_end.tv_sec - ts_start.tv_sec) * 1000000000LL +
		 (ts_end.tv_nsec - ts_start.tv_nsec)) / 1000LL);
	if (out != stdout) {
		if (fclose(out) != 0)
			fprintf(stderr, "Error closing output file '%s': %s\n",
				out_path, strerror(errno));
		else
			printf("Layout written to '%s'\n", out_path);
	}
#undef GEN_LAYOUT_USAGE
}

/*
 * diff_layout [ver=<number>]
 *
 * Calls pl_obj_find_rebuild() for the current OID (set by gen_oid) and
 * prints each rebuild shard together with the target's status and flags.
 */
static void
cmd_diff_layout(const char *arg)
{
	struct daos_obj_md    md      = {0};
	char                  arg_copy[1024];
	char                 *tok, *save;
	uint32_t              ver     = 0; /* 0 → use latest map version */
	uint32_t             *tgt_ids  = NULL;
	uint32_t             *shard_ids = NULL;
	int                   ntgt, i, rc;
	unsigned int          array_size;
	char                  name[64] = {0};

#define DIFF_LAYOUT_USAGE \
	"Usage: diff_layout [ver=<number>]\n"

	if (g_pl_map == NULL) {
		fprintf(stderr, "Placement map unavailable\n");
		return;
	}

	if (!g_oid_set) {
		fprintf(stderr,
			"No OID set; run 'gen_oid id=<number>' first\n");
		return;
	}

	if (arg != NULL && *arg != '\0') {
		snprintf(arg_copy, sizeof(arg_copy), "%s", arg);
		for (tok = strtok_r(arg_copy, " \t", &save); tok != NULL;
		     tok = strtok_r(NULL, " \t", &save)) {
			if (strncmp(tok, "ver=", 4) == 0) {
				char         *endp;
				unsigned long v;

				v = strtoul(tok + 4, &endp, 0);
				if (*endp != '\0' || v > UINT32_MAX) {
					fprintf(stderr,
						"Invalid ver value: %s\n",
						tok + 4);
					return;
				}
				ver = (uint32_t)v;
			} else {
				fprintf(stderr,
					"Unknown diff_layout option: '%s'\n",
					tok);
				fprintf(stderr, DIFF_LAYOUT_USAGE);
				return;
			}
		}
	}

	md.omd_id  = g_oid;
	md.omd_ver = (ver != 0) ? ver : pool_map_get_version(g_pl_map->pl_poolmap);
	md.omd_pda = 0;

	/* Allocate output arrays sized to the total number of targets */
	array_size = pool_map_target_nr(g_po_map);
	D_ALLOC_ARRAY(tgt_ids, array_size);
	D_ALLOC_ARRAY(shard_ids, array_size);
	if (tgt_ids == NULL || shard_ids == NULL) {
		fprintf(stderr, "Out of memory\n");
		D_FREE(tgt_ids);
		D_FREE(shard_ids);
		return;
	}

	ntgt = pl_obj_find_rebuild(g_pl_map, PLD_LAYOUT_VERSION, &md, NULL,
				   md.omd_ver, tgt_ids, shard_ids, array_size);
	if (ntgt < 0) {
		fprintf(stderr, "pl_obj_find_rebuild failed: %d\n", ntgt);
		D_FREE(tgt_ids);
		D_FREE(shard_ids);
		return;
	}

	daos_oclass_id2name(g_oid_class, name);
	printf("Rebuild shards for OID lo=%" PRIu64 " class=%s ver=%u: %d shard(s)\n",
	       g_oid.lo, name, md.omd_ver, ntgt);

	for (i = 0; i < ntgt; i++) {
		struct pool_target *tgt  = NULL;
		char                fbuf[64];

		rc = pool_map_find_target(g_po_map, tgt_ids[i], &tgt);
		if (rc != 1 || tgt == NULL) {
			printf("  shard %3u: target_id=%4u  (target not found)\n",
			       shard_ids[i], tgt_ids[i]);
			continue;
		}

		printf("  shard %3u: target_id=%4u  rank=%4u  idx=%2u"
		       "  status=%-8s",
		       shard_ids[i],
		       tgt->ta_comp.co_id,
		       tgt->ta_comp.co_rank,
		       tgt->ta_comp.co_index,
		       pool_comp_state2str(tgt->ta_comp.co_status));

		if (tgt->ta_comp.co_flags != 0)
			printf("  flags=%s",
			       fmt_comp_flags(tgt->ta_comp.co_flags, fbuf,
					      sizeof(fbuf)));
		printf("\n");
	}

	D_FREE(tgt_ids);
	D_FREE(shard_ids);
#undef DIFF_LAYOUT_USAGE
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

/*
 * Build a human-readable string for pool_component_flags.
 * Returns buf (always NUL-terminated, empty string when flags==0).
 */
static const char *
fmt_comp_flags(uint32_t flags, char *buf, size_t bufsz)
{
	const uint32_t known = PO_COMPF_DOWN2UP | PO_COMPF_DOWN2OUT |
			       PO_COMPF_CHK_DONE;
	int            off   = 0;
	int            n;

	buf[0] = '\0';
	if (flags == 0)
		return buf;

#define APPEND(fmt, ...)                                                         \
	do {                                                                     \
		n = snprintf(buf + off, bufsz - off, fmt, ##__VA_ARGS__);        \
		if (n > 0)                                                       \
			off = (off + n < (int)bufsz) ? off + n : (int)bufsz - 1; \
	} while (0)

	if (flags & PO_COMPF_DOWN2UP)
		APPEND("%sDOWN2UP", off ? "|" : "");
	if (flags & PO_COMPF_DOWN2OUT)
		APPEND("%sDOWN2OUT", off ? "|" : "");
	if (flags & PO_COMPF_CHK_DONE)
		APPEND("%sCHK_DONE", off ? "|" : "");
	if (flags & ~known)
		APPEND("%s0x%x", off ? "|" : "", flags & ~known);

#undef APPEND
	return buf;
}

/*
 * Print the status of the specified rank domain and all of its targets.
 */
static void
print_rank_status(const struct pool_domain *rank_dom)
{
	const struct pool_component *rc = &rank_dom->do_comp;
	char                         fbuf[64];
	int                          j;

	printf("  rank %u  id=%u  status=%-8s  ver=%u  targets=%u",
	       rc->co_rank, rc->co_id,
	       pool_comp_state2str(rc->co_status),
	       rc->co_ver, rank_dom->do_target_nr);
	if (rc->co_flags != 0)
		printf("  flags=%s", fmt_comp_flags(rc->co_flags, fbuf, sizeof(fbuf)));
	printf("\n");

	for (j = 0; j < (int)rank_dom->do_target_nr; j++) {
		const struct pool_component *tc = &rank_dom->do_targets[j].ta_comp;

		printf("    target[%2d]  id=%4u  idx=%2u  status=%-8s  ver=%u  fseq=%u",
		       j, tc->co_id, tc->co_index,
		       pool_comp_state2str(tc->co_status),
		       tc->co_ver, tc->co_fseq);
		if (tc->co_flags != 0)
			printf("  flags=%s",
			       fmt_comp_flags(tc->co_flags, fbuf, sizeof(fbuf)));
		printf("\n");
	}
}

/*
 * query rank=<n>  - print the rank's status and all its targets.
 * query node=<n>  - print the node's status, then each rank and its targets.
 */
static void
cmd_query(const char *arg)
{
	unsigned long id;
	char         *endp;

	if (arg == NULL) {
		fprintf(stderr, "Usage: query rank=<n>|node=<n>\n");
		return;
	}

	if (strncmp(arg, "rank=", 5) == 0) {
		struct pool_domain *rank_dom;

		id = strtoul(arg + 5, &endp, 0);
		if (*endp != '\0') {
			fprintf(stderr, "Invalid rank number: %s\n", arg + 5);
			return;
		}
		rank_dom = pool_map_find_dom_by_rank(g_po_map, (uint32_t)id);
		if (rank_dom == NULL) {
			fprintf(stderr, "Rank %lu not found\n", id);
			return;
		}
		printf("Pool map version: %u\n",
		       pool_map_get_version(g_po_map));
		print_rank_status(rank_dom);
	} else if (strncmp(arg, "node=", 5) == 0) {
		struct pool_domain *node_dom;
		int                 i, rc;

		id = strtoul(arg + 5, &endp, 0);
		if (*endp != '\0') {
			fprintf(stderr, "Invalid node number: %s\n", arg + 5);
			return;
		}
		rc = pool_map_find_domain(g_po_map, PO_COMP_TP_NODE,
					  (uint32_t)id, &node_dom);
		if (rc != 1) {
			fprintf(stderr, "Node %lu not found\n", id);
			return;
		}
		printf("Pool map version: %u\n",
		       pool_map_get_version(g_po_map));
		{
			const struct pool_component *nc = &node_dom->do_comp;
			char                         fbuf[64];

			printf("node %u  id=%u  status=%-8s  ver=%u  ranks=%u",
			       (unsigned int)id, nc->co_id,
			       pool_comp_state2str(nc->co_status),
			       nc->co_ver, node_dom->do_child_nr);
			if (nc->co_flags != 0)
				printf("  flags=%s",
				       fmt_comp_flags(nc->co_flags, fbuf,
						      sizeof(fbuf)));
			printf("\n");
		}

		for (i = 0; i < (int)node_dom->do_child_nr; i++)
			print_rank_status(&node_dom->do_children[i]);
	} else {
		fprintf(stderr, "Usage: query rank=<n>|node=<n>\n");
	}
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
		} else if (strcmp(cmd, "print_obj_class") == 0) {
			cmd_print_obj_class(arg);
		} else if (strcmp(cmd, "gen_oid") == 0) {
			cmd_gen_oid(arg);
		} else if (strcmp(cmd, "gen_layout") == 0) {
			cmd_gen_layout(arg);
		} else if (strcmp(cmd, "diff_layout") == 0) {
			cmd_diff_layout(arg);
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
		} else if (strcmp(cmd, "query") == 0) {
			cmd_query(arg);
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
		"  -t <number>  Number of targets per rank\n"
		"  -h, --help   Show this help text\n",
		prog);
}

int
main(int argc, char *argv[])
{
	int n = 0, r = 0, t = 0;
	int opt, rc;

	static struct option long_opts[] = {
		{"help", no_argument, NULL, 'h'},
		{NULL,   0,           NULL,  0 }
	};

	while ((opt = getopt_long(argc, argv, "n:r:t:h", long_opts, NULL)) != -1) {
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
