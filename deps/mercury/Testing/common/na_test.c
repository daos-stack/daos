/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "na_test.h"
#include "na_test_getopt.h"

#include "mercury_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <Winsock2.h>
#    include <Ws2tcpip.h>
#else
#    include <arpa/inet.h>
#    include <netdb.h>
#    include <netinet/in.h>
#    include <sys/socket.h>
#    include <sys/types.h>
#    include <unistd.h>
#    if defined(HG_TEST_HAS_SYSPRCTL_H)
#        include <sys/prctl.h>
#    endif
#endif

/****************/
/* Local Macros */
/****************/
#define HG_TEST_CONFIG_FILE_NAME "/port.cfg"

#ifdef _WIN32
#    undef strdup
#    define strdup _strdup
#endif

/************************************/
/* Local Type and Struct Definition */
/************************************/

/********************/
/* Local Prototypes */
/********************/

static void
na_test_parse_options(
    int argc, char *argv[], struct na_test_info *na_test_info);

static size_t
na_test_parse_size(const char *str);

static enum na_traffic_class
na_test_tclass(const char *str);

#ifdef HG_TEST_HAS_CXI
static na_return_t
na_test_alloc_svc(struct na_test_cxi_info *cxi_info, const char *init_str);

static na_return_t
na_test_print_svc(
    const struct na_test_cxi_info *cxi_info, char *buf, size_t buf_size);

static na_return_t
na_test_free_svc(struct na_test_cxi_info *cxi_info);
#endif

static char *
na_test_gen_config(struct na_test_info *na_test_info, unsigned int i);

static na_return_t
na_test_self_addr_publish(
    const char *hostfile, na_class_t *na_class, bool append);

/*******************/
/* Local Variables */
/*******************/

extern int na_test_opt_ind_g;         /* token pointer */
extern const char *na_test_opt_arg_g; /* flag argument (or value) */
extern const char *na_test_short_opt_g;
extern const struct na_test_opt na_test_opt_g[];

/* Default log outlets */
#ifdef _WIN32
HG_LOG_OUTLET_DECL(na_test) = HG_LOG_OUTLET_INITIALIZER(
    na_test, HG_LOG_PASS, NULL, NULL);
#else
HG_LOG_DECL_REGISTER(na_test);
#endif

/*---------------------------------------------------------------------------*/
void
na_test_usage(const char *execname)
{
    printf("usage: %s [OPTIONS]\n", execname);
    printf("    NA OPTIONS\n");
    printf("    -h, --help           Print a usage message and exit\n");
    printf("    -c, --comm           Select NA plugin\n"
           "                         NA plugins: ofi, ucx, etc\n");
    printf("    -d, --domain         Select NA OFI domain\n");
    printf("    -p, --protocol       Select plugin protocol\n"
           "                         Available protocols: tcp, verbs, etc\n");
    printf("    -H, --hostname       Select hostname / IP address to use\n"
           "                         Default: any\n");
    printf("    -P, --port           Select port to use\n"
           "                         Default: any\n");
    printf("    -S, --self_send      Send to self\n");
    printf("    -k, --key            Pass auth key\n");
    printf("    -T, --tclass         Traffic class to use\n");
    printf("    -l, --loop           Number of loops (default: 1)\n");
    printf("    -b, --busy           Busy wait\n");
    printf("    -y  --buf_size_min   Min buffer size (in bytes)\n");
    printf("    -z, --buf_size_max   Max buffer size (in bytes)\n");
    printf("    -w  --buf_count      Number of buffers used\n");
    printf("    -R, --force-register Force registration of buffers\n");
    printf("    -M, --mbps           Output in MB/s instead of MiB/s\n");
    printf("    -U, --no-multi-recv  Disable multi-recv\n");
    printf("    -f, --hostfile       Specify hostfile to use\n"
           "                         Default: " HG_TEST_TEMP_DIRECTORY
               HG_TEST_CONFIG_FILE_NAME "\n");
    printf("    -V, --verbose        Print verbose output\n");
}

