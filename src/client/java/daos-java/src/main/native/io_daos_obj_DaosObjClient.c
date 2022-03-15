/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define _GNU_SOURCE

#include "io_daos_dfs_DaosFsClient.h"
#include "DaosObjectAttribute.pb-c.h"
#include <daos.h>
#include <daos_obj.h>
#include <daos_jni_common.h>
#include <daos_types.h>

static inline void
parse_object_id(char *buffer, daos_obj_id_t *oid)
{
	memcpy(&oid->hi, buffer, 8);
	memcpy(&oid->lo, buffer + 8, 8);
}

JNIEXPORT void JNICALL
Java_io_daos_obj_DaosObjClient_encodeObjectId(JNIEnv *env, jclass clientClass,
		jlong oidBufferAddress, jlong contHandle, jint objectType,
		jstring objectClass, jint hint, jint args)
{
	daos_obj_id_t oid;
	daos_handle_t coh;
	enum daos_otype_t otype = (enum daos_otype_t)objectType;
	uint32_t oclass;
	const char *oclass_name = (*env)->GetStringUTFChars(env, objectClass,
			NULL);
	char *buffer = (char *)oidBufferAddress;

	memcpy(&coh, &contHandle, sizeof(coh));
	oclass = daos_oclass_name2id(oclass_name);
	parse_object_id(buffer, &oid);
	daos_obj_generate_oid(coh, &oid, otype, oclass, hint, args);
	memcpy(buffer, &oid.hi, 8);
	memcpy(buffer + 8, &oid.lo, 8);

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
		char *msg = NULL;

		asprintf(&msg, tmp, mode);
		throw_obj(env, msg, rc);
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

		throw_const_obj(env,
				msg,
				rc);
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

		throw_const_obj(env,
				msg,
				rc);
	}
}

JNIEXPORT void JNICALL
Java_io_daos_obj_DaosObjClient_punchObjectDkeys(JNIEnv *env,
		jobject clientObject, jlong objectHandle,
		jlong flags, jint nbrOfDkeys,
		jlong bufferAddress, jint dataLen)
{
	daos_handle_t oh;
	daos_key_t *dkeys = (daos_key_t *)calloc(nbrOfDkeys,
		sizeof(daos_key_t));
	char *buffer = (char *)bufferAddress;
	uint16_t len;
	uint32_t totalLen = 0;
	int i;
	int rc;

	if (dkeys == NULL) {
		throw_const_obj(env, "memory allocation failed", rc);
		return;
	}
	memcpy(&oh, &objectHandle, sizeof(oh));
	for (i = 0; i < nbrOfDkeys; i++) {
		memcpy(&len, buffer, 2);
		buffer += 2;
		totalLen += (2 + len);
		if (totalLen > dataLen) {
			char *msg = NULL;

			asprintf(&msg, "length %d exceeds buffer capacity %d",
				 totalLen, dataLen);
			throw_obj(env, msg, CUSTOM_ERR7);
			goto out;
		}
		d_iov_set(&dkeys[i], buffer, len);
		buffer += len;
	}
	rc = daos_obj_punch_dkeys(oh, DAOS_TX_NONE, flags,
			(unsigned int)nbrOfDkeys, dkeys, NULL);

	if (rc) {
		throw_const_obj(env, "Failed to punch DAOS object dkeys",
				rc);
		goto out;
	}

out:
	if (dkeys) {
		free(dkeys);
	}
}

