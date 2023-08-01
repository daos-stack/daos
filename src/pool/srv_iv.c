/**
 * (C) Copyright 2017-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * ds_pool: Pool IV cache
 */
#define D_LOGFAC	DD_FAC(pool)

#include <daos_srv/pool.h>
#include <daos_srv/container.h>
#include <daos_srv/security.h>
#include <daos/pool_map.h>
#include <daos_srv/iv.h>
#include <daos_prop.h>
#include "srv_internal.h"

static struct pool_iv_key *
key2priv(struct ds_iv_key *iv_key)
{
	return (struct pool_iv_key *)iv_key->key_buf;
}

uint32_t
pool_iv_map_ent_size(int nr)
{
	return sizeof(struct pool_iv_map) + pool_buf_size(nr) -
	       sizeof(struct pool_buf);
}

static uint32_t
pool_iv_prop_ent_size(int nr_aces, int nr_ranks)
{
	uint32_t acl_size;
	uint32_t svc_size;

	/* Calculate pool_iv_buf size */
	acl_size = roundup(offsetof(struct daos_acl, dal_ace[nr_aces]), 8);
	svc_size = roundup(nr_ranks * sizeof(d_rank_t), 8);

	return sizeof(struct pool_iv_entry) + acl_size + svc_size;
}

static int
pool_iv_value_alloc_internal(struct ds_iv_key *key, d_sg_list_t *sgl)
{
	struct pool_iv_key *pool_key = key2priv(key);
	uint32_t	buf_size = pool_key->pik_entry_size;
	int		rc;

	D_ASSERT(buf_size > 0);
	rc = d_sgl_init(sgl, 1);
	if (rc)
		return rc;

	D_ALLOC(sgl->sg_iovs[0].iov_buf, buf_size);
	if (sgl->sg_iovs[0].iov_buf == NULL)
		D_GOTO(free, rc = -DER_NOMEM);

	sgl->sg_iovs[0].iov_buf_len = buf_size;
	sgl->sg_iovs[0].iov_len = buf_size;
	if (key->class_id == IV_POOL_CONN) {
		struct pool_iv_conns *conns = sgl->sg_iovs[0].iov_buf;

		D_ASSERT(buf_size > sizeof(*conns));
		conns->pic_buf_size = buf_size - sizeof(*conns);
		conns->pic_size = 0;
	}
free:
	if (rc)
		d_sgl_fini(sgl, true);

	return rc;
}

static inline size_t
pool_iv_conn_size(size_t cred_size)
{
	return sizeof(struct pool_iv_conn) + cred_size;
}

static inline struct pool_iv_conn*
pool_iv_conn_next(struct pool_iv_conn *conn)
{
	return (struct pool_iv_conn *)((char *)conn +
		pool_iv_conn_size(conn->pic_cred_size));
}

static inline size_t
pool_iv_conn_ent_size(size_t cred_size)
{
	return sizeof(struct pool_iv_entry) +
	       pool_iv_conn_size(cred_size);
}

/* FIXME: Need better handling here, for example retry if the size is not
 * enough.
 */
#define PROP_SVC_LIST_MAX_TMP	16

static void
pool_iv_prop_l2g(daos_prop_t *prop, struct pool_iv_prop *iv_prop)
{
	struct daos_prop_entry	*prop_entry;
	struct daos_acl		*acl;
	d_rank_list_t		*svc_list;
	unsigned int		offset = 0;
	int			i;

	D_ASSERT(prop->dpp_nr == DAOS_PROP_PO_NUM);
	for (i = 0; i < DAOS_PROP_PO_NUM; i++) {
		prop_entry = &prop->dpp_entries[i];
		switch (prop_entry->dpe_type) {
		case DAOS_PROP_PO_LABEL:
			D_ASSERT(strlen(prop_entry->dpe_str) <=
				 DAOS_PROP_LABEL_MAX_LEN);
			strcpy(iv_prop->pip_label, prop_entry->dpe_str);
			break;
		case DAOS_PROP_PO_OWNER:
			D_ASSERT(strlen(prop_entry->dpe_str) <=
				 DAOS_ACL_MAX_PRINCIPAL_LEN);
			strcpy(iv_prop->pip_owner, prop_entry->dpe_str);
			break;
		case DAOS_PROP_PO_OWNER_GROUP:
			D_ASSERT(strlen(prop_entry->dpe_str) <=
				 DAOS_ACL_MAX_PRINCIPAL_LEN);
			strcpy(iv_prop->pip_owner_grp, prop_entry->dpe_str);
			break;
		case DAOS_PROP_PO_SPACE_RB:
			iv_prop->pip_space_rb = prop_entry->dpe_val;
			break;
		case DAOS_PROP_PO_SELF_HEAL:
			iv_prop->pip_self_heal = prop_entry->dpe_val;
			break;
		case DAOS_PROP_PO_RECLAIM:
			iv_prop->pip_reclaim = prop_entry->dpe_val;
			break;
		case DAOS_PROP_PO_EC_CELL_SZ:
			iv_prop->pip_ec_cell_sz = prop_entry->dpe_val;
			break;
		case DAOS_PROP_PO_REDUN_FAC:
			iv_prop->pip_redun_fac = prop_entry->dpe_val;
			break;
		case DAOS_PROP_PO_ACL:
			acl = prop_entry->dpe_val_ptr;
			if (acl != NULL) {
				ssize_t acl_size = daos_acl_get_size(acl);

				iv_prop->pip_acl_offset = offset;
				iv_prop->pip_acl =
						(void *)(iv_prop->pip_iv_buf +
						roundup(offset, 8));
				memcpy(iv_prop->pip_acl, acl, acl_size);
				offset += roundup(acl_size, 8);
			}
			break;
		case DAOS_PROP_PO_SVC_LIST:
			svc_list = prop_entry->dpe_val_ptr;
			if (svc_list) {
				int rc;

				D_ASSERT(svc_list->rl_nr <
					 PROP_SVC_LIST_MAX_TMP);
				iv_prop->pip_svc_list.rl_nr = svc_list->rl_nr;
				iv_prop->pip_svc_list.rl_ranks =
						(void *)(iv_prop->pip_iv_buf +
						roundup(offset, 8));
				iv_prop->pip_svc_list_offset = offset;
				rc = d_rank_list_copy(&iv_prop->pip_svc_list, svc_list);
				D_ASSERT(rc == 0);
				offset += roundup(
					svc_list->rl_nr * sizeof(d_rank_t), 8);
			}
			break;
		case DAOS_PROP_PO_EC_PDA:
			iv_prop->pip_ec_pda = prop_entry->dpe_val;
			break;
		case DAOS_PROP_PO_RP_PDA:
			iv_prop->pip_rp_pda = prop_entry->dpe_val;
			break;
		case DAOS_PROP_PO_POLICY:
			D_ASSERT(strlen(prop_entry->dpe_str) <=
				 DAOS_PROP_POLICYSTR_MAX_LEN);
			strcpy(iv_prop->pip_policy_str, prop_entry->dpe_str);
			break;
		case DAOS_PROP_PO_GLOBAL_VERSION:
			iv_prop->pip_global_version = prop_entry->dpe_val;
			break;
		case DAOS_PROP_PO_OBJ_VERSION:
			iv_prop->pip_obj_version = prop_entry->dpe_val;
			break;
		case DAOS_PROP_PO_UPGRADE_STATUS:
			iv_prop->pip_upgrade_status = prop_entry->dpe_val;
			break;
		case DAOS_PROP_PO_PERF_DOMAIN:
			iv_prop->pip_perf_domain = prop_entry->dpe_val;
			break;
		case DAOS_PROP_PO_SCRUB_MODE:
			iv_prop->pip_scrub_mode = prop_entry->dpe_val;
			break;
		case DAOS_PROP_PO_SCRUB_FREQ:
			iv_prop->pip_scrub_freq = prop_entry->dpe_val;
			break;
		case DAOS_PROP_PO_SCRUB_THRESH:
			iv_prop->pip_scrub_thresh = prop_entry->dpe_val;
			break;
		case DAOS_PROP_PO_SVC_REDUN_FAC:
			iv_prop->pip_svc_redun_fac = prop_entry->dpe_val;
			break;
		case DAOS_PROP_PO_CHECKPOINT_MODE:
			iv_prop->pip_checkpoint_mode = prop_entry->dpe_val;
			break;
		case DAOS_PROP_PO_CHECKPOINT_FREQ:
			iv_prop->pip_checkpoint_freq = prop_entry->dpe_val;
			break;
		case DAOS_PROP_PO_CHECKPOINT_THRESH:
			iv_prop->pip_checkpoint_thresh = prop_entry->dpe_val;
			break;
		default:
			D_ASSERTF(0, "bad dpe_type %d\n", prop_entry->dpe_type);
			break;
		}
	}
}

