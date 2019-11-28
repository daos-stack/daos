#include "daos_jni_common.h"

static const char* server_group = "daos_server";
static jint JNI_VERSION = JNI_VERSION_1_8;
static const int max_svc_nreplicas = 13;
static const unsigned int	default_svc_nreplicas = 1;

jclass JC_String;
jclass JC_Exception;



/** A-key name of DFS Layout Version */
#define LAYOUT_NAME	"DFS_LAYOUT_VERSION"
/** D-key name of SB metadata */
#define SB_DKEY		"DFS_SB_METADATA"
#define SB_AKEYS	5
/** A-key name of SB magic */
#define MAGIC_NAME	"DFS_MAGIC"
/** A-key name of SB version */
#define SB_VERSION_NAME	"DFS_SB_VERSION"
/** A-key name of DFS Layout Version */
#define LAYOUT_NAME	"DFS_LAYOUT_VERSION"
/** A-key name of Default chunk size */
#define CS_NAME		"DFS_CHUNK_SIZE"
/** A-key name of Default Object Class */
#define OC_NAME		"DFS_OBJ_CLASS"
/** Magic Value */
#define DFS_SB_MAGIC		0xda05df50da05df50
/** DFS Layout version value */
#define DFS_SB_VERSION		1
/** DFS SB Version Value */
#define DFS_LAYOUT_VERSION	1
/** Array object stripe size for regular files */
#define DFS_DEFAULT_CHUNK_SIZE	1048576
#define DFS_DEFAULT_OBJ_CLASS	OC_SX

/** Number of A-keys for attributes in any object entry */
#define INODE_AKEYS	7
/** A-key name of mode_t value */
#define MODE_NAME	"mode"
/** A-key name of object ID value */
#define OID_NAME	"oid"
/** A-key name of chunk size; will be stored only if not default */
#define CSIZE_NAME	"chunk_size"
/** A-key name of last access time */
#define ATIME_NAME	"atime"
/** A-key name of last modify time */
#define MTIME_NAME	"mtime"
/** A-key name of last change time */
#define CTIME_NAME	"ctime"
/** A-key name of symlink value */
#define SYML_NAME	"syml"

/** OIDs for Superblock and Root objects */
#define RESERVED_LO	0
#define SB_HI		0
#define ROOT_HI		1

struct dfs_entry {
	/** mode (permissions + entry type) */
	mode_t		mode;
	/** Object ID if not a symbolic link */
	daos_obj_id_t	oid;
	/** chunk size of file */
	daos_size_t	chunk_size;
	/** Sym Link value */
	char		*value;
	/* Time of last access */
	time_t		atime;
	/* Time of last modification */
	time_t		mtime;
	/* Time of last status change */
	time_t		ctime;
};

static int
insert_entry(daos_handle_t oh, daos_handle_t th, const char *name,
	     struct dfs_entry entry)
{
	d_sg_list_t	sgls[INODE_AKEYS];
	d_iov_t		sg_iovs[INODE_AKEYS];
	daos_iod_t	iods[INODE_AKEYS];
	daos_key_t	dkey;
	unsigned int	akeys_nr, i;
	int		rc;

	d_iov_set(&dkey, (void *)name, strlen(name));

	i = 0;

	/** Add the mode */
	d_iov_set(&sg_iovs[i], &entry.mode, sizeof(mode_t));
	d_iov_set(&iods[i].iod_name, MODE_NAME, strlen(MODE_NAME));
	iods[i].iod_size = sizeof(mode_t);
	i++;

	/** Add the oid */
	d_iov_set(&sg_iovs[i], &entry.oid, sizeof(daos_obj_id_t));
	d_iov_set(&iods[i].iod_name, OID_NAME, strlen(OID_NAME));
	iods[i].iod_size = sizeof(daos_obj_id_t);
	i++;

	/** Add the chunk size if set */
	if (entry.chunk_size) {
		d_iov_set(&sg_iovs[i], &entry.chunk_size, sizeof(daos_size_t));
		d_iov_set(&iods[i].iod_name, CSIZE_NAME, strlen(CSIZE_NAME));
		iods[i].iod_size = sizeof(daos_size_t);
		i++;
	}

