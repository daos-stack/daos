/**
 * (C) Copyright 2018-2024 Intel Corporation.
 * (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/** DFS container operations */

#define D_LOGFAC DD_FAC(dfs)

#include <daos/pool.h>
#include <daos/container.h>
#include <daos/object.h>
#include <daos/common.h>

#include "dfs_internal.h"

static int
suggest_dfs_cs(daos_handle_t poh, daos_prop_t *prop, uint64_t rf, daos_oclass_id_t oc_id,
	       daos_size_t *cs)
{
	struct daos_oclass_attr *oc_attr;
	struct daos_prop_entry  *dpe;
	uint64_t                 ec_cell_size;
	uint32_t                 nr_grps;
	daos_size_t              base;
	int                      rc;

	/** No EC above RF 3, use default CS */
	if (rf > 3) {
		*cs = DFS_DEFAULT_CHUNK_SIZE;
		return 0;
	}

	if (oc_id == 0) {
		daos_obj_id_t oid       = {.hi = 0, .lo = 0};
		uint32_t      pa_domain = daos_cont_prop2redunlvl(prop);

		/** generate the oclass that would be used for file  */
		rc = daos_obj_generate_oid_by_rf(poh, rf, &oid, DAOS_OT_ARRAY_BYTE, OC_UNKNOWN, 0,
						 0, pa_domain);
		if (rc) {
			D_ERROR("daos_obj_generate_oid_by_rf() Failed: " DF_RC "\n", DP_RC(rc));
			return daos_der2errno(rc);
		}

		oc_attr = daos_oclass_attr_find(oid, &nr_grps);
		if (oc_attr == NULL)
			return EINVAL;
	} else {
		oc_attr = daos_oclass_id2attr(oc_id, &nr_grps);
		if (oc_attr == NULL)
			return EINVAL;
	}

	/** for Replication (including non-redundant), return the default chunk size - 1 MiB */
	if (oc_attr->ca_resil == DAOS_RES_REPL) {
		*cs = DFS_DEFAULT_CHUNK_SIZE;
		return 0;
	}

	/** query the EC cell size from container first */
	dpe = daos_prop_entry_get(prop, DAOS_PROP_CO_EC_CELL_SZ);
	if (dpe) {
		ec_cell_size = dpe->dpe_val;
	} else {
		daos_prop_t            *pool_prop;
		struct daos_prop_entry *entry;

		/** Check the EC Cell size property on pool */
		pool_prop                          = daos_prop_alloc(1);
		pool_prop->dpp_entries[0].dpe_type = DAOS_PROP_PO_EC_CELL_SZ;

		rc = daos_pool_query(poh, NULL, NULL, pool_prop, NULL);
		if (rc) {
			daos_prop_free(pool_prop);
			return daos_der2errno(rc);
		}
		entry = daos_prop_entry_get(pool_prop, DAOS_PROP_PO_EC_CELL_SZ);
		if (entry != NULL)
			ec_cell_size = entry->dpe_val;
		else
			ec_cell_size = DAOS_EC_CELL_DEF;
		daos_prop_free(pool_prop);
	}

	/** set the DFS chunk size to the the EC cell size x the number of data cells */
	base = oc_attr->u.ec.e_k * ec_cell_size;

	/*
	 * If the chunk size is still < than the default chunk size, bump it to be >=, but still a
	 * multiple of the EC cell size. Find the next multiple of EC_CELL_SIZE greater than
	 * DFS_DEFAULT_CHUNK_SIZE.
	 */
	if (base < DFS_DEFAULT_CHUNK_SIZE) {
		int nchunks;

		nchunks = DFS_DEFAULT_CHUNK_SIZE / base;
		if (DFS_DEFAULT_CHUNK_SIZE % base != 0)
			nchunks++;
		D_DEBUG(DB_TRACE,
			"Calculated chunk size (%zu) is smaller than %d, bump it by x %d\n", base,
			DFS_DEFAULT_CHUNK_SIZE, nchunks);
		*cs = nchunks * base;
	} else {
		*cs = base * 2;
	}
	D_DEBUG(DB_TRACE, "Setting the DFS chunk size of the container to %zu\n", *cs);
	return 0;
}

int
dfs_cont_create(daos_handle_t poh, uuid_t *cuuid, dfs_attr_t *attr, daos_handle_t *_coh,
		dfs_t **_dfs)
{
	daos_handle_t             coh, super_oh;
	struct dfs_entry          entry           = {0};
	daos_prop_t              *prop            = NULL;
	daos_oclass_hints_t       dir_oclass_hint = 0;
	uint64_t                  rf;
	daos_cont_info_t          co_info;
	dfs_t                    *dfs;
	dfs_attr_t                dattr = {0};
	char                      str[37];
	struct daos_prop_co_roots roots;
	int                       rc, rc2;
	struct daos_prop_entry   *dpe;
	struct daos_prop_entry   *roots_entry  = NULL;
	struct daos_prop_entry   *layout_entry = NULL;
	struct timespec           now;
	uint32_t                  cid_tf;
	uint32_t                  pa_domain;
	uint32_t                  prop_nr      = 0;
	uint32_t                  props_to_add = 0;
	int                       cont_tf;

	if (cuuid == NULL)
		return EINVAL;
	if (_dfs && _coh == NULL) {
		D_ERROR("Should pass a valid container handle pointer\n");
		return EINVAL;
	}

	if (attr != NULL && attr->da_props != NULL) {
		prop_nr = attr->da_props->dpp_nr;

		roots_entry = daos_prop_entry_get(attr->da_props, DAOS_PROP_CO_ROOTS);
		if (roots_entry == NULL)
			props_to_add++;

		layout_entry = daos_prop_entry_get(attr->da_props, DAOS_PROP_CO_LAYOUT_TYPE);
		if (layout_entry == NULL)
			props_to_add++;
	} else {
		/* roots and layout are still needed */
		props_to_add = 2;
	}
	prop_nr += props_to_add;

	prop = daos_prop_alloc(prop_nr);
	if (prop == NULL)
		return ENOMEM;

	if (attr != NULL && attr->da_props != NULL) {
		rc = daos_prop_copy(prop, attr->da_props);
		if (rc) {
			D_ERROR("failed to copy properties " DF_RC "\n", DP_RC(rc));
			D_GOTO(err_prop, rc = daos_der2errno(rc));
		}
	}

	/** check if RF factor is set on property */
	dpe = daos_prop_entry_get(prop, DAOS_PROP_CO_REDUN_FAC);
	if (!dpe) {
		rc = dc_pool_get_redunc(poh);
		if (rc < 0)
			D_GOTO(err_prop, rc = daos_der2errno(rc));
		rf = rc;
	} else {
		rf = dpe->dpe_val;
	}

	if (attr) {
		if (attr->da_oclass_id) {
			dattr.da_dir_oclass_id  = attr->da_oclass_id;
			dattr.da_file_oclass_id = attr->da_oclass_id;
			dattr.da_oclass_id      = attr->da_oclass_id;
		}
		if (attr->da_file_oclass_id)
			dattr.da_file_oclass_id = attr->da_file_oclass_id;
		if (attr->da_dir_oclass_id) {
			dattr.da_dir_oclass_id = attr->da_dir_oclass_id;
			if (daos_cid_is_ec(dattr.da_dir_oclass_id)) {
				D_WARN("EC object class for directories is not supported,"
				       " reverting to use default");
				dattr.da_dir_oclass_id = 0;
			}
		}

		/** check non default mode */
		if ((attr->da_mode & MODE_MASK) == DFS_RELAXED ||
		    (attr->da_mode & MODE_MASK) == DFS_BALANCED)
			dattr.da_mode = attr->da_mode;
		else
			dattr.da_mode = DFS_RELAXED;

		/** check non default chunk size */
		if (attr->da_chunk_size != 0)
			dattr.da_chunk_size = attr->da_chunk_size;
		else {
			rc = suggest_dfs_cs(poh, prop, rf, dattr.da_file_oclass_id,
					    &dattr.da_chunk_size);
			if (rc)
				D_GOTO(err_prop, rc);
		}

		if (attr->da_hints[0] != 0) {
			strncpy(dattr.da_hints, attr->da_hints, DAOS_CONT_HINT_MAX_LEN - 1);
			dattr.da_hints[DAOS_CONT_HINT_MAX_LEN - 1] = '\0';
		}
	} else {
		dattr.da_oclass_id      = 0;
		dattr.da_dir_oclass_id  = 0;
		dattr.da_file_oclass_id = 0;
		dattr.da_mode           = DFS_RELAXED;
		rc                      = suggest_dfs_cs(poh, prop, rf, 0, &dattr.da_chunk_size);
		if (rc)
			D_GOTO(err_prop, rc);
	}

	/** verify object class redundancy */
	cont_tf = daos_cont_rf2allowedfailures(rf);
	if (cont_tf < 0)
		D_GOTO(err_prop, rc = EINVAL);

	if (dattr.da_file_oclass_id) {
		rc = daos_oclass_cid2allowedfailures(dattr.da_file_oclass_id, &cid_tf);
		if (rc) {
			D_ERROR("Invalid oclass OID\n");
			D_GOTO(err_prop, rc = daos_der2errno(rc));
		}
		if (cid_tf < cont_tf) {
			D_ERROR("File object class cannot tolerate RF failures\n");
			D_GOTO(err_prop, rc = EINVAL);
		}
	}
	if (dattr.da_dir_oclass_id) {
		rc = daos_oclass_cid2allowedfailures(dattr.da_dir_oclass_id, &cid_tf);
		if (rc) {
			D_ERROR("Invalid oclass OID\n");
			D_GOTO(err_prop, rc = daos_der2errno(rc));
		}
		if (cid_tf < cont_tf) {
			D_ERROR("Directory object class cannot tolerate RF failures\n");
			D_GOTO(err_prop, rc = EINVAL);
		}
	}

	pa_domain = daos_cont_prop2redunlvl(prop);

	/** check hints for SB and Root Dir */
	if (dattr.da_hints[0] != 0) {
		daos_oclass_hints_t file_hints;

		rc = get_oclass_hints(dattr.da_hints, &dir_oclass_hint, &file_hints, rf);
		if (rc)
			D_GOTO(err_prop, rc);
	}

	/* select oclass and generate SB OID */
	roots.cr_oids[0].lo = RESERVED_LO;
	roots.cr_oids[0].hi = SB_HI;
	rc = daos_obj_generate_oid_by_rf(poh, rf, &roots.cr_oids[0], 0, dattr.da_dir_oclass_id,
					 dir_oclass_hint, 0, pa_domain);
	if (rc) {
		D_ERROR("Failed to generate SB OID " DF_RC "\n", DP_RC(rc));
		D_GOTO(err_prop, rc = daos_der2errno(rc));
	}

	/* select oclass and generate ROOT OID */
	roots.cr_oids[1].lo = RESERVED_LO;
	roots.cr_oids[1].hi = ROOT_HI;
	rc = daos_obj_generate_oid_by_rf(poh, rf, &roots.cr_oids[1], 0, dattr.da_dir_oclass_id,
					 dir_oclass_hint, 0, pa_domain);
	if (rc) {
		D_ERROR("Failed to generate ROOT OID " DF_RC "\n", DP_RC(rc));
		D_GOTO(err_prop, rc = daos_der2errno(rc));
	}

	/* select oclass and generate HLM (hardlink metadata) OID */
	roots.cr_oids[2].lo = RESERVED_LO;
	roots.cr_oids[2].hi = HLM_HI;
	rc = daos_obj_generate_oid_by_rf(poh, rf, &roots.cr_oids[2], 0, dattr.da_dir_oclass_id,
					 dir_oclass_hint, 0, pa_domain);
	if (rc) {
		D_ERROR("Failed to generate HLM OID " DF_RC "\n", DP_RC(rc));
		D_GOTO(err_prop, rc = daos_der2errno(rc));
	}

	/* store SB, root & HLM OIDs as container property */
	roots.cr_oids[3] = DAOS_OBJ_NIL;

	if (roots_entry == NULL) {
		/* need to add roots prop to list */
		roots_entry           = &prop->dpp_entries[prop->dpp_nr - props_to_add];
		roots_entry->dpe_type = DAOS_PROP_CO_ROOTS;
		props_to_add--;
	}
	/* if an existing roots prop was passed in, it will be overwritten */
	rc = daos_prop_entry_set_ptr(roots_entry, &roots, sizeof(roots));
	if (rc)
		D_GOTO(err_prop, rc = daos_der2errno(rc));

	if (layout_entry == NULL) {
		/* need to add layout prop to list */
		layout_entry           = &prop->dpp_entries[prop->dpp_nr - props_to_add];
		layout_entry->dpe_type = DAOS_PROP_CO_LAYOUT_TYPE;
		props_to_add--;
	}

	/* layout passed in will be overwritten */
	layout_entry->dpe_val = DAOS_PROP_CO_LAYOUT_POSIX;

	D_ASSERT(props_to_add == 0);

	rc = daos_cont_create(poh, cuuid, prop, NULL);
	if (rc) {
		D_ERROR("daos_cont_create() failed " DF_RC "\n", DP_RC(rc));
		D_GOTO(err_prop, rc = daos_der2errno(rc));
	}

	uuid_unparse(*cuuid, str);
	rc = daos_cont_open(poh, str, DAOS_COO_RW, &coh, &co_info, NULL);
	if (rc) {
		D_ERROR("daos_cont_open() failed " DF_RC "\n", DP_RC(rc));
		D_GOTO(err_destroy, rc = daos_der2errno(rc));
	}

	/** Create SB */
	rc = open_sb(coh, true, false, DAOS_OO_RW, roots.cr_oids[0], &dattr, &super_oh, NULL);
	if (rc)
		D_GOTO(err_close, rc);

	/** Add root object */
	entry.oid  = roots.cr_oids[1];
	entry.mode = S_IFDIR | 0755;
	rc         = clock_gettime(CLOCK_REALTIME, &now);
	if (rc)
		D_GOTO(err_super, rc = errno);
	entry.mtime = entry.ctime = now.tv_sec;
	entry.mtime_nano = entry.ctime_nano = now.tv_nsec;
	entry.uid                           = geteuid();
	entry.gid                           = getegid();

	/*
	 * Since we don't support daos cont create atomicity (2 or more cont
	 * creates on the same container will always succeed), we can get into a
	 * situation where the SB is created by one process, but return EEXIST
	 * on another. in this case we can just assume it is inserted, and
	 * continue.
	 */
	rc = insert_entry(DFS_LAYOUT_VERSION, super_oh, DAOS_TX_NONE, "/", 1, DAOS_COND_DKEY_INSERT,
			  &entry);
	if (rc && rc != EEXIST) {
		D_ERROR("Failed to insert root entry: %d (%s)\n", rc, strerror(rc));
		D_GOTO(err_super, rc);
	}

	rc = daos_obj_close(super_oh, NULL);
	if (rc) {
		D_ERROR("Failed to close SB object " DF_RC "\n", DP_RC(rc));
		D_GOTO(err_close, rc = daos_der2errno(rc));
	}

	if (_dfs) {
		/** Mount DFS on the container we just created */
		rc = dfs_mount(poh, coh, O_RDWR, &dfs);
		if (rc) {
			D_ERROR("dfs_mount() failed (%d)\n", rc);
			D_GOTO(err_close, rc);
		}
		dfs->layout_v = DFS_LAYOUT_VERSION;
		*_dfs         = dfs;
	}

	if (_coh) {
		*_coh = coh;
	} else {
		rc = daos_cont_close(coh, NULL);
		if (rc) {
			D_ERROR("daos_cont_close() failed " DF_RC "\n", DP_RC(rc));
			D_GOTO(err_close, rc = daos_der2errno(rc));
		}
	}
	daos_prop_free(prop);
	return 0;
err_super:
	daos_obj_close(super_oh, NULL);
err_close:
	rc2 = daos_cont_close(coh, NULL);
	if (rc2)
		D_ERROR("daos_cont_close failed " DF_RC "\n", DP_RC(rc2));
err_destroy:
	/*
	 * DAOS container create returns success even if container exists -
	 * DAOS-2700. So if the error here is EEXIST (it means we got it from
	 * the SB creation, so do not destroy the container, since another
	 * process might have created it.
	 */
	if (rc != EEXIST) {
		rc2 = daos_cont_destroy(poh, str, 1, NULL);
		if (rc2)
			D_ERROR("daos_cont_destroy failed " DF_RC "\n", DP_RC(rc));
	}
err_prop:
	daos_prop_free(prop);
	return rc;
}

