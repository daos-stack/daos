/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define _GNU_SOURCE

#include "io_daos_dfs_DaosFsClient.h"
#include <sys/stat.h>
#include <sys/xattr.h>
#include <dirent.h>
#include <gurt/common.h>
#include <libgen.h>
#include <stdio.h>
#include <daos.h>
#include <daos_security.h>
#include <daos_fs.h>
#include <daos_uns.h>
#include <daos_jni_common.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include "DunsAttribute.pb-c.h"

/**
 * JNI method to mount FS on given pool and container.
 *
 * \param[in]	env		JNI environment
 * \param[in]	clientClass	class of DaosFsClient
 * \param[in]	poolHandle	pool handle
 * \param[in]	contHandle	container handle
 * \param[in]	readOnly	is read only FS
 *
 * \return	address of dfs object
 */
JNIEXPORT jlong JNICALL
Java_io_daos_dfs_DaosFsClient_dfsMountFs(JNIEnv *env,
		jclass clientClass, jlong poolHandle, jlong contHandle,
		jboolean readOnly)
{
	int flags = readOnly ? O_RDONLY : O_RDWR;
	dfs_t *dfsPtr;
	daos_handle_t poh;
	daos_handle_t coh;

	memcpy(&poh, &poolHandle, sizeof(poh));
	memcpy(&coh, &contHandle, sizeof(coh));
	int rc = dfs_mount(poh, coh, flags, &dfsPtr);

	if (rc) {
		throw_const(env, "Failed to mount fs",
			    rc);
		return -1;
	}
	return *(jlong *)&dfsPtr;
}

/**
 * JNI method to unmount FS denoted by \a dfsPtr.
 *
 * \param[in]	env		JNI environment
 * \param[in]	clientClass	class of DaosFsClient
 * \param[in]	dfsPtr		address of dfs object
 */
JNIEXPORT void JNICALL
Java_io_daos_dfs_DaosFsClient_dfsUnmountFs(JNIEnv *env,
		jclass clientClass, jlong dfsPtr)
{
	dfs_t *dfs = *(dfs_t **)&dfsPtr;
	int rc = dfs_umount(dfs);

	if (rc) {
		printf("Failed to unmount fs rc: %d\n", rc);
		printf("error msg: %.256s\n", strerror(rc));
	}
}

/**
 * JNI method to move file from \a srcPath to \a destPath.
 *
 * \param[in]	env		JNI environment
 * \param[in]	obj		object of DaosFsClient
 * \param[in]	dfsPtr		pointer to dfs object
 * \param[in]	srcPath		source path
 * \param[in]	destPath	destination path
 */
JNIEXPORT void JNICALL
Java_io_daos_dfs_DaosFsClient_move__JLjava_lang_String_2Ljava_lang_String_2(
		JNIEnv *env, jobject obj, jlong dfsPtr, jstring srcPath,
		jstring destPath)
{
	if (srcPath == NULL || destPath == NULL) {
		char *msg = "Empty source path or empty dest path";

		throw_const(env, msg, CUSTOM_ERR6);
		return;
	}
	const char *src_path = (*env)->GetStringUTFChars(env, srcPath, NULL);
	const char *dest_path = (*env)->GetStringUTFChars(env, destPath, NULL);
	char *src_dir_path = NULL;
	char *src_base_path = NULL;
	char *dest_dir_path = NULL;
	char *dest_base_path = NULL;

	src_dir_path = strdup(src_path);
	src_base_path = strdup(src_path);
	if (src_dir_path == NULL || src_base_path == NULL) {
		char *msg = NULL;

		asprintf(&msg, "Failed to duplicate source path, len: %ld",
			 strlen(src_path));
		throw_base(env, msg, CUSTOM_ERR3, 1, 0);
		goto out;
	}
	dest_dir_path = strdup(dest_path);
	dest_base_path = strdup(dest_path);
	if (dest_dir_path == NULL || dest_base_path == NULL) {
		char *msg = NULL;

		asprintf(&msg, "Failed to duplicate dest path, len: %ld",
			 strlen(dest_path));
		throw_base(env, msg, CUSTOM_ERR3, 1, 0);
		goto out;
	}
	char *src_dir = dirname(src_dir_path);
	char *src_base = basename(src_base_path);
	char *dest_dir = dirname(dest_dir_path);
	char *dest_base = basename(dest_base_path);
	dfs_obj_t *src_dir_handle = NULL;
	dfs_obj_t *dest_dir_handle = NULL;
	mode_t tmp_mode;
	dfs_t *dfs = *(dfs_t **)&dfsPtr;
	int rc = dfs_lookup(dfs, src_dir, O_RDWR, &src_dir_handle, &tmp_mode,
			NULL);

	if (rc) {
		char *msg = NULL;

		asprintf(&msg, "Cannot open source directory (%s)", src_dir);
		throw_exc(env, msg, rc);
		goto out;
	}
	if (strcmp(src_dir, dest_dir) == 0) {
		dest_dir_handle = src_dir_handle;
	} else {
		rc = dfs_lookup(dfs, dest_dir, O_RDWR, &dest_dir_handle,
				&tmp_mode, NULL);
		if (rc) {
			char *msg = NULL;

			asprintf(&msg,
				 "Cannot open destination directory (%s)",
				 dest_dir);
			throw_exc(env, msg, rc);
			goto out;
		}
	}
	rc = dfs_move(dfs, src_dir_handle, src_base, dest_dir_handle, dest_base,
			NULL);
	if (rc) {
		char *msg = NULL;

		asprintf(&msg,
			 "Failed to move source path (%s) to destination "
			 "path (%s)", src_path, dest_path);
		throw_exc(env, msg, rc);
	}
out:
	if (src_dir_path) free(src_dir_path);
	if (src_base_path) free(src_base_path);
	if (dest_dir_path) free(dest_dir_path);
	if (dest_base_path) free(dest_base_path);
	if (src_dir_handle) dfs_release(src_dir_handle);
	if (src_dir_handle != dest_dir_handle) dfs_release(dest_dir_handle);
	(*env)->ReleaseStringUTFChars(env, srcPath, src_path);
	(*env)->ReleaseStringUTFChars(env, destPath, dest_path);
}

/**
 * move file from \a srcName under directory denoted by \a srcPrtObjId
 * to \a destName under directory denoted by \a destPrtObjId.
 * This method is more efficient than last move(srcPath, destPath) since we
 * don't need to open both source directory and destination directory.
 *
 * \param[in]	env		JNI environment
 * \param[in]	obj		object of DaosFsClient
 * \param[in]	dfsPtr		pointer to dfs object
 * \param[in]	srcPrtObjId	source parent object id
 * \param[in]	srcName		source name, just file name without any path
 * \param[in]	destPrtObjId	dest parent object id
 * \param[in]	destName	destination name, just file name without any
 * 				path
 */
