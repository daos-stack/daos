/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mercury_test.h"

#include "mercury_util.h"
#include "na_test_getopt.h"

#ifdef _WIN32
#    include <Windows.h>
#else
#    include <unistd.h>
#endif

/****************/
/* Local Macros */
/****************/

/************************************/
/* Local Type and Struct Definition */
/************************************/

/********************/
/* Local Prototypes */
/********************/

static void
hg_test_usage(const char *execname);

void
hg_test_parse_options(
    int argc, char *argv[], struct hg_test_info *hg_test_info);

static hg_return_t
hg_test_self_addr_publish(
    const char *hostfile, hg_class_t *hg_class, bool append);

/*******************/
/* Local Variables */
/*******************/

extern int na_test_opt_ind_g;         /* token pointer */
extern const char *na_test_opt_arg_g; /* flag argument (or value) */
extern const char *na_test_short_opt_g;
extern const struct na_test_opt na_test_opt_g[];

/* Default log outlets */
#ifdef _WIN32
HG_LOG_OUTLET_DECL(hg_test) = HG_LOG_OUTLET_INITIALIZER(
    hg_test, HG_LOG_PASS, NULL, NULL);
#else
HG_LOG_DECL_REGISTER(hg_test);
#endif

/*---------------------------------------------------------------------------*/
static void
hg_test_usage(const char *execname)
{
    na_test_usage(execname);
    printf("    HG OPTIONS\n");
    printf("    -x, --handle        Max number of handles\n");
    printf("    -m, --memory        Use shared-memory with local targets\n");
    printf("    -t, --threads       Number of server threads\n");
    printf("    -B, --bidirectional Bidirectional communication\n");
    printf("    -u, --mrecv-ops     Number of multi-recv ops (server only)\n");
    printf("    -i, --post-init     Number of handles posted (server only)\n");
}

