/**
 * (C) Copyright 2016-2019 Intel Corporation.
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
 * This file is part of daos
 *
 * vos/iterator.c
 */
#define D_LOGFAC	DD_FAC(vos)

#include <daos/btree.h>
#include <daos_srv/vos.h>
#include <vos_internal.h>

/** Dictionary for all known vos iterators */
struct vos_iter_dict {
	vos_iter_type_t		 id_type;
	const char		*id_name;
	struct vos_iter_ops	*id_ops;
};

static struct vos_iter_dict vos_iterators[] = {
	{
		.id_type	= VOS_ITER_COUUID,
		.id_name	= "co",
		.id_ops		= &vos_cont_iter_ops,
	},
	{
		.id_type	= VOS_ITER_OBJ,
		.id_name	= "obj",
		.id_ops		= &vos_oi_iter_ops,
	},
	{
		.id_type	= VOS_ITER_DKEY,
		.id_name	= "dkey",
		.id_ops		= &vos_obj_iter_ops,
	},
	{
		.id_type	= VOS_ITER_AKEY,
		.id_name	= "akey",
		.id_ops		= &vos_obj_iter_ops,
	},
	{
		.id_type	= VOS_ITER_SINGLE,
		.id_name	= "single",
		.id_ops		= &vos_obj_iter_ops,
	},
	{
		.id_type	= VOS_ITER_RECX,
		.id_name	= "recx",
		.id_ops		= &vos_obj_iter_ops,
	},
	{
		.id_type	= VOS_ITER_DTX,
		.id_name	= "dtx",
		.id_ops		= &vos_dtx_iter_ops,
	},
	{
		.id_type	= VOS_ITER_NONE,
		.id_name	= "unknown",
		.id_ops		= NULL,
	},
};

const char *
vos_iter_type2name(vos_iter_type_t type)
{
	struct vos_iter_dict	*dict;

	for (dict = &vos_iterators[0]; dict->id_ops != NULL; dict++) {
		if (dict->id_type == type)
			break;
	}
	return dict->id_name;
}

static daos_handle_t
vos_iter2hdl(struct vos_iterator *iter)
{
	daos_handle_t	hdl;

	hdl.cookie = (uint64_t)iter;
	return hdl;
}

static int
nested_prepare(vos_iter_type_t type, struct vos_iter_dict *dict,
	       vos_iter_param_t *param, daos_handle_t *cih)
{
	struct vos_iterator	*iter = vos_hdl2iter(param->ip_ih);
	struct vos_iterator	*citer;
	struct vos_iter_info	 info;
	int			 rc;

	D_ASSERT(iter->it_ops != NULL);

	if (dict->id_ops->iop_nested_prepare == NULL ||
	    iter->it_ops->iop_nested_tree_fetch == NULL) {
		D_ERROR("nested iterator prepare isn't supported for %s",
			dict->id_name);
		return -DER_NOSYS;
	}

	if (iter->it_state == VOS_ITS_NONE) {
		D_ERROR("Please call vos_iter_probe to initialise cursor\n");
		return -DER_NO_PERM;
	}

	if (iter->it_state == VOS_ITS_END) {
		D_DEBUG(DB_TRACE, "The end of iteration\n");
		return -DER_NONEXIST;
	}

	rc = iter->it_ops->iop_nested_tree_fetch(iter, type, &info);

	if (rc != 0) {
		if ((type == VOS_ITER_RECX || type == VOS_ITER_SINGLE) &&
		    rc == -DER_NONEXIST)
			D_DEBUG(DB_TRACE, "%s iterator does not exist\n",
				dict->id_name);
		else
			D_ERROR("Problem fetching nested tree from iterator:"
				" %d", rc);
		return rc;
	}

	info.ii_epc_expr = param->ip_epc_expr;
	info.ii_flags = param->ip_flags;
	info.ii_akey = &param->ip_akey;

	rc = dict->id_ops->iop_nested_prepare(type, &info, &citer);
	if (rc != 0) {
		D_ERROR("Failed to prepare %s iterator: %d\n", dict->id_name,
			rc);
		return rc;
	}

	iter->it_ref_cnt++;

	citer->it_type		= type;
	citer->it_ops		= dict->id_ops;
	citer->it_state		= VOS_ITS_NONE;
	citer->it_ref_cnt	= 1;
	citer->it_parent	= iter;
	citer->it_from_parent	= 1;

	*cih = vos_iter2hdl(citer);
	return 0;
}

