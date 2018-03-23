/**
 * (C) Copyright 2016 Intel Corporation.
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
/**
 * This file is part of daos. It implements some miscellaneous functions which
 * not belong to other parts.
 */
#define DDSUBSYS	DDFAC(common)

#include <daos/common.h>

/**
 * Initialise a scatter/gather list, create an array to store @nr iovecs.
 */
int
daos_sgl_init(d_sg_list_t *sgl, unsigned int nr)
{
	memset(sgl, 0, sizeof(*sgl));

	sgl->sg_nr = nr;
	D__ALLOC(sgl->sg_iovs, nr * sizeof(*sgl->sg_iovs));

	return sgl->sg_iovs == NULL ? -DER_NOMEM : 0;
}

/**
 * Finalise a scatter/gather list, it can also free iovecs if @free_iovs
 * is true.
 */
void
daos_sgl_fini(d_sg_list_t *sgl, bool free_iovs)
{
	int	i;

	if (sgl->sg_iovs == NULL)
		return;

	for (i = 0; free_iovs && i < sgl->sg_nr; i++) {
		if (sgl->sg_iovs[i].iov_buf != NULL) {
			D__FREE(sgl->sg_iovs[i].iov_buf,
			       sgl->sg_iovs[i].iov_buf_len);
		}
	}

	D__FREE(sgl->sg_iovs, sgl->sg_nr * sizeof(*sgl->sg_iovs));
	memset(sgl, 0, sizeof(*sgl));
}

static int
daos_sgls_copy_internal(d_sg_list_t *dst_sgl, uint32_t dst_nr,
			d_sg_list_t *src_sgl, uint32_t src_nr,
			bool copy_data, bool by_out)
{
	int i;

	if (src_nr > dst_nr) {
		D_ERROR("%u > %u\n", src_nr, dst_nr);
		return -DER_INVAL;
	}

	for (i = 0; i < src_nr; i++) {
		int num;

		if (by_out)
			num = src_sgl[i].sg_nr_out;
		else
			num = src_sgl[i].sg_nr;

		if (num == 0)
			continue;

		if (src_sgl[i].sg_nr > dst_sgl[i].sg_nr) {
			D_ERROR("%d : %u > %u\n", i,
				src_sgl[i].sg_nr, dst_sgl[i].sg_nr);
			return -DER_INVAL;
		}

		if (by_out)
			dst_sgl[i].sg_nr_out = num;

		if (copy_data) {
			int j;

			/* only copy data */
			for (j = 0; j < num; j++) {
				if (src_sgl[i].sg_iovs[j].iov_len == 0)
					continue;

				if (src_sgl[i].sg_iovs[j].iov_len >
				    dst_sgl[i].sg_iovs[j].iov_buf_len) {
					D_ERROR("%d:%d "DF_U64" > "DF_U64"\n",
					   i, j, src_sgl[i].sg_iovs[j].iov_len,
					   src_sgl[i].sg_iovs[j].iov_buf_len);
					return -DER_INVAL;
				}
				memcpy(dst_sgl[i].sg_iovs[j].iov_buf,
				       src_sgl[i].sg_iovs[j].iov_buf,
				       src_sgl[i].sg_iovs[j].iov_len);
				dst_sgl[i].sg_iovs[j].iov_len =
					src_sgl[i].sg_iovs[j].iov_len;
			}
		} else {
			/* only copy the pointer */
			memcpy(dst_sgl[i].sg_iovs, src_sgl[i].sg_iovs,
			       num * sizeof(*dst_sgl[i].sg_iovs));
		}
	}
	return 0;
}

int
daos_sgl_copy_ptr(d_sg_list_t *dst, d_sg_list_t *src)
{
	return daos_sgls_copy_internal(dst, 1, src, 1, false, false);
}

int
daos_sgls_copy_data_out(d_sg_list_t *dst, int dst_nr, d_sg_list_t *src,
		       int src_nr)
{
	return daos_sgls_copy_internal(dst, dst_nr, src, src_nr, true, true);
}

int
daos_sgl_copy_data_out(d_sg_list_t *dst, d_sg_list_t *src)
{
	return daos_sgls_copy_internal(dst, 1, src, 1, true, true);
}

int
daos_sgl_copy_data(d_sg_list_t *dst, d_sg_list_t *src)
{
	return daos_sgls_copy_internal(dst, 1, src, 1, true, false);
}