JNIEXPORT void JNICALL
Java_io_daos_dfs_DaosFsClient_move__JJLjava_lang_String_2JLjava_lang_String_2(
		JNIEnv *env, jobject obj, jlong dfsPtr, jlong srcPrtObjId,
		jstring srcName, jlong destPrtObjId, jstring destName)
{
	if (srcName == NULL || destName == NULL) {
		throw_const(env,
			    "Empty source name or empty dest name",
			    CUSTOM_ERR6);
		return;
	}
	const char *src_base = (*env)->GetStringUTFChars(env, srcName, NULL);
	const char *dest_base = (*env)->GetStringUTFChars(env, destName, NULL);
	dfs_obj_t *src_dir_handle = *(dfs_obj_t **)&srcPrtObjId;
	dfs_obj_t *dest_dir_handle = *(dfs_obj_t **)&destPrtObjId;
	mode_t tmp_mode;
	dfs_t *dfs = *(dfs_t **)&dfsPtr;
	int rc = dfs_move(dfs, src_dir_handle, (char *)src_base, dest_dir_handle,
			(char *)dest_base, NULL);

	if (rc) {
		char *msg = NULL;

		asprintf(&msg,
			 "Failed to move source file (%s) under dir (%ld)"
			 " to destination file (%s) under dir (%ld)", src_base,
			 srcPrtObjId, dest_base, destPrtObjId);
		throw_exc(env, msg, rc);
	}
out:
	(*env)->ReleaseStringUTFChars(env, srcName, src_base);
	(*env)->ReleaseStringUTFChars(env, destName, dest_base);
}

static inline void
copy_msg(char *msg, const char *dir)
{
	if (msg[0] == '\0') {
		int dir_len = strlen(dir);
		int max_len = ERROR_PATH_LEN - 4;
		int l = dir_len > max_len ? max_len : dir_len;

		memcpy(msg, dir, l);
		if (l == max_len) {
			char *suffix = "...\0";

			memcpy(msg + l, suffix, 4);
		} else {
			msg[l] = '\0';
		}
	}
}

/**
 * utility function to create directories. It could be called recursively.
 *
 * \param[in]		dfs		dfs object
 * \param[in]		path		path of directory
 * \param[in]		mode		mode of directory
 * \param[in]		recursive	recursively create ancestors
 * \param[in,out]	handle		handle of open DAOS object
 * \param[in,out]	msg		error message during creation
 *
 * \return	0 for success, non-zero for failure
 */
static int
mkdirs(dfs_t *dfs, const char *path, int mode,
		unsigned char recursive,
		dfs_obj_t **handle, char *msg)
{
	char *dirs = NULL;
	char *bases = NULL;
	char *dir = NULL;
	char *base = NULL;
	dfs_obj_t *parent_handle = NULL;
	mode_t parent_mode;
	int rc = dfs_lookup(dfs, path, O_RDWR, handle, &parent_mode, NULL);

	if (rc == -DER_NONEXIST || rc == -ENOENT || rc == ENOENT) {
		if (!recursive) {
			goto out;
		}
		/* recursively create it */
		dirs = strdup(path);
		bases = strdup(path);
		if (dirs == NULL || bases == NULL) {
			copy_msg(msg, path);
			goto out;
		}
		dir = dirname(dirs);
		base = basename(bases);
		rc = mkdirs(dfs, dir, mode, recursive, &parent_handle,
				msg);
		if (rc)	{
			copy_msg(msg, dir);
			goto out;
		}
		rc = dfs_mkdir(dfs, parent_handle, base, mode, 0);
		/* code to mitigate DAOS concurrency issue */
		/* to be fixed by the conditional update feature in DAOS */
		if (rc == ERROR_NOT_EXIST) {
			int count = 0;

			while (rc && (count < ERROR_LOOKUP_MAX_RETRIES)) {
				rc = dfs_lookup(dfs, path, O_RDWR, handle,
						&parent_mode, NULL);
				count++;
			}
		} else {
			rc = dfs_lookup(dfs, path, O_RDWR, handle, &parent_mode,
					NULL);
		}
	}
out:
	if (dirs) free(dirs);
	if (bases) free(bases);
	if (parent_handle) dfs_release(parent_handle);
	return rc;
}

/**
 * JNI method to create directory. If parent directory doesn't exist and
 * \a recursive is 0, a Java exception will be thrown.
 *
 * \param[in]	env		JNI environment
 * \param[in]	client		DaosFsClient object
 * \param[in]	dfsPtr		pointer to dfs object
 * \param[in]	path		path to directory to be created
 * \param[in]	mode		mode of directory
 * \param[in]	recursive	create directory recursively
 */
JNIEXPORT void JNICALL
Java_io_daos_dfs_DaosFsClient_mkdir(JNIEnv *env, jobject client,
		jlong dfsPtr, jstring path, jint mode, jboolean recursive)
{
	if (path == NULL) {
		throw_const(env, "Empty path", CUSTOM_ERR6);
		return;
	}
	const char* path_str = (*env)->GetStringUTFChars(env, path, NULL);
	dfs_t *dfs = *(dfs_t **)&dfsPtr;
	char *dirs;
	char *bases;
	char *parent_dir;
	char *base;
	dfs_obj_t *parent_handle = NULL;
	mode_t parent_mode;
	char *parentError = NULL;
	int rc = 0;

	dirs = strdup(path_str);
	bases = strdup(path_str);
	if (dirs == NULL || bases == NULL) {
		char *msg = NULL;

		asprintf(&msg, "Failed to duplicate path %s",
			 path_str);
		throw_base(env, msg, 1, 1, 0);
		goto out;
	}
	parent_dir = dirname(dirs);
	base = basename(bases);

	if ((strlen(parent_dir) > 0) &&
			(strcmp(parent_dir, "/") != 0)) {
		parentError = (char *)malloc(ERROR_PATH_LEN);
		if (parentError == NULL) {
			char *msg = NULL;

			asprintf(&msg,
				 "Failed to allocate char array, len %d",
				 ERROR_PATH_LEN);
			throw_base(env, msg, 1, 1, 0);
			goto out;
		}
		parentError[0] = '\0';
		rc = mkdirs(dfs, parent_dir, mode, recursive, &parent_handle,
				parentError);
	}
	if (rc) {
		char *dir_msg = parent_dir;
		char *tmp;

		if (recursive) {
			tmp = "Failed to create parent or ancestor "
					"directories (%s)";
			if (parentError[0] != '\0') {
				dir_msg = parentError;
			}
		} else {
			tmp = "Parent directory doesn't exist (%s)";
		}
		char *msg = NULL;

		asprintf(&msg, tmp, dir_msg);
		throw_exc(env, msg, rc);
	} else {
		rc = dfs_mkdir(dfs, parent_handle, base, mode, 0);
		if (rc) {
			char *tmp = "Failed to create directory (%s) "
					"under parent directory (%s)";
			char *msg = NULL;

			asprintf(&msg, tmp, base, parent_dir);
			throw_exc(env, msg, rc);
		}
	}
out:
	if (dirs) free(dirs);
	if (bases) free(bases);
	if (parentError) free(parentError);
	if (parent_handle) dfs_release(parent_handle);
	(*env)->ReleaseStringUTFChars(env, path, path_str);
}