int
vos_iter_prepare(vos_iter_type_t type, vos_iter_param_t *param,
		 daos_handle_t *ih)
{
	struct vos_iter_dict	*dict;
	struct vos_iterator	*iter;
	int			 rc;

	if (ih == NULL) {
		D_ERROR("Argument 'ih' is invalid to vos_iter_param\n");
		return -DER_INVAL;
	}

	*ih = DAOS_HDL_INVAL;

	if (daos_handle_is_inval(param->ip_hdl) &&
	    daos_handle_is_inval(param->ip_ih)) {
		D_ERROR("No valid handle specified in vos_iter_param\n");
		return -DER_INVAL;
	}

	for (dict = &vos_iterators[0]; dict->id_ops != NULL; dict++) {
		if (dict->id_type == type)
			break;
	}

	if (dict->id_ops == NULL) {
		D_ERROR("Can't find iterator type %d\n", type);
		return -DER_NOSYS;
	}
	if (!daos_handle_is_inval(param->ip_ih)) {
		D_DEBUG(DB_TRACE, "Preparing nested iterator of type %s\n",
			dict->id_name);
		return nested_prepare(type, dict, param, ih);
	}

	D_DEBUG(DB_TRACE, "Preparing standalone iterator of type %s\n",
		dict->id_name);
	rc = dict->id_ops->iop_prepare(type, param, &iter);
	if (rc != 0) {
		if (rc == -DER_NONEXIST)
			D_DEBUG(DB_TRACE, "No %s to iterate: %d\n",
				dict->id_name, rc);
		else
			D_ERROR("Failed to prepare %s iterator: %d\n",
				dict->id_name, rc);
		return rc;
	}

	D_ASSERT(iter->it_type == type);

	iter->it_ops		= dict->id_ops;
	iter->it_state		= VOS_ITS_NONE;
	iter->it_ref_cnt	= 1;
	iter->it_parent		= NULL;
	iter->it_from_parent	= 0;

	*ih = vos_iter2hdl(iter);
	return 0;
}

/* Internal function to ensure parent iterator remains
 * allocated while any nested iterators are active
 */
static int
iter_decref(struct vos_iterator *iter)
{
	iter->it_ref_cnt--;

	if (iter->it_ref_cnt)
		return 0;

	D_ASSERT(iter->it_ops != NULL);
	return iter->it_ops->iop_finish(iter);
}

int
vos_iter_finish(daos_handle_t ih)
{
	struct vos_iterator	*iter;
	struct vos_iterator	*parent;
	int			 rc = 0;
	int			 prc = 0;

	if (daos_handle_is_inval(ih))
		return -DER_INVAL;

	iter = vos_hdl2iter(ih);
	parent = iter->it_parent;

	iter->it_parent = NULL;
	rc = iter_decref(iter);

	if (parent)
		prc = iter_decref(parent);

	return rc || prc;
}

int
vos_iter_probe(daos_handle_t ih, daos_anchor_t *anchor)
{
	struct vos_iterator *iter = vos_hdl2iter(ih);
	int		     rc;

	D_ASSERT(iter->it_ops != NULL);
	rc = iter->it_ops->iop_probe(iter, anchor);
	if (rc == 0)
		iter->it_state = VOS_ITS_OK;
	else if (rc == -DER_NONEXIST)
		iter->it_state = VOS_ITS_END;
	else
		iter->it_state = VOS_ITS_NONE;

	return rc;
}

