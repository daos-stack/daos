#include "daos_jni_common.h"
#include <time.h>

JNIEXPORT jlong JNICALL Java_com_intel_daos_DaosJNI_daosObjectOpen
  (JNIEnv *env, jobject obj, jlong java_poh, jlong java_coh, jlong oid_lo, jint mode, jint ofeat, jint cid) {
	daos_handle_t oh, poh, coh;
	memcpy(&poh, &java_poh, sizeof(daos_handle_t));
	memcpy(&coh, &java_coh, sizeof(daos_handle_t));
	daos_obj_id_t oid = {
			.lo = oid_lo
	};
	daos_ofeat_t obj_feat = ofeat;
	daos_oclass_id_t classid = cid;
	daos_obj_generate_id(&oid, obj_feat, cid, 0);
	int rc = daos_obj_open(coh, oid, mode, &oh, NULL);
	if (rc) {
		D_ERROR("daos native error: failed to open object with rc = %d", rc);
	    return daos_errno2der(rc);
	}
	jlong java_oh;
	memcpy(&java_oh, &oh, sizeof(daos_handle_t));
	return java_oh;
}

JNIEXPORT jint JNICALL Java_com_intel_daos_DaosJNI_daosObjectClose
  (JNIEnv *env, jobject obj, jlong oh) {
	daos_handle_t c_oh;
	memcpy(&c_oh, &oh, sizeof(daos_handle_t));
	return daos_obj_close(c_oh, NULL);
}

JNIEXPORT jlong JNICALL Java_com_intel_daos_DaosJNI_allocateIOReq
  (JNIEnv *env, jclass jobj, jint keys_length, jlong eqh) {
    ioreq *req = malloc(sizeof(ioreq) + keys_length + 2);
    if (!req) {
    	D_ERROR("out of memory");
        return daos_errno2der(ENOMEM);
    }
    daos_handle_t c_eq;
    memcpy(&c_eq, &eqh, sizeof(daos_handle_t));
    int rc = daos_event_init(&req->ev, c_eq, NULL);
    if (rc) {
    	D_ERROR("daos native error: failed to init event with %d", rc);
        free(req);
        return daos_errno2der(rc);
    }
    jlong pointer;
    memcpy(&pointer, &req, sizeof(ioreq*));
    return pointer;
}

JNIEXPORT void JNICALL Java_com_intel_daos_DaosJNI_free
  (JNIEnv *env, jclass jobj, jlong pointer) {
  void *p;
  memcpy(&p, &pointer, sizeof(void*));
  free(p);
}

static void ioreq_init
(ioreq *req, JNIEnv *env, jstring dkey, jstring akey, jobject buffer) {
    const char *dkey_tmp = (*env)->GetStringUTFChars(env, dkey, NULL);
    const char *akey_tmp = (*env)->GetStringUTFChars(env, akey, NULL);
    char *dkey_str = req->keys;
    char *akey_str = &req->keys[strlen(dkey_tmp) + 1];
    strcpy(dkey_str, dkey_tmp);
    strcpy(akey_str, akey_tmp);
    (*env)->ReleaseStringUTFChars(env, dkey, dkey_tmp);
    (*env)->ReleaseStringUTFChars(env, akey, akey_tmp);
    jbyte *buf = (jbyte*)(*env)->GetDirectBufferAddress(env,buffer);
    jlong buflen = (*env)->GetDirectBufferCapacity(env, buffer);
    req->iod.iod_csums = NULL;
    req->iod.iod_eprs = NULL;
    req->iod.iod_nr	= 1;
    req->sgl.sg_nr = 1;
    req->sgl.sg_nr_out = 0;
    req->sgl.sg_iovs = &req->sg_iov;
    dcb_set_null(&req->iod.iod_kcsum);
    d_iov_set(&req->iod.iod_name, akey_str, strlen(akey_str));
    d_iov_set(&req->dkey, dkey_str, strlen(dkey_str));
    d_iov_set(&req->sg_iov, buf, buflen);
}

