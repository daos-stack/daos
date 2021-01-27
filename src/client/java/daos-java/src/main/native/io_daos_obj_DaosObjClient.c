/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

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
		jlong oidBufferAddress, jint feats,
		jstring objectClass, jint args)
{
	daos_obj_id_t oid;
	uint16_t type;
	const char *oclass_name = (*env)->GetStringUTFChars(env, objectClass,
			NULL);
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
	memcpy(buffer + 8, &oid.lo, 8);

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
		jobject clientObject, jlong objectHandle,
		jlong flags, jint nbrOfDkeys,
		jlong bufferAddress, jint dataLen)
{
	daos_handle_t oh;
	daos_key_t *dkeys = (daos_key_t *)calloc(nbrOfDkeys,
		sizeof(daos_key_t));
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
	rc = daos_obj_punch_dkeys(oh, DAOS_TX_NONE, flags,
			(unsigned int)nbrOfDkeys, dkeys, NULL);

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
	rc = daos_obj_query(oh, &attr, &ranks, NULL);

	if (rc) {
		char *msg = "Failed to query DAOS object attribute";

		throw_exception_const_msg_object(env, msg, rc);
	}
	/* TODO: convert and serialize attribute */
	return NULL;
}

static inline void
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
	uint32_t value;
	uint32_t nbr_of_record;
	uint64_t address;
	int i;

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
			memcpy(&value, desc_buffer, 4);
			desc->recxs[i].rx_idx = (uint64_t)value;
			desc_buffer += 4;
			/* length */
			memcpy(&nbr_of_record, desc_buffer, 4);
			desc->recxs[i].rx_nr = (uint64_t)nbr_of_record;
			desc_buffer += 4;
			iod->iod_recxs = &desc->recxs[i];
		} else {
			nbr_of_record = 1;
		}

		/* sgl */
		memcpy(&address, desc_buffer, 8);
		desc_buffer += 8;
		d_iov_set(&desc->iovs[i], address,
			nbr_of_record * desc->record_size);
		desc->sgls[i].sg_iovs = &desc->iovs[i];
		desc->sgls[i].sg_nr = 1;
		desc->sgls[i].sg_nr_out = 0;
	}
	desc->ret_buf_address = desc_buffer;
}

static inline void
decode_reused(data_desc_t *desc, char *desc_buffer,
		int actual_nbr_of_keys) {
	uint16_t len;
	uint16_t pad;
	uint32_t nbr_of_record;
	uint32_t value;
	uint64_t address;
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
			memcpy(&value, desc_buffer, 4);
			desc->recxs[i].rx_idx = (uint64_t)value;
			desc_buffer += 4;
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

static inline void
decode(JNIEnv *env, jlong objectHandle, jint nbrOfAkeys,
		jlong descBufAddress, daos_handle_t *oh, daos_key_t *dkey,
		int *nbr_of_akeys_with_data, data_desc_t **ret_desc)
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
			char *msg = (char *)malloc(strlen(tmp) + 20);

			sprintf(msg, tmp, *nbr_of_akeys_with_data, nbrOfAkeys);
			throw_exception_object(env, msg, 0);
			return;
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
	if (address == 0) {/* no desc yet, reusable */
		desc = (data_desc_t *)malloc(sizeof(data_desc_t));
		uint16_t pad = maxKeyLen - len;

		if (pad > 0) {
			desc_buffer += pad;
		}
		desc->maxKeyLen = maxKeyLen;
		desc->iod_type = (daos_iod_type_t)iod_type;
		desc->record_size = record_size;
		desc->reusable = 1;
		desc->nbrOfAkeys = nbrOfAkeys;
		decode_initial(desc, desc_buffer);
		/* put address to the start of desc buffer */
		memcpy((char *)descBufAddress, &desc, 8);
	} else if (address == -1) {/* no desc yet, not reusable */
		desc = (data_desc_t *)malloc(sizeof(data_desc_t));
		desc->maxKeyLen = -1;
		desc->iod_type = (daos_iod_type_t)iod_type;
		desc->record_size = record_size;
		desc->reusable = 0;
		desc->nbrOfAkeys = nbrOfAkeys;
		decode_initial(desc, desc_buffer);
	} else {
		desc = *(data_desc_t **)&address;
		uint16_t pad = desc->maxKeyLen - len;

		if (pad > 0) {
			desc_buffer += pad;
		}
		decode_reused(desc, desc_buffer, *nbr_of_akeys_with_data);
	}
	*ret_desc = desc;
}

