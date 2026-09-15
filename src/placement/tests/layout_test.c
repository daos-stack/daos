/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <getopt.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "place_obj_common.h"
#include "layout_test_helpers.h"

static void
print_usage(const char *prog)
{
	printf("Usage: %s [options]\n", prog);
	printf("\nOptions:\n");
	printf("  -n, --nodes N         Number of nodes (default: %d)\n", DEFAULT_NODES);
	printf("  -r, --ranks N         Ranks per node (default: %d)\n", DEFAULT_RANKS_PER_NODE);
	printf("  -t, --targets N       Targets per rank (default: %d)\n", DEFAULT_TGTS_PER_RANK);
	printf("  -c, --class CLASS     Object class (default: %s)\n", DEFAULT_OBJ_CLASS);
	printf("  -N, --obj-count N     Number of OIDs (default: %d)\n", DEFAULT_OBJ_COUNT);
	printf("  -o, --operations STR  Comma-separated list of operations\n");
	printf("                        Format: <type> <mode>=[<id,...>]\n");
	printf("                        Types : exclude, reint, drain, add\n");
	printf("                        Modes : rank, node, target\n");
	printf("                        IDs can be single values or ranges (e.g., 0-3, 5, 7-8)\n");
	printf("                        Example: \"exclude rank=[0-3, 5, 7-8], reint node=[2]\"\n");
	printf("  -p, --print-memory    Print memory statistics (disabled by default)\n");
	printf("  -h, --help            Print this help message\n");
}

static int
parse_int_arg(const char *name, const char *value, int64_t *out)
{
	char     *end = NULL;
	long long parsed;

	errno  = 0;
	parsed = strtoll(value, &end, 10);
	if (errno != 0 || end == value || *end != '\0') {
		fprintf(stderr, "ERROR: %s must be an integer, got \"%s\"\n", name, value);
		*out = -1;
		return -DER_INVAL;
	}

	*out = parsed;
	return 0;
}

static int
get_oclass_ft(daos_oclass_id_t oclass)
{
	struct daos_oclass_attr *attr;
	attr = daos_oclass_id2attr(oclass, 0);
	switch (attr->ca_resil) {
	case DAOS_RES_REPL:
		return attr->u.rp.r_num - 1;
	case DAOS_RES_EC:
		return attr->u.ec.e_p;
	default:
		return 0;
	}
}

static void
format_memory_value(uint64_t bytes, double *value, const char **unit)
{
	const uint64_t kb = 1024ULL;
	const uint64_t mb = kb * kb;
	const uint64_t gb = mb * kb;

	if (bytes >= gb) {
		*value = (double)bytes / (double)gb;
		*unit  = "GB";
		return;
	}

	if (bytes >= mb) {
		*value = (double)bytes / (double)mb;
		*unit  = "MB";
		return;
	}

	*value = (double)bytes;
	*unit  = "bytes";
}

static void
print_memory_line(const char *label, uint64_t bytes)
{
	double      value;
	const char *unit;

	format_memory_value(bytes, &value, &unit);
	if (strcmp(unit, "bytes") == 0)
		printf("    %-20s: %llu bytes\n", label, (unsigned long long)bytes);
	else
		printf("    %-20s: %.2f %s\n", label, value, unit);
}

static void
print_time_line(const char *label, double ms)
{
	if (ms >= 3600000.0)
		printf("    %-28s: %.3f h\n", label, ms / 3600000.0);
	else if (ms >= 60000.0)
		printf("    %-28s: %.3f min\n", label, ms / 60000.0);
	else if (ms >= 1000.0)
		printf("    %-28s: %.3f s\n", label, ms / 1000.0);
	else
		printf("    %-28s: %.3f ms\n", label, ms);
}

static void
print_ops_per_sec_line(const char *label, int64_t num_oids, double ms)
{
	double ops_per_sec;

	if (ms <= 0.0)
		return;

	ops_per_sec = (num_oids * 1000.0) / ms;
	printf("    %-28s: %.3f ops/s\n", label, ops_per_sec);
}

