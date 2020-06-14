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
    int type;
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
    uint32_t len;
    int idx = 0;
    int i;
    int rc;


    memcpy(&oh, &objectHandle, sizeof(oh));
    for (i = 0; i < nbrOfDkeys; i++) {
        memcpy(&len, buffer + idx, 4);
        idx += 4;
        dkeys[i].iov_buf = buffer + idx;
        dkeys[i].iov_buf_len = len;
        dkeys[i].iov_len = len;
        idx += len;
    }
    daos_obj_punch_dkeys(oh, DAOS_TX_NONE, flags, (unsigned int)nbrOfDkeys,
                        dkeys, null);

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
    daos_key_t *dkey = keys[0];
    daos_key_t *akeys = keys + sizeof(daos_key_t);
    char *buffer = (char *)bufferAddress;
    uint32_t len;
    int idx = 0;
    int i;
    int rc;

    memcpy(&oh, &objectHandle, sizeof(oh));
    for (i = 0; i < nbrOfAkeys; i++) {
        memcpy(&len, buffer + idx, 4);
        idx += 4;
        dkeys[i].iov_buf = buffer + idx;
        dkeys[i].iov_buf_len = len;
        dkeys[i].iov_len = len;
        idx += len;
    }
    daos_obj_punch_akeys(oh, DAOS_TX_NONE, flags, dkey,
                        (unsigned int)nbrOfAkeys, akeys, NULL);

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
    daos_iod_t *iods = (daos_iod_t *)calloc(nbrOfAkeys, sizeof(daos_iod_t));
    d_sg_list_t *sgls = (d_sg_list_t *)calloc(nbrOfAkeys, sizeof(d_sg_list_t));
    int rc;

    // TODO: initialize iods and sgls
    memcpy(&oh, &objectHandle, sizeof(oh));
    rc = daos_obj_fetch(oh, DAOS_TX_NONE, flags, &dkey, nbrOfAkeys, iods, sgls,
                        NULL, NULL);

    if (rc) {
        char *msg = "Failed to fetch DAOS object";

        throw_exception_const_msg_object(env, msg, rc);
    }
    if (iods) {
        free(iods);
    }
    if (sgls) {
        fetch(sgls);
    }
}

JNIEXPORT void JNICALL
Java_io_daos_obj_DaosObjClient_updateObject(JNIEnv *env, jobject clientObject,
jlong objectHandle, jlong flags, jint nbrOfAkeys, jlong descBufAddress,
        jlong dataBufAddress)
{
    daos_handle_t oh;
    daos_key_t dkey;
    daos_iod_t *iods = (daos_iod_t *)calloc(nbrOfAkeys, sizeof(daos_iod_t));
    d_sg_list_t *sgls = (d_sg_list_t *)calloc(nbrOfAkeys, sizeof(d_sg_list_t));
    int rc;

    // TODO: initialize iods and sgls
    memcpy(&oh, &objectHandle, sizeof(oh));
    rc = daos_obj_update(oh, DAOS_TX_NONE, flags, &dkey, nbrOfAkeys, iods, sgls,
                        NULL, NULL);

    if (rc) {
        char *msg = "Failed to update DAOS object";

        throw_exception_const_msg_object(env, msg, rc);
    }
    if (iods) {
        free(iods);
    }
    if (sgls) {
        fetch(sgls);
    }
}

JNIEXPORT void JNICALL
Java_io_daos_obj_DaosObjClient_listObjectDkeys(JNIEnv *env,
        jobject clientObject, jlong objectHandle, jlong descBufAddress,
        jlong keyBufAddress, jlong anchorBufAddress, jint nbrOfDesc)
{
    daos_handle_t oh;
    daos_anchor_t anchor;
    daos_key_desc_t *kds = (daos_key_desc_t *)calloc(nbrOfDesc,
                                sizeof(daos_iod_t));
    d_sg_list_t sgl;
    int rc;
    unsigned int nbr = nbrOfDesc;
    int total_nbr;

    // TODO: initialize anchor, kds and sgl
    memcpy(&oh, &objectHandle, sizeof(oh));
    while (1) {
        rc = daos_obj_list_dkey(oh, DAOS_TX_NONE, &nbr, kds, &sgl, &anchor,
                            NULL);

        if (rc) {
            char *msg = "Failed to list DAOS object dkeys";

            throw_exception_const_msg_object(env, msg, rc);
            break;
        }
        total_nbr += nbr;
        if (total_nbr >= nbrOfDesc || daos_anchor_is_eof(&anchor)) {
            break;
        }
        // TODO: adjust sgl and kds
    }
    if (kds) {
        free(iods);
    }
}

JNIEXPORT void JNICALL
Java_io_daos_obj_DaosObjClient_listObjectAkeys(JNIEnv *env,
        jobject objectClient, jlong objectHandle, jlong descBufAddress,
        jlong keyBufAddress, jlong anchorBufAddress, jint nbrOfDesc)
{
    daos_handle_t oh;
    daos_key_t dkey;
    daos_anchor_t anchor;
    daos_key_desc_t *kds = (daos_key_desc_t *)calloc(nbrOfDesc,
                                sizeof(daos_iod_t));
    d_sg_list_t sgl;
    int rc;
    unsigned int nbr = nbrOfDesc;
    int total_nbr;

    // TODO: initialize dkey, anchor, kds and sgl
    memcpy(&oh, &objectHandle, sizeof(oh));
    while (1) {
        rc = daos_obj_list_akey(oh, DAOS_TX_NONE, &dkey, &nbr, kds, &sgl,
                            &anchor, NULL);

        if (rc) {
            char *msg = "Failed to list DAOS object akeys";

            throw_exception_const_msg_object(env, msg, rc);
            break;
        }
        total_nbr += nbr;
        if (total_nbr >= nbrOfDesc || daos_anchor_is_eof(&anchor)) {
            break;
        }
        // TODO: adjust sgl and kds
    }
    if (kds) {
        free(iods);
    }
}
