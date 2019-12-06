#include "com_intel_daos_client_DaosFsClient.h"
#include <stdio.h>
#include <daos.h>
#include <daos_fs.h>
#include <daos_jni_common.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <gurt/common.h>

static jint JNI_VERSION = JNI_VERSION_1_8;

static jclass daos_io_exception_class;

static jmethodID new_exception_msg;
static jmethodID new_exception_cause;
static jmethodID new_exception_msg_code;
static jmethodID new_exception_msg_code_cause;

jint JNI_OnLoad(JavaVM* vm, void *reserved) {
    JNIEnv *env;
    if ((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION) != JNI_OK) {
        return JNI_ERR;
    }
    daos_io_exception_class = (*env)->FindClass(env, "com/intel/daos/client/DaosIOException");
    new_exception_msg = (*env)->GetMethodID(env, daos_io_exception_class, "<init>", "(Ljava/lang/String;)V");
    new_exception_cause = (*env)->GetMethodID(env, daos_io_exception_class, "<init>", "(Ljava/lang/Throwable;)V");
    new_exception_msg_code = (*env)->GetMethodID(env, daos_io_exception_class, "<init>", "(Ljava/lang/String;I)V");
    new_exception_msg_code_cause = (*env)->GetMethodID(env, daos_io_exception_class, "<init>", "(Ljava/lang/String;ILjava/lang/Throwable;)V");

    int rc = daos_init();
    if (rc) {
    	printf("daos_init() failed with rc = %d\n", rc);
    	return rc;
    }
    return JNI_VERSION;
}

/**
 * TODO: handle failed exception throwing
 */
static int throw_exception(JNIEnv *env, char *msg, int errorCode){
	jstring jmsg = (*env)->NewStringUTF(env, msg);
	jobject obj = (*env)->NewObject(env, daos_io_exception_class, new_exception_msg_code, jmsg, errorCode);
	return (*env)->Throw(env, obj);
}

JNIEXPORT jstring JNICALL Java_com_intel_daos_client_DaosFsClient_daosCreatePool
  (JNIEnv *env, jclass clientClass, jstring serverGroup, jint svcReplics, jint mode, jlong scmSize, jlong nvmeSize){
	printf("creating pool ... \n");
	d_rank_t svc[svcReplics];
	d_rank_list_t svcl = {};
	memset(svc, 0, sizeof(svc));
	svcl.rl_ranks = svc;
	svcl.rl_nr = svcReplics;
	if (!(scmSize > 0 || nvmeSize > 0)) {
		char *tmp = "Either scm size (%ld) or nvme size (%ld) should be greater than 0";
		char msg[sizeof(tmp) + 16] = {'\0'};
		sprintf(msg, tmp, scmSize, nvmeSize);
		throw_exception(env, msg, -1001);
		return NULL;
	}
	uuid_t pool_uuid;
	const char *server_group = (*env)->GetStringUTFChars(env, serverGroup, 0);
	int rc = daos_pool_create(mode /* mode */,
					geteuid() /* user owner */,
					getegid() /* group owner */,
					server_group /* daos server process set ID */,
					NULL /* list of targets, NULL = all */,
					"pmem" /* storage type to use, use default */,
					scmSize ,
					nvmeSize ,
					NULL ,
					&svcl /* pool service nodes, used for connect */,
					pool_uuid, /* the uuid of the pool created */
					NULL /* event, use blocking call for now */);
	jstring ret;
	if (rc) {
		char *tmp = "Failed to create pool with server group (%s), service replics (%d), mode (%d), scm size (%ld), nvme size (%ld)";
		char msg[sizeof(tmp) + strlen(server_group) + 24] = {'\0'};
		sprintf(msg, tmp, serverGroup, svcReplics, mode, scmSize, nvmeSize);
		throw_exception(env, msg, rc);
		ret = NULL;
	}else{
		char tmp[50 + svcl.rl_nr*5] = {'\0'};
		uuid_unparse(pool_uuid, tmp);
		strcat(tmp, " ");
		int i;
		/* Print the pool service replica ranks. */
		for (i = 0; i < svcl.rl_nr - 1; i++){
			if(i) strcat(tmp, ":");
			strcat(tmp, svcl.rl_ranks[i]);
		}
		printf("pool created successfully, %s", tmp);
		ret = (*env)->NewStringUTF(env, tmp);
	}
	(*env)->ReleaseStringUTFChars(env, serverGroup, server_group);
	return ret;
}

