/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mercury_string_object.h"
#include "mercury_error.h"

#include <stdlib.h>
#include <string.h>

/****************/
/* Local Macros */
/****************/

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

/*******************/
/* Local Variables */
/*******************/

/*---------------------------------------------------------------------------*/
hg_return_t
hg_string_object_init(hg_string_object_t *string)
{
    hg_return_t ret = HG_SUCCESS;

    string->data = NULL;
    string->is_owned = 0;
    string->is_const = 0;

    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
hg_string_object_init_char(
    hg_string_object_t *string, char *s, uint8_t is_owned)
{
    hg_return_t ret = HG_SUCCESS;

    string->data = s;
    string->is_owned = is_owned;
    string->is_const = 0;

    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
hg_string_object_init_const_char(
    hg_string_object_t *string, const char *s, uint8_t is_owned)
{
    union {
        char *p;
        const char *const_p;
    } safe_string = {.const_p = s};
    hg_return_t ret = HG_SUCCESS;

    string->data = safe_string.p;
    string->is_owned = is_owned;
    string->is_const = 1;

    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
hg_string_object_free(hg_string_object_t *string)
{
    hg_return_t ret = HG_SUCCESS;

    if (string->is_owned) {
        free(string->data);
        string->data = NULL;
    }

    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
hg_string_object_dup(hg_string_object_t string, hg_string_object_t *new_string)
{
    hg_return_t ret = HG_SUCCESS;

    new_string->data = strdup(string.data);
    HG_CHECK_ERROR(new_string->data == NULL, done, ret, HG_NOMEM,
        "Could not dup string data");
    new_string->is_owned = 1;
    new_string->is_const = 0;

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
char *
hg_string_object_swap(hg_string_object_t *string, char *s)
{
    char *old = string->data;

    string->data = s;
    string->is_const = 0;
    string->is_owned = 0;

    return old;
}
