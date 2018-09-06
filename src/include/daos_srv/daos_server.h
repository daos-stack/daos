/**
 * (C) Copyright 2016-2018 Intel Corporation.
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
 * DAOS server-side infrastructure
 * Provides a modular interface to load server-side code on demand
 */

#ifndef __DSS_API_H__
#define __DSS_API_H__

#include <daos/common.h>
#include <daos/rpc.h>
#include <daos_srv/iv.h>
#include <daos_srv/vos_types.h>
#include <daos_event.h>
#include <daos_task.h>

#include <pthread.h>
#include <hwloc.h>
#include <abt.h>
#include <cart/iv.h>

/** Number of execution streams started or cores used */
extern unsigned int	dss_nxstreams;

/** Server node topoloby */
extern hwloc_topology_t	dss_topo;

/** Storage path (hack) */
extern const char      *storage_path;

/**
 * Stackable Module API
 * Provides a modular interface to load and register server-side code on
 * demand. A module is composed of:
 * - a set of request handlers which are registered when the module is loaded.
 * - a server-side API (see header files suffixed by "_srv") used for
 *   inter-module direct calls.
 *
 * For now, all loaded modules are assumed to be trustful, but sandboxes can be
 * implemented in the future.
 */
/*
 * Thead-local storage
 */
struct dss_thread_local_storage {
	uint32_t	dtls_tag;
	void		**dtls_values;
};

enum dss_module_tag {
	/* Server tag */
	DAOS_SERVER_TAG	= 1 << 0,
};

/* The module key descriptor for each server thread */
struct dss_module_key {
	/* Indicate where the keys should be instantiated */
	enum dss_module_tag dmk_tags;

	/* The position inside the dss_module_keys */
	int dmk_index;
	/* init keys for context */
	void  *(*dmk_init)(const struct dss_thread_local_storage *dtls,
			   struct dss_module_key *key);

	/* fini keys for context */
	void  (*dmk_fini)(const struct dss_thread_local_storage *dtls,
			  struct dss_module_key *key, void *data);
};

extern pthread_key_t dss_tls_key;
extern struct dss_module_key *dss_module_keys[];
#define DAOS_MODULE_KEYS_NR 10

static inline struct dss_thread_local_storage *
dss_tls_get()
{
	return (struct dss_thread_local_storage *)
		pthread_getspecific(dss_tls_key);
}

/**
 * Get value from context by the key
 *
 * Get value inside dtls by key. So each module will use this API to
 * retrieve their own value in the thread context.
 *
 * \param[in] dtls	the thread context.
 * \param[in] key	key used to retrieve the dtls_value.
 *
 * \retval		the dtls_value retrieved by key.
 */
static inline void *
dss_module_key_get(struct dss_thread_local_storage *dtls,
		   struct dss_module_key *key)
{
	D_ASSERT(key->dmk_index >= 0);
	D_ASSERT(key->dmk_index < DAOS_MODULE_KEYS_NR);
	D_ASSERT(dss_module_keys[key->dmk_index] == key);
	D_ASSERT(dtls != NULL);

	return dtls->dtls_values[key->dmk_index];
}

void dss_register_key(struct dss_module_key *key);
void dss_unregister_key(struct dss_module_key *key);

struct dss_module_info {
	crt_context_t		dmi_ctx;
	struct eio_xs_context	*dmi_nvme_ctxt;
	struct dss_xstream	*dmi_xstream;
	int			dmi_tid;
	tse_sched_t		dmi_sched;
	uint64_t		dmi_tse_ult_created:1;
};

extern struct dss_module_key	daos_srv_modkey;

static inline struct dss_module_info *
dss_get_module_info(void)
{
	struct dss_module_info *dmi;
	struct dss_thread_local_storage *dtc;

	dtc = dss_tls_get();
	dmi = (struct dss_module_info *)
	      dss_module_key_get(dtc, &daos_srv_modkey);
	return dmi;
}

static inline tse_sched_t *
dss_tse_scheduler(void)
{
	return &dss_get_module_info()->dmi_sched;
}

