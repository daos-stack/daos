#include <daos_jni_common.h>

struct dfs_obj {
	/** DAOS object ID */
	daos_obj_id_t		oid;
	/** DAOS object open handle */
	daos_handle_t		oh;
	/** mode_t containing permissions & type */
	mode_t			mode;
	/** DAOS object ID of the parent of the object */
	daos_obj_id_t		parent_oid;
	/** entry name of the object in the parent */
	char			name[DFS_MAX_PATH];
	/** Symlink value if object is a symbolic link */
	char			*value;
};

/** dfs struct that is instantiated for a mounted DFS namespace */
struct dfs {
	/** flag to indicate whether the dfs is mounted */
	bool			mounted;
	/** lock for threadsafety */
	pthread_mutex_t		lock;
	/** uid - inherited from pool. TODO - make this from container. */
	uid_t			uid;
	/** gid - inherited from pool. TODO - make this from container. */
	gid_t			gid;
	/** Access mode (RDONLY, RDWR) */
	int			amode;
	/** Open pool handle of the DFS */
	daos_handle_t		poh;
	/** Open container handle of the DFS */
	daos_handle_t		coh;
	/** Object ID reserved for this DFS (see oid_gen below) */
	daos_obj_id_t		oid;
	/** Open object handle of SB */
	daos_handle_t		super_oh;
	/** Root object info */
	dfs_obj_t		root;
	/** DFS container attributes (Default chunk size, oclass, etc.) */
	dfs_attr_t		attr;
};

static dfs_t *DFS = NULL;

JNIEXPORT jint JNICALL Java_com_intel_daos_DaosJNI_daosFSMount
  (JNIEnv *env, jobject obj, jlong java_poh, jlong java_coh, jboolean readOnly) {
  	daos_handle_t poh, coh;
  	memcpy(&poh, &java_poh, sizeof(daos_handle_t));
  	memcpy(&coh, &java_coh, sizeof(daos_handle_t));
  	int flags = readOnly ? O_RDONLY : O_RDWR;
  	int rc = dfs_mount(poh, coh, flags, &DFS);
  	return rc;
}

JNIEXPORT jint JNICALL Java_com_intel_daos_DaosJNI_daosFSUmount
  (JNIEnv *env, jobject obj) {
   if (DFS) {
       int rc = dfs_umount(DFS);
       if (!rc) DFS = NULL;
       return rc;
   }
}

JNIEXPORT jboolean JNICALL Java_com_intel_daos_DaosJNI_daosFSIsDir
  (JNIEnv *env, jobject jobj, jstring path) {
    const char* path_str = (*env)->GetStringUTFChars(env, path, NULL);
    dfs_obj_t *entry = NULL;
    mode_t tmp_mode;
    int rc = dfs_lookup(DFS, path_str, O_RDONLY, &entry, &tmp_mode, NULL);
    (*env)->ReleaseStringUTFChars(env, path, path_str);
    if (rc) {
    	D_ERROR("daos native error in isDir(): failed to lookup with rc = %d", rc);
        return false;
    }
    bool result =  S_ISDIR(entry->mode);
    dfs_release(entry);
    return result;
}

JNIEXPORT jboolean JNICALL Java_com_intel_daos_DaosJNI_daosFsIfExist
  (JNIEnv *env, jobject jobj, jstring path) {
    const char* path_str = (*env)->GetStringUTFChars(env, path, NULL);
    dfs_obj_t *entry = NULL;
    mode_t tmp_mode;
    int rc = dfs_lookup(DFS, path_str, O_RDONLY, &entry, &tmp_mode, NULL);
    (*env)->ReleaseStringUTFChars(env, path, path_str);
    if (rc) {
    	D_ERROR("daos native error in ifExist(): failed to lookup with rc = %d", rc);
         return false;
    } else {
         dfs_release(entry);
         return true;
    }
}