JNIEXPORT void JNICALL
Java_io_daos_obj_DaosObjClient_punchObjectAkeys(JNIEnv *env,
		jobject clientObject, jlong objectHandle,
		jlong flags, jint nbrOfAkeys,
		jlong bufferAddress, jint dataLen)
{
	daos_handle_t oh;
	daos_key_t *keys = (daos_key_t *)calloc(nbrOfAkeys + 1,
				sizeof(daos_key_t));
	daos_key_t *dkey = &keys[0];
	char *buffer = (char *)bufferAddress;
	uint16_t len;
	uint32_t totalLen = 0;
	int i;
	int rc;

	if (keys == NULL) {
		throw_const_obj(env, "memory allocation failed", rc);
		return;
	}
	memcpy(&oh, &objectHandle, sizeof(oh));
	for (i = 0; i < nbrOfAkeys + 1; i++) {
		memcpy(&len, buffer, 2);
		buffer += 2;
		totalLen += (2 + len);
		if (totalLen > dataLen) {
			char *msg = NULL;

			asprintf(&msg, "length %d exceeds buffer capacity %d",
				 totalLen, dataLen);
			throw_obj(env, msg, CUSTOM_ERR7);
			goto out;
		}
		d_iov_set(&keys[i], buffer, len);
		buffer += len;
	}
	rc = daos_obj_punch_akeys(oh, DAOS_TX_NONE, flags, dkey,
			(unsigned int)nbrOfAkeys, &keys[1], NULL);

	if (rc) {
		throw_const_obj(env, "Failed to punch DAOS object akeys",
				rc);
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
	rc = daos_obj_query(oh, &attr, &ranks, NULL);

	if (rc) {
		char *msg = "Failed to query DAOS object attribute";

		throw_const_obj(env, msg, rc);
	}
	/* TODO: convert and serialize attribute */
	return NULL;
}

static inline int
decode_initial(data_desc_t *desc, char *desc_buffer)
{
	desc->iods = (daos_iod_t *)calloc(desc->nbrOfAkeys,
		sizeof(daos_iod_t));
	desc->sgls = (d_sg_list_t *)calloc(desc->nbrOfAkeys,
		sizeof(d_sg_list_t));
	desc->recxs = (daos_recx_t *)calloc(desc->nbrOfAkeys,
		sizeof(daos_recx_t));
	desc->iovs = (d_iov_t *)calloc(desc->nbrOfAkeys,
		sizeof(d_iov_t));
	daos_iod_t *iod;
	uint16_t len;
	uint16_t pad;
	uint32_t nbr_of_record;
	uint64_t value64;
	int i;

	if (desc->iods == NULL | desc->sgls == NULL
		| desc->recxs == NULL | desc->iovs == NULL) {
		return CUSTOM_ERR3;
	}

	for (i = 0; i < desc->nbrOfAkeys; i++) {
		/* iod */
		/* akey */
		iod = &desc->iods[i];
		memcpy(&len, desc_buffer, 2);
		desc_buffer += 2;
		d_iov_set(&iod->iod_name, desc_buffer, len);
		desc_buffer += len;
		if (desc->reusable) {
			pad = desc->maxKeyLen - len;
			if (pad > 0) {
				desc_buffer += pad;
			}
		}
		iod->iod_type = desc->iod_type;
		iod->iod_size = (uint64_t)desc->record_size;
		iod->iod_nr = 1;
		if (iod->iod_type == DAOS_IOD_ARRAY) {
			/* offset */
			memcpy(&value64, desc_buffer, 8);
			desc->recxs[i].rx_idx = value64;
			desc_buffer += 8;
			/* length */
			memcpy(&nbr_of_record, desc_buffer, 4);
			desc->recxs[i].rx_nr = (uint64_t)nbr_of_record;
			desc_buffer += 4;
			iod->iod_recxs = &desc->recxs[i];
		} else {
			nbr_of_record = 1;
		}

		/* sgl */
		memcpy(&value64, desc_buffer, 8);
		desc_buffer += 8;
		d_iov_set(&desc->iovs[i], (void *)value64,
			nbr_of_record * desc->record_size);
		desc->sgls[i].sg_iovs = &desc->iovs[i];
		desc->sgls[i].sg_nr = 1;
		desc->sgls[i].sg_nr_out = 0;
	}
	desc->ret_buf_address = (uint64_t)desc_buffer;
	return 0;
}

static inline void
decode_reused(data_desc_t *desc, char *desc_buffer,
		int actual_nbr_of_keys) {
	uint16_t len;
	uint16_t pad;
	uint32_t nbr_of_record;
	uint64_t value64;
	daos_iod_t *iod;
	int i;

	for (i = 0; i < actual_nbr_of_keys; i++) {
		/* iod */
		/* akey */
		iod = &desc->iods[i];
		memcpy(&len, desc_buffer, 2);
		desc_buffer += 2;
		d_iov_set(&iod->iod_name, desc_buffer, len);
		desc_buffer += len;
		pad = desc->maxKeyLen - len;
		if (pad > 0) {
			desc_buffer += pad;
		}
		if (desc->iod_type == DAOS_IOD_ARRAY) {
			/* offset */
			memcpy(&value64, desc_buffer, 8);
			desc->recxs[i].rx_idx = value64;
			desc_buffer += 8;
			/* length */
			memcpy(&nbr_of_record, desc_buffer, 4);
			desc->recxs[i].rx_nr = (uint64_t)nbr_of_record;
			desc_buffer += 4;
		} else {
			nbr_of_record = 1;
		}
		/* sgl */
		desc_buffer += 8;
		desc->iovs[i].iov_len = nbr_of_record * desc->record_size;
		desc->iovs[i].iov_buf_len = desc->iovs[i].iov_len;
		desc->sgls[i].sg_nr_out = 0;
	}
}

JNIEXPORT void JNICALL
Java_io_daos_obj_DaosObjClient_releaseDesc(JNIEnv *env, jclass clientClass,
		jlong descPtr)
{
	data_desc_t *desc = *(data_desc_t **)&descPtr;

	if (desc->iods) {
		free(desc->iods);
	}
	if (desc->sgls) {
		free(desc->sgls);
	}
	if (desc->recxs) {
		free(desc->recxs);
	}
	if (desc->iovs) {
		free(desc->iovs);
	}
	free(desc);
}

static void
release_simple_desc(data_desc_simple_t *desc)
{
	if (desc->iods) {
		free(desc->iods);
	}
	if (desc->sgls) {
		free(desc->sgls);
	}
	if (desc->recxs) {
		free(desc->recxs);
	}
	if (desc->iovs) {
		free(desc->iovs);
	}
	free(desc);
}

JNIEXPORT void JNICALL
Java_io_daos_obj_DaosObjClient_releaseDescSimple(JNIEnv *env,
		jclass clientClass, jlong descPtr)
{
	data_desc_simple_t *desc = *(data_desc_simple_t **)&descPtr;

	release_simple_desc(desc);
}

static inline int
init_desc(JNIEnv *env, data_desc_t **desc_addr, char *desc_buffer,
	  uint16_t maxKeyLen, uint8_t iod_type,
	  uint32_t record_size, int reusable,
	  int nbrOfAkeys, jlong endAddress)
{
	*desc_addr = (data_desc_t *)malloc(sizeof(data_desc_t));
	data_desc_t *desc = *desc_addr;
	int rc;

	if (desc == NULL) {
		throw_const_obj(env, "memory allocation failed",
				CUSTOM_ERR3);
		return 1;
	}
	desc->maxKeyLen = maxKeyLen;
	desc->iod_type = (daos_iod_type_t)iod_type;
	desc->record_size = record_size;
	desc->reusable = reusable;
	desc->nbrOfAkeys = nbrOfAkeys;
	rc = decode_initial(desc, desc_buffer);
	if (rc) {
		throw_const_obj(env, "failed to decode initial", rc);
		return 1;
	}
	if ((desc->ret_buf_address != endAddress) &&
	    (desc->ret_buf_address + 8 * nbrOfAkeys != endAddress)) {
		throw_const_obj(env, "failed to decode initial",
				CUSTOM_ERR7);
		return 1;
	}
	return 0;
}

static inline int
decode(JNIEnv *env, jlong objectHandle, jint nbrOfAkeys,
       jlong descBufAddress, jint descBufCap, daos_handle_t *oh,
       daos_key_t *dkey, int *nbr_of_akeys_with_data,
       data_desc_t **ret_desc)
{
	uint8_t iod_type;
	uint16_t len;
	uint16_t maxKeyLen;
	uint32_t record_size;
	uint64_t address = 0;
	char *desc_buffer = (char *)descBufAddress;
	data_desc_t *desc;
	int i;
	int rc;

	memcpy(oh, &objectHandle, sizeof(oh));
	/* address of data_desc_t */
	memcpy(&address, desc_buffer, 8);
	desc_buffer += 8;
	if (address == 0) {/* first time for reusable */
		memcpy(&len, desc_buffer, 2);
		maxKeyLen = len;
		desc_buffer += 2;
		memcpy(&len, desc_buffer, 2);
		*nbr_of_akeys_with_data = len;
		desc_buffer += 2;
		memcpy(&iod_type, desc_buffer, 1);
		desc_buffer += 1;
		memcpy(&record_size, desc_buffer, 4);
		desc_buffer += 4;
		if (*nbr_of_akeys_with_data > nbrOfAkeys) {
			char *tmp = "number of akeys %d in reused desc "
				"should be no larger than initial number of"
				" akeys %d";
			char *msg = NULL;

			asprintf(&msg, tmp, *nbr_of_akeys_with_data,
				 nbrOfAkeys);
			throw_obj(env, msg, 0);
			return 1;
		}
	} else if (address == -1) {/* not reusable */
		*nbr_of_akeys_with_data = nbrOfAkeys;
		memcpy(&iod_type, desc_buffer, 1);
		desc_buffer += 1;
		memcpy(&record_size, desc_buffer, 4);
		desc_buffer += 4;
	} else {/* reused */
		desc_buffer += 2; /* skip maxkeylen */
		memcpy(&len, desc_buffer, 2);
		*nbr_of_akeys_with_data = len;
		/* move 2 and skip type and record size */
		desc_buffer += 7;
	}
	/* dkey */
	memcpy(&len, desc_buffer, 2);
	desc_buffer += 2;
	d_iov_set(dkey, desc_buffer, len);
	desc_buffer += len;
	if ((uint64_t)desc_buffer - descBufAddress > descBufCap) {
		char *msg = NULL;

		asprintf(&msg, "length %ld exceeds buffer capacity %d",
			 (uint64_t)desc_buffer - descBufAddress, descBufCap);
		throw_obj(env, msg, CUSTOM_ERR7);
		return 1;
	}
	if (address == 0) {/* no desc yet, reusable */
		uint16_t pad = maxKeyLen - len;

		if (pad > 0) {
			desc_buffer += pad;
		}
		if (init_desc(env, &desc, desc_buffer, maxKeyLen, iod_type,
			      record_size, 1, nbrOfAkeys,
			      descBufAddress + descBufCap)) {
			return 1;
		}
		/* put address to the start of desc buffer */
		memcpy((char *)descBufAddress, &desc, 8);
	} else if (address == -1) {/* no desc yet, not reusable */
		if (init_desc(env, &desc, desc_buffer, -1, iod_type,
			      record_size, 0, nbrOfAkeys,
			      descBufAddress + descBufCap)) {
			return 1;
		}
	} else {
		desc = *(data_desc_t **)&address;
		uint16_t pad = desc->maxKeyLen - len;

		if (pad > 0) {
			desc_buffer += pad;
		}
		decode_reused(desc, desc_buffer, *nbr_of_akeys_with_data);
	}
	*ret_desc = desc;
	return 0;
}

JNIEXPORT void JNICALL
Java_io_daos_obj_DaosObjClient_fetchObject(JNIEnv *env, jobject clientObject,
		jlong objectHandle, jlong flags,
		jint nbrOfAkeys, jlong descBufAddress, jint descBufCap)
{
	daos_handle_t oh;
	daos_key_t dkey;
	int nbr_of_akeys_with_data;
	data_desc_t *desc = NULL;
	char *desc_buffer;
	uint32_t value;
	int i;
	int rc;

	if (decode(env, objectHandle, nbrOfAkeys, descBufAddress, descBufCap,
		   &oh, &dkey, &nbr_of_akeys_with_data, &desc)) {
		goto cleanup;
	}
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, flags, &dkey,
			(unsigned int)nbr_of_akeys_with_data, desc->iods,
			desc->sgls, NULL, NULL);
	if (rc) {
		throw_const_obj(env, "Failed to fetch DAOS object",
				rc);
		goto cleanup;
	}
	/* actual data size and actual record size */
	desc_buffer = (char *)desc->ret_buf_address;
	for (i = 0; i < nbr_of_akeys_with_data; i++) {
		value = desc->sgls[i].sg_nr_out == 0 ?
			0 : desc->sgls[i].sg_iovs->iov_len;
		memcpy(desc_buffer, &value, 4);
		desc_buffer += 4;
		value = desc->iods[i].iod_size;
		memcpy(desc_buffer, &value, 4);
		desc_buffer += 4;
	}

cleanup:
	if (desc && !desc->reusable) {
		if (desc->iods) {
			free(desc->iods);
		}
		if (desc->sgls) {
			free(desc->sgls);
		}
		if (desc->recxs) {
			free(desc->recxs);
		}
		if (desc->iovs) {
			free(desc->iovs);
		}
		free(desc);
	}
}

