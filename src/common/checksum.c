/**
 * (C) Copyright 2015-2018 Intel Corporation.
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
#define DDSUBSYS	DDFAC(common)

#include <daos/checksum.h>

struct daos_csum_entry {
	char		*cs_name;	/**< name string of the checksum */
	uint64_t	 cs_size;
};


static struct daos_csum_entry	csum_dict[] = {
	[DAOS_CS_CRC32] = {
		.cs_name	= "crc32",
		.cs_size	= sizeof(uint32_t),
	},
	[DAOS_CS_CRC64] = {
		.cs_name	= "crc64",
		.cs_size	= sizeof(uint64_t),
	},
};

/**
 * This function converts checksum name to csum_type
 */
static inline unsigned int
daos_name_to_type(const char *cs_name)
{
	int i;

	if (!cs_name)
		return -DER_INVAL;

	for (i = 0; i < DAOS_CS_MAX; i++) {
		if (!strcasecmp(csum_dict[i].cs_name, cs_name))
			return i;
	}
	D_ERROR("Unsupported checksum - no type for: %s\n", cs_name);
	return DAOS_CS_UNKNOWN;
}


/**
 * This function initializes a checksum and
 * returns error code, if checksum is not supported
 */
inline int
daos_csum_init(const char *cs_name, daos_csum_t *cs_obj)
{
	struct daos_csum_entry	*dict;
	unsigned int		type;

	type = daos_name_to_type(cs_name);
	if (type > DAOS_CS_MAX)
		return -DER_NOSYS;
	dict = &csum_dict[type];

#if defined(__x86_64__)
	cs_obj->dc_csum = type;
#else
	int rc;

	rc = mchecksum_init(cs_name, &cs_obj->dc_csum);
	if (rc < 0) {
		D_ERROR("Error in initializing checksum\n");
		return -DER_NOMEM;
	}
#endif
	cs_obj->dc_init = 1;
	memset(cs_obj->dc_buf, 0, DAOS_CSUM_SIZE);
	D_DEBUG(DB_IO, "Initialize checksum=%s\n", dict->cs_name);
	return 0;
}

inline int
daos_csum_reset(daos_csum_t *cs_obj)
{
	if (!cs_obj->dc_init)
		return -DER_UNINIT;
#if defined(__x86_64__)
	memset(cs_obj->dc_buf, 0, DAOS_CSUM_SIZE);
#else
	int  rc;

	rc = mchecksum_reset(cs_obj->dc_csum);
	if (rc < 0) {
		D_ERROR("Error resetting mchecksum: %d\n", rc);
		return -DER_UNINIT;
	}
#endif
	return 0;
}

inline int
daos_csum_free(daos_csum_t *cs_obj)
{
#if !defined(__x86_64__)
	mchecksum_destroy(cs_obj->dc_csum);
#endif
	return 0;
}

inline daos_size_t
daos_csum_get_size(const daos_csum_t *csum)
{
#if defined(__x86_64__)
	return csum_dict[csum->dc_csum].cs_size;
#else
	return mchecksum_get_size(csum->dc_csum);
#endif
}

inline int
daos_csum_get(daos_csum_t *csum, daos_csum_buf_t *csum_buf)
{

#if defined(__x86_64__)
	if (csum_buf->cs_buf_len != csum_dict[csum->dc_csum].cs_size) {
		D_ERROR("Incorrect result buffer size provided\n");
		return -DER_INVAL;
	}
	memcpy(csum_buf->cs_csum, csum->dc_buf,
	       csum_dict[csum->dc_csum].cs_size);

	return 0;
#else
	return mchecksum_get(csum, csum_buf->cs_csum, csum_buf->cs_buf_len,
			     MCHECKSUM_FINALIZE);
#endif
}
inline int
daos_csum_compare(daos_csum_t *csum, daos_csum_t *csum_src)
{

#if defined(__x86_64__)
	return (csum->dc_csum == csum_src->dc_csum) &&
		!(memcmp(csum->dc_buf, csum_src->dc_buf,
			 csum_dict[csum->dc_csum].cs_size));
#else
	size_t	hash_size;

	hash_size = mchecksum_get_size(csum_src->dc_csum);
	mchecksum_get(csum_src->dc_csum, &csum_src->dc_buf, hash_size,
		      MCHECKSUM_FINALIZE);
	mchecksum_get(csum->dc_csum, &csum->dc_buf, hash_size,
		      MCHECKSUM_FINALIZE);
	return !(strncmp(csum_src->dc_buf, csum->dc_buf, hash_size));
#endif
}

static int
daos_csum_update(daos_csum_t *csum, const void *buf,
		 uint64_t len)
{
#if defined(__x86_64__)
	switch (csum->dc_csum) {
	case DAOS_CS_CRC64:
	{
		uint64_t *cur_crc64;

		cur_crc64 = (uint64_t *)csum->dc_buf;
		*cur_crc64 = crc64_ecma_refl(*cur_crc64,
					     (const unsigned char *)buf,
					     len);
		break;
	}
	case DAOS_CS_CRC32:
	{
		uint32_t *cur_crc32;

		cur_crc32 = (uint32_t *)csum->dc_buf;
		*cur_crc32 = crc32_iscsi((unsigned char *)buf,
					 (int)len,
					 *cur_crc32);
		break;
	}
	default:
		D_ERROR("Unknown checksum type\n");
		return -DER_NOSYS;
	}

#else
	/* accumulates a partial checksum of the input data */
	int	 rc;

	rc = mchecksum_update(csum->dc_csum, buf, len);
	if (rc < 1)
		return -DER_NOSYS;
#endif
	return 0;
}

int
daos_csum_compute(daos_csum_t *csum, daos_sg_list_t *sgl)
{
	int	i;
	int	rc = 0;

	if (!sgl->sg_iovs)
		return 0;

	for (i = 0; i < sgl->sg_nr_out; i++) {
		if (!sgl->sg_iovs[i].iov_buf ||
		    !sgl->sg_iovs[i].iov_len)
			continue;
		rc = daos_csum_update(csum, sgl->sg_iovs[i].iov_buf,
				      sgl->sg_iovs[i].iov_len);
		if (rc != 0) {
			D_ERROR("Error in updating checksum: %d\n", rc);
			D_GOTO(failed, rc = -DER_IO);
		}
	}
failed:
	return rc;
}