static int createParentDirs(dfs_t *dfs, const char *path, int dir_mode) {
    char *dirs, *bases, *dir, *base;
    dirs = strdup(path);
    dir = dirname(dirs);
    bases = strdup(path);
    base = basename(bases);
    dfs_obj_t *handle = NULL;
    mode_t mode;
    int rc = dfs_lookup(dfs, dir, O_RDWR, &handle, &mode, NULL);
    if (rc == -DER_NONEXIST || rc == -ENOENT || rc == ENOENT) {
        // recursively create it
        rc = createParentDirs(dfs, dir, dir_mode);
        if (rc) goto out;
        rc = dfs_lookup(dfs, dir, O_RDWR, &handle, &mode, NULL);
        if (rc) goto out;
    }
    else if (rc) goto out;
    if (dir_mode) rc = dfs_mkdir(dfs, handle, base, 0755);
    else rc = dfs_mkdir(dfs, handle, base, dir_mode);
out:
    if (dirs) free(dirs);
    if (bases) free(bases);
    if (handle) dfs_release(handle);
    return rc;
}

JNIEXPORT jlong JNICALL Java_com_intel_daos_DaosJNI_daosFSCreateDir
  (JNIEnv *env, jobject jobj, jstring path, jint mode) {
    const char* path_str = (*env)->GetStringUTFChars(env, path, NULL);
    dfs_obj_t *dir = NULL;
    mode_t tmp_mode;
    int rc = createParentDirs(DFS, path_str, mode);
    if (rc) {
        goto out;
    }
    rc = dfs_lookup(DFS, path_str, O_RDWR, &dir, &tmp_mode, NULL);
    if (rc) {
        goto out;
    }
out:
    (*env)->ReleaseStringUTFChars(env, path, path_str);
    if (rc) {
        if (dir) dfs_release(dir);
        D_ERROR("daos native error in createDir(): failed with rc = %d", rc);
        return daos_errno2der(rc);
    }
    jlong handle;
    memcpy(&handle, &dir, sizeof(dfs_obj_t*));
    return handle;
}

JNIEXPORT jlong JNICALL Java_com_intel_daos_DaosJNI_daosFSOpenDir
  (JNIEnv *env, jobject jobj, jstring path, jboolean readOnly) {
    const char* path_str = (*env)->GetStringUTFChars(env, path, NULL);
    mode_t tmp_mode;
    int flags = readOnly ? O_RDONLY : O_RDWR;
    dfs_obj_t *dir = NULL;
    int rc = dfs_lookup(DFS, path_str, flags, &dir, &tmp_mode, NULL);
    (*env)->ReleaseStringUTFChars(env, path, path_str);
    if (rc) {
    	D_ERROR("daos native error in openDir(): failed with rc = %d", rc);
        return daos_errno2der(rc);
    }
    if (!S_ISDIR(dir->mode)){
    	D_ERROR("daos JNI error in openDir(): supplied path is not a directory");
        return daos_errno2der(ENOTDIR);
    }
    jlong handle;
    memcpy(&handle, &dir, sizeof(dfs_obj_t*));
    return handle;
}

JNIEXPORT jlong JNICALL Java_com_intel_daos_DaosJNI_daosFSOpenFile
  (JNIEnv *env, jobject jobj, jstring path, jboolean readOnly) {
    const char* path_str = (*env)->GetStringUTFChars(env, path, NULL);
    mode_t tmp_mode;
    int flags = readOnly ? O_RDONLY : O_RDWR;
    dfs_obj_t *file = NULL;
    int rc = dfs_lookup(DFS, path_str, flags, &file, &tmp_mode, NULL);
    (*env)->ReleaseStringUTFChars(env, path, path_str);
    if (rc) {
    	D_ERROR("daos native error in openFile(): failed with rc = %d", rc);
        return daos_errno2der(rc);
    }
    if (!S_ISREG(file->mode)){
    	D_ERROR("daos JNI error in openFile(): supplied path is not a file");
        return daos_errno2der(ENOENT);
    }
    jlong handle;
    memcpy(&handle, &file, sizeof(dfs_obj_t*));
    return handle;
}