JNIEXPORT void JNICALL
Java_io_daos_obj_DaosObjClient_updateObject(JNIEnv *env, jobject clientObject,
		jlong objectHandle, jlong flags,
		jint nbrOfAkeys, jlong descBufAddress, jint descBufCap)
{
	daos_handle_t oh;
	daos_key_t dkey;
	int nbr_of_akeys_with_data;
	data_desc_t *desc = NULL;
	char *desc_buffer;
	uint32_t value;
	int i;
	int rc;

	if (decode(env, objectHandle, nbrOfAkeys, descBufAddress, descBufCap,
		   &oh, &dkey, &nbr_of_akeys_with_data, &desc)) {
		goto out;
	}
	rc = daos_obj_update(oh, DAOS_TX_NONE, flags, &dkey,
			(unsigned int)nbr_of_akeys_with_data, desc->iods,
			desc->sgls, NULL);
	if (rc) {
		throw_const_obj(env, "Failed to update DAOS object",
				rc);
		goto out;
	}

out:
	if (desc && !desc->reusable) {
		if (desc->iods) {
			free(desc->iods);
		}
		if (desc->sgls) {
			free(desc->sgls);
		}
		if (desc->recxs) {
			free(desc->recxs);
		}
		if (desc->iovs) {
			free(desc->iovs);
		}
		free(desc);
	}
}

