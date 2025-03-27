/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mercury_test.h"

#include "mercury_atomic.h"
#include "mercury_thread.h"

#define HG_TEST_MAX_ADDR_LEN 256

struct hg_test_lookup_arg {
    hg_class_t *hg_class;
    hg_context_t *context;
    hg_id_t id;
    hg_addr_t addr;
};

static unsigned int signaled = 0;

extern const char *
na_test_gen_config(int argc, char *argv[], int listen);

static hg_return_t
hg_test_forward_cb(const struct hg_cb_info *info)
{
    struct hg_test_lookup_arg *arg = (struct hg_test_lookup_arg *) info->arg;
    signaled++;

    HG_Destroy(info->info.forward.handle);
    HG_Addr_free(arg->hg_class, arg->addr);
    return HG_SUCCESS;
}

static hg_return_t
hg_test_lookup_cb(const struct hg_cb_info *info)
{
    struct hg_test_lookup_arg *arg = (struct hg_test_lookup_arg *) info->arg;
    hg_handle_t handle;

    arg->addr = info->info.lookup.addr;
    HG_Create(arg->context, arg->addr, arg->id, &handle);

    HG_Forward(handle, hg_test_forward_cb, arg, NULL);

    return HG_SUCCESS;
}

static hg_return_t
hg_test_signal_cb(hg_handle_t handle)
{
    HG_Respond(handle, NULL, NULL, NULL);
    HG_Destroy(handle);
    return HG_SUCCESS;
}

/**
 *
 */
int
main(int argc, char *argv[])
{
    hg_class_t *hg_class1, *hg_class2;
    hg_context_t *context1, *context2;
    hg_addr_t addr1, addr2;
    char *na_info_string1, *na_info_string2;
    char addr1_str[HG_TEST_MAX_ADDR_LEN] = {"\0"};
    char addr2_str[HG_TEST_MAX_ADDR_LEN] = {"\0"};
    size_t addr_str_len = HG_TEST_MAX_ADDR_LEN;
    hg_bool_t listen = HG_TRUE;
    hg_id_t id = 1;
    struct hg_test_lookup_arg arg1, arg2;
    //    hg_return_t ret = HG_SUCCESS;

    /* Generate info strings */
    na_info_string1 = strdup(na_test_gen_config(argc, argv, listen));
    //    HG_TEST_LOG_WARNING("%s", na_info_string1);
    na_info_string2 = strdup(na_test_gen_config(argc, argv, listen));
    //    HG_TEST_LOG_WARNING("%s", na_info_string2);

    hg_class1 = HG_Init(na_info_string1, listen);
    hg_class2 = HG_Init(na_info_string2, listen);

    HG_Register(hg_class1, id, NULL, NULL, hg_test_signal_cb);
    HG_Register(hg_class2, id, NULL, NULL, hg_test_signal_cb);

    HG_Addr_self(hg_class1, &addr1);
    HG_Addr_to_string(hg_class1, addr1_str, &addr_str_len, addr1);

    HG_Addr_self(hg_class2, &addr2);
    HG_Addr_to_string(hg_class2, addr2_str, &addr_str_len, addr2);

    context1 = HG_Context_create(hg_class1);
    context2 = HG_Context_create(hg_class2);

    arg1.hg_class = hg_class1;
    arg1.context = context1;
    arg1.id = id;
    HG_Addr_lookup(
        context1, hg_test_lookup_cb, &arg1, addr2_str, HG_OP_ID_IGNORE);

    arg2.hg_class = hg_class2;
    arg2.context = context2;
    arg2.id = id;
    HG_Addr_lookup(
        context2, hg_test_lookup_cb, &arg2, addr1_str, HG_OP_ID_IGNORE);

    while (signaled < 2) {
        HG_Progress(context1, 0);
        HG_Progress(context2, 0);

        HG_Trigger(context1, 0, 1, NULL);
        HG_Trigger(context2, 0, 1, NULL);
    }

    if (signaled != 2)
        HG_TEST_LOG_ERROR("signaled: %d", signaled);

    HG_Context_destroy(context1);
    HG_Context_destroy(context2);

    HG_Addr_free(hg_class1, addr1);
    HG_Addr_free(hg_class2, addr2);

    HG_Finalize(hg_class1);
    HG_Finalize(hg_class2);

    free(na_info_string1);
    free(na_info_string2);

    return EXIT_SUCCESS;
}
