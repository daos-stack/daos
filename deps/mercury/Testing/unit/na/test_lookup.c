/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "na_test_common.h"

#include "mercury_thread.h"

/****************/
/* Local Macros */
/****************/

/************************************/
/* Local Type and Struct Definition */
/************************************/

struct na_test_common_thread {
    struct na_test_common_class_info *class_info;
    char **target_names;
    hg_thread_t thread;
    size_t target_name_count;
    int thread_id;
};

/********************/
/* Local Prototypes */
/********************/

static na_return_t
na_test_send_all(struct na_test_common_class_info *info, char **target_names,
    size_t target_name_count, int thread_id);

/*******************/
/* Local Variables */
/*******************/

/*---------------------------------------------------------------------------*/
static HG_THREAD_RETURN_TYPE
na_test_send_thread(void *arg)
{
    struct na_test_common_thread *thread_arg =
        (struct na_test_common_thread *) arg;
    hg_thread_ret_t tret = (hg_thread_ret_t) 0;
    na_return_t na_ret;

    na_ret = na_test_send_all(thread_arg->class_info, thread_arg->target_names,
        thread_arg->target_name_count, thread_arg->thread_id);
    NA_TEST_CHECK_NA_ERROR(error, na_ret, "na_test_send() failed (%s)",
        NA_Error_to_string(na_ret));

error:
    hg_thread_exit(tret);
    return tret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_test_send_all(struct na_test_common_class_info *info, char **target_names,
    size_t target_name_count, int NA_UNUSED thread_id)
{
    na_return_t ret;
    int i, j, loop = 10;

    NA_TEST_LOG_DEBUG("Sending msg to %zu targets", target_name_count);

    for (j = 0; j < loop; j++) {
        for (i = 0; i < (int) target_name_count; i++) {
            na_addr_t *target_addr = NULL;

            NA_TEST_LOG_DEBUG(
                "(%d) Sending msg to %s", thread_id, target_names[i]);
            ret = NA_Addr_lookup(info->na_class, target_names[i], &target_addr);
            NA_TEST_CHECK_NA_ERROR(error, ret, "NA_Addr_lookup() failed (%s)",
                NA_Error_to_string(ret));

            /* Reset */
            hg_request_reset(info->request);

            /* Post one-way msg send */
            ret = NA_Msg_send_expected(info->na_class, info->context,
                na_test_common_request_complete, info->request,
                info->msg_unexp_buf, info->msg_unexp_header_size,
                info->msg_unexp_data, target_addr, 0,
                NA_TEST_COMMON_TAG_CONTINUE, info->msg_unexp_op_id);
            NA_TEST_CHECK_NA_ERROR(error, ret,
                "NA_Msg_send_unexpected() failed (%s)",
                NA_Error_to_string(ret));

            hg_request_wait(info->request, NA_MAX_IDLE_TIME, NULL);

            NA_Addr_free(info->na_class, target_addr);
        }
    }

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_test_finalize_all(struct na_test_common_class_info *info,
    char **target_names, size_t target_name_count)
{
    na_return_t ret;
    int i;

    for (i = 0; i < (int) target_name_count; i++) {
        na_addr_t *target_addr = NULL;

        ret = NA_Addr_lookup(info->na_class, target_names[i], &target_addr);
        NA_TEST_CHECK_NA_ERROR(error, ret, "NA_Addr_lookup() failed (%s)",
            NA_Error_to_string(ret));

        ret = na_test_common_send_finalize(info, target_addr);
        NA_TEST_CHECK_NA_ERROR(error, ret,
            "na_test_common_send_finalize() failed (%s)",
            NA_Error_to_string(ret));

        NA_Addr_free(info->na_class, target_addr);
    }

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
int
main(int argc, char *argv[])
{
    struct na_test_common_info info;
    na_return_t na_ret;
    struct na_test_common_thread *send_threads = NULL;
    int i;

    /* Initialize the interface */
    na_ret = na_test_common_init(argc, argv, false, &info);
    NA_TEST_CHECK_NA_ERROR(error, na_ret, "na_test_common_init() failed (%s)",
        NA_Error_to_string(na_ret));

    send_threads = (struct na_test_common_thread *) malloc(
        sizeof(*send_threads) * info.na_test_info.max_classes);
    NA_TEST_CHECK_ERROR_NORET(
        send_threads == NULL, error, "Could not allocate send threads");

    for (i = 0; i < (int) info.na_test_info.max_classes; i++) {
        int rc;

        send_threads[i].class_info = &info.class_info[i];
        send_threads[i].target_names = info.na_test_info.target_names;
        send_threads[i].target_name_count =
            (size_t) info.na_test_info.max_targets;
        send_threads[i].thread_id = i;

        rc = hg_thread_create(
            &send_threads[i].thread, na_test_send_thread, &send_threads[i]);
        NA_TEST_CHECK_ERROR_NORET(rc != 0, error, "hg_thread_create() failed");
    }

    for (i = 0; i < (int) info.na_test_info.max_classes; i++)
        hg_thread_join(send_threads[i].thread);

    /* Finalize interface */
    printf("Finalizing...\n");
    na_test_finalize_all(&info.class_info[0], info.na_test_info.target_names,
        info.na_test_info.max_targets);
    na_test_common_cleanup(&info);
    free(send_threads);

    return EXIT_SUCCESS;

error:
    na_test_common_cleanup(&info);
    free(send_threads);

    return EXIT_FAILURE;
}
