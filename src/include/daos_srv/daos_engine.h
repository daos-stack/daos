/**
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
#include <daos/cont_props.h>
#include <daos_srv/iv.h>
#include <daos_srv/vos_types.h>
#include <daos_srv/pool.h>
#include <daos_srv/ras.h>
#include <daos_event.h>
#include <daos_task.h>
#include <pthread.h>
#include <hwloc.h>
#include <abt.h>
#include <cart/iv.h>
#include <daos/checksum.h>

/* Standard max length of addresses e.g. URI, PCI */
#define ADDR_STR_MAX_LEN 128

/** number of target (XS set) per engine */
extern unsigned int	 dss_tgt_nr;

/** Storage path (hack) */
extern const char	*dss_storage_path;

/** NVMe config file */
extern const char	*dss_nvme_conf;

/** Socket Directory */
extern const char	*dss_socket_dir;

/** NVMe mem_size for SPDK memory allocation (in MB) */
extern unsigned int	dss_nvme_mem_size;

/** NVMe hugepage_size for DPDK/SPDK memory allocation (in MB) */
extern unsigned int	 dss_nvme_hugepage_size;

/** I/O Engine instance index */
extern unsigned int	 dss_instance_idx;

/** Bypass for the nvme health check */
extern bool		 dss_nvme_bypass_health_check;

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
	DAOS_SYS_TAG    = 1 << 0, /** only run on system xstream */
	DAOS_TGT_TAG    = 1 << 1, /** only run on target xstream */
	DAOS_RDB_TAG    = 1 << 2, /** only run on rdb xstream */
	DAOS_OFF_TAG    = 1 << 3, /** only run on offload/helper xstream */
	DAOS_SERVER_TAG = 0xff,   /** run on all xstream */
};

/* The module key descriptor for each xstream */
struct dss_module_key {
	/* Indicate where the keys should be instantiated */
	enum dss_module_tag dmk_tags;

	/* The position inside the dss_module_keys */
	int dmk_index;
	/* init keys for context */
	void *(*dmk_init)(int tags, int xs_id, int tgt_id);