/*---------------------------------------------------------------------------*/
static void
na_test_parse_options(int argc, char *argv[], struct na_test_info *na_test_info)
{
    int opt;

    if (argc < 2) {
        na_test_usage(argv[0]);
        exit(1);
    }

    while ((opt = na_test_getopt(
                argc, argv, na_test_short_opt_g, na_test_opt_g)) != EOF) {
        switch (opt) {
            case 'h':
                na_test_usage(argv[0]);
                exit(1);
            case 'c': /* Comm */
                /* Prevent from overriding comm */
                if (!na_test_info->comm) {
                    if (strcmp(na_test_opt_arg_g, "sm") == 0)
                        na_test_info->comm = strdup("na");
                    else
                        na_test_info->comm = strdup(na_test_opt_arg_g);
                }
                break;
            case 'd': /* Domain */
                na_test_info->domain = strdup(na_test_opt_arg_g);
                break;
            case 'p': /* Protocol */
                /* Prevent from overriding protocol */
                if (!na_test_info->protocol)
                    na_test_info->protocol = strdup(na_test_opt_arg_g);
                break;
            case 'H': /* hostname */
                na_test_info->hostname = strdup(na_test_opt_arg_g);
                break;
            case 'P': /* port */
                na_test_info->port = atoi(na_test_opt_arg_g);
                break;
            case 's': /* static */
                na_test_info->mpi_static = true;
                break;
            case 'S': /* self */
                na_test_info->self_send = true;
                break;
            case 'k': /* key */
                na_test_info->key = strdup(na_test_opt_arg_g);
                break;
            case 'l': /* loop */
                na_test_info->loop = atoi(na_test_opt_arg_g);
                break;
            case 'b': /* busy */
                na_test_info->busy_wait = true;
                break;
            case 'C': /* number of classes */
                na_test_info->max_classes = (size_t) atol(na_test_opt_arg_g);
                break;
            case 'X': /* number of contexts */
                na_test_info->max_contexts = (uint8_t) atoi(na_test_opt_arg_g);
                break;
            case 'y': /* min buffer size */
                na_test_info->buf_size_min =
                    na_test_parse_size(na_test_opt_arg_g);
                break;
            case 'z': /* max buffer size */
                na_test_info->buf_size_max =
                    na_test_parse_size(na_test_opt_arg_g);
                break;
            case 'w': /* buffer count */
                na_test_info->buf_count = (size_t) atol(na_test_opt_arg_g);
                break;
            case 'Z': /* msg size */
                na_test_info->max_msg_size =
                    na_test_parse_size(na_test_opt_arg_g);
                break;
            case 'R': /* force-register */
                na_test_info->force_register = true;
                break;
            case 'v': /* verify */
                na_test_info->verify = true;
                break;
            case 'V': /* verbose */
                na_test_info->verbose = true;
                break;
            case 'M': /* OSU-style output MB/s */
                na_test_info->mbps = true;
                break;
            case 'U': /* no-multi-recv */
                na_test_info->no_multi_recv = true;
                break;
            case 'f': /* hostfile */
                na_test_info->hostfile = strdup(na_test_opt_arg_g);
                break;
            case 'T': /* tclass */
                na_test_info->tclass = strdup(na_test_opt_arg_g);
                break;
            default:
                break;
        }
    }
    na_test_opt_ind_g = 1;

    if (!na_test_info->protocol) {
        na_test_usage(argv[0]);
        exit(1);
    }
    if (!na_test_info->loop)
        na_test_info->loop = 1; /* Default */
}

/*---------------------------------------------------------------------------*/
static size_t
na_test_parse_size(const char *str)
{
    size_t size;
    char prefix;

    if (sscanf(str, "%zu%c", &size, &prefix) == 2) {
        switch (prefix) {
            case 'k':
                size *= 1024;
                break;
            case 'm':
                size *= (1024 * 1024);
                break;
            case 'g':
                size *= (1024 * 1024 * 1024);
                break;
            default:
                break;
        }
        return size;
    } else if (sscanf(str, "%zu", &size) == 1)
        return size;
    else
        return 0;
}