/**
 * Module facility feature bits
 * DSS_FAC_LOAD_CLI - the module requires loading client stack.
 */
#define DSS_FAC_LOAD_CLI (0x1ULL)

/**
 * Each module should provide a dss_module structure which defines the module
 * interface. The name of the allocated structure must be the library name
 * (without the ".so" extension) suffixed by "module". This symbol will be
 * looked up automatically when the module library is loaded and failed if not
 * found.
 *
 * For instance, the dmg module reports a "sm_name" of "daos_mgmt_srv", the
 * actual library filename is libdaos_mgmt_srv.so and it defines a dss_module
 * structure called daos_mgmt_srv_module.
 */
struct dss_module {
	/* Name of the module */
	const char		 *sm_name;
	/* Module id see enum dss_module_id */
	int			  sm_mod_id;
	/* Module version */
	int			  sm_ver;
	/* Module facility bitmask, can be feature bits like DSS_FAC_LOAD_CLI */
	uint64_t		  sm_facs;
	/* key of local thread storage */
	struct dss_module_key	*sm_key;
	/* Initialization function, invoked just after successful load */
	int			(*sm_init)(void);
	/* Finalization function, invoked just before module unload */
	int			(*sm_fini)(void);
	/* Setup function, invoked after starting progressing */
	int			(*sm_setup)(void);
	/* Cleanup function, invoked before stopping progressing */
	int			(*sm_cleanup)(void);
	/* Array of RPC definition for request sent by client nodes, last entry
	 * of the array must be empty */
	struct daos_rpc		 *sm_cl_rpcs;
	/* Array of RPC definition for request sent by other servers, last entry
	 * of the array must be empty */
	struct daos_rpc		 *sm_srv_rpcs;

	/* RPC handler of these RPC, last entry of the array must be empty */
	struct daos_rpc_handler	 *sm_handlers;
};

int
dss_parameters_set(unsigned int key_id, uint64_t value);

typedef ABT_pool (*dss_abt_pool_choose_cb_t)(crt_rpc_t *rpc, ABT_pool *pools);

void dss_abt_pool_choose_cb_register(unsigned int mod_id,
				     dss_abt_pool_choose_cb_t cb);
int dss_ult_create(void (*func)(void *), void *arg,
		   int stream_id, size_t stack_size, ABT_thread *ult);
int dss_ult_create_all(void (*func)(void *), void *arg);
int dss_ult_create_execute(int (*func)(void *), void *arg,
			   void (*user_cb)(void *), void *cb_args,
			   int stream_id, size_t stack_size);

/* Pack return codes with additional argument to reduce */
struct dss_stream_arg_type {
	/** return value */
	int		st_rc;
	/** collective arguments for streams */
	void		*st_coll_args;
	/** optional reduce args for aggregation */
	void		*st_arg;
};

struct dss_coll_stream_args {
	struct dss_stream_arg_type *csa_streams;
};

struct dss_coll_ops {
	/**
	 * Function to be invoked by dss_collective
	 *
	 * \param f_args		[IN]	Arguments for function
	 */
	int				(*co_func)(void *f_args);

	/**
	 * Callback for reducing after dss_collective (optional)
	 *
	 * \param a_args		[IN/OUT]
	 *					Aggregator arguments for
	 *					reducing results
	 * \param s_args		[IN]	Reduce arguments for this
	 *					current stream
	 */
	void				(*co_reduce)(void *a_args,
						     void *s_args);

	/**
	 * Alloc function for allocating reduce arguments (optional)
	 *
	 * \param args			[IN/OUT] coll_args for this streams
	 * \param aggregator_args	[IN]	 aggregator args for
	 *					 initializatuin
	 */
	void				(*co_reduce_arg_alloc)
					(struct dss_stream_arg_type *args,
					 void *a_args);
	/**
	 * Free the allocated reduce arguments
	 * (Mandatory if co_rarg_alloc was provided)
	 *
	 * \param args			[IN]	coll_args for this stream
	 */
	void				(*co_reduce_arg_free)
					(struct dss_stream_arg_type *args);
};

