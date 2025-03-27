/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mercury_proc_bulk.h"
#include "mercury_bulk_proc.h"
#include "mercury_error.h"

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
hg_proc_hg_bulk_t(hg_proc_t proc, void *data)
{
    void *buf = NULL;
    hg_bulk_t *bulk_ptr = (hg_bulk_t *) data;
    uint64_t buf_size = 0;
    hg_return_t ret;

    switch (hg_proc_get_op(proc)) {
        case HG_ENCODE: {
            uint8_t flags = 0;
            bool try_eager = false; /* Flag will not be set if bulk handle does
                                       not support it */

            HG_LOG_SUBSYS_DEBUG(proc, "HG_ENCODE");

            /* If HG_BULK_NULL set 0 to buf_size */
            if (*bulk_ptr == HG_BULK_NULL) {
                /* Encode zero size */
                ret = hg_proc_uint64_t(proc, &buf_size);
                HG_CHECK_SUBSYS_HG_ERROR(
                    proc, error, ret, "Could not encode serialize size");
                break;
            }

#ifdef NA_HAS_SM
            /* Are we using SM routing */
            if (hg_proc_get_flags(proc) & HG_PROC_SM)
                flags |= HG_BULK_SM;
#endif

            /* Try to make everything fit in an eager buffer */
            if (hg_proc_get_flags(proc) & HG_PROC_BULK_EAGER) {
                HG_LOG_SUBSYS_DEBUG(proc, "Proc size left is %" PRIu64 " bytes",
                    hg_proc_get_size_left(proc));
                buf_size = HG_Bulk_get_serialize_size(
                    *bulk_ptr, HG_BULK_EAGER | flags);

                if (hg_proc_get_size_left(proc) >=
                    (buf_size + sizeof(uint64_t)))
                    try_eager = true;
            }
            if (try_eager) {
                HG_LOG_SUBSYS_DEBUG(proc, "HG_BULK_EAGER flag set");
                flags |= HG_BULK_EAGER;
            } else /* We must recompute the serialize size without eager flag */
                buf_size = HG_Bulk_get_serialize_size(*bulk_ptr, flags);

            HG_LOG_SUBSYS_DEBUG(
                proc, "Serialize size for bulk handle is %" PRIu64, buf_size);

            /* Encode size */
            ret = hg_proc_uint64_t(proc, &buf_size);
            HG_CHECK_SUBSYS_HG_ERROR(
                proc, error, ret, "Could not encode serialize size");

            if (buf_size == hg_bulk_get_serialize_cached_size(*bulk_ptr)) {
                HG_LOG_SUBSYS_DEBUG(
                    proc, "Using cached pointer to serialized handle");
                void *cached_ptr = hg_bulk_get_serialize_cached_ptr(*bulk_ptr);
                hg_proc_bytes(proc, cached_ptr, buf_size);
            } else {
                buf = hg_proc_save_ptr(proc, buf_size);
                ret = HG_Bulk_serialize(buf, buf_size, flags, *bulk_ptr);
                HG_CHECK_SUBSYS_HG_ERROR(
                    proc, error, ret, "Could not serialize handle");
                hg_proc_restore_ptr(proc, buf, buf_size);
            }
            break;
        }
        case HG_DECODE: {
            hg_class_t *hg_class = hg_proc_get_class(proc);

            HG_LOG_SUBSYS_DEBUG(proc, "HG_DECODE");

            /* Decode size */
            ret = hg_proc_uint64_t(proc, &buf_size);
            HG_CHECK_SUBSYS_HG_ERROR(
                proc, error, ret, "Could not decode serialize size");

            /* If buf_size is 0, define handle to HG_BULK_NULL */
            if (buf_size == 0) {
                *bulk_ptr = HG_BULK_NULL;
                break;
            }

            buf = hg_proc_save_ptr(proc, buf_size);
            ret = HG_Bulk_deserialize(hg_class, bulk_ptr, buf, buf_size);
            HG_CHECK_SUBSYS_HG_ERROR(
                proc, error, ret, "Could not deserialize handle");

            /* Cache serialize ptr to buf */
            HG_LOG_SUBSYS_DEBUG(proc,
                "Caching pointer to serialized bulk handle (%p, %" PRIu64 ")",
                buf, buf_size);
            hg_bulk_set_serialize_cached_ptr(*bulk_ptr, buf, buf_size);
            hg_proc_restore_ptr(proc, buf, buf_size);
            break;
        }
        case HG_FREE:
            HG_LOG_SUBSYS_DEBUG(proc, "HG_FREE");

            /* If bulk handle is HG_BULK_NULL, just return success */
            if (*bulk_ptr == HG_BULK_NULL)
                break;

            /* Set serialize ptr to NULL */
            hg_bulk_set_serialize_cached_ptr(*bulk_ptr, NULL, 0);

            /* Decrement refcount on bulk handle */
            ret = HG_Bulk_free(*bulk_ptr);
            HG_CHECK_SUBSYS_HG_ERROR(proc, error, ret, "Could not free handle");
            *bulk_ptr = HG_BULK_NULL;
            break;
        default:
            break;
    }

    return HG_SUCCESS;

error:
    return ret;
}