JNIEXPORT jlong JNICALL Java_com_intel_daos_DaosJNI_daosObjectFetchSingle
  (JNIEnv *env, jobject obj, jlong oh, jstring dkey, jstring akey, jobject buffer) {
    daos_handle_t c_oh;
    memcpy(&c_oh, &oh, sizeof(daos_handle_t));
    ioreq *req = malloc(sizeof(ioreq) + (*env)->GetStringUTFLength(env, dkey)
                                      + (*env)->GetStringUTFLength(env, akey) + 2);

    ioreq_init(req, env, dkey, akey, buffer);
    req->iod.iod_size = DAOS_REC_ANY;
    req->iod.iod_recxs = NULL;
    req->iod.iod_type = DAOS_IOD_SINGLE;
    int rc = daos_obj_fetch(c_oh, DAOS_TX_NONE, &req->dkey, 1, &req->iod, &req->sgl, NULL, NULL);
    long size = req->iod.iod_size;
    free(req);
    if (rc) {
    	D_ERROR("daos native error in fetch(): failed to fetch with %d", rc);
        return daos_errno2der(rc);
    }
    return size;
}
JNIEXPORT jint JNICALL Java_com_intel_daos_DaosJNI_daosObjectFetchSingleAsync
  (JNIEnv *env, jobject obj, jlong oh, jstring dkey, jstring akey, jobject buffer, jlong j_req) {
    daos_handle_t c_oh;
    memcpy(&c_oh, &oh, sizeof(daos_handle_t));
    ioreq *req;
    memcpy(&req, &j_req, sizeof(ioreq*));
    ioreq_init(req, env, dkey, akey, buffer);
    req->iod.iod_size = DAOS_REC_ANY;
    req->iod.iod_recxs = NULL;
    req->iod.iod_type = DAOS_IOD_SINGLE;
    return daos_obj_fetch(c_oh, DAOS_TX_NONE, &req->dkey, 1, &req->iod, &req->sgl, NULL, &req->ev);
}


JNIEXPORT jlong JNICALL Java_com_intel_daos_DaosJNI_daosObjectFetchArray
  (JNIEnv *env, jobject obj, jlong oh, jstring dkey, jstring akey,
  jlong idx, jlong number, jobject buffer) {
	daos_handle_t c_oh;
	memcpy(&c_oh, &oh, sizeof(daos_handle_t));
	ioreq *req = malloc(sizeof(ioreq) + (*env)->GetStringUTFLength(env, dkey)
                                      + (*env)->GetStringUTFLength(env, akey) + 2);
    ioreq_init(req, env, dkey, akey, buffer);
    req->iod.iod_size = DAOS_REC_ANY;
    req->iod.iod_recxs = &req->recx;
    req->iod.iod_type = DAOS_IOD_ARRAY;
    req->recx.rx_idx = idx;
    req->recx.rx_nr = number;
    int rc = daos_obj_fetch(c_oh, DAOS_TX_NONE, &req->dkey, 1, &req->iod, &req->sgl, NULL, NULL);
    long size = req->iod.iod_size;
    free(req);
    if (rc) {
    	D_ERROR("daos native error in fetch(): failed to fetch with %d", rc);
        return daos_errno2der(rc);
    }
    return size;
}

JNIEXPORT jint JNICALL Java_com_intel_daos_DaosJNI_daosObjectFetchArrayAsync
  (JNIEnv *env, jobject obj, jlong oh, jstring dkey, jstring akey,
  jlong idx, jlong number, jobject buffer, jlong j_req) {
    daos_handle_t c_oh;
    memcpy(&c_oh, &oh, sizeof(daos_handle_t));
    ioreq *req;
    memcpy(&req, &j_req, sizeof(ioreq*));
    ioreq_init(req, env, dkey, akey, buffer);
    req->iod.iod_size = DAOS_REC_ANY;
    req->iod.iod_recxs = &req->recx;
    req->iod.iod_type = DAOS_IOD_ARRAY;
    req->recx.rx_idx = idx;
    req->recx.rx_nr = number;
    return daos_obj_fetch(c_oh, DAOS_TX_NONE, &req->dkey, 1, &req->iod, &req->sgl, NULL, &req->ev);
}

JNIEXPORT jint JNICALL Java_com_intel_daos_DaosJNI_daosObjectUpdateSingle
  (JNIEnv *env, jobject obj, jlong oh, jstring dkey, jstring akey, jobject buffer) {
	daos_handle_t c_oh;
	memcpy(&c_oh, &oh, sizeof(daos_handle_t));
	ioreq *req = malloc(sizeof(ioreq) + (*env)->GetStringUTFLength(env, dkey)
                                      + (*env)->GetStringUTFLength(env, akey) + 2);
    ioreq_init(req, env, dkey, akey, buffer);
    req->iod.iod_size = req->sg_iov.iov_buf_len;
    req->iod.iod_recxs = NULL;
    req->iod.iod_type = DAOS_IOD_SINGLE;
    int rc = daos_obj_update(c_oh, DAOS_TX_NONE, &req->dkey, 1, &req->iod, &req->sgl, NULL);
    free(req);
    return rc;
}

