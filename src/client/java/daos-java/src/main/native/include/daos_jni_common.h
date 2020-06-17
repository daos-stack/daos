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

#include <jni.h>
#include <stdio.h>
#include <daos.h>
#include <daos_fs.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <gurt/common.h>

#ifndef _INCLUDED_DAOS_JNI_COMMON
#define _INCLUDED_DAOS_JNI_COMMON

typedef struct {
    uint16_t type;
    uint32_t offset;
    uint32_t record_size;
    uint32_t bufferIdx;
    uint32_t data_size;
} record_desc_t;

static jint JNI_VERSION = JNI_VERSION_1_8;

static const int READ_DIR_BATCH_SIZE = 10;
static const int READ_DIR_INITIAL_BUFFER_SIZE = 1024;
static const int CUSTOM_ERROR_CODE_BASE = -1000000;

static const int CUSTOM_ERR1 = -1000001; // scm size and nvme size no greater than 0
static const int CUSTOM_ERR2 = -1000002; // failed to parse service replics string
static const int CUSTOM_ERR3 = -1000003; // malloc or realloc buffer failed
static const int CUSTOM_ERR4 = -1000004; // value length greater than expected
static const int CUSTOM_ERR6 = -1000006; // invalid argument in object

static jclass daos_io_exception_class;

static jmethodID new_exception_msg;
static jmethodID new_exception_cause;
static jmethodID new_exception_msg_code_msg;
static jmethodID new_exception_msg_code_cause;

static int ERROR_PATH_LEN = 256;
static int ERROR_NOT_EXIST = 2;
static int ERROR_LOOKUP_MAX_RETRIES = 100;

static uint8_t KEY_LIST_CODE_EMPTY = (uint8_t)0;
static uint8_t KEY_LIST_CODE_IN_USE = (uint8_t)1;
static uint8_t KEY_LIST_CODE_ANCHOR_END = (uint8_t)2;
static uint8_t KEY_LIST_CODE_KEY2BIG = (uint8_t)3;
static uint8_t KEY_LIST_CODE_REACH_LIMIT = (uint8_t)4;

/**
 * utility function to throw Java exception.
 *
 * \param[in]	env		JNI environment
 * \param[in]	msg		error message provided by caller
 * \param[in]	error_code	non-zero return code of DFS function or
 * 				customized error code
 * \param[in]	release_msg	is \a msg needed to be released, (0 or 1)
 * \param[in]	posix_error	is \a error_code posix error,
 * 				1 for true, 0 for false
 *
 * \return	return code of Throw function of \a env
 */
int
throw_exception_base(JNIEnv *env, char *msg, int error_code,
			int release_msg, int posix_error);

/**
 * throw Java exception with dynamically constructed message for posix error.
 *
 * \param[in]	env		JNI environment
 * \param[in]	msg		error message provided by caller
 * \param[in]	error_code	non-zero return code of DFS function
 *
 * \return	return code of throw_exception_base
 */
int
throw_exception(JNIEnv *env, char *msg, int error_code);

/**
 * throw Java exception with dynamically constructed message for object error.
 *
 * \param[in]	env		JNI environment
 * \param[in]	msg		error message provided by caller
 * \param[in]	error_code	non-zero return code of DFS function
 *
 * \return	return code of throw_exception_base
 */
int
throw_exception_object(JNIEnv *env, char *msg, int error_code);

/**
 * throw Java exception with constant message for posix error.
 *
 * \param[in]	env		JNI environment
 * \param[in]	msg		error message provided by caller
 * \param[in]	error_code	non-zero return code of DFS function
 *
 * \return	return code of throw_exception_base
 */
int
throw_exception_const_msg(JNIEnv *env, char *msg, int error_code);

/**
 * throw Java exception with constant message for object error.
 *
 * \param[in]	env		JNI environment
 * \param[in]	msg		error message provided by caller
 * \param[in]	error_code	non-zero return code of DFS function
 *
 * \return	return code of throw_exception_base
 */
int
throw_exception_const_msg_object(JNIEnv *env, char *msg, int error_code);

#endif
