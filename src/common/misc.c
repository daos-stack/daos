/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos. It implements some miscellaneous functions which
 * not belong to other parts.
 */
#define D_LOGFAC	DD_FAC(common)

#include <daos/common.h>
#include <daos/checksum.h>
#include <daos/dtx.h>

int
daos_sgl_init(d_sg_list_t *sgl, unsigned int nr)
{
	D_ASSERTF(0, "This function is deprecated.  Use d_sgl_init\n");
	return 0;
}

int
daos_sgl_fini(d_sg_list_t *sgl, bool free_iovs)
{
	D_ASSERTF(0, "This function is deprecated.  Use d_sgl_fini\n");
	return 0;
}

static int
sgl_iovs_alloc(d_sg_list_t *dst_sgl, d_sg_list_t *src_sgl)
{
	int num = src_sgl->sg_nr;
	int i;
	int rc = 0;

	/* copy_ptr does not need allocate iovs */
	for (i = 0; i < num; i++) {
		if (src_sgl->sg_iovs[i].iov_buf_len == 0)
			continue;

		rc = daos_iov_alloc(&dst_sgl->sg_iovs[i],
				    src_sgl->sg_iovs[i].iov_buf_len, false);
		if (rc) {
			d_sgl_fini(dst_sgl, true);
			break;
		}
	}

	return rc;
}

static int
daos_sgls_copy_internal(d_sg_list_t *dst_sgl, uint32_t dst_nr,
			d_sg_list_t *src_sgl, uint32_t src_nr,
			bool copy_data, bool copy_ptr, bool by_out, bool alloc)
{
	int rc = 0;
	int i;

	if (src_nr > dst_nr) {
		D_ERROR("%u > %u\n", src_nr, dst_nr);
		return -DER_INVAL;
	}

	for (i = 0; i < src_nr; i++) {
		int num;

		if (by_out) {
			num = src_sgl[i].sg_nr_out;
			dst_sgl[i].sg_nr_out = num;
		} else {
			num = src_sgl[i].sg_nr;
		}

		if (num == 0)
			continue;

		if (alloc) {
			rc = d_sgl_init(&dst_sgl[i], src_sgl[i].sg_nr);
			if (rc)
				D_GOTO(out, rc);

			if (!copy_ptr) {
				/* copy_ptr does not need allocate iovs */
				rc = sgl_iovs_alloc(&dst_sgl[i], &src_sgl[i]);
				if (rc)
					D_GOTO(out, rc);
			}
		}

		if (src_sgl[i].sg_nr > dst_sgl[i].sg_nr) {
			D_ERROR("%d : %u > %u\n", i, src_sgl[i].sg_nr, dst_sgl[i].sg_nr);
			D_GOTO(out, rc = -DER_INVAL);
		}

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
					D_GOTO(out, rc = -DER_INVAL);
				}
				memcpy(dst_sgl[i].sg_iovs[j].iov_buf,
				       src_sgl[i].sg_iovs[j].iov_buf,
				       src_sgl[i].sg_iovs[j].iov_len);
				dst_sgl[i].sg_iovs[j].iov_len =
					src_sgl[i].sg_iovs[j].iov_len;
			}
		} else if (copy_ptr) {
			/* only copy the pointer */
			memcpy(dst_sgl[i].sg_iovs, src_sgl[i].sg_iovs,
			       num * sizeof(*dst_sgl[i].sg_iovs));
		}
	}
out:
	if (alloc && rc) {
		for (i = 0; i < src_nr; i++) {
			if (!copy_ptr)
				d_sgl_fini(&dst_sgl[i], true);
			else
				d_sgl_fini(&dst_sgl[i], false);
		}
	}

	return rc;
}

int
daos_sgls_copy_ptr(d_sg_list_t *dst, int dst_nr, d_sg_list_t *src, int src_nr)
{
	return daos_sgls_copy_internal(dst, dst_nr, src, src_nr, false, true, false, true);
}

int
daos_sgls_alloc(d_sg_list_t *dst, d_sg_list_t *src, int nr)
{
	return daos_sgls_copy_internal(dst, nr, src, nr, false, false, false, true);
}

