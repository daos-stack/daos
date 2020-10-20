/**
 * (C) Copyright 2020 Intel Corporation.
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

#ifndef __OBJ_CTL_H__
#define __OBJ_CTL_H__

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Interactive function testing shell for DAOS
 *
 * Provides a shell to test VOS and DAOS commands.
 *
 * \param[in]     argc   number of arguments
 * \param[in,out] argv   array of character pointers listing the arguments.
 * \return               0 on success, daos_errno code on failure.
 */
int shell(int argc, char *argv[]);

#if defined(__cplusplus)
}
#endif

#endif /*  __OBJ_CTL_H__ */
