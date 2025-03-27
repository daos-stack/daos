/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mercury_proc_string.h"

#include <stdlib.h>

/****************/
/* Local Macros */
/****************/

/************************************/
/* Local Type and Struct Definition */
/************************************/

/********************/
/* Local Prototypes */
/********************/

/*******************/
/* Local Variables */
/*******************/

/*---------------------------------------------------------------------------*/
hg_return_t
hg_proc_hg_string_object_t(hg_proc_t proc, void *string)
{
    uint64_t string_len = 0;
    hg_return_t ret = HG_SUCCESS;
    hg_string_object_t *strobj = (hg_string_object_t *) string;

    switch (hg_proc_get_op(proc)) {
        case HG_ENCODE:
            string_len = (strobj->data) ? strlen(strobj->data) + 1 : 0;
            ret = hg_proc_uint64_t(proc, &string_len);
            if (ret != HG_SUCCESS)
                goto done;
            if (string_len) {
                ret = hg_proc_bytes(proc, strobj->data, string_len);
                if (ret != HG_SUCCESS)
                    goto done;
                ret = hg_proc_uint8_t(proc, (uint8_t *) &strobj->is_const);
                if (ret != HG_SUCCESS)
                    goto done;
                ret = hg_proc_uint8_t(proc, (uint8_t *) &strobj->is_owned);
                if (ret != HG_SUCCESS)
                    goto done;
            }
            break;
        case HG_DECODE:
            ret = hg_proc_uint64_t(proc, &string_len);
            if (ret != HG_SUCCESS)
                goto done;
            if (string_len) {
                strobj->data = (char *) malloc(string_len);
                if (strobj->data == NULL) {
                    ret = HG_NOMEM;
                    goto done;
                }
                ret = hg_proc_bytes(proc, strobj->data, string_len);
                if (ret != HG_SUCCESS) {
                    free(strobj->data);
                    strobj->data = NULL;
                    goto done;
                }
                ret = hg_proc_uint8_t(proc, (uint8_t *) &strobj->is_const);
                if (ret != HG_SUCCESS) {
                    free(strobj->data);
                    strobj->data = NULL;
                    goto done;
                }
                ret = hg_proc_uint8_t(proc, (uint8_t *) &strobj->is_owned);
                if (ret != HG_SUCCESS) {
                    free(strobj->data);
                    strobj->data = NULL;
                    goto done;
                }
            } else
                strobj->data = NULL;
            break;
        case HG_FREE:
            ret = hg_string_object_free(strobj);
            if (ret != HG_SUCCESS)
                goto done;
            break;
        default:
            break;
    }

done:
    return ret;
}
