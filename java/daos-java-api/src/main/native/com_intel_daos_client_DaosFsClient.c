#include "com_intel_daos_client_DaosFsClient.h"
#include <stdio.h>
#include <daos.h>
#include <fcntl.h>
#include <sys/stat.h>

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

/**JNIEXPORT jstring JNICALL Java_com_intel_daos_client_DaosFsClient_daosCreatePool
  (JNIEnv *env, jclass clientClass, jstring serverGroup, jint svcReplics, jint mode, jlong scmSize, jlong nvmeSize){
	printf("creating pool ... \n");
	d_rank_t svc[svcReplics];
	d_rank_list_t svcl = {};
	memset(svc, 0, sizeof(svc));
	svcl.rl_ranks = svc;
	svcl.rl_nr = svcReplics;
	if (!(scmSize > 0 || nvmeSize > 0)) {

		jstring msg = (*env)->NewStringUTF("Either scm size or nvme size should be greater than 0");

	}
	uuid_t pool_uuid;
	int	i;
		printf(" daosPoolCreate 1 \n");
	int rc = daos_pool_create(mode /* mode *///,
					//geteuid() /* user owner */,
					//getegid() /* group owner */,
					//server_group /* daos server process set ID */,
					//NULL /* list of targets, NULL = all */,
					//"pmem" /* storage type to use, use default */,
					//scm ,
					//nvme ,
					//NULL ,
					//&svcl /* pool service nodes, used for connect */,
					//pool_uuid, /* the uuid of the pool created */
					//NULL /* event, use blocking call for now */);
	/**printf(" daosPoolCreate 2 ,rc =%d \n",rc);
	if (rc) {
		D_ERROR("daos native error in create_pool():failed to create pool with rc = %d", rc);
		D_GOTO(out_daos, rc);
	}

	char tmp[50];
	uuid_unparse(pool_uuid, tmp);
	strcat(tmp," ");
	/* Print the pool service replica ranks. */
	/**for (i = 0; i < svcl.rl_nr - 1; i++){
		if(i) strcpy(tmp,",");
		strcat(tmp,svcl.rl_ranks[i]);
	}
	char *buf[sizeof(svcl.rl_ranks[svcl.rl_nr - 1])];
	sprintf(buf,"%d",svcl.rl_ranks[svcl.rl_nr - 1]);
	strcat(tmp,buf);
	return (*env)->NewStringUTF(env, tmp);
out_daos:
	daos_fini();
	return rc;
}**/

void JNI_OnUnload(JavaVM* vm, void *reserved) {
	daos_io_exception_class = NULL;
    daos_fini();
}
