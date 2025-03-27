/**
 * Copyright (c) 2013-2021 UChicago Argonne, LLC and The HDF Group.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mchecksum.h"

#include "mchecksum_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUF_SIZE   512
#define BUF_SIZE_X 32
#define BUF_SIZE_Y 16

int
main(int argc, char *argv[])
{
    int buf1[BUF_SIZE];
    int buf2[BUF_SIZE_X][BUF_SIZE_Y];
    int i, j;
    mchecksum_object_t checksum1, checksum2;
    void *hash1 = NULL, *hash2 = NULL;
    size_t hash_size;
    const char *hash_method;
    int ret = EXIT_SUCCESS, rc;

    MCHECKSUM_CHECK_ERROR(
        argc < 2, done, ret, EXIT_FAILURE, "Usage: %s [method]", argv[0]);
    hash_method = argv[1];

    /* Initialize buf1 */
    for (i = 0; i < BUF_SIZE; i++)
        buf1[i] = i;

    /* Initialize buf2 */
    for (i = 0; i < BUF_SIZE_X; i++)
        for (j = 0; j < BUF_SIZE_Y; j++)
            buf2[i][j] = i * BUF_SIZE_Y + j;

    /* Initialize checksums */
    rc = mchecksum_init(hash_method, &checksum1);
    MCHECKSUM_CHECK_ERROR(
        rc != 0, done, ret, EXIT_FAILURE, "mchecksum_init() failed");

    rc = mchecksum_init(hash_method, &checksum2);
    MCHECKSUM_CHECK_ERROR(
        rc != 0, done, ret, EXIT_FAILURE, "mchecksum_init() failed");

    /* Update checksums */
    rc = mchecksum_update(checksum1, buf1, BUF_SIZE * sizeof(int));
    MCHECKSUM_CHECK_ERROR(
        rc != 0, done, ret, EXIT_FAILURE, "mchecksum_update() failed");

    for (i = 0; i < BUF_SIZE_X; i++) {
        rc = mchecksum_update(checksum2, buf2[i], BUF_SIZE_Y * sizeof(int));
        MCHECKSUM_CHECK_ERROR(
            rc != 0, done, ret, EXIT_FAILURE, "mchecksum_update() failed");
    }

    /* Get size of checksums */
    hash_size = mchecksum_get_size(checksum1);
    MCHECKSUM_CHECK_ERROR(
        hash_size == 0, done, ret, EXIT_FAILURE, "mchecksum_get_size() failed");

    hash1 = malloc(hash_size);
    MCHECKSUM_CHECK_ERROR(hash1 == NULL, done, ret, EXIT_FAILURE,
        "malloc(%zu) failed", hash_size);

    hash2 = malloc(hash_size);
    MCHECKSUM_CHECK_ERROR(hash2 == NULL, done, ret, EXIT_FAILURE,
        "malloc(%zu) failed", hash_size);

    rc = mchecksum_get(checksum1, hash1, hash_size, MCHECKSUM_FINALIZE);
    MCHECKSUM_CHECK_ERROR(
        rc != 0, done, ret, EXIT_FAILURE, "mchecksum_get() failed");

    rc = mchecksum_get(checksum2, hash2, hash_size, MCHECKSUM_FINALIZE);
    MCHECKSUM_CHECK_ERROR(
        rc != 0, done, ret, EXIT_FAILURE, "mchecksum_get() failed");

    /*
    printf("Checksum of buf1 is: %016lX\n",
            *(mchecksum_uint64_t*)hash1);

    printf("Checksum of buf2 is: %016lX\n",
            *(mchecksum_uint64_t*)hash2);

    printf("Checksum of buf1 is: %04X\n",
            *(mchecksum_uint16_t*)hash1);

    printf("Checksum of buf2 is: %04X\n",
            *(mchecksum_uint16_t*)hash2);
    */

    MCHECKSUM_CHECK_ERROR(strncmp(hash1, hash2, hash_size) != 0, done, ret,
        EXIT_FAILURE, "Checksums do not match");

    /* Corrupting buf2 and recomputing checksum */
    buf2[0][0] = 1;

    rc = mchecksum_reset(checksum2);
    MCHECKSUM_CHECK_ERROR(
        rc != 0, done, ret, EXIT_FAILURE, "mchecksum_reset() failed");

    for (i = 0; i < BUF_SIZE_X; i++) {
        rc = mchecksum_update(checksum2, buf2[i], BUF_SIZE_Y * sizeof(int));
        MCHECKSUM_CHECK_ERROR(
            rc != 0, done, ret, EXIT_FAILURE, "mchecksum_update() failed");
    }
    rc = mchecksum_get(checksum2, hash2, hash_size, MCHECKSUM_FINALIZE);
    MCHECKSUM_CHECK_ERROR(
        rc != 0, done, ret, EXIT_FAILURE, "mchecksum_get() failed");

    MCHECKSUM_CHECK_ERROR(strncmp(hash1, hash2, hash_size) == 0, done, ret,
        EXIT_FAILURE, "Checksums should not match");

done:
    /* Destroy checksums and free hash buffers */
    mchecksum_destroy(checksum1);
    mchecksum_destroy(checksum2);
    free(hash1);
    free(hash2);

    return ret;
}