/*---------------------------------------------------------------------------*/
static enum na_traffic_class
na_test_tclass(const char *str)
{
    if (strcmp(str, "best_effort") == 0)
        return NA_TC_BEST_EFFORT;
    else if (strcmp(str, "low_latency") == 0)
        return NA_TC_LOW_LATENCY;
    else if (strcmp(str, "bulk_data") == 0)
        return NA_TC_BULK_DATA;
    else if (strcmp(str, "dedicated_access") == 0)
        return NA_TC_DEDICATED_ACCESS;
    else if (strcmp(str, "scavenger") == 0)
        return NA_TC_SCAVENGER;
    else if (strcmp(str, "network_ctrl") == 0)
        return NA_TC_NETWORK_CTRL;
    else {
        fprintf(stderr,
            "Traffic class does not match: best_effort, low_latency, "
            "bulk_data, dedicated_access, scavenger, network_ctrl\n");
        return NA_TC_UNSPEC;
    }
}

/*---------------------------------------------------------------------------*/
#ifdef HG_TEST_HAS_CXI
static na_return_t
na_test_alloc_svc(struct na_test_cxi_info *cxi_info, const char *init_str)
{
    struct cxi_svc_fail_info svc_fail_info = {};
    uint16_t vni_min = 0, vni_max = 0;
    na_return_t ret;
    unsigned int i;
    int rc;

    rc = sscanf(init_str, "0:%" SCNu16 ":%" SCNu16, &vni_min, &vni_max);
    NA_TEST_CHECK_ERROR(rc != 1 && rc != 2, error, ret, NA_PROTONOSUPPORT,
        "Invalid CXI auth key range string (%s), format is "
        "\"0:vni_min<:vni_max>\"",
        init_str);

    cxi_info->dev = NULL;

    rc = cxil_open_device(0, &cxi_info->dev);
    NA_TEST_CHECK_ERROR(rc != 0, error, ret, NA_PROTOCOL_ERROR,
        "cxil_open_device() failed (%d)", rc);

    memset(&cxi_info->svc_desc, 0, sizeof(cxi_info->svc_desc));

    cxi_info->svc_desc.restricted_vnis = 1;
    cxi_info->svc_desc.enable = 1;
    cxi_info->svc_desc.num_vld_vnis =
        (vni_max > vni_min) ? vni_max - vni_min + 1 : 1;

    for (i = 0; i < cxi_info->svc_desc.num_vld_vnis; i++)
        cxi_info->svc_desc.vnis[i] = vni_min + i;

    rc = cxil_alloc_svc(cxi_info->dev, &cxi_info->svc_desc, &svc_fail_info);
    NA_TEST_CHECK_ERROR(rc <= 0, error, ret, NA_PROTOCOL_ERROR,
        "cxil_alloc_svc() failed (%d)", rc);

    cxi_info->svc_desc.svc_id = rc;

    return NA_SUCCESS;

error:
    (void) na_test_free_svc(cxi_info);

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_test_print_svc(
    const struct na_test_cxi_info *cxi_info, char *buf, size_t buf_size)
{
    na_return_t ret;
    int rc;

    if (cxi_info->svc_desc.num_vld_vnis == 1)
        rc = snprintf(buf, buf_size, "%" PRIu32 ":%" PRIu16,
            cxi_info->svc_desc.svc_id, cxi_info->svc_desc.vnis[0]);
    else
        rc = snprintf(buf, buf_size, "%" PRIu32 ":%" PRIu16 ":%" PRIu16,
            cxi_info->svc_desc.svc_id, cxi_info->svc_desc.vnis[0],
            cxi_info->svc_desc.vnis[cxi_info->svc_desc.num_vld_vnis - 1]);
    NA_TEST_CHECK_ERROR(rc < 0 || rc > (int) buf_size, error, ret, NA_OVERFLOW,
        "snprintf() failed or name truncated, rc: %d (expected %zu)", rc,
        buf_size);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_test_free_svc(struct na_test_cxi_info *cxi_info)
{
    na_return_t ret;

    if (cxi_info->svc_desc.svc_id > 0) {
        int rc = cxil_destroy_svc(cxi_info->dev, cxi_info->svc_desc.svc_id);
        NA_TEST_CHECK_ERROR(rc != 0, error, ret, NA_PROTOCOL_ERROR,
            "cxil_destroy_svc() failed (%d)", rc);
        cxi_info->svc_desc.svc_id = 0;
    }

    if (cxi_info->dev != NULL) {
        cxil_close_device(cxi_info->dev);
        cxi_info->dev = NULL;
    }

    return NA_SUCCESS;

error:
    return ret;
}
#endif

/*---------------------------------------------------------------------------*/
static char *
na_test_gen_config(struct na_test_info *na_test_info, unsigned int i)
{
    char *info_string = NULL, *info_string_ptr = NULL;

    info_string = (char *) malloc(sizeof(char) * NA_TEST_MAX_ADDR_NAME);
    NA_TEST_CHECK_ERROR_NORET(
        info_string == NULL, error, "Could not allocate info string");

    memset(info_string, '\0', NA_TEST_MAX_ADDR_NAME);
    info_string_ptr = info_string;
    if (na_test_info->comm)
        info_string_ptr += sprintf(info_string_ptr, "%s+", na_test_info->comm);
    info_string_ptr +=
        sprintf(info_string_ptr, "%s://", na_test_info->protocol);
    if (na_test_info->domain)
        info_string_ptr +=
            sprintf(info_string_ptr, "%s/", na_test_info->domain);

    if (strcmp("sm", na_test_info->protocol) == 0) {
#if defined(PR_SET_PTRACER) && defined(PR_SET_PTRACER_ANY)
        FILE *scope_config;
        int yama_val = '0';

        /* Try to open ptrace_scope */
        scope_config = fopen("/proc/sys/kernel/yama/ptrace_scope", "r");
        if (scope_config) {
            yama_val = fgetc(scope_config);
            fclose(scope_config);
        }

        /* Enable CMA on systems with YAMA */
        if (yama_val != '0') {
            int rc = prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
            NA_TEST_CHECK_ERROR_NORET(rc < 0, error, "Could not set ptracer");
        }
#endif
    } else if (strcmp("static", na_test_info->protocol) == 0) {
        /* Nothing */
    } else if (strcmp("dynamic", na_test_info->protocol) == 0) {
        /* Nothing */
    } else {
        if (na_test_info->hostname)
            info_string_ptr +=
                sprintf(info_string_ptr, "%s", na_test_info->hostname);
        if (na_test_info->port)
            info_string_ptr += sprintf(
                info_string_ptr, ":%u", (unsigned int) na_test_info->port + i);
    }

    return info_string;

error:
    free(info_string);

    return NULL;
}

/*---------------------------------------------------------------------------*/
na_return_t
na_test_set_config(const char *hostfile, const char *addr_name, bool append)
{
    const char *config_file =
        hostfile != NULL ? hostfile
                         : HG_TEST_TEMP_DIRECTORY HG_TEST_CONFIG_FILE_NAME;
    FILE *config = NULL;
    na_return_t ret;
    int rc;

    if (!append)
        printf("# Writing config to %s\n", config_file);
    config = fopen(config_file, append ? "a" : "w");
    NA_TEST_CHECK_ERROR(config == NULL, error, ret, NA_NOENTRY,
        "Could not open config file from: %s",
        HG_TEST_TEMP_DIRECTORY HG_TEST_CONFIG_FILE_NAME);

    rc = fprintf(config, "%s\n", addr_name);
    NA_TEST_CHECK_ERROR(
        rc < 0, error, ret, NA_PROTOCOL_ERROR, "fprintf() failed");

    rc = fclose(config);
    config = NULL;
    NA_TEST_CHECK_ERROR(
        rc != 0, error, ret, NA_PROTOCOL_ERROR, "fclose() failed");

    return NA_SUCCESS;

error:
    if (config != NULL)
        fclose(config);

    return ret;
}

/*---------------------------------------------------------------------------*/
na_return_t
na_test_get_config(const char *hostfile, char *addrs[], size_t *count_p)
{
    const char *config_file =
        hostfile != NULL ? hostfile
                         : HG_TEST_TEMP_DIRECTORY HG_TEST_CONFIG_FILE_NAME;
    FILE *config = NULL;
    na_return_t ret;
    size_t count = 0;
    int rc;

    if (addrs != NULL)
        printf("# Reading config from %s\n", config_file);
    config = fopen(config_file, "r");
    NA_TEST_CHECK_ERROR(config == NULL, error, ret, NA_NOENTRY,
        "Could not open config file from: %s",
        HG_TEST_TEMP_DIRECTORY HG_TEST_CONFIG_FILE_NAME);

    for (count = 0; !feof(config) && (!*count_p || count < *count_p); count++) {
        char addr[NA_TEST_MAX_ADDR_NAME];

        rc = fscanf(config, "%s\n", addr);
        NA_TEST_CHECK_ERROR(
            rc < 0, error, ret, NA_PROTOCOL_ERROR, "fscanf() failed");

        if (addrs) {
            addrs[count] = strdup(addr);
            NA_TEST_CHECK_ERROR(addrs[count] == NULL, error, ret, NA_NOMEM,
                "strdup() of %s failed", addr);
        }
    }

    rc = fclose(config);
    config = NULL;
    NA_TEST_CHECK_ERROR(
        rc != 0, error, ret, NA_PROTOCOL_ERROR, "fclose() failed");

    *count_p = count;

    return NA_SUCCESS;

error:
    if (addrs) {
        size_t i;

        for (i = 0; i < count; i++) {
            free(addrs[i]);
            addrs[i] = NULL;
        }
    }
    if (config != NULL)
        fclose(config);

    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_test_self_addr_publish(
    const char *hostfile, na_class_t *na_class, bool append)
{
    char addr_string[NA_TEST_MAX_ADDR_NAME];
    size_t addr_string_len = NA_TEST_MAX_ADDR_NAME;
    na_addr_t *self_addr = NULL;
    na_return_t ret;

    ret = NA_Addr_self(na_class, &self_addr);
    NA_TEST_CHECK_NA_ERROR(
        error, ret, "NA_Addr_self() failed (%s)", NA_Error_to_string(ret));

    ret = NA_Addr_to_string(na_class, addr_string, &addr_string_len, self_addr);
    NA_TEST_CHECK_NA_ERROR(
        error, ret, "NA_Addr_to_string() failed (%s)", NA_Error_to_string(ret));

    NA_Addr_free(na_class, self_addr);
    self_addr = NULL;

    ret = na_test_set_config(hostfile, addr_string, append);
    NA_TEST_CHECK_NA_ERROR(error, ret, "na_test_set_config() failed (%s)",
        NA_Error_to_string(ret));

    return NA_SUCCESS;

error:
    if (self_addr != NULL)
        (void) NA_Addr_free(na_class, self_addr);

    return ret;
}

/*---------------------------------------------------------------------------*/
na_return_t
NA_Test_init(int argc, char *argv[], struct na_test_info *na_test_info)
{
    char *info_string = NULL;
    struct na_init_info na_init_info = NA_INIT_INFO_INITIALIZER;
    na_return_t ret = NA_SUCCESS;
    const char *log_subsys = getenv("HG_LOG_SUBSYS");
#ifdef HG_TEST_HAS_CXI
    char auth_key[64];
#endif
    size_t i;

    if (!log_subsys) {
        const char *log_level = getenv("HG_LOG_LEVEL");

        /* Set log level */
        if (!log_level)
            log_level = "warning";

        /* Set global log level */
        NA_Set_log_level(log_level);
        HG_Util_set_log_level(log_level);
    }

    na_test_parse_options(argc, argv, na_test_info);

    /* Test run in parallel using mpirun so must intialize MPI to get
     * basic setup info etc */
    ret = na_test_mpi_init(&na_test_info->mpi_info, na_test_info->listen,
        na_test_info->use_threads, na_test_info->mpi_static);
    NA_TEST_CHECK_NA_ERROR(error, ret, "na_test_mpi_init() failed");

#ifdef HG_TEST_HAS_CXI
    if (na_test_info->key != NULL) {
        ret = na_test_alloc_svc(&na_test_info->cxi_info, na_test_info->key);
        NA_TEST_CHECK_NA_ERROR(error, ret, "na_test_alloc_svc() failed");

        ret = na_test_print_svc(
            &na_test_info->cxi_info, auth_key, sizeof(auth_key));
        NA_TEST_CHECK_NA_ERROR(error, ret, "na_test_print_svc() failed");
    }
#endif

    if (na_test_info->max_classes == 0)
        na_test_info->max_classes = 1;

    /* Call cleanup before doing anything */
    if (na_test_info->listen && na_test_info->mpi_info.rank == 0)
        NA_Cleanup();

    if (na_test_info->busy_wait) {
        na_init_info.progress_mode = NA_NO_BLOCK;
        if (na_test_info->mpi_info.rank == 0)
            printf("# Initializing NA in busy wait mode\n");
    }
#ifdef HG_TEST_HAS_CXI
    if (na_test_info->key != NULL)
        na_init_info.auth_key = auth_key;
#else
    na_init_info.auth_key = na_test_info->key;
#endif
    if (na_test_info->max_contexts != 0)
        na_init_info.max_contexts = na_test_info->max_contexts;
    na_init_info.max_unexpected_size = (size_t) na_test_info->max_msg_size;
    na_init_info.max_expected_size = (size_t) na_test_info->max_msg_size;
    na_init_info.thread_mode =
        na_test_info->use_threads ? 0 : NA_THREAD_MODE_SINGLE;
    if (na_test_info->tclass != NULL) {
        na_init_info.traffic_class = na_test_tclass(na_test_info->tclass);
        NA_TEST_CHECK_ERROR(na_init_info.traffic_class == NA_TC_UNSPEC, error,
            ret, NA_PROTONOSUPPORT, "Unsupported traffic class");
        if (na_test_info->mpi_info.rank == 0)
            printf("# Using traffic class: %s\n", na_test_info->tclass);
    }

    na_test_info->na_classes = (na_class_t **) malloc(
        sizeof(na_class_t *) * na_test_info->max_classes);
    NA_TEST_CHECK_ERROR(na_test_info->na_classes == NULL, error, ret, NA_NOMEM,
        "Could not allocate NA classes");

    for (i = 0; i < na_test_info->max_classes; i++) {
        /* Generate NA init string and get config options */
        info_string = na_test_gen_config(na_test_info,
            (unsigned int) (i + (unsigned int) na_test_info->mpi_info.rank *
                                    na_test_info->max_classes));
        NA_TEST_CHECK_ERROR(info_string == NULL, error, ret, NA_PROTOCOL_ERROR,
            "Could not generate config string");

        if (na_test_info->mpi_info.rank == 0)
            printf("# Class %zu using info string: %s\n", i + 1, info_string);

        na_test_info->na_classes[i] =
            NA_Initialize_opt2(info_string, na_test_info->listen,
                NA_VERSION(NA_VERSION_MAJOR, NA_VERSION_MINOR), &na_init_info);
        NA_TEST_CHECK_ERROR(na_test_info->na_classes[i] == NULL, error, ret,
            NA_PROTOCOL_ERROR, "NA_Initialize_opt2(%s) failed", info_string);

        free(info_string);
        info_string = NULL;
    }
    na_test_info->na_class = na_test_info->na_classes[0]; /* default */

    if (na_test_info->listen && !na_test_info->extern_init) {
        for (i = 0; i < na_test_info->max_classes; i++) {
            ret = na_test_self_addr_publish(
                na_test_info->hostfile, na_test_info->na_classes[i], i > 0);
            NA_TEST_CHECK_NA_ERROR(
                error, ret, "na_test_self_addr_publish() failed");
        }

        /* If static client must wait for server to write config file */
        if (na_test_info->mpi_static)
            na_test_mpi_barrier_world();
    }
    /* Get config from file if self option is not passed */
    else if (!na_test_info->listen && !na_test_info->self_send) {
        /* If static client must wait for server to write config file */
        if (na_test_info->mpi_static)
            na_test_mpi_barrier_world();

        if (na_test_info->mpi_info.rank == 0) {
            size_t count = 0;

            ret = na_test_get_config(na_test_info->hostfile, NULL, &count);
            NA_TEST_CHECK_NA_ERROR(error, ret,
                "na_test_get_config() failed (%s)", NA_Error_to_string(ret));
            NA_TEST_CHECK_ERROR(count > UINT32_MAX, error, ret, NA_OVERFLOW,
                "Exceeded maximum number of targets (%zu)", count);
            na_test_info->max_targets = (uint32_t) count;
        }

        if (na_test_info->mpi_info.size > 1)
            na_test_mpi_bcast(&na_test_info->mpi_info,
                &na_test_info->max_targets, sizeof(uint32_t), 0);

        na_test_info->target_names = (char **) malloc(
            na_test_info->max_targets * sizeof(*na_test_info->target_names));
        NA_TEST_CHECK_ERROR(na_test_info->target_names == NULL, error, ret,
            NA_NOMEM, "Could not allocated target name array");

        if (na_test_info->mpi_info.rank == 0) {
            size_t count = na_test_info->max_targets;

            ret = na_test_get_config(
                na_test_info->hostfile, na_test_info->target_names, &count);
            NA_TEST_CHECK_NA_ERROR(error, ret,
                "na_test_get_config() failed (%s)", NA_Error_to_string(ret));
        }

        if (na_test_info->mpi_info.size > 1) {
            uint32_t j;

            for (j = 0; j < na_test_info->max_targets; j++) {
                char test_addr_name[NA_TEST_MAX_ADDR_NAME];

                if (na_test_info->mpi_info.rank == 0)
                    strcpy(test_addr_name, na_test_info->target_names[j]);

                na_test_mpi_bcast(&na_test_info->mpi_info, test_addr_name,
                    NA_TEST_MAX_ADDR_NAME, 0);

                if (na_test_info->mpi_info.rank != 0) {
                    na_test_info->target_names[j] = strdup(test_addr_name);
                    NA_TEST_CHECK_ERROR(na_test_info->target_names[j] == NULL,
                        error, ret, NA_NOMEM, "strdup() of target_name failed");
                }
            }
        }

        na_test_info->target_name = na_test_info->target_names[0];
        if (na_test_info->mpi_info.rank == 0) {
            uint32_t j;

            printf("# %" PRIu32 " target name(s) read:\n",
                na_test_info->max_targets);
            for (j = 0; j < na_test_info->max_targets; j++)
                printf("# - %" PRIu32 "/%" PRIu32 ": %s\n", j + 1,
                    na_test_info->max_targets, na_test_info->target_names[j]);
        }
    }

    return NA_SUCCESS;

error:
    free(info_string);
    (void) NA_Test_finalize(na_test_info);

    return ret;
}

/*---------------------------------------------------------------------------*/
na_return_t
NA_Test_finalize(struct na_test_info *na_test_info)
{
    na_return_t ret = NA_SUCCESS;

    if (na_test_info->na_classes != NULL) {
        size_t i;

        for (i = 0; i < na_test_info->max_classes; i++) {
            ret = NA_Finalize(na_test_info->na_classes[i]);
            NA_TEST_CHECK_NA_ERROR(done, ret, "NA_Finalize() failed (%s)",
                NA_Error_to_string(ret));
        }
        free(na_test_info->na_classes);
        na_test_info->na_classes = NULL;
        na_test_info->na_class = NULL;
    }

    if (na_test_info->target_names != NULL) {
        uint32_t i;

        for (i = 0; i < na_test_info->max_targets; i++)
            free(na_test_info->target_names[i]);
        free(na_test_info->target_names);
        na_test_info->target_names = NULL;
        na_test_info->target_name = NULL;
    }

    if (na_test_info->comm != NULL) {
        free(na_test_info->comm);
        na_test_info->comm = NULL;
    }
    if (na_test_info->protocol != NULL) {
        free(na_test_info->protocol);
        na_test_info->protocol = NULL;
    }
    if (na_test_info->hostname != NULL) {
        free(na_test_info->hostname);
        na_test_info->hostname = NULL;
    }
    if (na_test_info->domain != NULL) {
        free(na_test_info->domain);
        na_test_info->domain = NULL;
    }
    if (na_test_info->key != NULL) {
#ifdef HG_TEST_HAS_CXI
        (void) na_test_free_svc(&na_test_info->cxi_info);
#endif
        free(na_test_info->key);
        na_test_info->key = NULL;
    }
    if (na_test_info->hostfile != NULL) {
        free(na_test_info->hostfile);
        na_test_info->hostfile = NULL;
    }
    if (na_test_info->tclass != NULL) {
        free(na_test_info->tclass);
        na_test_info->tclass = NULL;
    }

    na_test_mpi_finalize(&na_test_info->mpi_info);

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
void
NA_Test_barrier(const struct na_test_info *na_test_info)
{
    if (na_test_info->mpi_info.size > 1)
        na_test_mpi_barrier(&na_test_info->mpi_info);
}

/*---------------------------------------------------------------------------*/
void
NA_Test_bcast(
    void *buf, size_t size, int root, const struct na_test_info *na_test_info)
{
    if (na_test_info->mpi_info.size > 1)
        na_test_mpi_bcast(&na_test_info->mpi_info, buf, size, root);
}