JNIEXPORT jlong JNICALL Java_com_intel_daos_DaosJNI_daosFSCreateFile
  (JNIEnv *env, jobject jobj, jstring path, jint mode, jlong chunk_size, jint cid) {
    const char* path_str = (*env)->GetStringUTFChars(env, path, NULL);
    dfs_obj_t *file = NULL, *parent = NULL;
    mode_t tmp_mode;
    char *dirs = NULL, *bases = NULL, *dir, *base;
    dirs = strdup(path_str);
    dir = dirname(dirs);
    bases = strdup(path_str);
    base = basename(bases);
    int rc = dfs_lookup(DFS, dir, O_RDWR, &parent, &tmp_mode, NULL);
    if (rc == -DER_NONEXIST || rc == -ENOENT || rc == ENOENT) {
        rc = createParentDirs(DFS, dir, 0755);
        if (rc) goto out;
        rc = dfs_lookup(DFS, dir, O_RDWR, &parent, &tmp_mode, NULL);
        if (rc) goto out;
    }
    rc = dfs_open(DFS, parent, base, S_IFREG | mode, O_CREAT | mode, cid, chunk_size, NULL, &file);
out:
    (*env)->ReleaseStringUTFChars(env, path, path_str);
    if (dirs) free(dirs);
    if (bases) free(bases);
    if (parent) dfs_release(parent);
    if (rc) {
        if (file) dfs_release(file);
        D_ERROR("daos native error in createFile(): failed with rc = %d", rc);
        return daos_errno2der(rc);
    }
    jlong handle;
    memcpy(&handle, &file, sizeof(dfs_obj_t*));
    return handle;
}

JNIEXPORT jint JNICALL Java_com_intel_daos_DaosJNI_daosFSClose
  (JNIEnv *env, jobject jobj, jlong handle) {
    dfs_obj_t *file;
    memcpy(&file, &handle, sizeof(dfs_obj_t*));
    return dfs_release(file);
}

JNIEXPORT jint JNICALL Java_com_intel_daos_DaosJNI_daosFSRead
  (JNIEnv *env, jobject jobj, jlong handle, jlong offset, jobject buffer) {
    dfs_obj_t *file;
    memcpy(&file, &handle, sizeof(dfs_obj_t*));
    jbyte *buf = (jbyte*)(*env)->GetDirectBufferAddress(env, buffer);
    jlong buflen = (*env)->GetDirectBufferCapacity(env, buffer);
    d_iov_t sg_iov = {0};
    d_sg_list_t sgl = {
        .sg_nr = 1,
        .sg_nr_out = 0,
        .sg_iovs = &sg_iov
    };
    d_iov_set(&sg_iov, buf, buflen);
    daos_size_t size = 0;

    int rc = dfs_read(DFS, file, &sgl, offset, &size, NULL);
    if (rc) {
    	D_ERROR("daos native error in read(): failed with rc = %d", rc);
    	return rc;
    }
    return size;
}

JNIEXPORT jint JNICALL Java_com_intel_daos_DaosJNI_daosFSWrite
  (JNIEnv *env, jobject jobj, jlong handle, jlong offset,
   jobject buffer, jint buffer_offset, jint length) {
    dfs_obj_t *file;
    memcpy(&file, &handle, sizeof(dfs_obj_t*));
    jbyte *buf = (jbyte*)(*env)->GetDirectBufferAddress(env, buffer);
    jlong buflen = (*env)->GetDirectBufferCapacity(env, buffer);
    if (buffer_offset + length > buflen) {
    	D_ERROR("Specified length is beyond border of buffer.");
        return daos_errno2der(EINVAL);
    }
    d_iov_t sg_iov = {0};
    d_sg_list_t sgl = {
        .sg_nr = 1,
        .sg_nr_out = 0,
        .sg_iovs = &sg_iov
    };
    d_iov_set(&sg_iov, buf, length);
    int rc = dfs_write(DFS, file, &sgl, offset, NULL);
    if (rc) {
    	D_ERROR("daos native error: dfs failed to write with %d", rc);
    	return daos_errno2der(rc);
    }
    return length;
}

JNIEXPORT jlong JNICALL Java_com_intel_daos_DaosJNI_daosFSGetSize__Ljava_lang_String_2
  (JNIEnv *env, jobject jobj, jstring path) {
    const char* path_str = (*env)->GetStringUTFChars(env, path, NULL);
    dfs_obj_t *file;
    mode_t tmp_mode;
    int rc = dfs_lookup(DFS, path_str, O_RDONLY, &file, &tmp_mode, NULL);
    (*env)->ReleaseStringUTFChars(env, path, path_str);
    if (rc) {
    	D_ERROR("daos native error in size(): failed to lookup with rc = %d", rc);
        return daos_errno2der(rc);
    }
    daos_size_t size;
    rc = dfs_get_size(DFS, file, &size);
    if (file) dfs_release(file);
    if (rc) {
    	D_ERROR("daos native error in size(): failed to get size with rc = %d", rc);
        return daos_errno2der(rc);
    }
    return size;
}