static inline int
iter_verify_state(struct vos_iterator *iter)
{
	if (iter->it_state == VOS_ITS_NONE) {
		D_ERROR("Please call vos_iter_probe to initialise cursor\n");
		return -DER_NO_PERM;
	} else if (iter->it_state == VOS_ITS_END) {
		D_DEBUG(DB_TRACE, "The end of iteration\n");
		return -DER_NONEXIST;
	} else {
		return 0;
	}
}

int
vos_iter_next(daos_handle_t ih)
{
	struct vos_iterator *iter = vos_hdl2iter(ih);
	int		     rc;

	rc = iter_verify_state(iter);
	if (rc)
		return rc;

	D_ASSERT(iter->it_ops != NULL);
	rc = iter->it_ops->iop_next(iter);
	if (rc == 0)
		iter->it_state = VOS_ITS_OK;
	else if (rc == -DER_NONEXIST)
		iter->it_state = VOS_ITS_END;
	else
		iter->it_state = VOS_ITS_NONE;

	return rc;
}

int
vos_iter_fetch(daos_handle_t ih, vos_iter_entry_t *it_entry,
	       daos_anchor_t *anchor)
{
	struct vos_iterator *iter = vos_hdl2iter(ih);
	int rc;

	rc = iter_verify_state(iter);
	if (rc)
		return rc;

	D_ASSERT(iter->it_ops != NULL);
	return iter->it_ops->iop_fetch(iter, it_entry, anchor);
}

int
vos_iter_copy(daos_handle_t ih, vos_iter_entry_t *it_entry,
	      d_iov_t *iov_out)
{
	struct vos_iterator *iter = vos_hdl2iter(ih);
	int rc;

	rc = iter_verify_state(iter);
	if (rc)
		return rc;

	D_ASSERT(iter->it_ops != NULL);
	if (iter->it_ops->iop_copy == NULL)
		return -DER_NOSYS;

	return iter->it_ops->iop_copy(iter, it_entry, iov_out);
}

int
vos_iter_delete(daos_handle_t ih, void *args)
{
	struct vos_iterator *iter = vos_hdl2iter(ih);
	int rc;

	rc = iter_verify_state(iter);
	if (rc)
		return rc;

	D_ASSERT(iter->it_ops != NULL);
	if (iter->it_ops->iop_delete == NULL)
		return -DER_NOSYS;

	return iter->it_ops->iop_delete(iter, args);
}

int
vos_iter_empty(daos_handle_t ih)
{
	struct vos_iterator *iter = vos_hdl2iter(ih);

	D_ASSERT(iter->it_ops != NULL);
	if (iter->it_ops->iop_empty == NULL)
		return -DER_NOSYS;

	return iter->it_ops->iop_empty(iter);
}

static inline daos_anchor_t *
type2anchor(vos_iter_type_t type, struct vos_iter_anchors *anchors)
{
	switch (type) {
	case VOS_ITER_OBJ:
		D_ASSERT(anchors->ia_reprobe_obj == 0);
		return &anchors->ia_obj;
	case VOS_ITER_DKEY:
		D_ASSERT(anchors->ia_reprobe_dkey == 0);
		return &anchors->ia_dkey;
	case VOS_ITER_AKEY:
		D_ASSERT(anchors->ia_reprobe_akey == 0);
		return &anchors->ia_akey;
	case VOS_ITER_RECX:
		D_ASSERT(anchors->ia_reprobe_ev == 0);
		return &anchors->ia_ev;
	case VOS_ITER_SINGLE:
		D_ASSERT(anchors->ia_reprobe_sv == 0);
		return &anchors->ia_sv;
	default:
		D_ASSERTF(false, "invalid iter type %d\n", type);
		return NULL;
	}
}

static inline bool
is_last_level(vos_iter_type_t type)
{
	return (type == VOS_ITER_SINGLE || type == VOS_ITER_RECX);
}