	/* fini keys for context */
	void (*dmk_fini)(int tags, void *data);
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

/** pthread names are limited to 16 chars */
#define DSS_XS_NAME_LEN		(32)

/* Opaque xstream configuration data */
struct dss_xstream;

int  dss_xstream_set_affinity(struct dss_xstream *dxs);
bool dss_xstream_exiting(struct dss_xstream *dxs);
bool dss_xstream_is_busy(void);
daos_epoch_t dss_get_start_epoch(void);
void dss_set_start_epoch(void);
bool dss_has_enough_helper(void);

struct dss_module_info {
	crt_context_t		dmi_ctx;
	struct bio_xs_context  *dmi_nvme_ctxt;
	struct dss_xstream     *dmi_xstream;
	/* the xstream id */
	int			dmi_xs_id;
	/* the VOS target id */
	int			dmi_tgt_id;
	/* the cart context id */
	int			dmi_ctx_id;
	uint32_t		dmi_dtx_batched_started:1,
				dmi_srv_shutting_down:1;
	d_list_t		dmi_dtx_batched_cont_open_list;
	d_list_t		dmi_dtx_batched_cont_close_list;
	d_list_t		dmi_dtx_batched_pool_list;
	/* the profile information */
	struct daos_profile	*dmi_dp;
	struct sched_request	*dmi_dtx_cmt_req;
	struct sched_request	*dmi_dtx_agg_req;
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

static inline struct dss_xstream *
dss_current_xstream(void)
{
	return dss_get_module_info()->dmi_xstream;
}

/**
 * Is the engine shutting down? If this function returns false, then before the
 * current xstream enters the scheduler (e.g., by yielding), the engine won't
 * finish entering shutdown mode (i.e., any dss_srv_set_shutting_down call
 * won't return).
 */
bool
dss_srv_shutting_down(void);

/**
 * Module facility feature bits
 * DSS_FAC_LOAD_CLI - the module requires loading client stack.
 */
#define DSS_FAC_LOAD_CLI (0x1ULL)

/**
 * Any dss_module that accepts dRPC communications over the Unix Domain Socket
 * must provide one or more dRPC handler functions. The handler is used by the
 * I/O Engine to multiplex incoming dRPC messages for processing.
 *
 * The dRPC messaging module ID is different from the dss_module's ID. A
 * dss_module may handle more than one dRPC module ID.
 */
struct dss_drpc_handler {
	int		module_id;	/** dRPC messaging module ID */
	drpc_handler_t	handler;	/** dRPC handler for the module */
};

enum {
	SCHED_REQ_UPDATE	= 0,
	SCHED_REQ_FETCH,
	SCHED_REQ_GC,
	SCHED_REQ_SCRUB,
	SCHED_REQ_MIGRATE,
	SCHED_REQ_MAX,
	/* Anonymous request which doesn't associate to a DAOS pool */
	SCHED_REQ_ANONYM = SCHED_REQ_MAX,
	SCHED_REQ_TYPE_MAX,
};

enum {
	SCHED_REQ_FL_NO_DELAY	= (1 << 0),
	SCHED_REQ_FL_PERIODIC	= (1 << 1),
};

struct sched_req_attr {
	uuid_t		sra_pool_id;
	uint32_t	sra_type;
	uint32_t	sra_flags;
};

static inline void
sched_req_attr_init(struct sched_req_attr *attr, unsigned int type,
		    uuid_t *pool_id)
{
	attr->sra_type = type;
	attr->sra_flags = 0;
	uuid_copy(attr->sra_pool_id, *pool_id);
}

struct sched_request;	/* Opaque schedule request */

/**
 * Get a sched request.
 *
 * \param[in] attr	Sched request attributes.
 * \param[in] ult	ULT attached to the sched request, self ULT will be
 *			used when ult == ABT_THREAD_NULL. If not
 *			ABT_THREAD_NULL, ult will be freed by sched_req_put.
 *			Unnamed ULTs (e.g., from ABT_thread_self) are
 *			prohibited.
 *
 * \retval		Sched request.
 */
struct sched_request *
sched_req_get(struct sched_req_attr *attr, ABT_thread ult);

/**
 * Put a sched request. If the associated ULT was passed in by the caller, it
 * will be freed.
 *
 * \param[in] req	Sched request.
 *
 * \retval		N/A
 */
void sched_req_put(struct sched_request *req);

/**
 * Suspend (or yield) a sched request attached ULT.
 *
 * \param[in] req	Sched request.
 *
 * \retval		N/A
 */
void sched_req_yield(struct sched_request *req);

/**
 * Put a sched request attached ULT to sleep for few msecs.
 *
 * \param[in] req	Sched request.
 * \param[in] msec	Milli seconds.
 *
 * \retval		N/A
 */
void sched_req_sleep(struct sched_request *req, uint32_t msec);

/**
 * Wakeup a sched request attached ULT.
 *
 * \param[in] req	Sched request.
 *
 * \retval		N/A
 */
void sched_req_wakeup(struct sched_request *req);

/**
 * Wakeup a sched request attached ULT terminated. The associated ULT of \a req
 * must not an unnamed ULT.
 *
 * \param[in] req	Sched request.
 * \param[in] abort	Abort the ULT or not.
 *
 * \retval		N/A
 */
void sched_req_wait(struct sched_request *req, bool abort);

/**
 * Check if a sched request is set as aborted.
 *
 * \param[in] req	Sched request.
 *
 * \retval		True for aborted, False otherwise.
 */
bool sched_req_is_aborted(struct sched_request *req);

#define SCHED_SPACE_PRESS_NONE	0

/**
 * Check space pressure of the pool of current sched request.
 *
 * \param[in] req	Sched request.
 *
 * \retval		None, light or severe.
 */
int sched_req_space_check(struct sched_request *req);

/**
 * Wrapper of ABT_cond_wait(), inform scheduler that it's going
 * to be blocked for a relative long time.
 */
void sched_cond_wait(ABT_cond cond, ABT_mutex mutex);

/**
 * Get current monotonic time in milli-seconds.
 */
uint64_t sched_cur_msec(void);

/**
 * Get current schedule sequence, by comparing the results of two
 * sched_cur_seq() calls, we can tell if an ULT was yielding between
 * these two calls.
 */
uint64_t sched_cur_seq(void);

/**
 * Get current ULT/Task execution time. The execution time is the elapsed
 * time since current ULT/Task was scheduled last time.
 *
 * \param[out]	msecs		executed time in milli-second
 * \param[in]	ult_name	ULT name (optional)
 *
 * \retval			-DER_NOSYS or 0 on success.
 */
int sched_exec_time(uint64_t *msecs, const char *ult_name);

/**
 * Create an ULT on the caller xstream and return the associated sched_request.
 * Caller is responsible for freeing the sched_request by sched_req_put().
 *
 * \param[in]	attr		sched request attributes
 * \param[in]	func		ULT function
 * \param[in]	arg		ULT argument
 * \param[in]	stack_size	ULT stack size
 *
 * \retval			associated shed_request on success, NULL on error.
 */
struct sched_request *
sched_create_ult(struct sched_req_attr *attr, void (*func)(void *), void *arg,
		 size_t stack_size);

static inline bool
dss_ult_exiting(struct sched_request *req)
{
	struct dss_xstream	*dx = dss_current_xstream();

	return dss_xstream_exiting(dx) || sched_req_is_aborted(req);
}

/*
 * Yield function regularly called by long-run ULTs.
 *
 * \param[in] req	Sched request.
 *
 * \retval		True:  Abort ULT;
 *			False: Yield then continue;
 */
static inline bool
dss_ult_yield(void *arg)
{
	struct sched_request	*req = (struct sched_request *)arg;

	if (dss_ult_exiting(req))
		return true;

	sched_req_yield(req);
	return false;
}

struct dss_module_ops {
	/* Get schedule request attributes from RPC */
	int (*dms_get_req_attr)(crt_rpc_t *rpc, struct sched_req_attr *attr);
};

int srv_profile_stop();
int srv_profile_start(char *path, int avg);

struct dss_module_metrics {
	/* Indicate where the keys should be instantiated */
	enum dss_module_tag dmm_tags;

