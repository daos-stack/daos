/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
	new_exception_msg = (*env)->GetMethodID(env, daos_io_exception_class,
			"<init>",
			"(Ljava/lang/String;)V");
	if (new_exception_msg == NULL) {
		printf("failed to get constructor msg\n");
		return JNI_ERR;
	}
	new_exception_cause = (*env)->GetMethodID(env, daos_io_exception_class,
			"<init>",
			"(Ljava/lang/Throwable;)V");
	if (new_exception_cause == NULL) {
		printf("failed to get constructor cause\n");
		return JNI_ERR;
	}
	new_exception_msg_code_msg = (*env)->GetMethodID(env,
			daos_io_exception_class,
			"<init>",
			"(Ljava/lang/String;ILjava/lang/String;)V");
	if (new_exception_msg_code_msg == NULL) {
		printf("failed to get constructor msg, code and daos msg\n");
		return JNI_ERR;
	}
	new_exception_msg_code_cause = (*env)->GetMethodID(env,
			daos_io_exception_class,
			"<init>",
			"(Ljava/lang/String;ILjava/lang/Throwable;)V");
	if (new_exception_msg_code_cause == NULL) {
		printf("failed to get constructor msg, code and cause\n");
		return JNI_ERR;
	}
	int rc = daos_init();

	if (rc) {
		printf("daos_init() failed with rc = %d\n", rc);
		printf("error msg: %.256s\n", d_errstr(rc));
		return rc;
	}
	return JNI_VERSION;
}

int
throw_base(JNIEnv *env, char *msg, int error_code,
	   int release_msg, int posix_error)
{
	char *daos_msg;
	jstring jmsg = (*env)->NewStringUTF(env, msg);

	if (error_code > CUSTOM_ERROR_CODE_BASE) {
		const char *temp = posix_error ?
				strerror(error_code) : d_errstr(error_code);

		daos_msg = (char *)temp;
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
throw_exc(JNIEnv *env, char *msg, int error_code)
{
	return throw_base(env, msg, error_code, 1, 1);
}

int
throw_obj(JNIEnv *env, char *msg, int error_code)
{
	return throw_base(env, msg, error_code, 1, 0);
}

int
throw_const(JNIEnv *env, char *msg, int error_code)
{
	return throw_base(env, msg, error_code, 0, 1);
}

int
throw_const_obj(JNIEnv *env, char *msg, int error_code)
{
	return throw_base(env, msg, error_code, 0, 0);
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
	daos_fini();
}
