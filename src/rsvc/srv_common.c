/*
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Replicated Service Common Functions
 */

#define D_LOGFAC DD_FAC(rsvc)

#include <sys/stat.h>
#include <daos_srv/daos_engine.h>
#include <daos_srv/rsvc.h>

struct attr_list_iter_args {
	size_t		 available; /* Remaining client buffer space */
	size_t		 length; /* Aggregate length of attribute names */
	size_t		 iov_index;
	size_t		 iov_count;
	d_iov_t	*iovs;
};

static int
attr_list_iter_cb(daos_handle_t ih, d_iov_t *key, d_iov_t *val,
		  void *arg);
static int
attr_bulk_transfer(crt_rpc_t *rpc, crt_bulk_op_t op,
		   crt_bulk_t local_bulk, crt_bulk_t remote_bulk,
		   off_t local_off, off_t remote_off, size_t length);

int
ds_rsvc_set_attr(struct ds_rsvc *svc, struct rdb_tx *tx, rdb_path_t *path,
		 crt_bulk_t remote_bulk, crt_rpc_t *rpc, uint64_t count)
{
	crt_bulk_t			 local_bulk;
	daos_size_t			 bulk_size;
	d_iov_t			 iov;
	d_sg_list_t			 sgl;
	void				*data;
	char				*names;
	char				*values;
	size_t				*sizes;
	int				 rc;
	int				 i;

	rc = crt_bulk_get_len(remote_bulk, &bulk_size);
	if (rc != 0)
		goto out;
	D_DEBUG(DB_MD, "%s: count=%lu, size=%lu\n", svc->s_name, count,
		bulk_size);

	D_ALLOC(data, bulk_size);
	if (data == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}

	sgl.sg_nr = 1;
	sgl.sg_nr_out = sgl.sg_nr;
	sgl.sg_iovs = &iov;
	d_iov_set(&iov, data, bulk_size);
	rc = crt_bulk_create(rpc->cr_ctx, &sgl, CRT_BULK_RW, &local_bulk);
	if (rc != 0)
		goto out_mem;

	rc = attr_bulk_transfer(rpc, CRT_BULK_GET, local_bulk,
				remote_bulk, 0, 0, bulk_size);
	if (rc != 0)
		goto out_bulk;

	names = data;
	/* go to the end of names array */
	for (values = names, i = 0; i < count; ++values)
		if (*values == '\0')
			++i;
	sizes = (size_t *)values;
	values = (char *)(sizes + count);

	for (i = 0; i < count; i++) {
		size_t len;
		d_iov_t key;
		d_iov_t value;

		len = strlen(names) /* trailing '\0' */ + 1;
		d_iov_set(&key, names, len);
		names += len;
		d_iov_set(&value, values, sizes[i]);
		values += sizes[i];

		rc = rdb_tx_update(tx, path, &key, &value);
		if (rc != 0) {
			D_ERROR("%s: failed to update attribute "DF_KEY
				": "DF_RC"\n",
				svc->s_name, DP_KEY(&key), DP_RC(rc));
			goto out_bulk;
		}
	}

out_bulk:
	crt_bulk_free(local_bulk);
out_mem:
	D_FREE(data);
out:
	return rc;
}