static inline char *
decode_reused_simple(data_desc_simple_t *desc, char *desc_buffer)
{
	uint16_t len;
	uint32_t value;
	uint64_t value64;
	daos_iod_t *iod;
	int i;

	for (i = 0; i < desc->nbrOfRequests; i++) {
		/* iod */
		/* akey */
		iod = &desc->iods[i];
		memcpy(&len, desc_buffer, 2);
		desc_buffer += 2;
		iod->iod_name.iov_len = iod->iod_name.iov_buf_len = len;
		desc_buffer += desc->maxKeyLen;
		/* offset */
		memcpy(&value64, desc_buffer, 8);
		desc->recxs[i].rx_idx = value64;
		desc_buffer += 8;
		/* length */
		memcpy(&value, desc_buffer, 4);
		desc->recxs[i].rx_nr = (uint64_t)value;
		desc_buffer += 4;
		/* sgl */
		desc_buffer += 8;
		desc->iovs[i].iov_len = desc->iovs[i].iov_buf_len = value;
		desc->sgls[i].sg_nr_out = 0;
	}
	return desc_buffer;
}

static inline int
decode_simple(JNIEnv *env, jlong descBufAddress,
		data_desc_simple_t **ret_desc, jboolean async)
{
	uint16_t value16;
	uint16_t maxDkeyLen;
	uint64_t address = 0;
	char *desc_buffer = (char *)descBufAddress;
	data_desc_simple_t *desc;
	int i;
	int rc;
	/* address of data_desc_simple_t */
	memcpy(&address, desc_buffer, 8);

	desc = *(data_desc_simple_t **)&address;
	if (async) {
		/* skip address, maxKeyLen, nbrOfEntries, */
		/* eventqueue address */
		desc_buffer += 20;
		memcpy(&value16, desc_buffer, 2);
		desc_buffer += 2;
		desc->event = desc->eq->events[value16];
	} else {
		/* skip address, maxKeyLen, nbrOfEntries */
		desc_buffer += 12;
	}
	memcpy(&value16, desc_buffer, 2);
	desc_buffer += 2;
	/* dkey */
	desc->dkey.iov_len = desc->dkey.iov_buf_len = value16;
	desc_buffer += desc->maxKeyLen;
	/* akeys with requests */
	memcpy(&desc->nbrOfRequests, desc_buffer, 2);
	desc_buffer += 2;
	if (desc->nbrOfRequests > desc->nbrOfEntries) {
		char *tmp = "number of akeys %d in reused desc should be "
			"no larger than initial number of akeys %d";
		char *msg = NULL;

		asprintf(&msg, tmp, desc->nbrOfRequests, desc->nbrOfEntries);
		throw_obj(env, msg, 0);
		return -2;
	}
	desc_buffer = decode_reused_simple(desc, desc_buffer);
	*ret_desc = desc;
	return 0;
}

static int
allocate_simple_desc(char *descBufAddress, data_desc_simple_t *desc,
		bool async)
{
	uint16_t value16;
	uint16_t maxDkeyLen;
	uint64_t address = 0;
	char *desc_buffer = (char *)descBufAddress;
	int i;

	/* address of data_desc_simple_t */
	desc_buffer += 8;
	memcpy(&desc->maxKeyLen, desc_buffer, 2);
	desc_buffer += 2;
	memcpy(&desc->nbrOfEntries, desc_buffer, 2);
	desc_buffer += 2;
	if (async) {
		memcpy(&desc->eq, desc_buffer, 8);
		/* skip event idx (2) */
		desc_buffer += 10;
	} else {
		desc->event = NULL;
	}
	/* skip dkeylen and dkey */
	desc_buffer += 2;
	d_iov_set(&desc->dkey, desc_buffer, 0);
	desc_buffer += desc->maxKeyLen;
	/* skip akeys with request */
	desc_buffer += 2;
	/* entries */
	desc->iods = (daos_iod_t *)calloc(desc->nbrOfEntries,
			sizeof(daos_iod_t));
	desc->sgls = (d_sg_list_t *)calloc(desc->nbrOfEntries,
			sizeof(d_sg_list_t));
	desc->recxs = (daos_recx_t *)calloc(desc->nbrOfEntries,
			sizeof(daos_recx_t));
	desc->iovs = (d_iov_t *)calloc(desc->nbrOfEntries,
			sizeof(d_iov_t));
	daos_iod_t *iod;

	if (desc->iods == NULL | desc->sgls == NULL
		| desc->recxs == NULL | desc->iovs == NULL) {
		return CUSTOM_ERR3;
	}

	for (i = 0; i < desc->nbrOfEntries; i++) {
		/* iod */
		/* akey */
		iod = &desc->iods[i];
		/* skip akeylen */
		desc_buffer += 2;
		d_iov_set(&iod->iod_name, desc_buffer, 0);
		/* skip akey */
		desc_buffer += desc->maxKeyLen;
		iod->iod_type = DAOS_IOD_ARRAY;
		iod->iod_size = 1;
		iod->iod_nr = 1;
		/* skip offset and length */
		desc_buffer += 12;
		iod->iod_recxs = &desc->recxs[i];
		/* sgl */
		memcpy(&address, desc_buffer, 8);
		desc_buffer += 8;
		d_iov_set(&desc->iovs[i], (void *)address, 0);
		desc->sgls[i].sg_iovs = &desc->iovs[i];
		desc->sgls[i].sg_nr = 1;
		desc->sgls[i].sg_nr_out = 0;
	}
	desc->ret_buf_address = (uint64_t)desc_buffer;
	/* put address to the start of desc buffer */
	memcpy((char *)descBufAddress, &desc, 8);
	return 0;
}