JNIEXPORT jint JNICALL Java_com_intel_daos_DaosJNI_daosObjectUpdateSingleAsync
  (JNIEnv *env, jobject obj, jlong oh, jstring dkey, jstring akey, jobject buffer, jlong j_req) {
    daos_handle_t c_oh;
    memcpy(&c_oh, &oh, sizeof(daos_handle_t));
    ioreq *req;
    memcpy(&req, &j_req, sizeof(ioreq*));
    ioreq_init(req, env, dkey, akey, buffer);
    req->iod.iod_size = req->sg_iov.iov_buf_len;
    req->iod.iod_recxs = NULL;
    req->iod.iod_type = DAOS_IOD_SINGLE;
    return daos_obj_update(c_oh, DAOS_TX_NONE, &req->dkey, 1, &req->iod, &req->sgl, &req->ev);
}

JNIEXPORT jint JNICALL Java_com_intel_daos_DaosJNI_daosObjectUpdateArray
  (JNIEnv *env, jobject obj, jlong oh, jstring dkey, jstring akey,
   jlong index, jlong size, jobject buffer) {
	daos_handle_t c_oh;
	memcpy(&c_oh, &oh, sizeof(daos_handle_t));
	ioreq *req = malloc(sizeof(ioreq) + (*env)->GetStringUTFLength(env, dkey)
                                      + (*env)->GetStringUTFLength(env, akey) + 2);
    ioreq_init(req, env, dkey, akey, buffer);
    req->iod.iod_size = size;
    req->iod.iod_recxs = &req->recx;
    req->iod.iod_type = DAOS_IOD_ARRAY;
    req->recx.rx_idx = index;
    req->recx.rx_nr = req->sg_iov.iov_buf_len / size;
    int rc = daos_obj_update(c_oh, DAOS_TX_NONE, &req->dkey, 1, &req->iod, &req->sgl, NULL);
    free(req);
    return rc;
}

JNIEXPORT jint JNICALL Java_com_intel_daos_DaosJNI_daosObjectUpdateArrayAsync
  (JNIEnv *env, jobject obj, jlong oh, jstring dkey, jstring akey,
   jlong index, jlong size, jobject buffer, jlong j_req) {
    daos_handle_t c_oh;
    memcpy(&c_oh, &oh, sizeof(daos_handle_t));
    ioreq *req;
    memcpy(&req, &j_req, sizeof(ioreq*));
    ioreq_init(req, env, dkey, akey, buffer);
    req->iod.iod_size = size;
    req->iod.iod_recxs = &req->recx;
    req->iod.iod_type = DAOS_IOD_ARRAY;
    req->recx.rx_idx = index;
    req->recx.rx_nr = req->sg_iov.iov_buf_len / size;
    return daos_obj_update(c_oh, DAOS_TX_NONE, &req->dkey, 1, &req->iod, &req->sgl, &req->ev);
 }

JNIEXPORT jint JNICALL Java_com_intel_daos_DaosJNI_daosObjectPunch
  (JNIEnv *env, jclass jobj, jlong oh) {
    daos_handle_t c_oh;
    memcpy(&c_oh, &oh, sizeof(daos_handle_t));
    return daos_obj_punch(c_oh, DAOS_TX_NONE, NULL);
}

JNIEXPORT jint JNICALL Java_com_intel_daos_DaosJNI_daosObjectPunchDkeys
  (JNIEnv *env, jclass jobj, jlong oh, jobjectArray dkeys) {
    daos_handle_t c_oh;
    memcpy(&c_oh, &oh, sizeof(daos_handle_t));
    int nr = (*env)->GetArrayLength(env, dkeys);
    daos_key_t *c_dkeys = calloc(sizeof(daos_key_t), nr);
    jstring *strings = calloc(sizeof(jstring), nr);
    for (int i = 0; i < nr; i++) {
        strings[i] = (jstring) (*env)->GetObjectArrayElement(env, dkeys, i);
        const char* dkey = (*env)->GetStringUTFChars(env, strings[i], NULL);
        d_iov_set(&c_dkeys[i], strdup(dkey), strlen(dkey));
        (*env)->ReleaseStringUTFChars(env, strings[i], dkey);
    }
    int rc = daos_obj_punch_dkeys(c_oh, DAOS_TX_NONE, nr, c_dkeys, NULL);
    for (int i = 0; i < nr; i++) {
        free(c_dkeys[i].iov_buf);
    }
    free(strings);
    free(c_dkeys);
    return rc;
}

