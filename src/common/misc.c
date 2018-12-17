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
#define D_LOGFAC	DD_FAC(common)

#include <daos/common.h>

/**
 * Initialise a scatter/gather list, create an array to store @nr iovecs.
 */
int
daos_sgl_init(d_sg_list_t *sgl, unsigned int nr)
{
	memset(sgl, 0, sizeof(*sgl));

	sgl->sg_nr = nr;
	if (nr == 0)
		return 0;

	D_ALLOC_ARRAY(sgl->sg_iovs, nr);

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
			D_FREE(sgl->sg_iovs[i].iov_buf);
		}
	}

	D_FREE(sgl->sg_iovs);
	memset(sgl, 0, sizeof(*sgl));
}

static int
daos_sgls_copy_internal(d_sg_list_t *dst_sgl, uint32_t dst_nr,
			d_sg_list_t *src_sgl, uint32_t src_nr,
			bool copy_data, bool by_out, bool alloc)
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

		if (alloc)
			daos_sgl_init(&dst_sgl[i], src_sgl[i].sg_nr);

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

				if (alloc) {
					daos_iov_copy(&dst_sgl[i].sg_iovs[j],
						      &src_sgl[i].sg_iovs[j]);
					continue;
				}

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
	return daos_sgls_copy_internal(dst, 1, src, 1, false, false, false);
}

int
daos_sgls_copy_data_out(d_sg_list_t *dst, int dst_nr, d_sg_list_t *src,
		       int src_nr)
{
	return daos_sgls_copy_internal(dst, dst_nr, src, src_nr, true, true,
				       false);
}

int
daos_sgl_copy_data_out(d_sg_list_t *dst, d_sg_list_t *src)
{
	return daos_sgls_copy_internal(dst, 1, src, 1, true, true, false);
}

int
daos_sgl_copy_data(d_sg_list_t *dst, d_sg_list_t *src)
{
	return daos_sgls_copy_internal(dst, 1, src, 1, true, false, false);
}

