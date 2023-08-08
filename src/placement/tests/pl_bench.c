/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC        DD_FAC(tests)

#include <getopt.h>

#include <daos/common.h>
#include <daos/placement.h>
#include <daos.h>

#include "place_obj_common.h"

#define USE_TIME_PROFILING
#include "benchmark_util.h"

extern int  obj_class_init(void);
extern void obj_class_fini(void);

/*
 * These are only at the top of the file for reference / easy changing
 * Do not use these anywhere except in the main function where arguments are
 * parsed!
 */
#define DEFAULT_NUM_DOMAINS             8
#define DEFAULT_NODES_PER_DOMAIN        1
#define DEFAULT_VOS_PER_TARGET          4

#define BENCHMARK_STEPS 100
#define BENCHMARK_COUNT_PER_STEP 10000
#define BENCHMARK_COUNT (BENCHMARK_STEPS * BENCHMARK_COUNT_PER_STEP)

#define DEFAULT_ADDITION_NUM_TO_ADD 32
#define DEFAULT_ADDITION_TEST_ENTRIES 100000

static void
print_usage(const char *prog_name, const char *const ops[], uint32_t num_ops)
{
	D_PRINT(
		"Usage: %s --operation <op> [optional arguments] -- [operation specific arguments]\n"
		"\n"
		"Required Arguments\n"
		"  --operation <op>\n"
		"      Short version: -o\n"
		"      The operation to invoke\n"
		"      Possible values:\n", prog_name);

	for (; num_ops > 0; num_ops--)
		D_PRINT("          %s\n", ops[num_ops - 1]);

	D_PRINT("\n"
		"Optional Arguments\n"
		"  --num-domains <num>\n"
		"      Short version: -d\n"
		"      Number of domains (i.e. racks) at the highest level of the pool map\n"
		"\n"
		"      Default: %u\n"
		"\n"
		"  --nodes-per-domain <num>\n"
		"      Short version: -n\n"
		"      Number of nodes contained under each top-level domain\n"
		"\n"
		"      Default: %u\n"
		"\n"
		"  --vos-per-target <num>\n"
		"      Short version: -v\n"
		"      Number of VOS containers per target\n"
		"\n"
		"      Default: %u\n"
		"\n"
		"  --gdb-wait\n"
		"      Short version: -g\n"
		"      Starts an infinite loop which can only be escaped via gdb\n",
		DEFAULT_NUM_DOMAINS, DEFAULT_NODES_PER_DOMAIN,
		DEFAULT_VOS_PER_TARGET);
}

typedef void (*test_op_t)(int argc, char **argv, uint32_t num_domains,
			  uint32_t nodes_per_domain, uint32_t vos_per_target);

static void
print_err_layout(struct pl_obj_layout **layout_table, uint32_t i)
{
	uint32_t j;

	D_PRINT("ERROR, CO-LOCATED SHARDS\n");
	D_PRINT("Layout of object: %i\n", i);

	for (j = 0; j < layout_table[i]->ol_nr; j++)
		D_PRINT("%d ", layout_table[i]->ol_shards[j].po_target);
	D_PRINT("\n");
}

static void
check_unique_layout(int num_domains, int nodes_per_domain, int vos_per_target,
		    struct pl_obj_layout **layout_table,
		    uint32_t num_layouts, uint32_t first_layout)
{
	uint32_t i, j;
	uint8_t *target_map;
	int total_targets = num_domains * nodes_per_domain * vos_per_target;

	D_ALLOC_ARRAY(target_map, total_targets);
	for (i = first_layout; i < first_layout + num_layouts; i++) {
		for (j = 0; j < layout_table[i]->ol_nr; ++j) {

			int index = layout_table[i]->ol_shards[j].po_target;

			if (target_map[index] == 1) {
				print_err_layout(layout_table, i);
				D_ASSERT(0);
			} else
				target_map[index] = 1;
		}
		memset(target_map, 0, sizeof(*target_map) * total_targets);
	}
	D_FREE(target_map);
}