JNIEXPORT jint JNICALL Java_com_intel_daos_DaosJNI_daosObjectPunchAkeys
  (JNIEnv *env, jclass jobj, jlong oh, jstring dkey, jobjectArray akeys) {
    daos_handle_t c_oh;
    memcpy(&c_oh, &oh, sizeof(daos_handle_t));
    const char *dkey_str = (*env)->GetStringUTFChars(env, dkey, NULL);
    daos_key_t c_dkey;
    d_iov_set(&c_dkey, strdup(dkey_str), strlen(dkey_str));
    (*env)->ReleaseStringUTFChars(env, dkey, dkey_str);
    int nr = (*env)->GetArrayLength(env, akeys);
    daos_key_t *c_akeys = calloc(sizeof(daos_key_t), nr);
    jstring *strings = calloc(sizeof(jstring), nr);
    for (int i = 0; i < nr; i++) {
        strings[i] = (jstring) (*env)->GetObjectArrayElement(env, akeys, i);
        const char* akey = (*env)->GetStringUTFChars(env, strings[i], NULL);
        d_iov_set(&c_akeys[i], strdup(akey), strlen(akey));
        (*env)->ReleaseStringUTFChars(env, strings[i], akey);

    }
    int rc = daos_obj_punch_akeys(c_oh, DAOS_TX_NONE, &c_dkey, nr, c_akeys, NULL);
    for (int i = 0; i < nr; i++) {
        free(c_akeys[i].iov_buf);
    }
    free(c_dkey.iov_buf);
    free(strings);
    free(c_akeys);
    return rc;
}

JNIEXPORT jstring JNICALL Java_com_intel_daos_DaosJNI_daosObjectListDkey
  (JNIEnv *env, jobject obj, jlong oh) {
	daos_handle_t c_oh;
	memcpy(&c_oh, &oh, sizeof(daos_handle_t));
	daos_anchor_t anchor = {0};
	daos_key_desc_t kds = {0};
	int buflen = STR_BUFFER_LEN, size = buflen, acc = 0, rc;
	char *buf = malloc(buflen);
	char *result = malloc(size);
    d_iov_t sg_iov = {0};
    d_sg_list_t sgl = {
        .sg_nr = 1,
        .sg_nr_out = 0,
        .sg_iovs = &sg_iov
    };
	d_iov_set(&sg_iov, buf, buflen);
    result[0] = '\0';
    buf[0] = '\0';
    for(int nr = 1; !daos_anchor_is_eof(&anchor); nr = 1) {
        rc = daos_obj_list_dkey(c_oh, DAOS_TX_NONE, &nr, &kds, &sgl, &anchor, NULL);
        if (rc != 0) {
            if (rc == -DER_KEY2BIG) {
                buflen = kds.kd_key_len;
                buf = realloc(buf, buflen);
                if(!buf) {
                	D_ERROR("native error in list_dkey(): failed to allocate memory");
                    if (result) free(result);
                    return NULL;
                }
                d_iov_set(&sg_iov, buf, buflen);
                continue;
            }
            else break;
        }
        if (nr == 1) {
            // if key is returned
            // exactly 1 for each file because ',' and \0
            if (acc + kds.kd_key_len + 1 >= size) {
                size += STR_BUFFER_LEN;
                result = realloc(result, size);
                if (!result) {
                	D_ERROR("native error in list_dkey(): failed to allocate memory");
                    if (buf) free(buf);
                    return NULL;
                }
            }
            if (result[0]) result[acc++] = ',';
            memcpy(result + acc, buf, kds.kd_key_len);
            acc += kds.kd_key_len;
        }
    }
	if (rc) {
		D_ERROR("daos native error in list_dkey(): failed with %d", rc);
	    return NULL;
	}
    result[acc] = '\0';
	free(buf);
	jstring dkeys = (*env)->NewStringUTF(env, result);
	free(result);
	return dkeys;
}

