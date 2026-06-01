/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "layout_test_helpers.h"

static void
print_usage(const char *prog)
{
    printf("Usage: %s [options]\n", prog);
    printf("\nOptions:\n");
    printf("  --nodes N\n");
    printf("  --ranks N\n");
    printf("  --targets N\n");
    printf("  --class CLASS\n");
    printf("  --obj-count N\n");
    printf("  --operation \"add_node[1] exclude_rank[0,1]\"\n");
    printf("  --help\n");
}


static enum operation_type
get_operation_type(const char *name)
{
    struct op_map op_table[] = {
        { "add_node",         OP_ADD_NODE },
        { "exclude_node",     OP_EXCLUDE_NODE },
        { "exclude_rank",     OP_EXCLUDE_RANK },
        { "reintegrate_rank", OP_REINTEGRATE_RANK },
    };
    size_t i;
    for (i = 0; i < sizeof(op_table) / sizeof(op_table[0]); i++) {
        if (strcmp(name, op_table[i].name) == 0)
            return op_table[i].type;
    }
    return OP_INVALID;
}

static const char *
operation_name(enum operation_type type)
{
    switch (type) {
    case OP_ADD_NODE:
        return "ADD_NODE";
    case OP_EXCLUDE_NODE:
        return "EXCLUDE_NODE";
    case OP_EXCLUDE_RANK:
        return "EXCLUDE_RANK";
    case OP_REINTEGRATE_RANK:
        return "REINTEGRATE_RANK";
    default:
        return "INVALID";
    }
}

static int
parse_operations(char *op_string,
                 struct operation ops[],
                 int *op_count)
{
    char *token;
    char *saveptr = NULL;

    token = strtok_r(op_string, " ", &saveptr);

    while (token != NULL) {

        char op_name[64] = {0};
        char args[64] = {0};
        enum operation_type type;

        if (*op_count >= MAX_OPERATIONS) {
            fprintf(stderr, "Too many operations (max=%d)\n", MAX_OPERATIONS);
            return -1;
        }

        if (sscanf(token,"%63[^[][%63[^]]", op_name, args) != 2) {
            fprintf(stderr, "Invalid operation format: %s\n", token);
            return -1;
        }

        type = get_operation_type(op_name);
        if (type == OP_INVALID) {
            fprintf(stderr, "Unsupported operation: %s\n", op_name);
            return -1;
        }

        ops[*op_count].type = type;
        snprintf(ops[*op_count].args, sizeof(ops[*op_count].args), "%s", args);
        (*op_count)++;
        token = strtok_r(NULL, " ", &saveptr);
    }
    return 0;
}

static void
execute_operation(struct operation *op)
{
    switch (op->type) {
    case OP_ADD_NODE:
        printf("Applying add_node[%s]\n", op->args);
        /* add node implementation */
        break;

    case OP_EXCLUDE_NODE:
        printf("Applying exclude_node[%s]\n", op->args);
        /* exclude node implementation */
        break;

    case OP_EXCLUDE_RANK:
        printf("Applying exclude_rank[%s]\n", op->args);
        /* exclude rank implementation */
        break;

    case OP_REINTEGRATE_RANK:
        printf("Applying reintegrate_rank[%s]\n", op->args);
        /* reintegrate rank implementation */
        break;

    default:
        printf("Invalid operation type\n");
        break;
    }
}

static int
layout_test_runner(struct test_ctx *ctx, struct operation *operations, int operation_count, struct test_oid *oids)
{
    int rc;
    struct oid_layout	*layouts;

    D_ALLOC_ARRAY(layouts, ctx->num_oids);
    if (layouts == NULL)
        return -DER_NOMEM;

    rc = capture_layouts(ctx, oids, layouts);
    if (rc != 0) {
        printf("capture_layouts failed rc=%d\n", rc);
        D_FREE(layouts);
        return rc;
    }

    for (int i = 0; i < operation_count; i++) {
        printf("\n=== Operation %d ===\n", i + 1);
        execute_operation(&operations[i]);
    }
    /* The actual test logic would go here, including layout validation after each operation */

    D_FREE(layouts);
    return 0;
}

