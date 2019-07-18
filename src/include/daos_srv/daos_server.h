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
 * DAOS server-side infrastructure
 * Provides a modular interface to load server-side code on demand
 */

#ifndef __DSS_API_H__
#define __DSS_API_H__

#include <daos/common.h>
#include <daos/drpc.h>
#include <daos/rpc.h>
#include <daos_srv/iv.h>
#include <daos_srv/vos_types.h>
#include <daos_event.h>
#include <daos_task.h>
#include <pthread.h>
#include <hwloc.h>
#include <abt.h>
#include <cart/iv.h>

/** number of target (XS set) per server */
extern unsigned int	dss_tgt_nr;

/** Storage path (hack) */
extern const char      *dss_storage_path;

/** NVMe config file */
extern const char      *dss_nvme_conf;

/** Socket Directory */
extern const char      *dss_socket_dir;

/** NVMe shm_id for enabling SPDK multi-process mode */
extern int		dss_nvme_shm_id;


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

#define D_TIME_START(sp, op)			\
do {						\
	if ((sp) == NULL)			\
		break;				\
	D_ASSERT(op < MAX_PROFILE_OP);		\
	D_ASSERT(sp->sp_time[op] == 0);		\
	sp->sp_time[op] = daos_get_ntime();	\
} while (0)

#define D_TIME_END(sp, op)			\
do {						\
	if ((sp) == NULL)			\
		break;				\
	D_ASSERT(op < MAX_PROFILE_OP);		\
	D_ASSERT(sp->sp_time[op] != 0);		\
	srv_profile_count(sp, op, (int)(daos_get_ntime() - sp->sp_time[op])); \
	sp->sp_time[op] = 0;			\
} while (0)

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

/**
 * Different type of ES pools, there are 4 pools for now
 *
 *  DSS_POOL_URGENT	The highest priority pool. ULTs in this pool will be
 *			scheduled firstly.
 *  DSS_POOL_PRIV	Private pool: I/O requests will be added to this pool.
 *  DSS_POOL_SHARE	Shared pool: Other requests and ULT created during
 *			processing rpc.
 *  DSS_POOL_REBUILD	rebuild pool: pools specially for rebuild tasks.
 */
enum {
	DSS_POOL_URGENT,
	DSS_POOL_PRIV,
	DSS_POOL_SHARE,
	DSS_POOL_REBUILD,
	DSS_POOL_CNT,
};

#define DSS_XS_NAME_LEN		64

/** Per-xstream configuration data */
struct dss_xstream {
	char		dx_name[DSS_XS_NAME_LEN];
	ABT_future	dx_shutdown;
	hwloc_cpuset_t	dx_cpuset;
	ABT_xstream	dx_xstream;
	ABT_pool	dx_pools[DSS_POOL_CNT];
	ABT_sched	dx_sched;
	ABT_thread	dx_progress;
	/* xstream id, [0, DSS_XS_NR_TOTAL - 1] */
	int		dx_xs_id;
	/* VOS target id, [0, dss_tgt_nr - 1]. Invalid (-1) for system XS.
	 * For offload XS it is same value as its main XS.
	 */
	int		dx_tgt_id;
	/* CART context id, invalid (-1) for the offload XS w/o CART context */
	int		dx_ctx_id;
	bool		dx_main_xs;	/* true for main XS */
	bool		dx_comm;	/* true with cart context */
};