static int
allocate_desc_update_async(char *descBuf, data_desc_upd_async_t *desc,
			   int reuse)
{
	char *desc_buffer = descBuf;
	uint16_t value16 = 0;

	desc->maxKeyLen = 0;
	if (reuse) {
		/* set max key len */
		memcpy(&desc->maxKeyLen, desc_buffer, 2);
		desc_buffer += 2;
	}
	desc->event = NULL;
	/* set dkey address */
	if (!reuse) {
		memcpy(&value16, desc_buffer, 2);
	}
	desc_buffer += 2;
	d_iov_set(&desc->dkey, desc_buffer, value16);
	desc_buffer += (desc->maxKeyLen == 0 ? value16 : desc->maxKeyLen);
	/* entries */
	desc->iods = (daos_iod_t *)calloc(1, sizeof(daos_iod_t));
	desc->sgls = (d_sg_list_t *)calloc(1, sizeof(d_sg_list_t));
	desc->recxs = (daos_recx_t *)calloc(1, sizeof(daos_recx_t));
	desc->iovs = (d_iov_t *)calloc(1, sizeof(d_iov_t));
	daos_iod_t *iod;

	if (desc->iods == NULL || desc->sgls == NULL
		|| desc->recxs == NULL || desc->iovs == NULL) {
		return CUSTOM_ERR3;
	}

	/* iod */
	/* akey */
	iod = &desc->iods[0];
	if (!reuse) {
		memcpy(&value16, desc_buffer, 2);
	}
	desc_buffer += 2;
	d_iov_set(&iod->iod_name, desc_buffer, value16);
	desc_buffer += (desc->maxKeyLen == 0 ? value16 : desc->maxKeyLen);
	iod->iod_type = DAOS_IOD_ARRAY;
	iod->iod_size = 1;
	iod->iod_nr = 1;
	iod->iod_recxs = &desc->recxs[0];
	/* sgl */
	d_iov_set(&desc->iovs[0], 0, 0);
	desc->sgls[0].sg_iovs = &desc->iovs[0];
	desc->sgls[0].sg_nr = 1;
	desc->sgls[0].sg_nr_out = 0;
	desc->ret_buf_address = (uint64_t)desc_buffer;
	/* put address to the start of desc buffer */
	if (reuse) {
		memcpy(descBuf - 8, &desc, 8);
	}
	return 0;
}

JNIEXPORT jlong JNICALL
Java_io_daos_obj_DaosObjClient_allocateSimDescGroup(
		JNIEnv *env, jclass clientClass, jlong memAddress, jint nbr)
{
	data_desc_simple_grp_t *grp = (data_desc_simple_grp_t *)malloc(
		sizeof(data_desc_simple_grp_t));
	char *buffer = (char *)memAddress;
	uint64_t address;
	int i;
	int rc;
	char *msg = "memory allocation failed";

	if (grp == NULL) {
		throw_const_obj(env, msg, CUSTOM_ERR3);
		return -1L;
	}
	grp->descs = (data_desc_simple_t **)malloc(
		nbr * sizeof(data_desc_simple_t *));
	if (grp->descs == NULL) {
		throw_const_obj(env, msg, CUSTOM_ERR3);
		return -1L;
	}
	grp->nbrOfDescs = nbr;
	for (i = 0; i < nbr; i++) {
		grp->descs[i] = (data_desc_simple_t *)malloc(
			sizeof(data_desc_simple_t));
		if (grp->descs[i] == NULL) {
			throw_const_obj(env, msg, CUSTOM_ERR3);
			return -1L;
		}
		memcpy(&address, buffer, 8);
		buffer += 8;
		rc = allocate_simple_desc((char *)address,
					  grp->descs[i], 1);
		if (rc) {
			throw_const_obj(env, "allocation failed",
					rc);
			return -1L;
		}
	}
	return *(jlong *)&grp;
}

JNIEXPORT void JNICALL
Java_io_daos_obj_DaosObjClient_allocateDescUpdAsync(
		JNIEnv *env, jclass clientClass,
		jlong memAddress)
{
	char *buffer = (char *)memAddress;
	char *msg = "memory allocation failed";
	int rc;
	data_desc_upd_async_t *desc = (data_desc_upd_async_t *)malloc(
		sizeof(data_desc_upd_async_t));

	if (desc == NULL) {
		throw_const_obj(env, msg, CUSTOM_ERR3);
		return;
	}
	/* skip native desc hdl */
	buffer += 8;
	rc = allocate_desc_update_async(buffer, desc, 1);
	if (rc) {
		throw_const_obj(env, "allocation failed",
				rc);
	}
}

JNIEXPORT jlong JNICALL
Java_io_daos_obj_DaosObjClient_releaseSimDescGroup(
		JNIEnv *env, jclass clientClass, jlong grpHdl)
{
	data_desc_simple_grp_t *grp = *(data_desc_simple_grp_t **)&grpHdl;
	int i;

	for (i = 0; i < grp->nbrOfDescs; i++) {
		release_simple_desc(grp->descs[i]);
	}
	free(grp->descs);
	free(grp);
}

static void
release_desc_upd_async(data_desc_upd_async_t *desc) {
	if (desc->iods) {
		free(desc->iods);
	}
	if (desc->sgls) {
		free(desc->sgls);
	}
	if (desc->recxs) {
		free(desc->recxs);
	}
	if (desc->iovs) {
		free(desc->iovs);
	}
	free(desc);
}

JNIEXPORT void JNICALL
Java_io_daos_obj_DaosObjClient_releaseDescUpdAsync(
		JNIEnv *env, jclass clientClass, jlong descPtr)
{
	data_desc_upd_async_t *desc = *(data_desc_upd_async_t **)&descPtr;

	release_desc_upd_async(desc);
}

