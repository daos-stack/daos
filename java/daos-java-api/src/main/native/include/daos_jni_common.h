/*
 * (C) Copyright 2018-2019 Intel Corporation.
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

#include <stdio.h>
#include <daos.h>
#include <daos_fs.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <gurt/common.h>

#ifndef _INCLUDED_DAOS_JNI_COMMON
#define _INCLUDED_DAOS_JNI_COMMON

static jint JNI_VERSION = JNI_VERSION_1_8;

static const int READ_DIR_BATCH_SIZE = 10;
static const int READ_DIR_INITIAL_BUFFER_SIZE = 1024;
static const int CUSTOM_ERROR_CODE_BASE = -1000000;

static const int CUSTOM_ERR1 = -1000001; // scm size and nvme size no greater than 0
static const int CUSTOM_ERR2 = -1000002; // failed to parse service replics string
static const int CUSTOM_ERR3 = -1000003; // malloc or realloc buffer failed
static const int CUSTOM_ERR4 = -1000004; // value length greater than expected

#endif