	/**
	 * allocate metrics with path to ephemeral shmem for to the
	 * newly-created pool
	 */
	void	*(*dmm_init)(const char *path, int tgt_id);
	void	 (*dmm_fini)(void *data);

	/**
	 * Get the number of metrics allocated by this module in total (including all targets).
	 */
	int	 (*dmm_nr_metrics)(void);
};

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
	const char			*sm_name;
	/* Module id see enum daos_module_id */
	int				sm_mod_id;
	/* Module version */
	int				sm_ver;
	/* Module facility bitmask, can be feature bits like DSS_FAC_LOAD_CLI */
	uint64_t			sm_facs;
	/* key of local thread storage */
	struct dss_module_key		*sm_key;
	/* Initialization function, invoked just after successful load */
	int				(*sm_init)(void);
	/* Finalization function, invoked just before module unload */
	int				(*sm_fini)(void);
	/* Setup function, invoked after starting progressing */
	int				(*sm_setup)(void);
	/* Cleanup function, invoked before stopping progressing */
	int				(*sm_cleanup)(void);
	/* Number of RPC protocols this module supports - max 2 */
	int				sm_proto_count;
	/* Array of whole list of RPC definition for request sent by nodes */
	struct crt_proto_format		*sm_proto_fmt[2];
	/* Array of the count of RPCs which are dedicated for client nodes only */
	uint32_t			sm_cli_count[2];
	/* Array of RPC handler of these RPC, last entry of the array must be empty */
	struct daos_rpc_handler		*sm_handlers[2];
	/* dRPC handlers, for unix socket comm, last entry must be empty */
	struct dss_drpc_handler		*sm_drpc_handlers;

	/* Different module operation */
	struct dss_module_ops		*sm_mod_ops;

	/* Per-pool metrics (optional) */
	struct dss_module_metrics	*sm_metrics;
};

/**
 * Stack size used for ULTs with deep stack
 */
#define DSS_DEEP_STACK_SZ	65536

enum dss_xs_type {
	/** current xstream */
	DSS_XS_SELF	= -1,
	/** operations need to access VOS */
	DSS_XS_VOS	= 0,
	/** forward/dispatch IO request for TX coordinator */
	DSS_XS_IOFW	= 1,
	/** EC/checksum/compress computing offload */
	DSS_XS_OFFLOAD	= 2,
	/** pool service, RDB, drpc handler */
	DSS_XS_SYS	= 3,
	/** SWIM operations */
	DSS_XS_SWIM	= 4,
	/** drpc listener */
	DSS_XS_DRPC	= 5,
};

int dss_parameters_set(unsigned int key_id, uint64_t value);

enum dss_ult_flags {
	/* Periodically created ULTs */
	DSS_ULT_FL_PERIODIC	= (1 << 0),
	/* Use DSS_DEEP_STACK_SZ as the stack size */
	DSS_ULT_DEEP_STACK	= (1 << 1),
};

int dss_ult_create(void (*func)(void *), void *arg, int xs_type, int tgt_id,
		   size_t stack_size, ABT_thread *ult);
int dss_ult_execute(int (*func)(void *), void *arg, void (*user_cb)(void *),
		    void *cb_args, int xs_type, int tgt_id, size_t stack_size);
int dss_ult_create_all(void (*func)(void *), void *arg, bool main);
int __attribute__((weak)) dss_offload_exec(int (*func)(void *), void *arg);
int __attribute__((weak)) dss_main_exec(void (*func)(void *), void *arg);

/*
 * If server wants to create ULTs periodically, it should call this special
 * ult create function to avoid bumping the 'xstream busy timestamp'.
 */
int dss_ult_periodic(void (*func)(void *), void *arg, int xs_type, int tgt_id,
		     size_t stack_size, ABT_thread *ult);

int dss_sleep(uint64_t ms);

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
	int				*ca_exclude_tgts;
	unsigned int			ca_exclude_tgts_cnt;
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
			   struct dss_coll_args *coll_args, unsigned int flags);
