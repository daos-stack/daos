/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <daos.h>
#include <daos_fs.h>
#include <daos_uns.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <gurt/common.h>

#if (__STDC_VERSION__ >= 199901L)
#include <stdint.h>
#endif

#ifndef _INCLUDED_DAOS_JNI_COMMON
#define _INCLUDED_DAOS_JNI_COMMON

typedef struct {
    int nbrOfEvents;
    daos_handle_t eqhdl;
    daos_event_t **events;
} event_queue_wrapper_t;

typedef struct {
    int reusable;
    int nbrOfAkeys;
    uint16_t maxKeyLen;
    daos_iod_t *iods;
    d_sg_list_t *sgls;
    daos_recx_t *recxs;
    d_iov_t *iovs;
    daos_iod_type_t iod_type;
    uint16_t record_size;
    uint64_t ret_buf_address;
} data_desc_t;

typedef struct {
    daos_key_t dkey;
    uint16_t maxKeyLen;
    uint16_t nbrOfEntries;
    uint16_t nbrOfRequests;
    event_queue_wrapper_t *eq;
    daos_event_t *event;
    daos_iod_t *iods;
    d_sg_list_t *sgls;
    daos_recx_t *recxs;
    d_iov_t *iovs;
    uint64_t ret_buf_address;
} data_desc_simple_t;

typedef struct {
    int nbrOfDescs;
    data_desc_simple_t **descs;
} data_desc_simple_grp_t;

static jint JNI_VERSION = JNI_VERSION_1_8;

static const int READ_DIR_BATCH_SIZE = 10;
static const int READ_DIR_INITIAL_BUFFER_SIZE = 1024;
static const int CUSTOM_ERROR_CODE_BASE = -1000000;

/* scm size and nvme size no greater than 0 */
static const int CUSTOM_ERR1 = -1000001;
/* failed to parse service replics string */
static const int CUSTOM_ERR2 = -1000002;
/* malloc or realloc buffer failed */
static const int CUSTOM_ERR3 = -1000003;
/* value length greater than expected */
static const int CUSTOM_ERR4 = -1000004;
/* invalid argument in UNS */
static const int CUSTOM_ERR5 = -1000005;
/* invalid argument in object */
static const int CUSTOM_ERR6 = -1000006;

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
 *				customized error code
 * \param[in]	release_msg	is \a msg needed to be released, (0 or 1)
 * \param[in]	posix_error	is \a error_code posix error,
 *				1 for true, 0 for false
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