static void
benchmark_placement_usage()
{
	D_PRINT("Placement benchmark usage: -- --map-type <type>\n"
		"\n"
		"Required Arguments\n"
		"  --map-type <type>\n"
		"      Short version: -m\n"
		"      The map type to use\n"
		"      Possible values:\n"
		"          PL_TYPE_RING\n"
		"          PL_TYPE_JUMP_MAP\n"
		"\n"
		"Optional Arguments\n"
		"  --vtune-loop\n"
		"      Short version: -t\n"
		"      If specified, runs a tight loop on placement for analysis with VTune\n");
}

static void
benchmark_placement(int argc, char **argv, uint32_t num_domains,
		    uint32_t nodes_per_domain, uint32_t vos_per_target)
{
	struct pool_map *pool_map;
	struct pl_map *pl_map;
	struct daos_obj_md *obj_table;
	int i;
	struct pl_obj_layout **layout_table;

	pl_map_type_t map_type = PL_TYPE_UNKNOWN;
	int vtune_loop = 0;

	while (1) {
		static struct option long_options[] = {
			{"map-type", required_argument, 0, 'm'},
			{"vtune-loop", no_argument, 0, 't'},
			{0, 0, 0, 0}
		};
		int c;

		c = getopt_long(argc, argv, "m:t", long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'm':
			if (strncmp(optarg, "PL_TYPE_RING", 12) == 0) {
				map_type = PL_TYPE_RING;
			} else if (strncmp(optarg, "PL_TYPE_JUMP_MAP", 15)
				   == 0) {
				map_type = PL_TYPE_JUMP_MAP;
			} else {
				D_PRINT("ERROR: Unknown map-type '%s'\n",
					optarg);
				benchmark_placement_usage();
				return;
			}
			break;
		case 't':
			vtune_loop = 1;
			break;
		case '?':
		default:
			D_PRINT("ERROR: Unrecognized argument '%s'\n", optarg);
			benchmark_placement_usage();
			return;
		}
	}
	if (map_type == PL_TYPE_UNKNOWN) {
		D_PRINT("ERROR: --map-type must be specified!\n");
		benchmark_placement_usage();
		return;
	}

	/* Create reference pool/placement map */
	gen_pool_and_placement_map(1, num_domains, nodes_per_domain,
				   vos_per_target, map_type, PO_COMP_TP_RANK,
				   &pool_map, &pl_map);
	D_ASSERT(pool_map != NULL);
	D_ASSERT(pl_map != NULL);

	/* Generate list of OIDs to look up */
	D_ALLOC_ARRAY(obj_table, BENCHMARK_COUNT);
	D_ASSERT(obj_table != NULL);

	/* Storage for returned layout data */
	D_ALLOC_ARRAY(layout_table, BENCHMARK_COUNT);
	D_ASSERT(layout_table != NULL);

	for (i = 0; i < BENCHMARK_COUNT; i++) {
		int rc;

		memset(&obj_table[i], 0, sizeof(obj_table[i]));
		obj_table[i].omd_id.lo = rand();
		obj_table[i].omd_id.hi = 5;
		rc = daos_obj_set_oid_by_class(&obj_table[i].omd_id, 0, OC_RP_4G2, 0);
		D_ASSERT(rc == 0);
		obj_table[i].omd_ver = 1;
	}

	/* Warm up the cache and check that it works correctly */
	for (i = 0; i < BENCHMARK_COUNT; i++)
		pl_obj_place(pl_map, 0, &obj_table[i], 0, NULL, &layout_table[i]);
	check_unique_layout(num_domains, nodes_per_domain, vos_per_target,
			    layout_table, BENCHMARK_COUNT, 0);

	if (vtune_loop) {
		D_PRINT("Starting vtune loop!\n");
		while (1)
			for (i = 0; i < BENCHMARK_COUNT; i++)
				pl_obj_place(pl_map, 0, &obj_table[i], 0, NULL,
					     &layout_table[i]);
	}

	/* Simple layout calculation benchmark */
	{
		struct benchmark_handle *bench_hdl;

		bench_hdl = benchmark_alloc();
		D_ASSERT(bench_hdl != NULL);

		benchmark_start(bench_hdl);
		for (i = 0; i < BENCHMARK_COUNT; i++)
			pl_obj_place(pl_map, 0, &obj_table[i], 0, NULL,
				     &layout_table[i]);
		benchmark_stop(bench_hdl);

		D_PRINT("\nPlacement benchmark results:\n");
		D_PRINT(
			"# Iterations, Wallclock time (ns), thread time (ns), Wallclock placements per second\n"
		);
		D_PRINT("%d,%lld,%lld,%lld\n", BENCHMARK_COUNT,
			bench_hdl->wallclock_delta_ns,
			bench_hdl->thread_delta_ns,
			NANOSECONDS_PER_SECOND * BENCHMARK_COUNT /
			bench_hdl->wallclock_delta_ns);

		benchmark_free(bench_hdl);
	}

	free_pool_and_placement_map(pool_map, pl_map);
	D_FREE(obj_table);
	D_FREE(layout_table);
}

