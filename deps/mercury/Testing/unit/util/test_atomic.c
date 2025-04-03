/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "mercury_atomic.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

int
main(void)
{
    hg_atomic_int32_t atomic_int32;
    int32_t val32, init_val32;
    hg_atomic_int64_t atomic_int64;
    int64_t val64, init_val64;
    int ret = EXIT_SUCCESS;

    /* Init32 test */
    hg_atomic_init32(&atomic_int32, 1);
    val32 = hg_atomic_get32(&atomic_int32);
    if (val32 != 1) {
        fprintf(stderr,
            "Error in hg_atomic_init32: atomic value is %" PRId32 "\n", val32);
        ret = EXIT_FAILURE;
        goto done;
    }

    /* Set32 test */
    hg_atomic_set32(&atomic_int32, 2);
    val32 = hg_atomic_get32(&atomic_int32);
    if (val32 != 2) {
        fprintf(stderr,
            "Error in hg_atomic_set32: atomic value is %" PRId32 "\n", val32);
        ret = EXIT_FAILURE;
        goto done;
    }

    /* Incr32 test */
    val32 = hg_atomic_incr32(&atomic_int32);
    if (val32 != 3) {
        fprintf(stderr,
            "Error in hg_atomic_incr32: atomic value is %" PRId32 "\n", val32);
        ret = EXIT_FAILURE;
        goto done;
    }
    val32 = hg_atomic_get32(&atomic_int32);
    if (val32 != 3) {
        fprintf(stderr,
            "Error in hg_atomic_incr32: atomic value is %" PRId32 "\n", val32);
        ret = EXIT_FAILURE;
        goto done;
    }

    /* Decr32 test */
    val32 = hg_atomic_decr32(&atomic_int32);
    if (val32 != 2) {
        fprintf(stderr,
            "Error in hg_atomic_decr32: atomic value is %" PRId32 "\n", val32);
        ret = EXIT_FAILURE;
        goto done;
    }
    val32 = hg_atomic_get32(&atomic_int32);
    if (val32 != 2) {
        fprintf(stderr,
            "Error in hg_atomic_decr32: atomic value is %" PRId32 "\n", val32);
        ret = EXIT_FAILURE;
        goto done;
    }

    /* Or32 test */
    init_val32 = hg_atomic_get32(&atomic_int32);
    val32 = hg_atomic_or32(&atomic_int32, 8);
    if (val32 != init_val32) {
        fprintf(stderr,
            "Error in hg_atomic_or32: atomic value is %" PRId32 "\n", val32);
        ret = EXIT_FAILURE;
        goto done;
    }
    val32 = hg_atomic_get32(&atomic_int32);
    if (val32 != (init_val32 | 8)) {
        fprintf(stderr,
            "Error in hg_atomic_or32: atomic value is %" PRId32 "\n", val32);
        ret = EXIT_FAILURE;
        goto done;
    }

    /* Xor32 test */
    init_val32 = hg_atomic_get32(&atomic_int32);
    val32 = hg_atomic_xor32(&atomic_int32, 17);
    if (val32 != init_val32) {
        fprintf(stderr,
            "Error in hg_atomic_xor32: atomic value is %" PRId32 "\n", val32);
        ret = EXIT_FAILURE;
        goto done;
    }
    val32 = hg_atomic_get32(&atomic_int32);
    if (val32 != (init_val32 ^ 17)) {
        fprintf(stderr,
            "Error in hg_atomic_xor32: atomic value is %" PRId32 "\n", val32);
        ret = EXIT_FAILURE;
        goto done;
    }

    /* And32 test */
    init_val32 = hg_atomic_get32(&atomic_int32);
    val32 = hg_atomic_and32(&atomic_int32, 33);
    if (val32 != init_val32) {
        fprintf(stderr,
            "Error in hg_atomic_and32: atomic value is %" PRId32 "\n", val32);
        ret = EXIT_FAILURE;
        goto done;
    }
    val32 = hg_atomic_get32(&atomic_int32);
    if (val32 != (init_val32 & 33)) {
        fprintf(stderr,
            "Error in hg_atomic_and32: atomic value is %" PRId32 "\n", val32);
        ret = EXIT_FAILURE;
        goto done;
    }

    /* Cas32 test */
    init_val32 = hg_atomic_get32(&atomic_int32);
    val32 = 128;
    if (!hg_atomic_cas32(&atomic_int32, init_val32, val32)) {
        fprintf(stderr,
            "Error in hg_atomic_cas32: could not swap values"
            "with %" PRId32 ", is %" PRId32 ", expected %" PRId32 "\n",
            val32, hg_atomic_get32(&atomic_int32), init_val32);
        ret = EXIT_FAILURE;
        goto done;
    }
    val32 = hg_atomic_get32(&atomic_int32);
    if (val32 != 128) {
        fprintf(stderr,
            "Error in hg_atomic_cas32: atomic value is %" PRId32 "\n", val32);
        ret = EXIT_FAILURE;
        goto done;
    }
    if (hg_atomic_cas32(&atomic_int32, 1, 0)) {
        fprintf(stderr, "Error in hg_atomic_cas32: should not swap values\n");
        ret = EXIT_FAILURE;
        goto done;
    }

    /* Init64 test */
    hg_atomic_init64(&atomic_int64, 1);
    val64 = hg_atomic_get64(&atomic_int64);
    if (val64 != 1) {
        fprintf(stderr,
            "Error in hg_atomic_init64: atomic value is %" PRId64 "\n", val64);
        ret = EXIT_FAILURE;
        goto done;
    }

    /* Set64 test */
    hg_atomic_set64(&atomic_int64, 2);
    val64 = hg_atomic_get64(&atomic_int64);
    if (val64 != 2) {
        fprintf(stderr,
            "Error in hg_atomic_set64: atomic value is %" PRId64 "\n", val64);
        ret = EXIT_FAILURE;
        goto done;
    }

    /* Incr64 test */
    val64 = hg_atomic_incr64(&atomic_int64);
    if (val64 != 3) {
        fprintf(stderr,
            "Error in hg_atomic_incr64: atomic value is %" PRId64 "\n", val64);
        ret = EXIT_FAILURE;
        goto done;
    }
    val64 = hg_atomic_get64(&atomic_int64);
    if (val64 != 3) {
        fprintf(stderr,
            "Error in hg_atomic_incr64: atomic value is %" PRId64 "\n", val64);
        ret = EXIT_FAILURE;
        goto done;
    }

    /* Decr64 test */
    val64 = hg_atomic_decr64(&atomic_int64);
    if (val64 != 2) {
        fprintf(stderr,
            "Error in hg_atomic_decr64: atomic value is %" PRId64 "\n", val64);
        ret = EXIT_FAILURE;
        goto done;
    }
    val64 = hg_atomic_get64(&atomic_int64);
    if (val64 != 2) {
        fprintf(stderr,
            "Error in hg_atomic_decr64: atomic value is %" PRId64 "\n", val64);
        ret = EXIT_FAILURE;
        goto done;
    }

    /* Or64 test */
    init_val64 = hg_atomic_get64(&atomic_int64);
    val64 = hg_atomic_or64(&atomic_int64, 8);
    if (val64 != init_val64) {
        fprintf(stderr,
            "Error in hg_atomic_or64: atomic value is %" PRId64 "\n", val64);
        ret = EXIT_FAILURE;
        goto done;
    }
    val64 = hg_atomic_get64(&atomic_int64);
    if (val64 != (init_val64 | 8)) {
        fprintf(stderr,
            "Error in hg_atomic_or64: atomic value is %" PRId64 "\n", val64);
        ret = EXIT_FAILURE;
        goto done;
    }

    /* Xor64 test */
    init_val64 = hg_atomic_get64(&atomic_int64);
    val64 = hg_atomic_xor64(&atomic_int64, 17);
    if (val64 != init_val64) {
        fprintf(stderr,
            "Error in hg_atomic_xor64: atomic value is %" PRId64 "\n", val64);
        ret = EXIT_FAILURE;
        goto done;
    }
    val64 = hg_atomic_get64(&atomic_int64);
    if (val64 != (init_val64 ^ 17)) {
        fprintf(stderr,
            "Error in hg_atomic_xor64: atomic value is %" PRId64 "\n", val64);
        ret = EXIT_FAILURE;
        goto done;
    }

    /* And64 test */
    init_val64 = hg_atomic_get64(&atomic_int64);
    val64 = hg_atomic_and64(&atomic_int64, 33);
    if (val64 != init_val64) {
        fprintf(stderr,
            "Error in hg_atomic_and64: atomic value is %" PRId64 "\n", val64);
        ret = EXIT_FAILURE;
        goto done;
    }
    val64 = hg_atomic_get64(&atomic_int64);
    if (val64 != (init_val64 & 33)) {
        fprintf(stderr,
            "Error in hg_atomic_and64: atomic value is %" PRId64 "\n", val64);
        ret = EXIT_FAILURE;
        goto done;
    }

    /* Cas64 test */
    init_val64 = hg_atomic_get64(&atomic_int64);
    val64 = 128;
    if (!hg_atomic_cas64(&atomic_int64, init_val64, val64)) {
        fprintf(stderr,
            "Error in hg_atomic_cas64: could not swap values"
            "with %" PRId64 ", is %" PRId64 ", expected %" PRId64 "\n",
            val64, hg_atomic_get64(&atomic_int64), init_val64);
        ret = EXIT_FAILURE;
        goto done;
    }
    val64 = hg_atomic_get64(&atomic_int64);
    if (val64 != 128) {
        fprintf(stderr,
            "Error in hg_atomic_cas64: atomic value is %" PRId64 "\n", val64);
        ret = EXIT_FAILURE;
        goto done;
    }
    if (hg_atomic_cas64(&atomic_int64, 1, 0)) {
        fprintf(stderr, "Error in hg_atomic_cas64: should not swap values\n");
        ret = EXIT_FAILURE;
        goto done;
    }

done:
    return ret;
}