JNIEXPORT void JNICALL
Java_io_daos_obj_DaosObjClient_allocateSimpleDesc(
		JNIEnv *env, jclass clientClass,
		jlong descBufAddress, jboolean async)
{
	char *desc_buffer = (char *)descBufAddress;
	data_desc_simple_t *desc = (data_desc_simple_t *)malloc(
		sizeof(data_desc_simple_t));
	int rc;

	if (desc == NULL) {
		throw_const_obj(env, "memory allocation failed",
				CUSTOM_ERR3);
		return;
	}
	rc = allocate_simple_desc(desc_buffer, desc, async);
	if (rc) {
		throw_const_obj(env, "allocation failed", rc);
	}
}

static int
update_ret_code(void *udata, daos_event_t *ev, int ret)
{
	data_desc_simple_t *desc = (data_desc_simple_t *)udata;
	char *desc_buffer = (char *)desc->ret_buf_address;

	memcpy(desc_buffer, &ret, 4);
	if (ev) {
		desc->event->status = 0;
	}
	return 0;
}

JNIEXPORT void JNICALL
Java_io_daos_obj_DaosObjClient_updateObjectSimple(
		JNIEnv *env, jobject clientObject,
		jlong objectHandle, jlong flags,
		jlong descBufAddress, jboolean async)
{
	daos_handle_t oh;
	data_desc_simple_t *desc = NULL;
	int rc;

	memcpy(&oh, &objectHandle, 8);
	if (decode_simple(env, descBufAddress, &desc, async)) {
		return;
	}
	if (async) {
		desc->event->status = EVENT_IN_USE;
		desc->event->event.ev_error = 0;
		rc = daos_event_register_comp_cb(&desc->event->event,
			update_ret_code, desc);
		if (rc) {
			char *msg = "Failed to register update callback";

			throw_const_obj(env,
					msg,
					rc);
			return;
		}
	}
	rc = daos_obj_update(oh, DAOS_TX_NONE, flags, &desc->dkey,
				desc->nbrOfRequests, desc->iods,
				desc->sgls,
				async ? &desc->event->event : NULL);
	if (rc) {
		throw_const_obj(env, "Failed to update DAOS object", rc);
	}
}

static int
update_ret_code_no_decode(void *udata, daos_event_t *ev, int ret)
{
	data_desc_upd_async_t *desc = (data_desc_upd_async_t *)udata;
	char *desc_buffer = (char *)desc->ret_buf_address;

	memcpy(desc_buffer, &ret, 4);
	if (ev) {
		ev->ev_error = 0;
	}
	/* free native desc if not reuse */
	if (desc->maxKeyLen == -1) {
		release_desc_upd_async(desc);
	}
	return 0;
}

JNIEXPORT void JNICALL
Java_io_daos_obj_DaosObjClient_updateObjNoDecode(JNIEnv *env,
		jobject clientObj, jlong objPtr,
		jlong descBufAddress, jlong eqWrapHdl,
		jshort eqId, jlong offset, jint len,
		jlong dataBufAddress)
{
	daos_handle_t oh;
	char *desc_buf = (char *)descBufAddress;
	char *data_buf = (char *)dataBufAddress;
	data_desc_upd_async_t *desc = NULL;
	uint64_t descPtr;
	uint16_t value16;
	daos_iod_t *iod;
	int rc;

	memcpy(&oh, &objPtr, 8);
	memcpy(&descPtr, desc_buf, 8);
	desc_buf += 8;
	if (descPtr != 0L) { /* reusable */
		/* skip maxlen */
		desc_buf += 2;
		desc = *(data_desc_upd_async_t **)&descPtr;
		/* dkey */
		memcpy(&value16, desc_buf, 2);
		desc_buf += 2;
		desc->dkey.iov_len = desc->dkey.iov_buf_len
				= value16;
		desc_buf += desc->maxKeyLen;
		/* akey */
		memcpy(&value16, desc_buf, 2);
		desc_buf += 2;
		iod = &desc->iods[0];
		iod->iod_name.iov_len =
			iod->iod_name.iov_buf_len = value16;
		desc_buf += desc->maxKeyLen;
	} else { /* not reusable */
		char *msg = "memory allocation failed";

		desc =	(data_desc_upd_async_t *)malloc(
			sizeof(data_desc_upd_async_t));

		if (desc == NULL) {
			throw_const_obj(env,
			msg,
			CUSTOM_ERR3);
			return;
		}
		rc = allocate_desc_update_async(desc_buf, desc, 0);
		if (rc) {
			throw_const_obj(env, "allocation failed",
					rc);
			return;
		}

	}
	/* event */
	event_queue_wrapper_t *eq = *(event_queue_wrapper_t **)
				&eqWrapHdl;

	desc->event = eq->events[eqId];
	/* offset */
	desc->recxs[0].rx_idx = offset;
	/* length */
	desc->recxs[0].rx_nr = (uint64_t)len;
	/* sgl */
	d_iov_set(&desc->iovs[0], data_buf, (uint64_t)len);
	desc->sgls[0].sg_nr_out = 0;
	rc = daos_event_register_comp_cb(&desc->event->event,
		update_ret_code_no_decode,
		desc);
	if (rc) {
		char *msg = "Failed to register update callback";

		throw_const_obj(env,
				msg,
				rc);
		return;
	}
	desc->event->status = EVENT_IN_USE;
	desc->event->event.ev_error = 0;
	rc = daos_obj_update(oh, DAOS_TX_NONE, 0L, &desc->dkey,
			1, desc->iods,
			desc->sgls, &desc->event->event);
	if (rc) {
		throw_const_obj(env,
				"Failed to update DAOS object",
				rc);
	}
}