/**
 * JNI method to create new file, \a name, under directory \a parentPath.
 * Java exception will be thrown if \a parentPath doesn't exist and
 * \a createParent is 0.
 *
 * \param[in]	env		JNI environment
 * \param[in]	client		DaosFsClient object
 * \param[in]	dfsPtr		pointer to dfs object
 * \param[in]	parentPath	path to directory under which new file is
 * 				created
 * \param[in]	name		new file name
 * \param[in]	mode		file mode
 * \param[in]	accessFlags	file access flags
 * \param[in]	objectType	object type of this new file
 * \param[in]	chunkSize	file chunk size
 * \param[in]	createParent	create parent directory recursively if it
 * 				doesn't exist
 *
 * \return	memory address of opened dfs object of new file
 */
JNIEXPORT jlong JNICALL
Java_io_daos_dfs_DaosFsClient_createNewFile(JNIEnv *env,
		jobject client, jlong dfsPtr, jstring parentPath, jstring name,
		jint mode, jint accessFlags, jstring objectType, jint chunkSize,
		jboolean createParent)
{
	if (parentPath == NULL || name == NULL) {
		throw_const(env,
			    "Empty parent path or empty name",
			    CUSTOM_ERR6);
		return -1;
	}

	dfs_t *dfs = *(dfs_t **)&dfsPtr;
	const char *parent_path = (*env)->GetStringUTFChars(env, parentPath,
			NULL);
	const char *file_name = (*env)->GetStringUTFChars(env, name, NULL);
	const char *object_type = (*env)->GetStringUTFChars(env, objectType,
								NULL);
	int type_id = daos_oclass_name2id(object_type);
	char *parentError = NULL;
	dfs_obj_t *file = NULL;
	dfs_obj_t *parent = NULL;
	mode_t tmp_mode;

	if (!type_id) {
		char *msg = NULL;

		asprintf(&msg, "unsupported object class, %s", object_type);
		throw_exc(env, msg, CUSTOM_ERR6);
		goto out;
	}
	int rc = dfs_lookup(dfs, parent_path, O_RDWR, &parent,
			    &tmp_mode, NULL);

	if (rc) {
		if (createParent) {
			parentError = (char *)malloc(ERROR_PATH_LEN);
			if (parentError == NULL) {
				char *msg = NULL;
				char *tmp = "Failed to allocate char array,"
					" len %d";

				asprintf(&msg,
					 tmp,
					 ERROR_PATH_LEN);
				throw_base(env, msg, 1, 1, 0);
				goto out;
			}
			parentError[0] = '\0';
			rc = mkdirs(dfs, parent_path, mode, 1, &parent,
					parentError);
			if (rc) {
				const char *dir_msg = parent_path;

				if (parentError[0] != '\0') {
					dir_msg = parentError;
				}
				char *tmp = "Failed to create parent/ancestor "
						"directories (%s)";
				char *msg = NULL;

				asprintf(&msg, tmp, dir_msg);
				throw_exc(env, msg, rc);
				goto out;
			}
		} else {
			char *tmp = "Failed to find parent directory (%s)";
			char *msg = NULL;

			asprintf(&msg, tmp, parent_path);
			throw_exc(env, msg, rc);
			goto out;
		}
	}

	rc = dfs_open(dfs, parent, file_name, S_IFREG | mode,
			O_CREAT | accessFlags, type_id, chunkSize, NULL,
			&file);
	if (rc) {
		char *tmp = "Failed to create new file (%s) under "
				"directory (%s)";
		char *msg = NULL;

		asprintf(&msg, tmp, file_name, parent_path);
		throw_exc(env, msg, rc);
	}
out:
	(*env)->ReleaseStringUTFChars(env, parentPath, parent_path);
	(*env)->ReleaseStringUTFChars(env, name, file_name);
	(*env)->ReleaseStringUTFChars(env, objectType, object_type);
	if (parentError) free(parentError);
	if (parent) dfs_release(parent);
	return *(jlong *)&file;
}

/**
 * JNI method to delete a file, \a name, from directory \a parentPath.
 *
 * \param[in]	env		JNI environment
 * \param[in]	client		DaosFsClient object
 * \param[in]	dfsPtr		pointer to dfs object
 * \param[in]	parentPath	directory from which file is deleted
 * \param[in]	name		file name
 * \param[in]	force		force delete if it's directory
 *
 * \return	0 if failed to delete, 1 if deleted successfully
 */
JNIEXPORT jboolean JNICALL
Java_io_daos_dfs_DaosFsClient_delete(JNIEnv *env, jobject client,
		jlong dfsPtr, jstring parentPath, jstring name,
		jboolean force)
{
	if (parentPath == NULL || name == NULL) {
		throw_const(env,
			    "Empty parent path or empty name",
			    CUSTOM_ERR6);
		return 0;
	}
	dfs_t *dfs = *(dfs_t **)&dfsPtr;
	const char *parent_path = (*env)->GetStringUTFChars(env, parentPath,
					NULL);
	const char *file_name = (*env)->GetStringUTFChars(env, name, NULL);
	dfs_obj_t *parent = NULL;
	mode_t tmp_mode;
	int rc;

	if ((strlen(parent_path) > 0) &&
			(strcmp(parent_path, "/") != 0)) {
		rc = dfs_lookup(dfs, parent_path, O_RDWR, &parent, &tmp_mode,
				NULL);
		if (rc) {
			goto out;
		}
	}
	dfs_remove(dfs, parent, file_name, force, NULL);
out:
	(*env)->ReleaseStringUTFChars(env, parentPath, parent_path);
	(*env)->ReleaseStringUTFChars(env, name, file_name);
	if (parent) dfs_release(parent);
	return 1;
}

/**
 * JNI method to open file, \a name, under directory denoted by open object,
 * \a parentObjId, which is pointer to DAOS fs object.
 *
 * \param[in]	env		JNI environment
 * \param[in]	client		DaosFsClient object
 * \param[in]	dfsPtr		pointer to dfs object
 * \param[in]	parentObjId	pointer to opened fs object
 * \param[in]	name		file name
 * \param[in]	flags		file flags
 * \param[in]	bufferAddress	buffer to hold stat attributes, not implemented
 * 				yet
 *
 * \return	memory address of opened fs object.
 */
JNIEXPORT jlong JNICALL
Java_io_daos_dfs_DaosFsClient_dfsLookup__JJLjava_lang_String_2IJ(
		JNIEnv *env, jobject client, jlong dfsPtr, jlong parentObjId,
		jstring name, jint flags, jlong bufferAddress)
{
	if (name == NULL) {
		throw_const(env, "Empty name", CUSTOM_ERR6);
		return -1;
	}
	dfs_t *dfs = *(dfs_t **)&dfsPtr;
	dfs_obj_t *parent = *(dfs_obj_t **)&parentObjId;
	dfs_obj_t *file;
	mode_t tmp_mode;
	const char *file_name = (*env)->GetStringUTFChars(env, name, NULL);
	int rc = dfs_lookup_rel(dfs, parent, file_name, flags, &file, &tmp_mode,
				NULL);

	if (rc) {
		char *tmp = "Failed to open file (%s) under parent with "
				"flags (%d)";
		char *msg = NULL;

		asprintf(&msg, tmp, file_name, flags);
		throw_exc(env, msg, rc);
		file = NULL;
	}
	(*env)->ReleaseStringUTFChars(env, name, file_name);
	return *(jlong *)&file;
}