static int
pool_iv_prop_g2l(struct pool_iv_prop *iv_prop, daos_prop_t *prop)
{
	struct daos_prop_entry *prop_entry;
	struct daos_acl        *acl;
	d_rank_list_t          *svc_list;
	d_rank_list_t          *dst_list;
	int                     i;
	int                     rc = 0;

	D_ASSERT(prop->dpp_nr == DAOS_PROP_PO_NUM);
	for (i = 0; i < DAOS_PROP_PO_NUM; i++) {
		prop_entry = &prop->dpp_entries[i];
		prop_entry->dpe_type = DAOS_PROP_PO_MIN + i + 1;
		switch (prop_entry->dpe_type) {
		case DAOS_PROP_PO_LABEL:
			D_ASSERT(strlen(iv_prop->pip_label) <=
				 DAOS_PROP_LABEL_MAX_LEN);
			D_STRNDUP(prop_entry->dpe_str, iv_prop->pip_label,
				  DAOS_PROP_LABEL_MAX_LEN);
			if (prop_entry->dpe_str == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			break;
		case DAOS_PROP_PO_OWNER:
			D_ASSERT(strlen(iv_prop->pip_owner) <=
				 DAOS_ACL_MAX_PRINCIPAL_LEN);
			D_STRNDUP(prop_entry->dpe_str, iv_prop->pip_owner,
				  DAOS_ACL_MAX_PRINCIPAL_LEN);
			if (prop_entry->dpe_str == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			break;
		case DAOS_PROP_PO_OWNER_GROUP:
			D_ASSERT(strlen(iv_prop->pip_owner_grp) <=
				 DAOS_ACL_MAX_PRINCIPAL_LEN);
			D_STRNDUP(prop_entry->dpe_str, iv_prop->pip_owner_grp,
				  DAOS_ACL_MAX_PRINCIPAL_LEN);
			if (prop_entry->dpe_str == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			break;
		case DAOS_PROP_PO_SPACE_RB:
			prop_entry->dpe_val = iv_prop->pip_space_rb;
			break;
		case DAOS_PROP_PO_SELF_HEAL:
			prop_entry->dpe_val = iv_prop->pip_self_heal;
			break;
		case DAOS_PROP_PO_SCRUB_MODE:
			prop_entry->dpe_val = iv_prop->pip_scrub_mode;
			break;
		case DAOS_PROP_PO_SCRUB_FREQ:
			prop_entry->dpe_val = iv_prop->pip_scrub_freq;
			break;
		case DAOS_PROP_PO_SCRUB_THRESH:
			prop_entry->dpe_val = iv_prop->pip_scrub_thresh;
			break;
		case DAOS_PROP_PO_RECLAIM:
			prop_entry->dpe_val = iv_prop->pip_reclaim;
			break;
		case DAOS_PROP_PO_EC_CELL_SZ:
			prop_entry->dpe_val = iv_prop->pip_ec_cell_sz;
			break;
		case DAOS_PROP_PO_REDUN_FAC:
			prop_entry->dpe_val = iv_prop->pip_redun_fac;
			break;
		case DAOS_PROP_PO_ACL:
			iv_prop->pip_acl =
				(void *)(iv_prop->pip_iv_buf +
				 roundup(iv_prop->pip_acl_offset, 8));
			acl = iv_prop->pip_acl;
			if (acl->dal_len > 0) {
				rc = daos_acl_validate(acl);
				if (rc != -DER_SUCCESS)
					D_GOTO(out, rc);
				prop_entry->dpe_val_ptr = daos_acl_dup(acl);
				if (prop_entry->dpe_val_ptr == NULL)
					D_GOTO(out, rc = -DER_NOMEM);
			} else {
				prop_entry->dpe_val_ptr = NULL;
			}
			break;
		case DAOS_PROP_PO_SVC_LIST:
			iv_prop->pip_svc_list.rl_ranks =
				(void *)(iv_prop->pip_iv_buf +
				 roundup(iv_prop->pip_svc_list_offset, 8));
			svc_list = &iv_prop->pip_svc_list;
			if (svc_list->rl_nr > 0) {
				rc = d_rank_list_dup(&dst_list, svc_list);
				if (rc)
					D_GOTO(out, rc);
				prop_entry->dpe_val_ptr = dst_list;
			}
			break;
		case DAOS_PROP_PO_EC_PDA:
			prop_entry->dpe_val = iv_prop->pip_ec_pda;
			break;
		case DAOS_PROP_PO_RP_PDA:
			prop_entry->dpe_val = iv_prop->pip_rp_pda;
			break;
		case DAOS_PROP_PO_POLICY:
			D_ASSERT(strnlen(iv_prop->pip_policy_str,
					DAOS_PROP_POLICYSTR_MAX_LEN) <=
				 DAOS_PROP_POLICYSTR_MAX_LEN);
			D_STRNDUP(prop_entry->dpe_str, iv_prop->pip_policy_str,
				  DAOS_PROP_POLICYSTR_MAX_LEN);
			if (prop_entry->dpe_str == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			break;
		case DAOS_PROP_PO_GLOBAL_VERSION:
			prop_entry->dpe_val = iv_prop->pip_global_version;
			break;
		case DAOS_PROP_PO_OBJ_VERSION:
			prop_entry->dpe_val = iv_prop->pip_obj_version;
			break;
		case DAOS_PROP_PO_UPGRADE_STATUS:
			prop_entry->dpe_val = iv_prop->pip_upgrade_status;
			break;
		case DAOS_PROP_PO_PERF_DOMAIN:
			prop_entry->dpe_val = iv_prop->pip_perf_domain;
			break;
		case DAOS_PROP_PO_SVC_REDUN_FAC:
			prop_entry->dpe_val = iv_prop->pip_svc_redun_fac;
			break;
		case DAOS_PROP_PO_CHECKPOINT_MODE:
			prop_entry->dpe_val = iv_prop->pip_checkpoint_mode;
			break;
		case DAOS_PROP_PO_CHECKPOINT_FREQ:
			prop_entry->dpe_val = iv_prop->pip_checkpoint_freq;
			break;
		case DAOS_PROP_PO_CHECKPOINT_THRESH:
			prop_entry->dpe_val = iv_prop->pip_checkpoint_thresh;
			break;
		default:
			D_ASSERTF(0, "bad dpe_type %d\n", prop_entry->dpe_type);
			break;
		}
	}

out:
	return rc;
}

static bool
pool_iv_conn_valid(struct pool_iv_conn *conn, char *end)
{
	return (conn->pic_creds <= end) &&
	       ((char *)conn + pool_iv_conn_size(conn->pic_cred_size) <= end);
}

struct pool_iv_conn *
pool_iv_conn_lookup(struct pool_iv_conns *conns, uuid_t uuid)
{
	struct pool_iv_conn	*conn = conns->pic_conns;
	char			*end = (char *)conn + conns->pic_size;

	if (conns->pic_size == (uint32_t)(-1))
		return NULL;

	while (pool_iv_conn_valid(conn, end)) {
		if (uuid_compare(conn->pic_hdl, uuid) == 0)
			return conn;
		conn = pool_iv_conn_next(conn);
	}

	return NULL;
}

static int
pool_iv_conn_delete(struct pool_iv_conns *conns, uuid_t hdl_uuid)
{
	struct pool_iv_conn	*conn, *next;
	char			*end;
	size_t			size;

	conn = pool_iv_conn_lookup(conns, hdl_uuid);
	if (conn == NULL)
		return 0;

	D_DEBUG(DB_MD, "delete hdl uuid "DF_UUID"/%zd\n", DP_UUID(hdl_uuid),
		pool_iv_conn_size(conn->pic_cred_size));

	size = pool_iv_conn_size(conn->pic_cred_size);
	D_ASSERT(conns->pic_size >= size);
	end = (char *)conns->pic_conns + conns->pic_size;
	next = pool_iv_conn_next(conn);
	if (pool_iv_conn_valid(next, end))
		memmove((char *)conn, (char *)next,
			(unsigned long)(end - (char *)next));
	conns->pic_size -= size;

	return 0;
}

static int
pool_iv_conn_insert(struct pool_iv_conns *conns, struct pool_iv_conn *new_conn)
{
	struct pool_iv_conn	*conn;
	uint32_t		new_conn_size;
	char			*end;

	D_ASSERT(conns != NULL);
	conn = pool_iv_conn_lookup(conns, new_conn->pic_hdl);
	if (conn != NULL)
		return 0;

	/* append new_conn to the buffer */
	new_conn_size = pool_iv_conn_size(new_conn->pic_cred_size);
	if (conns->pic_buf_size < conns->pic_size + new_conn_size)
		return -DER_REC2BIG;

	end = (char *)conns->pic_conns + conns->pic_size;
	memcpy(end, new_conn, new_conn_size);
	conns->pic_size += new_conn_size;
	D_DEBUG(DB_MD, "insert conn %u/%u\n", conns->pic_size,
		conns->pic_buf_size);
	return 0;
}

static int
pool_iv_conns_buf_insert(struct pool_iv_conns *dst_conns,
			 struct pool_iv_conns *src_conns)
{
	struct pool_iv_conn	*conn;
	char			*end;
	int			rc = 0;

	/* Insert all connections from src_conns to dst_sgl */
	D_ASSERT(src_conns->pic_size != (uint32_t)(-1));
	conn = src_conns->pic_conns;
	end = (char *)conn + src_conns->pic_size;
	while (pool_iv_conn_valid(conn, end)) {
		rc = pool_iv_conn_insert(dst_conns, conn);
		if (rc)
			break;

		D_DEBUG(DB_MD, "insert conn "DF_UUID": %d\n",
			DP_UUID(conn->pic_hdl), rc);
		conn = pool_iv_conn_next(conn);
	}

	return rc;
}

static int
pool_iv_conns_ent_fetch(d_sg_list_t *dst_sgl, struct pool_iv_entry *src_iv)
{
	struct pool_iv_conns	*src_conns;
	struct pool_iv_entry	*dst_entry;
	struct pool_iv_conns	*dst_conns;
	uint32_t		dst_conns_size;
	int			rc = 0;

	src_conns = &src_iv->piv_conn_hdls;
	if (src_conns->pic_size == 0)
		return 0;

	if (src_conns->pic_size == (uint32_t)(-1)) {
		struct pool_iv_entry *dst_iv;

		dst_iv = dst_sgl->sg_iovs[0].iov_buf;
		memcpy(&dst_iv->piv_conn_hdls, &src_iv->piv_conn_hdls,
		       sizeof(struct pool_iv_conns));
		return -DER_IVCB_FORWARD;
	}

	dst_entry = dst_sgl->sg_iovs[0].iov_buf;
	dst_conns = &dst_entry->piv_conn_hdls;
	dst_conns_size = dst_conns->pic_size + src_conns->pic_size;
	if (dst_conns_size > dst_conns->pic_buf_size) {
		D_DEBUG(DB_MD, "dst_conns_size %d > pic_buf_size %u\n",
			dst_conns_size, dst_conns->pic_buf_size);
		dst_conns->pic_size = (uint32_t)(-1);
		dst_conns->pic_buf_size = dst_conns_size + sizeof(*dst_conns);
		return 0;
	}

	rc = pool_iv_conns_buf_insert(dst_conns, src_conns);

	return rc;
}

static int
pool_iv_conns_resize(d_sg_list_t *sgl, unsigned int old_size,
		     unsigned int new_size)
{
	struct pool_iv_entry *old_ent = sgl->sg_iovs[0].iov_buf;
	struct pool_iv_entry *new_ent;
	struct pool_iv_conns *new_conns;

	D_REALLOC(new_ent, old_ent, old_size, new_size);
	if (new_ent == NULL)
		return -DER_NOMEM;

	new_conns = &new_ent->piv_conn_hdls;
	new_conns->pic_buf_size = new_size - sizeof(*new_conns);
	D_DEBUG(DB_MD, "reset iv conns to %u/%u\n", new_conns->pic_size,
		new_conns->pic_buf_size);
	sgl->sg_iovs[0].iov_buf = new_ent;
	sgl->sg_iovs[0].iov_buf_len = new_size;
	return 0;
}

static int
pool_iv_conns_ent_update(d_sg_list_t *dst_sgl, struct pool_iv_entry *src_iv)
{
	struct pool_iv_conns	*src_conns;
	struct pool_iv_entry	*dst_entry;
	struct pool_iv_conns	*dst_conns;
	uint32_t		dst_conns_size;
	int			rc = 0;

	src_conns = &src_iv->piv_conn_hdls;
	if (src_conns->pic_size == 0)
		return 0;

	if (src_conns->pic_size == (uint32_t)(-1)) {
		struct pool_iv_entry *dst_iv;

		D_DEBUG(DB_MD, "Update -1 entry dst_sgl %p\n", dst_sgl);
		dst_iv = dst_sgl->sg_iovs[0].iov_buf;
		memcpy(&dst_iv->piv_conn_hdls, &src_iv->piv_conn_hdls,
		       sizeof(struct pool_iv_conns));
		return 0;
	}

	dst_entry = dst_sgl->sg_iovs[0].iov_buf;
	dst_conns = &dst_entry->piv_conn_hdls;

	/* Previous size query retry process might leave some -1 entry here.
	 * a bit hacky here XXX, will clean this up when supporting on-line
	 * pool reintegration.
	 */
	if (dst_conns->pic_size == (uint32_t)(-1))
		dst_conns->pic_size = 0;

	dst_conns_size = dst_conns->pic_size + src_conns->pic_size;
	if (dst_conns_size > dst_conns->pic_buf_size ||
	    dst_conns->pic_buf_size > dst_sgl->sg_iovs[0].iov_buf_len) {
		unsigned int old_size;
		unsigned int new_size;

		new_size = sizeof(*dst_conns) + dst_conns_size;
		old_size = dst_sgl->sg_iovs[0].iov_buf_len;
		rc = pool_iv_conns_resize(dst_sgl, old_size, new_size);
		if (rc)
			return rc;

		dst_entry = dst_sgl->sg_iovs[0].iov_buf;
		dst_conns = &dst_entry->piv_conn_hdls;
	}

	rc = pool_iv_conns_buf_insert(dst_conns, src_conns);

	return rc;
}

static int
pool_iv_ent_init(struct ds_iv_key *iv_key, void *data,
		 struct ds_iv_entry *entry)
{
	int	rc;

	rc = pool_iv_value_alloc_internal(iv_key, &entry->iv_value);
	if (rc)
		return rc;

	memcpy(&entry->iv_key, iv_key, sizeof(*iv_key));

	return rc;
}

static int
pool_iv_ent_get(struct ds_iv_entry *entry, void **priv)
{
	return 0;
}

static void
pool_iv_ent_put(struct ds_iv_entry *entry, void *priv)
{
	return;
}

static int
pool_iv_ent_destroy(d_sg_list_t *sgl)
{
	d_sgl_fini(sgl, true);
	return 0;
}

static int
pool_iv_map_ent_fetch(d_sg_list_t *dst_sgl, struct pool_iv_entry *src_iv)
{
	struct pool_iv_entry	*dst_iv;
	uint32_t		pb_nr;
	uint32_t		src_pbuf_size;
	uint32_t		dst_pbuf_size;

	dst_iv = dst_sgl->sg_iovs[0].iov_buf;
	if (src_iv->piv_map.piv_pool_buf.pb_target_nr == (uint32_t)(-1)) {
		memcpy(&dst_iv->piv_map.piv_pool_buf,
		       &src_iv->piv_map.piv_pool_buf,
		       sizeof(src_iv->piv_map.piv_pool_buf));
		return -DER_IVCB_FORWARD;
	}

	pb_nr = src_iv->piv_map.piv_pool_buf.pb_nr;
	D_ASSERT(pb_nr > 0);
	src_pbuf_size = pool_buf_size(pb_nr);
	dst_pbuf_size = dst_sgl->sg_iovs[0].iov_buf_len -
		  sizeof(struct pool_iv_map) + sizeof(struct pool_buf);

	if (src_pbuf_size <= dst_pbuf_size) {
		memcpy(&dst_iv->piv_map.piv_pool_buf,
		       &src_iv->piv_map.piv_pool_buf, src_pbuf_size);
		dst_sgl->sg_iovs[0].iov_len = pool_iv_map_ent_size(pb_nr);
		return 0;
	}

	dst_iv->piv_map.piv_pool_buf.pb_target_nr = (uint32_t)(-1);
	dst_iv->piv_map.piv_pool_buf.pb_nr =
				src_iv->piv_map.piv_pool_buf.pb_nr;
	D_DEBUG(DB_MD, "retry pool buf nr %u\n",
		src_iv->piv_map.piv_pool_buf.pb_nr);

	return 0;
}

/* update the pool IV map entry */
static int
pool_iv_map_ent_update(d_sg_list_t *dst_sgl, struct pool_iv_entry *src_iv)
{
	struct pool_iv_entry	*dst_iv;
	uint32_t		pb_nr = src_iv->piv_map.piv_pool_buf.pb_nr;
	uint32_t		src_pbuf_size;
	uint32_t		dst_pbuf_size;

	dst_iv = dst_sgl->sg_iovs[0].iov_buf;
	if (src_iv->piv_map.piv_pool_buf.pb_target_nr == (uint32_t)(-1)) {
		/* During fetch aggregation, update/refresh callback might
		 * be called to store these -1 entry.
		 **/
		memcpy(&dst_iv->piv_map.piv_pool_buf,
		       &src_iv->piv_map.piv_pool_buf,
		       sizeof(src_iv->piv_map.piv_pool_buf));
		return 0;
	}

	pb_nr = src_iv->piv_map.piv_pool_buf.pb_nr;
	src_pbuf_size = pool_buf_size(pb_nr);
	dst_pbuf_size = dst_sgl->sg_iovs[0].iov_buf_len -
			sizeof(struct pool_iv_map) + sizeof(struct pool_buf);
	if (src_pbuf_size > dst_pbuf_size) {
		void *new_buf;
		uint32_t new_size;

		new_size = pool_iv_map_ent_size(pb_nr);
		D_REALLOC_NZ(new_buf, dst_sgl->sg_iovs[0].iov_buf, new_size);
		if (new_buf == NULL)
			return -DER_NOMEM;

		dst_sgl->sg_iovs[0].iov_buf = new_buf;
		dst_sgl->sg_iovs[0].iov_buf_len = new_size;
		dst_iv = new_buf;
	}

	memcpy(&dst_iv->piv_map.piv_pool_buf,
	       &src_iv->piv_map.piv_pool_buf, src_pbuf_size);

	return 0;
}

static int
pool_iv_prop_ent_copy(struct pool_iv_entry *dst_iv, struct pool_iv_entry *src_iv)
{
	daos_prop_t *prop_fetch;
	int          rc;

	prop_fetch = daos_prop_alloc(DAOS_PROP_PO_NUM);
	if (prop_fetch == NULL)
		return -DER_NOMEM;

	rc = pool_iv_prop_g2l(&src_iv->piv_prop, prop_fetch);
	if (rc) {
		D_ERROR("pool_iv_prop_g2l failed: " DF_RC "\n", DP_RC(rc));
		goto out;
	}

	pool_iv_prop_l2g(prop_fetch, &dst_iv->piv_prop);
out:
	daos_prop_free(prop_fetch);

	return rc;
}

static void
pool_iv_srv_hdl_ent_copy(struct pool_iv_entry *dst_iv,
			 struct pool_iv_entry *src_iv)
{
	uuid_copy(dst_iv->piv_hdl.pih_pool_hdl,
		  src_iv->piv_hdl.pih_pool_hdl);
	uuid_copy(dst_iv->piv_hdl.pih_cont_hdl,
		  src_iv->piv_hdl.pih_cont_hdl);
	D_DEBUG(DB_MD, "pool/cont "DF_UUID"/"DF_UUID"\n",
		DP_UUID(dst_iv->piv_hdl.pih_pool_hdl),
		DP_UUID(dst_iv->piv_hdl.pih_cont_hdl));
}

static int
pool_iv_ent_copy(struct ds_iv_key *key, d_sg_list_t *dst_sgl,
		 struct pool_iv_entry *src_iv, bool update)
{
	struct pool_iv_entry	*dst_iv = dst_sgl->sg_iovs[0].iov_buf;
	int			rc = 0;

	D_ASSERT(dst_iv != src_iv);
	switch (key->class_id) {
	case IV_POOL_MAP:
		if (update)
			rc = pool_iv_map_ent_update(dst_sgl, src_iv);
		else
			rc = pool_iv_map_ent_fetch(dst_sgl, src_iv);
		break;
	case IV_POOL_PROP:
		rc = pool_iv_prop_ent_copy(dst_iv, src_iv);
		break;
	case IV_POOL_HDL:
		pool_iv_srv_hdl_ent_copy(dst_iv, src_iv);
		break;
	case IV_POOL_CONN:
		if (update)
			rc = pool_iv_conns_ent_update(dst_sgl, src_iv);
		else
			rc = pool_iv_conns_ent_fetch(dst_sgl, src_iv);
		break;
	default:
		D_ERROR("bad class id %d\n", key->class_id);
		rc = -DER_INVAL;
	}

	D_DEBUG(DB_MD, "%u update: %d\n", key->class_id, rc);
	return rc;
}

static int
pool_iv_ent_fetch(struct ds_iv_entry *entry, struct ds_iv_key *key,
		  d_sg_list_t *dst_sgl, void **priv)
{
	struct pool_iv_entry	*iv_entry = entry->iv_value.sg_iovs[0].iov_buf;
	int			rc;

	rc = pool_iv_ent_copy(key, dst_sgl, iv_entry, false);

	return rc;
}

static int
pool_iv_ent_update(struct ds_iv_entry *entry, struct ds_iv_key *key,
		   d_sg_list_t *src, void **priv)
{
	struct pool_iv_entry	*src_iv = src->sg_iovs[0].iov_buf;
	struct pool_iv_key	*ent_pool_key = key2priv(&entry->iv_key);
	struct pool_iv_key	*pool_key = key2priv(key);
	struct ds_pool		*pool;
	d_rank_t		rank;
	int			rc;

	rc = ds_pool_lookup(entry->ns->iv_pool_uuid, &pool);
	if (rc) {
		D_WARN("No pool "DF_UUID": %d\n", DP_UUID(entry->ns->iv_pool_uuid), rc);
		if (rc == -DER_NONEXIST)
			rc = 0;
		D_GOTO(out_put, rc);
	}

	rc = crt_group_rank(pool->sp_group, &rank);
	if (rc)
		D_GOTO(out_put, rc);

	if (rank != entry->ns->iv_master_rank)
		D_GOTO(out_put, rc = -DER_IVCB_FORWARD);

	if (ent_pool_key->pik_eph > pool_key->pik_eph && pool_key->pik_eph != 0) {
		/* If incoming key/eph is older than the current entry/key, then it means
		 * incoming update request is stale, especially for LAZY/asynchronous/retry
		 * cases, see iv_op().
		 */
		D_DEBUG(DB_MD, "current entry eph "DF_U64" > "DF_U64"\n",
			ent_pool_key->pik_eph, pool_key->pik_eph);
		D_GOTO(out_put, rc);
	}

	D_DEBUG(DB_TRACE, DF_UUID "rank %d master rank %d\n",
		DP_UUID(entry->ns->iv_pool_uuid), rank,
		entry->ns->iv_master_rank);

	/* Update pool map version or pool map */
	if (entry->iv_class->iv_class_id == IV_POOL_MAP) {
		int dst_len = entry->iv_value.sg_iovs[0].iov_buf_len -
			      sizeof(struct pool_iv_map) +
			      sizeof(struct pool_buf);
		int src_len = pool_buf_size(
			src_iv->piv_map.piv_pool_buf.pb_nr);

		rc = ds_pool_tgt_map_update(pool,
			src_iv->piv_map.piv_pool_buf.pb_nr > 0 ?
			&src_iv->piv_map.piv_pool_buf : NULL,
			src_iv->piv_map.piv_pool_map_ver);
		if (rc)
			D_GOTO(out_put, rc);

		/* realloc the pool iv buffer if the size is not enough */
		if (dst_len < src_len) {
			int new_alloc_size = src_len +
					     sizeof(struct pool_iv_map) -
					     sizeof(struct pool_buf);

			rc = daos_sgl_buf_extend(&entry->iv_value, 0,
						 new_alloc_size);
			if (rc)
				D_GOTO(out_put, rc);
		}
	} else if (entry->iv_class->iv_class_id == IV_POOL_PROP) {
		rc = ds_pool_tgt_prop_update(pool, &src_iv->piv_prop);
		if (rc)
			D_GOTO(out_put, rc);
	}

	/* Since pool_tgt_connect/prop_update/refresh_hdl might yield due to
	 * connective operation, so it need check sp_stopping again before
	 * pool_iv_ent_copy, in case the entry has been destroyed.
	 */
	if (!pool->sp_stopping) {
		rc = pool_iv_ent_copy(key, &entry->iv_value, src_iv, true);
		if (rc == 0 && pool_key->pik_eph != 0)
			ent_pool_key->pik_eph = pool_key->pik_eph;
	}

out_put:
	D_DEBUG(DB_MD, DF_UUID": key %u rc %d\n",
		DP_UUID(entry->ns->iv_pool_uuid), key->class_id, rc);
	if (pool != NULL)
		ds_pool_put(pool);
	return rc;
}

int
ds_pool_iv_refresh_hdl(struct ds_pool *pool, struct pool_iv_hdl *pih)
{
	int rc;

	if (!uuid_is_null(pool->sp_srv_cont_hdl)) {
		if (uuid_compare(pool->sp_srv_cont_hdl,
				 pih->pih_cont_hdl) == 0)
			return 0;
		ds_cont_tgt_close(pool->sp_srv_cont_hdl);
		uuid_clear(pool->sp_srv_cont_hdl);
		uuid_clear(pool->sp_srv_pool_hdl);
	}

	rc = ds_cont_tgt_open(pool->sp_uuid, pih->pih_cont_hdl, NULL, 0,
			      ds_sec_get_rebuild_cont_capabilities(), 0);
	if (rc == 0) {
		uuid_copy(pool->sp_srv_cont_hdl, pih->pih_cont_hdl);
		uuid_copy(pool->sp_srv_pool_hdl, pih->pih_pool_hdl);
	}

	return rc;
}

static int
pool_iv_ent_invalid(struct ds_iv_entry *entry, struct ds_iv_key *key)
{
	struct pool_iv_entry	*iv_entry = entry->iv_value.sg_iovs[0].iov_buf;
	struct ds_pool		*pool;
	int			rc;

	if (!entry->iv_valid)
		return 0;

	if (entry->iv_class->iv_class_id == IV_POOL_HDL) {
		if (!uuid_is_null(iv_entry->piv_hdl.pih_cont_hdl)) {
			rc = ds_pool_lookup(entry->ns->iv_pool_uuid, &pool);
			if (rc) {
				if (rc == -DER_NONEXIST)
					rc = 0;
				return rc;
			}
			ds_cont_tgt_close(iv_entry->piv_hdl.pih_cont_hdl);
			uuid_clear(pool->sp_srv_cont_hdl);
			uuid_clear(pool->sp_srv_pool_hdl);
			uuid_clear(iv_entry->piv_hdl.pih_cont_hdl);
			ds_pool_put(pool);
			return 0;
		}
	} else if (entry->iv_class->iv_class_id == IV_POOL_CONN) {
		struct pool_iv_key *pool_key = key2priv(key);

		rc = pool_iv_conn_delete(&iv_entry->piv_conn_hdls,
					 pool_key->pik_uuid);
		return rc;
	}

	return 0;
}

static int
pool_iv_ent_refresh(struct ds_iv_entry *entry, struct ds_iv_key *key,
		    d_sg_list_t *src, int ref_rc, void **priv)
{
	struct pool_iv_key	*pool_key = key2priv(key);
	struct pool_iv_key	*ent_pool_key = key2priv(&entry->iv_key);
	struct pool_iv_entry	*src_iv;
	struct ds_pool		*pool = 0;
	int			rc;

	rc = ds_pool_lookup(entry->ns->iv_pool_uuid, &pool);
	if (rc) {
		D_WARN("No pool "DF_UUID": %d\n", DP_UUID(entry->ns->iv_pool_uuid), rc);
		if (rc == -DER_NONEXIST)
			rc = 0;
		D_GOTO(out_put, rc);
	}

	if (ent_pool_key->pik_eph > pool_key->pik_eph && pool_key->pik_eph != 0) {
		/* If incoming key/eph is older than the current entry/key, then it means
		 * incoming update request is stale, especially for LAZY/asynchronous/retry
		 * cases, see iv_op().
		 */
		D_DEBUG(DB_MD, "current entry eph "DF_U64" > "DF_U64"\n",
			ent_pool_key->pik_eph, pool_key->pik_eph);
		D_GOTO(out_put, rc);
	}

	if (src == NULL) {
		rc = pool_iv_ent_invalid(entry, key);
		D_GOTO(out_put, rc);
	}

	src_iv = src->sg_iovs[0].iov_buf;
	if (entry->iv_class->iv_class_id == IV_POOL_PROP) {
		rc = ds_pool_tgt_prop_update(pool, &src_iv->piv_prop);
	} else if (entry->iv_class->iv_class_id == IV_POOL_CONN) {
		struct pool_iv_conn *conn;
		char *end;

		if (src_iv->piv_conn_hdls.pic_size != (unsigned int)(-1)) {
			conn = src_iv->piv_conn_hdls.pic_conns;
			end = (char *)conn + src_iv->piv_conn_hdls.pic_size;
			while (pool_iv_conn_valid(conn, end)) {
				rc = ds_pool_tgt_connect(pool, conn);
				if (rc)
					break;
				conn = pool_iv_conn_next(conn);
			}
		} else {
			if (entry->ns->iv_master_rank == dss_self_rank()) {
				D_DEBUG(DB_MD, "skip -1 update on master %u\n",
					entry->ns->iv_master_rank);
				D_GOTO(out_put, rc);
			} else {
				D_DEBUG(DB_MD, "update -1 iv cache %u/%u\n",
					dss_self_rank(), entry->ns->iv_master_rank);
				D_GOTO(update_iv_cache, rc);
			}
		}
	} else if (entry->iv_class->iv_class_id == IV_POOL_MAP) {
		if (src_iv->piv_map.piv_pool_buf.pb_target_nr != (unsigned int)(-1)) {
			rc = ds_pool_tgt_map_update(pool,
					src_iv->piv_map.piv_pool_buf.pb_nr > 0 ?
					&src_iv->piv_map.piv_pool_buf : NULL,
					src_iv->piv_map.piv_pool_map_ver);
		} else {
			if (entry->ns->iv_master_rank == dss_self_rank()) {
				D_DEBUG(DB_MD, "skip -1 update on master %u\n",
					entry->ns->iv_master_rank);
				D_GOTO(out_put, rc);
			} else {
				D_DEBUG(DB_MD, "update -1 iv cache %u/%u\n",
					dss_self_rank(), entry->ns->iv_master_rank);
				D_GOTO(update_iv_cache, rc);
			}
		}
	} else if (entry->iv_class->iv_class_id == IV_POOL_HDL) {
		rc = ds_pool_iv_refresh_hdl(pool, &src_iv->piv_hdl);
	}
	if (rc)
		D_GOTO(out_put, rc);

update_iv_cache:
	/* Since pool_tgt_connect/prop_update/refresh_hdl might yield due to
	 * connective operation, so it need check sp_stopping again before
	 * pool_iv_ent_copy, in case the entry has been destroyed.
	 */
	if (!pool->sp_stopping) {
		rc = pool_iv_ent_copy(key, &entry->iv_value, src_iv, true);
		if (rc == 0 && pool_key->pik_eph != 0)
			ent_pool_key->pik_eph = pool_key->pik_eph;
	}
out_put:
	D_DEBUG(DB_MD, DF_UUID": key %u rc %d\n",
		DP_UUID(entry->ns->iv_pool_uuid), key->class_id, rc);
	if (pool)
		ds_pool_put(pool);

	return rc;
}

static int
pool_iv_value_alloc(struct ds_iv_entry *entry,
		    struct ds_iv_key *key, d_sg_list_t *sgl)
{
	return pool_iv_value_alloc_internal(key, sgl);
}

static int
pool_iv_pre_sync(struct ds_iv_entry *entry, struct ds_iv_key *key,
		 d_sg_list_t *value)
{
	struct pool_iv_entry	*v = value->sg_iovs[0].iov_buf;
	struct pool_iv_key	*pool_key;
	struct ds_pool		*pool;
	struct pool_buf		*map_buf = NULL;
	int			 rc;

	/* This function is only for IV_POOL_MAP. */
	if (entry->iv_class->iv_class_id != IV_POOL_MAP)
		return 0;

	rc = ds_pool_lookup(entry->ns->iv_pool_uuid, &pool);
	if (rc != 0) {
		D_DEBUG(DB_TRACE, DF_UUID": pool not found: %d\n",
			DP_UUID(entry->ns->iv_pool_uuid), rc);
		/* Return 0 to keep forwarding this sync request. */
		if (rc == -DER_NONEXIST)
			rc = 0;
		return rc;
	}

	if (v->piv_map.piv_pool_buf.pb_nr > 0)
		map_buf = &v->piv_map.piv_pool_buf;

	pool_key = (struct pool_iv_key *)key->key_buf;
	ds_pool_iv_ns_update(pool, v->piv_map.piv_master_rank,
			     pool_key->pik_term);

	rc = ds_pool_tgt_map_update(pool, map_buf,
				    v->piv_map.piv_pool_map_ver);

	ABT_mutex_lock(pool->sp_mutex);
	ABT_cond_signal(pool->sp_fetch_hdls_cond);
	ABT_mutex_unlock(pool->sp_mutex);

	ds_pool_put(pool);
	return rc;
}

struct ds_iv_class_ops pool_iv_ops = {
	.ivc_ent_init		= pool_iv_ent_init,
	.ivc_ent_get		= pool_iv_ent_get,
	.ivc_ent_put		= pool_iv_ent_put,
	.ivc_ent_destroy	= pool_iv_ent_destroy,
	.ivc_ent_fetch		= pool_iv_ent_fetch,
	.ivc_ent_update		= pool_iv_ent_update,
	.ivc_ent_refresh	= pool_iv_ent_refresh,
	.ivc_value_alloc	= pool_iv_value_alloc,
	.ivc_pre_sync		= pool_iv_pre_sync
};

static int
pool_iv_map_fetch(void *ns)
{
	d_sg_list_t		sgl = { 0 };
	d_iov_t			iov = { 0 };
	uint32_t		pool_iv_len = 0;
	struct ds_iv_key	key;
	struct pool_iv_entry	iv_entry = { 0 };
	struct pool_iv_key	*pool_key;
	int			pb_nr = 128; /* Init tgt nr */
	int			rc;

	/* sizeof(iv_result) < pool_iv_len, so it will only fetch
	 * the pool map and cache it locally, instead of copying
	 * out from ds_iv_fetch. see ds_iv_done().
	 */
	d_iov_set(&iov, &iv_entry, sizeof(iv_entry));
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &iov;

	memset(&key, 0, sizeof(key));
	key.class_id = IV_POOL_MAP;
	pool_key = (struct pool_iv_key *)key.key_buf;
retry:
	pool_iv_len = pool_buf_size(pb_nr);
	pool_key->pik_entry_size = pool_iv_len;
	rc = ds_iv_fetch(ns, &key, &sgl, false /* retry */);
	if (rc) {
		D_ERROR("iv fetch failed "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	if (iv_entry.piv_map.piv_pool_buf.pb_target_nr == (unsigned int)(-1)) {
		D_ASSERT(iv_entry.piv_map.piv_pool_buf.pb_nr > pb_nr);
		pb_nr = iv_entry.piv_map.piv_pool_buf.pb_nr;
		D_DEBUG(DB_MD, "retry by %u\n", pb_nr);
		goto retry;
	}

	return rc;
}

static int
pool_iv_update(struct ds_iv_ns *ns, int class_id, uuid_t key_uuid,
	       struct pool_iv_entry *pool_iv,
	       uint32_t pool_iv_len, unsigned int shortcut,
	       unsigned int sync_mode, bool retry)
{
	d_sg_list_t		sgl;
	d_iov_t			iov;
	struct ds_iv_key	key;
	struct pool_iv_key	*pool_key;
	int			rc;

	iov.iov_buf = pool_iv;
	iov.iov_len = pool_iv_len;
	iov.iov_buf_len = pool_iv_len;
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &iov;

	memset(&key, 0, sizeof(key));
	key.class_id = class_id;
	pool_key = (struct pool_iv_key *)key.key_buf;
	pool_key->pik_entry_size = pool_iv_len;
	pool_key->pik_eph = d_hlc_get();
	pool_key->pik_term = ns->iv_master_term;
	uuid_copy(pool_key->pik_uuid, key_uuid);

	rc = ds_iv_update(ns, &key, &sgl, shortcut, sync_mode, 0, retry);
	if (rc)
		D_ERROR("iv update failed "DF_RC"\n", DP_RC(rc));

	return rc;
}

int
ds_pool_iv_map_update(struct ds_pool *pool, struct pool_buf *buf,
		      uint32_t map_ver)
{
	struct pool_iv_entry	*iv_entry;
	uint32_t		 iv_entry_size;
	uint32_t		 nr;
	int			 rc;

	D_DEBUG(DB_MD, DF_UUID": map_ver=%u\n", DP_UUID(pool->sp_uuid),
		map_ver);

	nr = buf != NULL ? buf->pb_nr : 0;
	iv_entry_size = pool_iv_map_ent_size(nr);
	D_ALLOC(iv_entry, iv_entry_size);
	if (iv_entry == NULL)
		return -DER_NOMEM;

	crt_group_rank(pool->sp_group, &iv_entry->piv_map.piv_master_rank);
	iv_entry->piv_map.piv_pool_map_ver =
		buf == NULL ? 0 : pool->sp_map_version;
	if (buf != NULL)
		memcpy(&iv_entry->piv_map.piv_pool_buf, buf,
		       pool_buf_size(buf->pb_nr));

	/* FIXME: Let's update the pool map synchronously for the moment,
	 * since there is no easy way to free the iv_entry buffer. Needs
	 * to revisit here once pool/cart_group/IV is upgraded.
	 */
	rc = pool_iv_update(pool->sp_iv_ns, IV_POOL_MAP, pool->sp_uuid,
			    iv_entry, iv_entry_size, CRT_IV_SHORTCUT_NONE,
			    CRT_IV_SYNC_EAGER, false);
	if (rc != 0)
		D_DEBUG(DB_MD, DF_UUID": map_ver=%u: %d\n",
			DP_UUID(pool->sp_uuid), map_ver, rc);

	D_DEBUG(DB_MD, DF_UUID": map_ver=%u: %d\n", DP_UUID(pool->sp_uuid),
		map_ver, rc);

	D_FREE(iv_entry);
	return rc;
}

int
ds_pool_iv_conn_hdl_update(struct ds_pool *pool, uuid_t hdl_uuid,
			   uint64_t flags, uint64_t sec_capas,
			   d_iov_t *cred, uint32_t global_ver, uint32_t layout_ver)
{
	struct pool_iv_entry	*iv_entry;
	daos_size_t		iv_entry_size;
	struct pool_iv_conn	*pic;
	int			rc;

	iv_entry_size = pool_iv_conn_ent_size(cred->iov_len);
	D_ALLOC(iv_entry, iv_entry_size);
	if (iv_entry == NULL)
		return -DER_NOMEM;

	iv_entry->piv_conn_hdls.pic_size = pool_iv_conn_size(cred->iov_len);
	iv_entry->piv_conn_hdls.pic_buf_size = pool_iv_conn_size(cred->iov_len);
	pic = &iv_entry->piv_conn_hdls.pic_conns[0];
	uuid_copy(pic->pic_hdl, hdl_uuid);
	pic->pic_flags = flags;
	pic->pic_capas = sec_capas;
	pic->pic_cred_size = cred->iov_len;
	pic->pic_global_ver = global_ver;
	pic->pic_obj_ver = layout_ver;
	memcpy(&pic->pic_creds[0], cred->iov_buf, cred->iov_len);

	rc = pool_iv_update(pool->sp_iv_ns, IV_POOL_CONN, hdl_uuid,
			    iv_entry, iv_entry_size, CRT_IV_SHORTCUT_NONE,
			    CRT_IV_SYNC_EAGER, false);
	D_DEBUG(DB_MD, DF_UUID" distribute hdl "DF_UUID" capas "DF_U64" %d\n",
		DP_UUID(pool->sp_uuid), DP_UUID(hdl_uuid), sec_capas, rc);

	D_FREE(iv_entry);
	return rc;
}

/* Get all pool connect handles for the pool */
int
ds_pool_iv_conn_hdl_fetch(struct ds_pool *pool)
{
	d_sg_list_t		sgl = { 0 };
	d_iov_t			iov = { 0 };
	struct pool_iv_entry	iv_entry_buf = { 0 };
	struct pool_iv_key	*pool_key;
	struct ds_iv_key	key = { 0 };
	struct pool_iv_entry	*iv_entry;
	uint32_t		iv_entry_size;
	int			rc;

	iv_entry = &iv_entry_buf;
	iv_entry_size = sizeof(iv_entry_buf);
	key.class_id = IV_POOL_CONN;
	pool_key = key2priv(&key);
retry:
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	d_iov_set(&iov, iv_entry, iv_entry_size);
	sgl.sg_iovs = &iov;
	pool_key->pik_entry_size = iv_entry_size;
	rc = ds_iv_fetch(pool->sp_iv_ns, &key, &sgl, false /* retry */);
	if (rc) {
		D_ERROR("iv fetch failed "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	if (iv_entry->piv_conn_hdls.pic_size == (uint32_t)(-1)) {
		D_ASSERT(iv_entry->piv_conn_hdls.pic_buf_size > iv_entry_size);
		iv_entry_size = iv_entry->piv_conn_hdls.pic_buf_size;
		D_ALLOC(iv_entry, iv_entry_size);
		if (iv_entry == NULL)
			D_GOTO(out, rc = -DER_NOMEM);

		D_DEBUG(DB_MD, "retry by %u\n", iv_entry_size);
		goto retry;
	}
out:
	if (iv_entry != &iv_entry_buf)
		D_FREE(iv_entry);

	return rc;
}

int
ds_pool_iv_conn_hdl_invalidate(struct ds_pool *pool, uuid_t hdl_uuid)
{
	struct ds_iv_key	key = { 0 };
	struct pool_iv_key	*pool_key = key2priv(&key);
	int			rc;

	key.class_id = IV_POOL_CONN;
	uuid_copy(pool_key->pik_uuid, hdl_uuid);
	rc = ds_iv_invalidate(pool->sp_iv_ns, &key, CRT_IV_SHORTCUT_NONE,
			      CRT_IV_SYNC_NONE, 0, false /* retry */);
	if (rc)
		D_ERROR("iv invalidate failed "DF_RC"\n", DP_RC(rc));

	return rc;
}

static int
pool_iv_map_invalidate(void *ns, unsigned int shortcut, unsigned int sync_mode)
{
	struct ds_iv_key	key = { 0 };
	int			rc;

	key.class_id = IV_POOL_MAP;
	rc = ds_iv_invalidate(ns, &key, shortcut, sync_mode, 0,
			      false /* retry */);
	if (rc)
		D_ERROR("iv invalidate failed "DF_RC"\n", DP_RC(rc));

	return rc;
}

/* ULT to refresh pool map version */
void
ds_pool_map_refresh_ult(void *arg)
{
	struct pool_map_refresh_ult_arg	*iv_arg = arg;
	struct ds_pool			*pool;
	d_rank_t			 rank;
	int				 rc = 0;

	/* Pool IV fetch should only be done in xstream 0 */
	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);
	rc = ds_pool_lookup(iv_arg->iua_pool_uuid, &pool);
	if (rc != 0) {
		D_WARN(DF_UUID" refresh pool map: %d\n", DP_UUID(iv_arg->iua_pool_uuid), rc);
		goto out;
	}

	rc = crt_group_rank(pool->sp_group, &rank);
	if (rc)
		goto out;

	if (rank == pool->sp_iv_ns->iv_master_rank) {
		D_WARN("try to refresh pool map on pool leader\n");
		goto out;
	}

	/* If there are already refresh going on, let's wait
	 * until the refresh is done.
	 */
	ABT_mutex_lock(pool->sp_mutex);
	if (pool->sp_map_version >= iv_arg->iua_pool_version &&
	    pool->sp_map != NULL &&
	    !DAOS_FAIL_CHECK(DAOS_FORCE_REFRESH_POOL_MAP)) {
		D_DEBUG(DB_TRACE, "current pool version %u >= %u\n",
			pool_map_get_version(pool->sp_map),
			iv_arg->iua_pool_version);
		goto unlock;
	}

	/* Invalidate the local pool IV cache, then call pool_iv_map_fetch to
	 * refresh local pool IV cache, which will update the local pool map
	 * see pool_iv_ent_refresh()
	 */
	rc = pool_iv_map_invalidate(pool->sp_iv_ns, CRT_IV_SHORTCUT_NONE,
				    CRT_IV_SYNC_NONE);
	if (rc)
		goto unlock;

	rc = pool_iv_map_fetch(pool->sp_iv_ns);

unlock:
	ABT_mutex_unlock(pool->sp_mutex);
out:
	if (pool != NULL)
		ds_pool_put(pool);
	if (iv_arg->iua_eventual)
		ABT_eventual_set(iv_arg->iua_eventual, (void *)&rc, sizeof(rc));
	D_FREE(iv_arg);
}

int
ds_pool_iv_srv_hdl_invalidate(struct ds_pool *pool)
{
	struct ds_iv_key	key = { 0 };
	int			rc;

	key.class_id = IV_POOL_HDL;
	rc = ds_iv_invalidate(pool->sp_iv_ns, &key, CRT_IV_SHORTCUT_NONE,
			      CRT_IV_SYNC_NONE, 0, false /* retry */);
	if (rc)
		D_ERROR("iv invalidate failed "DF_RC"\n", DP_RC(rc));

	return rc;
}

int
ds_pool_iv_srv_hdl_update(struct ds_pool *pool, uuid_t pool_hdl_uuid,
			  uuid_t cont_hdl_uuid)
{
	struct pool_iv_entry	iv_entry = { 0 };
	int			rc;

	/* Only happens on xstream 0 */
	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);

	uuid_copy(iv_entry.piv_hdl.pih_pool_hdl, pool_hdl_uuid);
	uuid_copy(iv_entry.piv_hdl.pih_cont_hdl, cont_hdl_uuid);

	rc = pool_iv_update(pool->sp_iv_ns, IV_POOL_HDL, pool_hdl_uuid,
			    &iv_entry, sizeof(struct pool_iv_entry),
			    CRT_IV_SHORTCUT_NONE, CRT_IV_SYNC_LAZY, true);

	if (rc != 0)
		D_ERROR("pool_iv_update failed "DF_RC"\n", DP_RC(rc));

	return rc;
}

int
ds_pool_iv_srv_hdl_fetch(struct ds_pool *pool, uuid_t *pool_hdl_uuid,
			 uuid_t *cont_hdl_uuid)
{
	struct pool_iv_entry	iv_entry = { 0 };
	d_sg_list_t		sgl = { 0 };
	d_iov_t			iov = { 0 };
	struct ds_iv_key	 key;
	struct pool_iv_key	*pool_key;
	int			 rc;

	d_iov_set(&iov, &iv_entry, sizeof(iv_entry));
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &iov;

	memset(&key, 0, sizeof(key));
	key.class_id = IV_POOL_HDL;
	pool_key = (struct pool_iv_key *)key.key_buf;
	pool_key->pik_entry_size = sizeof(struct pool_iv_entry);
	rc = ds_iv_fetch(pool->sp_iv_ns, &key, &sgl, false /* retry */);
	if (rc) {
		D_CDEBUG(rc == -DER_NOTLEADER || rc == -DER_SHUTDOWN,
			 DB_ANY, DLOG_ERR,
			 "iv fetch failed "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	if (pool_hdl_uuid)
		uuid_copy(*pool_hdl_uuid, iv_entry.piv_hdl.pih_pool_hdl);
	if (cont_hdl_uuid)
		uuid_copy(*cont_hdl_uuid, iv_entry.piv_hdl.pih_cont_hdl);
out:
	return rc;
}

struct srv_hdl_ult_arg {
	struct ds_pool	*pool;
	ABT_eventual	eventual;
};

static void
pool_iv_srv_hdl_fetch_ult(void *data)
{
	struct srv_hdl_ult_arg *arg = data;
	int rc;

	rc = ds_pool_iv_srv_hdl_fetch(arg->pool, NULL, NULL);

	ABT_eventual_set(arg->eventual, (void *)&rc, sizeof(rc));
}

int
ds_pool_iv_srv_hdl_fetch_non_sys(struct ds_pool *pool, uuid_t *srv_cont_hdl,
				 uuid_t *srv_pool_hdl)
{
	struct srv_hdl_ult_arg	arg;
	ABT_eventual		eventual;
	int			*status;
	int			rc;

	/* Fetch the capability from the leader. To avoid extra locks,
	 * all metadatas are maintained by xstream 0, so let's create
	 * an ULT on xstream 0 to let xstream 0 to handle capa fetch
	 * and update.
	 */
	rc = ABT_eventual_create(sizeof(*status), &eventual);
	if (rc != ABT_SUCCESS)
		return dss_abterr2der(rc);

	arg.pool = pool;
	arg.eventual = eventual;
	rc = dss_ult_create(pool_iv_srv_hdl_fetch_ult, &arg, DSS_XS_SYS,
			    0, 0, NULL);
	if (rc)
		D_GOTO(out_eventual, rc);

	rc = ABT_eventual_wait(eventual, (void **)&status);
	if (rc != ABT_SUCCESS)
		D_GOTO(out_eventual, rc = dss_abterr2der(rc));
	if (*status != 0)
		D_GOTO(out_eventual, rc = *status);

	if (srv_cont_hdl)
		uuid_copy(*srv_cont_hdl, pool->sp_srv_cont_hdl);
	if (srv_pool_hdl)
		uuid_copy(*srv_pool_hdl, pool->sp_srv_pool_hdl);

out_eventual:
	ABT_eventual_free(&eventual);
	return rc;
}

int
ds_pool_iv_prop_update(struct ds_pool *pool, daos_prop_t *prop)
{
	struct pool_iv_entry	*iv_entry = NULL;
	uint32_t		 iv_entry_size;
	struct daos_prop_entry	*prop_entry;
	d_rank_list_t		*svc_list;
	int			 rc;

	/* Only happens on xstream 0 */
	D_ASSERT(dss_get_module_info()->dmi_xs_id == 0);

	/* Serialize the prop */
	prop_entry = daos_prop_entry_get(prop, DAOS_PROP_PO_SVC_LIST);
	if (prop_entry == NULL)
		D_GOTO(out, rc = -DER_NONEXIST);

	svc_list = prop_entry->dpe_val_ptr;

	iv_entry_size = pool_iv_prop_ent_size(DAOS_ACL_MAX_ACE_LEN,
					      svc_list->rl_nr);
	D_ALLOC(iv_entry, iv_entry_size);
	if (iv_entry == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	pool_iv_prop_l2g(prop, &iv_entry->piv_prop);

	rc = pool_iv_update(pool->sp_iv_ns, IV_POOL_PROP, pool->sp_uuid,
			    iv_entry, iv_entry_size, CRT_IV_SHORTCUT_NONE,
			    CRT_IV_SYNC_LAZY, true);
	if (rc != 0)
		D_ERROR("pool_iv_update failed "DF_RC"\n", DP_RC(rc));

	D_FREE(iv_entry);
out:
	return rc;
}

/* parameter for fetching service rank list */
struct pool_svc_iv_args {
	struct ds_pool	*ia_pool;
	d_rank_list_t	*ia_svc;
};

static int
cont_pool_svc_ult(void *args)
{
	struct pool_svc_iv_args	*iv_args = args;
	struct daos_prop_entry	*entry;
	daos_prop_t		 prop;
	int			 rc;

	memset(&prop, 0, sizeof(prop));
	rc = ds_pool_iv_prop_fetch(iv_args->ia_pool, &prop);
	if (rc)
		return rc;

	entry = daos_prop_entry_get(&prop, DAOS_PROP_PO_SVC_LIST);
	D_ASSERT(entry != NULL);

	rc = d_rank_list_dup(&iv_args->ia_svc, entry->dpe_val_ptr);
	if (rc)
		D_GOTO(out, rc);
out:
	daos_prop_fini(&prop);
	return rc;
}

int
ds_pool_iv_svc_fetch(struct ds_pool *pool, d_rank_list_t **svc_p)
{
	struct pool_svc_iv_args	 ia;
	int			 rc;

	ia.ia_pool = pool;
	ia.ia_svc  = NULL;

	if (dss_get_module_info()->dmi_xs_id == 0) {
		/* OK to fetch it from xstream-0 */
		rc = cont_pool_svc_ult(&ia);
		if (rc)
			D_GOTO(failed, rc);
	} else {
		/* create a ULT and schedule it on xstream-0 */
		rc = dss_ult_execute(cont_pool_svc_ult, &ia, NULL, NULL,
				     DSS_XS_SYS, 0, 0);
		if (rc)
			D_GOTO(failed, rc);
	}
	*svc_p = ia.ia_svc;
	return 0;
failed:
	return rc;
}

int
ds_pool_iv_prop_fetch(struct ds_pool *pool, daos_prop_t *prop)
{
	daos_prop_t		*prop_fetch = NULL;
	struct pool_iv_entry	*iv_entry;
	uint32_t		 iv_entry_size;
	d_sg_list_t		 sgl = { 0 };
	d_iov_t			 iov = { 0 };
	struct ds_iv_key	 key;
	struct pool_iv_key	*pool_key;
	int			 rc;

	if (prop == NULL)
		return -DER_INVAL;

	iv_entry_size = pool_iv_prop_ent_size(DAOS_ACL_MAX_ACE_LEN,
					      PROP_SVC_LIST_MAX_TMP);
	D_ALLOC(iv_entry, iv_entry_size);
	if (iv_entry == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	d_iov_set(&iov, iv_entry, iv_entry_size);
	sgl.sg_nr = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs = &iov;

	memset(&key, 0, sizeof(key));
	key.class_id = IV_POOL_PROP;
	pool_key = (struct pool_iv_key *)key.key_buf;
	pool_key->pik_entry_size = iv_entry_size;
	rc = ds_iv_fetch(pool->sp_iv_ns, &key, &sgl, false /* retry */);
	if (rc) {
		D_ERROR("iv fetch failed "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	prop_fetch = daos_prop_alloc(DAOS_PROP_PO_NUM);
	if (prop_fetch == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = pool_iv_prop_g2l(&iv_entry->piv_prop, prop_fetch);
	if (rc) {
		D_ERROR("pool_iv_prop_g2l failed "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

	rc = daos_prop_copy(prop, prop_fetch);
	if (rc) {
		D_ERROR("daos_prop_copy failed "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc);
	}

out:
	D_FREE(iv_entry);
	daos_prop_free(prop_fetch);
	return rc;
}

int
ds_pool_iv_fini(void)
{
	ds_iv_class_unregister(IV_POOL_MAP);
	ds_iv_class_unregister(IV_POOL_PROP);
	ds_iv_class_unregister(IV_POOL_CONN);
	ds_iv_class_unregister(IV_POOL_HDL);

	return 0;
}

int
ds_pool_iv_init(void)
{
	int	rc;

	rc = ds_iv_class_register(IV_POOL_MAP, &iv_cache_ops, &pool_iv_ops);
	if (rc)
		D_GOTO(out, rc);

	rc = ds_iv_class_register(IV_POOL_PROP, &iv_cache_ops, &pool_iv_ops);
	if (rc)
		D_GOTO(out, rc);

	rc = ds_iv_class_register(IV_POOL_CONN, &iv_cache_ops, &pool_iv_ops);
	if (rc)
		D_GOTO(out, rc);

	rc = ds_iv_class_register(IV_POOL_HDL, &iv_cache_ops, &pool_iv_ops);
	if (rc)
		D_GOTO(out, rc);
out:
	if (rc)
		ds_pool_iv_fini();

	return rc;
}