JNIEXPORT jlong JNICALL Java_com_intel_daos_client_DaosFsClient_daosOpenPool
  (JNIEnv *env, jclass clientClass, jstring poolId, jstring serverGroup, jstring svcReplics, jint flags){
	const char *pool_str = (*env)->GetStringUTFChars(env, poolId, 0);
	const char *server_group = (*env)->GetStringUTFChars(env, serverGroup, 0);
	const char *svc_replics = (*env)->GetStringUTFChars(env, svcReplics, 0);
	uuid_t pool_uuid;
	uuid_parse(pool_str, pool_uuid);
	d_rank_list_t *svcl=daos_rank_list_parse(svc_replics, ":");
	jlong ret;
	if (svcl == NULL) {
		char *tmp = "Invalid pool service rank list (%s) when open pool (%s)";
		char msg[sizeof(tmp) + strlen(svc_replics) + strlen(pool_str)] = {'\0'};
		sprintf(msg, tmp, svcReplics, pool_str);
		throw_exception(env, msg, -1101);
		ret = -1;
	}else{
		daos_handle_t poh;
		int rc;
		rc = daos_pool_connect(pool_uuid, server_group, svcl,
						flags,
						&poh /* returned pool handle */,
						NULL /* returned pool info */,
						NULL /* event */);

		if (rc) {
			char *tmp = "Failed to connect to pool (%s)";
			char msg[sizeof(tmp) + strlen(pool_str)] = {'\0'};
			sprintf(msg, tmp, svcReplics, pool_str);
			throw_exception(env, msg, rc);
			ret = -1;
		}else {
			memcpy(&ret, &poh, sizeof(poh));
		}
	}
	(*env)->ReleaseStringUTFChars(env, poolId, pool_str);
	(*env)->ReleaseStringUTFChars(env, serverGroup, server_group);
	(*env)->ReleaseStringUTFChars(env, svcReplics, svc_replics);
	return ret;
}

JNIEXPORT void JNICALL Java_com_intel_daos_client_DaosFsClient_daosClosePool
  (JNIEnv *env, jclass clientClass, jlong poolPtr){
	daos_handle_t poh;
	memcpy(&poh, &poolPtr, sizeof(poh));
	int rc = daos_pool_disconnect(poh, NULL);
	if(rc){
		printf("Failed to close pool rc: %d\n", rc);
	}
}

JNIEXPORT jlong JNICALL Java_com_intel_daos_client_DaosFsClient_daosOpenCont
  (JNIEnv *env, jclass clientClass, jlong poolPtr, jstring contUuid, jint mode){
	daos_handle_t poh = (daos_handle_t)poolPtr;
	daos_cont_info_t	co_info;
	const char *cont_str = (*env)->GetStringUTFChars(env, contUuid, NULL);
	uuid_t cont_uuid;
	uuid_parse(cont_str, cont_uuid);
	daos_handle_t coh;
	jlong ret = -1;
	int rc = daos_cont_open(poh, cont_uuid, mode, &coh, &co_info, NULL);
	if (rc) {
		char *tmp = "Failed to open container (id: %s)";
		char msg[sizeof(tmp) + strlen(cont_str)] = {'\0'};
		sprintf(msg, tmp, cont_str);
		throw_exception(env, msg, rc);
		ret = -1;
	} else {
		memcpy(&ret, &coh, sizeof(coh));
	}
	(*env)->ReleaseStringUTFChars(env, contUuid, cont_str);
	return ret;
}

