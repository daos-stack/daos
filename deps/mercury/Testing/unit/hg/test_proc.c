/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mercury_unit.h"

#include "mercury_proc.h"

#include "mercury_mem.h"

/****************/
/* Local Macros */
/****************/

/************************************/
/* Local Type and Struct Definition */
/************************************/

typedef struct {
    hg_uint8_t val8;
    hg_uint16_t val16;
    hg_uint32_t val32;
    hg_uint64_t val64;
} hg_test_proc_uint_t;

typedef struct {
    hg_const_string_t string;
} hg_test_proc_string_t;

/********************/
/* Local Prototypes */
/********************/

static hg_return_t
hg_proc_hg_test_proc_uint_t(hg_proc_t proc, void *data)
{
    hg_test_proc_uint_t *struct_data = (hg_test_proc_uint_t *) data;
    hg_return_t ret = HG_SUCCESS;

    ret = hg_proc_hg_uint8_t(proc, &struct_data->val8);
    if (ret != HG_SUCCESS)
        return ret;

    ret = hg_proc_hg_uint16_t(proc, &struct_data->val16);
    if (ret != HG_SUCCESS)
        return ret;

    ret = hg_proc_hg_uint32_t(proc, &struct_data->val32);
    if (ret != HG_SUCCESS)
        return ret;

    ret = hg_proc_hg_uint64_t(proc, &struct_data->val64);
    if (ret != HG_SUCCESS)
        return ret;

    return ret;
}

static hg_return_t
hg_proc_hg_test_proc_string_t(hg_proc_t proc, void *data)
{
    hg_test_proc_string_t *struct_data = (hg_test_proc_string_t *) data;
    hg_return_t ret = HG_SUCCESS;

    ret = hg_proc_hg_string_t(proc, &struct_data->string);
    if (ret != HG_SUCCESS)
        return ret;

    return ret;
}

/*******************/
/* Local Variables */
/*******************/

static hg_return_t
hg_test_proc_generic(
    hg_return_t (*proc_cb)(hg_proc_t proc, void *data), void *in, void *out)
{
    hg_proc_t proc = HG_PROC_NULL;
    void *in_buf = NULL, *out_buf = NULL;
    size_t buf_size = (size_t) hg_mem_get_page_size();
    hg_return_t ret;
#ifdef HG_HAS_CHECKSUMS
    hg_uint32_t checksum = 0;
#endif

    /* CRC32 is enough for small size buffers */
    ret = hg_proc_create((hg_class_t *) 1, HG_CRC32, &proc);
    HG_TEST_CHECK_HG_ERROR(done, ret, "Cannot create HG proc");

    in_buf = calloc(1, buf_size);
    HG_TEST_CHECK_ERROR(
        in_buf == NULL, done, ret, HG_NOMEM_ERROR, "Could not allocate buf");

    out_buf = calloc(1, buf_size);
    HG_TEST_CHECK_ERROR(
        out_buf == NULL, done, ret, HG_NOMEM_ERROR, "Could not allocate buf");

    /* Reset proc */
    ret = hg_proc_reset(proc, in_buf, buf_size, HG_ENCODE);
    HG_TEST_CHECK_HG_ERROR(done, ret, "Could not reset proc");

    ret = proc_cb(proc, in);
    HG_TEST_CHECK_HG_ERROR(done, ret, "Could not proc uint_t struct");

    /* Flush proc */
    ret = hg_proc_flush(proc);
    HG_TEST_CHECK_HG_ERROR(done, ret, "Error in proc flush");

#ifdef HG_HAS_CHECKSUMS
    /* Set checksum in header */
    ret = hg_proc_checksum_get(proc, &checksum, sizeof(checksum));
    HG_TEST_CHECK_HG_ERROR(done, ret, "Error in getting proc checksum");
#endif

    /* Simulate RPC copy */
    memcpy(out_buf, in_buf, buf_size);

    /* Reset proc */
    ret = hg_proc_reset(proc, out_buf, buf_size, HG_DECODE);
    HG_TEST_CHECK_HG_ERROR(done, ret, "Could not reset proc");

    ret = proc_cb(proc, out);
    HG_TEST_CHECK_HG_ERROR(done, ret, "Could not proc uint_t struct");

    /* Flush proc */
    ret = hg_proc_flush(proc);
    HG_TEST_CHECK_HG_ERROR(done, ret, "Error in proc flush");

#ifdef HG_HAS_CHECKSUMS
    /* Compare checksum with header hash */
    ret = hg_proc_checksum_verify(proc, &checksum, sizeof(checksum));
    HG_TEST_CHECK_HG_ERROR(done, ret, "Error in proc checksum verify");
#endif

done:
    if (proc != HG_PROC_NULL)
        hg_proc_free(proc);
    free(in_buf);
    free(out_buf);

    return ret;
}

