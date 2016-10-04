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
 * This file is part of daos_sr
 *
 * src/dsr/cli_obj.c
 */
#include "cli_internal.h"

#define CLI_OBJ_IO_PARMS	8

struct cli_obj_io_ctx;
typedef int (*cli_obj_io_comp_t)(struct cli_obj_io_ctx *iocx, int rc);

/** I/O context for DSR client object */
struct cli_obj_io_ctx {
	/** reference of the object */
	struct dsr_cli_obj	*cx_obj;
	/** operation group */
	struct daos_event	*cx_event;
	/** pointer to list anchor */
	void			*cx_args[CLI_OBJ_IO_PARMS];
	/** completion callback */
	cli_obj_io_comp_t	 cx_comp;

	int			cx_sync:1;
};

struct cli_obj_io_oper {
	daos_handle_t		 oo_oh;
	daos_event_t		*oo_ev;
};

static struct dsr_cli_obj *
cli_obj_alloc(void)
{
	struct dsr_cli_obj *obj;

	D_ALLOC_PTR(obj);
	if (obj == NULL)
		return NULL;

	obj->cob_ref = 1;
	return obj;
}

static void
cli_obj_free(struct dsr_cli_obj *obj)
{
	struct pl_obj_layout *layout;
	int		      i;

	layout = obj->cob_layout;
	if (layout == NULL)
		goto out;

	if (obj->cob_mohs != NULL) {
		for (i = 0; i < layout->ol_nr; i++) {
			if (!daos_handle_is_inval(obj->cob_mohs[i]))
				dsr_shard_obj_close(obj->cob_mohs[i], NULL);
		}
		D_FREE(obj->cob_mohs, layout->ol_nr * sizeof(*obj->cob_mohs));
	}

	pl_obj_layout_free(layout);
 out:
	D_FREE_PTR(obj);
}

/* TODO use real handle for the client objects */

static void
cli_obj_decref(struct dsr_cli_obj *obj)
{
	obj->cob_ref--;
	if (obj->cob_ref == 0)
		cli_obj_free(obj);
}

static void
cli_obj_addref(struct dsr_cli_obj *obj)
{
	obj->cob_ref++;
}

static daos_handle_t
cli_obj2hdl(struct dsr_cli_obj *obj)
{
	daos_handle_t oh;

	oh.cookie = (uint64_t)obj;
	return oh;
}

static struct dsr_cli_obj *
cli_hdl2obj(daos_handle_t oh)
{
	struct dsr_cli_obj *obj;

	obj = (struct dsr_cli_obj *)oh.cookie;
	cli_obj_addref(obj);
	return obj;
}

static void
cli_obj_hdl_link(struct dsr_cli_obj *obj)
{
	cli_obj_addref(obj);
}

static void
cli_obj_hdl_unlink(struct dsr_cli_obj *obj)
{
	cli_obj_decref(obj);
}

/**
 * Open an object shard (dsr shard object), cache the open handle.
 */
static int
cli_obj_open_shard(struct dsr_cli_obj *obj, unsigned int shard,
		   daos_handle_t *oh)
{
	struct pl_obj_layout	*layout;
	int			 rc = 0;

	layout = obj->cob_layout;

	/* Skip the invalid shards and targets */
	if (layout->ol_shards[shard] == -1 ||
	    layout->ol_targets[shard] == -1)
		return -DER_NONEXIST;

	/* XXX could be otherwise for some object classes? */
	D_ASSERT(layout->ol_shards[shard] == shard);

	D_DEBUG(DF_SRC, "Open object shard %d\n", shard);
	if (daos_handle_is_inval(obj->cob_mohs[shard])) {
		daos_unit_oid_t	 oid;

		memset(&oid, 0, sizeof(oid));
		oid.id_shard = shard;
		oid.id_pub   = obj->cob_md.omd_id;
		/* NB: dsr open is a local operation, so it is ok to call
		 * it in sync mode, at least for now.
		 */
		rc = dsr_shard_obj_open(obj->cob_coh,
					layout->ol_targets[shard],
					oid, obj->cob_mode,
					&obj->cob_mohs[shard], NULL);
	}

	if (rc == 0)
		*oh = obj->cob_mohs[shard];

	return rc;
}