JNIEXPORT void JNICALL
Java_io_daos_obj_DaosObjClient_fetchObject(JNIEnv *env, jobject clientObject,
		jlong objectHandle, jlong flags,
		jint nbrOfAkeys, jlong descBufAddress)
{
	daos_handle_t oh;
	daos_key_t dkey;
	int nbr_of_akeys_with_data;
	data_desc_t *desc = NULL;
	char *desc_buffer;
	uint32_t value;
	int i;
	int rc;

	decode(env, objectHandle, nbrOfAkeys, descBufAddress, &oh, &dkey,
			&nbr_of_akeys_with_data, &desc);
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, flags, &dkey,
			(unsigned int)nbr_of_akeys_with_data, desc->iods,
			desc->sgls, NULL, NULL);
	if (rc) {
		char *msg = "Failed to fetch DAOS object";

		throw_exception_const_msg_object(env, msg, rc);
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
		jint nbrOfAkeys, jlong descBufAddress)
{
	daos_handle_t oh;
	daos_key_t dkey;
	int nbr_of_akeys_with_data;
	data_desc_t *desc = NULL;
	char *desc_buffer;
	uint32_t value;
	int i;
	int rc;

	decode(env, objectHandle, nbrOfAkeys, descBufAddress, &oh, &dkey,
			&nbr_of_akeys_with_data, &desc);
	rc = daos_obj_update(oh, DAOS_TX_NONE, flags, &dkey,
			(unsigned int)nbr_of_akeys_with_data, desc->iods,
			desc->sgls, NULL);
	if (rc) {
		char *msg = "Failed to update DAOS object";

		throw_exception_const_msg_object(env, msg, rc);
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
	uint64_t address;
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
		memcpy(&value, desc_buffer, 4);
		desc->recxs[i].rx_idx = (uint64_t)value;
		desc_buffer += 4;
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
		char *msg = (char *)malloc(strlen(tmp) + 20);

		sprintf(msg, tmp, desc->nbrOfRequests, desc->nbrOfEntries);
		throw_exception_object(env, msg, 0);
		return -2;
	}
	desc_buffer = decode_reused_simple(desc, desc_buffer);
	*ret_desc = desc;
	return 0;
}

static void
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
		desc_buffer += 8;
		iod->iod_recxs = &desc->recxs[i];
		/* sgl */
		memcpy(&address, desc_buffer, 8);
		desc_buffer += 8;
		d_iov_set(&desc->iovs[i], address, 0);
		desc->sgls[i].sg_iovs = &desc->iovs[i];
		desc->sgls[i].sg_nr = 1;
		desc->sgls[i].sg_nr_out = 0;
	}
	desc->ret_buf_address = desc_buffer;
	/* put address to the start of desc buffer */
	memcpy((char *)descBufAddress, &desc, 8);
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

	grp->descs = (data_desc_simple_t **)malloc(
		nbr * sizeof(data_desc_simple_t *));
	grp->nbrOfDescs = nbr;
	for (i = 0; i < nbr; i++) {
		grp->descs[i] = (data_desc_simple_t *)malloc(
			sizeof(data_desc_simple_t));
		memcpy(&address, buffer, 8);
		buffer += 8;
		allocate_simple_desc((char *)address, grp->descs[i], 1);
	}
	return *(jlong *)&grp;
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

JNIEXPORT void JNICALL
Java_io_daos_obj_DaosObjClient_allocateSimpleDesc(
		JNIEnv *env, jclass clientClass,
		jlong descBufAddress, jboolean async)
{
	char *desc_buffer = (char *)descBufAddress;

	data_desc_simple_t *desc = (data_desc_simple_t *)malloc(
		sizeof(data_desc_simple_t));

	allocate_simple_desc(desc_buffer, desc, async);
}

static int
update_ret_code(void *udata, daos_event_t *ev, int ret)
{
	data_desc_simple_t *desc = (data_desc_simple_t *)udata;
	char *desc_buffer = desc->ret_buf_address;

	memcpy(desc_buffer, &ret, 4);
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
		rc = daos_event_register_comp_cb(desc->event,
			update_ret_code, desc);
		if (rc) {
			char *msg = "Failed to register fetch callback";

			throw_exception_const_msg_object(env, msg, rc);
			return;
		}
	}
	rc = daos_obj_update(oh, DAOS_TX_NONE, flags, &desc->dkey,
				desc->nbrOfRequests, desc->iods,
				desc->sgls, async ? desc->event : NULL);
	if (rc) {
		char *msg = "Failed to update DAOS object";

		throw_exception_const_msg_object(env, msg, rc);
	}
}

static int
update_actual_size(void *udata, daos_event_t *ev, int ret)
{
	data_desc_simple_t *desc = (data_desc_simple_t *)udata;
	char *desc_buffer = desc->ret_buf_address;
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
		rc = daos_event_register_comp_cb(desc->event,
				update_actual_size,
				desc);
		if (rc) {
			char *msg = "Failed to register fetch callback";

			throw_exception_const_msg_object(env, msg, rc);
			return;
		}
	}
	rc = daos_obj_fetch(oh, DAOS_TX_NONE, flags, &desc->dkey,
			desc->nbrOfRequests, desc->iods,
			desc->sgls, NULL, async ? desc->event : NULL);
	if (rc) {
		char *msg = "Failed to fetch DAOS object";

		throw_exception_const_msg_object(env, msg, rc);
		return;
	}
	/* actual data size */
	if (!async) {
		update_actual_size(desc, desc->event, 0);
	}
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
	/*set number of keys listed */
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
	rc = daos_obj_list_recx(oh, DAOS_TX_NONE, &dkey, &akey,
			&size, &nbr, &recx,
			&erange, &anchor, false, NULL);
	if (rc) {
		char *tmp = "Failed to get record size";

		throw_exception_const_msg_object(env, tmp, rc);
	}
	return (jint)size;
}