	/** Add the access time */
	d_iov_set(&sg_iovs[i], &entry.atime, sizeof(time_t));
	d_iov_set(&iods[i].iod_name, ATIME_NAME, strlen(ATIME_NAME));
	iods[i].iod_size = sizeof(time_t);
	i++;

	/** Add the modify time */
	d_iov_set(&sg_iovs[i], &entry.mtime, sizeof(time_t));
	d_iov_set(&iods[i].iod_name, MTIME_NAME, strlen(MTIME_NAME));
	iods[i].iod_size = sizeof(time_t);
	i++;

	/** Add the change time */
	d_iov_set(&sg_iovs[i], &entry.ctime, sizeof(time_t));
	d_iov_set(&iods[i].iod_name, CTIME_NAME, strlen(CTIME_NAME));
	iods[i].iod_size = sizeof(time_t);
	i++;

	/** Add symlink value if Symlink */
	if (S_ISLNK(entry.mode)) {
		d_iov_set(&sg_iovs[i], entry.value, strlen(entry.value) + 1);
		d_iov_set(&iods[i].iod_name, SYML_NAME, strlen(SYML_NAME));
		iods[i].iod_size = strlen(entry.value) + 1;
		i++;
	}

	akeys_nr = i;

	for (i = 0; i < akeys_nr; i++) {
		sgls[i].sg_nr		= 1;
		sgls[i].sg_nr_out	= 0;
		sgls[i].sg_iovs		= &sg_iovs[i];

		dcb_set_null(&iods[i].iod_kcsum);
		iods[i].iod_nr		= 1;
		iods[i].iod_recxs	= NULL;
		iods[i].iod_eprs	= NULL;
		iods[i].iod_csums	= NULL;
		iods[i].iod_type	= DAOS_IOD_SINGLE;
	}

	rc = daos_obj_update(oh, th, &dkey, akeys_nr, iods, sgls, NULL);
	if (rc) {
		D_ERROR("Failed to insert entry %s (%d)\n", name, rc);
		return daos_der2errno(rc);
	}

	return 0;
}

jint JNI_OnLoad(JavaVM* vm, void *reserved) {
    JNIEnv* env;
    if ((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION) != JNI_OK) {
        return JNI_ERR;
    }
    jclass localString, localException;
    localString = (*env)->FindClass(env, "java/lang/String");
    localException = (*env)->FindClass(env, "com/intel/daos/DaosNativeException");
    JC_String = (jclass) (*env)->NewGlobalRef(env, localString);
    JC_Exception = (jclass) (*env)->NewGlobalRef(env, localException);
    (*env)->DeleteLocalRef(env, localString);
    (*env)->DeleteLocalRef(env, localException);
    int rc = daos_init();
    if (rc) {
    	D_ERROR("daos init failed with rc = %d", rc);
    	return rc;
    }
    return JNI_VERSION;
}