/**
 * JNI method to open file, \a name, under directory denoted by,
 * \a path.
 *
 * \param[in]	env		JNI environment
 * \param[in]	client		DaosFsClient object
 * \param[in]	dfsPtr		pointer to dfs object
 * \param[in]	path		path of file
 * \param[in]	flags		file flags
 * \param[in]	bufferAddress	buffer to hold stat attributes, not implemented
 * 				yet
 *
 * \return	memory address of opened fs object.
 */
JNIEXPORT jlong JNICALL
Java_io_daos_dfs_DaosFsClient_dfsLookup__JLjava_lang_String_2IJ(
		JNIEnv *env, jobject client, jlong dfsPtr, jstring path,
		jint flags, jlong bufferAddress)
{
	if (path == NULL) {
		throw_const(env, "Empty path", CUSTOM_ERR6);
		return -1;
	}
	dfs_t *dfs = *(dfs_t **)&dfsPtr;
	dfs_obj_t *file;
	mode_t tmp_mode;
	const char *file_path = (*env)->GetStringUTFChars(env, path, NULL);
	int rc = dfs_lookup(dfs, file_path, flags, &file, &tmp_mode, NULL);

	if (rc) {
		char *tmp = "Failed to open file (%s) with flags (%d)";
		char *msg = NULL;

		asprintf(&msg, tmp, file_path, flags);
		throw_exc(env, msg, rc);
		file = NULL;
	}
	(*env)->ReleaseStringUTFChars(env, path, file_path);
	return *(jlong *)&file;
}

/**
 * JNI method to get size of file denoted by \a objId.
 *
 * \param[in]	env		JNI environment
 * \param[in]	client		DaosFsClient object
 * \param[in]	dfsPtr		pointer to dfs object
 * \param[in]	objId		pointer to opened fs object
 *
 * \return	size of file
 */
JNIEXPORT jlong JNICALL
Java_io_daos_dfs_DaosFsClient_dfsGetSize(JNIEnv *env, jobject client,
		jlong dfsPtr, jlong objId)
{
	dfs_t *dfs = *(dfs_t **)&dfsPtr;
	dfs_obj_t *file = *(dfs_obj_t **)&objId;
	daos_size_t size;
	int rc = dfs_get_size(dfs, file, &size);

	if (rc) {
		throw_const(env,
			    "Failed to get file size", rc);
		return -1;
	}
	return size;
}

/**
 * JNI method to duplicae a file denoted by \a objId.
 *
 * \param[in]	env		JNI environment
 * \param[in]	client		DaosFsClient object
 * \param[in]	dfsPtr		pointer to dfs object
 * \param[in]	objId		pointer to opened fs object
 * \param[in]	flags		flags of new file
 *
 * \return	memory address of new file object
 */
JNIEXPORT jlong JNICALL
Java_io_daos_dfs_DaosFsClient_dfsDup(JNIEnv *env, jobject client,
		jlong dfsPtr, jlong objId, jint flags)
{
	dfs_t *dfs = *(dfs_t **)&dfsPtr;
	dfs_obj_t *file = *(dfs_obj_t **)&objId;
	dfs_obj_t *new_file;
	int rc = dfs_dup(dfs, file, flags, &new_file);

	if (rc) {
		throw_const(env,
			    "Failed to duplicate file", rc);
		return -1;
	}
	return *(jlong *)&new_file;
}

/**
 * JNI method to release a file denoted by \a objId.
 *
 * \param[in]	env		JNI environment
 * \param[in]	clientClass	DaosFsClient class
 * \param[in]	objId		pointer to fs object
 */
JNIEXPORT void JNICALL
Java_io_daos_dfs_DaosFsClient_dfsRelease(JNIEnv *env,
		jclass clientClass, jlong objId)
{
	dfs_obj_t *file = *(dfs_obj_t **)&objId;
	int rc = dfs_release(file);

	if (rc) {
		throw_const(env,
			    "Failed to release file", rc);
	}
}

/**
 * allocate and initialize dfs description.
 *
 * \param[in]	env		JNI environment
 * \param[in]	clientClass	DaosFsClient class
 * \param[in]	objId		pointer to fs object
 *
 * \return  pointer address of dfsDesc
 */
JNIEXPORT jlong JNICALL
Java_io_daos_dfs_DaosFsClient_allocateDfsDesc(JNIEnv *env,
					      jclass clientClass,
					      jlong descBufAddress)
{
	uint64_t value64;
	uint16_t value16;
	char *desc_buffer = (char *)descBufAddress;
	dfs_desc_t *desc = (dfs_desc_t *)malloc(
		sizeof(dfs_desc_t));

	desc_buffer += 8; /* reserve for handle */
	memcpy(&value64, desc_buffer, 8);
	desc_buffer += 8;
	desc->sgl.sg_iovs = &desc->iov;
	d_iov_set(&desc->iov, (char *)value64, 0);
	/* event queue */
	memcpy(&value64, desc_buffer, 8);
	desc->eq = (event_queue_wrapper_t *)value64;
	/* move by 8 and skip offset, length, event id */
	desc_buffer += 26;
	desc->ret_buf_address = (uint64_t)desc_buffer;
	desc->event = NULL;
	/* copy back address */
	memcpy((char *)descBufAddress, &desc, 8);
	return *(jlong *)&desc;
}

/**
 * release dfs description.
 *
 * \param[in]	env		JNI environment
 * \param[in]	clientClass	DaosFsClient class
 * \param[in]	descHandle	handle to dfs description
 */
JNIEXPORT void JNICALL
Java_io_daos_dfs_DaosFsClient_releaseDfsDesc(JNIEnv *env, jclass clientClass,
					     jlong descHandle)
{
	dfs_desc_t *desc = (dfs_desc_t *)descHandle;

	free(desc);
}

/**
 * JNI method to read a file denoted by \a objId, into buffer denoted by
 * \a bufferAddress.
 *
 * \param[in]	env		JNI environment
 * \param[in]	client		DaosFsClient object
 * \param[in]	dfsPtr		pointer to dfs object
 * \param[in]	objId		pointer to opened fs object
 * \param[in]	bufferAddress	buffer address
 * \param[in]	fileOffset	file offset
 * \param[in]	len		requested length of bytes to be read
 * \param[in]	eventNo		event no for asynchronous read
 *
 * \return	actual read length
 */
JNIEXPORT jlong JNICALL
Java_io_daos_dfs_DaosFsClient_dfsRead(JNIEnv *env, jobject client,
		jlong dfsPtr, jlong objId, jlong bufferAddress,
		jlong fileOffset, jlong len)
{
	dfs_t *dfs = *(dfs_t **)&dfsPtr;
	dfs_obj_t *file = *(dfs_obj_t **)&objId;
	char *buf = (char *)bufferAddress;
	d_iov_t sg_iov = {0};
	d_sg_list_t sgl = {
		.sg_nr = 1,
		.sg_nr_out = 0,
		.sg_iovs = &sg_iov
	};

	d_iov_set(&sg_iov, buf, len);
	daos_size_t size = 0;
	int rc = dfs_read(dfs, file, &sgl, fileOffset, &size, NULL);

	if (rc) {
		char *tmp = "Failed to read %ld bytes from file starting "
				"at %ld";
		char *msg = NULL;

		asprintf(&msg, tmp, len, fileOffset);
		throw_exc(env, msg, rc);
		return 0;
	}
	return size;
}