daos_size_t
daos_sgl_data_len(d_sg_list_t *sgl)
{
	daos_size_t	len;
	int		i;

	if (sgl == NULL || sgl->sg_iovs == NULL)
		return 0;

	for (i = 0, len = 0; i < sgl->sg_nr; i++)
		len += sgl->sg_iovs[i].iov_len;

	return len;
}

daos_size_t
daos_sgl_buf_len(d_sg_list_t *sgl)
{
	daos_size_t	len;
	int		i;

	if (sgl == NULL || sgl->sg_iovs == NULL)
		return 0;

	for (i = 0, len = 0; i < sgl->sg_nr; i++)
		len += sgl->sg_iovs[i].iov_buf_len;

	return len;
}

daos_size_t
daos_iod_len(daos_iod_t *iod)
{
	uint64_t	len;
	int		i;

	if (iod->iod_size == DAOS_REC_ANY)
		return -1; /* unknown size */

	len = 0;

	if (iod->iod_type == DAOS_IOD_SINGLE) {
		len += iod->iod_size;
	} else {
		if (iod->iod_recxs == NULL)
			return 0;

		for (i = 0, len = 0; i < iod->iod_nr; i++)
			len += iod->iod_size * iod->iod_recxs[i].rx_nr;
	}

	return len;
}

/**
 * Trim white space inplace for a string, it returns NULL if the string
 * only has white spaces.
 */
char *
daos_str_trimwhite(char *str)
{
	char	*end = str + strlen(str);

	while (isspace(*str))
		str++;

	if (str == end)
		return NULL;

	while (isspace(end[-1]))
		end--;

	*end = 0;
	return str;
}

int
daos_iov_copy(daos_iov_t *dst, daos_iov_t *src)
{
	D__ALLOC(dst->iov_buf, src->iov_buf_len);
	if (dst->iov_buf == NULL)
		return -DER_NOMEM;
	dst->iov_buf_len = src->iov_buf_len;
	memcpy(dst->iov_buf, src->iov_buf, src->iov_len);
	dst->iov_len = src->iov_len;
	return 0;
}

void
daos_iov_free(daos_iov_t *iov)
{
	if (iov->iov_buf == NULL)
		return;
	D__ASSERT(iov->iov_buf_len > 0);

	D__FREE(iov->iov_buf, iov->iov_buf_len);
	iov->iov_buf = NULL;
	iov->iov_buf_len = 0;
	iov->iov_len = 0;
}

d_rank_list_t *
daos_rank_list_parse(const char *str, const char *sep)
{
	d_rank_t	       *buf;
	int			cap = 8;
	d_rank_list_t	       *ranks;
	char		       *s, *s_saved;
	char		       *p;
	int			n = 0;

	D__ALLOC(buf, sizeof(*buf) * cap);
	if (buf == NULL)
		D__GOTO(out, ranks = NULL);
	s = s_saved = strdup(str);
	if (s == NULL)
		D__GOTO(out_buf, ranks = NULL);

	while ((s = strtok_r(s, sep, &p)) != NULL) {
		if (n == cap) {
			d_rank_t    *buf_new;
			int		cap_new;

			/* Double the buffer. */
			cap_new = cap * 2;
			D__ALLOC(buf_new, sizeof(*buf_new) * cap_new);
			if (buf_new == NULL)
				D__GOTO(out_s, ranks = NULL);
			memcpy(buf_new, buf, sizeof(*buf_new) * n);
			D__FREE(buf, sizeof(*buf) * cap);
			buf = buf_new;
			cap = cap_new;
		}
		buf[n] = atoi(s);
		n++;
		s = NULL;
	}

	ranks = daos_rank_list_alloc(n);
	if (ranks == NULL)
		D__GOTO(out_s, ranks = NULL);
	memcpy(ranks->rl_ranks, buf, sizeof(*buf) * n);

out_s:
	free(s_saved);
out_buf:
	D__FREE(buf, sizeof(*buf) * cap);
out:
	return ranks;
}

/* Find the first unset bit. */
int
daos_first_unset_bit(uint32_t *bits, unsigned int size)
{
	unsigned int idx = 0;
	unsigned int off;

	while (*bits == (uint32_t)(-1) && ++idx < size)
		bits++;

	if (idx == size)
		return -1;

	for (off = 0; off < 32; off++)
		if (isclr(bits, off))
			break;

	return idx * 32 + off;
}