JNIEXPORT void JNICALL Java_com_intel_daos_client_DaosFsClient_daosCloseContainer
  (JNIEnv *env, jclass clientClass, jlong contPtr){
	daos_handle_t coh;
	memcpy(&coh, &contPtr, sizeof(coh));
	int rc = daos_cont_close(coh, NULL);
	if(rc){
		printf("Failed to close container rc: %d\n", rc);
	}
}

JNIEXPORT jlong JNICALL Java_com_intel_daos_client_DaosFsClient_dfsMountFs
  (JNIEnv *env, jclass clientClass, jlong poolPtr, jlong contPtr, jboolean readOnly){
	int flags = readOnly ? O_RDONLY : O_RDWR;
	dfs_t *dfsPtr;
	daos_handle_t poh;
	daos_handle_t coh;
	memcpy(&poh, &poolPtr, sizeof(poh));
	memcpy(&coh, &contPtr, sizeof(coh));
	int rc = dfs_mount(poh, coh, flags, &dfsPtr);
	if(rc){
		char *msg = "Failed to mount fs";
		throw_exception(env, msg, rc);
		return -1;
	}
	return *(jlong*)&dfsPtr;
}

JNIEXPORT void JNICALL Java_com_intel_daos_client_DaosFsClient_dfsUnmountFs
  (JNIEnv *env, jclass clientClass, jlong dfsPtr){
	dfs_t *dfs = *(dfs_t**)&dfsPtr;
	int rc = dfs_umount(dfs);
	if(rc){
		printf("Failed to unmount fs rc: %d\n", rc);
	}
}

JNIEXPORT jlong JNICALL Java_com_intel_daos_client_DaosFsClient_dfsMountFsOnRoot
  (JNIEnv *env, jclass clientClass, jlong poolPtr){
	dfs_t *dfsPtr;
	daos_handle_t poh;
	memcpy(&poh, &poolPtr, sizeof(poh));
	int rc = dfs_mount_root_cont(poh, &dfsPtr);
	if(rc){
		char *msg = "Failed to mount fs on root container";
		throw_exception(env, msg, rc);
		return -1;
	}
	return *(jlong*)&dfsPtr;
}

JNIEXPORT void JNICALL Java_com_intel_daos_client_DaosFsClient_dfsUnmountFsOnRoot
  (JNIEnv *env, jclass clientClass, jlong dfsPtr){
	dfs_t *dfs = *(dfs_t**)&dfsPtr;
	int rc = dfs_umount_root_cont(dfs);
	if(rc){
		printf("Failed to unmount fs on root container rc: %d\n", rc);
	}
}

JNIEXPORT void JNICALL Java_com_intel_daos_client_DaosFsClient_daosFinalize
  (JNIEnv *env, jclass clientClass){
	int rc = daos_fini();
	if(rc){
		printf("Failed to finalize daos rc: %d\n", rc);
	}
}