int
daos_sgls_copy_data_out(d_sg_list_t *dst, int dst_nr, d_sg_list_t *src,
			int src_nr)
{
	return daos_sgls_copy_internal(dst, dst_nr, src, src_nr, true, false, true,
				       false);
}

int
daos_sgls_copy_all(d_sg_list_t *dst, int dst_nr, d_sg_list_t *src, int src_nr)
{
	return daos_sgls_copy_internal(dst, dst_nr, src, src_nr, true, false, false,
				       true);
}

int
daos_sgl_copy_data_out(d_sg_list_t *dst, d_sg_list_t *src)
{
	return daos_sgls_copy_internal(dst, 1, src, 1, true, false, true, false);
}

int
daos_sgl_copy_data(d_sg_list_t *dst, d_sg_list_t *src)
{
	return daos_sgls_copy_internal(dst, 1, src, 1, true, false, false, false);
}

int
daos_sgl_alloc_copy_data(d_sg_list_t *dst, d_sg_list_t *src)
{
	return daos_sgls_copy_internal(dst, 1, src, 1, true, false, false, true);
}

int
daos_sgl_merge(d_sg_list_t *dst, d_sg_list_t *src)
{
	d_iov_t *new_iovs = NULL;
	int total;
	int i;
	int rc = 0;

	D_ASSERT(dst != NULL);
	D_ASSERT(src != NULL);

	if (src->sg_nr == 0)
		return 0;

	total = dst->sg_nr + src->sg_nr;
	D_REALLOC_ARRAY(new_iovs, dst->sg_iovs, dst->sg_nr, total);
	if (new_iovs == NULL)
		return -DER_NOMEM;

	for (i = dst->sg_nr; i < total; i++) {
		int idx = i - dst->sg_nr;

		D_ALLOC_NZ(new_iovs[i].iov_buf, src->sg_iovs[idx].iov_buf_len);
		if (new_iovs[i].iov_buf == NULL)
			D_GOTO(free, rc = -DER_NOMEM);

		memcpy(new_iovs[i].iov_buf, src->sg_iovs[idx].iov_buf,
		       src->sg_iovs[idx].iov_len);
		new_iovs[i].iov_len = src->sg_iovs[idx].iov_len;
		new_iovs[i].iov_buf_len = src->sg_iovs[idx].iov_buf_len;
	}

free:
	dst->sg_iovs = new_iovs;
	dst->sg_nr = i;
	return rc;
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
daos_sgls_buf_size(d_sg_list_t *sgls, int nr)
{
	daos_size_t size = 0;
	int	    i;

	if (sgls == NULL)
		return 0;

	for (i = 0; i < nr; i++)
		size += daos_sgl_buf_size(&sgls[i]);

	return size;
}

int
daos_sgl_buf_extend(d_sg_list_t *sgl, int idx, size_t new_size)
{
	char	*new_buf;

	if (sgl == NULL || sgl->sg_iovs == NULL)
		return 0;

	D_ASSERT(sgl->sg_nr > idx);
	if (sgl->sg_iovs[idx].iov_buf_len >= new_size)
		return 0;

	D_REALLOC(new_buf, sgl->sg_iovs[idx].iov_buf,
		  sgl->sg_iovs[idx].iov_buf_len, new_size);
	if (new_buf == NULL)
		return -DER_NOMEM;

	sgl->sg_iovs[idx].iov_buf = new_buf;
	sgl->sg_iovs[idx].iov_buf_len = new_size;

	return 0;
}

/**
 * Query the size of packed sgls, if the \a buf_size != NULL then will set its
 * value as buffer size as well.
 */
daos_size_t
daos_sgls_packed_size(d_sg_list_t *sgls, int nr, daos_size_t *buf_size)
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

bool
daos_sgl_get_bytes(d_sg_list_t *sgl, bool check_buf, struct daos_sgl_idx *idx,
		   daos_size_t buf_len_req, uint8_t **p_buf, size_t *p_buf_len)
{
	daos_size_t buf_len = 0;
	daos_size_t len;

	if (p_buf_len != NULL)
		*p_buf_len = 0;

	if (idx->iov_idx >= sgl->sg_nr) {
		if (p_buf != NULL)
			*p_buf = NULL;
		return true; /** no data in sgl to get bytes from */
	}

	len = check_buf ? sgl->sg_iovs[idx->iov_idx].iov_buf_len :
		sgl->sg_iovs[idx->iov_idx].iov_len;

	D_ASSERT(idx->iov_offset < len);
	/** Point to current idx */
	if (p_buf != NULL)
		*p_buf = sgl->sg_iovs[idx->iov_idx].iov_buf + idx->iov_offset;

	/**
	 * Determine how many bytes to be used from buf by using
	 * minimum between requested bytes and bytes left in IOV buffer
	 */
	buf_len = MIN(buf_len_req, len - idx->iov_offset);

	/** Increment index */
	idx->iov_offset += buf_len;

	/** If end of iov was reached, go to next iov */
	if (idx->iov_offset == len) {
		idx->iov_idx++;
		idx->iov_offset = 0;
	}

	if (p_buf_len != NULL)
		*p_buf_len = buf_len;

	return idx->iov_idx == sgl->sg_nr;
}

int
daos_sgl_processor(d_sg_list_t *sgl, bool check_buf, struct daos_sgl_idx *idx,
		   size_t requested_bytes, daos_sgl_process_cb process_cb,
		   void *cb_args)
{
	uint8_t		*buf;
	size_t		 len = 0;
	bool		 end = false;
	int		 rc  = 0;

	/*
	 * loop until all bytes are consumed, the end of the sgl is reached, or
	 * an error occurs
	 */
	while (requested_bytes > 0 && !end && !rc) {
		buf = NULL;
		end = daos_sgl_get_bytes(sgl, check_buf, idx, requested_bytes,
					 &buf, &len);
		requested_bytes -= len;
		if (process_cb != NULL && buf != NULL)
			rc = process_cb(buf, len, cb_args);
	}

	if (requested_bytes)
		D_INFO("Requested more bytes than what's available in sgl\n");

	return rc;
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
daos_iov_copy(d_iov_t *dst, d_iov_t *src)
{
	if (src == NULL || src->iov_buf == NULL)
		return 0;
	D_ASSERT(src->iov_buf_len > 0);
	D_ASSERT(dst != NULL);

	D_ALLOC(dst->iov_buf, src->iov_buf_len);
	if (dst->iov_buf == NULL)
		return -DER_NOMEM;
	dst->iov_buf_len = src->iov_buf_len;
	memcpy(dst->iov_buf, src->iov_buf, src->iov_len);
	dst->iov_len = src->iov_len;
	D_DEBUG(DB_TRACE, "iov_len %d\n", (int)dst->iov_len);
	return 0;
}

int
daos_iov_alloc(d_iov_t *iov, daos_size_t size, bool set_full)
{
	D_ASSERT(iov != NULL);
	D_ASSERT(size > 0);

	memset(iov, 0, sizeof(*iov));
	D_ALLOC(iov->iov_buf, size);
	if (iov->iov_buf == NULL)
		return -DER_NOMEM;

	iov->iov_buf_len = size;
	if (set_full)
		iov->iov_len = size;

	return 0;
}

void
daos_iov_free(d_iov_t *iov)
{
	if (iov == NULL || iov->iov_buf == NULL)
		return;
	D_ASSERT(iov->iov_buf_len > 0);

	D_FREE(iov->iov_buf);
	iov->iov_buf_len = 0;
	iov->iov_len = 0;
}

bool
daos_iov_cmp(d_iov_t *iov1, d_iov_t *iov2)
{
	D_ASSERT(iov1 != NULL);
	D_ASSERT(iov2 != NULL);
	D_ASSERT(iov1->iov_buf != NULL);
	D_ASSERT(iov2->iov_buf != NULL);

	if (iov1->iov_len != iov2->iov_len)
		return false;

	return !memcmp(iov1->iov_buf, iov2->iov_buf, iov1->iov_len);
}

void
daos_iov_append(d_iov_t *iov, void *buf, uint64_t buf_len)
{
	D_ASSERTF(iov->iov_len + buf_len <= iov->iov_buf_len,
		  "iov->iov_len(%lu) + buf_len(%lu) <= iov->iov_buf_len(%lu)",
		  iov->iov_len, buf_len, iov->iov_buf_len);
	memcpy(iov->iov_buf + iov->iov_len, buf, buf_len);
	iov->iov_len += buf_len;
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
	D_STRNDUP(s_saved, str, strlen(str));
	s = s_saved;
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
daos_hhash_init_feats(uint32_t feats)
{
	int rc;

	D_MUTEX_LOCK(&daos_ht_lock);
	if (daos_ht_ref > 0) {
		daos_ht_ref++;
		D_GOTO(unlock, rc = 0);
	}

	/* D_HASH_FT_NO_KEYINIT_LOCK for optimized link_insert perf */
	rc = d_hhash_create(feats | D_HASH_FT_NO_KEYINIT_LOCK, D_HHASH_BITS, &daos_ht.dht_hhash);
	if (rc == 0) {
		D_ASSERT(daos_ht.dht_hhash != NULL);
		daos_ht_ref = 1;
	} else {
		D_ERROR("failed to create handle hash table: "DF_RC"\n",
			DP_RC(rc));
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
	uint32_t	limit = 0;

	/** enable statistics on the server side */
	daos_crt_init_opt.cio_use_sensors = server;

	/** configure cart for maximum bulk threshold */
	d_getenv_int("DAOS_RPC_SIZE_LIMIT", &limit);

	daos_crt_init_opt.cio_use_expected_size = 1;
	daos_crt_init_opt.cio_max_expected_size = limit ? limit : DAOS_RPC_SIZE;
	daos_crt_init_opt.cio_use_unexpected_size = 1;
	daos_crt_init_opt.cio_max_unexpected_size = limit ? limit : DAOS_RPC_SIZE;

	if (!server) {
		/* to workaround a bug in mercury/ofi, that the basic EP cannot
		 * communicate with SEP. Setting 2 for client to make it to use
		 * SEP for client.
		 */
		daos_crt_init_opt.cio_ctx_max_num = 2;
	} else {
		daos_crt_init_opt.cio_ctx_max_num = ctx_nr;
	}

	/** Scalable EndPoint-related settings */
	d_getenv_bool("CRT_CTX_SHARE_ADDR", &sep);
	if (!sep)
		goto out;

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
		goto out;
	}

	daos_crt_init_opt.cio_use_sep = 1;

out:
	return &daos_crt_init_opt;
}

void
daos_dti_gen_unique(struct dtx_id *dti)
{
	uuid_t uuid;

	uuid_generate(uuid);

	uuid_copy(dti->dti_uuid, uuid);
	dti->dti_hlc = d_hlc_get();
}

void
daos_dti_gen(struct dtx_id *dti, bool zero)
{
	static __thread uuid_t uuid;

	if (zero) {
		memset(dti, 0, sizeof(*dti));
	} else {
		if (uuid_is_null(uuid))
			uuid_generate(uuid);

		uuid_copy(dti->dti_uuid, uuid);
		dti->dti_hlc = d_hlc_get();
	}
}

/**
 * daos_recx_alloc/_free to provide same log facility for recx's alloc and free
 * for iom->iom_recxs' usage for example.
 */
daos_recx_t *
daos_recx_alloc(uint32_t nr)
{
	daos_recx_t	*recxs;

	D_ALLOC_ARRAY(recxs, nr);
	return recxs;
}

void
daos_recx_free(daos_recx_t *recx)
{
	D_FREE(recx);
}

int
daos_hlc2timespec(uint64_t hlc, struct timespec *ts)
{
	return d_hlc2timespec(hlc, ts);
}

int
daos_hlc2timestamp(uint64_t hlc, time_t *ts)
{
	struct timespec		tspec;
	int			rc;

	if (ts == NULL)
		return -DER_INVAL;

	rc = d_hlc2timespec(hlc, &tspec);
	if (rc)
		return rc;

	*ts = tspec.tv_sec;
	return 0;
}
