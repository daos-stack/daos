/**
 * Copyright (c) 2013-2021 UChicago Argonne, LLC and The HDF Group.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mchecksum_plugin.h"

#include "mchecksum_error.h"

#include <stdlib.h>
#include <string.h>

/****************/
/* Local Macros */
/****************/

/************************************/
/* Local Type and Struct Definition */
/************************************/

/********************/
/* Local Prototypes */
/********************/

/* Plugin class table */
/* clang-format off */
static const struct mchecksum_ops *const mchecksum_ops_table_g[] = {
    &MCHECKSUM_PLUGIN_OPS(crc16),
    &MCHECKSUM_PLUGIN_OPS(crc32c),
    &MCHECKSUM_PLUGIN_OPS(crc64),
#ifdef MCHECKSUM_HAS_ZLIB
    &MCHECKSUM_PLUGIN_OPS(crc32),
    &MCHECKSUM_PLUGIN_OPS(adler32),
#endif
    NULL};
/* clang-format on */

/*---------------------------------------------------------------------------*/
int
mchecksum_init(const char *hash_method, mchecksum_object_t *checksum_p)
{
    struct mchecksum_object *object = NULL;
    int rc, i;

    object = (struct mchecksum_object *) malloc(sizeof(*object));
    MCHECKSUM_CHECK_ERROR(
        object == NULL, error, rc, -1, "Could not allocate checksum class");

    for (i = 0; mchecksum_ops_table_g[i] != NULL; i++)
        if (strcmp(hash_method, mchecksum_ops_table_g[i]->name) == 0)
            break;

    object->ops = mchecksum_ops_table_g[i];
    MCHECKSUM_CHECK_ERROR(object->ops == NULL, error, rc, -1,
        "Unknown hash method (%s)", hash_method);

    rc = object->ops->init(&object->data);
    MCHECKSUM_CHECK_RC_ERROR(error, rc, "Could not initialize checksum");

    *checksum_p = object;

    return 0;

error:
    free(object);

    return rc;
}

/*---------------------------------------------------------------------------*/
void
mchecksum_destroy(mchecksum_object_t checksum)
{
    struct mchecksum_object *object = (struct mchecksum_object *) checksum;

    if (object == NULL)
        return;

    object->ops->destroy(object->data);
    free(object);
}

/*---------------------------------------------------------------------------*/
int
mchecksum_reset(mchecksum_object_t checksum)
{
    struct mchecksum_object *object = (struct mchecksum_object *) checksum;
    int rc;

    MCHECKSUM_CHECK_ERROR(
        object == NULL, error, rc, -1, "Checksum not initialized");

    object->ops->reset(object->data);

    return 0;

error:
    return rc;
}

/*---------------------------------------------------------------------------*/
size_t
mchecksum_get_size(mchecksum_object_t checksum)
{
    struct mchecksum_object *object = (struct mchecksum_object *) checksum;
    size_t rc;

    MCHECKSUM_CHECK_ERROR(
        object == NULL, error, rc, 0, "Checksum not initialized");

    return object->ops->get_size(object->data);

error:
    return rc;
}

/*---------------------------------------------------------------------------*/
int
mchecksum_get(mchecksum_object_t checksum, void *buf, size_t size, int finalize)
{
    struct mchecksum_object *object = (struct mchecksum_object *) checksum;
    int rc;

    MCHECKSUM_CHECK_ERROR(
        object == NULL, error, rc, -1, "Checksum not initialized");

    return object->ops->get(object->data, buf, size, finalize);

error:
    return rc;
}

/*---------------------------------------------------------------------------*/
int
mchecksum_update(mchecksum_object_t checksum, const void *buf, size_t size)
{
    struct mchecksum_object *object = (struct mchecksum_object *) checksum;
    int rc;

    MCHECKSUM_CHECK_ERROR(
        object == NULL, error, rc, -1, "Checksum not initialized");

    object->ops->update(object->data, buf, size);

    return 0;

error:
    return rc;
}