struct daos_csum_dict {
	char		*cs_name;	/**< name string of the checksum */
};

static struct daos_csum_dict	csum_dict[] = {
	{
		.cs_name	= "crc64",
	},
	{
		.cs_name	= "crc32",
	},
	{
		.cs_name	= NULL,
	}
};

/**
 * This function returns true if the checksum type can be supported by DAOS,
 * return false otherwise.
 */
bool
daos_csum_supported(const char *cs_name)
{
	struct daos_csum_dict	*dict;

	if (!cs_name)
		return false;

	for (dict = &csum_dict[0]; dict->cs_name; dict++) {
		if (!strcasecmp(dict->cs_name, cs_name))
			return true;
	}
	D_ERROR("Unsuppprted checksum type: %s\n", cs_name);
	return false;
}

bool
daos_file_is_dax(const char *pathname)
{
	return !strncmp(pathname, "/dev/dax", strlen("/dev/dax"));
}

/**
 * Some helper functions for daos handle hash-table.
 */
struct daos_hhash_table {
	struct d_hhash	*dht_hhash;
	bool		 dht_ptrtype;
};

struct daos_hhash_table	daos_ht;
static pthread_mutex_t	daos_ht_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned int	daos_ht_ref;

int
daos_hhash_init(void)
{
	int rc;

	D_MUTEX_LOCK(&daos_ht_lock);
	if (daos_ht_ref > 0) {
		daos_ht_ref++;
		D__GOTO(unlock, rc = 0);
	}

	rc = d_hhash_create(D_HHASH_BITS, &daos_ht.dht_hhash);
	if (rc == 0) {
		D__ASSERT(daos_ht.dht_hhash != NULL);
		daos_ht_ref = 1;
	} else {
		D_ERROR("failed to create handle hash table: %d\n", rc);
	}

unlock:
	D_MUTEX_UNLOCK(&daos_ht_lock);
	return rc;
}

int
daos_hhash_fini(void)
{
	int rc = 0;

	D_MUTEX_LOCK(&daos_ht_lock);
	if (daos_ht_ref == 0)
		D__GOTO(unlock, rc = -DER_UNINIT);
	if (daos_ht_ref > 1) {
		daos_ht_ref--;
		D__GOTO(unlock, rc = 0);
	}

	D__ASSERT(daos_ht.dht_hhash != NULL);
	d_hhash_destroy(daos_ht.dht_hhash);
	daos_ht.dht_hhash = NULL;
	daos_ht.dht_ptrtype = false;

	daos_ht_ref = 0;
unlock:
	D_MUTEX_UNLOCK(&daos_ht_lock);
	return rc;
}

void
daos_hhash_set_ptrtype(void)
{
	daos_ht.dht_ptrtype = true;
}

static bool
daos_hhash_is_ptrtype()
{
	return daos_ht.dht_ptrtype;
}

struct d_hlink *
daos_hhash_link_lookup(uint64_t key)
{
	D__ASSERT(daos_ht.dht_hhash != NULL);
	return d_hhash_link_lookup(daos_ht.dht_hhash, key);
}

void
daos_hhash_link_insert(struct d_hlink *hlink, int type)
{
	D__ASSERT(daos_ht.dht_hhash != NULL);
	if (daos_hhash_is_ptrtype() && d_hhash_key_isptr((uintptr_t)hlink))
		type = D_HTYPE_PTR;

	d_hhash_link_insert(daos_ht.dht_hhash, hlink, type);
}

void
daos_hhash_link_getref(struct d_hlink *hlink)
{
	D__ASSERT(daos_ht.dht_hhash != NULL);
	d_hhash_link_getref(daos_ht.dht_hhash, hlink);
}

void
daos_hhash_link_putref(struct d_hlink *hlink)
{
	D__ASSERT(daos_ht.dht_hhash != NULL);
	d_hhash_link_putref(daos_ht.dht_hhash, hlink);
}

bool
daos_hhash_link_delete(struct d_hlink *hlink)
{
	D__ASSERT(daos_ht.dht_hhash != NULL);
	return d_hhash_link_delete(daos_ht.dht_hhash, hlink);
}