void
benchmark_add_data_movement_usage()
{
	D_PRINT(
		"Addition data movement benchmark usage: -- --map-type <type1,type2,...> [optional arguments]\n"
		"\n"
		"Required Arguments\n"
		"  --map-type <type1,type2,...>\n"
		"      Short version: -m\n"
		"      A comma delimited list of map types to test\n"
		"      Possible values:\n"
		"          PL_TYPE_RING\n"
		"          PL_TYPE_JUMP_MAP\n"
		"\n"
		"Optional Arguments\n"
		"  --num-domains-to-add <num>\n"
		"      Short version: -a\n"
		"      Number of top-level domains to add\n"
		"      Default: %d\n"
		"\n"
		"  --num-test-entries <num>\n"
		"      Short version: -t\n"
		"      Number of objects to test placing each iteration\n"
		"      Default: %d\n"
		"\n"
		"  --use-x11\n"
		"      Short version: -x\n"
		"      Display the resulting graph using x11 instead of the default console\n"
		"\n",
		DEFAULT_ADDITION_NUM_TO_ADD, DEFAULT_ADDITION_TEST_ENTRIES);
}

static void
compute_data_movement(uint32_t domains, uint32_t nodes_per_domain,
		      uint32_t vos_per_target, pl_map_type_t map_type,
		      int test_entries, struct daos_obj_md *obj_table,
		      struct pl_obj_layout **initial_layout,
		      struct pl_obj_layout **iter_layout,
		      double *percent_moved)
{
	struct pool_map *iter_pool_map;
	struct pl_map *iter_pl_map;
	int num_moved_all_at_once = 0;
	int obj_idx;
	int j;

	/*
	 * Generate a new pool/placement map combination for
	 * this new configuration
	 */
	gen_pool_and_placement_map(1, domains, nodes_per_domain, vos_per_target,
				   map_type, PO_COMP_TP_RANK, &iter_pool_map, &iter_pl_map);
	D_ASSERT(iter_pool_map != NULL);
	D_ASSERT(iter_pl_map != NULL);

	/* Calculate new placement using this configuration */
	for (obj_idx = 0; obj_idx < test_entries; obj_idx++)
		pl_obj_place(iter_pl_map, 0, &obj_table[obj_idx], 0,
			     NULL, &iter_layout[obj_idx]);

	/* Compute the number of objects that moved */
	for (obj_idx = 0; obj_idx < test_entries; obj_idx++) {
		for (j = 0; j < iter_layout[obj_idx]->ol_nr; j++) {
			if (iter_layout[obj_idx]->ol_shards[j].po_target !=
			    initial_layout[obj_idx]->ol_shards[j].po_target)
				num_moved_all_at_once++;
		}
	}

	*percent_moved = (double)num_moved_all_at_once /
			 ((double)test_entries * iter_layout[0]->ol_nr);

	free_pool_and_placement_map(iter_pool_map, iter_pl_map);
}