static inline void
decode_dfs_desc(char *buf, dfs_desc_t **desc_ret, uint64_t *offset_ret,
		uint64_t *len)
{
	uint64_t dfs_mem;
	uint16_t eid;
	dfs_desc_t *desc;

	memcpy(&dfs_mem, buf, 8);
	desc = (dfs_desc_t *)dfs_mem;
	*desc_ret = desc;
	desc->sgl.sg_nr = 1;
	desc->sgl.sg_nr_out = 0;
	desc->size = 0;
	buf += 24; /* skip native handle, data mem address and eq handle */
	memcpy(offset_ret, buf, 8);
	buf += 8;
	memcpy(len, buf, 8);
	buf += 8;
	desc->iov.iov_len = desc->iov.iov_buf_len = (size_t)(*len);
	/* event */
	memcpy(&eid, buf, 2);
	desc->event = desc->eq->events[eid];
}

static int
update_actual_size(void *udata, daos_event_t *ev, int ret)
{
	dfs_desc_t *desc = (dfs_desc_t *)udata;
	char *desc_buffer = (char *)desc->ret_buf_address;
	uint32_t value = (uint32_t)desc->size;

	memcpy(desc_buffer, &ret, 4);
	desc_buffer += 4;
	memcpy(desc_buffer, &value, 4);
	desc->event->status = 0;
	return 0;
}

JNIEXPORT void JNICALL
Java_io_daos_dfs_DaosFsClient_dfsReadAsync(JNIEnv *env, jobject client,
					   jlong dfsPtr, jlong objId,
					   jlong descBufAddress)
{
	dfs_t *dfs = *(dfs_t **)&dfsPtr;
	dfs_obj_t *file = *(dfs_obj_t **)&objId;
	char *buf = (char *)descBufAddress;
	uint64_t offset;
	uint64_t len;
	dfs_desc_t *desc;
	int rc;

	decode_dfs_desc(buf, &desc, &offset, &len);
	desc->event->event.ev_error = 0;
	rc = daos_event_register_comp_cb(&desc->event->event,
					 update_actual_size, desc);
	if (rc) {
		char *msg = "Failed to register dfs read callback";

		throw_const_obj(env, msg, rc);
		return;
	}
	desc->event->status = EVENT_IN_USE;
	rc = dfs_read(dfs, file, &desc->sgl, offset, &desc->size, &desc->event->event);
	if (rc) {
		char *msg;

		asprintf(&msg,
			 "Failed to read %ld bytes from file starting at %ld",
			 len, offset);
		throw_exc(env, msg, rc);
	}
}

/**
 * JNI method to write data from buffer denoted by \a bufferAddress to
 * a file denoted by \a objId.
 *
 * \param[in]	env		JNI environment
 * \param[in]	client		DaosFsClient object
 * \param[in]	dfsPtr		pointer to dfs object
 * \param[in]	objId		pointer to opened fs object
 * \param[in]	bufferAddress	buffer address
 * \param[in]	fileOffset	file offset
 * \param[in]	len		requested length of bytes to write
 * \param[in]	eventNo		event no for asynchronous write
 *
 * \return	actual write length
 */
JNIEXPORT jlong JNICALL
Java_io_daos_dfs_DaosFsClient_dfsWrite(JNIEnv *env, jobject client,
		jlong dfsPtr, jlong objId, jlong bufferAddress,
		jlong fileOffset, jlong len)
{
	dfs_t *dfs = *(dfs_t **)&dfsPtr;
	dfs_obj_t *file = *(dfs_obj_t **)&objId;
	char *buf = (char *)bufferAddress;
	d_iov_t sg_iov = {0};
	d_sg_list_t sgl = {
		.sg_nr = 1,
		.sg_nr_out = 0,
		.sg_iovs = &sg_iov
	};

	d_iov_set(&sg_iov, buf, len);
	int rc = dfs_write(dfs, file, &sgl, fileOffset, NULL);

	if (rc) {
		char *tmp = "Failed to write %ld bytes to file starting at"
			    " %ld";
		char *msg = NULL;

		asprintf(&msg, tmp, len, fileOffset);
		throw_exc(env, msg, rc);
		return 0;
	}
	return len;
}

static int
update_ret_code(void *udata, daos_event_t *ev, int ret)
{
	dfs_desc_t *desc = (dfs_desc_t *)udata;
	char *desc_buffer = (char *)desc->ret_buf_address;

	memcpy(desc_buffer, &ret, 4);
	desc->event->status = 0;
	return 0;
}

JNIEXPORT void JNICALL
Java_io_daos_dfs_DaosFsClient_dfsWriteAsync(JNIEnv *env, jobject client,
					    jlong dfsPtr, jlong objId,
					    jlong descBufAddress)
{
	dfs_t *dfs = *(dfs_t **)&dfsPtr;
	dfs_obj_t *file = *(dfs_obj_t **)&objId;
	char *buf = (char *)descBufAddress;
	uint64_t offset;
	uint64_t len;
	dfs_desc_t *desc;
	int rc;

	decode_dfs_desc(buf, &desc, &offset, &len);
	desc->event->event.ev_error = 0;
	rc = daos_event_register_comp_cb(&desc->event->event, update_ret_code, desc);
	if (rc) {
		char *msg = "Failed to register dfs write callback";

		throw_const_obj(env, msg, rc);
		return;
	}
	desc->event->status = EVENT_IN_USE;
	rc = dfs_write(dfs, file, &desc->sgl, offset, &desc->event->event);
	if (rc) {
		char *msg;

		asprintf(&msg,
			 "Failed to write %ld bytes from file starting at %ld",
			 len, offset);
		throw_exc(env, msg, rc);
	}
}

/**
 * JNI method to read children entries from directory denoted by \a objId.
 *
 * \param[in]	env		JNI environment
 * \param[in]	client		DaosFsClient object
 * \param[in]	dfsPtr		ointer to dfs object
 * \param[in]	objId		pointer to opened fs object
 * \param[in]	maxEntries	maximum entries to be read. not implemented yet
 *
 * \return	file name separated by ','
 */