static int
open_sb(daos_handle_t coh, bool create, dfs_attr_t *attr, daos_handle_t *oh)
{
	d_sg_list_t		sgls[SB_AKEYS];
	d_iov_t			sg_iovs[SB_AKEYS];
	daos_iod_t		iods[SB_AKEYS];
	daos_key_t		dkey;
	uint64_t		magic;
	uint16_t		sb_ver;
	uint16_t		layout_ver;
	daos_size_t		chunk_size = 0;
	daos_oclass_id_t	oclass = OC_UNKNOWN;
	daos_obj_id_t		super_oid;
	int			i, rc;

	if (oh == NULL)
		return EINVAL;

	/** Open SB object */
	super_oid.lo = RESERVED_LO;
	super_oid.hi = SB_HI;
	daos_obj_generate_id(&super_oid, 0, OC_RP_XSF, 0);

	rc = daos_obj_open(coh, super_oid, create ? DAOS_OO_RW : DAOS_OO_RO,
			   oh, NULL);
	if (rc) {
		D_ERROR("daos_obj_open() Failed (%d)\n", rc);
		return daos_der2errno(rc);
	}

	d_iov_set(&dkey, SB_DKEY, strlen(SB_DKEY));

	i = 0;
	d_iov_set(&sg_iovs[i], &magic, sizeof(uint64_t));
	d_iov_set(&iods[i].iod_name, MAGIC_NAME, strlen(MAGIC_NAME));
	i++;

	d_iov_set(&sg_iovs[i], &sb_ver, sizeof(uint64_t));
	d_iov_set(&iods[i].iod_name, SB_VERSION_NAME, strlen(SB_VERSION_NAME));
	i++;

	d_iov_set(&sg_iovs[i], &layout_ver, sizeof(uint64_t));
	d_iov_set(&iods[i].iod_name, LAYOUT_NAME, strlen(LAYOUT_NAME));
	i++;

	d_iov_set(&sg_iovs[i], &chunk_size, sizeof(daos_size_t));
	d_iov_set(&iods[i].iod_name, CS_NAME, strlen(CS_NAME));
	i++;

	d_iov_set(&sg_iovs[i], &oclass, sizeof(daos_oclass_id_t));
	d_iov_set(&iods[i].iod_name, OC_NAME, strlen(OC_NAME));
	i++;

	for (i = 0; i < SB_AKEYS; i++) {
		sgls[i].sg_nr		= 1;
		sgls[i].sg_nr_out	= 0;
		sgls[i].sg_iovs		= &sg_iovs[i];


		dcb_set_null(&iods[i].iod_kcsum);
		iods[i].iod_nr		= 1;
		iods[i].iod_size	= DAOS_REC_ANY;
		iods[i].iod_recxs	= NULL;
		iods[i].iod_eprs	= NULL;
		iods[i].iod_csums	= NULL;
		iods[i].iod_type	= DAOS_IOD_SINGLE;
	}

	/** create the SB and exit */
	if (create) {
		iods[0].iod_size = sizeof(uint64_t);
		magic = DFS_SB_MAGIC;
		iods[1].iod_size = sizeof(uint16_t);
		sb_ver = DFS_SB_VERSION;
		iods[2].iod_size = sizeof(uint16_t);
		layout_ver = DFS_LAYOUT_VERSION;
		iods[3].iod_size = sizeof(daos_size_t);
		if (attr && attr->da_chunk_size != 0)
			chunk_size = attr->da_chunk_size;
		else
			chunk_size = DFS_DEFAULT_CHUNK_SIZE;
		iods[4].iod_size = sizeof(daos_oclass_id_t);
		if (attr && attr->da_oclass_id != OC_UNKNOWN)
			oclass = attr->da_oclass_id;
		else
			oclass = DFS_DEFAULT_OBJ_CLASS;

		rc = daos_obj_update(*oh, DAOS_TX_NONE, &dkey, SB_AKEYS, iods,
				     sgls, NULL);
		if (rc) {
			D_ERROR("Failed to update SB info (%d)\n", rc);
			D_GOTO(err, rc = daos_der2errno(rc));
		}

		return 0;
	}

	/* otherwise fetch the values and verify SB */
	rc = daos_obj_fetch(*oh, DAOS_TX_NONE, &dkey, SB_AKEYS, iods, sgls,
			    NULL, NULL);
	if (rc) {
		D_ERROR("Failed to fetch SB info (%d)\n", rc);
		D_GOTO(err, rc = daos_der2errno(rc));
	}

	/** check if SB info exists */
	if (iods[0].iod_size == 0) {
		D_ERROR("SB does not exist.\n");
		D_GOTO(err, rc = ENOENT);
	}

	if (magic != DFS_SB_MAGIC) {
		D_ERROR("SB MAGIC verification failed\n");
		D_GOTO(err, rc = EINVAL);
	}

	D_ASSERT(attr);
	attr->da_chunk_size = (chunk_size) ? chunk_size :
		DFS_DEFAULT_CHUNK_SIZE;
	attr->da_oclass_id = (oclass != OC_UNKNOWN) ? oclass :
		DFS_DEFAULT_OBJ_CLASS;

	/** TODO - check SB & layout versions */
	return 0;
err:
	daos_obj_close(*oh, NULL);
	return rc;
}

void JNI_OnUnload(JavaVM* vm, void *reserved) {
    JNIEnv* env;
    (*vm)->GetEnv(vm, (void**)&env, JNI_VERSION);
    (*env)->DeleteGlobalRef(env, JC_String);
    (*env)->DeleteGlobalRef(env, JC_Exception);
    daos_fini();
}

JNIEXPORT jint JNICALL Java_com_intel_daos_DaosJNI_daosInit
  (JNIEnv *env, jobject obj){
    int rc = daos_init();
    return rc;
}