void
benchmark_add_data_movement(int argc, char **argv, uint32_t num_domains,
			    uint32_t nodes_per_domain, uint32_t vos_per_target)
{
	struct pool_map *initial_pool_map;
	struct pl_map *initial_pl_map;
	struct daos_obj_md *obj_table = NULL;
	struct pl_obj_layout **initial_layout = NULL;
	struct pl_obj_layout **iter_layout = NULL;
	int obj_idx;
	int type_idx;
	int added;
	char *token;
	double *percent_moved = NULL;
	int j;

	/*
	 * This is the total number of requested map types from the user
	 * It is always +1 more than the user requested - and that last
	 * index is the "ideal" amount of moved data
	 */
	int num_map_types = 0;
	pl_map_type_t *map_types = NULL;
	const char **map_keys = NULL;
	int domains_to_add = DEFAULT_ADDITION_NUM_TO_ADD;
	int test_entries = DEFAULT_ADDITION_TEST_ENTRIES;
	bool use_x11 = false;

	D_PRINT("\n\n");
	D_PRINT("Addition test starting...\n");

	while (1) {
		static struct option long_options[] = {
			{"map-type", required_argument, 0, 'm'},
			{"num-domains-to-add", required_argument, 0, 'a'},
			{"num-test-entries", required_argument, 0, 't'},
			{"use-x11", no_argument, 0, 'x'},
			{0, 0, 0, 0}
		};
		int c;
		int ret;

		c = getopt_long(argc, argv, "m:a:t:x", long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'm':
			/* Figure out how many types there are */
			j = 0;
			num_map_types = 1;
			while (optarg[j] != '\0') {
				if (optarg[j] == ',')
					num_map_types++;
				j++;
			}
			/* Pad +1 for "ideal" */
			num_map_types++;

			if (map_types != NULL)
				D_FREE(map_types);
			D_ALLOC_ARRAY(map_types, num_map_types);
			D_ASSERT(map_types != NULL);

			if (map_keys != NULL)
				D_FREE(map_keys);
			D_ALLOC_ARRAY(map_keys, num_map_types);
			D_ASSERT(map_keys != NULL);

			/* Ideal */
			map_keys[num_map_types - 1] = "Ideal";

			/* Populate the types array */
			num_map_types = 0;
			token = strtok(optarg, ",");
			while (token != NULL) {
				if (strncmp(token, "PL_TYPE_RING", 12) == 0) {
					map_types[num_map_types] = PL_TYPE_RING;
					map_keys[num_map_types] =
						"PL_TYPE_RING";
				} else if (strncmp(token, "PL_TYPE_JUMP_MAP",
						   15) == 0) {
					map_types[num_map_types] =
						PL_TYPE_JUMP_MAP;
					map_keys[num_map_types] =
						"PL_TYPE_JUMP_MAP";
				} else {
					D_PRINT("ERROR: Unknown map-type: %s\n",
						token);
					benchmark_add_data_movement_usage();
					goto out;
				}
				num_map_types++;
				token = strtok(NULL, ",");
			}
			/* Pad +1 for "ideal" */
			num_map_types++;

			break;
		case 'a':
			ret = sscanf(optarg, "%d", &domains_to_add);
			if (ret != 1 || domains_to_add <= 0) {
				D_PRINT("ERROR: Invalid num-domains-to-add\n");
				benchmark_add_data_movement_usage();
				goto out;
			}
			break;
		case 't':
			ret = sscanf(optarg, "%d", &test_entries);
			if (ret != 1 || test_entries <= 0) {
				D_PRINT("ERROR: Invalid num-test-entries\n");
				benchmark_add_data_movement_usage();
				goto out;
			}
			break;
		case 'x':
			use_x11 = true;
			break;
		case '?':
		default:
			D_PRINT("ERROR: Unrecognized argument: %s\n", optarg);
			benchmark_add_data_movement_usage();
			goto out;
		}
	}

	if (num_map_types == 0) {
		D_PRINT("ERROR: --map-type must be specified!\n");
		benchmark_add_data_movement_usage();
		goto out;
	}

	/* Generate list of OIDs to look up */
	D_ALLOC_ARRAY(obj_table, test_entries);
	D_ASSERT(obj_table != NULL);

	for (obj_idx = 0; obj_idx < test_entries; obj_idx++) {
		int rc;

		memset(&obj_table[obj_idx], 0, sizeof(obj_table[obj_idx]));
		obj_table[obj_idx].omd_id.lo = rand();
		obj_table[obj_idx].omd_id.hi = 5;
		rc = daos_obj_set_oid_by_class(&obj_table[obj_idx].omd_id, 0,
					       OC_RP_4G2, 0);
		D_ASSERT(rc == 0);
		obj_table[obj_idx].omd_ver = 1;
	}

	/* Allocate space for layouts */
	/* Initial layout - without changes to the map */
	D_ALLOC_ARRAY(initial_layout, test_entries);
	D_ASSERT(initial_layout != NULL);
	/* Per-iteration layout to diff against others */
	D_ALLOC_ARRAY(iter_layout, test_entries);
	D_ASSERT(iter_layout != NULL);

	/*
	 * Allocate space for results data
	 * This is a flat 2D array of results!
	 */
	D_ALLOC_ARRAY(percent_moved, num_map_types * (domains_to_add + 1));
	D_ASSERT(percent_moved != NULL);

	/* Measure movement for all but ideal case */
	for (type_idx = 0; type_idx < num_map_types - 1; type_idx++) {
		/* Create initial reference pool/placement map */
		gen_pool_and_placement_map(1, num_domains, nodes_per_domain,
					   vos_per_target, map_types[type_idx],
					   PO_COMP_TP_RANK, &initial_pool_map, &initial_pl_map);
		D_ASSERT(initial_pool_map != NULL);
		D_ASSERT(initial_pl_map != NULL);

		/* Initial placement */
		for (obj_idx = 0; obj_idx < test_entries; obj_idx++)
			pl_obj_place(initial_pl_map, 0, &obj_table[obj_idx], 0, NULL,
				     &initial_layout[obj_idx]);

		for (added = 0; added <= domains_to_add; added++) {
			/* Pointer to where % data moved should be stored */
			double *out;

			out = &percent_moved[type_idx * (domains_to_add + 1)
					     + added];

			compute_data_movement(num_domains + added,
					      nodes_per_domain,
					      vos_per_target,
					      map_types[type_idx],
					      test_entries, obj_table,
					      initial_layout, iter_layout, out);
		}

		free_pool_and_placement_map(initial_pool_map, initial_pl_map);
	}

	/* Calculate the "ideal" data movement */
	for (added = 0; added <= domains_to_add; added++) {
		type_idx = num_map_types - 1;
		percent_moved[type_idx * (domains_to_add + 1) + added] =
			(double)added * 1 * nodes_per_domain /
			(1 * nodes_per_domain * num_domains +
			 added * 1 * nodes_per_domain);
	}

	/* Print out the data */
	for (type_idx = 0; type_idx < num_map_types; type_idx++) {
		D_PRINT("Addition Data: Type %d\n", type_idx);
		for (added = 0; added <= domains_to_add; added++) {
			D_PRINT("%f\n",
				percent_moved[type_idx * (domains_to_add + 1)
						       + added]);
		}
	}
	D_PRINT("\n");

	benchmark_graph((double *)percent_moved, map_keys, num_map_types,
			domains_to_add + 1, "Number of added racks",
			"% Data Moved", 1.0,
			"Data movement %% when adding racks", "/tmp/gnufifo",
			use_x11);

out:
	if (map_keys != NULL)
		D_FREE(map_keys);
	if (map_types != NULL)
		D_FREE(map_types);
	if (percent_moved != NULL)
		D_FREE(percent_moved);
	if (iter_layout != NULL)
		D_FREE(iter_layout);
	if (initial_layout != NULL)
		D_FREE(initial_layout);
	if (obj_table != NULL)
		D_FREE(obj_table);
}