JNIEXPORT jstring JNICALL
Java_io_daos_dfs_DaosFsClient_dfsReadDir(JNIEnv *env, jobject client,
		jlong dfsPtr, jlong objId, jint maxEntries)
{
	dfs_t *dfs = *(dfs_t **)&dfsPtr;
	dfs_obj_t *dir = *(dfs_obj_t **)&objId;
	daos_anchor_t anchor = {0};
	uint32_t nr = READ_DIR_BATCH_SIZE;
	struct dirent entries[nr];
	uint32_t size = READ_DIR_INITIAL_BUFFER_SIZE;
	uint32_t acc = 0;
	char *buffer = malloc(size);

	if (!buffer) {
		char *tmp = "Failed to allocate %d bytes for reading "
				"directory content";
		char *msg = NULL;

		asprintf(&msg, tmp, size);
		throw_exc(env, msg, CUSTOM_ERR3);
		return NULL;
	}
	buffer[0] = '\0';
	int rc;
	int total = 0;
	int failed = 0;

	while (!daos_anchor_is_eof(&anchor)) {
		nr = READ_DIR_BATCH_SIZE;
		rc = dfs_readdir(dfs, dir, &anchor, &nr, entries);
		if (rc) {
			char *tmp = "Failed to read %d more entries from "
					"directory after reading %d "
					"entries.\n buffer length: %d";
			char *msg = NULL;

			asprintf(&msg, tmp, READ_DIR_BATCH_SIZE, total, size);
			throw_exc(env, msg, rc);
			failed = 1;
			break;
		}
		if (!nr) continue;
		total += nr;
		int i;

		for (i = 0; i < nr; i++) {
			/* exactly 1 for each file because of separator // and \0 */
			acc += strlen(entries[i].d_name) + 2;
			if (acc >= size) {
				size += READ_DIR_INITIAL_BUFFER_SIZE;
				buffer = realloc(buffer, size);
				if (!buffer) {
					char *tmp = "Failed to re-allocate %d "
					"bytes for reading directory content.";
					char *msg = NULL;

					asprintf(&msg, tmp, size);
					throw_exc(env, msg, CUSTOM_ERR3);
					failed = 1;
					break;
				}
			}
			if (buffer[0]) strcat(buffer, "//");
			strcat(buffer, entries[i].d_name);
		}
	}
	jstring result;

	if ((!failed) && buffer[0] != '\0' ) {
		result = (*env)->NewStringUTF(env, buffer);
	} else {
		result = NULL;
	}
	free(buffer);
	return result;
}

static inline void
cpyfield(JNIEnv *env, char *buffer, void *value,
		int valueLen, int expLen)
{
	if (valueLen != expLen) {
		char *tmp = "value length (%d) not equal to expected (%d)";
		char *msg = NULL;

		asprintf(&msg, tmp, valueLen, expLen);
		throw_exc(env, msg, CUSTOM_ERR4);
		return;
	}
	memcpy(buffer, value, valueLen);
}

static void
set_user_group_name(JNIEnv *env, char *buffer, struct stat *stat)
{
	struct passwd *uentry = getpwuid(stat->st_uid);
	struct group *gentry = getgrgid(stat->st_gid);
	int inc = 4;
	int len = 0;

	if (uentry != NULL) {
		len = strlen(uentry->pw_name);
		if (len > 32) {
			len = 32;
		}
		cpyfield(env, buffer, &len, 4, 4);
		memcpy(buffer+4, uentry->pw_name, len);
		inc += len;
	} else {
		len = 0;
		cpyfield(env, buffer, &len, 4, 4);
	}
	if (gentry != NULL) {
		len = strlen(gentry->gr_name);
		if (len > 32) {
			len = 32;
		}
		cpyfield(env, buffer+inc, &len, 4, 4);
		memcpy(buffer+inc+4, gentry->gr_name, len);
	} else {
		len = 0;
		cpyfield(env, buffer+inc, &len, 4, 4);
	}
}

/**
 * JNI method to get stat attributes into buffer denoted by \a bufferAddress
 *  from a file denoted by \a objId.
 *
 * \param[in]	env		JNI environment
 * \param[in]	client		DaosFsClient object
 * \param[in]	dfsPtr		ointer to dfs object
 * \param[in]	objId		pointer to opened fs object
 * \param[in]	bufferAddress	pointer to opened fs object
 */
JNIEXPORT void JNICALL
Java_io_daos_dfs_DaosFsClient_dfsOpenedObjStat(JNIEnv *env,
		jobject client, jlong dfsPtr, jlong objId,
		jlong bufferAddress)
{
	dfs_t *dfs = *(dfs_t **)&dfsPtr;
	dfs_obj_t *file = *(dfs_obj_t **)&objId;
	struct stat stat = {};
	int rc = dfs_ostat(dfs, file, &stat);

	if (rc) {
		char *msg = "Failed to get StatAttribute of open object";

		throw_const(env, msg, rc);
	} else {
		if (bufferAddress == -1L) {
			return;
		}
		char *buffer = (char *)bufferAddress;

		cpyfield(env, buffer, &objId, sizeof(objId), 8);
		cpyfield(env, buffer + 8, &stat.st_mode,
			 sizeof(stat.st_mode), 4);
		cpyfield(env, buffer + 12, &stat.st_uid,
			 sizeof(stat.st_uid), 4);
		cpyfield(env, buffer + 16, &stat.st_gid,
			 sizeof(stat.st_gid), 4);
		cpyfield(env, buffer + 20, &stat.st_blocks,
			 sizeof(stat.st_blocks), 8);
		cpyfield(env, buffer + 28, &stat.st_blksize,
			 sizeof(stat.st_blksize), 8);
		cpyfield(env, buffer + 36, &stat.st_size,
			 sizeof(stat.st_size), 8);
		cpyfield(env, buffer + 44, &stat.st_atim,
			 sizeof(stat.st_atim), 16);
		cpyfield(env, buffer + 60, &stat.st_mtim,
			 sizeof(stat.st_mtim), 16);
		cpyfield(env, buffer + 76, &stat.st_ctim,
			 sizeof(stat.st_ctim), 16);
		buffer[92] = S_ISDIR(stat.st_mode) ? '\0':'1';
		set_user_group_name(env, buffer + 93, &stat);
	}
}

/**
 * JNI method to set extended attribute denoted by \a name
 * to file denoted by \a objId.
 *
 * \param[in]	env		JNI environment
 * \param[in]	client		DaosFsClient object
 * \param[in]	dfsPtr		ointer to dfs object
 * \param[in]	objId		pointer to opened fs object
 * \param[in]	name		attribute name
 * \param[in]	value		attribute value
 * \param[in]	flags		attribute flags
 */
JNIEXPORT void JNICALL
Java_io_daos_dfs_DaosFsClient_dfsSetExtAttr(JNIEnv *env,
		jobject client, jlong dfsPtr, jlong objId, jstring name,
		jstring value, jint flags)
{
	if (name == NULL || value == NULL) {
		throw_const(env,
			    "Empty name or empty value",
			    CUSTOM_ERR6);
		return;
	}
	dfs_t *dfs = *(dfs_t **)&dfsPtr;
	dfs_obj_t *file = *(dfs_obj_t **)&objId;
	const char *attr_name = (*env)->GetStringUTFChars(env, name, NULL);
	const char *attr_value = (*env)->GetStringUTFChars(env, value, NULL);
	int rc = dfs_setxattr(dfs, file, attr_name, attr_value,
			(uint64_t)strlen(attr_value), flags);

	if (rc) {
		char *tmp = "Failed to set ext attribute name: %s, "
				"value %s with flags %d.";
		char *msg = NULL;

		asprintf(&msg, tmp, attr_name, attr_value, flags);
		throw_exc(env, msg, rc);
	}
	(*env)->ReleaseStringUTFChars(env, name, attr_name);
	(*env)->ReleaseStringUTFChars(env, value, attr_value);
}

/**
 * JNI method to get extended attribute denoted by \a name
 * from file denoted by \a objId.
 *
 * \param[in]	env			JNI environment
 * \param[in]	client			DaosFsClient object
 * \param[in]	dfsPtr			ointer to dfs object
 * \param[in]	objId			pointer to opened fs object
 * \param[in]	name			attribute name
 * \param[in]	expectedValenLen	expected value length
 *
 * \return	attribute value
 */
