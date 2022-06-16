/**
 * (C) Copyright 2017-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef CMOCKA_TESTS_H
#define CMOCKA_TESTS_H

extern struct _cmocka_tests *generated_cmocka_tests();

extern int (*global_setup_functions[])(void **state);
extern int (*global_teardown_functions[])(void **state);

#endif /* !CMOCKA_TESTS_H */
