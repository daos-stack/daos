/*
 * (C) Copyright 2018-2019 Intel Corporation.
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

#include "io_daos_dfs_DaosFsClient.h"
#include "DaosObjectAttribute.pb-c.h"
#include <daos.h>
#include <daos_obj.h>
#include <daos_jni_common.h>
#include <daos_types.h>

static inline void parse_object_id(char *buffer, daos_obj_id_t *oid)
{
    memcpy(&oid->hi, buffer, 8);
    memcpy(&oid->lo, buffer+8, 8);
}

JNIEXPORT void JNICALL
Java_io_daos_obj_DaosObjClient_encodeObjectId(JNIEnv *env, jclass clientClass,
        jlong oidBufferAddress, jint feats, jstring objectClass, jint args)
{
    daos_obj_id_t oid;
    uint16_t type;
    const char *oclass_name = (*env)->GetStringUTFChars(env, objectClass, NULL);
    char *buffer = (char *)oidBufferAddress;

    type = daos_oclass_name2id(oclass_name);
    if (!type) {
        char *tmp = "unsupported object class, %s";
        char *msg = (char *)malloc(strlen(tmp) + strlen(oclass_name));

        sprintf(msg, tmp, oclass_name);
        throw_exception_object(env, msg, CUSTOM_ERR6);
        goto out;
    }
    parse_object_id(buffer, &oid);
    daos_obj_generate_id(&oid, feats, type, args);
    memcpy(buffer, &oid.hi, 8);
    memcpy(buffer+8, &oid.lo, 8);

out:
    (*env)->ReleaseStringUTFChars(env, objectClass, oclass_name);
}

JNIEXPORT jlong JNICALL
Java_io_daos_obj_DaosObjClient_openObject(JNIEnv *env, jclass clientClass,
        jlong contHandle, jlong oidBufferAddress, jint mode)
{
    daos_obj_id_t oid;
    daos_handle_t coh;
    daos_handle_t oh;
    char *buffer = (char *)oidBufferAddress;
    jlong ret;
    int rc;

    memcpy(&coh, &contHandle, sizeof(coh));
    parse_object_id(buffer, &oid);
    rc = daos_obj_open(coh, oid, (unsigned int)mode, &oh, NULL);
    if (rc) {
        char *tmp = "Failed to open DAOS object with mode (%d)";
        char *msg = (char *)malloc(strlen(tmp) + 10);

        sprintf(msg, tmp, mode);
        throw_exception_object(env, msg, rc);
        return -1;
    }
    memcpy(&ret, &oh, sizeof(oh));
    return ret;
}

JNIEXPORT void JNICALL
Java_io_daos_obj_DaosObjClient_closeObject(JNIEnv *env, jclass clientClass,
        jlong objectHandle)
{
    daos_handle_t oh;
    int rc;

    memcpy(&oh, &objectHandle, sizeof(oh));
    rc = daos_obj_close(oh, NULL);
    if (rc) {
        char *msg = "Failed to close DAOS object";

        throw_exception_const_msg_object(env, msg, rc);
    }
}

JNIEXPORT void JNICALL
Java_io_daos_obj_DaosObjClient_punchObject(JNIEnv *env, jobject clientObject,
        jlong objectHandle, jlong flags)
{
    daos_handle_t oh;
    int rc;

    memcpy(&oh, &objectHandle, sizeof(oh));
    rc = daos_obj_punch(oh, DAOS_TX_NONE, flags, NULL);
    if (rc) {
        char *msg = "Failed to punch DAOS object";

        throw_exception_const_msg_object(env, msg, rc);
    }
}

JNIEXPORT void JNICALL
Java_io_daos_obj_DaosObjClient_punchObjectDkeys(JNIEnv *env,
        jobject clientObject, jlong objectHandle, jlong flags, jint nbrOfDkeys,
        jlong bufferAddress, jint dataLen)
{
    daos_handle_t oh;
    daos_key_t *dkeys = (daos_key_t *)calloc(nbrOfDkeys, sizeof(daos_key_t));
    char *buffer = (char *)bufferAddress;
    uint16_t len;
    int i;
    int rc;

    memcpy(&oh, &objectHandle, sizeof(oh));
    for (i = 0; i < nbrOfDkeys; i++) {
        memcpy(&len, buffer, 2);
        buffer += 2;
        d_iov_set(&dkeys[i], buffer, len);
        buffer += len;
    }
    rc = daos_obj_punch_dkeys(oh, DAOS_TX_NONE, flags, (unsigned int)nbrOfDkeys,
                        dkeys, NULL);

    if (rc) {
        char *msg = "Failed to punch DAOS object dkeys";

        throw_exception_const_msg_object(env, msg, rc);
        goto out;
    }

out:
    if (dkeys) {
        free(dkeys);
    }
}

JNIEXPORT void JNICALL
Java_io_daos_obj_DaosObjClient_punchObjectAkeys(JNIEnv *env,
        jobject clientObject, jlong objectHandle, jlong flags, jint nbrOfAkeys,
        jlong bufferAddress, jint dataLen)
{
    daos_handle_t oh;
    daos_key_t *keys = (daos_key_t *)calloc(nbrOfAkeys + 1, sizeof(daos_key_t));
    daos_key_t *dkey = &keys[0];
    char *buffer = (char *)bufferAddress;
    uint16_t len;
    int i;
    int rc;

    memcpy(&oh, &objectHandle, sizeof(oh));
    for (i = 0; i < nbrOfAkeys + 1; i++) {
        memcpy(&len, buffer, 2);
        buffer += 2;
        d_iov_set(&keys[i], buffer, len);
        buffer += len;
    }
    rc = daos_obj_punch_akeys(oh, DAOS_TX_NONE, flags, dkey,
                        (unsigned int)nbrOfAkeys, &keys[1], NULL);

    if (rc) {
        char *msg = "Failed to punch DAOS object akeys";

        throw_exception_const_msg_object(env, msg, rc);
        goto out;
    }

out:
    if (keys) {
        free(keys);
    }
}

JNIEXPORT jbyteArray JNICALL
Java_io_daos_obj_DaosObjClient_queryObjectAttribute(JNIEnv *env,
        jobject clientObject, jlong objectHandle)
{
    daos_handle_t oh;
    struct daos_obj_attr attr;
    d_rank_list_t ranks;
    int rc;

    memcpy(&oh, &objectHandle, sizeof(oh));
    rc = daos_obj_query(oh, DAOS_TX_NONE, &attr, &ranks, NULL);

    if (rc) {
        char *msg = "Failed to query DAOS object attribute";

        throw_exception_const_msg_object(env, msg, rc);
    }
    // TODO: convert and serialize attribute
    return NULL;
}

JNIEXPORT void JNICALL
Java_io_daos_obj_DaosObjClient_fetchObject(JNIEnv *env, jobject clientObject,
        jlong objectHandle, jlong flags, jint nbrOfAkeys, jlong descBufAddress,
        jlong dataBufAddress)
{
    daos_handle_t oh;
    daos_key_t dkey;
    char *desc_buffer = (char *)descBufAddress;
    char *data_buffer = (char *)dataBufAddress;
    char *data_buffer_cur;
    daos_iod_t *iods = (daos_iod_t *)calloc(nbrOfAkeys, sizeof(daos_iod_t));
    d_sg_list_t *sgls = (d_sg_list_t *)calloc(nbrOfAkeys, sizeof(d_sg_list_t));
    daos_recx_t *recxs = (daos_recx_t *)calloc(nbrOfAkeys, sizeof(daos_recx_t));
    d_iov_t *iovs = (d_iov_t *)calloc(nbrOfAkeys, sizeof(d_iov_t));
    daos_iod_t *iod;
    uint8_t b;
    uint16_t len;
    uint32_t value;
    int dataBufIdx = 0;
    int i;
    int rc;

    memcpy(&oh, &objectHandle, sizeof(oh));
    memcpy(&len, desc_buffer, 2);
    desc_buffer += 2;
    d_iov_set(&dkey, desc_buffer, len);
    desc_buffer += len;
    for (i = 0; i < nbrOfAkeys; i++) {
        // iod
        iod = &iods[i];
        memcpy(&len, desc_buffer, 2);
        desc_buffer += 2;
        d_iov_set(&iod->iod_name, desc_buffer, len);
        desc_buffer += len;
        memcpy(&b, desc_buffer, 1);
        iod->iod_type = (daos_iod_type_t)b;
        desc_buffer += 1;
        memcpy(&value, desc_buffer, 4);
        iod->iod_size = (uint64_t)value;
        iod->iod_nr = 1;
        desc_buffer += 4;
        if (iod->iod_type == DAOS_IOD_ARRAY) {
            memcpy(&value, desc_buffer, 4);
            recxs[i].rx_idx = (uint64_t)value;
            desc_buffer += 4;
            memcpy(&value, desc_buffer, 4);
            recxs[i].rx_nr = (uint64_t)value;
            desc_buffer += 4;
            iod->iod_recxs = &recxs[i];
        }
        // sgl
        memcpy(&value, desc_buffer, 4);
        desc_buffer += 4;
        dataBufIdx = value;
        data_buffer_cur = data_buffer + dataBufIdx;
        memcpy(&value, data_buffer_cur, 4);
        data_buffer_cur += 4;
        d_iov_set(&iovs[i], data_buffer_cur, value);
        sgls[i].sg_iovs = &iovs[i];
        sgls[i].sg_nr = 1;
        sgls[i].sg_nr_out = 0;
    }
    rc = daos_obj_fetch(oh, DAOS_TX_NONE, flags, &dkey,
                        (unsigned int)nbrOfAkeys, iods, sgls,
                        NULL, NULL);
    if (rc) {
        char *msg = "Failed to fetch DAOS object";

        throw_exception_const_msg_object(env, msg, rc);
    }
    // actual data size and actual record size
    for (i = 0; i < nbrOfAkeys; i++) {
        value = sgls[i].sg_nr_out == 0 ? 0 : sgls[i].sg_iovs->iov_len;
        memcpy(desc_buffer, &value, 4);
        desc_buffer += 4;
        value = iods[i].iod_size;
        memcpy(desc_buffer, &value, 4);
        desc_buffer += 4;
    }
    if (iods) {
        free(iods);
    }
    if (sgls) {
        free(sgls);
    }
    if (recxs) {
        free(recxs);
    }
    if (iovs) {
        free(iovs);
    }
}

JNIEXPORT void JNICALL
Java_io_daos_obj_DaosObjClient_updateObject(JNIEnv *env, jobject clientObject,
        jlong objectHandle, jlong flags, jint nbrOfAkeys, jlong descBufAddress,
        jlong dataBufAddress)
{
    daos_handle_t oh;
    daos_key_t dkey;
    char *desc_buffer = (char *)descBufAddress;
    char *data_buffer = (char *)dataBufAddress;
    char *data_buffer_cur;
    daos_iod_t *iods = (daos_iod_t *)calloc(nbrOfAkeys, sizeof(daos_iod_t));
    d_sg_list_t *sgls = (d_sg_list_t *)calloc(nbrOfAkeys, sizeof(d_sg_list_t));
    daos_recx_t *recxs = (daos_recx_t *)calloc(nbrOfAkeys, sizeof(daos_recx_t));
    d_iov_t *iovs = (d_iov_t *)calloc(nbrOfAkeys, sizeof(d_iov_t));
    daos_iod_t *iod;
    uint8_t b;
    uint16_t len;
    uint32_t value;
    int dataBufIdx = 0;
    int i;
    int rc;

    memcpy(&oh, &objectHandle, sizeof(oh));
    memcpy(&len, desc_buffer, 2);
    desc_buffer += 2;
    d_iov_set(&dkey, desc_buffer, len);
    desc_buffer += len;
    for (i = 0; i < nbrOfAkeys; i++) {
        // iod
        iod = &iods[i];
        memcpy(&len, desc_buffer, 2);
        desc_buffer += 2;
        d_iov_set(&iod->iod_name, desc_buffer, len);
        desc_buffer += len;
        memcpy(&b, desc_buffer, 1);
        iod->iod_type = (daos_iod_type_t)b;
        desc_buffer += 1;
        memcpy(&value, desc_buffer, 4);
        iod->iod_size = (uint64_t)value;
        iod->iod_nr = 1;
        desc_buffer += 4;
        if (iod->iod_type == DAOS_IOD_ARRAY) {
            memcpy(&value, desc_buffer, 4);
            recxs[i].rx_idx = (uint64_t)value;
            desc_buffer += 4;
            memcpy(&value, desc_buffer, 4);
            recxs[i].rx_nr = (uint64_t)value;
            desc_buffer += 4;
            iod->iod_recxs = &recxs[i];
        }
        // sgl
        memcpy(&value, desc_buffer, 4);
        desc_buffer += 4;
        dataBufIdx = value;
        data_buffer_cur = data_buffer + dataBufIdx;
        memcpy(&value, data_buffer_cur, 4);
        data_buffer_cur += 4;
        d_iov_set(&iovs[i], data_buffer_cur, value);
        sgls[i].sg_iovs = &iovs[i];
        sgls[i].sg_nr = 1;
        sgls[i].sg_nr_out = 0;
    }
    rc = daos_obj_update(oh, DAOS_TX_NONE, flags, &dkey,
                         (unsigned int)nbrOfAkeys, iods, sgls,
                         NULL);
    if (rc) {
        char *msg = "Failed to update DAOS object";

        throw_exception_const_msg_object(env, msg, rc);
        goto out;
    }

out:
    if (iods) {
        free(iods);
    }
    if (sgls) {
        free(sgls);
    }
    if (recxs) {
        free(recxs);
    }
    if (iovs) {
        free(iovs);
    }
}

static inline void copy_kd(char *desc_buffer, daos_key_desc_t *kd)
{
    memcpy(desc_buffer, &kd->kd_key_len, 8);
    desc_buffer += 8;
    memcpy(desc_buffer, &kd->kd_val_type, 4);
    desc_buffer += 4;
    memcpy(desc_buffer, &kd->kd_csum_type, 2);
    desc_buffer += 2;
    memcpy(desc_buffer, &kd->kd_csum_len, 2);
    desc_buffer += 2;
}

static inline int list_keys(jlong objectHandle, char *desc_buffer_head,
        char *key_buffer, jint keyBufLen, char *anchor_buffer_head,
        jint nbrOfDesc, daos_key_t *dkey, int dkey_len)
{
    daos_handle_t oh;
    daos_anchor_t anchor;
    char *desc_buffer = desc_buffer_head + 4;
    char *anchor_buffer = anchor_buffer_head + 1;
    daos_key_desc_t *kds = (daos_key_desc_t *)calloc(nbrOfDesc,
                                sizeof(daos_key_desc_t));
    d_sg_list_t sgl;
    d_iov_t iov;
    int rc = 0;
    uint8_t quit_code = KEY_LIST_CODE_ANCHOR_END;
    int i;
    int idx = 0;
    int key_buffer_idx = 0;
    int desc_buffer_idx = 0;
    int remaining = nbrOfDesc;
    unsigned int nbr;

    if (dkey != NULL) {
        desc_buffer += dkey_len;
    }
    memcpy(&oh, &objectHandle, sizeof(oh));
    memset(&anchor, 0, sizeof(anchor));
    if ((int)anchor_buffer_head[0] != 0) { // anchor in use
        memcpy(&anchor.da_type, anchor_buffer, 2);
        memcpy(&anchor.da_shard, anchor_buffer + 2, 2);
        memcpy(&anchor.da_flags, anchor_buffer + 4, 4);
        memcpy(&anchor.da_buf, anchor_buffer + 8, DAOS_ANCHOR_BUF_MAX);
    }
    sgl.sg_nr = 1;
    sgl.sg_nr_out = 0;
    sgl.sg_iovs = &iov;
    d_iov_set(&iov, key_buffer, keyBufLen);
    while (!daos_anchor_is_eof(&anchor)) {
        nbr = remaining;
        if (dkey == NULL) {
            rc = daos_obj_list_dkey(oh, DAOS_TX_NONE, &nbr, &kds[idx], &sgl,
                            &anchor, NULL);
        } else {
            rc = daos_obj_list_akey(oh, DAOS_TX_NONE, dkey, &nbr, &kds[idx],
                                    &sgl, &anchor, NULL);
        }
        if (rc) {
            if (rc == -DER_KEY2BIG) {
                copy_kd(desc_buffer, &kds[idx]);
                idx += 1;
                quit_code = KEY_LIST_CODE_KEY2BIG;
                rc = 0;
                break;
            }
            goto out;
        }
        if (nbr == 0) {
            continue;
        }
        idx += nbr;
        remaining -= nbr;
        // copy to kds and adjust sgl iov
        for (i = idx - nbr; i < idx; i++) {
            copy_kd(desc_buffer, &kds[i]);
            desc_buffer += 16;
            key_buffer_idx += (kds[i].kd_key_len + kds[i].kd_csum_len);
        }
        if (remaining <= 0) {
            quit_code = KEY_LIST_CODE_REACH_LIMIT;
            break;
        }
        d_iov_set(&iov, key_buffer + key_buffer_idx, keyBufLen - key_buffer_idx);
    }
    // copy anchor back if necessary
    memcpy(anchor_buffer_head, &quit_code, 1);
    if (quit_code != KEY_LIST_CODE_ANCHOR_END) {
        memcpy(anchor_buffer, &anchor.da_type, 2);
        memcpy(anchor_buffer + 2, &anchor.da_shard, 2);
        memcpy(anchor_buffer + 4, &anchor.da_flags, 4);
        memcpy(anchor_buffer + 8, &anchor.da_buf, DAOS_ANCHOR_BUF_MAX);
    }

out:
    if (kds) {
        free(kds);
    }
    // set number of keys listed
    memcpy(desc_buffer_head, &idx, 4);
    return rc;
}

JNIEXPORT void JNICALL
Java_io_daos_obj_DaosObjClient_listObjectDkeys(JNIEnv *env,
        jobject clientObject, jlong objectHandle, jlong descBufAddress,
        jlong keyBufAddress, jint keyBufLen, jlong anchorBufAddress,
        jint nbrOfDesc)
{
    char *desc_buffer_head = (char *)descBufAddress;
    char *key_buffer = (char *)keyBufAddress;
    char *anchor_buffer_head = (char *)anchorBufAddress;
    int rc = list_keys(objectHandle, desc_buffer_head, key_buffer, keyBufLen,
                        anchor_buffer_head, nbrOfDesc, NULL, 0);

    if (rc) {
        char *tmp = "Failed to list DAOS object dkeys, kds index: %d";
        char *msg = (char *)malloc(strlen(tmp) + 10);
        int idx;

        memcpy(&idx, desc_buffer_head, 4);
        sprintf(msg, tmp, idx);
        throw_exception_object(env, msg, rc);
    }
}

JNIEXPORT void JNICALL
Java_io_daos_obj_DaosObjClient_listObjectAkeys(JNIEnv *env,
        jobject objectClient, jlong objectHandle, jlong descBufAddress,
        jlong keyBufAddress, jint keyBufLen, jlong anchorBufAddress,
        jint nbrOfDesc)
{
    char *desc_buffer_head = (char *)descBufAddress;
    char *desc_buffer = desc_buffer_head + 4;
    char *key_buffer = (char *)keyBufAddress;
    char *anchor_buffer_head = (char *)anchorBufAddress;
    daos_key_t dkey;
    uint16_t dkey_len;
    int rc;

    memcpy(&dkey_len, desc_buffer, 2);
    desc_buffer += 2;
    d_iov_set(&dkey, desc_buffer, dkey_len);
    rc = list_keys(objectHandle, desc_buffer_head, key_buffer, keyBufLen,
                        anchor_buffer_head, nbrOfDesc, &dkey, dkey_len + 2);

    if (rc) {
        char *tmp = "Failed to list DAOS object akeys, kds index: %d";
        char *msg = (char *)malloc(strlen(tmp) + 10);
        int idx;

        memcpy(&idx, desc_buffer_head, 4);
        sprintf(msg, tmp, idx);
        throw_exception_object(env, msg, rc);
    }
}

JNIEXPORT jint JNICALL
Java_io_daos_obj_DaosObjClient_getRecordSize(JNIEnv *env,
        jobject clientObject, jlong objectHandle, jlong bufferAddress)
{
    char *buffer = (char *)bufferAddress;
    daos_handle_t oh;
    daos_key_t dkey;
    daos_key_t akey;
    daos_anchor_t anchor;
    daos_recx_t recx;
    daos_epoch_range_t erange;
    uint16_t key_len;
    uint64_t size;
    uint32_t nbr = 1;
    int rc;

    memcpy(&oh, &objectHandle, sizeof(oh));
    memset(&anchor, 0, sizeof(anchor));
    memcpy(&key_len, buffer, 2);
    buffer += 2;
    d_iov_set(&dkey, buffer, key_len);
    buffer += key_len;
    memcpy(&key_len, buffer, 2);
    buffer += 2;
    d_iov_set(&akey, buffer, key_len);
    rc = daos_obj_list_recx(oh, DAOS_TX_NONE, &dkey, &akey, &size, &nbr, &recx,
                            &erange, &anchor, false, NULL);
    if (rc) {
        char *tmp = "Failed to get record size";

        throw_exception_const_msg_object(env, tmp, rc);
    }
    return (jint)size;
}
