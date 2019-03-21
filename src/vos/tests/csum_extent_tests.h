/**
 * (C) Copyright 2019 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */


#ifndef __CSUM_TESTS_H__
#define __CSUM_TESTS_H__

void
csum_multiple_extents_tests(void **state);

void
csum_test_csum_buffer_of_0_during_fetch(void **state);

void
csum_test_holes(void **state);

void
csum_helper_functions_tests(void **state);

void
csum_extent_not_starting_at_0(void **state);

void
csum_extent_not_chunk_aligned(void **state);

void
evt_csum_helper_functions_tests(void **state);

void
csum_invalid_input_tests(void **state);

#endif