int
daos_sgl_alloc_copy_data(d_sg_list_t *dst, d_sg_list_t *src)
{
	return daos_sgls_copy_internal(dst, 1, src, 1, true, false, true);
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
daos_sgl_buf_size(d_sg_list_t *sgl)
{
	daos_size_t	size = 0;
	int		i;

	if (sgl == NULL || sgl->sg_iovs == NULL)
		return 0;

	for (i = 0, size = 0; i < sgl->sg_nr; i++)
		size += sgl->sg_iovs[i].iov_buf_len;

	return size;
}

daos_size_t
daos_sgls_buf_size(daos_sg_list_t *sgls, int nr)
{
	daos_size_t size = 0;
	int	    i;

	if (sgls == NULL)
		return 0;

	for (i = 0; i < nr; i++)
		size += daos_sgl_buf_size(&sgls[i]);

	return size;
}

daos_size_t
daos_sgls_size(daos_sg_list_t *sgls, int nr)
{
	daos_size_t size = 0;
	int i;

	if (sgls == NULL)
		return 0;

	for (i = 0; i < nr; i++) {
		int j;

		size += sizeof(sgls[i].sg_nr) + sizeof(sgls[i].sg_nr_out);
		for (j = 0; j < sgls[i].sg_nr; j++) {
			size += sizeof(sgls[i].sg_iovs[j].iov_len) +
				sizeof(sgls[i].sg_iovs[j].iov_buf_len) +
				sgls[i].sg_iovs[j].iov_buf_len;
		}
	}

	return size;
}

static daos_size_t
daos_iod_len(daos_iod_t *iod)
{
	daos_size_t	len;
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

daos_size_t
daos_iods_len(daos_iod_t *iods, int nr)
{
	daos_size_t iod_length = 0;
	int	    i;

	for (i = 0; i < nr; i++) {
		daos_size_t len = daos_iod_len(&iods[i]);

		if (len == (daos_size_t)-1) /* unknown */
			return -1;

		iod_length += len;
	}
	return iod_length;
}

int
daos_iod_copy(daos_iod_t *dst, daos_iod_t *src)
{
	int rc;

	rc = daos_iov_copy(&dst->iod_name, &src->iod_name);
	if (rc)
		return rc;

	dst->iod_kcsum = src->iod_kcsum;
	dst->iod_type = src->iod_type;
	dst->iod_size = src->iod_size;
	dst->iod_nr = src->iod_nr;
	dst->iod_recxs = src->iod_recxs;
	dst->iod_csums = src->iod_csums;
	dst->iod_eprs = src->iod_eprs;

	return 0;
}

void
daos_iods_free(daos_iod_t *iods, int nr, bool need_free)
{
	int i;

	for (i = 0; i < nr; i++) {
		daos_iov_free(&iods[i].iod_name);

		if (iods[i].iod_recxs)
			D_FREE(iods[i].iod_recxs);

		if (iods[i].iod_eprs)
			D_FREE(iods[i].iod_eprs);

		if (iods[i].iod_csums)
			D_FREE(iods[i].iod_csums);
	}

	if (need_free)
		D_FREE(iods);
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
	D_ALLOC(dst->iov_buf, src->iov_buf_len);
	if (dst->iov_buf == NULL)
		return -DER_NOMEM;
	dst->iov_buf_len = src->iov_buf_len;
	memcpy(dst->iov_buf, src->iov_buf, src->iov_len);
	dst->iov_len = src->iov_len;
	D_DEBUG(DB_TRACE, "iov_len %d\n", (int)dst->iov_len);
	return 0;
}

void
daos_iov_free(daos_iov_t *iov)
{
	if (iov->iov_buf == NULL)
		return;
	D_ASSERT(iov->iov_buf_len > 0);

	D_FREE(iov->iov_buf);
	iov->iov_buf = NULL;
	iov->iov_buf_len = 0;
	iov->iov_len = 0;
}

bool
daos_key_match(daos_key_t *key1, daos_key_t *key2)
{
	D_ASSERT(key1 != NULL);
	D_ASSERT(key2 != NULL);
	D_ASSERT(key1->iov_buf != NULL);
	D_ASSERT(key2->iov_buf != NULL);
	if (key1->iov_len != key2->iov_len)
		return false;

	if (memcmp(key1->iov_buf, key2->iov_buf, key1->iov_len))
		return false;

	return true;
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

	D_ALLOC_ARRAY(buf, cap);
	if (buf == NULL)
		D_GOTO(out, ranks = NULL);
	s = s_saved = strdup(str);
	if (s == NULL)
		D_GOTO(out_buf, ranks = NULL);

	while ((s = strtok_r(s, sep, &p)) != NULL) {
		if (n == cap) {
			d_rank_t    *buf_new;
			int		cap_new;

			/* Double the buffer. */
			cap_new = cap * 2;
			D_ALLOC_ARRAY(buf_new, cap_new);
			if (buf_new == NULL)
				D_GOTO(out_s, ranks = NULL);
			memcpy(buf_new, buf, sizeof(*buf_new) * n);
			D_FREE(buf);
			buf = buf_new;
			cap = cap_new;
		}
		buf[n] = atoi(s);
		n++;
		s = NULL;
	}

	ranks = daos_rank_list_alloc(n);
	if (ranks == NULL)
		D_GOTO(out_s, ranks = NULL);
	memcpy(ranks->rl_ranks, buf, sizeof(*buf) * n);

out_s:
	D_FREE(s_saved);
out_buf:
	D_FREE(buf);
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

bool
daos_file_is_dax(const char *pathname)
{
	return !strncmp(pathname, "/dev/dax", strlen("/dev/dax"));
}

/**
 * Some helper functions for daos handle hash-table.
 */

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
		D_GOTO(unlock, rc = 0);
	}

	rc = d_hhash_create(D_HHASH_BITS, &daos_ht.dht_hhash);
	if (rc == 0) {
		D_ASSERT(daos_ht.dht_hhash != NULL);
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
		D_GOTO(unlock, rc = -DER_UNINIT);
	if (daos_ht_ref > 1) {
		daos_ht_ref--;
		D_GOTO(unlock, rc = 0);
	}

	D_ASSERT(daos_ht.dht_hhash != NULL);
	d_hhash_destroy(daos_ht.dht_hhash);
	daos_ht.dht_hhash = NULL;

	daos_ht_ref = 0;
unlock:
	D_MUTEX_UNLOCK(&daos_ht_lock);
	return rc;
}

struct d_hlink *
daos_hhash_link_lookup(uint64_t key)
{
	D_ASSERT(daos_ht.dht_hhash != NULL);
	return d_hhash_link_lookup(daos_ht.dht_hhash, key);
}

void
daos_hhash_link_insert(struct d_hlink *hlink, int type)
{
	D_ASSERT(daos_ht.dht_hhash != NULL);

	if (d_hhash_is_ptrtype(daos_ht.dht_hhash) &&
	    d_hhash_key_isptr((uintptr_t)hlink))
		type = D_HTYPE_PTR;

	d_hhash_link_insert(daos_ht.dht_hhash, hlink, type);
}

void
daos_hhash_link_getref(struct d_hlink *hlink)
{
	D_ASSERT(daos_ht.dht_hhash != NULL);
	d_hhash_link_getref(daos_ht.dht_hhash, hlink);
}

void
daos_hhash_link_putref(struct d_hlink *hlink)
{
	D_ASSERT(daos_ht.dht_hhash != NULL);
	d_hhash_link_putref(daos_ht.dht_hhash, hlink);
}

bool
daos_hhash_link_delete(struct d_hlink *hlink)
{
	D_ASSERT(daos_ht.dht_hhash != NULL);
	return d_hhash_link_delete(daos_ht.dht_hhash, hlink);
}

