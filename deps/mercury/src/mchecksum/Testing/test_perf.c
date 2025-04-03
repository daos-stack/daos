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
#include <time.h>

/* Maximum size of buffer used */
#define MAX_BUF_SIZE (1 << 24)

/* Width of field used to report numbers */
#define FIELD_WIDTH 20

/* Precision of reported numbers */
#define FLOAT_PRECISION 2

#define BENCHMARK "MChecksum Perf Test"

#define MAX_LOOP 100

/* #define USE_MEMSET */

typedef struct timespec my_time_t;

#define MY_TIME_INIT                                                           \
    (my_time_t) { .tv_sec = 0, .tv_nsec = 0 }

/*---------------------------------------------------------------------------*/
static void
my_time_get_current(my_time_t *tv)
{
    clock_gettime(CLOCK_MONOTONIC, tv);
}

/*---------------------------------------------------------------------------*/
static my_time_t
my_time_subtract(my_time_t in1, my_time_t in2)
{
    my_time_t out;

    out.tv_sec = in1.tv_sec - in2.tv_sec;
    out.tv_nsec = in1.tv_nsec - in2.tv_nsec;
    if (out.tv_nsec < 0) {
        out.tv_nsec += 1000000000;
        out.tv_sec -= 1;
    }

    return out;
}

/*---------------------------------------------------------------------------*/
static double
my_time_to_double(my_time_t tv)
{
    return (double) tv.tv_sec + (double) (tv.tv_nsec) * 0.000000001;
}

/*---------------------------------------------------------------------------*/
int
main(int argc, char *argv[])
{
    mchecksum_object_t checksum;
    char *buf = NULL, *hash = NULL;
    size_t hash_size;
    const char *hash_method;
    unsigned int size;
    int i, rc;

    MCHECKSUM_CHECK_ERROR_NORET(argc < 2, error, "Usage: %s [method]", argv[0]);
    hash_method = argv[1];

    /* Initialize buf */
    buf = malloc(MAX_BUF_SIZE);
    MCHECKSUM_CHECK_ERROR_NORET(
        buf == NULL, error, "Could not allocate buffer");

    for (i = 0; i < MAX_BUF_SIZE; i++)
        buf[i] = (char) i;

    printf("# %s\n", BENCHMARK);
    printf("%-*s%*s%*s\n", 10, "# Size", FIELD_WIDTH, "Bandwidth (MB/s)",
        FIELD_WIDTH, "Average Time (us)");
    fflush(stdout);

    rc = mchecksum_init(hash_method, &checksum);
    MCHECKSUM_CHECK_ERROR_NORET(rc != 0, error, "Error in mchecksum_init!");

    hash_size = mchecksum_get_size(checksum);
    MCHECKSUM_CHECK_ERROR_NORET(hash_size == 0, error, "NULL hash size");

    hash = malloc(hash_size);
    MCHECKSUM_CHECK_ERROR_NORET(hash == NULL, error, "Could not allocate hash");

    /* Initialize the buffers */
    for (size = 1; size <= MAX_BUF_SIZE; size *= 2) {
        my_time_t t = MY_TIME_INIT;
        my_time_t t_start, t_end;

        my_time_get_current(&t_start);
        for (i = 0; i < MAX_LOOP; i++) {
            mchecksum_reset(checksum);
#ifdef USE_MEMSET
            memset(buf, 'B', size);
#else
            mchecksum_update(checksum, buf, size);
#endif
        }
        my_time_get_current(&t_end);
        t = my_time_subtract(t_end, t_start);

        fprintf(stdout, "%-*d%*.*f%*.*f\n", 10, size, FIELD_WIDTH,
            FLOAT_PRECISION, (size * MAX_LOOP) / (my_time_to_double(t) * 1e6),
            FIELD_WIDTH, FLOAT_PRECISION,
            my_time_to_double(t) * 1e6 / MAX_LOOP);
        fflush(stdout);

        mchecksum_get(checksum, hash, hash_size, MCHECKSUM_FINALIZE);
    }

    mchecksum_destroy(checksum);

    free(hash);
    free(buf);

    return EXIT_SUCCESS;

error:
    free(hash);
    free(buf);

    return EXIT_FAILURE;
}
