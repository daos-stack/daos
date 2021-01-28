/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
					 jint flags)
{
	const char *pool_str = (*env)->GetStringUTFChars(env, poolId, 0);
	const char *server_group = (*env)->GetStringUTFChars(env, serverGroup,
								0);
	uuid_t pool_uuid;
	jlong ret;
	daos_handle_t poh;
	int rc;

	uuid_parse(pool_str, pool_uuid);

	rc = daos_pool_connect(pool_uuid, server_group,
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
	(*env)->ReleaseStringUTFChars(env, poolId, pool_str);
	if (serverGroup != NULL) {
		(*env)->ReleaseStringUTFChars(env, serverGroup, server_group);
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

JNIEXPORT jlong JNICALL
Java_io_daos_DaosClient_createEventQueue(JNIEnv *env, jclass clientClass,
		jint nbrOfEvents)
{
	daos_handle_t eqhdl;
	int rc = daos_eq_create(&eqhdl);
	int i;
	int count;

	if (rc) {
		char *msg = "Failed to create EQ";

		throw_exception_const_msg(env, msg, rc);
		return -1;
	}

	event_queue_wrapper_t *eq = (event_queue_wrapper_t *)calloc(1,
			sizeof(event_queue_wrapper_t));
	eq->events = (daos_event_t **)malloc(
		nbrOfEvents * sizeof(daos_event_t *));
	eq->nbrOfEvents = nbrOfEvents;
	eq->eqhdl = eqhdl;
	for (i = 0; i < nbrOfEvents; i++) {
		eq->events[i] = (daos_event_t *)malloc(sizeof(daos_event_t));
		rc = daos_event_init(eq->events[i], eqhdl, NULL);
		if (rc) {
			char *tmp = "Failed to create event %d";
			char *msg = (char *)malloc(strlen(tmp) + 10);

			sprintf(msg, tmp, i);
			throw_exception_base(env, msg, rc, 1, 0);
			goto fail;
		}
		eq->events[i]->ev_debug = i;
	}

fail:
	if (rc) {
		count = i;
		while (i >= 0) {
			if (eq->events[i] && i < count) {
				daos_event_fini(eq->events[i]);
			}
			i--;
		}
		daos_eq_destroy(eqhdl, 1);
		for (i = 0; i <= count; i++) {
			if (eq->events[i]) {
				free(eq->events[i]);
			}
		}
		free(eq->events);
		free(eq);
	}
	return *(jlong *)&eq;
}

JNIEXPORT void JNICALL
Java_io_daos_DaosClient_pollCompleted(JNIEnv *env, jclass clientClass,
		jlong eqWrapperHdl, jlong memAddress,
		jint nbrOfEvents, jint timeoutMs)
{
	event_queue_wrapper_t *eq = *(event_queue_wrapper_t **)&eqWrapperHdl;
	char *buffer = (char *)memAddress;
	uint16_t idx;
	int i;
	struct daos_event **eps = (struct daos_event **)malloc(
			sizeof(struct daos_event *) * nbrOfEvents);
	int rc = daos_eq_poll(eq->eqhdl, 1, timeoutMs * 1000, nbrOfEvents,
				eps);

	if (rc < 0) {
		char *tmp = "Failed to poll completed events, max events: %d";
		char *msg = (char *)malloc(strlen(tmp) + 10);

		sprintf(msg, tmp, nbrOfEvents);
		throw_exception_base(env, msg, rc, 1, 0);
		goto fail;
	}
	idx = rc;
	memcpy(buffer, &idx, 2);
	buffer += 2;
	for (i = 0; i < rc; i++) {
		idx = eps[i]->ev_debug;
		memcpy(buffer, &idx, 2);
		buffer += 2;
	}
	return;
fail:
	if (eps) {
		free(eps);
	}
}

JNIEXPORT void JNICALL
Java_io_daos_DaosClient_destroyEventQueue(JNIEnv *env, jclass clientClass,
		jlong eqWrapperHdl)
{
	event_queue_wrapper_t *eq = *(event_queue_wrapper_t **)&eqWrapperHdl;
	int i;
	int rc;
	int count = 0;
	daos_event_t *ev;
	struct daos_event **eps = (struct daos_event **)malloc(
		sizeof(struct daos_event *) * eq->nbrOfEvents);

	while (daos_eq_poll(eq->eqhdl, 1, 1000, eq->nbrOfEvents, eps)) {
		count++;
		if (count > 4) {
			break;
		}
	}

	if (eq->events) {
		for (i = 0; i < eq->nbrOfEvents; i++) {
			ev = eq->events[i];
			if (!ev) {
				continue;
			}
			rc = daos_event_fini(ev);
			if (rc) {
				char *tmp = "Failed to finalize %d th event.";
				char *msg = (char *)malloc(strlen(tmp) + 10);

				sprintf(msg, tmp, i);
				throw_exception_base(env, msg, rc, 1, 0);
				goto fin;
			}
		}
	}
	if (eq->eqhdl.cookie) {
		rc = daos_eq_destroy(eq->eqhdl, 0);
		if (rc) {
			char *tmp = "Failed to destroy EQ.";

			throw_exception_const_msg_object(env, tmp, rc);
			goto fin;
		}
	}
	if (eq->events) {
		for (i = 0; i < eq->nbrOfEvents; i++) {
		ev = eq->events[i];
			if (ev) {
				free(ev);
			}
		}
		free(eq->events);
	}
	if (eq) {
		free(eq);
	}
fin:
	if (eps) {
		free(eps);
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