int
main(int argc, char **argv)
{
    uint32_t nodes = DEFAULT_NODES;
    uint32_t ranks = DEFAULT_RANKS;
    uint32_t targets = DEFAULT_TARGETS;
    uint32_t obj_count = DEFAULT_OBJ_COUNT;

    char *object_class_str = DEFAULT_OBJ_CLASS;

    struct operation operations[MAX_OPERATIONS];
    int operation_count = 0;

    while (1) {

        static struct option long_options[] = {
            {"nodes",     required_argument, 0, 'n'},
            {"ranks",     required_argument, 0, 'r'},
            {"targets",   required_argument, 0, 't'},
            {"class",     required_argument, 0, 'c'},
            {"obj-count", required_argument, 0, 'o'},
            {"operation", required_argument, 0, 'p'},
            {"help",      no_argument,       0, 'h'},
            {0, 0, 0, 0}
        };

        int ret;
        int opt;

        opt = getopt_long(argc, argv, "n:r:t:c:o:p:h", long_options, NULL);
        if (opt == -1)
            break;

        switch (opt) {
        case 'n':
            ret = sscanf(optarg, "%u", &nodes);
            if (ret != 1)
                printf("Invalid node count, using default\n");
            break;

        case 'r':
            ret = sscanf(optarg, "%u", &ranks);
            if (ret != 1)
                printf("Invalid rank count, using default\n");
            break;

        case 't':
            ret = sscanf(optarg, "%u", &targets);
            if (ret != 1)
                printf("Invalid target count, using default\n");
            break;

        case 'c':
            object_class_str = optarg;
            break;

        case 'o':
            ret = sscanf(optarg, "%u", &obj_count);
            if (ret != 1)
                printf("Invalid object count, using default\n");
            break;

        case 'p': {
            char op_buf[1024];
            strncpy(op_buf, optarg, sizeof(op_buf) - 1);
            op_buf[sizeof(op_buf) - 1] = '\0';
            if (parse_operations(op_buf, operations, &operation_count) != 0)
                return EXIT_FAILURE;
            break;
        }

        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    obj_class_init();

    // Validate the initial configuration before running any operations
    int ret = validate_configuration(nodes, ranks, targets, object_class_str, operations, operation_count);
    if (ret != 0) {
        obj_class_fini();
        return EXIT_FAILURE;
    }

    // Print the initial configuration and planned operations
    printf("Running layout test with the following configuration:\n");
    printf("Nodes            : %u\n", nodes);
    printf("Ranks per node   : %u\n", ranks);
    printf("Targets per rank : %u\n", targets);
    printf("Object class     : %s\n", object_class_str);
    printf("Object count     : %u\n", obj_count);
    printf("\nOperations (%d)\n", operation_count);
    for (int i = 0; i < operation_count; i++) {
        printf("  %d. %s [%s]\n", i + 1, operation_name(operations[i].type), operations[i].args);
    }

    struct test_ctx ctx = {
        .nodes = nodes,
        .ranks_per_node = ranks,
        .targets_per_rank = targets,
        .num_oids = obj_count,
        .oclass = daos_oclass_name2id(object_class_str),
        .pool_map = NULL,
        .pl_map = NULL
    };

    int rc;
    struct test_oid		*oids;

    D_ALLOC_ARRAY(oids, obj_count);
    if (oids == NULL) {
        printf("D_ALLOC_ARRAY oids failed\n");
        obj_class_fini();
        cleanup(&ctx);
        return EXIT_FAILURE;
    }

    rc = pool_map_init(&ctx);
    if (rc != 0) {
        printf("pool_map_init failed rc=%d\n", rc);
        D_FREE(oids);
        obj_class_fini();
        cleanup(&ctx);
        return EXIT_FAILURE;
    }
    rc = placement_map_init(&ctx);
    if (rc != 0) {
        printf("placement_map_init failed rc=%d\n", rc);
        D_FREE(oids);
        obj_class_fini();
        cleanup(&ctx);
        return EXIT_FAILURE;
    }
    
    rc = generate_oids(&ctx, oids);
    if (rc != 0) {
        printf("generate_oids failed rc=%d\n", rc);
        D_FREE(oids);
        obj_class_fini();
        cleanup(&ctx);
        return EXIT_FAILURE;
    }

    rc = layout_test_runner(&ctx, operations, operation_count, oids);
    if (rc != 0) {
        printf("layout_test_runner failed rc=%d\n", rc);
        D_FREE(oids);
        obj_class_fini();
        cleanup(&ctx);
        return EXIT_FAILURE;
    }

    /* Cleanup and finalize */

    D_FREE(oids);
    obj_class_fini();
    cleanup(&ctx);
    return 0;
}