int
dfs_cont_create_with_label(daos_handle_t poh, const char *label, dfs_attr_t *attr, uuid_t *cuuid,
			   daos_handle_t *coh, dfs_t **dfs)
{
	daos_prop_t *label_prop;
	daos_prop_t *merged_props = NULL;
	daos_prop_t *orig         = NULL;
	dfs_attr_t   local        = {};
	int          rc;

	label_prop = daos_prop_alloc(1);
	if (label_prop == NULL)
		return ENOMEM;

	label_prop->dpp_entries[0].dpe_type = DAOS_PROP_CO_LABEL;
	rc = daos_prop_entry_set_str(&label_prop->dpp_entries[0], label, DAOS_PROP_LABEL_MAX_LEN);
	if (rc)
		D_GOTO(out_prop, rc = daos_der2errno(rc));

	if (attr == NULL)
		attr = &local;

	if (attr->da_props) {
		rc = daos_prop_merge2(attr->da_props, label_prop, &merged_props);
		if (rc != 0)
			D_GOTO(out_prop, rc = daos_der2errno(rc));
		orig           = attr->da_props;
		attr->da_props = merged_props;
	} else {
		attr->da_props = label_prop;
	}

	if (cuuid == NULL) {
		uuid_t u;

		rc = dfs_cont_create(poh, &u, attr, coh, dfs);
	} else {
		rc = dfs_cont_create(poh, cuuid, attr, coh, dfs);
	}
	attr->da_props = orig;
	daos_prop_free(merged_props);
out_prop:
	daos_prop_free(label_prop);
	return rc;
}

#define DFS_ITER_NR        128
#define DFS_ITER_DKEY_BUF  (DFS_ITER_NR * sizeof(uint64_t))
#define DFS_ITER_ENTRY_BUF (DFS_ITER_NR * DFS_MAX_NAME)
#define DFS_ELAPSED_TIME   30

struct dfs_oit_args {
	daos_handle_t        oit;
	uint64_t             flags;
	uint64_t             snap_epoch;
	uint64_t             skipped;
	uint64_t             failed;
	time_t               start_time;
	time_t               print_time;
	uint64_t             num_scanned;
	struct d_hash_table *hlm_hash;
};

/** Hash table entry to track HLM objects for hardlink verification */
struct hlm_check_entry {
	daos_obj_id_t hce_oid;            /**< Object ID (key) */
	uint64_t      hce_stored_linkcnt; /**< Link count stored in HLM (ref_cnt) */
	uint64_t      hce_cur_linkcnt;    /**< Current link count from traversal */
	d_list_t      hce_link;           /**< Hash table link */
};

static inline struct hlm_check_entry *
hlm_check_obj(d_list_t *rlink)
{
	return container_of(rlink, struct hlm_check_entry, hce_link);
}

static bool
hlm_check_key_cmp(struct d_hash_table *htable, d_list_t *rlink, const void *key, unsigned int ksize)
{
	struct hlm_check_entry *entry = hlm_check_obj(rlink);

	D_ASSERT(ksize == sizeof(daos_obj_id_t));
	return daos_oid_cmp(entry->hce_oid, *(daos_obj_id_t *)key) == 0;
}

static uint32_t
hlm_check_rec_hash(struct d_hash_table *htable, d_list_t *rlink)
{
	struct hlm_check_entry *entry = hlm_check_obj(rlink);

	return d_hash_string_u32((const char *)&entry->hce_oid, sizeof(daos_obj_id_t));
}

static void
hlm_check_rec_free(struct d_hash_table *htable, d_list_t *rlink)
{
	struct hlm_check_entry *entry = hlm_check_obj(rlink);

	D_FREE(entry);
}

static d_hash_table_ops_t hlm_check_hash_ops = {
    .hop_key_cmp  = hlm_check_key_cmp,
    .hop_rec_hash = hlm_check_rec_hash,
    .hop_rec_free = hlm_check_rec_free,
};

#define HLM_CHECK_HASH_BITS 10

/**
 * Iterate through HLM object and populate the hash table with all entries.
 * Each entry in HLM has the OID as dkey and stores ref_cnt among other fields.
 */
static int
hlm_populate_check_hash(dfs_t *dfs, struct d_hash_table *hlm_hash, uint64_t flags)
{
	daos_anchor_t    anchor   = {0};
	daos_key_desc_t *kds      = NULL;
	char            *enum_buf = NULL;
	d_sg_list_t      sgl;
	d_iov_t          iov;
	int              rc = 0;

	/* If HLM is not enabled for this container, nothing to do */
	if (daos_obj_id_is_nil(dfs->hlm_oid))
		return 0;

	D_ALLOC_ARRAY(kds, DFS_ITER_NR);
	if (kds == NULL)
		return ENOMEM;

	D_ALLOC_ARRAY(enum_buf, DFS_ITER_NR * sizeof(daos_obj_id_t));
	if (enum_buf == NULL) {
		D_FREE(kds);
		return ENOMEM;
	}

	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	d_iov_set(&iov, enum_buf, DFS_ITER_NR * sizeof(daos_obj_id_t));
	sgl.sg_iovs = &iov;

	while (!daos_anchor_is_eof(&anchor)) {
		uint32_t nr = DFS_ITER_NR;
		uint32_t i;
		char    *ptr;

		rc = daos_obj_list_dkey(dfs->hlm_oh, DAOS_TX_NONE, &nr, kds, &sgl, &anchor, NULL);
		if (rc) {
			D_ERROR("daos_obj_list_dkey() on HLM failed " DF_RC "\n", DP_RC(rc));
			D_GOTO(out, rc = daos_der2errno(rc));
		}

		if (nr == 0)
			continue;

		ptr = enum_buf;
		for (i = 0; i < nr; i++) {
			struct hlm_check_entry *entry;
			daos_obj_id_t          *oid_ptr;
			struct dfs_entry        hlm_entry = {0};

			if (kds[i].kd_key_len != sizeof(daos_obj_id_t)) {
				D_ERROR("Unexpected dkey size in HLM: %lu\n",
					(unsigned long)kds[i].kd_key_len);
				D_GOTO(out, rc = EINVAL);
			}

			oid_ptr = (daos_obj_id_t *)ptr;

			/* Fetch the ref_cnt from HLM for this OID */
			rc = hlm_fetch_entry(dfs->hlm_oh, DAOS_TX_NONE, oid_ptr, &hlm_entry);
			if (rc) {
				D_ERROR("Failed to fetch HLM entry for " DF_OID ": %d\n",
					DP_OID(*oid_ptr), rc);
				D_GOTO(out, rc);
			}

			/**
			 * Only regular files (array type) should be in HLM. If we find a
			 * non-array object, this is corruption - punch the HLM entry.
			 */
			if (!daos_is_array_type(daos_obj_id2type(*oid_ptr))) {
				D_ERROR("Invalid object type in HLM. OID: " DF_OID ", type: %d\n",
					DP_OID(*oid_ptr), daos_obj_id2type(*oid_ptr));
				if (flags & (DFS_CHECK_REMOVE | DFS_CHECK_RELINK)) {
					daos_key_t dkey;

					D_PRINT("Removing invalid object from HLM entry\n");
					d_iov_set(&dkey, oid_ptr, sizeof(daos_obj_id_t));
					rc = daos_obj_punch_dkeys(dfs->hlm_oh, DAOS_TX_NONE, 0, 1,
								  &dkey, NULL);
					if (rc) {
						D_ERROR("Failed to punch invalid HLM entry " DF_OID
							" " DF_RC "\n",
							DP_OID(*oid_ptr), DP_RC(rc));
						D_GOTO(out, rc = daos_der2errno(rc));
					}
				}
				ptr += kds[i].kd_key_len;
				continue;
			}

			D_ALLOC_PTR(entry);
			if (entry == NULL)
				D_GOTO(out, rc = ENOMEM);

			oid_cp(&entry->hce_oid, *oid_ptr);
			entry->hce_stored_linkcnt = hlm_entry.ref_cnt;
			entry->hce_cur_linkcnt    = 0;

			rc = d_hash_rec_insert(hlm_hash, &entry->hce_oid, sizeof(daos_obj_id_t),
					       &entry->hce_link, true);
			if (rc) {
				D_ERROR("Failed to insert HLM entry into hash " DF_RC "\n",
					DP_RC(rc));
				D_FREE(entry);
				D_GOTO(out, rc = daos_der2errno(rc));
			}

			ptr += kds[i].kd_key_len;
		}
	}

out:
	D_FREE(kds);
	D_FREE(enum_buf);
	return rc;
}

