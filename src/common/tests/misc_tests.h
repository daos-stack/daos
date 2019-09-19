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
 *
 */


#include <daos.h>

#ifndef __DAOS_MISC_TESTS_H
#define __DAOS_MISC_TESTS_H

/** Initialize and SGL with a variable number of IOVs and set the IOV buffers
 *  to the value of the strings passed
 *
 * @param sgl		Scatter gather list to initialize
 * @param count		Number of IO Vectors that will be created in the SGL
 * @param str		First string that will be used
 * @param ...		Rest of strings, up to count
 */
void
daos_sgl_init_with_strings(d_sg_list_t *sgl, uint32_t count, char *str, ...);

int
misc_tests_run();

#endif /** __DAOS_MISC_TESTS_H */
