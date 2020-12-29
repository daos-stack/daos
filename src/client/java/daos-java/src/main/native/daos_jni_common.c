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

#include "daos_jni_common.h"

/**
 * This function is called when JVM load native library through
 * System.loadLibrary or System.load methods.
 *
 * \param[in]	vm		denote a Java VM
 * \param[in]	reserved	reserved for future
 *
 * \return	JNI_VERSION expected by JVM on success return.
 *	JNI_ERR or non-zeor rc code of daos_init() for any error. In
 *	this case, JVM throws JNI error.
 */
jint
JNI_OnLoad(JavaVM *vm, void *reserved)
{
	JNIEnv *env;

	if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION) != JNI_OK) {
		return JNI_ERR;
	}

	jclass local_class = (*env)->FindClass(env,
			"io/daos/DaosIOException");

	daos_io_exception_class = (*env)->NewGlobalRef(env, local_class);
	jmethodID m1 = (*env)->GetMethodID(env, daos_io_exception_class,
			"<init>",
			"(Ljava/lang/String;)V");

	new_exception_msg = (*env)->NewGlobalRef(env, m1);
	if (new_exception_msg == NULL) {
		printf("failed to get constructor msg\n");
		return JNI_ERR;
	}
	jmethodID m2 = (*env)->GetMethodID(env, daos_io_exception_class,
			"<init>",
			"(Ljava/lang/Throwable;)V");

	new_exception_cause = (*env)->NewGlobalRef(env, m2);
	if (new_exception_cause == NULL) {
		printf("failed to get constructor cause\n");
		return JNI_ERR;
	}
	jmethodID m3 = (*env)->GetMethodID(env, daos_io_exception_class,
			"<init>",
			"(Ljava/lang/String;ILjava/lang/String;)V");

	new_exception_msg_code_msg = (*env)->NewGlobalRef(env, m3);
	if (new_exception_msg_code_msg == NULL) {
		printf("failed to get constructor msg, code and daos msg\n");
		return JNI_ERR;
	}
	jmethodID m4 = (*env)->GetMethodID(env, daos_io_exception_class,
			"<init>",
			"(Ljava/lang/String;ILjava/lang/Throwable;)V");

	new_exception_msg_code_cause = (*env)->NewGlobalRef(env, m4);
	if (new_exception_msg_code_cause == NULL) {
		printf("failed to get constructor msg, code and cause\n");
		return JNI_ERR;
	}
	int rc = daos_init();

	if (rc) {
		printf("daos_init() failed with rc = %d\n", rc);
		printf("error msg: %s\n", d_errstr(rc));
		return rc;
	}
	rc = daos_eq_lib_init();
	if (rc) {
		printf("Failed daos_eq_lib_init: %d\n", rc);
		return rc;
	}
	return JNI_VERSION;
}

int
throw_exception_base(JNIEnv *env, char *msg, int error_code,
		     int release_msg, int posix_error)
{
	char *daos_msg;
	jstring jmsg = (*env)->NewStringUTF(env, strdup(msg));

	if (error_code > CUSTOM_ERROR_CODE_BASE) {
		const char *temp = posix_error ?
				strerror(error_code) : d_errstr(error_code);

		daos_msg = temp;
	} else {
		daos_msg = NULL;
	}
	jobject obj = (*env)->NewObject(env, daos_io_exception_class,
			new_exception_msg_code_msg, jmsg, error_code,
			daos_msg == NULL ?
				NULL : (*env)->NewStringUTF(env, daos_msg));

	if (release_msg) {
		free(msg);
	}
	return (*env)->Throw(env, obj);
}

int
throw_exception(JNIEnv *env, char *msg, int error_code)
{
	return throw_exception_base(env, msg, error_code, 1, 1);
}

int
throw_exception_object(JNIEnv *env, char *msg, int error_code)
{
	return throw_exception_base(env, msg, error_code, 1, 0);
}

int
throw_exception_const_msg(JNIEnv *env, char *msg, int error_code)
{
	return throw_exception_base(env, msg, error_code, 0, 1);
}

int
throw_exception_const_msg_object(JNIEnv *env, char *msg, int error_code)
{
	return throw_exception_base(env, msg, error_code, 0, 0);
}

/**
 * This function is called when JVM unload native library.
 *
 * \param[in]	vm		Java vm
 * \param[in]	reserved	reserved for future
 */
void
JNI_OnUnload(JavaVM *vm, void *reserved)
{
	JNIEnv *env;

	if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION) != JNI_OK) {
		return;
	}
	(*env)->DeleteGlobalRef(env, daos_io_exception_class);
	(*env)->DeleteGlobalRef(env, new_exception_msg);
	(*env)->DeleteGlobalRef(env, new_exception_cause);
	(*env)->DeleteGlobalRef(env, new_exception_msg_code_msg);
	(*env)->DeleteGlobalRef(env, new_exception_msg_code_cause);
	daos_eq_lib_fini();
	daos_fini();
}