int
dss_thread_collective_reduce(struct dss_coll_ops *ops,
			     struct dss_coll_args *coll_args,
			     unsigned int flags);
int dss_task_collective(int (*func)(void *), void *arg, unsigned int flags);
int dss_thread_collective(int (*func)(void *), void *arg, unsigned int flags);

/**
 * Loaded module management metholds
 */
struct dss_module *dss_module_get(int mod_id);
void dss_module_fini_metrics(enum dss_module_tag tag, void **metrics);
int dss_module_init_metrics(enum dss_module_tag tag, void **metrics,
			    const char *path, int tgt_id);
int dss_module_nr_pool_metrics(void);

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

/** RPC counter types */
enum dss_rpc_cntr_id {
	DSS_RC_OBJ	= 0,
	DSS_RC_CONT,
	DSS_RC_POOL,
	DSS_RC_MAX,
};

/** RPC counter */
struct dss_rpc_cntr {
	/**
	 * starting monotonic time, it can be used to calculate average
	 * workload.
	 */
	uint64_t		rc_stime;
	/* the time when processing last active RPC */
	uint64_t		rc_active_time;
	/** number of active RPCs */
	uint64_t		rc_active;
	/** total number of processed RPCs since \a rc_stime */
	uint64_t		rc_total;
	/** total number of failed RPCs since \a rc_stime */
	uint64_t		rc_errors;
};

void dss_rpc_cntr_enter(enum dss_rpc_cntr_id id);
void dss_rpc_cntr_exit(enum dss_rpc_cntr_id id, bool failed);
struct dss_rpc_cntr *dss_rpc_cntr_get(enum dss_rpc_cntr_id id);

int dss_rpc_send(crt_rpc_t *rpc);
int dss_rpc_reply(crt_rpc_t *rpc, unsigned int fail_loc);

enum {
	/** Min Value */
	DSS_OFFLOAD_MIN = -1,
	/** Does computation on same ULT */
	DSS_OFFLOAD_ULT = 1,
	/** Offload to an accelerator */
	DSS_OFFLOAD_ACC = 2,
	/** Max value */
	DSS_OFFLOAD_MAX = 7
};