struct dss_coll_args {
	/** Arguments for dss_collective func (Mandatory) */
	void				*ca_func_args;
	void				*ca_aggregator;
	/** Stream arguments for all streams */
	struct dss_coll_stream_args	ca_stream_args;
};

/* Generic dss_collective with custom aggregator
 *
 * TODO: rename these functions, thread & task are too generic name and
 * DAOS has already used task for something else.
 *
 * These functions should be dss_ult_collective/dss_tlt_collective.
 */
int
dss_task_collective_reduce(struct dss_coll_ops *ops,
			   struct dss_coll_args *coll_args);
int
dss_thread_collective_reduce(struct dss_coll_ops *ops,
			     struct dss_coll_args *coll_args);

int dss_task_collective(int (*func)(void *), void *arg);
int dss_thread_collective(int (*func)(void *), void *arg);

int dss_task_run(tse_task_t *task, unsigned int type, tse_task_cb_t cb,
		 void *arg);
unsigned int dss_get_threads_number(void);

/* Convert Argobots errno to DAOS ones. */
static inline int
dss_abterr2der(int abt_errno)
{
	switch (abt_errno) {
	case ABT_SUCCESS:	return 0;
	case ABT_ERR_MEM:	return -DER_NOMEM;
	default:		return -DER_INVAL;
	}
}

int dss_rpc_send(crt_rpc_t *rpc);
int dss_group_create(crt_group_id_t id, d_rank_list_t *ranks,
		     crt_group_t **group);
int dss_group_destroy(crt_group_t *group);
void dss_sleep(int ms);
int dss_rpc_reply(crt_rpc_t *rpc, unsigned int fail_loc);

enum {
	/** Min Value */
	DSS_OFFLOAD_MIN		= -1,
	/** Does computation on same ULT */
	DSS_OFFLOAD_ULT		= 1,
	/** Offload to an accelarator */
	DSS_OFFLOAD_ACC		= 2,
	/** Max value */
	DSS_OFFLOAD_MAX		= 7
};

struct dss_acc_task {
	/**
	 * Type of offload for this operation
	 * \param at_offload_type	[IN] type of accelaration
	 */
	int		at_offload_type;
	/**
	 * Opcode for this offload task
	 * \param at_opcode		[IN] opcode for offload
	 */
	int		at_opcode;
	/**
	 * Buffer arguments for task offload
	 * \param at_params		[IN] buffer for offload
	 */
	void		*at_params;
	/**
	 * Callback required for offload task
	 * \param cb_args		[IN] arguments for offload
	 */
	int		(*at_cb)(void *cb_args);
};

/**
 * Generic offload call abstraction for accelaration with both
 * ULT and FPGA
 */
int dss_acc_offload(struct dss_acc_task *at_args);

/** Different type of ES pools, there are 3 pools for now
 *
 *  DSS_POOL_PRIV     Private pool: I/O requests will be added to this pool.
 *  DSS_POOL_SHARE    Shared pool: Other requests and ULT created during
 *                    processing rpc.
 *  DSS_POOL_REBUILD  Private pool: pools specially for rebuild tasks.
 */
enum {
	DSS_POOL_PRIV,
	DSS_POOL_SHARE,
	DSS_POOL_REBUILD,
	DSS_POOL_CNT,
};

/* DAOS object API on the server side */
int ds_obj_open(daos_handle_t coh, daos_obj_id_t oid,
		daos_epoch_t epoch, unsigned int mode,
		daos_handle_t *oh);
int ds_obj_close(daos_handle_t obj_hl);

int ds_obj_list_akey(daos_handle_t oh, daos_epoch_t epoch,
		 daos_key_t *dkey, uint32_t *nr,
		 daos_key_desc_t *kds, daos_sg_list_t *sgl,
		 daos_anchor_t *anchor);

int ds_obj_fetch(daos_handle_t oh, daos_epoch_t epoch,
		 daos_key_t *dkey, unsigned int nr,
		 daos_iod_t *iods, daos_sg_list_t *sgls,
		 daos_iom_t *maps);
