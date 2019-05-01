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
#include <daos_security.h>

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

/**
 * Query the size of packed sgls, if the \a buf_size != NULL then will set its
 * value as buffer size as well.
 */
daos_size_t
daos_sgls_packed_size(daos_sg_list_t *sgls, int nr, daos_size_t *buf_size)
{
	daos_size_t size = 0;
	int i;

	if (sgls == NULL) {
		if (buf_size != NULL)
			*buf_size = 0;
		return 0;
	}

	size = daos_sgls_buf_size(sgls, nr);
	if (buf_size != NULL)
		*buf_size = size;

	for (i = 0; i < nr; i++) {
		size += sizeof(sgls[i].sg_nr) + sizeof(sgls[i].sg_nr_out);
		size += sgls[i].sg_nr * (sizeof(sgls[i].sg_iovs[0].iov_len) +
				sizeof(sgls[i].sg_iovs[0].iov_buf_len));
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
	d_rank_list_t	       *ranks = NULL;
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

	if (n > 0) {
		ranks = daos_rank_list_alloc(n);
		if (ranks == NULL)
			D_GOTO(out_s, ranks = NULL);
		memcpy(ranks->rl_ranks, buf, sizeof(*buf) * n);
	}
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


#define CRT_SOCKET_PROV		"ofi+sockets"
/**
 * a helper to get the needed crt_init_opt.
 * When using SEP (scalable endpoint), if user set un-reasonable CRT_CTX_NUM it
 * possibly cause failure when creating cart context. So in this helper it will
 * check the ENV setting, if SEP is used then will set the crt_init_opttions_t
 * and the caller can pass it to crt_init_opt().
 *
 * \param server [IN]	true for server
 * \param ctx_nr [IN]	number of contexts
 *
 * \return		the pointer to crt_init_options_t (NULL if not needed)
 */
crt_init_options_t daos_crt_init_opt;
crt_init_options_t *
daos_crt_init_opt_get(bool server, int ctx_nr)
{
	crt_phy_addr_t	addr_env;
	bool		sep = false;

	d_getenv_bool("CRT_CTX_SHARE_ADDR", &sep);
	if (!sep)
		return NULL;

	daos_crt_init_opt.cio_crt_timeout = 0;
	daos_crt_init_opt.cio_sep_override = 1;

	/* for socket provider, force it to use regular EP rather than SEP for:
	 * 1) now sockets provider cannot create more than 16 contexts for SEP
	 * 2) some problems if SEP communicates with regular EP.
	 */
	addr_env = (crt_phy_addr_t)getenv(CRT_PHY_ADDR_ENV);
	if (addr_env != NULL &&
	    strncmp(addr_env, CRT_SOCKET_PROV, strlen(CRT_SOCKET_PROV)) == 0) {
		D_INFO("for sockets provider force it to use regular EP.\n");
		daos_crt_init_opt.cio_use_sep = 0;
		return &daos_crt_init_opt;
	}

	/* for psm2 provider, set a reasonable cio_ctx_max_num for cart */
	daos_crt_init_opt.cio_use_sep = 1;
	if (!server) {
		/* to workaround a bug in mercury/ofi, that the basic EP cannot
		 * communicate with SEP. Setting 2 for client to make it to use
		 * SEP for client.
		 */
		daos_crt_init_opt.cio_ctx_max_num = 2;
	} else {
		daos_crt_init_opt.cio_ctx_max_num = ctx_nr;
	}

	return &daos_crt_init_opt;
}

daos_prop_t *
daos_prop_alloc(uint32_t entries_nr)
{
	daos_prop_t	*prop;

	if (entries_nr > DAOS_PROP_ENTRIES_MAX_NR) {
		D_ERROR("cannot create daos_prop_t with %d entries(> %d).\n",
			entries_nr, DAOS_PROP_ENTRIES_MAX_NR);
		return NULL;
	}

	D_ALLOC_PTR(prop);
	if (prop == NULL)
		return NULL;

	if (entries_nr > 0) {
		D_ALLOC_ARRAY(prop->dpp_entries, entries_nr);
		if (prop->dpp_entries == NULL) {
			D_FREE_PTR(prop);
			return NULL;
		}
	}
	prop->dpp_nr = entries_nr;
	return prop;
}

void
daos_prop_free(daos_prop_t *prop)
{
	int i;

	if (prop == NULL)
		return;
	if (prop->dpp_nr == 0 || prop->dpp_entries == NULL) {
		D_FREE_PTR(prop);
		return;
	}

	for (i = 0; i < prop->dpp_nr; i++) {
		struct daos_prop_entry *entry;

		entry = &prop->dpp_entries[i];
		switch (entry->dpe_type) {
		case DAOS_PROP_PO_LABEL:
		case DAOS_PROP_CO_LABEL:
		case DAOS_PROP_PO_OWNER:
		case DAOS_PROP_PO_OWNER_GROUP:
			if (entry->dpe_str)
				D_FREE(entry->dpe_str);
			break;
		case DAOS_PROP_PO_ACL:
		case DAOS_PROP_CO_ACL:
			if (entry->dpe_val_ptr)
				D_FREE(entry->dpe_val_ptr);
			break;
		default:
			break;
		};
	}

	D_FREE(prop->dpp_entries);
	D_FREE_PTR(prop);
}

static bool
daos_prop_str_valid(d_string_t str, const char *prop_name, size_t max_len)
{
	size_t len;

	if (str == NULL) {
		D_ERROR("invalid NULL %s\n", prop_name);
		return false;
	}
	/* Detect if it's longer than max_len */
	len = strnlen(str, max_len + 1);
	if (len == 0 || len > max_len) {
		D_ERROR("invalid %s len=%lu, max=%lu\n",
			prop_name, len, max_len);
		return false;
	}
	return true;
}

static bool
daos_prop_owner_valid(d_string_t owner)
{
	/* Max length passed in doesn't include the null terminator */
	return daos_prop_str_valid(owner, "owner",
				   DAOS_ACL_MAX_PRINCIPAL_LEN);
}

static bool
daos_prop_owner_group_valid(d_string_t owner)
{
	/* Max length passed in doesn't include the null terminator */
	return daos_prop_str_valid(owner, "owner-group",
				   DAOS_ACL_MAX_PRINCIPAL_LEN);
}

static bool
daos_prop_label_valid(d_string_t label)
{
	return daos_prop_str_valid(label, "label", DAOS_PROP_LABEL_MAX_LEN);
}

/**
 * Check if the input daos_prop_t parameter is valid
 * \a pool true for pool properties, false for container properties.
 * \a input true for input properties that should with reasonable value,
 *          false for output that need not check the value.
 */
bool
daos_prop_valid(daos_prop_t *prop, bool pool, bool input)
{
	uint32_t	type;
	uint64_t	val;
	struct daos_acl	*acl_ptr;
	int		i;

	if (prop == NULL) {
		D_ERROR("NULL properties\n");
		return false;
	}
	if (prop->dpp_nr > DAOS_PROP_ENTRIES_MAX_NR) {
		D_ERROR("invalid ddp_nr %d (> %d).\n",
			prop->dpp_nr, DAOS_PROP_ENTRIES_MAX_NR);
		return false;
	}
	if (prop->dpp_nr == 0) {
		if (prop->dpp_entries != NULL)
			D_ERROR("invalid properties, NON-NULL dpp_entries with "
				"zero dpp_nr.\n");
		return prop->dpp_entries == NULL;
	}
	if (prop->dpp_entries == NULL) {
		D_ERROR("invalid properties, NULL dpp_entries with non-zero "
			"dpp_nr.\n");
		return false;
	}
	for (i = 0; i < prop->dpp_nr; i++) {
		type = prop->dpp_entries[i].dpe_type;
		if (pool) {
			if (type <= DAOS_PROP_PO_MIN ||
			    type >= DAOS_PROP_PO_MAX) {
				D_ERROR("invalid type %d for pool.\n", type);
				return false;
			}
		} else {
			if (type <= DAOS_PROP_CO_MIN ||
			    type >= DAOS_PROP_CO_MAX) {
				D_ERROR("invalid type %d for container.\n",
					type);
				return false;
			}
		}
		/* for output parameter need not check entry value */
		if (!input)
			continue;
		switch (type) {
		/* pool properties */
		case DAOS_PROP_PO_LABEL:
			if (!daos_prop_label_valid(
				prop->dpp_entries[i].dpe_str))
				return false;
			break;
		case DAOS_PROP_PO_ACL:
			acl_ptr = prop->dpp_entries[i].dpe_val_ptr;
			if (daos_acl_validate(acl_ptr) != 0)
				return false;
			break;
		case DAOS_PROP_CO_ACL:
			/* TODO: Implement container ACL */
			break;
		case DAOS_PROP_PO_SPACE_RB:
			val = prop->dpp_entries[i].dpe_val;
			if (val > 100) {
				D_ERROR("invalid space_rb "DF_U64".\n", val);
				return false;
			}
			break;
		case DAOS_PROP_PO_SELF_HEAL:
			break;
		case DAOS_PROP_PO_RECLAIM:
			val = prop->dpp_entries[i].dpe_val;
			if (val != DAOS_RECLAIM_SNAPSHOT &&
			    val != DAOS_RECLAIM_BATCH &&
			    val != DAOS_RECLAIM_TIME) {
				D_ERROR("invalid reclaim "DF_U64".\n", val);
				return false;
			}
			break;
		case DAOS_PROP_PO_OWNER:
			if (!daos_prop_owner_valid(
				prop->dpp_entries[i].dpe_str))
				return false;
			break;
		case DAOS_PROP_PO_OWNER_GROUP:
			if (!daos_prop_owner_group_valid(
				prop->dpp_entries[i].dpe_str))
				return false;
			break;
		/* container properties */
		case DAOS_PROP_CO_LABEL:
			if (!daos_prop_label_valid(
				prop->dpp_entries[i].dpe_str))
				return false;
			break;
		case DAOS_PROP_CO_LAYOUT_TYPE:
			val = prop->dpp_entries[i].dpe_val;
			if (val != DAOS_PROP_CO_LAYOUT_UNKOWN &&
			    val != DAOS_PROP_CO_LAYOUT_POSIX &&
			    val != DAOS_PROP_CO_LAYOUT_HDF5) {
				D_ERROR("invalid layout type "DF_U64".\n", val);
				return false;
			}
			break;
		case DAOS_PROP_CO_LAYOUT_VER:
			break;
		case DAOS_PROP_CO_CSUM:
			val = prop->dpp_entries[i].dpe_val;
			if (val != DAOS_PROP_CO_CSUM_OFF &&
			    val != DAOS_PROP_CO_CSUM_CRC16 &&
			    val != DAOS_PROP_CO_CSUM_CRC32 &&
			    val != DAOS_PROP_CO_CSUM_SHA1 &&
			    val != DAOS_PROP_CO_CSUM_SHA2) {
				D_ERROR("invalid checksum type "DF_U64".\n",
					val);
				return false;
			}
			break;
		case DAOS_PROP_CO_REDUN_FAC:
			val = prop->dpp_entries[i].dpe_val;
			if (val != DAOS_PROP_CO_REDUN_RF1 &&
			    val != DAOS_PROP_CO_REDUN_RF3) {
				D_ERROR("invalid redundancy factor "DF_U64".\n",
					val);
				return false;
			}
			break;
		case DAOS_PROP_CO_REDUN_LVL:
			val = prop->dpp_entries[i].dpe_val;
			if (val != DAOS_PROP_CO_REDUN_RACK &&
			    val != DAOS_PROP_CO_REDUN_NODE) {
				D_ERROR("invalid redundancy level "DF_U64".\n",
					val);
				return false;
			}
			break;
		case DAOS_PROP_CO_SNAPSHOT_MAX:
		case DAOS_PROP_CO_COMPRESS:
		case DAOS_PROP_CO_ENCRYPT:
			break;
		default:
			D_ERROR("invaid dpe_type %d.\n", type);
			return false;
		}
	}
	return true;
}

/**
 * duplicate the properties
 * \a pool true for pool properties, false for container properties.
 */
daos_prop_t *
daos_prop_dup(daos_prop_t *prop, bool pool)
{
	daos_prop_t		*prop_dup;
	struct daos_prop_entry	*entry, *entry_dup;
	int			 i;
	struct daos_acl		*acl_ptr;

	if (!daos_prop_valid(prop, pool, true))
		return NULL;

	prop_dup = daos_prop_alloc(prop->dpp_nr);
	if (prop_dup == NULL)
		return NULL;

	for (i = 0; i < prop->dpp_nr; i++) {
		entry = &prop->dpp_entries[i];
		entry_dup = &prop_dup->dpp_entries[i];
		entry_dup->dpe_type = entry->dpe_type;
		switch (entry->dpe_type) {
		case DAOS_PROP_PO_LABEL:
		case DAOS_PROP_CO_LABEL:
			entry_dup->dpe_str = strndup(entry->dpe_str,
						     DAOS_PROP_LABEL_MAX_LEN);
			if (entry_dup->dpe_str == NULL) {
				D_ERROR("failed to dup label.\n");
				daos_prop_free(prop_dup);
				return NULL;
			}
			break;
		case DAOS_PROP_PO_ACL:
			acl_ptr = entry->dpe_val_ptr;
			entry_dup->dpe_val_ptr = daos_acl_dup(acl_ptr);
			if (entry_dup->dpe_val_ptr == NULL) {
				D_ERROR("failed to dup ACL\n");
				daos_prop_free(prop_dup);
				return NULL;
			}
			break;
		case DAOS_PROP_CO_ACL:
			/* TODO: Implement container ACL */
			break;
		case DAOS_PROP_PO_OWNER:
		case DAOS_PROP_PO_OWNER_GROUP:
			D_STRNDUP(entry_dup->dpe_str, entry->dpe_str,
				  DAOS_ACL_MAX_PRINCIPAL_LEN);
			if (entry_dup->dpe_str == NULL) {
				D_ERROR("failed to dup ownership info.\n");
				daos_prop_free(prop_dup);
				return NULL;
			}
			break;
		default:
			entry_dup->dpe_val = entry->dpe_val;
			break;
		}
	}

	return prop_dup;
}

/**
 * Get the property entry of \a type in \a prop
 * return NULL if not found.
 */
struct daos_prop_entry *
daos_prop_entry_get(daos_prop_t *prop, uint32_t type)
{
	int i;

	if (prop == NULL || prop->dpp_nr == 0 || prop->dpp_entries == NULL)
		return NULL;
	for (i = 0; i < prop->dpp_nr; i++) {
		if (prop->dpp_entries[i].dpe_type == type)
			return &prop->dpp_entries[i];
	}
	return NULL;
}

/**
 * Copy properties from \a prop_reply to \a prop_req.
 * Used to copy the properties from pool query or container query to user's
 * properties. If user provided \a prop_req with zero dpp_nr (and NULL
 * dpp_entries), it will allocate needed buffer and assign to user's daos_prop_t
 * struct, the needed buffer to store label will be allocated as well.
 * User can free properties buffer by calling daos_prop_free().
 */
int
daos_prop_copy(daos_prop_t *prop_req, daos_prop_t *prop_reply)
{
	struct daos_prop_entry	*entry_req, *entry_reply;
	bool			 entries_alloc = false;
	bool			 label_alloc = false;
	bool			 acl_alloc = false;
	bool			 owner_alloc = false;
	bool			 group_alloc = false;
	struct daos_acl		*acl;
	uint32_t		 type;
	int			 i;
	int			 rc = 0;

	if (prop_reply == NULL || prop_reply->dpp_nr == 0 ||
	    prop_reply->dpp_entries == NULL) {
		D_ERROR("no prop or empty prop in reply.\n");
		return -DER_PROTO;
	}
	if (prop_req->dpp_nr == 0) {
		prop_req->dpp_nr = prop_reply->dpp_nr;
		D_ALLOC_ARRAY(prop_req->dpp_entries, prop_req->dpp_nr);
		if (prop_req->dpp_entries == NULL)
			return -DER_NOMEM;
		entries_alloc = true;
	}

	for (i = 0; i < prop_req->dpp_nr; i++) {
		entry_req = &prop_req->dpp_entries[i];
		type = entry_req->dpe_type;
		if (type == 0) {
			/* this is the case that dpp_entries allocated above */
			D_ASSERT(prop_req->dpp_nr == prop_reply->dpp_nr);
			type = prop_reply->dpp_entries[i].dpe_type;
			entry_req->dpe_type = type;
		}
		entry_reply = daos_prop_entry_get(prop_reply, type);
		if (entry_reply == NULL) {
			D_ERROR("cannot find prop entry for type %d.\n", type);
			D_GOTO(out, rc = -DER_PROTO);
		}
		if (type == DAOS_PROP_PO_LABEL || type == DAOS_PROP_CO_LABEL) {
			entry_req->dpe_str = strndup(entry_reply->dpe_str,
						     DAOS_PROP_LABEL_MAX_LEN);
			if (entry_req->dpe_str == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			label_alloc = true;
		} else if (type == DAOS_PROP_PO_ACL) {
			acl = entry_reply->dpe_val_ptr;
			entry_req->dpe_val_ptr = daos_acl_dup(acl);
			if (entry_req->dpe_val_ptr == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			acl_alloc = true;
		} else if (type == DAOS_PROP_CO_ACL) {
			/* TODO: Implement container ACL */
		} else if (type == DAOS_PROP_PO_OWNER) {
			D_STRNDUP(entry_req->dpe_str,
				  entry_reply->dpe_str,
				  DAOS_ACL_MAX_PRINCIPAL_LEN);
			if (entry_req->dpe_str == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			owner_alloc = true;
		} else if (type == DAOS_PROP_PO_OWNER_GROUP) {
			D_STRNDUP(entry_req->dpe_str,
				  entry_reply->dpe_str,
				  DAOS_ACL_MAX_PRINCIPAL_LEN);
			if (entry_req->dpe_str == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			group_alloc = true;
		} else {
			entry_req->dpe_val = entry_reply->dpe_val;
		}
	}

out:
	if (rc) {
		if (label_alloc) {
			entry_req = daos_prop_entry_get(prop_req,
							DAOS_PROP_PO_LABEL);
			D_FREE(entry_req->dpe_str);
		}
		if (acl_alloc) {
			entry_req = daos_prop_entry_get(prop_req,
							DAOS_PROP_PO_ACL);
			D_FREE(entry_req->dpe_val_ptr);
		}
		if (owner_alloc) {
			entry_req = daos_prop_entry_get(prop_req,
							DAOS_PROP_PO_OWNER);
			D_FREE(entry_req->dpe_str);
		}
		if (group_alloc) {
			entry_req = daos_prop_entry_get(prop_req,
						DAOS_PROP_PO_OWNER_GROUP);
			D_FREE(entry_req->dpe_str);
		}
		if (entries_alloc) {
			D_FREE(prop_req->dpp_entries);
		}
	}
	return rc;
}