static void
release_desc_async(data_desc_async_t *desc)
{
	if (desc == NULL) {
		return;
	}
	if (desc->iods) {
		free(desc->iods);
	}
	if (desc->sgls) {
		free(desc->sgls);
	}
	if (desc->recxs) {
		free(desc->recxs);
	}
	if (desc->iovs) {
		free(desc->iovs);
	}
	free(desc);
}

static inline int
decode_async(JNIEnv *env, jlong descBufAddress,
	     data_desc_async_t **ret_desc)
{
	uint16_t value16;
	uint64_t value64;
	uint32_t value;
	char *desc_buffer = (char *)descBufAddress;
	data_desc_async_t *desc;
	int i;
	int rc;

	desc = (data_desc_async_t *)malloc(sizeof(data_desc_async_t));
	if (desc == NULL) {
		return CUSTOM_ERR3;
	}
	memcpy(&value64, desc_buffer, 8);
	desc_buffer += 8;
	event_queue_wrapper_t *eq = *(event_queue_wrapper_t **)&value64;

	memcpy(&value16, desc_buffer, 2);
	desc_buffer += 2;
	desc->event = eq->events[value16];

	/* dkey */
	memcpy(&value16, desc_buffer, 2);
	desc_buffer += 2;
	d_iov_set(&desc->dkey, desc_buffer, value16);
	desc_buffer += value16;

	memcpy(&desc->nbrOfEntries, desc_buffer, 2);
	desc_buffer += 2;

	/* entries */
	desc->iods = (daos_iod_t *)calloc(desc->nbrOfEntries,
			sizeof(daos_iod_t));
	desc->sgls = (d_sg_list_t *)calloc(desc->nbrOfEntries,
			sizeof(d_sg_list_t));
	desc->recxs = (daos_recx_t *)calloc(desc->nbrOfEntries,
			sizeof(daos_recx_t));
	desc->iovs = (d_iov_t *)calloc(desc->nbrOfEntries,
			sizeof(d_iov_t));
	daos_iod_t *iod;

	if (desc->iods == NULL | desc->sgls == NULL | desc->recxs == NULL
		| desc->iovs == NULL) {
		return CUSTOM_ERR3;
	}
	for (i = 0; i < desc->nbrOfEntries; i++) {
		/* iod */
		/* akey */
		iod = &desc->iods[i];
		memcpy(&value16, desc_buffer, 2);
		desc_buffer += 2;
		d_iov_set(&iod->iod_name, desc_buffer, value16);
		desc_buffer += value16;
		iod->iod_type = DAOS_IOD_ARRAY;
		iod->iod_size = 1;
		iod->iod_nr = 1;
		iod->iod_recxs = &desc->recxs[i];
		/* offset */
		memcpy(&value64, desc_buffer, 8);
		desc->recxs[i].rx_idx = value64;
		desc_buffer += 8;
		/* length */
		memcpy(&value, desc_buffer, 4);
		desc->recxs[i].rx_nr = (uint64_t)value;
		desc_buffer += 4;
		/* sgl */
		memcpy(&value64, desc_buffer, 8);
		desc_buffer += 8;
		d_iov_set(&desc->iovs[i], (void *)value64, value);
		desc->sgls[i].sg_iovs = &desc->iovs[i];
		desc->sgls[i].sg_nr = 1;
		desc->sgls[i].sg_nr_out = 0;
	}
	desc->ret_buf_address = (uint64_t)desc_buffer;
	*ret_desc = desc;
	return 0;
}

static int
update_ret_code_async(void *udata, daos_event_t *ev, int ret)
{
	data_desc_async_t *desc = (data_desc_async_t *)udata;
	char *desc_buffer = (char *)desc->ret_buf_address;

	memcpy(desc_buffer, &ret, 4);
	desc->event->status = 0;
	release_desc_async(desc);
	return 0;
}

JNIEXPORT void JNICALL
Java_io_daos_obj_DaosObjClient_updateObjectAsync(
		JNIEnv *env, jobject clientObject,
		jlong objectHandle, jlong flags,
		jlong descBufAddress)
{
	daos_handle_t oh;
	data_desc_async_t *desc = NULL;
	int rc;

	memcpy(&oh, &objectHandle, 8);
	rc = decode_async(env, descBufAddress, &desc);
	if (rc) {
		char *msg = "Failed to allocate memory";

		throw_const_obj(env, msg, rc);
		goto fail;
	}
	desc->event->status = EVENT_IN_USE;
	desc->event->event.ev_error = 0;
	rc = daos_event_register_comp_cb(&desc->event->event,
					 update_ret_code_async, desc);
	if (rc) {
		char *msg = "Failed to register update callback";

		throw_const_obj(env, msg, rc);
		goto fail;
	}
	rc = daos_obj_update(oh, DAOS_TX_NONE, flags, &desc->dkey,
			     desc->nbrOfEntries, desc->iods,
			     desc->sgls, &desc->event->event);
	if (rc) {
		char *msg = "Failed to update DAOS object asynchronously";

		throw_const_obj(env, msg, rc);
		goto fail;
	}
	return;

fail:
	release_desc_async(desc);
}

static int
update_actual_size(void *udata, daos_event_t *ev, int ret)
{
	data_desc_simple_t *desc = (data_desc_simple_t *)udata;
	char *desc_buffer = (char *)desc->ret_buf_address;
	int i;
	uint32_t value;

	memcpy(desc_buffer, &ret, 4);
	desc_buffer += 4;
	for (i = 0; i < desc->nbrOfRequests; i++) {
		value = desc->sgls[i].sg_nr_out == 0 ?
			0 : desc->sgls[i].sg_iovs->iov_len;
		memcpy(desc_buffer, &value, 4);
		desc_buffer += 4;
	}
	if (ev) {
		desc->event->status = 0;
	}
	return 0;
}

