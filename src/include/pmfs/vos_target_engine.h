/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

 #ifndef __VOS_TARGET_ENGINE_H__
 #define __VOS_TARGET_ENGINE_H__

 #include "vos_tasks.h"

 #if D_HAS_WARNING(4, "-Wframe-larger-than=")
 #pragma GCC diagnostic ignored "-Wframe-larger-than="
 #endif

/* Struct of linked list container */
struct pmfs_container {
	/**< container open handle **/
	daos_handle_t tsc_coh;
	/**< container uuid **/
	uuid_t tsc_cont_uuid;
	/**< open flag >  **/
	bool is_open;
	/**< list of container >**/
	d_list_t cl;
};

/* Struct of linked list pool */
struct pmfs_pool {
	/** INPUT: should be initialized by caller */
	/** optional, pmem file name */
	char *tsc_pmem_file;
	/** pool uuid */
	uuid_t tsc_pool_uuid;
	/** pool NVMe partition size */
	uint64_t tsc_nvme_size;
	/** pool SCM partition size */
	uint64_t tsc_scm_size;
	/** optional, if need to skip container create */
	bool tsc_skip_cont_create;
	/** pool open handle */
	daos_handle_t tsc_poh;
	/** pmfs containers associated with a pool> **/
	struct pmfs_container pmfs_container;
	/** pmfs list of pmfs pool */
	d_list_t pl;
};

/**
 * vos target startup context
 * It is input parameter which carries pools and their containers uuid etc.
 */
struct pmfs_context {
	/** struct of pmfs pool */
	struct pmfs_pool pmfs_pool;
	/** optional, if need to skip pool create */
	bool tsc_skip_pool_create;
	/** initialization steps, internal use only */
	int tsc_init;
	/** target engine */
	struct vos_target_engine *tsc_engine;
};

struct pmfs_obj_info {
	daos_unit_oid_t oid;
	uint64_t len;
	uint32_t nr;
	void *buf;
	daos_key_desc_t *kds;
};

struct scan_context {
	/* the uuid of a known pool */
	uuid_t pool_uuid;
	/* a handler of the pool */
	daos_handle_t pool_hdl;
	/* container pointer of the pool */
	struct pmfs_container cur_cont;
	/* information of obj */
	struct pmfs_obj_info uoi;
};

/* vos target engine definition */
/* containers init are embedded in pool init
 * so no need add it at here.
 */
struct vos_target_engine {
	char *vte_name;
	int (*vte_init)(void);
	void (*vte_fini)(void);
	int (*vte_pool_init)(struct pmfs_context *);
	void (*vte_pool_fini)(struct pmfs_context *);
};

int pmfs_scan_cont(struct scan_context *ctx, struct pmfs_obj_info **uoi,
		   enum task_op opc);
/* add combine ctx to pool list */
void pmfs_ctx_combine_pool_list(struct pmfs_context *pmfs_ctx);
/* add combine to pool fini list */
void pmfs_combine_pool_fini_list(d_list_t *fini_list);
/* this is to find the pmem matched pool */
struct pmfs_pool *pmfs_find_pool(const char *pmem);
/* function uses to scan pool before mount started */
int pmfs_scan_pool(struct scan_context *ctx);
/* allow user add a pool separately */
int engine_pool_single_node_init(struct pmfs_pool *pmfs_pool,
				 bool tsc_skip_pool_create);
/* vos target context init */
int vt_ctx_init(struct pmfs_context *vtx);
/* vos target context fini */
void vt_ctx_fini(struct pmfs_context *vtx);
 #endif