JNIEXPORT jstring JNICALL
Java_io_daos_dfs_DaosFsClient_dfsGetExtAttr(JNIEnv *env,
					    jobject client, jlong dfsPtr,
					    jlong objId, jstring name,
					    jint expectedValueLen)
{
	if (name == NULL) {
		throw_const(env, "Empty name", CUSTOM_ERR6);
		return NULL;
	}
	dfs_t *dfs = *(dfs_t **)&dfsPtr;
	dfs_obj_t *file = *(dfs_obj_t **)&objId;
	const char *attr_name = (*env)->GetStringUTFChars(env, name, NULL);
	long value_len = expectedValueLen;
	char *value = (char *)malloc(value_len + 1); /* 1 for \0 */
	jstring ret = NULL;

	if (value == NULL) {
		char *tmp = "Failed to allocate %d bytes for reading "
				"extended attribute value";
		char *msg = NULL;

		asprintf(&msg, tmp, value_len);
		throw_exc(env, msg, CUSTOM_ERR3);
		goto out;
	}
	int rc = dfs_getxattr(dfs, file, attr_name, value, &value_len);

	if (rc) {
		char *tmp = "Failed to get ext attribute name: %s";
		char *msg = NULL;

		asprintf(&msg, tmp, attr_name);
		throw_exc(env, msg, rc);
		goto out;
	}
	value[value_len] = '\0';
	ret = (*env)->NewStringUTF(env, value);

out:
	(*env)->ReleaseStringUTFChars(env, name, attr_name);
	if (value) free(value);
	return ret;
}

/**
 * JNI method to remove extended attribute denoted by \a name
 * from file denoted by \a objId.
 *
 * \param[in]	env		JNI environment
 * \param[in]	client		DaosFsClient object
 * \param[in]	dfsPtr		pointer to dfs object
 * \param[in]	objId		pointer to opened fs object
 * \param[in]	name		attribute name
 */
JNIEXPORT void JNICALL
Java_io_daos_dfs_DaosFsClient_dfsRemoveExtAttr(JNIEnv *env,
					       jobject client, jlong dfsPtr,
					       jlong objId, jstring name)
{
	if (name == NULL) {
		throw_const(env, "Empty name", CUSTOM_ERR6);
		return;
	}
	dfs_t *dfs = *(dfs_t **)&dfsPtr;
	dfs_obj_t *file = *(dfs_obj_t **)&objId;
	const char *attr_name = (*env)->GetStringUTFChars(env, name, NULL);
	int rc = dfs_removexattr(dfs, file, attr_name);

	if (rc) {
		char *tmp = "Failed to remove ext attribute name: %s";
		char *msg = NULL;

		asprintf(&msg, tmp, attr_name);
		throw_exc(env, msg, rc);
	}
	(*env)->ReleaseStringUTFChars(env, name, attr_name);
}

/**
 * JNI method to get chunk size of file denoted by \a objId.
 *
 * \param[in]	env		JNI environment
 * \param[in]	clientClass	DaosFsClient class
 * \param[in]	objId		pointer to opened fs object
 *
 * \return	chunk size
 */
JNIEXPORT jlong JNICALL
Java_io_daos_dfs_DaosFsClient_dfsGetChunkSize(JNIEnv *env,
					      jclass clientClass, jlong objId)
{
	dfs_obj_t *file = *(dfs_obj_t **)&objId;
	daos_size_t size;
	int rc = dfs_get_chunk_size(file, &size);

	if (rc) {
		char *msg = "Failed to get chunk size of object. "
			"It's a directory, not a file? ";

		throw_const(env, msg, rc);
	}
	return size;
}

/**
 * JNI method to get mode of file denoted by \a objId.
 *
 * \param[in]	env		JNI environment
 * \param[in]	clientClass	DaosFsClient class
 * \param[in]	objId		pointer to opened fs object
 *
 * \return	file mode
 */
JNIEXPORT jint JNICALL
Java_io_daos_dfs_DaosFsClient_dfsGetMode(JNIEnv *env,
					 jclass clientClass, jlong objId)
{
	dfs_obj_t *file = *(dfs_obj_t **)&objId;
	mode_t mode;
	int rc = dfs_get_mode(file, &mode);

	if (rc) {
		throw_const(env,
			    "Failed to get mode object",
			    rc);
	}
	return mode;
}

/**
 * JNI method to determine if file is directory by checking file \a mode.
 *
 * \param[in]	env		JNI environment
 * \param[in]	clientClass	DaosFsClient class
 * \param[in]	mode		file mode
 *
 * \return	0 for non-directory, 1 for directory
 */
JNIEXPORT jboolean JNICALL
Java_io_daos_dfs_DaosFsClient_dfsIsDirectory(JNIEnv *env,
					     jclass clientClass, jint mode)
{
	return S_ISDIR(mode) ? 1 : 0;
}

/**
 * extract and parse extended attributes from given \a pathStr.
 *
 * \param[in]	env				JNI environment
 * \param[in]	clientClass		DaosFsClient class
 * \param[in]   pathStr		 file path to resolve
 *
 * \return	duns_attribute_t serialized in binary by protobuf-c
 */
JNIEXPORT jbyteArray JNICALL
Java_io_daos_dfs_DaosFsClient_dunsResolvePath(JNIEnv *env, jclass clientClass,
		jstring pathStr)
{
	if (pathStr == NULL) {
		throw_const(env, "Empty path", CUSTOM_ERR6);
		return NULL;
	}
	const char *path = (*env)->GetStringUTFChars(env, pathStr, NULL);
	struct duns_attr_t attr = {0};
	Uns__DunsAttribute attribute = UNS__DUNS_ATTRIBUTE__INIT;
	char object_type[40] = "";
	int len;
	void *buf = NULL;
	jbyteArray barray = NULL;
	jbyte *bytes = NULL;
	const char *prefix = "daos://";
	bool has_prefix = strncmp(prefix, path, strlen(prefix)) == 0;
	int rc;

	attr.da_no_prefix = !has_prefix;
	rc = duns_resolve_path(path, &attr);
	if (rc) {
		char *tmp = "Failed to resolve UNS path, %s";
		char *msg = NULL;

		asprintf(&msg, tmp, path);
		throw_base(env, msg, rc, 1, 0);
		goto out;
	}

	attribute.poolid = attr.da_pool;
	attribute.contid = attr.da_cont;

	if (attr.da_type == DAOS_PROP_CO_LAYOUT_POSIX) {
		attribute.layout_type = UNS__LAYOUT__POSIX;
	} else if (attr.da_type == DAOS_PROP_CO_LAYOUT_HDF5) {
		attribute.layout_type = UNS__LAYOUT__HDF5;
	}
	daos_oclass_id2name(attr.da_oclass_id, object_type);
	attribute.object_type = object_type;
	attribute.chunk_size = attr.da_chunk_size;
	attribute.on_lustre = attr.da_on_lustre;
	attribute.rel_path = attr.da_rel_path;
	/* copy back in binary */
	len = uns__duns_attribute__get_packed_size(&attribute);
	buf = malloc(len);
	if (buf == NULL) {
		throw_const(env, "memory allocation failed", 1);
		goto out;
	}
	uns__duns_attribute__pack(&attribute, buf);
	barray = (*env)->NewByteArray(env, len);
	bytes = (*env)->GetByteArrayElements(env, barray, 0);
	memcpy(bytes, buf, len);

out:
	(*env)->ReleaseStringUTFChars(env, pathStr, path);
	if (bytes != NULL) {
		(*env)->ReleaseByteArrayElements(env, barray, bytes, 0);
	}
	if (buf != NULL) {
		free(buf);
	}
	duns_destroy_attr(&attr);
	return barray;
}