int
main(int argc, char **argv)
{
	uint32_t                 num_domains = DEFAULT_NUM_DOMAINS;
	uint32_t                 nodes_per_domain = DEFAULT_NODES_PER_DOMAIN;
	uint32_t                 vos_per_target = DEFAULT_VOS_PER_TARGET;

	int                      rc = -1;
	int                      i;

	test_op_t operation = NULL;

	test_op_t op_fn[] = {
		benchmark_placement,
		benchmark_add_data_movement,
	};
	const char *const op_names[] = {
		"benchmark-placement",
		"benchmark-add",
	};
	D_ASSERT(ARRAY_SIZE(op_fn) == ARRAY_SIZE(op_names));

	while (1) {
		static struct option long_options[] = {
			{"operation", required_argument, 0, 'o'},
			{"num-domains", required_argument, 0, 'd'},
			{"nodes-per-domain", required_argument, 0, 'n'},
			{"vos-per-target", required_argument, 0, 'v'},
			{"gdb-wait", no_argument, 0, 'g'},
			{0, 0, 0, 0}
		};
		int c;
		int ret;

		c = getopt_long(argc, argv, "o:d:n:v:g", long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 'd':
			ret = sscanf(optarg, "%u", &num_domains);
			if (ret != 1) {
				num_domains = DEFAULT_NUM_DOMAINS;
				D_PRINT("Warning: Invalid num-domains\n"
					"  Using default value %u instead\n",
					num_domains);
			}
			break;
		case 'n':
			ret = sscanf(optarg, "%u", &nodes_per_domain);
			if (ret != 1) {
				nodes_per_domain = DEFAULT_NODES_PER_DOMAIN;
				D_PRINT("Warning: Invalid nodes-per-domain\n"
					"  Using default value %u instead\n",
					nodes_per_domain);
			}
			break;
		case 'v':
			ret = sscanf(optarg, "%u", &vos_per_target);
			if (ret != 1) {
				vos_per_target = DEFAULT_VOS_PER_TARGET;
				D_PRINT("Warning: Invalid vos-per-target\n"
					"  Using default value %u instead\n",
					vos_per_target);
			}
			break;
		case 'o':
			for (i = 0; i < ARRAY_SIZE(op_fn); i++) {
				if (strncmp(optarg, op_names[i],
					    strlen(op_names[i])) == 0) {
					operation = op_fn[i];
					break;
				}
			}
			if (i == ARRAY_SIZE(op_fn)) {
				D_PRINT("ERROR: Unknown operation '%s'\n",
					optarg);
				print_usage(argv[0], op_names,
					    ARRAY_SIZE(op_names));
				goto out;
			}
			break;
		case 'g':
			{
				volatile int gdb = 0;

				D_PRINT("Entering infinite loop wait for GDB\n"
					"Connect via something like:\n"
					"  gdb -tui attach $(pidof pl_map)\n"
					"Once connected, run:\n"
					"  set gdb=1\n"
					"  continue\n");
				while (!gdb)
					usleep(1000);
			}
			break;
		case '?':
		default:
			print_usage(argv[0], op_names, ARRAY_SIZE(op_names));
			goto out;
		}
	}

	if (operation == NULL) {
		D_PRINT("ERROR: operation argument is required!\n");

		print_usage(argv[0], op_names, ARRAY_SIZE(op_names));
		goto out;
	}
	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc)
		goto out;

	rc = obj_class_init();
	if (rc)
		goto out_debug;

	rc = pl_init();
	if (rc)
		goto out_class;

	operation(argc, argv, num_domains, nodes_per_domain, vos_per_target);

	pl_fini();
out_class:
	obj_class_fini();
out_debug:
	daos_debug_fini();
out:
	return rc;
}
