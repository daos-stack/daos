/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mercury_proc.h"
#include "mercury_error.h"
#include "mercury_mem.h"

#ifdef HG_HAS_CHECKSUMS
#    include <mchecksum.h>
#endif
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
hg_proc_create(hg_class_t *hg_class, hg_proc_hash_t hash, hg_proc_t *proc_p)
{
    struct hg_proc *hg_proc = NULL;
#ifdef HG_HAS_CHECKSUMS
    const char *hash_method;
#endif
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(
        proc, hg_class == NULL, error, ret, HG_INVALID_ARG, "NULL HG class");

    hg_proc = (struct hg_proc *) calloc(1, sizeof(*hg_proc));
    HG_CHECK_SUBSYS_ERROR(
        proc, hg_proc == NULL, error, ret, HG_NOMEM, "Could not allocate proc");

    hg_proc->hg_class = hg_class;

#ifdef HG_HAS_CHECKSUMS
    /* Map enum to string */
    switch (hash) {
        case HG_CRC16:
            hash_method = "crc16";
            break;
        case HG_CRC32:
            hash_method = "crc32c";
            break;
        case HG_CRC64:
            hash_method = "crc64";
            break;
        default:
            hash_method = NULL;
            break;
    }

    if (hash_method) {
        int rc = mchecksum_init(hash_method, &hg_proc->checksum);
        HG_CHECK_SUBSYS_ERROR(proc, rc != 0, error, ret, HG_CHECKSUM_ERROR,
            "Could not initialize checksum");

        hg_proc->checksum_size = mchecksum_get_size(hg_proc->checksum);
        hg_proc->checksum_hash = (char *) malloc(hg_proc->checksum_size);
        HG_CHECK_SUBSYS_ERROR(proc, hg_proc->checksum_hash == NULL, error, ret,
            HG_NOMEM, "Could not allocate space for checksum hash");
    }
#else
    (void) hash;
#endif

    /* Default to proc_buf */
    hg_proc->current_buf = &hg_proc->proc_buf;

    *proc_p = (struct hg_proc *) hg_proc;

    return HG_SUCCESS;

error:
    if (hg_proc) {
#ifdef HG_HAS_CHECKSUMS
        mchecksum_destroy(hg_proc->checksum);
        free(hg_proc->checksum_hash);
#endif
        free(hg_proc);
    }
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
hg_proc_create_set(hg_class_t *hg_class, void *buf, hg_size_t buf_size,
    hg_proc_op_t op, hg_proc_hash_t hash, hg_proc_t *proc_p)
{
    hg_proc_t hg_proc = HG_PROC_NULL;
    hg_return_t ret;

    ret = hg_proc_create(hg_class, hash, &hg_proc);
    HG_CHECK_SUBSYS_HG_ERROR(proc, error, ret, "Could not create proc");

    ret = hg_proc_reset(hg_proc, buf, buf_size, op);
    HG_CHECK_SUBSYS_HG_ERROR(proc, error, ret, "Could not reset proc");

    *proc_p = hg_proc;

    return HG_SUCCESS;

error:
    if (hg_proc != HG_PROC_NULL)
        hg_proc_free(hg_proc);

    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
hg_proc_free(hg_proc_t proc)
{
    struct hg_proc *hg_proc = (struct hg_proc *) proc;

    if (!hg_proc)
        return HG_SUCCESS;

#ifdef HG_HAS_CHECKSUMS
    mchecksum_destroy(hg_proc->checksum);
    free(hg_proc->checksum_hash);
#endif

    /* Free extra proc buffer if needed */
    if (hg_proc->extra_buf.buf && hg_proc->extra_buf.is_mine)
        hg_mem_aligned_free(hg_proc->extra_buf.buf);

    /* Free proc */
    free(hg_proc);

    return HG_SUCCESS;
}

/*---------------------------------------------------------------------------*/
hg_return_t
hg_proc_reset(hg_proc_t proc, void *buf, hg_size_t buf_size, hg_proc_op_t op)
{
    struct hg_proc *hg_proc = (struct hg_proc *) proc;
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(
        proc, proc == HG_PROC_NULL, error, ret, HG_INVALID_ARG, "NULL HG proc");
    HG_CHECK_SUBSYS_ERROR(
        proc, !buf && op != HG_FREE, error, ret, HG_INVALID_ARG, "NULL buffer");

    hg_proc->op = op;
#ifdef HG_HAS_XDR
    switch (op) {
        case HG_ENCODE:
            xdrmem_create(&hg_proc->proc_buf.xdr, (char *) buf,
                (hg_uint32_t) buf_size, XDR_ENCODE);
            break;
        case HG_DECODE:
            xdrmem_create(&hg_proc->proc_buf.xdr, (char *) buf,
                (hg_uint32_t) buf_size, XDR_DECODE);
            break;
        case HG_FREE:
            xdrmem_create(&hg_proc->proc_buf.xdr, (char *) buf,
                (hg_uint32_t) buf_size, XDR_FREE);
            break;
        default:
            HG_GOTO_SUBSYS_ERROR(
                proc, error, ret, HG_INVALID_PARAM, "Unknown proc operation");
    }
#endif

    /* Reset flags */
    hg_proc->flags = 0;

    /* Reset proc buf */
    hg_proc->proc_buf.buf = buf;
    hg_proc->proc_buf.size = buf_size;
    hg_proc->proc_buf.buf_ptr = hg_proc->proc_buf.buf;
    hg_proc->proc_buf.size_left = hg_proc->proc_buf.size;

    /* Free extra proc buffer if needed */
    if (hg_proc->extra_buf.buf && hg_proc->extra_buf.is_mine)
        hg_mem_aligned_free(hg_proc->extra_buf.buf);
    hg_proc->extra_buf.buf = NULL;
    hg_proc->extra_buf.size = 0;
    hg_proc->extra_buf.buf_ptr = hg_proc->extra_buf.buf;
    hg_proc->extra_buf.size_left = hg_proc->extra_buf.size;

    /* Default to proc_buf */
    hg_proc->current_buf = &hg_proc->proc_buf;

#ifdef HG_HAS_CHECKSUMS
    /* Reset checksum */
    if (hg_proc->checksum != MCHECKSUM_OBJECT_NULL) {
        int rc = mchecksum_reset(hg_proc->checksum);
        HG_CHECK_SUBSYS_ERROR(proc, rc != 0, error, ret, HG_CHECKSUM_ERROR,
            "Could not reset checksum");
        memset(hg_proc->checksum_hash, 0, hg_proc->checksum_size);
    }
#endif

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
hg_proc_set_size(hg_proc_t proc, hg_size_t req_buf_size)
{
    struct hg_proc *hg_proc = (struct hg_proc *) proc;
    hg_size_t new_buf_size;
    hg_size_t page_size = (hg_size_t) hg_mem_get_page_size();
    void *new_buf = NULL;
    ptrdiff_t current_pos;
    bool allocated = false;
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(proc, proc == HG_PROC_NULL, error, ret,
        HG_INVALID_ARG, "Proc is not initialized");

    /* Save current position */
    current_pos = (char *) hg_proc->current_buf->buf_ptr -
                  (char *) hg_proc->current_buf->buf;

    /* Get one more page size buf */
    new_buf_size = ((hg_size_t) (req_buf_size / page_size) + 1) * page_size;
    HG_CHECK_SUBSYS_ERROR(proc, new_buf_size <= hg_proc_get_size(proc), error,
        ret, HG_INVALID_ARG, "Buffer is already of the size requested");

    /* If was not using extra buffer init extra buffer */
    if (!hg_proc->extra_buf.buf) {
        /* Allocate buffer */
        new_buf = hg_mem_aligned_alloc(page_size, new_buf_size);
        allocated = true;
    } else
        new_buf = realloc(hg_proc->extra_buf.buf, new_buf_size);
    HG_CHECK_SUBSYS_ERROR(proc, new_buf == NULL, error, ret, HG_NOMEM,
        "Could not allocate buffer of size %" PRIu64, new_buf_size);

    if (!hg_proc->extra_buf.buf) {
        /* Copy proc_buf (should be small) */
        memcpy(new_buf, hg_proc->proc_buf.buf, (size_t) current_pos);

        /* Switch buffer */
        hg_proc->current_buf = &hg_proc->extra_buf;
    }

    hg_proc->extra_buf.buf = new_buf;
    hg_proc->extra_buf.size = new_buf_size;
    hg_proc->extra_buf.buf_ptr = (char *) hg_proc->extra_buf.buf + current_pos;
    hg_proc->extra_buf.size_left =
        hg_proc->extra_buf.size - (hg_size_t) current_pos;
    hg_proc->extra_buf.is_mine = true;

    return HG_SUCCESS;

error:
    if (new_buf && allocated)
        hg_mem_aligned_free(new_buf);
    return ret;
}

/*---------------------------------------------------------------------------*/
void *
hg_proc_save_ptr(hg_proc_t proc, hg_size_t data_size)
{
    struct hg_proc *hg_proc = (struct hg_proc *) proc;
    void *ptr;
#ifdef HG_HAS_XDR
    unsigned int cur_pos;
#endif

    HG_CHECK_SUBSYS_ERROR_NORET(
        proc, proc == HG_PROC_NULL, error, "Proc is not initialized");
    HG_CHECK_SUBSYS_ERROR_NORET(
        proc, hg_proc->op == HG_FREE, error, "Cannot save_ptr on HG_FREE");

    /* If not enough space allocate extra space if encoding or
     * just get extra buffer if decoding */
    if (data_size && hg_proc->current_buf->size_left < data_size)
        hg_proc_set_size(
            proc, hg_proc->proc_buf.size + hg_proc->extra_buf.size + data_size);

    ptr = hg_proc->current_buf->buf_ptr;
    hg_proc->current_buf->buf_ptr =
        (char *) hg_proc->current_buf->buf_ptr + data_size;
    hg_proc->current_buf->size_left -= data_size;
#ifdef HG_HAS_XDR
    cur_pos = xdr_getpos(&hg_proc->current_buf->xdr);
    xdr_setpos(&hg_proc->current_buf->xdr, (hg_uint32_t) (cur_pos + data_size));
#endif

    return ptr;

error:
    return NULL;
}

/*---------------------------------------------------------------------------*/
hg_return_t
hg_proc_restore_ptr(hg_proc_t proc, void *data, hg_size_t data_size)
{
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(proc, proc == HG_PROC_NULL, error, ret,
        HG_INVALID_ARG, "Proc is not initialized");
    HG_CHECK_SUBSYS_ERROR(proc, ((struct hg_proc *) proc)->op == HG_FREE, error,
        ret, HG_INVALID_ARG, "Cannot restore_ptr on HG_FREE");

#ifdef HG_HAS_CHECKSUMS
    hg_proc_checksum_update(proc, data, data_size);
#else
    /* Silent warning */
    (void) data;
    (void) data_size;
#endif

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
hg_proc_set_extra_buf_is_mine(hg_proc_t proc, uint8_t theirs)
{
    struct hg_proc *hg_proc = (struct hg_proc *) proc;
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(proc, proc == HG_PROC_NULL, error, ret,
        HG_INVALID_ARG, "Proc is not initialized");
    HG_CHECK_SUBSYS_ERROR(proc, hg_proc->extra_buf.buf == NULL, error, ret,
        HG_INVALID_ARG, "Extra buf is not set");

    hg_proc->extra_buf.is_mine = (!theirs);

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
hg_proc_flush(hg_proc_t proc)
{
    hg_return_t ret;
#ifdef HG_HAS_CHECKSUMS
    struct hg_proc *hg_proc = (struct hg_proc *) proc;
    int rc;
#endif

    HG_CHECK_SUBSYS_ERROR(proc, proc == HG_PROC_NULL, error, ret,
        HG_INVALID_ARG, "Proc is not initialized");

#ifdef HG_HAS_CHECKSUMS
    if (hg_proc->checksum == MCHECKSUM_OBJECT_NULL)
        return HG_SUCCESS;

    rc = mchecksum_get(hg_proc->checksum, hg_proc->checksum_hash,
        hg_proc->checksum_size, MCHECKSUM_FINALIZE);
    HG_CHECK_SUBSYS_ERROR(
        proc, rc != 0, error, ret, HG_CHECKSUM_ERROR, "Could not get checksum");
#endif

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
#ifdef HG_HAS_CHECKSUMS
void
hg_proc_checksum_update(hg_proc_t proc, void *data, hg_size_t data_size)
{
    struct hg_proc *hg_proc = (struct hg_proc *) proc;
    int rc;

    if (hg_proc->checksum == MCHECKSUM_OBJECT_NULL)
        return;

    /* Update checksum */
    rc = mchecksum_update(((struct hg_proc *) proc)->checksum, data, data_size);
    HG_CHECK_SUBSYS_ERROR_NORET(
        proc, rc != 0, error, "Could not update checksum");

    return;

error:
    return;
}

/*---------------------------------------------------------------------------*/
hg_return_t
hg_proc_checksum_get(hg_proc_t proc, void *hash, hg_size_t hash_size)
{
    struct hg_proc *hg_proc = (struct hg_proc *) proc;
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(proc, proc == HG_PROC_NULL, error, ret,
        HG_INVALID_ARG, "Proc is not initialized");
    HG_CHECK_SUBSYS_ERROR(
        proc, hash == NULL, error, ret, HG_INVALID_ARG, "NULL hash pointer");
    HG_CHECK_SUBSYS_ERROR(proc, hg_proc->checksum_hash == NULL, error, ret,
        HG_INVALID_ARG, "Proc has no checksum hash");
    HG_CHECK_SUBSYS_ERROR(proc, hash_size < hg_proc->checksum_size, error, ret,
        HG_INVALID_ARG, "Hash size passed is too small");

    memcpy(hash, hg_proc->checksum_hash, hg_proc->checksum_size);

    return HG_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
hg_proc_checksum_verify(hg_proc_t proc, const void *hash, hg_size_t hash_size)
{
    struct hg_proc *hg_proc = (struct hg_proc *) proc;
    hg_return_t ret;

    HG_CHECK_SUBSYS_ERROR(proc, proc == HG_PROC_NULL, error, ret,
        HG_INVALID_ARG, "Proc is not initialized");
    HG_CHECK_SUBSYS_ERROR(
        proc, hash == NULL, error, ret, HG_INVALID_ARG, "NULL hash pointer");
    HG_CHECK_SUBSYS_ERROR(proc, hg_proc->checksum_hash == NULL, error, ret,
        HG_INVALID_ARG, "Proc has no checksum hash");
    HG_CHECK_SUBSYS_ERROR(proc, hash_size < hg_proc->checksum_size, error, ret,
        HG_INVALID_ARG, "Hash size passed is too small");

    /* Verify checksums */
    if (memcmp(hash, hg_proc->checksum_hash, hg_proc->checksum_size) != 0) {
        if (hg_proc->checksum_size == sizeof(uint16_t))
            HG_LOG_SUBSYS_ERROR(proc,
                "checksum 0x%04X does not match (expected 0x%04X!)",
                *(uint16_t *) hg_proc->checksum_hash, *(const uint16_t *) hash);
        else if (hg_proc->checksum_size == sizeof(uint32_t))
            HG_LOG_SUBSYS_ERROR(proc,
                "checksum 0x%08X does not match (expected 0x%08X!)",
                *(uint32_t *) hg_proc->checksum_hash, *(const uint32_t *) hash);
        else if (hg_proc->checksum_size == sizeof(uint64_t))
            HG_LOG_SUBSYS_ERROR(proc,
                "checksum 0x%016" PRIx64
                " does not match (expected 0x%016" PRIx64 "!)",
                *(uint64_t *) hg_proc->checksum_hash, *(const uint64_t *) hash);
        else
            HG_LOG_SUBSYS_ERROR(proc, "Checksums do not match (unknown size?)");
        return HG_CHECKSUM_ERROR;
    }

    return HG_SUCCESS;

error:
    return ret;
}
#endif