int ds_obj_list_obj(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
		daos_key_t *akey, daos_size_t *size, uint32_t *nr,
		daos_key_desc_t *kds, daos_epoch_range_t *eprs,
		d_sg_list_t *sgl, daos_anchor_t *anchor,
		daos_anchor_t *dkey_anchor, daos_anchor_t *akey_anchor);

typedef int (*dss_vos_iterate_cb_t)(daos_handle_t ih, vos_iter_entry_t *entry,
				    vos_iter_type_t type,
				    vos_iter_param_t *param, void *arg);

int dss_vos_iterate(vos_iter_type_t type, vos_iter_param_t *param,
		    daos_anchor_t *anchor, dss_vos_iterate_cb_t cb,
		    void *arg);

struct dss_enum_arg {
	/* Iteration fields */
	vos_iter_param_t	param;
	bool			recursive;	/* enumerate lower levels */
	bool			fill_recxs;	/* type == S||R */
	daos_anchor_t		obj_anchor;	/* type == OBJ (<= if recur) */
	daos_anchor_t		dkey_anchor;	/* type == DKEY (<= if recur) */
	daos_anchor_t		akey_anchor;	/* type == AKEY (<= if recur) */
	daos_anchor_t		recx_anchor;	/* type == S||R (<= if recur) */
	daos_epoch_range_t     *eprs;
	int			eprs_cap;
	int			eprs_len;

	/* Buffer fields */
	union {
		struct {	/* !recxs_eprs */
			daos_key_desc_t	       *kds;
			int			kds_cap;
			int			kds_len;
			daos_sg_list_t	       *sgl;
			int			sgl_idx;
		};
		struct {	/* recxs_eprs && type == S||R */
			daos_recx_t	       *recxs;
			int			recxs_cap;
			int			recxs_len;
		};
	};
	daos_size_t		inline_thres;	/* type == S||R || recursive */
	int			rnum;		/* records num (type == S||R) */
	daos_size_t		rsize;		/* record size (type == S||R) */
};

int dss_enum_pack(vos_iter_type_t type, struct dss_enum_arg *arg);

/**
 * Used by dss_enum_unpack to accumulate recxs that can be stored with a single
 * VOS update.
 *
 * ui_oid and ui_dkey are only filled by dss_enum_unpack for certain
 * enumeration types, as commented after each field. Callers may fill ui_oid,
 * for instance, when the enumeration type is VOS_ITER_DKEY, to pass the object
 * ID to the callback.
 *
 * ui_iods, ui_recxs_caps, and ui_sgls are arrays of the same capacity
 * (ui_iods_cap) and length (ui_iods_len). That is, the iod in ui_iods[i] can
 * hold at most ui_recxs_caps[i] recxs, which have their inline data described
 * by ui_sgls[i]. ui_sgls is optional. If ui_iods[i].iod_recxs[j] has no inline
 * data, then ui_sgls[i].sg_iovs[j] will be empty.
 */
struct dss_enum_unpack_io {
	daos_unit_oid_t	ui_oid;		/**< type <= OBJ */
	daos_key_t	ui_dkey;	/**< type <= DKEY */
	daos_iod_t     *ui_iods;
	int		ui_iods_cap;
	int		ui_iods_len;
	int	       *ui_recxs_caps;
	daos_epoch_t	ui_dkey_eph;
	daos_epoch_t   *ui_akey_ephs;
	daos_sg_list_t *ui_sgls;	/**< optional */
	uuid_t		ui_cookie;
	uint32_t	ui_version;
};

void dss_enum_unpack_io_init(struct dss_enum_unpack_io *io, daos_iod_t *iods,
			     int *recxs_caps, daos_sg_list_t *sgls,
			     daos_epoch_t *ephs, int iods_cap);
void dss_enum_unpack_io_clear(struct dss_enum_unpack_io *io);
void dss_enum_unpack_io_fini(struct dss_enum_unpack_io *io);

typedef int (*dss_enum_unpack_cb_t)(struct dss_enum_unpack_io *io, void *arg);

int dss_enum_unpack(vos_iter_type_t type, struct dss_enum_arg *arg,
		    dss_enum_unpack_cb_t cb, void *cb_arg);

#endif /* __DSS_API_H__ */
