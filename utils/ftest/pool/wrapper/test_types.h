/**
 * (C) Copyright 2017 Intel Corporation.
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

#define TEST_SUCCESS 0
#define TEST_FAILED  1

#define POOL_CREATE  "create"
#define POOL_DESTROY  "destroy"
#define POOL_CONNECT  "connect"
#define POOL_DISCONNECT  "discon"
#define POOL_CREATE_AND_DESTROY  "candd"

#define FLAG_RO "RO"
#define FLAG_RW "RW"
#define FLAG_EX "EX"

/**
 * Acquire generic test resources.
 */
int setup(int argc, char **argv);


/**
 * Cleanup generic test resources.
 */
int done(void);