struct dss_module_info {
	crt_context_t		dmi_ctx;
	struct bio_xs_context	*dmi_nvme_ctxt;
	struct dss_xstream	*dmi_xstream;
	/* the xstream id */
	int			dmi_xs_id;
	/* the VOS target id */
	int			dmi_tgt_id;
	/* the cart context id */
	int			dmi_ctx_id;
	d_list_t		dmi_dtx_batched_list;
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
 * Any dss_module that accepts dRPC communications over the Unix Domain Socket
 * must provide one or more dRPC handler functions. The handler is used by the
 * I/O server to multiplex incoming dRPC messages for processing.
 *
 * The dRPC messaging module ID is different from the dss_module's ID. A
 * dss_module may handle more than one dRPC module ID.
 */
struct dss_drpc_handler {
	int		module_id;	/** dRPC messaging module ID */
	drpc_handler_t	handler;	/** dRPC handler for the module */
};

/* The profile structure to record single operation */
struct srv_profile_op {
	int		pro_id;		/* id in obj_profile_op */
	uint64_t	pro_time;	/* time cost for this id */
};

/* The chunk for a group of srv_profile */
struct srv_profile_chunk {
	d_list_t	      spc_chunk_list;
	struct srv_profile_op *spc_profiles;
	int		      spc_idx;
	int		      spc_chunk_size;
};

#define MAX_PROFILE_OP	64
/* Holding the total trunk list for a specific profiling module */
struct srv_profile {
	struct srv_profile_chunk *sp_current_chunk;
	uint64_t	sp_time[MAX_PROFILE_OP];
	d_list_t	sp_list;	/* active list for profile chunk */
	d_list_t	sp_idle_list;	/* idle list for profile chunk */
	/* Count in idle list & list */
	int		sp_chunk_total_cnt;
	/* count in list */
	int		sp_chunk_cnt;
	char		*sp_dir_path;	/* Where to dump the profiling */
	char		**sp_names;	/* profile name */
	ABT_thread	sp_dump_thread;	/* dump thread for profile */
	int		sp_stop:1;
};

struct dss_module_ops {
	/* The callback for each module will choose ABT pool to handle RPC */
	ABT_pool (*dms_abt_pool_choose_cb)(crt_rpc_t *rpc, ABT_pool *pools);

	/* Each module to start/stop the profiling */
	int	(*dms_profile_start)(char *path);
	int	(*dms_profile_stop)(void);
};

int srv_profile_stop(struct srv_profile *sp);
int srv_profile_count(struct srv_profile *sp, int id, int time);
int srv_profile_start(struct srv_profile **sp_p, char *path, char **names);
void srv_profile_destroy(struct srv_profile *sp);

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
	/* Module id see enum daos_module_id */
	int			  sm_mod_id;
	/* Module version */
	int			  sm_ver;
	/* Module facility bitmask, can be feature bits like DSS_FAC_LOAD_CLI */
	uint64_t		  sm_facs;
	/* key of local thread storage */
	struct dss_module_key	 *sm_key;
	/* Initialization function, invoked just after successful load */
	int			(*sm_init)(void);
	/* Finalization function, invoked just before module unload */
	int			(*sm_fini)(void);
	/* Setup function, invoked after starting progressing */
	int			(*sm_setup)(void);
	/* Cleanup function, invoked before stopping progressing */
	int			(*sm_cleanup)(void);
	/* Whole list of RPC definition for request sent by nodes */
	struct crt_proto_format	 *sm_proto_fmt;
	/* The count of RPCs which are dedicated for client nodes only */
	uint32_t		  sm_cli_count;
	/* RPC handler of these RPC, last entry of the array must be empty */
	struct daos_rpc_handler	 *sm_handlers;
	/* dRPC handlers, for unix socket comm, last entry must be empty */
	struct dss_drpc_handler	 *sm_drpc_handlers;

	/* Different module operation */
	struct dss_module_ops	*sm_mod_ops;
};

/**
 * DSS_TGT_SELF can be passed to dss_ult_xs to indicate scheduling ULT on
 * caller's self XS.
 */
#define DSS_TGT_SELF	(-1)