JNIEXPORT jlong JNICALL Java_com_intel_daos_DaosJNI_daosFSGetSize__J
  (JNIEnv *env, jobject jobj, jlong handle) {
    dfs_obj_t *file;
    memcpy(&file, &handle, sizeof(dfs_obj_t*));
    daos_size_t size;
    int rc = dfs_get_size(DFS, file, &size);
    if (rc) {
    	D_ERROR("daos native error in size(): failed to get size with rc = %d", rc);
        return daos_errno2der(rc);
    }
    return size;
}

JNIEXPORT jstring JNICALL Java_com_intel_daos_DaosJNI_daosFSListDir__Ljava_lang_String_2
  (JNIEnv *env, jobject jobj, jstring path) {
    const char* path_str = (*env)->GetStringUTFChars(env, path, NULL);
    dfs_obj_t *dir;
    mode_t tmp_mode;
    int rc = dfs_lookup(DFS, path_str, O_RDONLY, &dir, &tmp_mode, NULL);
    (*env)->ReleaseStringUTFChars(env, path, path_str);
    if (!S_ISDIR(dir->mode)) {
    	D_ERROR("Given path is not a directory");
        return NULL;
    }
    else if (rc) {
    	D_ERROR("daos native error in list(): failed to lookup with rc = %d", rc);
        return NULL;
    }
    daos_anchor_t anchor = {0};
    struct dirent entry;
    uint32_t nr = 1, size = STR_BUFFER_LEN, acc = 0;
    char *buffer = malloc(size);
    if (!buffer) {
    	D_ERROR("native error in list(): failed to allocate memory");
        return NULL;
    }
    buffer[0] = '\0';
    while(!daos_anchor_is_eof(&anchor)) {
        nr = 1;
        rc = dfs_readdir(DFS, dir, &anchor, &nr, &entry);
        if (rc) break;
        if (!nr) continue;
        // exactly 1 for each file because ',' and \0
        acc += strlen(entry.d_name) + 1;
        if (acc >= size) {
            size += STR_BUFFER_LEN;
            buffer = realloc(buffer, size);
            if (!buffer) {
            	D_ERROR("native error in list(): failed to allocate memory");
                return NULL;
            }
        }
        if (buffer[0]) strcat(buffer, ",");
        strcat(buffer, entry.d_name);
    }
    if (dir) dfs_release(dir);
    if (rc) {
    	D_ERROR("daos native error in list(): failed to readdir with rc = %d", rc);
        free(buffer);
        return NULL;
    }
    jstring result = (*env)->NewStringUTF(env, buffer);
    free(buffer);
    return result;
}

JNIEXPORT jstring JNICALL Java_com_intel_daos_DaosJNI_daosFSListDir__J
  (JNIEnv *env, jobject jobj, jlong handle) {
    dfs_obj_t *dir;
    memcpy(&dir, &handle, sizeof(dfs_obj_t*));
    daos_anchor_t anchor = {0};
    struct dirent entry;
    uint32_t nr = 1, size = STR_BUFFER_LEN, acc = 0, rc;
    char *buffer = malloc(size);
    if (!buffer) {
    	D_ERROR("native error in list(): failed to allocate memory");
        return NULL;
    }
    buffer[0] = '\0';
    while(!daos_anchor_is_eof(&anchor)) {
        nr = 1;
        rc = dfs_readdir(DFS, dir, &anchor, &nr, &entry);
        if (rc) break;
        if (!nr) continue;
        // exactly 1 for each file because ',' and \0
        acc += strlen(entry.d_name) + 1;
        if (acc >= size) {
            size += STR_BUFFER_LEN;
            buffer = realloc(buffer, size);
            if (!buffer) {
            	D_ERROR("native error in list(): failed to allocate memory");
                return NULL;
            }
        }
        if (buffer[0]) strcat(buffer, ",");
        strcat(buffer, entry.d_name);
    }
    if (rc) {
    	D_ERROR("daos native error in list(): failed to readdir with rc = %d", rc);
        free(buffer);
        return NULL;
    }
    jstring result = (*env)->NewStringUTF(env, buffer);
    free(buffer);
    return result;
}