static inline void
reset_anchors(vos_iter_type_t type, struct vos_iter_anchors *anchors)
{
	switch (type) {
	case VOS_ITER_DKEY:
		D_ASSERT(daos_anchor_is_eof(&anchors->ia_dkey));
		daos_anchor_set_zero(&anchors->ia_dkey);
		daos_anchor_set_zero(&anchors->ia_akey);
		daos_anchor_set_zero(&anchors->ia_ev);
		daos_anchor_set_zero(&anchors->ia_sv);
		break;
	case VOS_ITER_AKEY:
		D_ASSERT(daos_anchor_is_eof(&anchors->ia_akey));
		daos_anchor_set_zero(&anchors->ia_akey);
		daos_anchor_set_zero(&anchors->ia_ev);
		daos_anchor_set_zero(&anchors->ia_sv);
		break;
	case VOS_ITER_RECX:
		D_ASSERT(daos_anchor_is_eof(&anchors->ia_ev));
		daos_anchor_set_zero(&anchors->ia_ev);
		daos_anchor_set_zero(&anchors->ia_sv);
		break;
	case VOS_ITER_SINGLE:
		D_ASSERT(daos_anchor_is_eof(&anchors->ia_sv));
		daos_anchor_set_zero(&anchors->ia_sv);
		break;
	default:
		D_ASSERTF(false, "invalid iter type %d\n", type);
		break;
	}
}

static inline void
set_reprobe(vos_iter_type_t type, unsigned int acts,
	    struct vos_iter_anchors *anchors, bool unsorted)
{
	bool yield = (acts & VOS_ITER_CB_YIELD);
	bool delete = (acts & VOS_ITER_CB_DELETE);

	switch (type) {
	case VOS_ITER_SINGLE:
		if (yield || delete)
			anchors->ia_reprobe_sv = 1;
		/* fallthrough */
	case VOS_ITER_RECX:
		/* evtree only need reprobe on yield for unsorted iteration */
		if (unsorted && yield && (type == VOS_ITER_RECX))
			anchors->ia_reprobe_ev = 1;
		/* fallthrough */
	case VOS_ITER_AKEY:
		if (yield || (delete && (type == VOS_ITER_AKEY)))
			anchors->ia_reprobe_akey = 1;
		/* fallthrough */
	case VOS_ITER_DKEY:
		if (yield || (delete && (type == VOS_ITER_DKEY)))
			anchors->ia_reprobe_dkey = 1;
		/* fallthrough */
	case VOS_ITER_OBJ:
		if (yield || (delete && (type == VOS_ITER_OBJ)))
			anchors->ia_reprobe_obj = 1;
		break;
	default:
		D_ASSERTF(false, "invalid iter type %d\n", type);
		break;
	}
}

static inline bool
need_reprobe(vos_iter_type_t type, struct vos_iter_anchors *anchors)
{
	bool reprobe;

	switch (type) {
	case VOS_ITER_OBJ:
		reprobe = anchors->ia_reprobe_obj;
		anchors->ia_reprobe_obj = 0;
		break;
	case VOS_ITER_DKEY:
		reprobe = anchors->ia_reprobe_dkey;
		anchors->ia_reprobe_dkey = 0;
		break;
	case VOS_ITER_AKEY:
		reprobe = anchors->ia_reprobe_akey;
		anchors->ia_reprobe_akey = 0;
		break;
	case VOS_ITER_RECX:
		reprobe = anchors->ia_reprobe_ev;
		anchors->ia_reprobe_ev = 0;
		break;
	case VOS_ITER_SINGLE:
		reprobe = anchors->ia_reprobe_sv;
		anchors->ia_reprobe_sv = 0;
		break;
	default:
		D_ASSERTF(false, "invalid iter type %d\n", type);
		reprobe = false;
		break;
	}
	return reprobe;
}