JNIEXPORT void JNICALL Java_com_intel_daos_client_DaosFsClient_move
  (JNIEnv *env, jobject obj, jlong dfsPtr, jstring srcPath, jstring destPath){
	const char* src_path = (*env)->GetStringUTFChars(env, srcPath, NULL);
	const char* dest_path = (*env)->GetStringUTFChars(env, destPath, NULL);
	char *src_dir_path = NULL, *src_base_path = NULL, *dest_dir_path = NULL, *dest_base_path = NULL;
	src_dir_path = strdup(src_path);
	src_base_path = strdup(src_path);
	dest_dir_path = strdup(dest_path);
	dest_base_path = strdup(dest_path);
	char * src_dir = dirname(src_dir_path);
	char * src_base = basename(src_base_path);
	char * dest_dir = dirname(dest_dir_path);
	char * dest_base = basename(dest_base_path);
	dfs_obj_t *src_dir_handle = NULL, *dest_dir_handle = NULL;
	mode_t tmp_mode;
	dfs_t *dfs = *(dfs_t**)&dfsPtr;
	int rc = dfs_lookup(dfs, src_dir, O_RDWR, &src_dir_handle, &tmp_mode, NULL);
	if (rc) {
		char *tmp = "Cannot open source directory (%s)";
		char msg[sizeof(tmp) + strlen(src_dir)] = {'\0'};
		sprintf(msg, tmp, src_dir);
		throw_exception(env, msg, rc);
		goto out;
	}
	if (strcmp(src_dir, dest_dir) == 0) {
		dest_dir_handle = src_dir_handle;
	} else {
		rc = dfs_lookup(dfs, dest_dir, O_RDWR, &dest_dir_handle, &tmp_mode, NULL);
		if (rc) {
			char *tmp = "Cannot open destination directory (%s)";
			char msg[sizeof(tmp) + strlen(dest_dir)] = {'\0'};
			sprintf(msg, tmp, src_dir);
			throw_exception(env, msg, rc);
			goto out;
		}
	}
	rc = dfs_move(dfs, src_dir_handle, src_base, dest_dir_handle, dest_base, NULL);
	if(rc){
		char *tmp = "Failed to move source path (%s) to destination path (%s)";
		char msg[sizeof(tmp) + strlen(src_path) + strlen(dest_path)] = {'\0'};
		sprintf(msg, tmp, src_path, dest_path);
		throw_exception(env, msg, rc);
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

static int mkdirs(dfs_t *dfs, char *path, int mode, unsigned char recursive, dfs_obj_t **handle){
	char *dirs, *bases, *dir, *base;
	dfs_obj_t *parent_handle = NULL;
	mode_t parent_mode;
	int rc = dfs_lookup(dfs, path, O_RDWR, handle, &parent_mode, NULL);
	if (rc == -DER_NONEXIST || rc == -ENOENT || rc == ENOENT) {
		if(recursive){
			// recursively create it
			dirs = strdup(path);
			dir = dirname(dirs);
			bases = strdup(path);
			base = basename(bases);
			rc = mkdirs(dfs, dir, mode, recursive, &parent_handle);
			if (rc)	goto out;
			rc = dfs_mkdir(dfs, parent_handle, base, mode);
			if (rc) goto out;
			rc = dfs_lookup(dfs, path, O_RDWR, handle, &parent_mode, NULL);
		}else{
			goto out;
		}
	}
out:
	if (dirs) free(dirs);
	if (bases) free(bases);
	if (parent_handle) dfs_release(parent_handle);
	return rc;
}

JNIEXPORT void JNICALL Java_com_intel_daos_client_DaosFsClient_mkdir
  (JNIEnv *env, jobject client, jlong dfsPtr, jstring path, jint mode, jboolean recursive){
	const char* path_str = (*env)->GetStringUTFChars(env, path, NULL);
	dfs_t *dfs = *(dfs_t**)&dfsPtr;
	char *dirs, *bases, *parent_dir, *base;
	dfs_obj_t *parent_handle = NULL;
	mode_t parent_mode;

	dirs = strdup(path_str);
	parent_dir = dirname(dirs);
	bases = strdup(path_str);
	base = basename(bases);
	int rc = mkdirs(dfs, parent_dir, mode, recursive, &parent_handle);
	if (rc) {
		char *tmp;
		if(recursive){
			tmp = "Failed to create parent or ancestor directories (%s)";
		} else {
			tmp = "Parent directory doesn't exist (%s)";
		}
		char msg[sizeof(tmp) + strlen(parent_dir)] = {'\0'};
		sprintf(msg, tmp, parent_dir);
		throw_exception(env, msg, rc);
	} else {
		rc = dfs_mkdir(dfs, parent_handle, base, mode);
		if (rc) {
			char *tmp = "Failed to create directory (%s) under parent directory (%s)";
			char msg[sizeof(tmp) + strlen(base) + strlen(parent_dir)] = {'\0'};
			sprintf(msg, tmp, parent_dir);
			throw_exception(env, msg, rc);
		}
	}
	if (dirs) free(dirs);
	if (bases) free(bases);
	if (parent_handle) dfs_release(parent_handle);
	(*env)->ReleaseStringUTFChars(env, path, path_str);
}

JNIEXPORT jlong JNICALL Java_com_intel_daos_client_DaosFsClient_createNewFile
  (JNIEnv *env, jobject client, jlong dfsPtr, jstring parentPath, jstring name,
		  jint mode, jint accessFlags, jint objectId, jint chunkSize){
	dfs_t *dfs = *(dfs_t**)&dfsPtr;
	const char* parent_path = (*env)->GetStringUTFChars(env, parentPath, NULL);
	const char* file_name = (*env)->GetStringUTFChars(env, name, NULL);
	dfs_obj_t *file = NULL, *parent = NULL;
	mode_t tmp_mode;
	int rc = dfs_lookup(dfs, parent_path, O_RDWR, &parent, &tmp_mode, NULL);
	if (rc) {
		char *tmp = "Failed to find parent directory (%s)";
		char msg[sizeof(tmp) + strlen(parent_path)] = {'\0'};
		sprintf(msg, tmp, parent_path);
		throw_exception(env, msg, rc);
	} else {
		rc = dfs_open(dfs, parent, file_name, S_IFREG | mode, O_CREAT | mode, objectId, chunkSize, NULL, &file);
		if (rc) {
			char *tmp = "Failed to create new file (%s) under directory (%s)";
			char msg[sizeof(tmp) + strlen(file_name) + strlen(parent_path)] = {'\0'};
			sprintf(msg, tmp, file_name, parent_path);
			throw_exception(env, msg, rc);
		}
	}

	(*env)->ReleaseStringUTFChars(env, parentPath, parent_path);
	(*env)->ReleaseStringUTFChars(env, name, file_name);
	if (parent) dfs_release(parent);
	return *(jlong*)&file;;
}

JNIEXPORT jboolean JNICALL Java_com_intel_daos_client_DaosFsClient_delete
  (JNIEnv *env, jobject object, jlong dfsPtr, jstring parentPath, jstring name){
	dfs_t *dfs = *(dfs_t**)&dfsPtr;
	const char *parent_path = (*env)->GetStringUTFChars(env, parentPath, NULL);
	const char *file_name = (*env)->GetStringUTFChars(env, name, NULL);
	dfs_obj_t *parent = NULL;
	mode_t tmp_mode;
	int rc, ret;
	if (strcmp(parent_path, "/") != 0){
	    rc = dfs_lookup(dfs, parent_path, O_RDWR, &parent, &tmp_mode, NULL);
	    if(rc){
	    	ret = 0;
	    	goto out;
	    }
	}
	rc = dfs_remove(dfs, parent, file_name, false, NULL);
	if (rc) {
		ret = 0;
		goto out;
	}
	ret = 1;
out:
	(*env)->ReleaseStringUTFChars(env, parentPath, parent_path);
	(*env)->ReleaseStringUTFChars(env, name, file_name);
	if (parent) dfs_release(parent);
	return ret;
}

JNIEXPORT jlong JNICALL Java_com_intel_daos_client_DaosFsClient_dfsLookup__JJLjava_lang_String_2IJ
  (JNIEnv *env, jobject client, jlong dfsPtr, jlong, jstring, jint, jlong){

}

void JNI_OnUnload(JavaVM* vm, void *reserved) {
	daos_io_exception_class = NULL;
	new_exception_msg = NULL;
	new_exception_cause = NULL;
	new_exception_msg_code = NULL;
	new_exception_msg_code_cause = NULL;
    daos_fini();
}