struct dss_acc_task {
	/**
	 * Type of offload for this operation
	 * \param at_offload_type	[IN] type of acceleration
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
 * Generic offload call abstraction for acceleration with both
 * ULT and FPGA
 */
int dss_acc_offload(struct dss_acc_task *at_args);

/* DAOS client APIs called on the server side */
int dsc_obj_open(daos_handle_t coh, daos_obj_id_t oid,
		 unsigned int mode, daos_handle_t *oh);
int dsc_obj_close(daos_handle_t obj_hl);

int dsc_obj_list_akey(daos_handle_t oh, daos_epoch_t epoch,
		daos_key_t *dkey, uint32_t *nr,
		daos_key_desc_t *kds, d_sg_list_t *sgl,
		daos_anchor_t *anchor);

int dsc_obj_fetch(daos_handle_t oh, daos_epoch_t epoch, daos_key_t *dkey,
		  unsigned int nr, daos_iod_t *iods, d_sg_list_t *sgls,
		  daos_iom_t *maps, unsigned int extra_flag,
		  unsigned int *extra_arg, d_iov_t *csum_iov);

int dsc_obj_update(daos_handle_t oh, uint64_t flags, daos_key_t *dkey,
		   unsigned int nr, daos_iod_t *iods, d_sg_list_t *sgls);

int dsc_obj_list_obj(daos_handle_t oh, daos_epoch_range_t *epr,
		     daos_key_t *dkey, daos_key_t *akey, daos_size_t *size,
		     uint32_t *nr, daos_key_desc_t *kds, d_sg_list_t *sgl,
		     daos_anchor_t *anchor, daos_anchor_t *dkey_anchor,
		     daos_anchor_t *akey_anchor, d_iov_t *csum);
int dsc_obj_id2oc_attr(daos_obj_id_t oid, struct cont_props *prop,
		       struct daos_oclass_attr *oca);

int dsc_pool_tgt_exclude(const uuid_t uuid, const char *grp,
			 const d_rank_list_t *svc, struct d_tgt_list *tgts);
int dsc_pool_tgt_reint(const uuid_t uuid, const char *grp,
		       const d_rank_list_t *svc, struct d_tgt_list *tgts);

int dsc_task_run(tse_task_t *task, tse_task_cb_t retry_cb, void *arg,
		 int arg_size, bool sync);
tse_sched_t *dsc_scheduler(void);

d_rank_t dss_self_rank(void);

unsigned int dss_ctx_nr_get(void);

/* Cache for container root */
struct tree_cache_root {
	struct btr_root	btr_root;
	daos_handle_t	root_hdl;
	unsigned int	count;
};

int
obj_tree_insert(daos_handle_t toh, uuid_t co_uuid, uint64_t tgt_id,
		daos_unit_oid_t oid, d_iov_t *val_iov);
int
obj_tree_destroy(daos_handle_t btr_hdl);

/* Per xstream migrate status */
struct ds_migrate_status {
	uint64_t dm_rec_count;	/* migrated record size */
	uint64_t dm_obj_count;	/* migrated object count */
	uint64_t dm_total_size;	/* migrated total size */
	int	 dm_status;	/* migrate status */
	uint32_t dm_migrating:1; /* if it is migrating */
};

int
ds_migrate_query_status(uuid_t pool_uuid, uint32_t ver, uint32_t generation,
			struct ds_migrate_status *dms);
int
ds_object_migrate_send(struct ds_pool *pool, uuid_t pool_hdl_uuid, uuid_t cont_uuid,
		       uuid_t cont_hdl_uuid, int tgt_id, uint32_t version, unsigned int generation,
		       uint64_t max_eph, daos_unit_oid_t *oids, daos_epoch_t *ephs,
		       daos_epoch_t *punched_ephs, unsigned int *shards, int cnt,
		       uint32_t new_gl_ver, unsigned int migrate_opc);
int
ds_migrate_object(struct ds_pool *pool, uuid_t po_hdl, uuid_t co_hdl, uuid_t co_uuid,
		  uint32_t version, uint32_t generation, uint64_t max_eph, uint32_t opc,
		  daos_unit_oid_t *oids, daos_epoch_t *epochs, daos_epoch_t *punched_epochs,
		  unsigned int *shards, uint32_t count, unsigned int tgt_idx, uint32_t new_gl_ver);
void
ds_migrate_stop(struct ds_pool *pool, uint32_t ver, unsigned int generation);

int
obj_layout_diff(struct pl_map *map, daos_unit_oid_t oid, uint32_t new_ver, uint32_t old_ver,
		struct daos_obj_md *md, uint32_t *tgts, uint32_t *shards, int array_size);

/** Server init state (see server_init) */
enum dss_init_state {
	DSS_INIT_STATE_INIT,		/**< initial state */
	DSS_INIT_STATE_SET_UP		/**< ready to set up modules */
};

enum dss_media_error_type {
	MET_WRITE = 0,	/* write error */
	MET_READ,	/* read error */
	MET_UNMAP,	/* unmap error */
	MET_CSUM	/* checksum error */
};

void dss_init_state_set(enum dss_init_state state);

/** Call module setup from drpc setup call handler. */
int dss_module_setup_all(void);

/** Notify control-plane of a bio error. */
int ds_notify_bio_error(int media_err_type, int tgt_id);

int ds_get_pool_svc_ranks(uuid_t pool_uuid, d_rank_list_t **svc_ranks);
int ds_pool_find_bylabel(d_const_string_t label, uuid_t pool_uuid,
			 d_rank_list_t **svc_ranks);

/** Flags for dss_drpc_call */
enum dss_drpc_call_flag {
	/** Do not wait for a response. Implies DSS_DRPC_NO_SCHED. */
	DSS_DRPC_NO_RESP	= 1,
	/**
	 * Do not Argobots-schedule. If the dRPC requires a response, this will
	 * block the thread until a response is received. That is usually
	 * faster than waiting for the response by Argobots-scheduling if the
	 * dRPC can be quickly handled by the local daos_server alone.
	 */
	DSS_DRPC_NO_SCHED	= 2
};

int dss_drpc_call(int32_t module, int32_t method, void *req, size_t req_size,
		  unsigned int flags, Drpc__Response **resp);

#endif /* __DSS_API_H__ */