/*
 * Parse a single argument which can be either:
 * - An integer: "5"
 * - A range: "0-3" (expands to 0, 1, 2, 3)
 *
 * Returns the number of values added to the args array (1 for single int,
 * or range_end - range_start + 1 for ranges), or -1 on error.
 */
static int
parse_arg_or_range(const char *arg, int *args, int *nr_args, int max_args)
{
	char  buf[64];
	char *end = NULL;
	char *hyphen;
	long  start, end_val;
	int   count = 0;

	strncpy(buf, arg, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	/* Check if this is a range (contains hyphen) */
	hyphen = strchr(buf, '-');

	if (hyphen != NULL) {
		/* Parse as range: "start-end" */
		*hyphen               = '\0';
		const char *start_str = buf;
		const char *end_str   = hyphen + 1;

		/* Skip leading spaces in start_str */
		while (*start_str == ' ')
			start_str++;

		errno = 0;
		start = strtol(start_str, &end, 10);
		if (errno != 0 || end == start_str || (*end != '\0' && *end != ' ') || start < 0 ||
		    start > INT_MAX) {
			fprintf(stderr, "ERROR: invalid range start \"%s\"\n", start_str);
			return -1;
		}

		/* Skip leading spaces in end_str */
		while (*end_str == ' ')
			end_str++;

		errno   = 0;
		end_val = strtol(end_str, &end, 10);
		if (errno != 0 || end == end_str || (*end != '\0' && *end != ' ') || end_val < 0 ||
		    end_val > INT_MAX) {
			fprintf(stderr, "ERROR: invalid range end \"%s\"\n", end_str);
			return -1;
		}

		if (start > end_val) {
			fprintf(stderr, "ERROR: invalid range %ld-%ld (start > end)\n", start,
				end_val);
			return -1;
		}

		/* Expand range and add to args */
		for (long i = start; i <= end_val; i++) {
			if (*nr_args >= max_args) {
				fprintf(stderr, "ERROR: too many arguments in operation (max %d)\n",
					max_args);
				return -1;
			}
			args[(*nr_args)++] = (int)i;
			count++;
		}
	} else {
		/* Parse as single integer */
		errno = 0;
		start = strtol(buf, &end, 10);
		if (errno != 0 || end == buf || (*end != '\0' && *end != ' ') || start < INT_MIN ||
		    start > INT_MAX) {
			fprintf(stderr,
				"ERROR: invalid argument \"%s\"; must be an integer or range\n",
				arg);
			return -1;
		}

		if (*nr_args >= max_args) {
			fprintf(stderr, "ERROR: too many arguments in operation (max %d)\n",
				max_args);
			return -1;
		}
		args[(*nr_args)++] = (int)start;
		count              = 1;
	}

	return count;
}

/*
 * Parse a comma-separated list of operations from a string of the form
 * "<type> <mode>=[<id,...>], ...".  Each token is split on commas that are
 * not inside square brackets so that multi-value arguments such as
 * node=[1,2] are kept intact.  Supports both single integers and ranges
 * (e.g., "0-3" expands to 0, 1, 2, 3).  The results are written into ops[]
 * and op_count is set to the number of operations parsed.
 *
 * Returns 0 on success, -1 on parse error.
 */
static int
parse_operations(char *input, struct operation ops[], int *op_count)
{
	char *pos = input;
	*op_count = 0;

	while (pos != NULL && *pos != '\0') {
		char              type_str[32];
		char              mode_str[32];
		char              arg_str[128];
		char             *arg;
		char             *arg_saveptr = NULL;
		struct operation *op;
		char             *token;
		char             *p;
		int               depth;
		bool              found_sep;

		if (*op_count >= MAX_OPERATIONS) {
			fprintf(stderr, "ERROR: too many operations (max %d)\n", MAX_OPERATIONS);
			return -1;
		}

		while (*pos == ' ')
			pos++;

		if (*pos == '\0')
			break;

		token     = pos;
		depth     = 0;
		found_sep = false;

		/*
		 * Advance pos to the next comma that is NOT inside [...],
		 * so that multi-value args like node=[1,2] are kept intact.
		 */
		for (p = pos; *p != '\0'; p++) {
			if (*p == '[')
				depth++;
			else if (*p == ']')
				depth--;
			else if (*p == ',' && depth == 0) {
				*p        = '\0';
				pos       = p + 1;
				found_sep = true;
				break;
			}
		}
		if (!found_sep)
			pos = NULL; /* last token, no more after this */

		{
			int consumed = 0;

			if (sscanf(token, "%31s %31[^=]=[%127[^]]%n", type_str, mode_str, arg_str,
				   &consumed) != 3) {
				fprintf(stderr,
					"ERROR: invalid operation format: \"%s\"\n"
					"       Expected: <type> <mode>=[<id,...>]\n"
					"       Example : exclude node=[0-3, 5, 7-8]\n",
					token);
				return -1;
			}

			const char *leftover = token + consumed;

			if (*leftover == ']')
				leftover++;
			while (*leftover == ' ')
				leftover++;
			if (*leftover != '\0') {
				fprintf(stderr,
					"ERROR: operations must be comma-separated, got "
					"space-separated "
					"extra content: \"%s\"\n"
					"       Use commas between operations, e.g.: "
					"\"exclude node=[2], add node=[8]\"\n",
					leftover);
				return -1;
			}
		}

		op = &ops[*op_count];

		/* type */
		if (strcmp(type_str, "exclude") == 0)
			op->type = OP_EXCLUDE;
		else if (strcmp(type_str, "reint") == 0)
			op->type = OP_REINT;
		else if (strcmp(type_str, "drain") == 0)
			op->type = OP_DRAIN;
		else if (strcmp(type_str, "add") == 0)
			op->type = OP_ADD;
		else {
			fprintf(stderr,
				"ERROR: unknown operation type: \"%s\"\n"
				"       Valid types: exclude, reint, drain, add\n",
				type_str);
			return -1;
		}

		/* component */
		if (strcmp(mode_str, "rank") == 0)
			op->component = RANK;
		else if (strcmp(mode_str, "node") == 0)
			op->component = NODE;
		else if (strcmp(mode_str, "target") == 0)
			op->component = TARGET;
		else {
			fprintf(stderr,
				"ERROR: unknown operation component: \"%s\"\n"
				"       Valid components: rank, node, target\n",
				mode_str);
			return -1;
		}

		/* cross-validate type + component */
		if (op->type == OP_ADD && op->component != NODE) {
			fprintf(stderr,
				"ERROR: \"add\" only supports node, got \"%s\"\n"
				"       Usage: add node=[<id,...>]\n",
				mode_str);
			return -1;
		}

		/* args */
		op->nr_args = 0;

		arg = strtok_r(arg_str, ",", &arg_saveptr);

		while (arg != NULL) {
			size_t len;
			int    parse_result;

			while (*arg == ' ')
				arg++;
			len = strlen(arg);
			while (len > 0 && arg[len - 1] == ' ') {
				arg[len - 1] = '\0';
				len--;
			}

			parse_result = parse_arg_or_range(arg, op->args, &op->nr_args, MAX_OP_ARGS);
			if (parse_result < 0)
				return -1;

			arg = strtok_r(NULL, ",", &arg_saveptr);
		}

		(*op_count)++;
	}
	return 0;
}

static const char *
op_type_str(enum operation_type type)
{
	switch (type) {
	case OP_EXCLUDE:
		return "exclude";
	case OP_REINT:
		return "reint";
	case OP_DRAIN:
		return "drain";
	case OP_ADD:
		return "add";
	default:
		return "unknown";
	}
}

static const char *
op_component_str(enum operation_component component)
{
	switch (component) {
	case RANK:
		return "rank";
	case NODE:
		return "node";
	case TARGET:
		return "target";
	default:
		return "unknown";
	}
}

/*
 * Execute all operations in sequence. For each operation:
 *   1. Capture the initial layout (pre-operation).
 *   2. For OP_ADD only: insert the new node(s) into the pool map.
 *   3. Build the complete target list (by target ID, rank, or node).
 *   4. Apply the two-stage status change and collect the per-OID rebuild diff.
 *   5. Capture the post-operation layout.
 *   6. Validate the diff against the layout delta; print moved-shard
 *      percentage, performance (pl_obj_place / pl_obj_find_rebuild wall-clock),
 *      and per-operation memory usage.
 *
 * @param ctx             Test context (pool/placement maps, OID count, oclass)
 * @param operations      Array of operations to execute
 * @param operation_count Number of operations
 * @return 0 on success, negative DAOS error code on failure
 */
static int
execute_tests(struct test_ctx *ctx, struct operation *operations, int operation_count)
{
	int i;
	int rc = 0;

	for (i = 0; i < operation_count; i++) {
		/*
		 * For OP_ADD/extend the jump map rebalances freely across the
		 * new topology — any number of shards per group can move, so
		 * Check 1 (bounded drift) does not apply.  Pass INT_MAX to
		 * effectively skip it.  Check 2 (diff exactness) still runs.
		 */
		int max_diff =
		    (operations[i].type == OP_ADD) ? INT_MAX : get_oclass_ft(ctx->oclass);
		struct pl_obj_layout     **pre_layout  = NULL;
		struct pl_obj_layout     **post_layout = NULL;
		struct shard_diff         *diff        = NULL;
		struct pool_target_id_list tgts        = {0};
		int                        a;
		uint64_t                   op_layout_test_mem = 0;
		uint64_t                   op_daos_api_mem    = 0;

		printf("\n[%d/%d] Running %s %s operation\n", i + 1, operation_count,
		       op_type_str(operations[i].type), op_component_str(operations[i].component));

		do {
			D_ALLOC_ARRAY(pre_layout, ctx->num_oids);
			if (pre_layout == NULL) {
				rc = -DER_NOMEM;
				break;
			}
			op_layout_test_mem += sizeof(*pre_layout) * ctx->num_oids;

			D_ALLOC_ARRAY(post_layout, ctx->num_oids);
			if (post_layout == NULL) {
				rc = -DER_NOMEM;
				break;
			}
			op_layout_test_mem += sizeof(*post_layout) * ctx->num_oids;

			D_ALLOC_ARRAY(diff, ctx->num_oids);
			if (diff == NULL) {
				rc = -DER_NOMEM;
				break;
			}
			op_layout_test_mem += sizeof(*diff) * ctx->num_oids;

			double ms_place1  = 0.0;
			double ms_rebuild = 0.0;
			double ms_place2  = 0.0;

			/* Capture the initial layout before the operation */
			printf("  Capturing initial layouts...\n");
			rc = capture_layouts(ctx, pre_layout, &ms_place1);
			if (rc != 0) {
				D_ERROR("capture_layouts failed rc=%d\n", rc);
				break;
			}
			/* Count actual DAOS API allocation: struct header + ol_shards array */
			{
				int k;

				for (k = 0; k < ctx->num_oids; k++) {
					if (pre_layout[k] != NULL)
						op_daos_api_mem +=
						    sizeof(*pre_layout[k]) +
						    pre_layout[k]->ol_nr *
							sizeof(*pre_layout[k]->ol_shards);
				}
			}

			/*
			 * For OP_ADD, insert the new node(s) into the pool map
			 * before trying to fetch their targets.
			 */
			if (operations[i].type == OP_ADD) {
				for (a = 0; a < operations[i].nr_args; a++) {
					int new_comps;

					rc = add_node(ctx, operations[i].args[a]);
					if (rc != 0) {
						D_ERROR("add_node failed rc=%d\n", rc);
						break;
					}
					/*
					 * add_node allocates a comps array then a pool_buf;
					 * both are freed before it returns, but they represent
					 * peak memory during the call.  Account for them here.
					 * pool_map_extend internals are opaque and not tracked.
					 */
					new_comps = 1 + ctx->ranks_per_node +
						    ctx->ranks_per_node * ctx->targets_per_rank;
					op_layout_test_mem +=
					    sizeof(struct pool_component) * new_comps;
					op_daos_api_mem += pool_buf_size(new_comps);
				}
				if (rc != 0)
					break;
			}

			/*
			 * Build the complete target list for this operation.
			 * All component-type logic (TARGET / RANK / NODE) is
			 * handled inside fetch_targets.
			 */
			rc = fetch_targets(ctx, &operations[i], &tgts);
			if (rc != 0) {
				D_ERROR("fetch_targets failed rc=%d\n", rc);
				break;
			}

			/*
			 * Apply the two-stage status change and collect the
			 * per-OID shard rebuild diff between the two stages.
			 */
			printf("  Applying status change and computing diff...\n");
			rc = set_tgt_status_and_find_diff(ctx, &tgts, operations[i].type, diff,
							  &ms_rebuild);
			if (rc != 0) {
				D_ERROR("set_tgt_status_and_find_diff failed rc=%d\n", rc);
				break;
			}

			/* Capture the post-operation layout */
			printf("  Capturing post-operation layouts...\n");
			rc = capture_layouts(ctx, post_layout, &ms_place2);
			if (rc != 0) {
				D_ERROR("capture_layouts failed rc=%d\n", rc);
				break;
			}
			/* Count actual DAOS API allocation: struct header + ol_shards array */
			{
				int k;

				for (k = 0; k < ctx->num_oids; k++) {
					if (post_layout[k] != NULL)
						op_daos_api_mem +=
						    sizeof(*post_layout[k]) +
						    post_layout[k]->ol_nr *
							sizeof(*post_layout[k]->ol_shards);
				}
			}

			/*
			 * Merge diff into pre_layout, verify it matches
			 * post_layout, and print the moved-shard percentage.
			 */
			printf("  Comparing layouts...\n");
			rc = compare_layout(ctx, pre_layout, diff, post_layout, max_diff,
					    operations[i].type);
			if (rc != 0) {
				fprintf(stderr, "  Layout comparison failed rc=%d\n", rc);
				break;
			}

			printf("  Performance (pl_obj_place / pl_obj_find_rebuild only):\n");
			print_time_line("pl_obj_place   (initial)", ms_place1);
			print_time_line("pl_obj_find_rebuild", ms_rebuild);
			print_time_line("pl_obj_place   (post-op)", ms_place2);
			print_time_line("Total", ms_place1 + ms_rebuild + ms_place2);

			printf("  Operation rate:\n");
			print_ops_per_sec_line("pl_obj_place   (initial)", ctx->num_oids,
					       ms_place1);
			print_ops_per_sec_line("pl_obj_find_rebuild", ctx->num_oids, ms_rebuild);
			print_ops_per_sec_line("pl_obj_place   (post-op)", ctx->num_oids,
					       ms_place2);
			print_ops_per_sec_line("Total", ctx->num_oids,
					       ms_place1 + ms_rebuild + ms_place2);
			if (ctx->print_memory) {
				printf("  Memory (this operation):\n");
				print_memory_line("Layout test memory", op_layout_test_mem);
				print_memory_line("DAOS API memory", op_daos_api_mem);
			}
		} while (0);

		free_layouts(pre_layout, ctx->num_oids);
		free_layouts(post_layout, ctx->num_oids);
		free_diffs(diff, ctx->num_oids);
		pool_target_id_list_free(&tgts);

		if (rc != 0)
			return rc;
	}

	return 0;
}

static int
parse_args(int argc, char **argv, int64_t *nodes, int64_t *ranks_per_node, int64_t *tgts_per_rank,
	   int64_t *obj_count, char **object_class_str, struct operation operations[],
	   int *operation_count, bool *print_memory)
{
	bool seen_n = false;
	bool seen_r = false;
	bool seen_t = false;
	bool seen_c = false;
	bool seen_N = false;
	bool seen_o = false;
	bool seen_p = false;
	int  rc;

	while (1) {
		static struct option long_options[] = {{"nodes", required_argument, 0, 'n'},
						       {"ranks", required_argument, 0, 'r'},
						       {"targets", required_argument, 0, 't'},
						       {"class", required_argument, 0, 'c'},
						       {"obj-count", required_argument, 0, 'N'},
						       {"operations", required_argument, 0, 'o'},
						       {"print-memory", no_argument, 0, 'p'},
						       {"help", no_argument, 0, 'h'},
						       {0, 0, 0, 0}};

		int                  opt;

		opt = getopt_long(argc, argv, "n:r:t:c:N:o:ph", long_options, NULL);
		if (opt == -1)
			break;

#define CHECK_DUPLICATE(flag, name)                                                                \
	do {                                                                                       \
		if (flag) {                                                                        \
			fprintf(stderr, "ERROR: " name " may only be specified once\n");           \
			print_usage(argv[0]);                                                      \
			return -DER_INVAL;                                                         \
		}                                                                                  \
		flag = true;                                                                       \
	} while (0)

		switch (opt) {
		case 'n':
			CHECK_DUPLICATE(seen_n, "-n/--nodes");
			rc = parse_int_arg("nodes", optarg, nodes);
			if (rc != 0)
				return rc;
			break;

		case 'r':
			CHECK_DUPLICATE(seen_r, "-r/--ranks");
			rc = parse_int_arg("ranks", optarg, ranks_per_node);
			if (rc != 0)
				return rc;
			break;

		case 't':
			CHECK_DUPLICATE(seen_t, "-t/--targets");
			rc = parse_int_arg("targets", optarg, tgts_per_rank);
			if (rc != 0)
				return rc;
			break;

		case 'c':
			CHECK_DUPLICATE(seen_c, "-c/--class");
			*object_class_str = optarg;
			break;

		case 'N':
			CHECK_DUPLICATE(seen_N, "-N/--obj-count");
			rc = parse_int_arg("obj-count", optarg, obj_count);
			if (rc != 0)
				return rc;
			break;

		case 'o': {
			CHECK_DUPLICATE(seen_o, "-o/--operations");
			char op_buf[1024];

			strncpy(op_buf, optarg, sizeof(op_buf) - 1);
			op_buf[sizeof(op_buf) - 1] = '\0';
			if (parse_operations(op_buf, operations, operation_count) != 0) {
				fprintf(stderr, "ERROR: failed to parse --operations \"%s\"\n",
					optarg);
				return -DER_INVAL;
			}
			break;
		}

		case 'p':
			CHECK_DUPLICATE(seen_p, "-p/--print-memory");
			*print_memory = true;
			break;

		case 'h':
			print_usage(argv[0]);
			return 1;
		default:
			print_usage(argv[0]);
			return -DER_INVAL;
		}
#undef CHECK_DUPLICATE
	}

	if (argc == 1) {
		print_usage(argv[0]);
		return -DER_INVAL;
	}

	return 0;
}

/*
 * Initialize the test context from parsed arguments.
 *
 * Validates the configuration, sets up obj_class and pl subsystems,
 * initializes pool map and placement map, allocates and generates the OID
 * array, and records setup memory usage in ctx->setup_layout_test_mem and
 * ctx->setup_daos_api_mem.
 *
 * @return 0 on success, negative DAOS error code on failure
 */
static int
config_setup(struct test_ctx *ctx, int64_t nodes, int64_t ranks_per_node, int64_t tgts_per_rank,
	     int64_t obj_count, char *object_class_str, struct operation *operations,
	     int operation_count, bool print_memory)
{
	int rc;

	memset(ctx, 0, sizeof(*ctx));
	ctx->print_memory = print_memory;
	rc                = obj_class_init();
	if (rc != 0) {
		D_ERROR("obj_class_init failed rc=%d\n", rc);
		return rc;
	}
	pl_init();

	rc = validate_configuration(nodes, ranks_per_node, tgts_per_rank, obj_count,
				    object_class_str, operations, operation_count);
	if (rc != 0) {
		obj_class_fini();
		pl_fini();
		return rc;
	}

	printf("Running layout test with the following configuration:\n");
	printf("Nodes            : %lld\n", (long long)nodes);
	printf("Ranks per node   : %lld\n", (long long)ranks_per_node);
	printf("Targets per rank : %lld\n", (long long)tgts_per_rank);
	printf("Object class     : %s\n", object_class_str);
	printf("Object count     : %lld\n", (long long)obj_count);

	printf("\nSetting up layout...\n");
	ctx->nodes            = (int)nodes;
	ctx->ranks_per_node   = (int)ranks_per_node;
	ctx->targets_per_rank = (int)tgts_per_rank;
	ctx->num_oids         = (int)obj_count;
	ctx->oclass           = daos_oclass_name2id(object_class_str);

	uuid_generate(ctx->uuid);
	pool_map_and_pl_map_init(ctx);
	D_ASSERT(ctx->pool_map != NULL);
	D_ASSERT(ctx->pl_map != NULL);

	D_ALLOC_ARRAY(ctx->oids, ctx->num_oids);
	if (ctx->oids == NULL) {
		obj_class_fini();
		cleanup(ctx);
		return -DER_NOMEM;
	}
	ctx->setup_layout_test_mem += sizeof(*ctx->oids) * ctx->num_oids;

	if (ctx->print_memory) {
		printf("Setup memory (one-time, pool/pl-map init + OID array):\n");
		print_memory_line("Layout test memory", ctx->setup_layout_test_mem);
		print_memory_line("DAOS API memory", ctx->setup_daos_api_mem);
		print_memory_line("Total", ctx->setup_layout_test_mem + ctx->setup_daos_api_mem);
	}

	srand((unsigned int)time(NULL));

	generate_oids(ctx);

	return 0;
}

int
main(int argc, char **argv)
{
	int64_t          nodes            = DEFAULT_NODES;
	int64_t          ranks_per_node   = DEFAULT_RANKS_PER_NODE;
	int64_t          tgts_per_rank    = DEFAULT_TGTS_PER_RANK;
	int64_t          obj_count        = DEFAULT_OBJ_COUNT;
	char            *object_class_str = DEFAULT_OBJ_CLASS;
	struct operation operations[MAX_OPERATIONS];
	int              operation_count = 0;
	bool             print_memory    = false;
	int              rc;
	struct test_ctx  ctx;

	rc = parse_args(argc, argv, &nodes, &ranks_per_node, &tgts_per_rank, &obj_count,
			&object_class_str, operations, &operation_count, &print_memory);
	if (rc == 1)
		return 0;
	if (rc != 0)
		return EXIT_FAILURE;

	rc = config_setup(&ctx, nodes, ranks_per_node, tgts_per_rank, obj_count, object_class_str,
			  operations, operation_count, print_memory);
	if (rc != 0)
		return EXIT_FAILURE;

	/*
	 * Execute tests
	 */
	rc = execute_tests(&ctx, operations, operation_count);
	if (rc != 0) {
		printf("layout_test_runner failed rc=%d\n", rc);
		D_FREE(ctx.oids);
		obj_class_fini();
		cleanup(&ctx);
		return EXIT_FAILURE;
	}
	printf("\nEnd of test...\n");

	/* Cleanup and finalize */
	D_FREE(ctx.oids);
	obj_class_fini();
	cleanup(&ctx); /* also calls pl_fini() */
	return 0;
}