/** Context for HLM link count verification traverse callback */
struct hlm_verify_arg {
	dfs_t   *dfs;
	uint64_t flags;
	uint64_t mismatches;
	uint64_t fixed;
};

/**
 * Callback for traversing HLM hash table to verify and fix link counts.
 * If hce_cur_linkcnt != hce_stored_linkcnt, update the HLM entry.
 */
static int
hlm_verify_linkcnt_cb(d_list_t *link, void *arg)
{
	struct hlm_verify_arg  *varg = (struct hlm_verify_arg *)arg;
	struct hlm_check_entry *hce  = hlm_check_obj(link);
	dfs_t                  *dfs  = varg->dfs;
	daos_key_t              dkey;
	d_sg_list_t             sgl;
	d_iov_t                 sg_iovs[3];
	daos_iod_t              iod;
	daos_recx_t             recxs[3];
	struct timespec         now;
	int                     rc;

	/**
	 * No directory entries point to this OID - it's an orphan in HLM.
	 * This could happen if:
	 * 1. Object exists but unmarked - handled by Pass 1 (REMOVE) or Pass 2 (RELINK)
	 * 2. Object doesn't exist - HLM entry is garbage, punch it
	 * 3. HLM entry has ref_cnt=0 stored (invalid state)
	 *
	 * We punch the HLM entry here if repair flags are set. If the object
	 * exists and gets relinked in Pass 2, the HLM entry will be recreated.
	 */
	if (hce->hce_cur_linkcnt == 0) {
		D_PRINT("HLM entry " DF_OID " has no directory entries (stored=%lu, cur=%lu)\n",
			DP_OID(hce->hce_oid), (unsigned long)hce->hce_stored_linkcnt,
			(unsigned long)hce->hce_cur_linkcnt);

		varg->mismatches++;
		if (varg->flags & (DFS_CHECK_REMOVE | DFS_CHECK_RELINK)) {
			d_iov_set(&dkey, &hce->hce_oid, sizeof(daos_obj_id_t));
			rc = daos_obj_punch_dkeys(dfs->hlm_oh, DAOS_TX_NONE, 0, 1, &dkey, NULL);
			if (rc && rc != -DER_NONEXIST) {
				D_ERROR("Failed to punch orphan HLM entry " DF_OID " " DF_RC "\n",
					DP_OID(hce->hce_oid), DP_RC(rc));
				return daos_der2errno(rc);
			}
			varg->fixed++;
		}
		return 0;
	}

	/** No mismatch - counts match */
	if (hce->hce_cur_linkcnt == hce->hce_stored_linkcnt)
		return 0;

	varg->mismatches++;

	/**
	 * Link count mismatch detected. The stored link count in HLM doesn't
	 * match the actual number of directory entries pointing to this OID.
	 * Update HLM with the correct (current) link count.
	 */
	D_PRINT("HLM entry " DF_OID " link count mismatch (stored=%lu, cur=%lu)\n",
		DP_OID(hce->hce_oid), (unsigned long)hce->hce_stored_linkcnt,
		(unsigned long)hce->hce_cur_linkcnt);

	/** Only fix if repair flags are set */
	if (!(varg->flags & (DFS_CHECK_REMOVE | DFS_CHECK_RELINK)))
		return 0;

	/* Get current time for ctime update */
	rc = clock_gettime(CLOCK_REALTIME, &now);
	if (rc) {
		D_ERROR("clock_gettime() failed: %d (%s)\n", errno, strerror(errno));
		return errno;
	}

	/* Update ref_cnt and ctime in HLM */
	d_iov_set(&dkey, &hce->hce_oid, sizeof(daos_obj_id_t));
	d_iov_set(&iod.iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	iod.iod_nr      = 3;
	recxs[0].rx_idx = REF_CNT_IDX;
	recxs[0].rx_nr  = sizeof(uint64_t);
	recxs[1].rx_idx = CTIME_IDX;
	recxs[1].rx_nr  = sizeof(uint64_t);
	recxs[2].rx_idx = CTIME_NSEC_IDX;
	recxs[2].rx_nr  = sizeof(uint64_t);
	iod.iod_recxs   = recxs;
	iod.iod_type    = DAOS_IOD_ARRAY;
	iod.iod_size    = 1;

	d_iov_set(&sg_iovs[0], &hce->hce_cur_linkcnt, sizeof(uint64_t));
	d_iov_set(&sg_iovs[1], &now.tv_sec, sizeof(uint64_t));
	d_iov_set(&sg_iovs[2], &now.tv_nsec, sizeof(uint64_t));
	sgl.sg_nr     = 3;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs   = sg_iovs;

	rc = daos_obj_update(dfs->hlm_oh, DAOS_TX_NONE, DAOS_COND_DKEY_UPDATE, &dkey, 1, &iod, &sgl,
			     NULL);
	if (rc) {
		D_ERROR("Failed to update HLM ref_cnt for " DF_OID " " DF_RC "\n",
			DP_OID(hce->hce_oid), DP_RC(rc));
		return daos_der2errno(rc);
	}

	varg->fixed++;
	return 0;
}

static int
fetch_mark_oids(daos_handle_t coh, daos_obj_id_t oid, daos_key_desc_t *kds, char *enum_buf,
		struct dfs_oit_args *args)
{
	daos_handle_t oh;
	d_sg_list_t   sgl, entry_sgl;
	d_iov_t       iov, sg_iov;
	daos_recx_t   recx;
	uint32_t      nr;
	daos_iod_t    iod;
	d_iov_t       marker;
	bool          mark_data = true;
	daos_anchor_t anchor    = {0};
	int           rc, rc2;

	rc = daos_obj_open(coh, oid, DAOS_OO_RW, &oh, NULL);
	if (rc) {
		D_ERROR("daos_obj_open() failed " DF_RC "\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	/** set sgl for enumeration */
	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	d_iov_set(&iov, enum_buf, DFS_ITER_ENTRY_BUF);
	sgl.sg_iovs = &iov;

	/** set sgl for fetch */
	entry_sgl.sg_nr     = 1;
	entry_sgl.sg_nr_out = 0;
	entry_sgl.sg_iovs   = &sg_iov;

	d_iov_set(&iod.iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	recx.rx_idx   = OID_IDX;
	recx.rx_nr    = sizeof(daos_obj_id_t);
	iod.iod_nr    = 1;
	iod.iod_recxs = &recx;
	iod.iod_type  = DAOS_IOD_ARRAY;
	iod.iod_size  = 1;

	d_iov_set(&marker, &mark_data, sizeof(mark_data));
	while (!daos_anchor_is_eof(&anchor)) {
		uint32_t i;
		char    *ptr;

		nr = DFS_ITER_NR;
		rc = daos_obj_list_dkey(oh, DAOS_TX_NONE, &nr, kds, &sgl, &anchor, NULL);
		if (rc) {
			D_ERROR("daos_obj_list_dkey() failed " DF_RC "\n", DP_RC(rc));
			D_GOTO(out_obj, rc = daos_der2errno(rc));
		}

		/** for every entry, fetch its oid and mark it in the oit table */
		for (ptr = enum_buf, i = 0; i < nr; i++) {
			daos_obj_id_t entry_oid;
			daos_key_t    dkey;

			d_iov_set(&dkey, ptr, kds[i].kd_key_len);
			d_iov_set(&sg_iov, &entry_oid, sizeof(daos_obj_id_t));

			rc = daos_obj_fetch(oh, DAOS_TX_NONE, DAOS_COND_DKEY_FETCH, &dkey, 1, &iod,
					    &entry_sgl, NULL, NULL);
			if (rc) {
				D_ERROR("daos_obj_fetch() failed " DF_RC "\n", DP_RC(rc));
				D_GOTO(out_obj, rc = daos_der2errno(rc));
			}

			/** mark oid in the oit table */
			rc = daos_oit_mark(args->oit, entry_oid, &marker, NULL);
			if (rc && rc != -DER_NONEXIST) {
				D_ERROR("daos_oit_mark() failed " DF_RC "\n", DP_RC(rc));
				D_GOTO(out_obj, rc = daos_der2errno(rc));
			}
			rc = 0;
			ptr += kds[i].kd_key_len;
		}
	}

out_obj:
	rc2 = daos_obj_close(oh, NULL);
	if (rc == 0)
		rc = rc2;
	return rc;
}

static int
oit_mark_cb(dfs_t *dfs, dfs_obj_t *parent, const char name[], void *args)
{
	struct dfs_oit_args *oit_args = (struct dfs_oit_args *)args;
	dfs_obj_t           *obj;
	daos_obj_id_t        oid;
	d_iov_t              marker;
	bool                 mark_data = true;
	struct timespec      current_time;
	d_list_t            *rlink;
	int                  rc;
	int                  rc2;

	rc = clock_gettime(CLOCK_REALTIME, &current_time);
	if (rc)
		return errno;
	oit_args->num_scanned++;
	if (current_time.tv_sec - oit_args->print_time >= DFS_ELAPSED_TIME) {
		D_PRINT("DFS checker: Scanned " DF_U64 " files/directories (runtime: " DF_U64
			" sec)\n",
			oit_args->num_scanned, current_time.tv_sec - oit_args->start_time);
		oit_args->print_time = current_time.tv_sec;
	}

	/** open the entry name and get the oid */
	rc = dfs_lookup_rel(dfs, parent, name, O_RDWR | O_NOFOLLOW, &obj, NULL, NULL);
	if (rc) {
		D_ERROR("dfs_lookup_rel() of %s failed: %d\n", name, rc);
		return rc;
	}

	rc = dfs_obj2id(obj, &oid);
	if (rc)
		D_GOTO(out_obj, rc);

	/** Check if the OID exists in HLM hash table */
	if (oit_args->hlm_hash != NULL) {
		rlink = d_hash_rec_find(oit_args->hlm_hash, &oid, sizeof(daos_obj_id_t));
		if (rlink != NULL) {
			struct hlm_check_entry *hce = hlm_check_obj(rlink);

			/** Increment current link count */
			hce->hce_cur_linkcnt++;

			/**
			 * File found in HLM but dentry missing hardlink bit.
			 * Note: Only array-type objects are added to hlm_hash
			 * (directories/symlinks are filtered in hlm_populate_check_hash).
			 */
			if (!dfs_is_hardlink(obj->mode)) {
				D_PRINT("OID " DF_OID " (parent " DF_OID ", name '%s') in HLM but "
					"dentry missing hardlink bit\n",
					DP_OID(oid), DP_OID(parent->oid), name);
				if (oit_args->flags & (DFS_CHECK_REMOVE | DFS_CHECK_RELINK)) {
					rc = set_hardlink_bit(dfs, DAOS_TX_NONE, parent->oh, obj,
							      obj->mode);
					if (rc) {
						D_ERROR("Failed to set hardlink bit for " DF_OID
							": %d\n",
							DP_OID(oid), rc);
						D_GOTO(out_obj, rc);
					}
				}
			}
		} else if (dfs_is_hardlink(obj->mode)) {
			/**
			 * Dentry has hardlink bit set but no HLM entry exists.
			 * This is corruption - the file claims to be a hardlink
			 * but has no metadata in HLM to support it. Clear the bit
			 * so the file becomes accessible as a regular file.
			 */
			D_PRINT("OID " DF_OID " (parent " DF_OID ", name '%s') has hardlink bit "
				"set but no HLM entry exists\n",
				DP_OID(oid), DP_OID(parent->oid), name);
			if (oit_args->flags & (DFS_CHECK_REMOVE | DFS_CHECK_RELINK)) {
				rc = clear_hardlink_bit(dfs, DAOS_TX_NONE, parent->oh, obj,
							obj->mode);
				if (rc) {
					D_ERROR("Failed to clear hardlink bit for " DF_OID ": %d\n",
						DP_OID(oid), rc);
					D_GOTO(out_obj, rc);
				}
			}
		}
	}

	/**
	 * Directories and symlinks should never have the hardlink bit set.
	 * This is corruption - clear the bit.
	 * Note: We check MODE_HARDLINK_BIT directly instead of dfs_is_hardlink()
	 * because dfs_is_hardlink() requires S_ISREG() which is false here.
	 */
	if ((S_ISDIR(obj->mode) || S_ISLNK(obj->mode)) && (obj->mode & MODE_HARDLINK_BIT)) {
		D_WARN("OID " DF_OID " (parent " DF_OID ", name '%s') is %s but has "
		       "hardlink bit set - needs clearing\n",
		       DP_OID(oid), DP_OID(parent->oid), name,
		       S_ISDIR(obj->mode) ? "directory" : "symlink");
		if (oit_args->flags & (DFS_CHECK_REMOVE | DFS_CHECK_RELINK)) {
			rc = clear_hardlink_bit(dfs, DAOS_TX_NONE, parent->oh, obj, obj->mode);
			if (rc) {
				D_ERROR("Failed to clear hardlink bit for " DF_OID ": %d\n",
					DP_OID(oid), rc);
				D_GOTO(out_obj, rc);
			}
		}
	}

	if (oit_args->flags & DFS_CHECK_VERIFY) {
		rc = daos_obj_verify(dfs->coh, oid, oit_args->snap_epoch);
		if (rc == -DER_NOSYS) {
			oit_args->skipped++;
		} else if (rc == -DER_MISMATCH) {
			oit_args->failed++;
			if (oit_args->flags & DFS_CHECK_PRINT)
				D_PRINT("" DF_OID " failed data consistency check!\n", DP_OID(oid));
		} else if (rc) {
			D_ERROR("daos_obj_verify() failed " DF_RC "\n", DP_RC(rc));
			D_GOTO(out_obj, rc = daos_der2errno(rc));
		}
	}

	d_iov_set(&marker, &mark_data, sizeof(mark_data));
	rc = daos_oit_mark(oit_args->oit, oid, &marker, NULL);
	/*
	 * If the entry exists but the file or directory are empty, the corresponding oid itself has
	 * not been written to, so it doesn't exist in the OIT. The mark operation would return
	 * NONEXIST in this case, so check and avoid returning an error in this case.
	 */
	if (rc && rc != -DER_NONEXIST) {
		D_ERROR("Failed to mark OID in OIT: " DF_RC "\n", DP_RC(rc));
		D_GOTO(out_obj, rc = daos_der2errno(rc));
	}
	rc = 0;

	/** descend into directories */
	if (S_ISDIR(obj->mode)) {
		daos_anchor_t anchor     = {0};
		uint32_t      nr_entries = DFS_ITER_NR;

		while (!daos_anchor_is_eof(&anchor)) {
			rc = dfs_iterate(dfs, obj, &anchor, &nr_entries, DFS_MAX_NAME * nr_entries,
					 oit_mark_cb, args);
			if (rc) {
				D_ERROR("dfs_iterate() failed: %d\n", rc);
				D_GOTO(out_obj, rc);
			}
			nr_entries = DFS_ITER_NR;
		}
	}

out_obj:
	rc2 = dfs_release(obj);
	return rc ? rc : rc2;
}

static int
adjust_chunk_size(daos_handle_t coh, daos_obj_id_t oid, daos_key_desc_t *kds, char *enum_buf,
		  uint64_t *_max_offset)
{
	daos_handle_t oh;
	daos_anchor_t anchor = {0};
	d_sg_list_t   sgl;
	d_iov_t       iov;
	uint64_t      max_offset = *_max_offset;
	int           rc, rc2;

	rc = daos_obj_open(coh, oid, DAOS_OO_RW, &oh, NULL);
	if (rc) {
		D_ERROR("daos_obj_open() failed " DF_RC "\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	/** iterate over all (integer) dkeys and then query the max record / offset */
	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	d_iov_set(&iov, enum_buf, DFS_ITER_DKEY_BUF);
	sgl.sg_iovs = &iov;

	while (!daos_anchor_is_eof(&anchor)) {
		uint32_t nr  = DFS_ITER_NR;
		char    *ptr = &enum_buf[0];
		uint32_t i;

		rc = daos_obj_list_dkey(oh, DAOS_TX_NONE, &nr, kds, &sgl, &anchor, NULL);
		if (rc) {
			D_ERROR("daos_obj_list_dkey() failed " DF_RC "\n", DP_RC(rc));
			D_GOTO(out_obj, rc = daos_der2errno(rc));
		}

		if (nr == 0)
			continue;

		for (i = 0; i < nr; i++) {
			daos_key_t  dkey, akey;
			uint64_t    dkey_val, offset;
			char        akey_val = '0';
			daos_recx_t recx;

			memcpy(&dkey_val, ptr, kds[i].kd_key_len);
			ptr += kds[i].kd_key_len;
			d_iov_set(&dkey, &dkey_val, sizeof(uint64_t));
			d_iov_set(&akey, &akey_val, 1);
			rc = daos_obj_query_key(oh, DAOS_TX_NONE, DAOS_GET_RECX | DAOS_GET_MAX,
						&dkey, &akey, &recx, NULL);
			if (rc) {
				D_ERROR("daos_obj_query_key() failed " DF_RC "\n", DP_RC(rc));
				D_GOTO(out_obj, rc = daos_der2errno(rc));
			}

			/** maintain the highest offset seen in each dkey */
			offset = recx.rx_idx + recx.rx_nr;
			if (max_offset < offset)
				max_offset = offset;
		}
	}

	*_max_offset = max_offset;
out_obj:
	rc2 = daos_obj_close(oh, NULL);
	if (rc == 0)
		rc = daos_der2errno(rc2);
	return rc;
}

int
dfs_cont_check(daos_handle_t poh, const char *cont, uint64_t flags, const char *name)
{
	dfs_t               *dfs;
	daos_handle_t        coh;
	struct dfs_oit_args *oit_args = NULL;
	daos_epoch_t         snap_epoch;
	dfs_obj_t           *lf, *now_dir;
	daos_anchor_t        anchor            = {0};
	uint32_t             nr_entries        = DFS_ITER_NR, i;
	daos_obj_id_t        oids[DFS_ITER_NR] = {0};
	daos_key_desc_t     *kds               = NULL;
	char                *dkey_enum_buf     = NULL;
	char                *entry_enum_buf    = NULL;
	uint64_t             unmarked_entries  = 0;
	struct d_hash_table *hlm_hash          = NULL;
	d_iov_t              marker;
	bool                 mark_data = true;
	daos_epoch_range_t   epr;
	struct timespec      now, current_time;
	uid_t                uid      = geteuid();
	gid_t                gid      = getegid();
	unsigned int         co_flags = DAOS_COO_EX;
	char                 now_name[24];
	struct tm           *now_tm;
	daos_size_t          len;
	int                  rc, rc2;

	rc = clock_gettime(CLOCK_REALTIME, &now);
	if (rc)
		return errno;
	now_tm = localtime(&now.tv_sec);
	len    = strftime(now_name, sizeof(now_name), "%Y-%m-%d-%H:%M:%S", now_tm);
	if (len == 0)
		return EINVAL;
	D_PRINT("DFS checker: Start (%s)\n", now_name);

	if (flags & DFS_CHECK_RELINK && flags & DFS_CHECK_REMOVE) {
		D_ERROR("can't request remove and link to l+f at the same time\n");
		return EINVAL;
	}

	if (flags & DFS_CHECK_EVICT_ALL)
		co_flags |= DAOS_COO_EVICT_ALL;

	rc = daos_cont_open(poh, cont, co_flags, &coh, NULL, NULL);
	if (rc) {
		D_ERROR("daos_cont_open() failed: " DF_RC "\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	rc = dfs_mount(poh, coh, O_RDWR, &dfs);
	if (rc) {
		D_ERROR("dfs_mount() failed (%d)\n", rc);
		D_GOTO(out_cont, rc);
	}

	D_PRINT("DFS checker: Create OIT table\n");
	/** create snapshot for OIT */
	rc = daos_cont_create_snap_opt(coh, &snap_epoch, NULL, DAOS_SNAP_OPT_CR | DAOS_SNAP_OPT_OIT,
				       NULL);
	if (rc) {
		D_ERROR("daos_cont_create_snap_opt failed " DF_RC "\n", DP_RC(rc));
		D_GOTO(out_dfs, rc = daos_der2errno(rc));
	}

	D_ALLOC_PTR(oit_args);
	if (oit_args == NULL)
		D_GOTO(out_dfs, rc = ENOMEM);
	oit_args->flags      = flags;
	oit_args->snap_epoch = snap_epoch;
	oit_args->start_time = now.tv_sec;
	oit_args->print_time = now.tv_sec;

	/** Open OIT table */
	rc = daos_oit_open(coh, snap_epoch, &oit_args->oit, NULL);
	if (rc) {
		D_ERROR("daos_oit_open failed " DF_RC "\n", DP_RC(rc));
		D_GOTO(out_snap, rc = daos_der2errno(rc));
	}

	/** get and mark the SB and root OIDs */
	d_iov_set(&marker, &mark_data, sizeof(mark_data));
	rc = daos_oit_mark(oit_args->oit, dfs->super_oid, &marker, NULL);
	if (rc) {
		D_ERROR("Failed to mark SB OID in OIT: " DF_RC "\n", DP_RC(rc));
		D_GOTO(out_oit, rc = daos_der2errno(rc));
	}
	rc = daos_oit_mark(oit_args->oit, dfs->root.oid, &marker, NULL);
	if (rc && rc != -DER_NONEXIST) {
		D_ERROR("Failed to mark ROOT OID in OIT: " DF_RC "\n", DP_RC(rc));
		D_GOTO(out_oit, rc = daos_der2errno(rc));
	}
	/** mark the HLM object as reachable */
	rc = daos_oit_mark(oit_args->oit, dfs->hlm_oid, &marker, NULL);
	if (rc && rc != -DER_NONEXIST) {
		D_ERROR("Failed to mark HLM OID in OIT: " DF_RC "\n", DP_RC(rc));
		D_GOTO(out_oit, rc = daos_der2errno(rc));
	}
	rc = 0;

	if (flags & DFS_CHECK_VERIFY) {
		rc = daos_obj_verify(coh, dfs->super_oid, snap_epoch);
		if (rc == -DER_NOSYS) {
			oit_args->skipped++;
		} else if (rc == -DER_MISMATCH) {
			oit_args->failed++;
			if (flags & DFS_CHECK_PRINT)
				D_PRINT("SB Object " DF_OID " failed data consistency check!\n",
					DP_OID(dfs->super_oid));
		} else if (rc) {
			D_ERROR("daos_obj_verify() failed " DF_RC "\n", DP_RC(rc));
			D_GOTO(out_oit, rc = daos_der2errno(rc));
		}

		rc = daos_obj_verify(coh, dfs->root.oid, snap_epoch);
		if (rc == -DER_NOSYS) {
			oit_args->skipped++;
		} else if (rc == -DER_MISMATCH) {
			oit_args->failed++;
			if (flags & DFS_CHECK_PRINT)
				D_PRINT("ROOT Object " DF_OID " failed data consistency check!\n",
					DP_OID(dfs->root.oid));
		} else if (rc) {
			D_ERROR("daos_obj_verify() failed " DF_RC "\n", DP_RC(rc));
			D_GOTO(out_oit, rc = daos_der2errno(rc));
		}

		if (!daos_obj_id_is_nil(dfs->hlm_oid)) {
			rc = daos_obj_verify(coh, dfs->hlm_oid, snap_epoch);
			if (rc == -DER_NOSYS) {
				oit_args->skipped++;
			} else if (rc == -DER_MISMATCH) {
				oit_args->failed++;
				if (flags & DFS_CHECK_PRINT)
					D_PRINT("HLM Object " DF_OID
						" failed data consistency check!\n",
						DP_OID(dfs->hlm_oid));
			} else if (rc) {
				D_ERROR("daos_obj_verify() failed " DF_RC "\n", DP_RC(rc));
				D_GOTO(out_oit, rc = daos_der2errno(rc));
			}
		}
	}

	/** Create and populate hash table with HLM entries for hardlink verification */
	rc = d_hash_table_create(D_HASH_FT_NOLOCK, HLM_CHECK_HASH_BITS, NULL, &hlm_check_hash_ops,
				 &hlm_hash);
	if (rc) {
		D_ERROR("Failed to create HLM check hash table " DF_RC "\n", DP_RC(rc));
		D_GOTO(out_oit, rc = daos_der2errno(rc));
	}

	D_PRINT("DFS checker: Populating HLM hash table for hardlink verification\n");
	rc = hlm_populate_check_hash(dfs, hlm_hash, flags);
	if (rc) {
		D_ERROR("Failed to populate HLM check hash table: %d\n", rc);
		D_GOTO(out_hlm_hash, rc);
	}

	/** Set the hash table in oit_args so oit_mark_cb can access it */
	oit_args->hlm_hash = hlm_hash;

	D_PRINT("DFS checker: Iterating namespace and marking objects\n");
	oit_args->num_scanned = 2;
	/** iterate through the namespace and mark OITs starting from the root object */
	while (!daos_anchor_is_eof(&anchor)) {
		rc = dfs_iterate(dfs, &dfs->root, &anchor, &nr_entries, DFS_MAX_NAME * nr_entries,
				 oit_mark_cb, oit_args);
		if (rc) {
			D_ERROR("dfs_iterate() failed: %d\n", rc);
			D_GOTO(out_hlm_hash, rc);
		}

		nr_entries = DFS_ITER_NR;
	}

	rc = clock_gettime(CLOCK_REALTIME, &current_time);
	if (rc)
		D_GOTO(out_hlm_hash, rc = errno);
	D_PRINT("DFS checker: marked " DF_U64 " files/directories (runtime: " DF_U64 " sec))\n",
		oit_args->num_scanned, current_time.tv_sec - oit_args->start_time);

	/** Create lost+found directory to link unmarked oids there. */
	if (flags & DFS_CHECK_RELINK) {
		rc = dfs_open(dfs, NULL, "lost+found", S_IFDIR | 0755, O_CREAT | O_RDWR, 0, 0, NULL,
			      &lf);
		if (rc) {
			D_ERROR("Failed to create/open lost+found directory: %d\n", rc);
			D_GOTO(out_hlm_hash, rc);
		}

		if (name == NULL) {
			/*
			 * Create a directory with current timestamp in l+f where leaked oids will
			 * be linked in this run.
			 */
			D_PRINT("DFS checker: Leaked OIDs will be inserted in /lost+found/%s\n",
				now_name);
		} else {
			D_PRINT("DFS checker: Leaked OIDs will be inserted in /lost+found/%s\n",
				name);
		}

		rc = dfs_open(dfs, lf, name ? name : now_name, S_IFDIR | 0755,
			      O_CREAT | O_RDWR | O_EXCL, 0, 0, NULL, &now_dir);
		if (rc) {
			D_ERROR("Failed to create dir in lost+found: %d\n", rc);
			D_GOTO(out_lf1, rc);
		}

		/** allocate kds and enumeration buffers */
		D_ALLOC_ARRAY(kds, DFS_ITER_NR);
		if (kds == NULL)
			D_GOTO(out_lf2, rc = ENOMEM);

		/** Allocate a buffer to store the array int dkeys */
		D_ALLOC_ARRAY(dkey_enum_buf, DFS_ITER_DKEY_BUF);
		if (dkey_enum_buf == NULL)
			D_GOTO(out_lf2, rc = ENOMEM);

		/** Allocate a buffer to store the entries */
		D_ALLOC_ARRAY(entry_enum_buf, DFS_ITER_ENTRY_BUF);
		if (entry_enum_buf == NULL)
			D_GOTO(out_lf2, rc = ENOMEM);
	}

	/*
	 * list all unmarked oids and print / remove / punch. In the case of the L+F relink flag, we
	 * need 2 passes instead of 1:
	 * Pass 1: check directories only and descend to mark all oids in the namespace of each dir.
	 * Pass 2: relink remaining oids in the L+F root that are unmarked still after first pass.
	 */
	D_PRINT("DFS checker: Checking unmarked OIDs (Pass 1)\n");
	oit_args->num_scanned = 0;
	memset(&anchor, 0, sizeof(anchor));
	/** Start Pass 1 */
	while (!daos_anchor_is_eof(&anchor)) {
		nr_entries = DFS_ITER_NR;
		rc = daos_oit_list_unmarked(oit_args->oit, oids, &nr_entries, &anchor, NULL);
		if (rc) {
			D_ERROR("daos_oit_list_unmarked() failed: " DF_RC "\n", DP_RC(rc));
			D_GOTO(out_lf2, rc = daos_der2errno(rc));
		}

		rc = clock_gettime(CLOCK_REALTIME, &current_time);
		if (rc) {
			D_ERROR("clock_gettime() failed: %d (%s)\n", errno, strerror(errno));
			D_GOTO(out_lf2, rc = errno);
		}
		oit_args->num_scanned += nr_entries;
		if (current_time.tv_sec - oit_args->print_time >= DFS_ELAPSED_TIME) {
			D_PRINT("DFS checker: Checked " DF_U64 " objects (runtime: " DF_U64
				" sec)\n",
				oit_args->num_scanned, current_time.tv_sec - oit_args->start_time);
			oit_args->print_time = current_time.tv_sec;
		}

		for (i = 0; i < nr_entries; i++) {
			if (flags & DFS_CHECK_RELINK) {
				enum daos_otype_t otype = daos_obj_id2type(oids[i]);

				/** Pass 1 - if a file is seen, skip in this pass */
				if (daos_is_array_type(otype))
					continue;

				/** for a directory, mark the oids reachable from it */
				rc = fetch_mark_oids(coh, oids[i], kds, entry_enum_buf, oit_args);
				if (rc)
					D_GOTO(out_lf2, rc);
				continue;
			}

			if (flags & DFS_CHECK_PRINT) {
				d_list_t *hlink = NULL;

				if (hlm_hash != NULL)
					hlink = d_hash_rec_find(hlm_hash, &oids[i],
								sizeof(daos_obj_id_t));
				if (hlink != NULL) {
					struct hlm_check_entry *hce = hlm_check_obj(hlink);

					D_PRINT("oid[" DF_U64 "]: " DF_OID " (hardlink, "
						"HLM ref_cnt=%lu)\n",
						unmarked_entries, DP_OID(oids[i]),
						(unsigned long)hce->hce_stored_linkcnt);
				} else {
					D_PRINT("oid[" DF_U64 "]: " DF_OID "\n", unmarked_entries,
						DP_OID(oids[i]));
				}
			}

			if (flags & DFS_CHECK_VERIFY) {
				rc = daos_obj_verify(dfs->coh, oids[i], snap_epoch);
				if (rc == -DER_NOSYS) {
					oit_args->skipped++;
				} else if (rc == -DER_MISMATCH) {
					oit_args->failed++;
					if (flags & DFS_CHECK_PRINT)
						D_PRINT("" DF_OID
							" failed data consistency check!\n",
							DP_OID(oids[i]));
				} else if (rc) {
					D_ERROR("daos_obj_verify() failed " DF_RC "\n", DP_RC(rc));
					D_GOTO(out_hlm_hash, rc = daos_der2errno(rc));
				}
			}

			if (flags & DFS_CHECK_REMOVE) {
				daos_handle_t oh;
				daos_key_t    hlm_dkey;

				rc = daos_obj_open(dfs->coh, oids[i], DAOS_OO_RW, &oh, NULL);
				if (rc)
					D_GOTO(out_hlm_hash, rc = daos_der2errno(rc));

				rc = daos_obj_punch(oh, DAOS_TX_NONE, 0, NULL);
				if (rc) {
					daos_obj_close(oh, NULL);
					D_GOTO(out_hlm_hash, rc = daos_der2errno(rc));
				}

				rc = daos_obj_close(oh, NULL);
				if (rc)
					D_GOTO(out_hlm_hash, rc = daos_der2errno(rc));

				/** Also punch any HLM entry for this OID */
				if (!daos_obj_id_is_nil(dfs->hlm_oid)) {
					d_iov_set(&hlm_dkey, &oids[i], sizeof(daos_obj_id_t));
					rc = daos_obj_punch_dkeys(dfs->hlm_oh, DAOS_TX_NONE, 0, 1,
								  &hlm_dkey, NULL);
					if (rc && rc != -DER_NONEXIST) {
						D_ERROR("Failed to punch HLM entry " DF_RC "\n",
							DP_RC(rc));
						D_GOTO(out_hlm_hash, rc = daos_der2errno(rc));
					}
				}

				/** Remove from HLM check hash table if it exists */
				d_hash_rec_delete(hlm_hash, &oids[i], sizeof(daos_obj_id_t));
			}

			unmarked_entries++;
		}
	}

	/** Start Pass 2 only if L+F flag is used */
	if (!(flags & DFS_CHECK_RELINK))
		goto done;

	D_PRINT("DFS checker: Checking unmarked OIDs (Pass 2)\n");
	oit_args->num_scanned = 0;
	memset(&anchor, 0, sizeof(anchor));
	while (!daos_anchor_is_eof(&anchor)) {
		nr_entries = DFS_ITER_NR;
		rc = daos_oit_list_unmarked(oit_args->oit, oids, &nr_entries, &anchor, NULL);
		if (rc) {
			D_ERROR("daos_oit_list_unmarked() failed: " DF_RC "\n", DP_RC(rc));
			D_GOTO(out_lf2, rc = daos_der2errno(rc));
		}

		rc = clock_gettime(CLOCK_REALTIME, &current_time);
		if (rc) {
			D_ERROR("clock_gettime() failed: %d (%s)\n", errno, strerror(errno));
			D_GOTO(out_lf2, rc = errno);
		}
		oit_args->num_scanned += nr_entries;
		if (current_time.tv_sec - oit_args->print_time >= DFS_ELAPSED_TIME) {
			D_PRINT("DFS checker: Checked " DF_U64 " objects (runtime: " DF_U64
				" sec)\n",
				oit_args->num_scanned, current_time.tv_sec - oit_args->start_time);
			oit_args->print_time = current_time.tv_sec;
		}

		for (i = 0; i < nr_entries; i++) {
			struct dfs_entry  entry = {0};
			enum daos_otype_t otype = daos_obj_id2type(oids[i]);
			char              oid_name[DFS_MAX_NAME + 1];

			if (flags & DFS_CHECK_PRINT) {
				d_list_t *hlink = NULL;

				if (hlm_hash != NULL)
					hlink = d_hash_rec_find(hlm_hash, &oids[i],
								sizeof(daos_obj_id_t));
				if (hlink != NULL) {
					struct hlm_check_entry *hce = hlm_check_obj(hlink);

					D_PRINT("oid[" DF_U64 "]: " DF_OID " (hardlink, "
						"HLM ref_cnt=%lu)\n",
						unmarked_entries, DP_OID(oids[i]),
						(unsigned long)hce->hce_stored_linkcnt);
				} else {
					D_PRINT("oid[" DF_U64 "]: " DF_OID "\n", unmarked_entries,
						DP_OID(oids[i]));
				}
			}

			if (flags & DFS_CHECK_VERIFY) {
				rc = daos_obj_verify(dfs->coh, oids[i], snap_epoch);
				if (rc == -DER_NOSYS) {
					oit_args->skipped++;
				} else if (rc == -DER_MISMATCH) {
					oit_args->failed++;
					if (flags & DFS_CHECK_PRINT)
						D_PRINT("" DF_OID
							" failed data consistency check!\n",
							DP_OID(oids[i]));
				} else if (rc) {
					D_ERROR("daos_obj_verify() failed " DF_RC "\n", DP_RC(rc));
					D_GOTO(out_lf2, rc = daos_der2errno(rc));
				}
			}

			if (daos_is_array_type(otype))
				entry.mode = S_IFREG | 0600;
			else
				entry.mode = S_IFDIR | 0700;
			entry.uid = uid;
			entry.gid = gid;
			oid_cp(&entry.oid, oids[i]);
			entry.mtime = entry.ctime = now.tv_sec;
			entry.mtime_nano = entry.ctime_nano = now.tv_nsec;
			entry.chunk_size                    = dfs->attr.da_chunk_size;

			/**
			 * Check if this OID has an HLM entry. If so, this is a hardlink
			 * that lost all its directory entries. Set the hardlink bit and
			 * update the HLM link count to 1.
			 */
			if (hlm_hash != NULL) {
				d_list_t *hlink;

				hlink = d_hash_rec_find(hlm_hash, &oids[i], sizeof(daos_obj_id_t));
				if (hlink != NULL) {
					struct hlm_check_entry *hce = hlm_check_obj(hlink);
					struct dfs_entry        hlm_entry = {0};
					int                     delta;

					/* Set hardlink bit in the new entry */
					entry.mode |= MODE_HARDLINK_BIT;

					/* Calculate delta to set link count to 1 */
					delta = 1 - (int)hce->hce_stored_linkcnt;

					/* Fetch HLM entry and update link count using delta */
					rc = hlm_fetch_entry(dfs->hlm_oh, DAOS_TX_NONE, &oids[i],
							     &hlm_entry);
					if (rc) {
						D_ERROR("Failed to fetch HLM entry for " DF_OID
							": %d\n",
							DP_OID(oids[i]), rc);
						D_GOTO(out_lf2, rc);
					}
					rc = hlm_update_ref_cnt(dfs, DAOS_TX_NONE, &hlm_entry,
								delta);
					if (rc) {
						D_ERROR("Failed to update HLM ref_cnt for " DF_OID
							": %d\n",
							DP_OID(oids[i]), rc);
						D_GOTO(out_lf2, rc);
					}

					/* Update hash entry to reflect new link count */
					hce->hce_cur_linkcnt    = 1;
					hce->hce_stored_linkcnt = 1;

					if (flags & DFS_CHECK_PRINT)
						D_PRINT("Restoring hardlink " DF_OID
							" with link count 1\n",
							DP_OID(oids[i]));
				}
			}

			/*
			 * If this is a regular file / array object, the user might have used a
			 * different chunk size than the default one. Since we lost the directory
			 * entry where the chunks size for that file would have been stored, we can
			 * make a best attempt to set the chunk size. To do that, we get the largest
			 * offset of every dkey in the array to see if it's larger than the default
			 * chunk size. If it is, adjust the chunk size to that, otherwise leave the
			 * chunk size as default. This of course does not account for the fact that
			 * if the chunk size is smaller than the default or is larger than the
			 * largest offset seen then we have added or removed existing holes from the
			 * file respectively. This can be fixed later by the user by adjusting the
			 * chunk size to the correct one they know using the daos tool.
			 */
			if (daos_is_array_type(otype)) {
				rc = adjust_chunk_size(dfs->coh, oids[i], kds, dkey_enum_buf,
						       &entry.chunk_size);
				if (rc)
					D_GOTO(out_lf2, rc);
				if (flags & DFS_CHECK_PRINT &&
				    entry.chunk_size != dfs->attr.da_chunk_size)
					D_PRINT("Adjusting File (" DF_OID ") chunk size to %zu\n",
						DP_OID(oids[i]), entry.chunk_size);
			}

			len = sprintf(oid_name, "%" PRIu64 ".%" PRIu64 "", oids[i].hi, oids[i].lo);
			D_ASSERT(len <= DFS_MAX_NAME);
			rc = insert_entry(dfs->layout_v, now_dir->oh, DAOS_TX_NONE, oid_name, len,
					  DAOS_COND_DKEY_INSERT, &entry);
			if (rc) {
				D_ERROR("Failed to insert leaked entry in l+f (%d)\n", rc);
				D_GOTO(out_lf2, rc);
			}
			unmarked_entries++;
		}
	}

done:
	/** Verify and fix HLM link counts */
	if (hlm_hash != NULL) {
		struct hlm_verify_arg varg = {0};

		D_PRINT("DFS checker: Verifying HLM link counts\n");
		varg.dfs   = dfs;
		varg.flags = flags;
		rc         = d_hash_table_traverse(hlm_hash, hlm_verify_linkcnt_cb, &varg);
		if (rc) {
			D_ERROR("HLM link count verification failed: %d\n", rc);
			D_GOTO(out_lf2, rc);
		}
		if (varg.mismatches > 0) {
			D_PRINT("DFS checker: Found " DF_U64 " HLM link count mismatches, "
				"fixed " DF_U64 "\n",
				varg.mismatches, varg.fixed);
		}
	}

	rc = clock_gettime(CLOCK_REALTIME, &current_time);
	if (rc)
		D_GOTO(out_lf2, rc = errno);
	D_PRINT("DFS checker: Done! (runtime: " DF_U64 " sec)\n",
		current_time.tv_sec - oit_args->start_time);
	D_PRINT("DFS checker: Number of leaked OIDs in namespace = " DF_U64 "\n", unmarked_entries);
	if (flags & DFS_CHECK_VERIFY) {
		if (oit_args->failed) {
			D_ERROR("" DF_U64 " OIDs failed data consistency check!\n",
				oit_args->failed);
			D_GOTO(out_lf2, rc = EIO);
		}
	}
out_lf2:
	D_FREE(kds);
	D_FREE(dkey_enum_buf);
	D_FREE(entry_enum_buf);
	if (flags & DFS_CHECK_RELINK) {
		rc2 = dfs_release(now_dir);
		if (rc == 0)
			rc = rc2;
	}
out_lf1:
	if (flags & DFS_CHECK_RELINK) {
		rc2 = dfs_release(lf);
		if (rc == 0)
			rc = rc2;
	}
out_hlm_hash:
	if (hlm_hash != NULL) {
		rc2 = d_hash_table_destroy(hlm_hash, true);
		if (rc2)
			D_ERROR("Failed to destroy HLM check hash table " DF_RC "\n", DP_RC(rc2));
	}
out_oit:
	rc2 = daos_oit_close(oit_args->oit, NULL);
	if (rc == 0)
		rc = daos_der2errno(rc2);
out_snap:
	D_FREE(oit_args);
	epr.epr_hi = epr.epr_lo = snap_epoch;
	rc2 = daos_cont_destroy_snap(coh, epr, NULL);
	if (rc2 != 0)
		D_ERROR("Failed to destroy OID table: " DF_RC "\n", DP_RC(rc2));
	if (rc == 0)
		rc = daos_der2errno(rc2);
out_dfs:
	rc2 = dfs_umount(dfs);
	if (rc == 0)
		rc = rc2;
out_cont:
	rc2 = daos_cont_close(coh, NULL);
	if (rc == 0)
		rc = daos_der2errno(rc2);

	return rc;
}

int
dfs_recreate_sb(daos_handle_t coh, dfs_attr_t *attr)
{
	uint32_t                   props[]   = {DAOS_PROP_CO_LAYOUT_TYPE, DAOS_PROP_CO_ROOTS};
	const int                  num_props = ARRAY_SIZE(props);
	daos_prop_t               *prop;
	struct daos_prop_entry    *entry;
	struct daos_prop_co_roots *roots;
	daos_handle_t              super_oh;
	struct dfs_entry           rentry = {0};
	struct timespec            now;
	int                        i;
	int                        rc, rc2;

	if (attr == NULL)
		return EINVAL;

	prop = daos_prop_alloc(num_props);
	if (prop == NULL)
		return ENOMEM;

	for (i = 0; i < num_props; i++)
		prop->dpp_entries[i].dpe_type = props[i];

	rc = daos_cont_query(coh, NULL, prop, NULL);
	if (rc) {
		D_ERROR("daos_cont_query() failed, " DF_RC "\n", DP_RC(rc));
		D_GOTO(out_prop, rc = daos_der2errno(rc));
	}

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_LAYOUT_TYPE);
	if (entry == NULL || entry->dpe_val != DAOS_PROP_CO_LAYOUT_POSIX) {
		D_ERROR("container is not of type POSIX\n");
		D_GOTO(out_prop, rc = EINVAL);
	}

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_ROOTS);
	D_ASSERT(entry != NULL);
	roots = (struct daos_prop_co_roots *)entry->dpe_val_ptr;
	if (daos_obj_id_is_nil(roots->cr_oids[0]) || daos_obj_id_is_nil(roots->cr_oids[1])) {
		D_ERROR("Invalid superblock or root object ID\n");
		D_GOTO(out_prop, rc = EIO);
	}

	/** Recreate SB */
	rc = open_sb(coh, true, true, DAOS_OO_RW, roots->cr_oids[0], attr, &super_oh, NULL);
	if (rc)
		D_GOTO(out_prop, rc);

	/** relink the root object */
	rentry.oid  = roots->cr_oids[1];
	rentry.mode = S_IFDIR | 0755;
	rc          = clock_gettime(CLOCK_REALTIME, &now);
	if (rc)
		D_GOTO(out_super, rc = errno);
	rentry.mtime = rentry.ctime = now.tv_sec;
	rentry.mtime_nano = rentry.ctime_nano = now.tv_nsec;
	rentry.uid                            = geteuid();
	rentry.gid                            = getegid();

	rc = insert_entry(DFS_LAYOUT_VERSION, super_oh, DAOS_TX_NONE, "/", 1, DAOS_COND_DKEY_INSERT,
			  &rentry);
	if (rc) {
		D_ERROR("Failed to insert root entry: %d (%s)\n", rc, strerror(rc));
		D_GOTO(out_super, rc);
	}

out_super:
	rc2 = daos_obj_close(super_oh, NULL);
	if (rc == 0)
		rc = daos_der2errno(rc2);
out_prop:
	daos_prop_free(prop);
	return rc;
}

int
dfs_relink_root(daos_handle_t coh)
{
	uint32_t                   props[]   = {DAOS_PROP_CO_LAYOUT_TYPE, DAOS_PROP_CO_ROOTS};
	const int                  num_props = ARRAY_SIZE(props);
	daos_prop_t               *prop;
	struct daos_prop_entry    *entry;
	struct daos_prop_co_roots *roots;
	daos_handle_t              super_oh;
	dfs_layout_ver_t           layout_v;
	dfs_attr_t                 attr;
	bool                       exists;
	struct dfs_entry           rentry = {0};
	struct timespec            now;
	int                        i;
	int                        rc, rc2;

	prop = daos_prop_alloc(num_props);
	if (prop == NULL)
		return ENOMEM;

	for (i = 0; i < num_props; i++)
		prop->dpp_entries[i].dpe_type = props[i];

	rc = daos_cont_query(coh, NULL, prop, NULL);
	if (rc) {
		D_ERROR("daos_cont_query() failed, " DF_RC "\n", DP_RC(rc));
		D_GOTO(out_prop, rc = daos_der2errno(rc));
	}

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_LAYOUT_TYPE);
	if (entry == NULL || entry->dpe_val != DAOS_PROP_CO_LAYOUT_POSIX) {
		D_ERROR("container is not of type POSIX\n");
		D_GOTO(out_prop, rc = EINVAL);
	}

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_ROOTS);
	D_ASSERT(entry != NULL);
	roots = (struct daos_prop_co_roots *)entry->dpe_val_ptr;
	if (daos_obj_id_is_nil(roots->cr_oids[0]) || daos_obj_id_is_nil(roots->cr_oids[1])) {
		D_ERROR("Invalid superblock or root object ID\n");
		D_GOTO(out_prop, rc = EIO);
	}

	/** Verify SB */
	rc = open_sb(coh, false, false, DAOS_OO_RW, roots->cr_oids[0], &attr, &super_oh, &layout_v);
	if (rc)
		D_GOTO(out_prop, rc);

	/** Check if super object has the root entry */
	rc = fetch_entry(layout_v, super_oh, DAOS_TX_NONE, "/", 1, false, &exists, &rentry, 0, NULL,
			 NULL, NULL);
	if (rc) {
		D_ERROR("Failed to fetch object: %d (%s)\n", rc, strerror(rc));
		D_GOTO(out_super, rc);
	}
	if (exists) {
		D_PRINT("Root object already linked in SB\n");
		D_GOTO(out_super, rc = 0);
	}

	/** relink the root object */
	rentry.oid  = roots->cr_oids[1];
	rentry.mode = S_IFDIR | 0755;
	rc          = clock_gettime(CLOCK_REALTIME, &now);
	if (rc)
		D_GOTO(out_super, rc = errno);
	rentry.mtime = rentry.ctime = now.tv_sec;
	rentry.mtime_nano = rentry.ctime_nano = now.tv_nsec;
	rentry.uid                            = geteuid();
	rentry.gid                            = getegid();

	rc = insert_entry(layout_v, super_oh, DAOS_TX_NONE, "/", 1, DAOS_COND_DKEY_INSERT, &rentry);
	if (rc) {
		D_ERROR("Failed to insert root entry: %d (%s)\n", rc, strerror(rc));
		D_GOTO(out_super, rc);
	}

out_super:
	rc2 = daos_obj_close(super_oh, NULL);
	if (rc == 0)
		rc = daos_der2errno(rc2);
out_prop:
	daos_prop_free(prop);
	return rc;
}

int
dfs_obj_fix_type(dfs_t *dfs, dfs_obj_t *parent, const char *name)
{
	struct dfs_entry  entry     = {0};
	struct dfs_entry  hlm_entry = {0};
	bool              exists;
	bool              is_hardlink = false;
	daos_key_t        dkey;
	size_t            len;
	enum daos_otype_t otype;
	mode_t            mode;
	d_sg_list_t       sgl;
	d_iov_t           sg_iov;
	daos_recx_t       recx;
	daos_iod_t        iod;
	int               rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (parent == NULL)
		parent = &dfs->root;
	else if (!S_ISDIR(parent->mode))
		return ENOTDIR;

	rc = check_name(name, &len);
	if (rc)
		return rc;

	rc = fetch_entry(dfs->layout_v, parent->oh, DAOS_TX_NONE, name, len, true, &exists, &entry,
			 0, NULL, NULL, NULL);
	if (rc) {
		D_ERROR("Failed to fetch entry %s (%d)\n", name, rc);
		D_GOTO(out, rc);
	}
	if (exists == false)
		D_GOTO(out, rc = ENOENT);

	/** get the object type from the oid */
	otype = daos_obj_id2type(entry.oid);

	/** reset the type bits to 0 and set 700 permission bits */
	mode = S_IWUSR | S_IRUSR | S_IXUSR;
	/** set the type bits according to oid type and entry value */
	if (daos_is_array_type(otype)) {
		mode |= S_IFREG;
		D_PRINT("Setting entry type to S_IFREG\n");

		/** Check if this is a hardlink by looking up the HLM */
		rc = hlm_fetch_entry(dfs->hlm_oh, DAOS_TX_NONE, &entry.oid, &hlm_entry);
		if (rc == 0) {
			mode |= MODE_HARDLINK_BIT;
			is_hardlink = true;
			D_PRINT("Entry is a hardlink, setting hardlink bit\n");
		} else if (rc != ENOENT) {
			D_ERROR("Failed to fetch HLM entry (%d)\n", rc);
			D_GOTO(out, rc);
		}
	} else if (entry.value_len) {
		mode |= S_IFLNK;
		D_PRINT("Setting entry type to S_IFLNK\n");
	} else {
		mode |= S_IFDIR;
		D_PRINT("Setting entry type to S_IFDIR\n");
	}

	/** Update mode bits on storage */
	d_iov_set(&dkey, (void *)name, len);
	d_iov_set(&iod.iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	recx.rx_idx   = MODE_IDX;
	recx.rx_nr    = sizeof(mode_t);
	iod.iod_nr    = 1;
	iod.iod_recxs = &recx;
	iod.iod_type  = DAOS_IOD_ARRAY;
	iod.iod_size  = 1;
	d_iov_set(&sg_iov, &mode, sizeof(mode_t));
	sgl.sg_nr     = 1;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs   = &sg_iov;
	rc = daos_obj_update(parent->oh, DAOS_TX_NONE, DAOS_COND_DKEY_UPDATE, &dkey, 1, &iod, &sgl,
			     NULL);
	if (rc) {
		D_ERROR("Failed to update object type " DF_RC "\n", DP_RC(rc));
		D_GOTO(out, rc = daos_der2errno(rc));
	}

	/** Update mode bits in HLM entry if this is a hardlink */
	if (is_hardlink) {
		d_iov_set(&dkey, &entry.oid, sizeof(daos_obj_id_t));
		rc = daos_obj_update(dfs->hlm_oh, DAOS_TX_NONE, DAOS_COND_DKEY_UPDATE, &dkey, 1,
				     &iod, &sgl, NULL);
		if (rc) {
			D_ERROR("Failed to update HLM entry with new mode " DF_RC "\n", DP_RC(rc));
			D_GOTO(out, rc = daos_der2errno(rc));
		}
	}

out:
	if (entry.value)
		D_FREE(entry.value);
	return rc;
}

int
dfs_get_size_by_oid(dfs_t *dfs, daos_obj_id_t oid, daos_size_t chunk_size, daos_size_t *size)
{
	daos_handle_t oh;
	int           rc;

	if (dfs == NULL || !dfs->mounted)
		return EINVAL;
	if (daos_obj_id2type(oid) != DAOS_OT_ARRAY_BYTE)
		return EINVAL;

	rc =
	    daos_array_open_with_attr(dfs->coh, oid, dfs->th, DAOS_OO_RO, 1,
				      chunk_size ? chunk_size : dfs->attr.da_chunk_size, &oh, NULL);
	if (rc != 0) {
		D_ERROR("daos_array_open() failed: " DF_RC "\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	rc = daos_array_get_size(oh, dfs->th, size, NULL);
	if (rc) {
		daos_array_close(oh, NULL);
		D_ERROR("daos_array_get_size() failed: " DF_RC "\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	rc = daos_array_close(oh, NULL);
	return daos_der2errno(rc);
}

inline static bool
is_uid_invalid(uid_t uid)
{
	return uid == (uid_t)-1;
}

inline static bool
is_gid_invalid(gid_t gid)
{
	return gid == (gid_t)-1;
}

int
dfs_cont_set_owner(daos_handle_t coh, d_string_t user, uid_t uid, d_string_t group, gid_t gid)
{
	daos_key_t                 dkey;
	d_sg_list_t                sgl;
	d_iov_t                    sg_iovs[4];
	daos_iod_t                 iod;
	daos_recx_t                recxs[4];
	daos_handle_t              oh;
	int                        i;
	struct timespec            now;
	daos_prop_t               *prop;
	uint32_t                   props[]   = {DAOS_PROP_CO_LAYOUT_TYPE, DAOS_PROP_CO_ROOTS};
	const int                  num_props = ARRAY_SIZE(props);
	struct daos_prop_entry    *entry;
	struct daos_prop_co_roots *roots;
	int                        rc;

	prop = daos_prop_alloc(num_props);
	if (prop == NULL)
		return ENOMEM;

	for (i = 0; i < num_props; i++)
		prop->dpp_entries[i].dpe_type = props[i];
	rc = daos_cont_query(coh, NULL, prop, NULL);
	if (rc) {
		D_ERROR("daos_cont_query() failed, " DF_RC "\n", DP_RC(rc));
		D_GOTO(out_prop, rc = daos_der2errno(rc));
	}

	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_LAYOUT_TYPE);
	if (entry == NULL || entry->dpe_val != DAOS_PROP_CO_LAYOUT_POSIX) {
		rc = EINVAL;
		D_ERROR("container is not of type POSIX: %d (%s)\n", rc, strerror(rc));
		D_GOTO(out_prop, rc);
	}

	/** retrieve the SB OID */
	entry = daos_prop_entry_get(prop, DAOS_PROP_CO_ROOTS);
	if (entry == NULL) {
		rc = EINVAL;
		D_ERROR("Missing ROOTS property from POSIX container: %d (%s)\n", rc, strerror(rc));
		D_GOTO(out_prop, rc);
	}

	roots = (struct daos_prop_co_roots *)entry->dpe_val_ptr;
	if (daos_obj_id_is_nil(roots->cr_oids[0]) || daos_obj_id_is_nil(roots->cr_oids[1])) {
		D_ERROR("Invalid superblock or root object ID: %d (%s)\n", rc, strerror(rc));
		D_GOTO(out_prop, rc = EIO);
	}

	rc = clock_gettime(CLOCK_REALTIME, &now);
	if (rc)
		D_GOTO(out_prop, rc = errno);

	i               = 0;
	recxs[i].rx_idx = CTIME_IDX;
	recxs[i].rx_nr  = sizeof(uint64_t);
	d_iov_set(&sg_iovs[i], &now.tv_sec, sizeof(uint64_t));
	i++;

	recxs[i].rx_idx = CTIME_NSEC_IDX;
	recxs[i].rx_nr  = sizeof(uint64_t);
	d_iov_set(&sg_iovs[i], &now.tv_nsec, sizeof(uint64_t));
	i++;

	if (user != NULL) {
		if (is_uid_invalid(uid)) {
			rc = daos_acl_principal_to_uid(user, &uid);
			if (rc) {
				D_ERROR("daos_acl_principal_to_uid() failed: " DF_RC "\n",
					DP_RC(rc));
				D_GOTO(out_prop, rc = EINVAL);
			}
		}
		d_iov_set(&sg_iovs[i], &uid, sizeof(uid_t));
		recxs[i].rx_idx = UID_IDX;
		recxs[i].rx_nr  = sizeof(uid_t);
		i++;
	}

	if (group != NULL) {
		if (is_gid_invalid(gid)) {
			rc = daos_acl_principal_to_gid(group, &gid);
			if (rc) {
				D_ERROR("daos_acl_principal_to_gid() failed: " DF_RC "\n",
					DP_RC(rc));
				D_GOTO(out_prop, rc = EINVAL);
			}
		}
		d_iov_set(&sg_iovs[i], &gid, sizeof(gid_t));
		recxs[i].rx_idx = GID_IDX;
		recxs[i].rx_nr  = sizeof(gid_t);
		i++;
	}

	/* set the owner ACL - already checked user/group are real above, if needed */
	rc = daos_cont_set_owner_no_check(coh, user, group, NULL);
	if (rc) {
		D_ERROR("daos_cont_set_owner() failed, " DF_RC "\n", DP_RC(rc));
		D_GOTO(out_prop, rc = daos_der2errno(rc));
	}

	/** set root dkey as the entry name */
	d_iov_set(&dkey, "/", 1);
	d_iov_set(&iod.iod_name, INODE_AKEY_NAME, sizeof(INODE_AKEY_NAME) - 1);
	iod.iod_nr    = i;
	iod.iod_recxs = recxs;
	iod.iod_type  = DAOS_IOD_ARRAY;
	iod.iod_size  = 1;

	/** set sgl for update */
	sgl.sg_nr     = i;
	sgl.sg_nr_out = 0;
	sgl.sg_iovs   = &sg_iovs[0];

	/** Open SB object */
	rc = daos_obj_open(coh, roots->cr_oids[0], DAOS_OO_RW, &oh, NULL);
	if (rc) {
		D_ERROR("daos_obj_open() Failed, " DF_RC "\n", DP_RC(rc));
		D_GOTO(out_prop, rc = daos_der2errno(rc));
	}

	/** update the owner of the root group in the SB entry */
	rc = daos_obj_update(oh, DAOS_TX_NONE, DAOS_COND_DKEY_UPDATE, &dkey, 1, &iod, &sgl, NULL);
	if (rc) {
		daos_obj_close(oh, NULL);
		D_ERROR("Failed to update owner/group, " DF_RC "\n", DP_RC(rc));
		D_GOTO(out_prop, rc = daos_der2errno(rc));
	}
	rc = daos_obj_close(oh, NULL);
	if (rc)
		D_GOTO(out_prop, rc = daos_der2errno(rc));

out_prop:
	daos_prop_free(prop);
	return rc;
}

struct dfs_scan_args {
	time_t   start_time;
	time_t   print_time;
	uint64_t cur_depth;
	uint64_t max_depth;
	uint64_t num_files;
	uint64_t num_dirs;
	uint64_t num_symlinks;
	uint64_t total_bytes;
	uint64_t largest_file;
	uint64_t largest_dir;
	uint64_t num_scanned;
};

static int
scan_cb(dfs_t *dfs, dfs_obj_t *parent, const char name[], void *args)
{
	struct dfs_scan_args *scan_args = (struct dfs_scan_args *)args;
	dfs_obj_t            *obj;
	struct timespec       current_time;
	int                   rc;

	rc = clock_gettime(CLOCK_REALTIME, &current_time);
	if (rc)
		return errno;

	scan_args->num_scanned++;
	if (scan_args->cur_depth > scan_args->max_depth)
		scan_args->max_depth = scan_args->cur_depth;

	if (current_time.tv_sec - scan_args->print_time >= DFS_ELAPSED_TIME) {
		D_PRINT("DFS scanner: Scanned " DF_U64 " files/directories (runtime: " DF_U64
			" sec)\n",
			scan_args->num_scanned, current_time.tv_sec - scan_args->start_time);
		scan_args->print_time = current_time.tv_sec;
	}

	/** open the entry name */
	rc = dfs_lookup_rel(dfs, parent, name, O_RDONLY | O_NOFOLLOW, &obj, NULL, NULL);
	if (rc) {
		D_ERROR("dfs_lookup_rel() of %s failed: %d\n", name, rc);
		return rc;
	}

	/** descend into directories */
	if (S_ISDIR(obj->mode)) {
		daos_anchor_t anchor     = {0};
		uint32_t      nr_entries = DFS_ITER_NR;
		uint64_t      nr_total   = 0;

		scan_args->num_dirs++;
		while (!daos_anchor_is_eof(&anchor)) {
			scan_args->cur_depth++;
			rc = dfs_iterate(dfs, obj, &anchor, &nr_entries, DFS_MAX_NAME * nr_entries,
					 scan_cb, args);
			scan_args->cur_depth--;
			if (rc) {
				D_ERROR("dfs_iterate() failed: %d\n", rc);
				D_GOTO(out_obj, rc);
			}
			nr_total += nr_entries;
			nr_entries = DFS_ITER_NR;
		}
		if (scan_args->largest_dir < nr_total)
			scan_args->largest_dir = nr_total;
	} else if (S_ISLNK(obj->mode)) {
		scan_args->num_symlinks++;
	} else {
		struct stat stbuf;

		scan_args->num_files++;
		rc = dfs_ostat(dfs, obj, &stbuf);
		if (rc) {
			D_ERROR("dfs_ostat() failed: %d\n", rc);
			D_GOTO(out_obj, rc);
		}
		scan_args->total_bytes += stbuf.st_size;
		if (scan_args->largest_file < stbuf.st_size)
			scan_args->largest_file = stbuf.st_size;
	}

out_obj:
	rc = dfs_release(obj);
	return rc;
}

int
dfs_cont_scan(daos_handle_t poh, const char *cont, uint64_t flags, const char *subdir)
{
	dfs_t               *dfs;
	daos_handle_t        coh;
	struct dfs_scan_args scan_args  = {0};
	daos_anchor_t        anchor     = {0};
	uint32_t             nr_entries = DFS_ITER_NR;
	uint64_t             nr_total   = 0;
	struct timespec      now, current_time;
	char                 now_name[24];
	struct tm           *now_tm;
	daos_size_t          len;
	int                  rc, rc2;

	rc = clock_gettime(CLOCK_REALTIME, &now);
	if (rc)
		return errno;
	now_tm = localtime(&now.tv_sec);
	len    = strftime(now_name, sizeof(now_name), "%Y-%m-%d-%H:%M:%S", now_tm);
	if (len == 0)
		return EINVAL;
	D_PRINT("DFS scanner: Start (%s)\n", now_name);

	rc = daos_cont_open(poh, cont, DAOS_COO_RO, &coh, NULL, NULL);
	if (rc) {
		D_ERROR("daos_cont_open() failed: " DF_RC "\n", DP_RC(rc));
		return daos_der2errno(rc);
	}

	rc = dfs_mount(poh, coh, O_RDONLY, &dfs);
	if (rc) {
		D_ERROR("dfs_mount() failed (%d)\n", rc);
		D_GOTO(out_cont, rc);
	}

	scan_args.start_time = now.tv_sec;
	scan_args.print_time = now.tv_sec;
	scan_args.cur_depth  = 1; /** starting from root at depth 1 */

	/** TODO: add support for starting from the subdir args */

	/** iterate through the namespace */
	while (!daos_anchor_is_eof(&anchor)) {
		rc = dfs_iterate(dfs, &dfs->root, &anchor, &nr_entries, DFS_MAX_NAME * nr_entries,
				 scan_cb, &scan_args);
		if (rc) {
			D_ERROR("dfs_iterate() failed: %d\n", rc);
			D_GOTO(out, rc);
		}

		nr_total += nr_entries;
		nr_entries = DFS_ITER_NR;
	}

	if (scan_args.largest_dir < nr_total)
		scan_args.largest_dir = nr_total;

	rc = clock_gettime(CLOCK_REALTIME, &current_time);
	if (rc)
		D_GOTO(out, rc = errno);
	D_PRINT("DFS scanner: Done! (runtime: " DF_U64 " sec)\n",
		current_time.tv_sec - scan_args.start_time);

	D_PRINT("DFS scanner: " DF_U64 " scanned objects\n", scan_args.num_scanned);
	D_PRINT("DFS scanner: " DF_U64 " files\n", scan_args.num_files);
	D_PRINT("DFS scanner: " DF_U64 " symlinks\n", scan_args.num_symlinks);
	D_PRINT("DFS scanner: " DF_U64 " directories\n", scan_args.num_dirs);
	D_PRINT("DFS scanner: " DF_U64 " max tree depth\n", scan_args.max_depth);
	D_PRINT("DFS scanner: " DF_U64 " bytes of total data\n", scan_args.total_bytes);
	if (scan_args.num_files > 0)
		D_PRINT("DFS scanner: " DF_U64 " bytes per file on average\n",
			scan_args.total_bytes / scan_args.num_files);
	D_PRINT("DFS scanner: " DF_U64 " bytes is largest file size\n", scan_args.largest_file);
	D_PRINT("DFS scanner: " DF_U64 " entries in the largest directory\n",
		scan_args.largest_dir);

out:
	rc2 = dfs_umount(dfs);
	if (rc == 0)
		rc = rc2;
out_cont:
	rc2 = daos_cont_close(coh, NULL);
	if (rc == 0)
		rc = daos_der2errno(rc2);

	return rc;
}
