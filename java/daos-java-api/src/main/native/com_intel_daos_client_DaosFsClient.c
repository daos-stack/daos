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
			ret = poh;
		}
	}
	(*env)->ReleaseStringUTFChars(env, poolId, pool_str);
	(*env)->ReleaseStringUTFChars(env, serverGroup, server_group);
	(*env)->ReleaseStringUTFChars(env, svcReplics, svc_replics);
	return ret;
}

JNIEXPORT jstring JNICALL Java_com_intel_daos_client_DaosFsClient_daosCreateContainer
  (JNIEnv *env, jclass clientClass, jlong poolPtr, jstring contUuid){
	daos_handle_t poh = (daos_handle_t)poolPtr;
	const char *cont_str = (*env)->GetStringUTFChars(env, contUuid, NULL);
	uuid_t cont_uuid;
	uuid_parse(cont_str, cont_uuid);
	daos_prop_t	*prop = NULL;
	prop = daos_prop_alloc(1);
	jstring ret;
	if (prop == NULL) {
		char *tmp = "Failed to allocate container (id: %s) property.";
		char msg[sizeof(tmp) + strlen(cont_str)] = {'\0'};
		sprintf(msg, tmp, cont_str);
		throw_exception(env, msg, -1201);
		ret = NULL;
	}else{
		prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_LAYOUT_TYPE;
		prop->dpp_entries[0].dpe_val = DAOS_PROP_CO_LAYOUT_POSIX;
		int rc = daos_cont_create(poh, cont_uuid, prop, NULL /* event */);
		daos_prop_free(prop);
		if (rc) {
			char *tmp = "Failed to create container (id: %s)";
			char msg[sizeof(tmp) + strlen(cont_str)] = {'\0'};
			sprintf(msg, tmp, cont_str);
			throw_exception(env, msg, rc);
			ret = NULL;
		}else{
			ret = contUuid;
		}
	}
	(*env)->ReleaseStringUTFChars(env, contUuid, cont_str);
	return ret;
}

JNIEXPORT jlong JNICALL Java_com_intel_daos_client_DaosFsClient_daosOpenCont
  (JNIEnv *env, jclass clientClass, jlong poolPtr, jstring contUuid, jint mode){
	daos_handle_t poh = (daos_handle_t)poolPtr;
	daos_handle_t super_oh;
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
		dfs_attr_t		dattr;
		dattr.da_chunk_size = DFS_DEFAULT_CHUNK_SIZE;
		dattr.da_oclass_id = DFS_DEFAULT_OBJ_CLASS;
		/** Create SB */

		rc = open_sb(coh, true, &dattr, &super_oh);

		if (rc)
			D_GOTO(err_close, rc);
		/** Add root object */
		struct dfs_entry entry = {0};
		entry.oid.lo = RESERVED_LO;
		entry.oid.hi = ROOT_HI;
		daos_obj_generate_id(&entry.oid, 0, dattr.da_oclass_id, 0);
		entry.mode = S_IFDIR | 0777;
		entry.atime = entry.mtime = entry.ctime = time(NULL);
		entry.chunk_size = dattr.da_chunk_size;

		rc = insert_entry(super_oh, DAOS_TX_NONE, "/", entry);

		if (rc) {
			D_ERROR("Failed to insert root entry (%d).", rc);
			D_GOTO(err_super, rc);
		}

		daos_obj_close(super_oh, NULL);
		jlong java_coh;
		memcpy(&java_coh, &coh, sizeof(daos_handle_t));
		return java_coh;
	err_super:
		daos_obj_close(super_oh, NULL);
		return rc;
	err_close:
		daos_cont_close(coh, NULL);
		return rc;
	err_destroy:
		daos_cont_destroy(poh, cont_uuid, 1, NULL);
		return rc;
	}
	(*env)->ReleaseStringUTFChars(env, contUuid, cont_str);
	return ret;
}

void JNI_OnUnload(JavaVM* vm, void *reserved) {
	daos_io_exception_class = NULL;
    daos_fini();
}
