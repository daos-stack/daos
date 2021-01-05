/*
 * (C) Copyright 2018-2020 Intel Corporation.
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

#include "io_daos_DaosClient.h"
#include <sys/stat.h>
#include <gurt/common.h>
#include <libgen.h>
#include <stdio.h>
#include <daos.h>
#include <daos_jni_common.h>
#include <fcntl.h>

/**
 * JNI method to open pool with given \a poolId.
 *
 * \param[in]	env		JNI environment
 * \param[in]	clientClass	class of DaosFsClient
 * \param[in]	poolId		pool UUID
 * \param[in]	serverGroup	server group name
 * \param[in]	ranks		ranks separated by ':'
 *
 * \return	copied pool handle in long
 */
JNIEXPORT jlong JNICALL
Java_io_daos_DaosClient_daosOpenPool(JNIEnv *env,
				     jclass clientClass, jstring poolId,
				     jstring serverGroup,
				     jstring ranks, jint flags)
{
	const char *pool_str = (*env)->GetStringUTFChars(env, poolId, 0);
	const char *server_group = (*env)->GetStringUTFChars(env, serverGroup,
								0);
	const char *svc_ranks = (*env)->GetStringUTFChars(env, ranks, 0);
	uuid_t pool_uuid;
	d_rank_list_t *svcl = daos_rank_list_parse(svc_ranks, ":");
	jlong ret;

	uuid_parse(pool_str, pool_uuid);
	if (svcl == NULL) {
		char *tmp = "Invalid pool service rank list (%s) when "
					"open pool (%s)";
		char *msg = (char *)malloc(strlen(tmp) + strlen(svc_ranks) +
				strlen(pool_str));

		sprintf(msg, tmp, ranks, pool_str);
		throw_exception(env, msg, CUSTOM_ERR2);
		ret = -1;
	} else {
		daos_handle_t poh;
		int rc;

		rc = daos_pool_connect(pool_uuid, server_group, svcl,
				       flags,
				       &poh /* returned pool handle */,
				       NULL /* returned pool info */,
				       NULL /* event */);

		if (rc) {
			char *tmp = "Failed to connect to pool (%s)";
			char *msg = (char *)malloc(strlen(tmp) +
					strlen(pool_str));

			sprintf(msg, tmp, pool_str);
			throw_exception_base(env, msg, rc, 1, 0);
			ret = -1;
		} else {
			memcpy(&ret, &poh, sizeof(poh));
		}
	}
	(*env)->ReleaseStringUTFChars(env, poolId, pool_str);
	if (serverGroup != NULL) {
		(*env)->ReleaseStringUTFChars(env, serverGroup, server_group);
	}
	if (ranks != NULL) {
		(*env)->ReleaseStringUTFChars(env, ranks, svc_ranks);
	}
	return ret;
}

/**
 * JNI method to close pool denoted by \a poolHandle.
 *
 * \param[in]	env		JNI environment
 * \param[in]	clientClass	class of DaosFsClient
 * \param[in]	poolHandle	pool handle
 */
JNIEXPORT void JNICALL
Java_io_daos_DaosClient_daosClosePool(JNIEnv *env,
				      jclass clientClass, jlong poolHandle)
{
	daos_handle_t poh;

	memcpy(&poh, &poolHandle, sizeof(poh));
	int rc = daos_pool_disconnect(poh, NULL);

	if (rc) {
		printf("Failed to close pool rc: %d\n", rc);
		printf("error msg: %s\n", d_errstr(rc));
	}
}

/**
 * JNI method to open container with given \a contUuid.
 *
 * \param[in]	env		JNI environment
 * \param[in]	clientClass	class of DaosFsClient
 * \param[in]	poolHandle	pool handle
 * \param[in]	contUuid	container UUID
 * \param[in]	mode		container mode
 *
 * \return	copied container handle in long
 */
JNIEXPORT jlong JNICALL
Java_io_daos_DaosClient_daosOpenCont(JNIEnv *env,
				     jclass clientClass, jlong poolHandle,
				     jstring contUuid, jint mode)
{
	daos_handle_t poh;
	daos_cont_info_t co_info;
	const char *cont_str = (*env)->GetStringUTFChars(env, contUuid, NULL);
	uuid_t cont_uuid;
	daos_handle_t coh;
	jlong ret = -1;

	uuid_parse(cont_str, cont_uuid);
	memcpy(&poh, &poolHandle, sizeof(poh));
	int rc = daos_cont_open(poh, cont_uuid, mode, &coh, &co_info, NULL);

	if (rc) {
		char *tmp = "Failed to open container (id: %s)";
		char *msg = (char *)malloc(strlen(tmp) + strlen(cont_str));

		sprintf(msg, tmp, cont_str);
		throw_exception_base(env, msg, rc, 1, 0);
		ret = -1;
	} else {
		memcpy(&ret, &coh, sizeof(coh));
	}
	(*env)->ReleaseStringUTFChars(env, contUuid, cont_str);
	return ret;
}

/**
 * JNI method to close container denoted by \a contHandle.
 *
 * \param[in]	env		JNI environment
 * \param[in]	clientClass	class of DaosFsClient
 * \param[in]	contHandle	container handle
 */
JNIEXPORT void JNICALL
Java_io_daos_DaosClient_daosCloseContainer(JNIEnv *env,
					   jclass clientClass, jlong contHandle)
{
	daos_handle_t coh;

	memcpy(&coh, &contHandle, sizeof(coh));
	int rc = daos_cont_close(coh, NULL);

	if (rc) {
		printf("Failed to close container rc: %d\n", rc);
		printf("error msg: %s\n", d_errstr(rc));
	}
}

/**
 * JNI method to finalize DAOS.
 *
 * \param[in]	env		JNI environment
 * \param[in]	clientClass	class of DaosFsClient
 */
JNIEXPORT void JNICALL
Java_io_daos_DaosClient_daosFinalize(JNIEnv *env,
				     jclass clientClass)
{
	int rc = daos_eq_lib_fini();

	if (rc) {
		printf("Failed to finalize EQ lib rc: %d\n", rc);
		printf("error msg: %s\n", d_errstr(rc));
	}

	rc = daos_fini();
	if (rc) {
		printf("Failed to finalize daos rc: %d\n", rc);
		printf("error msg: %s\n", d_errstr(rc));
	}
}
