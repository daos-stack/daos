/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "config.h"

#include "mercury.h"

#include "getopt.h"

#ifdef HG_INFO_HAS_JSON
#    include <json-c/json.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NWIDTH 20

struct options {
    char *info_string;
    bool output_csv;
    bool output_json;
    bool silent;
};

static const char *short_opts_g = "hcjs";
static const struct option long_opts_g[] = {
    {"csv", no_arg, 'c'}, {"json", no_arg, 'j'}, {"silent", no_arg, 's'},
    {NULL, 0, '\0'} /* Must add this at the end */
};

/*---------------------------------------------------------------------------*/
static void
usage(const char *execname)
{
    printf("usage: %s [OPTIONS] [<class+protocol>]\n", execname);
    printf("    OPTIONS\n");
    printf("    -h, --help           Print a usage message and exit\n");
    printf("    -c, --csv            Output in CSV format\n");
    printf("    -j, --json           Output in JSON format\n");
}

/*---------------------------------------------------------------------------*/
static void
parse_options(int argc, char **argv, struct options *opts)
{
    int opt;

    memset(opts, 0, sizeof(*opts));

    while ((opt = getopt(argc, argv, short_opts_g, long_opts_g)) != -1) {
        switch (opt) {
            case 'c':
                opts->output_csv = true;
                break;
            case 'j':
                opts->output_json = true;
                break;
            case 's':
                opts->silent = true;
                break;
            default:
                usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if ((argc - opt_ind_g) > 1) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    if ((argc - opt_ind_g) == 1)
        opts->info_string = strdup(argv[opt_ind_g]);
}

/*---------------------------------------------------------------------------*/
static void
free_options(const struct options *options)
{
    free(options->info_string);
}

/*---------------------------------------------------------------------------*/
static void
print_csv(const struct na_protocol_info *protocol_infos)
{
    const struct na_protocol_info *protocol_info;

    printf("class,protocol,device\n");
    for (protocol_info = protocol_infos; protocol_info != NULL;
         protocol_info = protocol_info->next)
        printf("%s,%s,%s\n", protocol_info->class_name,
            protocol_info->protocol_name, protocol_info->device_name);
}

/*---------------------------------------------------------------------------*/
#ifdef HG_INFO_HAS_JSON
static void
print_json(const struct na_protocol_info *protocol_infos)
{
    const struct na_protocol_info *protocol_info;
    struct json_object *json_protocol_infos = NULL, *json_protocols = NULL;

    json_protocols = json_object_new_object();
    json_protocol_infos = json_object_new_array();
    for (protocol_info = protocol_infos; protocol_info != NULL;
         protocol_info = protocol_info->next) {
        struct json_object *json_protocol_info = json_object_new_object();
        json_object_object_add(json_protocol_info, "class",
            json_object_new_string(protocol_info->class_name));
        json_object_object_add(json_protocol_info, "protocol",
            json_object_new_string(protocol_info->protocol_name));
        json_object_object_add(json_protocol_info, "device",
            json_object_new_string(protocol_info->device_name));
        json_object_array_add(json_protocol_infos, json_protocol_info);
    }
    json_object_object_add(json_protocols, "protocols", json_protocol_infos);
    printf(
        "%s\n", json_object_to_json_string_ext(json_protocols,
                    JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_NOSLASHESCAPE));
    json_object_put(json_protocols);
}
#endif

/*---------------------------------------------------------------------------*/
static void
print_std(const struct na_protocol_info *protocol_infos)
{
    const struct na_protocol_info *protocol_info;

    printf("--------------------------------------------------\n");
    printf("%-*s%*s%*s\n", 10, "Class", NWIDTH, "Protocol", NWIDTH, "Device");
    printf("--------------------------------------------------\n");
    for (protocol_info = protocol_infos; protocol_info != NULL;
         protocol_info = protocol_info->next)
        printf("%-*s%*s%*s\n", 10, protocol_info->class_name, NWIDTH,
            protocol_info->protocol_name, NWIDTH, protocol_info->device_name);
}

/*---------------------------------------------------------------------------*/
static hg_return_t
print_info(const struct options *options)
{
    struct na_protocol_info *protocol_infos = NULL;
    hg_return_t ret;

    if (!options->silent) {
        if (options->info_string == NULL)
            printf("# Retrieving protocol info for all protocols...\n");
        else
            printf("# Retrieving protocol info for \"%s\"...\n",
                options->info_string);
    }

    ret = HG_Get_na_protocol_info(options->info_string, &protocol_infos);
    if (ret != HG_SUCCESS) {
        fprintf(stderr, "HG_Get_protocol_info() failed (%s)\n",
            HG_Error_to_string(ret));
        return ret;
    }
    if (protocol_infos == NULL && options->info_string != NULL) {
        fprintf(stderr, "No protocol found for \"%s\"\n", options->info_string);
        return HG_PROTONOSUPPORT;
    }

    if (options->output_csv) {
        print_csv(protocol_infos);
    } else if (options->output_json) {
#ifdef HG_INFO_HAS_JSON
        print_json(protocol_infos);
#else
        fprintf(stderr, "JSON output format not supported\n");
        return HG_PROTONOSUPPORT;
#endif
    } else {
        print_std(protocol_infos);
    }

    HG_Free_na_protocol_info(protocol_infos);

    return HG_SUCCESS;
}

/*---------------------------------------------------------------------------*/
int
main(int argc, char *argv[])
{
    struct options options;
    hg_return_t hg_ret;

    parse_options(argc, argv, &options);

    hg_ret = print_info(&options);
    free_options(&options);

    return (hg_ret == HG_SUCCESS) ? EXIT_SUCCESS : EXIT_FAILURE;
}