/**
 * Iterate VOS entries (i.e., containers, objects, dkeys, etc.) and call \a
 * cb(\a arg) for each entry.
 */
int
vos_iterate(vos_iter_param_t *param, vos_iter_type_t type, bool recursive,
	    struct vos_iter_anchors *anchors, vos_iter_cb_t cb, void *arg)
{
	daos_anchor_t		*anchor, *probe_anchor = NULL;
	vos_iter_entry_t	iter_ent;
	daos_handle_t		ih;
	unsigned int		acts = 0;
	bool			skipped;
	int			rc;

	D_ASSERT(type >= VOS_ITER_OBJ && type <= VOS_ITER_RECX);
	D_ASSERT(anchors != NULL);

	anchor = type2anchor(type, anchors);

	rc = vos_iter_prepare(type, param, &ih);
	if (rc != 0) {
		if (rc == -DER_NONEXIST) {
			daos_anchor_set_eof(anchor);
			rc = 0;
		} else {
			D_ERROR("failed to prepare iterator (type=%d): %d\n",
				type, rc);
		}
		return rc;
	}

probe:
	if (!daos_anchor_is_zero(anchor))
		probe_anchor = anchor;

	rc = vos_iter_probe(ih, probe_anchor);
	if (rc != 0) {
		if (rc == -DER_NONEXIST || rc == -DER_AGAIN) {
			daos_anchor_set_eof(anchor);
			rc = 0;
		} else {
			D_ERROR("failed to probe iterator (type=%d anchor=%p): "
				"%d\n", type, probe_anchor, rc);
		}
		D_GOTO(out, rc);
	}

	while (1) {
		rc = vos_iter_fetch(ih, &iter_ent, anchor);
		if (rc != 0) {
			D_ERROR("failed to fetch iterator (type=%d): %d\n",
				type, rc);
			break;
		}

		rc = cb(ih, &iter_ent, type, param, arg, &acts);
		if (rc != 0)
			break;

		set_reprobe(type, acts, anchors,
			    param->ip_flags == VOS_IT_RECX_ALL);
		skipped = (acts & VOS_ITER_CB_SKIP);
		acts = 0;

		if (need_reprobe(type, anchors)) {
			D_ASSERT(!daos_anchor_is_zero(anchor) &&
				 !daos_anchor_is_eof(anchor));
			goto probe;
		}

		if (recursive && !is_last_level(type) && !skipped) {
			vos_iter_param_t	child_param = *param;

			child_param.ip_ih = ih;

			switch (type) {
			case VOS_ITER_OBJ:
				child_param.ip_oid = iter_ent.ie_oid;
				break;
			case VOS_ITER_DKEY:
				child_param.ip_dkey = iter_ent.ie_key;
				break;
			case VOS_ITER_AKEY:
				child_param.ip_akey = iter_ent.ie_key;
				break;
			default:
				D_ASSERTF(false, "invalid iter type:%d\n",
					  type);
				rc = -DER_INVAL;
				goto out;
			}

			rc = vos_iterate(&child_param, iter_ent.ie_child_type,
					 recursive, anchors, cb, arg);
			if (rc != 0)
				D_GOTO(out, rc);

			reset_anchors(iter_ent.ie_child_type, anchors);

			if (need_reprobe(type, anchors)) {
				D_ASSERT(!daos_anchor_is_zero(anchor) &&
					 !daos_anchor_is_eof(anchor));
				goto probe;
			}
		}

		rc = vos_iter_next(ih);
		if (rc) {
			if (rc != -DER_NONEXIST)
				D_ERROR("failed to iterate next (type=%d): "
					"%d\n", type, rc);
			break;
		}
	}

	if (rc == -DER_NONEXIST) {
		daos_anchor_set_eof(anchor);
		rc = 0;
	}
out:
	if (rc < 0)
		D_ERROR("abort iteration type:%d, rc:%d\n", type, rc);

	vos_iter_finish(ih);
	return rc;
}