/*---------------------------------------------------------------------------*/
void
hg_test_parse_options(int argc, char *argv[], struct hg_test_info *hg_test_info)
{
    int opt;

    /* Parse pre-init info */
    if (argc < 2) {
        hg_test_usage(argv[0]);
        exit(1);
    }

    while ((opt = na_test_getopt(
                argc, argv, na_test_short_opt_g, na_test_opt_g)) != EOF) {
        switch (opt) {
            case 'h':
                hg_test_usage(argv[0]);
                exit(1);
            case 'm': /* memory */
                hg_test_info->auto_sm = HG_TRUE;
                break;
            case 't': /* number of threads */
                hg_test_info->thread_count =
                    (unsigned int) atoi(na_test_opt_arg_g);
                break;
            case 'x': /* number of handles */
                hg_test_info->handle_max =
                    (unsigned int) atoi(na_test_opt_arg_g);
                break;
            case 'B': /* bidirectional */
                hg_test_info->bidirectional = HG_TRUE;
                break;
            case 'u': /* multi_recv_op_max */
                hg_test_info->multi_recv_op_max =
                    (unsigned int) atoi(na_test_opt_arg_g);
                break;
            case 'i': /* request_post_init */
                hg_test_info->request_post_init =
                    (unsigned int) atoi(na_test_opt_arg_g);
                break;
            default:
                break;
        }
    }
    na_test_opt_ind_g = 1;

    /* Set defaults */
    if (hg_test_info->thread_count == 0) {
#ifdef _WIN32
        long int cpu_count = 2;
#else
        /* Try to guess */
        long int cpu_count = sysconf(_SC_NPROCESSORS_CONF);
#endif

        hg_test_info->thread_count = (cpu_count > 0)
                                         ? (unsigned int) cpu_count
                                         : HG_TEST_NUM_THREADS_DEFAULT;
    }
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_test_self_addr_publish(
    const char *hostfile, hg_class_t *hg_class, bool append)
{
    char addr_string[NA_TEST_MAX_ADDR_NAME];
    hg_size_t addr_string_len = NA_TEST_MAX_ADDR_NAME;
    hg_addr_t self_addr = HG_ADDR_NULL;
    hg_return_t ret;
    na_return_t na_ret;

    ret = HG_Addr_self(hg_class, &self_addr);
    HG_TEST_CHECK_HG_ERROR(
        error, ret, "HG_Addr_self() failed (%s)", HG_Error_to_string(ret));

    ret = HG_Addr_to_string(hg_class, addr_string, &addr_string_len, self_addr);
    HG_TEST_CHECK_HG_ERROR(
        error, ret, "HG_Addr_to_string() failed (%s)", HG_Error_to_string(ret));

    ret = HG_Addr_free(hg_class, self_addr);
    HG_TEST_CHECK_HG_ERROR(
        error, ret, "HG_Addr_free() failed (%s)", HG_Error_to_string(ret));
    self_addr = HG_ADDR_NULL;

    na_ret = na_test_set_config(hostfile, addr_string, append);
    NA_TEST_CHECK_ERROR(na_ret != NA_SUCCESS, error, ret, (hg_return_t) na_ret,
        "na_test_set_config() failed (%s)", NA_Error_to_string(na_ret));

    return HG_SUCCESS;

error:
    if (self_addr != HG_ADDR_NULL)
        (void) HG_Addr_free(hg_class, self_addr);

    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Test_init(int argc, char *argv[], struct hg_test_info *hg_test_info)
{
    hg_return_t ret = HG_SUCCESS;
    na_return_t na_ret;
    const char *log_subsys = getenv("HG_LOG_SUBSYS");
    size_t i;

    if (!log_subsys) {
        const char *log_level = getenv("HG_LOG_LEVEL");

        /* Set log level */
        if (!log_level)
            log_level = "warning";

        /* Set global log level */
        HG_Set_log_level(log_level);
        HG_Set_log_subsys("hg,hg_test");
        HG_Util_set_log_level(log_level);
    }

    /* Get HG test options */
    hg_test_parse_options(argc, argv, hg_test_info);

    /* Initialize NA test layer */
    hg_test_info->na_test_info.extern_init = true;
    na_ret = NA_Test_init(argc, argv, &hg_test_info->na_test_info);
    HG_TEST_CHECK_ERROR(na_ret != NA_SUCCESS, error, ret, (hg_return_t) na_ret,
        "NA_Test_init() failed (%s)", NA_Error_to_string(na_ret));

    hg_test_info->hg_classes = (hg_class_t **) malloc(
        sizeof(hg_class_t *) * hg_test_info->na_test_info.max_classes);
    HG_TEST_CHECK_ERROR(hg_test_info->hg_classes == NULL, error, ret, HG_NOMEM,
        "Could not allocate array of HG classes");

    for (i = 0; i < hg_test_info->na_test_info.max_classes; i++) {
        struct hg_init_info hg_init_info = HG_INIT_INFO_INITIALIZER;

        /* Set progress mode */
        if (hg_test_info->na_test_info.busy_wait)
            hg_init_info.na_init_info.progress_mode = NA_NO_BLOCK;

        /* Set max contexts */
        if (hg_test_info->na_test_info.max_contexts)
            hg_init_info.na_init_info.max_contexts =
                hg_test_info->na_test_info.max_contexts;

        /* Set auto SM mode */
        hg_init_info.auto_sm = hg_test_info->auto_sm;

        /* Assign NA class */
        hg_init_info.na_class = hg_test_info->na_test_info.na_classes[i];

        /* Multi-recv */
        hg_init_info.no_multi_recv = hg_test_info->na_test_info.no_multi_recv;
        hg_init_info.multi_recv_op_max = hg_test_info->multi_recv_op_max;

        /* Post init */
        hg_init_info.request_post_init = hg_test_info->request_post_init;

        /* Init HG with init options */
        hg_test_info->hg_classes[i] =
            HG_Init_opt2(NULL, hg_test_info->na_test_info.listen,
                HG_VERSION(HG_VERSION_MAJOR, HG_VERSION_MINOR), &hg_init_info);
        HG_TEST_CHECK_ERROR(hg_test_info->hg_classes[i] == NULL, error, ret,
            HG_FAULT, "HG_Init_opt2() failed");
    }
    hg_test_info->hg_class = hg_test_info->hg_classes[0]; /* default */

    if (hg_test_info->na_test_info.listen) {
        int j;
        for (j = 0; j < hg_test_info->na_test_info.mpi_info.size; j++) {
            if (hg_test_info->na_test_info.mpi_info.rank == j) {
                for (i = 0; i < hg_test_info->na_test_info.max_classes; i++) {
                    ret = hg_test_self_addr_publish(
                        hg_test_info->na_test_info.hostfile,
                        hg_test_info->hg_classes[i],
                        i > 0 || hg_test_info->na_test_info.mpi_info.rank != 0);
                    HG_TEST_CHECK_HG_ERROR(
                        error, ret, "hg_test_self_addr_publish() failed");
                }
            }
            NA_Test_barrier(&hg_test_info->na_test_info);
        }
        /* If static client, must wait for server to write config file */
        if (hg_test_info->na_test_info.mpi_static)
            na_test_mpi_barrier_world();
    }

    return HG_SUCCESS;

error:
    (void) HG_Test_finalize(hg_test_info);

    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Test_finalize(struct hg_test_info *hg_test_info)
{
    hg_return_t ret = HG_SUCCESS;
    na_return_t na_ret;
    size_t i;

    /* Finalize interface */
    if (hg_test_info->hg_classes != NULL) {
        for (i = 0; i < hg_test_info->na_test_info.max_classes; i++) {
            ret = HG_Finalize(hg_test_info->hg_classes[i]);
            HG_TEST_CHECK_HG_ERROR(done, ret, "HG_Finalize() failed (%s)",
                HG_Error_to_string(ret));
        }
        free(hg_test_info->hg_classes);
        hg_test_info->hg_classes = NULL;
        hg_test_info->hg_class = NULL;
    }

    /* Finalize NA test interface */
    na_ret = NA_Test_finalize(&hg_test_info->na_test_info);
    HG_TEST_CHECK_ERROR(na_ret != NA_SUCCESS, done, ret, (hg_return_t) na_ret,
        "NA_Test_finalize() failed (%s)", NA_Error_to_string(na_ret));

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
void
HG_Test_log_disable(void)
{
    /* Set global log level */
    HG_Set_log_level("none");
    HG_Util_set_log_level("none");
}

/*---------------------------------------------------------------------------*/
void
HG_Test_log_enable(void)
{
    const char *log_subsys = getenv("HG_LOG_SUBSYS");
    const char *log_level = getenv("HG_LOG_LEVEL");

    /* Set log level */
    if (!log_level)
        log_level = "warning";

    /* Reset global log level */
    HG_Set_log_level(log_level);
    HG_Util_set_log_level(log_level);

    /* Reset log subsys if any */
    if (log_subsys != NULL)
        HG_Set_log_subsys(log_subsys);
}
