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
#include <daos.h>
#include <daos_obj.h>
#include <daos_jni_common.h>

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

    printf("oclass: %s\n", oclass_name);
    type = daos_oclass_name2id(oclass_name);
    printf("type: %d\n", type);
    parse_object_id(buffer, &oid);
    printf("eh: %ld\n", oid.hi);
    printf("el: %ld\n", oid.lo);
    daos_obj_generate_id(&oid, feats, OC_SX, args);
    printf("aeh: %ld\n", oid.hi);
    printf("ael: %ld\n", oid.lo);
    memcpy(buffer, &oid.hi, 8);
    memcpy(buffer+8, &oid.lo, 8);

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
    printf("h: %ld\n", oid.hi);
    printf("l: %ld\n", oid.lo);
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