JNIEXPORT void JNICALL
Java_io_daos_obj_DaosObjClient_fetchObjectSimple(
		JNIEnv *env, jobject clientObject,
		jlong objectHandle, jlong flags,
		jlong descBufAddress, jboolean async)
{
	daos_handle_t oh;
	data_desc_simple_t *desc = NULL;
	char *desc_buffer;
	int i;
	int rc;

	memcpy(&oh, &objectHandle, 8);
	if (decode_simple(env, descBufAddress, &desc, async)) {
		return;
	}
	if (async) {
		desc->event->status = EVENT_IN_USE;
		desc->event->event.ev_error = 0;
		rc = daos_event_register_comp_cb(&desc->event->event,
						 update_actual_size,
						 desc);
		if (rc) {
			char *msg = "Failed to register fetch callback";

			throw_const_obj(env, msg, rc);
			return;
		}
	}
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, flags, &desc->dkey,
			    desc->nbrOfRequests, desc->iods,
			    desc->sgls, NULL,
			    async ? &desc->event->event : NULL);
	if (rc) {
		throw_const_obj(env, "Failed to fetch DAOS object",
				rc);
		return;
	}
	/* actual data size */
	if (!async) {
		update_actual_size(desc, NULL, 0);
	}
}

static int
update_actual_size_async(void *udata, daos_event_t *ev, int ret)
{
	data_desc_async_t *desc = (data_desc_async_t *)udata;
	char *desc_buffer = (char *)desc->ret_buf_address;
	int i;
	uint32_t value;

	memcpy(desc_buffer, &ret, 4);
	desc_buffer += 4;
	for (i = 0; i < desc->nbrOfEntries; i++) {
		value = desc->sgls[i].sg_nr_out == 0 ?
			0 : desc->sgls[i].sg_iovs->iov_len;
		memcpy(desc_buffer, &value, 4);
		desc_buffer += 4;
	}
	desc->event->status = 0;
	release_desc_async(desc);
	return 0;
}

JNIEXPORT void JNICALL
Java_io_daos_obj_DaosObjClient_fetchObjectAsync(
		JNIEnv *env, jobject clientObject,
		jlong objectHandle, jlong flags,
		jlong descBufAddress)
{
	daos_handle_t oh;
	data_desc_async_t *desc = NULL;
	int rc;

	memcpy(&oh, &objectHandle, 8);
	rc = decode_async(env, descBufAddress, &desc);
	if (rc) {
		char *msg = "Failed to allocate memory";

		throw_const_obj(env, msg, rc);
		goto fail;
	}
	desc->event->status = EVENT_IN_USE;
	desc->event->event.ev_error = 0;
	rc = daos_event_register_comp_cb(&desc->event->event,
					 update_actual_size_async, desc);
	if (rc) {
		char *msg = "Failed to register fetch callback";

		throw_const_obj(env, msg, rc);
		goto fail;
	}
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, flags, &desc->dkey,
			    desc->nbrOfEntries, desc->iods,
			    desc->sgls, NULL, &desc->event->event);
	if (rc) {
		char *msg = "Failed to fetch DAOS object asynchronously";

		throw_const_obj(env, msg, rc);
		goto fail;
	}
	return;

fail:
	release_desc_async(desc);
}

static inline void
copy_kd(char *desc_buffer, daos_key_desc_t *kd)
{
	memcpy(desc_buffer, &kd->kd_key_len, 8);
	desc_buffer += 8;
	memcpy(desc_buffer, &kd->kd_val_type, 4);
	desc_buffer += 4;
	/* memcpy(desc_buffer, &kd->kd_csum_type, 2); */
	desc_buffer += 2;
	/* memcpy(desc_buffer, &kd->kd_csum_len, 2); */
	desc_buffer += 2;
}

static inline int
list_keys(jlong objectHandle, char *desc_buffer_head,
		char *key_buffer, jint keyBufLen,
		char *anchor_buffer_head, jint nbrOfDesc,
		daos_key_t *dkey, int dkey_len)
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

	if (kds == NULL) {
		return CUSTOM_ERR3;
	}

	if (dkey != NULL) {
		desc_buffer += dkey_len;
	}
	memcpy(&oh, &objectHandle, sizeof(oh));
	memset(&anchor, 0, sizeof(anchor));
	if ((int)anchor_buffer_head[0] != 0) {/* anchor in use */
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
			rc = daos_obj_list_dkey(oh, DAOS_TX_NONE, &nbr,
						&kds[idx], &sgl,
						&anchor, NULL);
		} else {
			rc = daos_obj_list_akey(oh, DAOS_TX_NONE, dkey,
						&nbr, &kds[idx],
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
		/* copy to kds and adjust sgl iov */
		for (i = idx - nbr; i < idx; i++) {
			copy_kd(desc_buffer, &kds[i]);
			desc_buffer += 12; /* 12 = key desc len */
			key_buffer_idx += kds[i].kd_key_len;
		}
		if (remaining <= 0) {
			quit_code = KEY_LIST_CODE_REACH_LIMIT;
			break;
		}
		d_iov_set(&iov, key_buffer + key_buffer_idx,
			keyBufLen - key_buffer_idx);
	}
	/* copy anchor back if necessary */
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
	/* set number of keys listed */
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
	int rc = list_keys(objectHandle, desc_buffer_head, key_buffer,
			keyBufLen, anchor_buffer_head, nbrOfDesc, NULL, 0);

	if (rc) {
		char *tmp = "Failed to list DAOS object dkeys, kds index: %d";
		char *msg = NULL;
		int idx;

		memcpy(&idx, desc_buffer_head, 4);
		asprintf(&msg, tmp, idx);
		throw_obj(env, msg, rc);
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
		char *msg = NULL;
		int idx;

		memcpy(&idx, desc_buffer_head, 4);
		asprintf(&msg, tmp, idx);
		throw_obj(env, msg, rc);
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
	rc = daos_obj_list_recx(oh, DAOS_TX_NONE, &dkey, &akey,
			&size, &nbr, &recx,
			&erange, &anchor, false, NULL);
	if (rc) {
		throw_const_obj(env, "Failed to get record size",
				rc);
	}
	return (jint)size;
}