JNIEXPORT jstring JNICALL Java_com_intel_daos_DaosJNI_daosPoolCreate
  (JNIEnv *env, jobject obj, jlong scm, jlong nvme) {
  	printf(" daosPoolCreate \n");
    d_rank_t  svc[max_svc_nreplicas];
	d_rank_list_t	svcl = {};
    memset(svc, 0, sizeof(svc));
    svcl.rl_ranks = svc;
    svcl.rl_nr = default_svc_nreplicas;
	if (scm < 0 || nvme < 0) {
		D_ERROR("Size provided is negative!");
		D_GOTO(out_daos, EINVAL);
	}
	uuid_t pool_uuid;
	int	i;
	  	printf(" daosPoolCreate 1 \n");
	int rc = daos_pool_create(0731 /* mode */,
					geteuid() /* user owner */,
					getegid() /* group owner */,
					server_group /* daos server process set ID */,
					NULL /* list of targets, NULL = all */,
					"pmem" /* storage type to use, use default */,
					scm ,
					nvme ,
					NULL ,
					&svcl /* pool service nodes, used for connect */,
					pool_uuid, /* the uuid of the pool created */
					NULL /* event, use blocking call for now */);
	printf(" daosPoolCreate 2 ,rc =%d \n",rc);
	if (rc) {
		D_ERROR("daos native error in create_pool():failed to create pool with rc = %d", rc);
		D_GOTO(out_daos, rc);
	}

	char tmp[50];
    uuid_unparse(pool_uuid, tmp);
    strcat(tmp," ");
	/* Print the pool service replica ranks. */
	for (i = 0; i < svcl.rl_nr - 1; i++){
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
}

JNIEXPORT jlong JNICALL Java_com_intel_daos_DaosJNI_daosPoolConnect
  (JNIEnv *env, jobject obj, jstring p_uuid, jint mode,jstring p_svc) {
    daos_handle_t poh;
    int rc;
    const char *pool_str = (*env)->GetStringUTFChars(env, p_uuid, NULL);\
    const char *svc = (*env)->GetStringUTFChars(env, p_svc, NULL);
    uuid_t pool_uuid;
    uuid_parse(pool_str, pool_uuid);
	d_rank_list_t *svcl=daos_rank_list_parse(svc, ":");
	if (svcl == NULL) {
		D_ERROR("Invalid pool service rank list");
		D_GOTO(out_daos, rc = -DER_INVAL);
	}
    rc = daos_pool_connect(pool_uuid, server_group, svcl,
                    mode,
                    &poh /* returned pool handle */,
                    NULL /* returned pool info */,
                    NULL /* event */);
    (*env)->ReleaseStringUTFChars(env, p_uuid, pool_str);

    if (rc != -DER_SUCCESS) {
    	D_ERROR("Failed to connect to pool (%d)", rc);
    	D_GOTO(out_daos, rc=daos_errno2der(rc));

    }else {
        jlong java_poh;
        memcpy(&java_poh, &poh, sizeof(daos_handle_t));
        return java_poh;
    }
out_daos:
    daos_fini();
    return rc;
}

JNIEXPORT jint JNICALL Java_com_intel_daos_DaosJNI_daosPoolDisconnect
  (JNIEnv *env, jobject obj, jlong java_poh) {
    daos_handle_t poh;
    memcpy(&poh, &java_poh, sizeof(daos_handle_t));
    return daos_pool_disconnect(poh, NULL);
}

JNIEXPORT jint JNICALL Java_com_intel_daos_DaosJNI_daosContCreate
  (JNIEnv *env, jobject obj, jlong java_poh, jstring c_uuid) {
    daos_handle_t poh;
    memcpy(&poh, &java_poh, sizeof(daos_handle_t));
    const char *cont_str = (*env)->GetStringUTFChars(env, c_uuid, NULL);
    uuid_t cont_uuid;
    uuid_parse(cont_str, cont_uuid);
	daos_prop_t		*prop = NULL;
	prop = daos_prop_alloc(1);
	if (prop == NULL) {
		D_ERROR("Failed to allocate container prop.");
		return ENOMEM;
	}
	prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_LAYOUT_TYPE;
	prop->dpp_entries[0].dpe_val = DAOS_PROP_CO_LAYOUT_POSIX;
    int rc = daos_cont_create(poh, cont_uuid, prop, NULL /* event */);
    daos_prop_free(prop);
    if (rc) {
    	D_ERROR("daos_cont_create() failed (%d)\n", rc);
    	return daos_der2errno(rc);
    }
    (*env)->ReleaseStringUTFChars(env, c_uuid, cont_str);
    return rc;
}

JNIEXPORT jlong JNICALL Java_com_intel_daos_DaosJNI_daosContOpen
  (JNIEnv *env, jobject obj, jlong java_poh, jstring c_uuid, jint mode) {
    daos_handle_t poh,super_oh;
    daos_cont_info_t	co_info;
    struct dfs_entry	entry = {0};
    memcpy(&poh, &java_poh, sizeof(daos_handle_t));
    const char *cont_str = (*env)->GetStringUTFChars(env, c_uuid, NULL);
    uuid_t cont_uuid;
    uuid_parse(cont_str, cont_uuid);
    daos_handle_t coh;
    int rc = daos_cont_open(poh, cont_uuid, mode, &coh, &co_info, NULL);
    if (rc) {
	   D_ERROR("daos_cont_open() failed (%d)\n", rc);
	   D_GOTO(err_destroy, rc=daos_der2errno(rc));
    }
    (*env)->ReleaseStringUTFChars(env, c_uuid, cont_str);

    dfs_attr_t		dattr;
   	dattr.da_chunk_size = DFS_DEFAULT_CHUNK_SIZE;
   	dattr.da_oclass_id = DFS_DEFAULT_OBJ_CLASS;

   	/** Create SB */

   	rc = open_sb(coh, true, &dattr, &super_oh);

   	if (rc)
   		D_GOTO(err_close, rc);
   	/** Add root object */
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

JNIEXPORT jint JNICALL Java_com_intel_daos_DaosJNI_daosContClose
  (JNIEnv *env, jobject obj, jlong java_coh) {
  daos_handle_t coh;
  memcpy(&coh, &java_coh, sizeof(daos_handle_t));
  return daos_cont_close(coh, NULL);
}

JNIEXPORT jlong JNICALL Java_com_intel_daos_DaosJNI_daosEventQueueCreate
  (JNIEnv *env, jobject obj) {
    daos_handle_t eq;
    int rc = daos_eq_create(&eq);
    if (rc) {
    	D_ERROR("daos native error in eq_create():failed to create event queue with rc = %d", rc);
    	return daos_errno2der(rc);
    }
	jlong java_eq;
	memcpy(&java_eq, &eq, sizeof(daos_handle_t));
	return java_eq;
}

JNIEXPORT jint JNICALL Java_com_intel_daos_DaosJNI_daosEventPoll
  (JNIEnv *env, jobject obj, jlong java_eq, jint num) {
    int acc = 0, rc = 0;
    daos_handle_t eq;
    memcpy(&eq, &java_eq, sizeof(daos_handle_t));
//    daos_event_t **c_evp = (daos_event_t**) (*env)->GetDirectBufferAddress(env, evp);
    daos_event_t **evp = malloc(num * sizeof(daos_event_t*));
    while (acc < num) {
        rc = daos_eq_poll(eq, 0, DAOS_EQ_WAIT, num, evp);
        if (rc >= 0) {
            for (int i = 0; i < rc; ++i) {
                if (evp[i]->ev_error != 0) {
                    rc = evp[i]->ev_error;
                    goto out;
                }
            }
            acc += rc;
        } else {
            goto out;
        }
    }
out:
    free(evp);
    return rc;
  }

JNIEXPORT jint JNICALL Java_com_intel_daos_DaosJNI_daosFinish
  (JNIEnv *env, jobject obj) {
	return daos_fini();
}

JNIEXPORT jint JNICALL Java_com_intel_daos_DaosJNI_daosPoolDestroy
  (JNIEnv *env, jobject obj, jstring p_uuid){
	daos_handle_t poh;
	int rc;
	const char *pool_str = (*env)->GetStringUTFChars(env, p_uuid, NULL);
	uuid_t pool_uuid;
	uuid_parse(pool_str, pool_uuid);
	/** destroy the pool created in pool_create */
	rc = daos_pool_destroy(pool_uuid, server_group, 1 /* force */,
				       NULL /* event */);
	return rc;
}