static int
cli_obj_iocx_comp(void *args, struct daos_event *ev, int rc)
{
	struct cli_obj_io_ctx	*iocx = args;

	D_DEBUG(DF_SRC, "iocx completion.\n");
	if (iocx->cx_comp)
		iocx->cx_comp(iocx, rc);

	if (iocx->cx_obj != NULL)
		cli_obj_decref(iocx->cx_obj);

	/* Let's destroy all os children created by cli_obj */
	daos_event_destroy_children(iocx->cx_event, 1);

	D_FREE_PTR(iocx);
	return rc;
}

/** Initialise I/O context for a client object */
static int
cli_obj_iocx_create(daos_handle_t oh, daos_event_t *ev,
		    struct cli_obj_io_ctx **iocx_pp)
{
	struct cli_obj_io_ctx	*iocx;
	int			 rc;

	D_ALLOC_PTR(iocx);
	if (iocx == NULL)
		return -DER_NOMEM;

	iocx->cx_obj = cli_hdl2obj(oh);
	if (iocx->cx_obj == NULL)
		D_GOTO(failed, rc = -DER_NO_HDL);
	iocx->cx_event = ev;
	if (ev != NULL) {
		rc = daos_event_register_comp_cb(ev, cli_obj_iocx_comp,
						 iocx);
		if (rc != 0)
			D_GOTO(failed, rc);
	}

	*iocx_pp = iocx;
	return 0;
 failed:
	cli_obj_iocx_comp((void *)iocx, NULL, rc);
	return rc;
}

static void
cli_obj_iocx_destroy(struct cli_obj_io_ctx *iocx, int rc)
{
	if (iocx->cx_sync) {
		if (iocx->cx_event != NULL) {
			daos_event_abort(iocx->cx_event);
			daos_event_destroy(iocx->cx_event, 1);
			iocx->cx_event = NULL;
		}
	}
}

static int
cli_obj_iocx_launch(struct cli_obj_io_ctx *iocx)
{
	int rc;

	if (iocx->cx_event == NULL)
		return 0;

	rc = daos_event_launch(iocx->cx_event);
	if (rc != 0)
		return rc;

	if (iocx->cx_sync) {
		/* Wait the event to complete */
		struct daos_event *event = iocx->cx_event;

		D_ASSERT(event != NULL);
		rc = daos_event_test(event, DAOS_EQ_WAIT);
		daos_event_fini(event);
		D_FREE_PTR(event);
	}

	return rc;
}

static int
cli_obj_iocx_new_oper(struct cli_obj_io_ctx *iocx, unsigned int shard,
		      struct cli_obj_io_oper *oper)
{
	daos_handle_t	oh;
	int		rc;

	memset(oper, 0, sizeof(*oper));
	rc = cli_obj_open_shard(iocx->cx_obj, shard, &oh);
	if (rc != 0)
		return rc;

	if (iocx->cx_event == NULL) {
		iocx->cx_sync = 1;

		D_ALLOC_PTR(iocx->cx_event);
		if (iocx->cx_event == NULL)
			return -DER_NOMEM;

		rc = daos_event_init(iocx->cx_event, DAOS_HDL_INVAL, NULL);
		if (rc != 0) {
			D_FREE_PTR(iocx->cx_event);
			return rc;
		}

		rc = daos_event_register_comp_cb(iocx->cx_event,
						 cli_obj_iocx_comp,
						 iocx);
		if (rc != 0)
			return rc;
	}

	D_ALLOC_PTR(oper->oo_ev);
	if (oper->oo_ev == NULL)
		return -DER_NOMEM;

	rc = daos_event_init(oper->oo_ev, DAOS_HDL_INVAL, iocx->cx_event);
	if (rc != 0) {
		/* In the failed case, we don't need to close the opened shard
		 * because we want to cache the open handle anyway.
		 */
		D_FREE_PTR(oper->oo_ev);
		return rc;
	}
	oper->oo_oh = oh;
	return 0;
}