JNIEXPORT jint JNICALL Java_com_intel_daos_DaosJNI_daosFSMove__Ljava_lang_String_2Ljava_lang_String_2
  (JNIEnv *env, jobject jobj, jstring path, jstring new_path) {
    const char* path_str = (*env)->GetStringUTFChars(env, path, NULL);
    const char* new_path_str = (*env)->GetStringUTFChars(env, new_path, NULL);
    char *old_dir_str = NULL, *old_base_str = NULL, *new_dir_str = NULL, *new_base_str = NULL;
    old_dir_str = strdup(path_str);
    old_base_str = strdup(path_str);
    new_dir_str = strdup(new_path_str);
    new_base_str = strdup(new_path_str);
    (*env)->ReleaseStringUTFChars(env, path, path_str);
    (*env)->ReleaseStringUTFChars(env, new_path, new_path_str);
    char * old_dir = dirname(old_dir_str);
    char * old_base = basename(old_base_str);
    char * new_dir = dirname(new_dir_str);
    char * new_base = basename(new_base_str);
    dfs_obj_t *old_dir_handle = NULL, *new_dir_handle = NULL;
    mode_t tmp_mode;
    int rc = dfs_lookup(DFS, old_dir, O_RDWR, &old_dir_handle, &tmp_mode, NULL);
    if (rc) goto out;
    if (strcmp(old_dir, new_dir) == 0) new_dir_handle = old_dir_handle;
    else rc = dfs_lookup(DFS, new_dir, O_RDWR, &new_dir_handle, &tmp_mode, NULL);
    if (rc)  goto out;
    rc = dfs_move(DFS, old_dir_handle, old_base, new_dir_handle, new_base, NULL);
out:
    if (old_dir_str) free(old_dir_str);
    if (old_base_str) free(old_base_str);
    if (new_dir_str) free(new_dir_str);
    if (new_base_str) free(new_base_str);
    if (old_dir_handle) dfs_release(old_dir_handle);
    if (new_dir_handle != old_dir_handle) dfs_release(new_dir_handle);
    return rc;
}

JNIEXPORT jint JNICALL Java_com_intel_daos_DaosJNI_daosFSMove__JLjava_lang_String_2JLjava_lang_String_2
  (JNIEnv *env, jobject jobj, jlong parent, jstring name,
                              jlong new_parent, jstring new_name) {
    const char* name_str = (*env)->GetStringUTFChars(env, name, NULL);
    const char* new_name_str = (*env)->GetStringUTFChars(env, new_name, NULL);
    char *base = strdup(name_str);
    char *new_base = strdup(new_name_str);
    dfs_obj_t *old_dir, *new_dir;
    memcpy(&old_dir, &parent, sizeof(dfs_obj_t*));
    memcpy(&new_dir, &new_parent, sizeof(dfs_obj_t*));

    int rc = dfs_move(DFS, old_dir, base, new_dir, new_base, NULL);
    (*env)->ReleaseStringUTFChars(env, name, name_str);
    (*env)->ReleaseStringUTFChars(env, new_name, new_name_str);
    if(base) free(base);
    if(new_base) free(new_base);
    return rc;
}

JNIEXPORT jint JNICALL Java_com_intel_daos_DaosJNI_daosFSRemove__Ljava_lang_String_2
  (JNIEnv *env, jobject jobj, jstring path) {
    const char* path_str = (*env)->GetStringUTFChars(env, path, NULL);
    char *dir_str = strdup(path_str);
    char *base_str = strdup(path_str);
    char *dir = dirname(dir_str);
    char *base = basename(base_str);
    mode_t tmp_mode;
    dfs_obj_t *dir_handle = NULL;
    int rc = 0;
    if (strcmp(dir, "/") != 0)
        rc = dfs_lookup(DFS, dir, O_RDWR, &dir_handle, &tmp_mode, NULL);
    if (rc) goto out;
    rc = dfs_remove(DFS, dir_handle, base, true, NULL);
out:
    (*env)->ReleaseStringUTFChars(env, path, path_str);
    if (dir_str) free(dir_str);
    if (base_str) free(base_str);
    if (dir_handle) dfs_release(dir_handle);
    return rc;
}

JNIEXPORT jint JNICALL Java_com_intel_daos_DaosJNI_daosFSRemove__JLjava_lang_String_2
  (JNIEnv *env, jobject jobj, jlong parent, jstring name) {
    const char* name_str = (*env)->GetStringUTFChars(env, name, NULL);
    dfs_obj_t *dir;
    memcpy(&dir, &parent, sizeof(dfs_obj_t*));
    int rc = dfs_remove(DFS, dir, name_str, true, NULL);
    (*env)->ReleaseStringUTFChars(env, name, name_str);
    return rc;
}