static hg_return_t
hg_test_proc_free(
    hg_return_t (*proc_cb)(hg_proc_t proc, void *data), void *data)
{
    hg_proc_t proc = HG_PROC_NULL;
    void *buf = NULL;
    size_t buf_size = (size_t) hg_mem_get_page_size();
    hg_return_t ret;

    /* CRC32 is enough for small size buffers */
    ret = hg_proc_create((hg_class_t *) 1, HG_CRC32, &proc);
    HG_TEST_CHECK_HG_ERROR(done, ret, "Cannot create HG proc");

    buf = malloc(buf_size);
    HG_TEST_CHECK_ERROR(
        buf == NULL, done, ret, HG_NOMEM_ERROR, "Could not allocate buf");

    /* Reset proc */
    ret = hg_proc_reset(proc, buf, buf_size, HG_FREE);
    HG_TEST_CHECK_HG_ERROR(done, ret, "Could not reset proc");

    ret = proc_cb(proc, data);
    HG_TEST_CHECK_HG_ERROR(done, ret, "Could not proc uint_t struct");

    /* Flush proc */
    ret = hg_proc_flush(proc);
    HG_TEST_CHECK_HG_ERROR(done, ret, "Error in proc flush");

done:
    if (proc != HG_PROC_NULL)
        hg_proc_free(proc);
    free(buf);

    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_test_proc_uint(void)
{
    hg_return_t ret;
    hg_test_proc_uint_t in = {1, 2, 3, 4}, out = {0, 0, 0, 0};

    ret = hg_test_proc_generic(hg_proc_hg_test_proc_uint_t, &in, &out);
    HG_TEST_CHECK_HG_ERROR(done, ret, "hg_test_proc_generic() failed");

    HG_TEST_CHECK_ERROR(in.val8 != out.val8 && in.val16 != out.val16 &&
                            in.val32 != out.val32 && in.val64 != out.val64,
        done, ret, HG_PROTOCOL_ERROR,
        "Encoded and decoded values do not match");

    ret = hg_test_proc_free(hg_proc_hg_test_proc_uint_t, &out);
    HG_TEST_CHECK_HG_ERROR(done, ret, "hg_test_proc_free() failed");

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_test_proc_string(void)
{
    hg_return_t ret;
    hg_test_proc_string_t in = {"Hello"}, out = {"NULL"};

    ret = hg_test_proc_generic(hg_proc_hg_test_proc_string_t, &in, &out);
    HG_TEST_CHECK_HG_ERROR(done, ret, "hg_test_proc_generic() failed");

    HG_TEST_CHECK_ERROR(strcmp(in.string, out.string) != 0, done, ret,
        HG_PROTOCOL_ERROR,
        "Encoded and decoded strings do not match (%s != %s)", in.string,
        out.string);

    ret = hg_test_proc_free(hg_proc_hg_test_proc_string_t, &out);
    HG_TEST_CHECK_HG_ERROR(done, ret, "hg_test_proc_free() failed");

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
int
main(void)
{
    hg_return_t hg_ret;
    int ret = EXIT_SUCCESS;

    /* uint proc test */
    HG_TEST("uint proc");
    hg_ret = hg_test_proc_uint();
    HG_TEST_CHECK_ERROR(
        hg_ret != HG_SUCCESS, done, ret, EXIT_FAILURE, "uint proc test failed");
    HG_PASSED();

    /* string proc test */
    HG_TEST("string proc");
    hg_ret = hg_test_proc_string();
    HG_TEST_CHECK_ERROR(hg_ret != HG_SUCCESS, done, ret, EXIT_FAILURE,
        "string proc test failed");
    HG_PASSED();

done:
    if (ret != EXIT_SUCCESS)
        HG_FAILED();

    return ret;
}