JNIEXPORT jstring JNICALL Java_com_intel_daos_DaosJNI_daosObjectListAkey
  (JNIEnv *env, jobject obj, jlong oh, jstring dkey) {
	daos_handle_t c_oh;
	memcpy(&c_oh, &oh, sizeof(daos_handle_t));
	daos_anchor_t anchor = {0};
	daos_key_desc_t kds = {0};
    const char *dkey_tmp = (*env)->GetStringUTFChars(env, dkey, NULL);
    char dkey_str[strlen(dkey_tmp)+1];
    strcpy(dkey_str, dkey_tmp);
	(*env)->ReleaseStringUTFChars(env, dkey, dkey_tmp);
	int buflen = STR_BUFFER_LEN, size = buflen, acc = 0, rc;
	char *buf = malloc(buflen);
	char *result = malloc(size);
    result[0] = '\0';
    buf[0] = '\0';
	d_iov_t sg_iov = {0}, c_dkey = {0};
    d_sg_list_t sgl = {
        .sg_nr = 1,
        .sg_nr_out = 0,
        .sg_iovs = &sg_iov
    };
	d_iov_set(&sg_iov, buf, buflen);
	d_iov_set(&c_dkey, dkey_str, strlen(dkey_str));
    for(int nr = 1; !daos_anchor_is_eof(&anchor); nr = 1) {
        rc = daos_obj_list_akey(c_oh, DAOS_TX_NONE, &c_dkey, &nr, &kds, &sgl, &anchor, NULL);
        if (rc < 0) {
            if (rc == -DER_KEY2BIG) {
                buflen = kds.kd_key_len + 1;
                buf = realloc(buf, buflen);
                if(!buf) {
                	D_ERROR("native error in list_dkey(): failed to allocate memory");
                    if (result) free(result);
                    return NULL;
                }
                d_iov_set(&sg_iov, buf, buflen);
                continue;
            }
            else break;
        }
        if (nr == 1) {
            // if key is returned
            // exactly 1 for each file because ',' and \0
            if (acc + kds.kd_key_len + 1 >= size) {
                size += STR_BUFFER_LEN;
                result = realloc(result, size);
                if (!result) {
                	D_ERROR("native error in list_dkey(): failed to allocate memory");
                    if (buf) free(buf);
                    return NULL;
                }
            }
            if (result[0]) result[acc++] = ',';
            memcpy(result + acc, buf, kds.kd_key_len);
            acc += kds.kd_key_len;
        }
    }
	if (rc) {
		D_ERROR("daos native error in list_dkey(): failed with rc = %d", rc);
	    return NULL;
	}
    result[acc] = '\0';
	free(buf);
	jstring akeys = (*env)->NewStringUTF(env, result);
	free(result);
	return akeys;
}

JNIEXPORT jint JNICALL Java_com_intel_daos_DaosJNI_daosObjListRecx
  (JNIEnv *env, jobject obj, jlong oh, jstring dkey, jstring akey, jboolean incr_order) {
    daos_handle_t c_oh;
  	memcpy(&c_oh, &oh, sizeof(daos_handle_t));
  	daos_anchor_t anchor = {0};
    const char *dkey_tmp = (*env)->GetStringUTFChars(env, dkey, NULL);
	const char *akey_tmp = (*env)->GetStringUTFChars(env, akey, NULL);
	char dkey_str[strlen(dkey_tmp)+1];
	char akey_str[strlen(akey_tmp)+1];
	strcpy(dkey_str, dkey_tmp);
	strcpy(akey_str, akey_tmp);
	(*env)->ReleaseStringUTFChars(env, dkey, dkey_tmp);
	(*env)->ReleaseStringUTFChars(env, akey, akey_tmp);
    daos_key_t c_dkey, c_akey;
	d_iov_set(&c_dkey, dkey_str, strlen(dkey_str));
	d_iov_set(&c_akey, akey_str, strlen(akey_str));
    daos_size_t size = {0};
    uint32_t nr = 10;
    daos_recx_t recxs[10];
    daos_epoch_range_t eprs[10];
    int rc = daos_obj_list_recx(c_oh, DAOS_TX_NONE, &c_dkey, &c_akey, &size, &nr, recxs, eprs, &anchor, incr_order, NULL);
    printf("There are %d recx(s) with record size %d\n", nr, size);
    for (int i = 0; i < nr; ++i) {
        printf("\trecx %d: index %d number %d\n", i, recxs[i].rx_idx, recxs[i].rx_nr);
    }
    return rc;
}