int
ds_rsvc_del_attr(struct ds_rsvc *svc, struct rdb_tx *tx, rdb_path_t *path,
		 crt_bulk_t remote_bulk, crt_rpc_t *rpc, uint64_t count)
{
	crt_bulk_t			 local_bulk;
	daos_size_t			 bulk_size;
	d_iov_t				 iov;
	d_sg_list_t			 sgl;
	void				*data;
	char				*names;
	int				 rc;
	int				 i, nonexist = 0;

	rc = crt_bulk_get_len(remote_bulk, &bulk_size);
	if (rc != 0)
		goto out;
	D_DEBUG(DB_MD, "%s: count=%lu, size=%lu\n", svc->s_name, count,
		bulk_size);

	D_ALLOC(data, bulk_size);
	if (data == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	sgl.sg_nr = 1;
	sgl.sg_nr_out = sgl.sg_nr;
	sgl.sg_iovs = &iov;
	d_iov_set(&iov, data, bulk_size);
	rc = crt_bulk_create(rpc->cr_ctx, &sgl, CRT_BULK_RW, &local_bulk);
	if (rc != 0)
		goto out_mem;

	rc = attr_bulk_transfer(rpc, CRT_BULK_GET, local_bulk,
				remote_bulk, 0, 0, bulk_size);
	if (rc != 0)
		goto out_bulk;

	names = data;

	for (i = 0; i < count; i++) {
		size_t len;
		d_iov_t key;

		len = strlen(names) /* trailing '\0' */ + 1;
		d_iov_set(&key, names, len);
		names += len;

		rc = rdb_tx_delete(tx, path, &key);
		if (rc == -DER_NONEXIST) {
			D_DEBUG(DB_ANY, "%s: failed to delete attribute "DF_KEY
				": "DF_RC"\n", svc->s_name, DP_KEY(&key),
				DP_RC(rc));
			nonexist++;
		} else if (rc != 0) {
			D_ERROR("%s: failed to delete attribute "DF_KEY
				": "DF_RC"\n",
				svc->s_name, DP_KEY(&key), DP_RC(rc));
			goto out_bulk;
		}
	}

	if (nonexist == count) {
		rc = -DER_NONEXIST;
		goto out_bulk;
	}

out_bulk:
	crt_bulk_free(local_bulk);
out_mem:
	D_FREE(data);
out:
	return rc;
}

int
ds_rsvc_get_attr(struct ds_rsvc *svc, struct rdb_tx *tx, rdb_path_t *path,
		 crt_bulk_t remote_bulk, crt_rpc_t *rpc, uint64_t count,
		 uint64_t key_length)
{
	crt_bulk_t			 local_bulk;
	daos_size_t			 bulk_size;
	daos_size_t			 input_size;
	daos_size_t			 local_offset, remote_offset;
	d_iov_t			*iovs;
	d_sg_list_t			 sgl;
	void				*data;
	char				*names;
	size_t				*sizes;
	int				 rc, i, j, nonexist = 0, new_index = -1;

	rc = crt_bulk_get_len(remote_bulk, &bulk_size);
	if (rc != 0)
		goto out;
	D_DEBUG(DB_MD, "%s: count=%lu, key_length=%lu, size=%lu\n",
		svc->s_name, count, key_length, bulk_size);

	input_size = key_length + count * sizeof(*sizes);
	D_ASSERT(input_size <= bulk_size);

	D_ALLOC(data, input_size);
	if (data == NULL) {
		rc = -DER_NOMEM;
		goto out;
	}

	/* for output sizes */
	D_ALLOC_ARRAY(iovs, (int)(1 + count));
	if (iovs == NULL) {
		rc = -DER_NOMEM;
		goto out_data;
	}

	sgl.sg_nr = 1;
	sgl.sg_nr_out = sgl.sg_nr;
	sgl.sg_iovs = &iovs[0];
	d_iov_set(&iovs[0], data, input_size);
	rc = crt_bulk_create(rpc->cr_ctx, &sgl, CRT_BULK_RW, &local_bulk);
	if (rc != 0)
		goto out_iovs;

	rc = attr_bulk_transfer(rpc, CRT_BULK_GET, local_bulk,
				remote_bulk, 0, 0, input_size);
	crt_bulk_free(local_bulk);
	if (rc != 0)
		goto out_iovs;

	names = data;
	sizes = (size_t *)(names + key_length);
	d_iov_set(&iovs[0], (void *)sizes,
		     count * sizeof(*sizes));

	for (i = 0, j = 1; i < count; ++i) {
		size_t len;
		d_iov_t key;
		char *new_buf;

		len = strlen(names) + /* trailing '\0' */ 1;
		d_iov_set(&key, names, len);
		names += len;
		d_iov_set(&iovs[j], NULL, 0);

		rc = rdb_tx_lookup(tx, path, &key, &iovs[j]);

		if (rc == -DER_NONEXIST) {
			/* attribute do not exist */
			iovs[j - 1].iov_buf_len += sizes[i];
			sizes[i] = 0;

			D_DEBUG(DB_ANY, "%s: failed to lookup attribute "DF_KEY": "DF_RC"\n",
				svc->s_name, DP_KEY(&key), DP_RC(rc));
			nonexist++;
		} else if (rc != 0) {
			D_ERROR("%s: failed to lookup attribute "DF_KEY": "DF_RC"\n",
				svc->s_name, DP_KEY(&key), DP_RC(rc));
			goto out_iovs;
		} else if (sizes[i] > 0) {
			daos_size_t size;

			size = min(sgl.sg_iovs[j].iov_len, sizes[i]);

			/* need to copy from pmem to ram to avoid errors
			 * during RDMA registration
			 */
			D_ALLOC(new_buf, size);
			if (new_buf == NULL) {
				rc = -DER_NOMEM;
				goto out_iovs;
			}
			memcpy(new_buf, iovs[j].iov_buf, size);
			iovs[j].iov_buf_len = sizes[i];
			sizes[i] = iovs[j].iov_len;
			iovs[j].iov_len = size;
			iovs[j].iov_buf = new_buf;
			new_index = j;
		} else {
			/* only return size of attr */
			sizes[i] = iovs[j].iov_len;
			iovs[j].iov_buf_len = 0;
		}

		/* If buffer length is zero, send only size */
		if (iovs[j].iov_buf_len > 0)
			++j;
	}

	sgl.sg_nr = j;
	sgl.sg_nr_out = sgl.sg_nr;
	sgl.sg_iovs = iovs;
	rc = crt_bulk_create(rpc->cr_ctx, &sgl, CRT_BULK_RO, &local_bulk);
	if (rc != 0)
		goto out_iovs;

	rc = attr_bulk_transfer(rpc, CRT_BULK_PUT, local_bulk, remote_bulk,
				0, key_length, count * sizeof(*sizes));
	if (rc != 0)
		goto out_iovs;

	/* sizes have been sent back, so if none of attrs exist, just stop here
	 * and return -DER_NONEXIST
	 */
	if (nonexist == count) {
		rc = -DER_NONEXIST;
		goto out_iovs;
	}

	local_offset = iovs[0].iov_buf_len;
	remote_offset = key_length + iovs[0].iov_buf_len;

	for (i = 1; i < sgl.sg_nr; i++) {
		daos_size_t size;

		size = min(sgl.sg_iovs[i].iov_len, sgl.sg_iovs[i].iov_buf_len);
		rc = attr_bulk_transfer(rpc, CRT_BULK_PUT, local_bulk,
					remote_bulk, local_offset,
					remote_offset, size);
		if (rc != 0)
			goto out_iovs;

		local_offset += sgl.sg_iovs[i].iov_buf_len;
		remote_offset += sgl.sg_iovs[i].iov_buf_len;
	}

	crt_bulk_free(local_bulk);

out_iovs:
	for (i = 1; i <= new_index; i++)
		D_FREE(iovs[i].iov_buf);
	D_FREE(iovs);
out_data:
	D_FREE(data);
out:
	return rc;
}

int
ds_rsvc_list_attr(struct ds_rsvc *svc, struct rdb_tx *tx, rdb_path_t *path,
		  crt_bulk_t remote_bulk, crt_rpc_t *rpc, uint64_t *size)
{
	crt_bulk_t			 local_bulk;
	daos_size_t			 bulk_size;
	int				 rc, i, new_index = -1;
	struct attr_list_iter_args	 iter_args;

	/*
	 * If remote bulk handle does not exist, only aggregate size is sent.
	 */
	if (remote_bulk) {
		rc = crt_bulk_get_len(remote_bulk, &bulk_size);
		if (rc != 0)
			goto out;
		D_DEBUG(DB_MD, "%s: bulk_size=%lu\n", svc->s_name, bulk_size);

		/* Start with 1 and grow as needed */
		D_ALLOC_PTR(iter_args.iovs);
		if (iter_args.iovs == NULL) {
			rc = -DER_NOMEM;
			goto out;
		}
		iter_args.iov_count = 1;
	} else {
		bulk_size = 0;
		iter_args.iovs = NULL;
		iter_args.iov_count = 0;
	}
	iter_args.iov_index = 0;
	iter_args.length	 = 0;
	iter_args.available = bulk_size;
	rc = rdb_tx_iterate(tx, path, false /* !backward */,
			    attr_list_iter_cb, &iter_args);
	*size = iter_args.length;
	if (rc != 0)
		goto out_mem;

	if (iter_args.iov_index > 0) {
		d_sg_list_t	 sgl = {
			.sg_nr_out = iter_args.iov_index,
			.sg_nr	   = iter_args.iov_index,
			.sg_iovs   = iter_args.iovs
		};
		char *new_buf;

		/* need to copy from pmem to ram to avoid errors
		 * during RDMA registration
		 */
		for (i = 0; i < iter_args.iov_index; i++) {
			D_ALLOC(new_buf, iter_args.iovs[i].iov_len);
			if (new_buf == NULL) {
				rc = -DER_NOMEM;
				goto out_mem;
			}
			memcpy(new_buf, iter_args.iovs[i].iov_buf,
			       iter_args.iovs[i].iov_len);
			iter_args.iovs[i].iov_buf = new_buf;
			new_index = i;
		}

		rc = crt_bulk_create(rpc->cr_ctx, &sgl,
				     CRT_BULK_RW, &local_bulk);
		if (rc != 0)
			goto out_mem;

		rc = attr_bulk_transfer(rpc, CRT_BULK_PUT, local_bulk,
					remote_bulk, 0, 0,
					bulk_size - iter_args.available);
		crt_bulk_free(local_bulk);
	}

out_mem:
	for (i = 0; i <= new_index; i++)
		D_FREE(iter_args.iovs[i].iov_buf);
	D_FREE(iter_args.iovs);
out:
	return rc;
}

static int
bulk_cb(const struct crt_bulk_cb_info *cb_info)
{
	ABT_eventual *eventual = cb_info->bci_arg;

	DABT_EVENTUAL_SET(*eventual, (void *)&cb_info->bci_rc, sizeof(cb_info->bci_rc));
	return 0;
}

static int
attr_bulk_transfer(crt_rpc_t *rpc, crt_bulk_op_t op,
		   crt_bulk_t local_bulk, crt_bulk_t remote_bulk,
		   off_t local_off, off_t remote_off, size_t length)
{
	ABT_eventual		 eventual;
	int			*status;
	int			 rc;
	struct crt_bulk_desc	 bulk_desc = {
				.bd_rpc		= rpc,
				.bd_bulk_op	= op,
				.bd_local_hdl	= local_bulk,
				.bd_local_off	= local_off,
				.bd_remote_hdl	= remote_bulk,
				.bd_remote_off	= remote_off,
				.bd_len		= length,
			};

	rc = ABT_eventual_create(sizeof(*status), &eventual);
	if (rc != ABT_SUCCESS) {
		rc = dss_abterr2der(rc);
		goto out;
	}

	rc = crt_bulk_transfer(&bulk_desc, bulk_cb, &eventual, NULL);
	if (rc != 0)
		goto out_eventual;

	DABT_EVENTUAL_WAIT(eventual, (void **)&status);
	rc = *status;

out_eventual:
	DABT_EVENTUAL_FREE(&eventual);
out:
	return rc;
}

static int
attr_list_iter_cb(daos_handle_t ih, d_iov_t *key, d_iov_t *val, void *arg)
{
	struct attr_list_iter_args *i_args = arg;

	i_args->length += key->iov_len;

	if (i_args->available >= key->iov_len && key->iov_len > 0) {
		/*
		 * Exponentially grow the array of IOVs if insufficient.
		 * Considering the pathological case where each key is just
		 * a single character, with one additional trailing '\0',
		 * if the client buffer is 'N' bytes, it can hold at the most
		 * N/2 keys, which requires that many IOVs to be allocated.
		 * Thus, the upper limit on the space required for IOVs is:
		 * sizeof(d_iov_t) * N/2 = 24 * N/2 = 12*N bytes.
		 */
		if (i_args->iov_index == i_args->iov_count) {
			void *ptr;

			D_REALLOC_ARRAY(ptr, i_args->iovs, i_args->iov_count,
					i_args->iov_count * 2);
			/*
			 * TODO: Fail or continue transferring
			 *	 iteratively using available memory?
			 */
			if (ptr == NULL)
				return -DER_NOMEM;
			i_args->iovs = ptr;
			i_args->iov_count *= 2;
		}

		memcpy(&i_args->iovs[i_args->iov_index],
		       key, sizeof(d_iov_t));
		i_args->iovs[i_args->iov_index]
			.iov_buf_len = key->iov_len;
		i_args->available -= key->iov_len;
		++i_args->iov_index;
	}
	return 0;
}
