/*
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * \file
 *
 * ds_obj: Object Server API
 */

#ifndef __DAOS_SRV_OBJ_H__
#define __DAOS_SRV_OBJ_H__

#include <daos/common.h>
#include <daos_types.h>
#include <daos_srv/dtx_srv.h>

typedef int (*iter_copy_data_cb_t)(daos_handle_t ih,
				   vos_iter_entry_t *it_entry,
				   d_iov_t *iov_out);

struct ds_obj_enum_arg {
	daos_epoch_range_t     *eprs;
	struct daos_csummer    *csummer;
	int			eprs_cap;
	int			eprs_len;
	int			last_type;	/* hack for tweaking kds_len */
	iter_copy_data_cb_t	copy_data_cb;
	/* Buffer fields */
	union {
		struct {	/* !fill_recxs */
			daos_key_desc_t	       *kds;
			int			kds_cap;
			int			kds_len;
			d_sg_list_t	       *sgl;
			d_iov_t			csum_iov;
			uint32_t		ec_cell_sz;
			int			sgl_idx;
		};
		struct {	/* fill_recxs && type == S||R */
			daos_recx_t	       *recxs;
			int			recxs_cap;
			int			recxs_len;
		};
	};
	daos_size_t		inline_thres;	/* type == S||R || chk_key2big*/
	int			rnum;		/* records num (type == S||R) */
	daos_size_t		rsize;		/* record size (type == S||R) */
	daos_unit_oid_t		oid;		/* for unpack */
	uint32_t		fill_recxs:1,	/* type == S||R */
				chk_key2big:1,
				need_punch:1,	/* need to pack punch epoch */
				obj_punched:1,	/* object punch is packed   */
				size_query:1;	/* Only query size */
};

struct dtx_handle;
typedef int (*enum_iterate_cb_t)(vos_iter_param_t *param, vos_iter_type_t type,
				 bool recursive, struct vos_iter_anchors *anchors,
				 vos_iter_cb_t pre_cb, vos_iter_cb_t post_cb,
				 void *arg, struct dtx_handle *dth);

int ds_obj_enum_pack(vos_iter_param_t *param, vos_iter_type_t type, bool recursive,
		     struct vos_iter_anchors *anchors, struct ds_obj_enum_arg *arg,
		     enum_iterate_cb_t iter_cb, struct dtx_handle *dth);

/* Per xstream migrate status */
struct ds_migrate_status {
	uint64_t dm_rec_count;     /* migrated record size */
	uint64_t dm_obj_count;     /* migrated object count */
	uint64_t dm_total_size;    /* migrated total size */
	int      dm_status;        /* migrate status */
	uint32_t dm_migrating : 1; /* if it is migrating */
};

int
ds_migrate_query_status(uuid_t pool_uuid, uint32_t ver, unsigned int generation, int op,
			struct ds_migrate_status *dms);

int
ds_object_migrate_send(struct ds_pool *pool, uuid_t pool_hdl_uuid, uuid_t cont_uuid,
		       uuid_t cont_hdl_uuid, int tgt_id, uint32_t version, unsigned int generation,
		       uint64_t max_eph, daos_unit_oid_t *oids, daos_epoch_t *ephs,
		       daos_epoch_t *punched_ephs, unsigned int *shards, int cnt,
		       uint32_t new_gl_ver, unsigned int migrate_opc, uint64_t *enqueue_id,
		       uint32_t *max_delay);
int
ds_migrate_object(struct ds_pool *pool, uuid_t po_hdl, uuid_t co_hdl, uuid_t co_uuid,
		  uint32_t version, uint32_t generation, uint64_t max_eph, uint32_t opc,
		  daos_unit_oid_t *oids, daos_epoch_t *epochs, daos_epoch_t *punched_epochs,
		  unsigned int *shards, uint32_t count, unsigned int tgt_idx, uint32_t new_gl_ver);
void
ds_migrate_stop(struct ds_pool *pool, uint32_t ver, unsigned int generation);

#endif /* __DAOS_SRV_OBJ_H__ */