int
dsr_obj_declare(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
		dsr_obj_attr_t *oa, daos_event_t *ev)
{
	struct daos_oclass_attr *oc_attr;
	int			 rc;

	/* XXX Only support internal classes for now */
	oc_attr = dsr_oclass_attr_find(oid);
	rc = oc_attr != NULL ? 0 : -DER_INVAL;

	if (rc == 0 && ev != NULL) {
		daos_event_launch(ev);
		daos_event_complete(ev, 0);
	}
	return rc;
}

static int
cli_obj_md_fetch(daos_obj_id_t oid, struct dsr_obj_md *md, daos_event_t *ev)
{
	/* For predefined object classes, do nothing at here. But for those
	 * customized classes, we need to fetch for the remote OI table.
	 */
	memset(md, 0, sizeof(*md));
	md->omd_id = oid;
	return 0;
}

int
dsr_obj_open(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch,
	     unsigned int mode, daos_handle_t *oh, daos_event_t *ev)
{
	struct dsr_cli_obj	*obj;
	struct pl_obj_layout	*layout;
	struct pl_map		*map;
	int			 i;
	int			 nr;
	int			 rc;

	obj = cli_obj_alloc();
	if (obj == NULL)
		return -DER_NOMEM;

	obj->cob_coh  = coh;
	obj->cob_mode = mode;

	/* it is a local operation for now, does not require event */
	rc = cli_obj_md_fetch(oid, &obj->cob_md, NULL);
	if (rc != 0)
		D_GOTO(out, rc);

	map = dsr_pl_map_find(coh, oid);
	if (map == NULL) {
		D_DEBUG(DF_SRC, "Cannot find valid placement map\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	rc = pl_obj_place(map, &obj->cob_md, NULL, &layout);
	if (rc != 0) {
		D_DEBUG(DF_SRC, "Failed to generate object layout\n");
		D_GOTO(out, rc);
	}
	D_DEBUG(DF_SRC, "Place object on %d targets\n", layout->ol_nr);

	obj->cob_layout = layout;
	nr = layout->ol_nr;

	D_ALLOC(obj->cob_mohs, nr * sizeof(*obj->cob_mohs));
	if (obj->cob_mohs == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	for (i = 0; i < nr; i++)
		obj->cob_mohs[i] = DAOS_HDL_INVAL;

	cli_obj_hdl_link(obj);
	*oh = cli_obj2hdl(obj);
 out:
	cli_obj_decref(obj);
	if (rc == 0 && ev != NULL) {
		daos_event_launch(ev);
		daos_event_complete(ev, 0);
	}
	return rc;
}

int
dsr_obj_close(daos_handle_t oh, daos_event_t *ev)
{
	struct dsr_cli_obj   *obj;

	obj = cli_hdl2obj(oh);
	if (obj == NULL)
		return -DER_NO_HDL;

	cli_obj_hdl_unlink(obj);
	cli_obj_decref(obj);

	if (ev != NULL) {
		daos_event_launch(ev);
		daos_event_complete(ev, 0);
	}
	return 0;
}

int
dsr_obj_punch(daos_handle_t oh, daos_epoch_t epoch, daos_event_t *ev)
{
	D_ERROR("Unsupported API\n");
	return -DER_NOSYS;
}

int
dsr_obj_query(daos_handle_t oh, daos_epoch_t epoch, dsr_obj_attr_t *oa,
	      daos_rank_list_t *ranks, daos_event_t *ev)
{
	D_ERROR("Unsupported API\n");
	return -DER_NOSYS;
}

static int
cli_obj_get_grp_size(struct dsr_cli_obj *obj)
{
	struct daos_oclass_attr *oc_attr;

	oc_attr = dsr_oclass_attr_find(obj->cob_md.omd_id);
	D_ASSERT(oc_attr != NULL);
	return dsr_oclass_grp_size(oc_attr);
}

static unsigned int
cli_obj_dkey2shard(struct dsr_cli_obj *obj, daos_dkey_t *dkey,
		   unsigned int *shards_cnt,
		   unsigned int *random_shard)
{
	int			grp_size;
	uint64_t		hash;
	uint64_t		start_shard;
	uint64_t		adjust_grp_size;

	grp_size = cli_obj_get_grp_size(obj);
	D_ASSERT(grp_size > 0);

	hash = daos_hash_murmur64((unsigned char *)dkey->iov_buf,
				  dkey->iov_len, 5731);

	if (obj->cob_layout->ol_nr < grp_size)
		adjust_grp_size = obj->cob_layout->ol_nr;
	else
		adjust_grp_size = grp_size;

	/* XXX, consistent hash? */
	start_shard = hash % (obj->cob_layout->ol_nr / adjust_grp_size);

	if (shards_cnt != NULL)
		*shards_cnt = adjust_grp_size;

	/* Return an random shard among the group for fetch. */
	/* XXX check if the target of the shard is avaible. */
	if (random_shard != NULL) {
		uint32_t idx = hash % grp_size + start_shard;
		int i;

		*random_shard = -1;
		for (i = 0; i < grp_size; i++) {
			if (obj->cob_layout->ol_shards[idx] != -1) {
				*random_shard = idx;
				break;
			}

			if (idx < start_shard + grp_size)
				idx++;
			else
				idx = start_shard;
		}
		D_ASSERT(*random_shard != -1);
	}

	return (unsigned int)start_shard;
}

int
dsr_obj_fetch(daos_handle_t oh, daos_epoch_t epoch, daos_dkey_t *dkey,
	      unsigned int nr, daos_vec_iod_t *iods, daos_sg_list_t *sgls,
	      daos_vec_map_t *maps, daos_event_t *ev)
{
	struct cli_obj_io_ctx	*iocx;
	struct cli_obj_io_oper	 oper;
	unsigned int		 shard;
	int			 rc;

	rc = cli_obj_iocx_create(oh, ev, &iocx);
	if (rc != 0)
		return rc;

	shard = cli_obj_dkey2shard(iocx->cx_obj, dkey, NULL, &shard);
	rc = cli_obj_iocx_new_oper(iocx, shard, &oper);
	if (rc != 0)
		D_GOTO(failed, rc);

	rc = dsr_shard_obj_fetch(oper.oo_oh, epoch, dkey, nr, iods, sgls,
				 maps, oper.oo_ev);
	if (rc != 0) {
		D_DEBUG(DF_SRC, "Failed to fetch data from DSM: %d\n", rc);
		D_GOTO(failed, rc);
	}

	rc = cli_obj_iocx_launch(iocx);
	if (rc != 0)
		D_GOTO(failed, rc);

	return 0;
 failed:
	cli_obj_iocx_destroy(iocx, rc);
	return rc;
}

#define TMP_OPER_CNT	6
int
dsr_obj_update(daos_handle_t oh, daos_epoch_t epoch, daos_dkey_t *dkey,
	       unsigned int nr, daos_vec_iod_t *iods, daos_sg_list_t *sgls,
	       daos_event_t *ev)
{
	struct cli_obj_io_ctx	*iocx;
	struct cli_obj_io_oper	 tmp_oper[6];
	struct cli_obj_io_oper	 *oper;
	unsigned int		 shard;
	unsigned int		 shards_cnt;
	int			 i;
	int			 rc;

	rc = cli_obj_iocx_create(oh, ev, &iocx);
	if (rc != 0)
		return rc;

	shard = cli_obj_dkey2shard(iocx->cx_obj, dkey, &shards_cnt, NULL);
	if (rc != 0)
		D_GOTO(failed, rc);

	if (shards_cnt > TMP_OPER_CNT) {
		D_ALLOC(oper, shards_cnt * sizeof(*oper));
		if (oper == NULL)
			D_GOTO(failed, rc = -DER_NOMEM);
	} else {
		oper = tmp_oper;
	}

	D_DEBUG(DF_MISC, "update "DF_OID" start %u cnt %u\n",
		DP_OID(iocx->cx_obj->cob_md.omd_id), shard,
		shards_cnt);

	for (i = 0; i < shards_cnt; i++, shard++, oper++) {
		rc = cli_obj_iocx_new_oper(iocx, shard, oper);
		if (rc != 0) {
			if (rc == -DER_NONEXIST)
				continue;
			D_GOTO(failed, rc);
		}

		rc = dsr_shard_obj_update(oper->oo_oh, epoch, dkey,
					  nr, iods, sgls, oper->oo_ev);
		if (rc != 0)
			D_GOTO(failed, rc);
	}

	rc = cli_obj_iocx_launch(iocx);
	if (rc != 0)
		D_GOTO(failed, rc);

free:
	if (oper != NULL && shards_cnt > TMP_OPER_CNT)
		D_FREE(oper, shards_cnt * sizeof(*oper));

	return rc;

failed:
	cli_obj_iocx_destroy(iocx, rc);
	D_GOTO(free, rc);
}

static int
cli_obj_list_dkey_comp(struct cli_obj_io_ctx *ctx, int rc)
{
	struct dsr_cli_obj	*obj	 = ctx->cx_obj;
	daos_hash_out_t		*anchor  = (daos_hash_out_t *)ctx->cx_args[0];
	uint32_t		shard   = (unsigned long)ctx->cx_args[1];
	int			grp_size;
	unsigned int		enc_shard_at;

	if (rc != 0)
		return rc;

	grp_size = cli_obj_get_grp_size(obj);
	D_ASSERT(grp_size > 0);

	/* XXX This is a nasty workaround: shard is encoded in the highest
	 * four bytes of the hash anchor. It is ok for now because VOS does
	 * not use those bytes. We need a cleaner way to store shard index.
	 */
	enc_shard_at = sizeof(anchor->body) - sizeof(shard);

	if (!daos_hash_is_eof(anchor)) {
		D_DEBUG(DF_SRC, "More keys in shard %d\n", shard);
		memcpy(&anchor->body[enc_shard_at], &shard, sizeof(shard));
	} else if (shard < obj->cob_layout->ol_nr - grp_size) {
		shard += grp_size;
		D_DEBUG(DF_SRC, "Enumerate the next shard %d\n", shard);

		memset(anchor, 0, sizeof(*anchor));
		memcpy(&anchor->body[enc_shard_at], &shard, sizeof(shard));
	} else {
		D_DEBUG(DF_SRC, "Enumerated All shards\n");
	}
	return rc;
}

int
dsr_obj_list_dkey(daos_handle_t oh, daos_epoch_t epoch, uint32_t *nr,
		  daos_key_desc_t *kds, daos_sg_list_t *sgl,
		  daos_hash_out_t *anchor, daos_event_t *ev)
{
	struct cli_obj_io_ctx	*iocx;
	struct cli_obj_io_oper	 oper;
	uint32_t		 shard;
	int			 rc;
	int			 enc_shard_at;

	rc = cli_obj_iocx_create(oh, ev, &iocx);
	if (rc != 0)
		return rc;

	enc_shard_at = sizeof(anchor->body) - sizeof(shard);

	memcpy(&shard, &anchor->body[enc_shard_at], sizeof(shard));
	memset(&anchor->body[enc_shard_at], 0, sizeof(shard));

	D_DEBUG(DF_SRC, "Enumerate keys in shard %d\n", shard);

	iocx->cx_args[0] = (void *)anchor;
	iocx->cx_args[1] = (void *)(unsigned long)shard;
	iocx->cx_comp	 = cli_obj_list_dkey_comp;

	rc = cli_obj_iocx_new_oper(iocx, shard, &oper);
	if (rc != 0)
		D_GOTO(failed, rc);

	rc = dsr_shard_obj_list_dkey(oper.oo_oh, epoch, nr, kds,
				     sgl, anchor, oper.oo_ev);
	if (rc != 0)
		D_GOTO(failed, rc);

	rc = cli_obj_iocx_launch(iocx);
	if (rc != 0)
		D_GOTO(failed, rc);

	return 0;
 failed:
	cli_obj_iocx_destroy(iocx, rc);
	return rc;
}

int
dsr_obj_list_akey(daos_handle_t oh, daos_epoch_t epoch, daos_dkey_t *dkey,
		  uint32_t *nr, daos_key_desc_t *kds, daos_sg_list_t *sgl,
		  daos_hash_out_t *anchor, daos_event_t *ev)
{
	D_ERROR("Unsupported API.\n");
	return -DER_NOSYS;
}