/** ULT types to determine on which XS to schedule the ULT */
enum dss_ult_type {
	/** for dtx_resync */
	DSS_ULT_DTX_RESYNC = 100,
	/** forward/dispatch IO request for TX coordinator */
	DSS_ULT_IOFW,
	/** EC/checksum/compress computing offload */
	DSS_ULT_EC,
	DSS_ULT_CHECKSUM,
	DSS_ULT_COMPRESS,
	/** pool service ULT */
	DSS_ULT_POOL_SRV,
	/** RDB ULT */
	DSS_ULT_RDB,
	/** rebuild ULT such as scanner/puller, status checker etc. */
	DSS_ULT_REBUILD,
	/** aggregation ULT */
	DSS_ULT_AGGREGATE,
	/** drpc listener ULT */
	DSS_ULT_DRPC,
	/** miscellaneous ULT */
	DSS_ULT_MISC,
};

int dss_parameters_set(unsigned int key_id, uint64_t value);

typedef ABT_pool (*dss_abt_pool_choose_cb_t)(crt_rpc_t *rpc, ABT_pool *pools);

void dss_abt_pool_choose_cb_register(unsigned int mod_id,
				     dss_abt_pool_choose_cb_t cb);
int dss_ult_create(void (*func)(void *), void *arg, int ult_type, int tgt_id,
		   size_t stack_size, ABT_thread *ult);
int dss_ult_create_all(void (*func)(void *), void *arg, bool main);
int dss_ult_create_execute(int (*func)(void *), void *arg,
			   void (*user_cb)(void *), void *cb_args,
			   int ult_type, int tgt_id, size_t stack_size);

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
	int				(*co_reduce_arg_alloc)
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

/**
 * Generic dss_collective with custom aggregator
 *
 * TODO: rename these functions, thread & task are too generic name and
 * DAOS has already used task for something else.
 *
 * These functions should be dss_ult_collective/dss_tlt_collective.
 */
int
dss_task_collective_reduce(struct dss_coll_ops *ops,
			   struct dss_coll_args *coll_args, int flag);
int
dss_thread_collective_reduce(struct dss_coll_ops *ops,
			     struct dss_coll_args *coll_args, int flag);

int dss_task_collective(int (*func)(void *), void *arg, int flag);
int dss_thread_collective(int (*func)(void *), void *arg, int flag);
int dss_task_run(tse_task_t *task, unsigned int type, tse_task_cb_t cb,
		 void *arg, ABT_eventual eventual);
int dss_eventual_create(ABT_eventual *eventual_ptr);
int dss_eventual_wait(ABT_eventual eventual);
void dss_eventual_free(ABT_eventual *eventual);
struct dss_module *dss_module_get(int mod_id);
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

/* DAOS object API on the server side */
int ds_obj_open(daos_handle_t coh, daos_obj_id_t oid,
		unsigned int mode, daos_handle_t *oh);
int ds_obj_close(daos_handle_t obj_hl);

int ds_obj_list_akey(daos_handle_t oh, daos_epoch_t epoch,
		 daos_key_t *dkey, uint32_t *nr,
		 daos_key_desc_t *kds, d_sg_list_t *sgl,
		 daos_anchor_t *anchor);

int ds_obj_fetch(daos_handle_t oh, daos_epoch_t epoch,
		 daos_key_t *dkey, unsigned int nr,
		 daos_iod_t *iods, d_sg_list_t *sgls,
		 daos_iom_t *maps);
int ds_obj_list_obj(daos_handle_t oh, daos_epoch_t *epoch, daos_key_t *dkey,
		daos_key_t *akey, daos_size_t *size, uint32_t *nr,
		daos_key_desc_t *kds, daos_epoch_range_t *eprs,
		d_sg_list_t *sgl, daos_anchor_t *anchor,
		daos_anchor_t *dkey_anchor, daos_anchor_t *akey_anchor);

d_rank_t dss_self_rank(void);

unsigned int dss_ctx_nr_get(void);

/** Server init state (see server_init) */
enum dss_init_state {
	DSS_INIT_STATE_INIT,		/**< initial state */
	DSS_INIT_STATE_RANK_SET,	/**< rank has been set */
	DSS_INIT_STATE_SET_UP		/**< ready to set up modules */
};

void dss_init_state_set(enum dss_init_state state);

bool dss_pmixless(void);

#endif /* __DSS_API_H__ */