/**
 * set app-specific extended attributes on a given \a pathStr.
 * If \a valueStr is NULL or empty, this function will try to
 * remove the \a attrNameStr from the path.
 *
 * \param[in]	env				JNI environment
 * \param[in]	clientClass		DaosFsClient class
 * \param[in]   pathStr		 file path to set on
 * \param[in]   attrNameStr	 attribute name
 * \param[in]   valueStr		attribute value
 */
JNIEXPORT void JNICALL
Java_io_daos_dfs_DaosFsClient_dunsSetAppInfo(JNIEnv *env, jclass clientClass,
		jstring pathStr, jstring attrNameStr, jstring valueStr)
{
	if (pathStr == NULL) {
		throw_const(env, "Empty path", CUSTOM_ERR6);
		return;
	}
	if (attrNameStr == NULL) {
		throw_const(env, "Empty attribute name", CUSTOM_ERR6);
		return;
	}
	const char *path = (*env)->GetStringUTFChars(env, pathStr, NULL);
	const char *attrName = (*env)->GetStringUTFChars(env, attrNameStr,
							NULL);
	const char *value = valueStr == NULL ? NULL :
			(*env)->GetStringUTFChars(env, valueStr, NULL);
	int rc;

	if (!(value == NULL || strlen(value) == 0)) {
		rc = lsetxattr(path, attrName, value, strlen(value) + 1, 0);
		if (rc) {
			char *tmp = "failed to set app attribute"
				" (%s) = (%s) on path (%s)";
			char *msg = NULL;

			asprintf(&msg, tmp, attrName, value, path);
			rc = errno;
			throw_exc(env, msg, rc);
		}
	} else { /* remove attribute */
		rc = lremovexattr(path, attrName);
		if (rc) {
			char *tmp =
			"failed to remove app attribute (%s) from path (%s)";
			char *msg = NULL;

			asprintf(&msg, tmp, attrName, path);
			rc = errno;
			throw_exc(env, msg, rc);
		}
	}
out:
	(*env)->ReleaseStringUTFChars(env, pathStr, path);
	(*env)->ReleaseStringUTFChars(env, attrNameStr, attrName);
	if (value != NULL) {
		(*env)->ReleaseStringUTFChars(env, valueStr, value);
	}
}

/**
 * get app-specific extended attributes from a given \a pathStr.
 *
 * \param[in]	env				JNI environment
 * \param[in]	clientClass		DaosFsClient class
 * \param[in]   pathStr		 file path to set on
 * \param[in]   attrNameStr	 attribute name
 *
 * \return	JVM string of attribute value.
 */
JNIEXPORT jstring JNICALL
Java_io_daos_dfs_DaosFsClient_dunsGetAppInfo(JNIEnv *env, jclass clientClass,
		jstring pathStr, jstring attrNameStr, jint maxLen)
{
	if (pathStr == NULL || attrNameStr == NULL) {
		char *msg = "Empty path or empty attribute name";

		throw_const(env, msg, CUSTOM_ERR6);
		return NULL;
	}
	const char *path = (*env)->GetStringUTFChars(env, pathStr, NULL);
	const char *attrName = (*env)->GetStringUTFChars(env, attrNameStr,
		NULL);
	void *value = malloc(maxLen);

	if (value == NULL) {
		throw_const(env,
			    "memory allocation failed", CUSTOM_ERR7);
		return NULL;
	}
	int len = lgetxattr(path, attrName, value, maxLen);
	jstring ret = NULL;

	if (len < 0 || len > maxLen) {
		char *tmp =
		"failed to get app attribute (%s) from path (%s)";
		char *msg = NULL;

		asprintf(&msg, tmp, attrName, path);
		throw_exc(env, msg, errno);
		goto out;
	}

out:
	(*env)->ReleaseStringUTFChars(env, attrNameStr, attrName);
	(*env)->ReleaseStringUTFChars(env, pathStr, path);
	ret = (*env)->NewStringUTF(env, value);
	free(value);

	return ret;
}

/**
 * Parse input string to UNS attribute.
 *
 * \param[in]	env				JNI environment
 * \param[in]	clientClass		DaosFsClient class
 * \param[in]   inputStr		attribute string
 *
 * \return	duns_attribute_t serialized in binary by protobuf-c
 */
JNIEXPORT jbyteArray JNICALL
Java_io_daos_dfs_DaosFsClient_dunsParseAttribute(JNIEnv *env,
		jclass clientClass, jstring inputStr)
{
	if (inputStr == NULL) {
		throw_const(env, "Empty input", CUSTOM_ERR6);
		return NULL;
	}
	const char *input = (*env)->GetStringUTFChars(env, inputStr, NULL);
	int len = strlen(input);
	struct duns_attr_t attr = {0};
	Uns__DunsAttribute attribute = UNS__DUNS_ATTRIBUTE__INIT;
	char object_type[40] = "";
	void *buf = NULL;
	jbyteArray barray = NULL;
	jbyte *bytes = NULL;

	int rc = duns_parse_attr((char *)input, len, &attr);

	if (rc) {
		char *tmp = "Failed to parse UNS string, %s";
		char *msg = NULL;

		asprintf(&msg, tmp, input);
		throw_base(env, msg, rc, 1, 0);
		goto out;
	}

	attribute.poolid = attr.da_pool;
	attribute.contid = attr.da_cont;

	if (attr.da_type == DAOS_PROP_CO_LAYOUT_POSIX) {
		attribute.layout_type = UNS__LAYOUT__POSIX;
	} else if (attr.da_type == DAOS_PROP_CO_LAYOUT_HDF5) {
		attribute.layout_type = UNS__LAYOUT__HDF5;
	}
	daos_oclass_id2name(attr.da_oclass_id, object_type);
	attribute.object_type = object_type;
	attribute.chunk_size = attr.da_chunk_size;
	attribute.on_lustre = attr.da_on_lustre;
	/* copy back in binary */
	len = uns__duns_attribute__get_packed_size(&attribute);
	buf = malloc(len);
	if (buf == NULL) {
		throw_const(env,
			    "memory allocation failed",
			    1);
		goto out;
	}
	uns__duns_attribute__pack(&attribute, buf);
	barray = (*env)->NewByteArray(env, len);
	bytes = (*env)->GetByteArrayElements(env, barray, 0);
	memcpy(bytes, buf, len);

out:
	(*env)->ReleaseStringUTFChars(env, inputStr, input);
	if (bytes) {
		(*env)->ReleaseByteArrayElements(env, barray, bytes, 0);
	}
	if (buf) {
		free(buf);
	}

	return barray;
}
