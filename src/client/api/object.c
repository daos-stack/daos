/**
 * (C) Copyright 2015-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(client)

#include <daos/object.h>
#include <daos/task.h>
#include <daos/container.h>
#include "client_internal.h"
#include "task_internal.h"

int
daos_obj_register_class(daos_handle_t coh, daos_oclass_id_t cid,
			struct daos_oclass_attr *cattr, daos_event_t *ev)
{
	D_ERROR("Unsupported API\n");
	return -DER_NOSYS;
}

int
daos_obj_query_class(daos_handle_t coh, daos_oclass_id_t cid,
		     struct daos_oclass_attr *cattr, daos_event_t *ev)
{
	D_ERROR("Unsupported API\n");
	return -DER_NOSYS;
}

int
daos_obj_list_class(daos_handle_t coh, struct daos_oclass_list *clist,
		    daos_anchor_t *anchor, daos_event_t *ev)
{
	D_ERROR("Unsupported API\n");
	return -DER_NOSYS;
}

int
daos_obj_open(daos_handle_t coh, daos_obj_id_t oid, unsigned int mode,
	      daos_handle_t *oh, daos_event_t *ev)
{
	tse_task_t *task;
	int rc;

	rc = dc_obj_open_task_create(coh, oid, mode, oh, ev, NULL, &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

int
daos_obj_close(daos_handle_t oh, daos_event_t *ev)
{
	tse_task_t	*task;
	int		rc;

	rc = dc_obj_close_task_create(oh, ev, NULL, &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

int
daos_obj_punch(daos_handle_t oh, daos_handle_t th, uint64_t flags,
	       daos_event_t *ev)
{
	tse_task_t	*task;
	int		rc;

	rc = dc_obj_punch_task_create(oh, th, flags, ev, NULL, &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

int
daos_obj_punch_dkeys(daos_handle_t oh, daos_handle_t th, uint64_t flags,
		     unsigned int nr, daos_key_t *dkeys, daos_event_t *ev)
{
	tse_task_t	*task;
	int		rc;

	if (nr != 1) {
		/* TODO: create multiple tasks for punch of multiple dkeys */
		D_ERROR("Can't punch multiple dkeys for now\n");
		return -DER_INVAL;
	}

	rc = dc_obj_punch_dkeys_task_create(oh, th, flags, nr, dkeys, ev, NULL,
					    &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

int
daos_obj_punch_akeys(daos_handle_t oh, daos_handle_t th, uint64_t flags,
		     daos_key_t *dkey, unsigned int nr, daos_key_t *akeys,
		     daos_event_t *ev)
{
	tse_task_t	*task;
	int		rc;

	rc = dc_obj_punch_akeys_task_create(oh, th, flags, dkey, nr, akeys, ev,
					    NULL, &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

int
daos_obj_query(daos_handle_t oh, struct daos_obj_attr *oa, d_rank_list_t *ranks,
	       daos_event_t *ev)
{
	D_ERROR("Unsupported API\n");
	return -DER_NOSYS;
}

int
daos_obj_query_key(daos_handle_t oh, daos_handle_t th, uint64_t flags,
		   daos_key_t *dkey, daos_key_t *akey, daos_recx_t *recx,
		   daos_event_t *ev)
{
	tse_task_t	*task;
	int		rc;

	rc = dc_obj_query_key_task_create(oh, th, flags, dkey, akey, recx,
					  ev, NULL, &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

int
daos_obj_query_max_epoch(daos_handle_t oh, daos_handle_t th, daos_epoch_t *epoch, daos_event_t *ev)
{
	tse_task_t	*task;
	int		rc;

	*epoch = 0;
	rc = dc_obj_query_max_epoch_task_create(oh, th, epoch, ev, NULL, &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

int
daos_obj_fetch(daos_handle_t oh, daos_handle_t th, uint64_t flags,
	       daos_key_t *dkey, unsigned int nr, daos_iod_t *iods,
	       d_sg_list_t *sgls, daos_iom_t *maps, daos_event_t *ev)
{
	tse_task_t	*task;
	int		 rc;

	rc = dc_obj_fetch_task_create(oh, th, flags, dkey, nr, 0, iods,
				      sgls, maps, NULL, NULL, ev, NULL, &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

int
daos_obj_update(daos_handle_t oh, daos_handle_t th, uint64_t flags,
		daos_key_t *dkey, unsigned int nr, daos_iod_t *iods,
		d_sg_list_t *sgls, daos_event_t *ev)
{
	tse_task_t	*task;
	int		rc;

	rc = dc_obj_update_task_create(oh, th, flags, dkey, nr, iods, sgls,
				       ev, NULL, &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

int
daos_obj_list_dkey(daos_handle_t oh, daos_handle_t th, uint32_t *nr,
		   daos_key_desc_t *kds, d_sg_list_t *sgl,
		   daos_anchor_t *anchor, daos_event_t *ev)
{
	tse_task_t	*task;
	int		rc;

	rc = dc_obj_list_dkey_task_create(oh, th, nr, kds, sgl, anchor, ev,
					  NULL, &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

int
daos_obj_list_akey(daos_handle_t oh, daos_handle_t th, daos_key_t *dkey,
		   uint32_t *nr, daos_key_desc_t *kds, d_sg_list_t *sgl,
		   daos_anchor_t *anchor, daos_event_t *ev)
{
	tse_task_t	*task;
	int		rc;

	rc = dc_obj_list_akey_task_create(oh, th, dkey, nr, kds, sgl, anchor,
					  ev, NULL, &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

int
daos_obj_list_recx(daos_handle_t oh, daos_handle_t th, daos_key_t *dkey,
		   daos_key_t *akey, daos_size_t *size, uint32_t *nr,
		   daos_recx_t *recxs, daos_epoch_range_t *eprs,
		   daos_anchor_t *anchor, bool incr_order, daos_event_t *ev)
{
	tse_task_t	*task;
	int		rc;

	rc = dc_obj_list_recx_task_create(oh, th, dkey, akey, DAOS_IOD_ARRAY,
					  size, nr, recxs, eprs, anchor,
					  incr_order, ev, NULL, &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

/* Use to query the object layout */
int
daos_obj_layout_get(daos_handle_t coh, daos_obj_id_t oid,
		    struct daos_obj_layout **layout)
{
	daos_handle_t	oh;
	int		rc;

	rc = daos_obj_open(coh, oid, 0, &oh, NULL);
	if (rc)
		return rc;

	rc = dc_obj_layout_get(oh, layout);

	daos_obj_close(oh, NULL);
	if (rc != 0 && *layout != NULL)
		daos_obj_layout_free(*layout);

	return rc;
}

int
daos_obj_verify(daos_handle_t coh, daos_obj_id_t oid, daos_epoch_t epoch)
{
	tse_task_t	*task;
	daos_handle_t	 oh;
	daos_epoch_t	*epochs_p = NULL;
	int		 epoch_nr = 0;
	int		 rc;

	rc = daos_obj_open(coh, oid, 0, &oh, NULL);
	if (rc != 0)
		return rc;

	/* Sync object against the given @epoch. */
	rc = dc_obj_sync_task_create(oh, epoch, &epochs_p, &epoch_nr,
				     NULL, NULL, &task);
	if (rc == 0) {
		rc = dc_task_schedule(task, true);
		if (rc == 0)
			rc = dc_obj_verify(oh, epochs_p, epoch_nr);
		if (rc == -DER_NONEXIST)
			rc = 0;
	}

	D_FREE(epochs_p);
	daos_obj_close(oh, NULL);
	return rc;
}

int
daos_obj_anchor_split(daos_handle_t oh, uint32_t *nr, daos_anchor_t *anchors)
{
	struct daos_obj_layout	*layout;
	int			rc;

	if (nr == NULL)
		return -DER_INVAL;

	rc = dc_obj_layout_get(oh, &layout);
	if (rc)
		return rc;

	/** TBD - support more than per shard iteration */
	if (*nr != 0 && *nr != layout->ol_nr) {
		D_ERROR("For now, num anchors should be the same as what is"
			" reported as optimal\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	*nr = layout->ol_nr;

	if (anchors) {
		int		grp_size;
		uint32_t	i;

		rc = dc_obj_get_grp_size(oh, &grp_size);
		if (rc)
			D_GOTO(out, rc);

		for (i = 0; i < layout->ol_nr; i++) {
			daos_anchor_set_zero(&anchors[i]);
			dc_obj_shard2anchor(&anchors[i], i * grp_size);
			daos_anchor_set_flags(&anchors[i], DIOF_TO_SPEC_SHARD);
		}
	}
out:
	daos_obj_layout_free(layout);
	return rc;
}

int
daos_obj_anchor_set(daos_handle_t oh, uint32_t index, daos_anchor_t *anchor)
{
	int	grp_size;
	int	rc;

	rc = dc_obj_get_grp_size(oh, &grp_size);
	if (rc)
		return rc;

	/** TBD - support more than per shard iteration */
	daos_anchor_set_zero(anchor);
	dc_obj_shard2anchor(anchor, index * grp_size);
	daos_anchor_set_flags(anchor, DIOF_TO_SPEC_SHARD);

	return 0;
}

int
daos_oit_open(daos_handle_t coh, daos_epoch_t epoch,
	      daos_handle_t *oh, daos_event_t *ev)
{
	tse_task_t	*task;
	daos_obj_id_t	 oid;
	uint32_t	 cont_rf;
	int		 rc;

	rc = dc_cont_hdl2redunfac(coh, &cont_rf);
	if (rc) {
		D_ERROR("dc_cont_hdl2redunfac failed, "DF_RC"\n", DP_RC(rc));
		return rc;
	}

	oid = daos_oit_gen_id(epoch, cont_rf);
	rc = dc_obj_open_task_create(coh, oid, DAOS_OO_RW, oh, ev, NULL, &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

int
daos_oit_close(daos_handle_t oh, daos_event_t *ev)
{
	tse_task_t	*task;
	int		 rc;

	rc = dc_obj_close_task_create(oh, ev, NULL, &task);
	if (rc)
		return rc;

	return dc_task_schedule(task, true);
}

/* OI table enumeration args */
struct oit_args {
	daos_key_desc_t		*oa_kds;
	d_sg_list_t		 oa_sgl;
	daos_key_t		 oa_dkey;
	/* A bucket is just an integer dkey for OI object, the current
	 * implementation only has one bucket (dkey).
	 */
	uint32_t		 oa_bucket;
	int			 oa_nr;
};

static int
oit_list_cb(tse_task_t *task, void *args)
{
	struct oit_args	*oa = *(struct oit_args **)args;

	d_sgl_fini(&oa->oa_sgl, false);
	D_FREE(oa->oa_kds);
	D_FREE(oa);
	return 0;
}

int
daos_oit_list(daos_handle_t oh, daos_obj_id_t *oids, uint32_t *oids_nr,
	      daos_anchor_t *anchor, daos_event_t *ev)
{
	struct oit_args	*oa;
	tse_task_t	*task;
	int		 i;
	int		 rc;

	if (daos_handle_is_inval(oh) ||
	    oids == NULL || oids_nr == NULL || *oids_nr <= 0 || !anchor)
		return -DER_INVAL;

	D_ALLOC_PTR(oa);
	if (!oa)
		return -DER_NOMEM;

	oa->oa_nr = *oids_nr;
	D_ALLOC_ARRAY(oa->oa_kds, oa->oa_nr);
	if (!oa->oa_kds)
		D_GOTO(failed, rc = -DER_NOMEM);

	rc = d_sgl_init(&oa->oa_sgl, oa->oa_nr);
	if (rc)
		D_GOTO(failed, rc = -DER_NOMEM);

	for (i = 0; i < oa->oa_nr; i++)
		d_iov_set(&oa->oa_sgl.sg_iovs[i], &oids[i], sizeof(oids[i]));

	/* all OIDs are stored under one dkey for now */
	d_iov_set(&oa->oa_dkey, &oa->oa_bucket, sizeof(oa->oa_bucket));
	rc = dc_obj_list_akey_task_create(oh, DAOS_TX_NONE, &oa->oa_dkey,
					  oids_nr, oa->oa_kds, &oa->oa_sgl,
					  anchor, ev, NULL, &task);
	if (rc)
		D_GOTO(failed, rc);

	rc = tse_task_register_comp_cb(task, oit_list_cb, &oa, sizeof(oa));
	if (rc) {
		tse_task_complete(task, rc);
		D_GOTO(failed, rc);
	}

	return dc_task_schedule(task, true);

failed:
	/* NB: OK to call with empty sgl */
	d_sgl_fini(&oa->oa_sgl, false);
	if (oa->oa_kds)
		D_FREE(oa->oa_kds);
	D_FREE(oa);
	return rc;
}

struct oit_mark_arg {
	void		*oma_buf;
	d_sg_list_t	*oma_sgl;
	daos_iod_t	*oma_iod;
	daos_obj_id_t	 oma_oid;
	daos_key_t	 oma_dkey;
	uint32_t	 oma_bid;
};

static void
oit_mark_arg_free(struct oit_mark_arg *arg)
{
	if (arg == NULL)
		return;
	D_FREE(arg);
}

static struct oit_mark_arg *
oit_mark_arg_alloc(daos_obj_id_t oid, d_iov_t *marker)
{
	struct oit_mark_arg	*arg;
	void			*buf, *ptr, *val;
	d_iov_t			*iov;
	size_t			 val_len, buf_len;

	val_len = DAOS_OIT_MARKER_MAX_LEN + DAOS_OIT_DEFAULT_VAL_LEN;
	buf_len = roundup(sizeof(struct oit_mark_arg), 8) + sizeof(d_sg_list_t) + sizeof(d_iov_t) +
		  roundup(sizeof(daos_iod_t), 8) + roundup(val_len, 8);

	D_ALLOC(buf, buf_len);
	if (buf == NULL)
		return NULL;

	ptr = buf;
	arg = (struct oit_mark_arg *)ptr;
	arg->oma_buf = buf;
	arg->oma_oid = oid;
	arg->oma_bid = 0;
	DAOS_OIT_DKEY_SET(&arg->oma_dkey, &arg->oma_bid);
	ptr += roundup(sizeof(struct oit_mark_arg), 8);
	arg->oma_sgl = (d_sg_list_t *)ptr;
	ptr += sizeof(d_sg_list_t);
	iov = (d_iov_t *)ptr;
	ptr += sizeof(d_iov_t);
	arg->oma_iod = (daos_iod_t *)ptr;
	ptr += roundup(sizeof(daos_iod_t), 8);
	val = ptr;

	arg->oma_sgl->sg_nr = arg->oma_sgl->sg_nr_out = 1;
	arg->oma_sgl->sg_iovs = iov;
	if (marker != NULL) {
		D_ASSERT(marker->iov_len <= DAOS_OIT_MARKER_MAX_LEN);
		memcpy(val + DAOS_OIT_DEFAULT_VAL_LEN, marker->iov_buf, marker->iov_len);
		d_iov_set(iov, val, DAOS_OIT_DEFAULT_VAL_LEN + marker->iov_len);
	} else {
		d_iov_set(iov, val, DAOS_OIT_DEFAULT_VAL_LEN);
	}

	DAOS_OIT_AKEY_SET(&arg->oma_iod->iod_name, &arg->oma_oid);
	arg->oma_iod->iod_type	= DAOS_IOD_SINGLE;
	arg->oma_iod->iod_size	= iov->iov_len;
	arg->oma_iod->iod_nr	= 1;

	return arg;
}

static int
oit_mark_cb(tse_task_t *task, void *data)
{
	struct oit_mark_arg	*arg = *((struct oit_mark_arg **)data);

	oit_mark_arg_free(arg);
	return 0;
}

int
daos_oit_mark(daos_handle_t oh, daos_obj_id_t oid, d_iov_t *marker, daos_event_t *ev)
{
	struct oit_mark_arg	*arg = NULL;
	tse_task_t		*task;
	int			 rc;

	if (daos_handle_is_inval(oh) ||
	    (marker != NULL && (marker->iov_buf == NULL || marker->iov_len == 0 ||
	     marker->iov_len > marker->iov_buf_len || marker->iov_len > DAOS_OIT_MARKER_MAX_LEN)))
		return -DER_INVAL;

	arg = oit_mark_arg_alloc(oid, marker);
	if (arg == NULL)
		return -DER_NOMEM;

	rc = dc_obj_update_task_create(oh, DAOS_TX_NONE, DAOS_COND_AKEY_UPDATE, &arg->oma_dkey,
				       1, arg->oma_iod, arg->oma_sgl, ev, NULL, &task);
	if (rc) {
		oit_mark_arg_free(arg);
		return rc;
	}

	rc = tse_task_register_comp_cb(task, oit_mark_cb, &arg, sizeof(arg));
	if (rc) {
		oit_mark_arg_free(arg);
		tse_task_complete(task, rc);
		return 0;
	}

	return dc_task_schedule(task, true);
}

struct oit_filter_arg {
	daos_handle_t		 oa_oh;
	daos_obj_id_t		*oa_oids;
	uint32_t		*oa_oids_nr;
	daos_event_t		*oa_ev;
	daos_oit_filter_cb	*oa_filter;
	daos_key_desc_t		*oa_kds;
	d_sg_list_t		 oa_sgl;
	daos_key_t		 oa_dkey;
	uint32_t		 oa_bucket;
	int			 oa_nr;
	void			*oa_fbuf;
	daos_iod_t		*oa_fiods;
	d_sg_list_t		*oa_fsgls;
	d_iov_t			*oa_fiovs;
};

static int
oit_filter_fetch_init(struct oit_filter_arg *oa)
{
	uint32_t	 i, nr = *oa->oa_oids_nr;
	uint32_t	 val_len = DAOS_OIT_MARKER_MAX_LEN + DAOS_OIT_DEFAULT_VAL_LEN;
	uint64_t	 buf_len;
	daos_iod_t	*iod;
	d_sg_list_t	*sgl;
	void		*buf, *buf_ptr, *val;

	buf_len = sizeof(daos_iod_t) + sizeof(d_sg_list_t) + sizeof(d_iov_t) + val_len;
	buf_len *= nr;
	D_ALLOC(buf, buf_len);
	if (buf == NULL)
		return -DER_NOMEM;

	oa->oa_fbuf = buf;
	buf_ptr = buf;
	oa->oa_fiods = buf_ptr;
	buf_ptr += sizeof(daos_iod_t) * nr;
	oa->oa_fsgls = buf_ptr;
	buf_ptr += sizeof(d_sg_list_t) * nr;
	oa->oa_fiovs = buf_ptr;
	buf_ptr += sizeof(d_iov_t) * nr;
	for (i = 0; i < nr; i++) {
		iod = &oa->oa_fiods[i];
		DAOS_OIT_AKEY_SET(&iod->iod_name, &oa->oa_oids[i]);
		iod->iod_type	= DAOS_IOD_SINGLE;
		iod->iod_size	= val_len;
		iod->iod_nr	= 1;

		sgl = &oa->oa_fsgls[i];
		sgl->sg_nr = sgl->sg_nr_out = 1;
		sgl->sg_iovs = &oa->oa_fiovs[i];
		val = buf_ptr + i * val_len;
		d_iov_set(&oa->oa_fiovs[i], val, val_len);
	}

	return 0;
}

static void
oit_filter_fetch_fini(struct oit_filter_arg *oa)
{
	if (oa == NULL || oa->oa_fbuf == NULL)
		return;
	D_FREE(oa->oa_fbuf);
	oa->oa_fiods = NULL;
	oa->oa_fsgls = NULL;
	oa->oa_fiovs = NULL;
}

static void
oit_filter_arg_free(struct oit_filter_arg *oa)
{
	d_sgl_fini(&oa->oa_sgl, false);
	D_FREE(oa->oa_kds);
	oit_filter_fetch_fini(oa);
	D_FREE(oa);
}

static int
oit_filter_cb(tse_task_t *task, void *args)
{
	struct oit_filter_arg	*oa = *(struct oit_filter_arg **)args;
	daos_oit_filter_cb	*filter = oa->oa_filter;
	daos_obj_id_t		*oids = oa->oa_oids;
	uint32_t		*oids_nr = oa->oa_oids_nr;
	d_iov_t			*iov, *iovs = oa->oa_fiovs;
	int			 i, j, iov_idx;
	int			 rc = 0;

	if (task->dt_result != 0) {
		oit_filter_arg_free(oa);
		return 0;
	}

	D_ASSERT(*oids_nr > 0);

	for (i = 0, iov_idx = 0; i < *oids_nr; i++) {
		iov = &iovs[iov_idx++];
		D_ASSERTF(iov->iov_len >= DAOS_OIT_DEFAULT_VAL_LEN &&
			  iov->iov_len <= DAOS_OIT_MARKER_MAX_LEN +
					  DAOS_OIT_DEFAULT_VAL_LEN,
			  "bad iov->iov_len %zu\n", iov->iov_len);
		iov->iov_len -= DAOS_OIT_DEFAULT_VAL_LEN;
		if (iov->iov_len > 0)
			memmove(iov->iov_buf, iov->iov_buf + DAOS_OIT_DEFAULT_VAL_LEN,
				iov->iov_len);
		memset(iov->iov_buf + iov->iov_len, 0, DAOS_OIT_DEFAULT_VAL_LEN);
		rc = filter(oids[i], iov);
		if (unlikely(rc < 0)) {
			break;
		} else if (rc == 0) {
			/* remove this oid from the oid array */
			for (j = i; j < *oids_nr - 1; j++)
				oids[j] = oids[j + 1];
			D_ASSERT(*oids_nr >= 1);
			oids[*oids_nr - 1].lo = 0;
			oids[*oids_nr - 1].hi = 0;
			*oids_nr = *oids_nr - 1;
			i--;
			continue;
		} else {
			rc = 0;
			continue;
		}
	}
	oit_filter_arg_free(oa);
	return rc;
}

static int
oit_filter_list_cb(tse_task_t *task, void *args)
{
	struct oit_filter_arg	*oa = *(struct oit_filter_arg **)args;
	uint32_t		*oids_nr = oa->oa_oids_nr;
	tse_task_t		*ftask = NULL;
	struct daos_task_args	*task_args;
	daos_obj_fetch_t	*fargs;
	int			 rc = 0;

	if (task->dt_result != 0 || *oids_nr == 0) {
		oit_filter_arg_free(oa);
		return 0;
	}

	rc = oit_filter_fetch_init(oa);
	if (rc) {
		oit_filter_arg_free(oa);
		return rc;
	}

	/* create fetch task to get back marker data, cannot directly call dc_obj_fetch_task_create
	 * here to avoid dc_task_create() -> daos_event_priv_get() failure.
	 */
	DAOS_API_ARG_ASSERT(*fargs, OBJ_FETCH);
	rc = tse_task_create(dc_obj_fetch_task, tse_task2sched(task), NULL, &ftask);
	if (rc) {
		oit_filter_arg_free(oa);
		return rc;
	}
	task_args = tse_task_buf_embedded(ftask, sizeof(struct daos_task_args));
	task_args->ta_magic = DAOS_TASK_MAGIC;
	fargs = (void *)&task_args->ta_u;
	fargs->oh		= oa->oa_oh;
	fargs->th		= DAOS_TX_NONE;
	fargs->flags		= 0;
	fargs->dkey		= &oa->oa_dkey;
	fargs->nr		= *oids_nr;
	fargs->extra_flags	= 0;
	fargs->iods		= oa->oa_fiods;
	fargs->sgls		= oa->oa_fsgls;
	fargs->ioms		= NULL;
	fargs->extra_arg	= NULL;
	fargs->csum_iov		= NULL;

	rc = tse_task_register_deps(task, 1, &ftask);
	if (rc != 0) {
		tse_task_complete(ftask, rc);
		oit_filter_arg_free(oa);
		return rc;
	}

	rc = tse_task_register_comp_cb(task, oit_filter_cb, &oa, sizeof(oa));
	if (rc) {
		tse_task_complete(ftask, rc);
		oit_filter_arg_free(oa);
		return rc;
	}

	rc = tse_task_schedule(ftask, 0);
	if (rc)
		tse_task_complete(ftask, rc);

	return 0;
}

int
daos_oit_list_filter(daos_handle_t oh, daos_obj_id_t *oids, uint32_t *oids_nr,
		     daos_anchor_t *anchor, daos_oit_filter_cb filter, daos_event_t *ev)
{
	struct oit_filter_arg	*oa;
	tse_task_t		*task;
	int			 i;
	int			 rc;

	if (daos_handle_is_inval(oh) || oids == NULL || oids_nr == NULL || *oids_nr == 0 ||
	    anchor == NULL || filter == NULL)
		return -DER_INVAL;

	D_ALLOC_PTR(oa);
	if (!oa)
		return -DER_NOMEM;

	oa->oa_oh	= oh;
	oa->oa_oids	= oids;
	oa->oa_oids_nr	= oids_nr;
	oa->oa_ev	= ev;
	oa->oa_filter	= filter;
	oa->oa_nr	= *oids_nr;
	D_ALLOC_ARRAY(oa->oa_kds, oa->oa_nr);
	if (!oa->oa_kds)
		D_GOTO(failed, rc = -DER_NOMEM);

	rc = d_sgl_init(&oa->oa_sgl, oa->oa_nr);
	if (rc)
		D_GOTO(failed, rc = -DER_NOMEM);

	for (i = 0; i < oa->oa_nr; i++)
		d_iov_set(&oa->oa_sgl.sg_iovs[i], &oids[i], sizeof(oids[i]));

	DAOS_OIT_DKEY_SET(&oa->oa_dkey, &oa->oa_bucket);
	rc = dc_obj_list_akey_task_create(oh, DAOS_TX_NONE, &oa->oa_dkey,
					  oids_nr, oa->oa_kds, &oa->oa_sgl,
					  anchor, ev, NULL, &task);
	if (rc)
		D_GOTO(failed, rc);

	rc = tse_task_register_comp_cb(task, oit_filter_list_cb, &oa, sizeof(oa));
	if (rc) {
		tse_task_complete(task, rc);
		D_GOTO(failed, rc);
	}

	return dc_task_schedule(task, true);

failed:
	/* NB: OK to call with empty sgl */
	oit_filter_arg_free(oa);
	return rc;
}

int
oit_filter_unmarked(daos_obj_id_t oid, d_iov_t *marker)
{
	if (marker != NULL && marker->iov_len > 0 && marker->iov_buf != 0)
		return 0;
	return 1;
}

int
daos_oit_list_unmarked(daos_handle_t oh, daos_obj_id_t *oids, uint32_t *oids_nr,
		       daos_anchor_t *anchor, daos_event_t *ev)
{
	return daos_oit_list_filter(oh, oids, oids_nr, anchor, oit_filter_unmarked, ev);
}
