/**
 * (C) Copyright 2016-2024 Intel Corporation.
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
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
#include <daos/tls.h>
#include <daos_srv/iv.h>
#include <daos_srv/vos_types.h>
#include <daos_srv/pool.h>
#include <daos_srv/ras.h>
#include <daos_event.h>
#include <daos_task.h>
#include <daos_mgmt.h>
#include <pthread.h>
#include <hwloc.h>
#include <abt.h>
#include <cart/iv.h>
#include <daos/checksum.h>

/* Standard max length of addresses e.g. URI, PCI */
#define ADDR_STR_MAX_LEN 128

#define AF_RC            "%s(%d): '%s'"
#define AP_RC(rc)        dss_abterr2str(rc), rc, dss_abterr2desc(rc)

/** DAOS system name (corresponds to crt group ID) */
extern char             *daos_sysname;

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
	struct daos_thread_local_storage *dtc;

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
	SCHED_REQ_FL_NO_REJECT	= (1 << 2),
	SCHED_REQ_FL_RESENT	= (1 << 3),
};

struct sched_req_attr {
	uuid_t		sra_pool_id;
	uint32_t	sra_type;
	uint32_t	sra_flags;
	uint32_t	sra_timeout;
	/* Hint for RPC rejection */
	uint64_t	sra_enqueue_id;
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
 * Wrapper of ABT_cond_wait(), inform scheduler that it's going
 * to be blocked for a relative long time. Unlike sched_cond_wait,
 * after waking up, this function will prevent relaxing for a while.
 */
void sched_cond_wait_for_business(ABT_cond cond, ABT_mutex mutex);

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

/**
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
	/* Set schedule request attributes to RPC */
	int (*dms_set_req)(crt_rpc_t *rpc, struct sched_req_attr *attr);
};

int srv_profile_stop();
int srv_profile_start(char *path, int avg);

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
	struct daos_module_metrics      *sm_metrics;
};

/**
 * Stack size used for ULTs with deep stack
 */
#if defined(__clang__)
#if defined(__has_feature) && __has_feature(address_sanitizer)
#define DSS_DEEP_STACK_SZ 98304
#endif
#elif defined(__GNUC__)
#ifdef __SANITIZE_ADDRESS__
#define DSS_DEEP_STACK_SZ 98304
#endif
#endif
#ifndef DSS_DEEP_STACK_SZ
#define DSS_DEEP_STACK_SZ 65536
#endif

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
	/* Use current ULT (instead of creating new one) for the task. */
	DSS_USE_CURRENT_ULT	= (1 << 2),
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
	 * \param[in] f_args	Arguments for function
	 */
	int (*co_func)(void *f_args);

	/**
	 * Callback for reducing after dss_collective (optional)
	 *
	 * \param[in,out] a_args	Aggregator arguments for reducing results
	 * \param[in] s_args		Reduce arguments for this current stream
	 */
	void (*co_reduce)(void *a_args, void *s_args);

	/**
	 * Alloc function for allocating reduce arguments (optional)
	 *
	 * \param[in,out] args		coll_args for this streams
	 * \param[in] aggregator_args	aggregator args for  initializatuin
	 */
	int (*co_reduce_arg_alloc)(struct dss_stream_arg_type *args, void *a_args);
	/**
	 * Free the allocated reduce arguments
	 * (Mandatory if co_rarg_alloc was provided)
	 *
	 * \param[in] args		coll_args for this stream
	 */
	void (*co_reduce_arg_free)(struct dss_stream_arg_type *args);
};

struct dss_coll_args {
	/** Arguments for dss_collective func (Mandatory) */
	void				*ca_func_args;
	void				*ca_aggregator;
	/* Specify on which targets to execute the task. */
	uint8_t				*ca_tgt_bitmap;
	/*
	 * The size (in byte) of ca_tgt_bitmap. It may be smaller than dss_tgt_nr if only some
	 * VOS targets are involved. It also may be larger than dss_tgt_nr if dss_tgt_nr is not
	 * 2 ^ n aligned.
	 */
	uint32_t			 ca_tgt_bitmap_sz;
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
int dss_build_coll_bitmap(int *exclude_tgts, uint32_t exclude_cnt, uint8_t **p_bitmap,
			  uint32_t *bitmap_sz);

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
	case ABT_SUCCESS:
		return 0;
	case ABT_ERR_MEM:
		return -DER_NOMEM;
	default:
		return -DER_INVAL;
	}
}

/* Convert DAOS errno to Argobots ones. */
static inline int
dss_der2abterr(int der)
{
	switch (der) {
	case -DER_SUCCESS:
		return ABT_SUCCESS;
	case -DER_NOMEM:
		return ABT_ERR_MEM;
	default:
		return ABT_ERR_OTHER;
	}
}

/** Helper converting ABT error code into human readable string */
const char *
dss_abterr2str(int rc);

/** Helper converting ABT error code into meaningful message */
const char *
dss_abterr2desc(int rc);

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
	struct btr_root	tcr_btr_root;
	daos_handle_t	tcr_root_hdl;
	unsigned int	tcr_count;
};

int
obj_tree_insert(daos_handle_t toh, uuid_t co_uuid, uint64_t tgt_id,
		daos_unit_oid_t oid, d_iov_t *val_iov);
int
obj_tree_destroy(daos_handle_t btr_hdl);

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

int ds_get_pool_svc_ranks(uuid_t pool_uuid, d_rank_list_t **svc_ranks);
int ds_pool_find_bylabel(d_const_string_t label, uuid_t pool_uuid,
			 d_rank_list_t **svc_ranks);
int
ds_get_pool_list(uint64_t *npools, daos_mgmt_pool_info_t *pools);

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

/** Status of a chore */
enum dss_chore_status {
	DSS_CHORE_NEW,		/**< ready to be scheduled for the first time (private) */
	DSS_CHORE_YIELD,	/**< ready to be scheduled again */
	DSS_CHORE_DONE		/**< no more scheduling required */
};

struct dss_chore;

/**
 * Must return either DSS_CHORE_YIELD (if yielding to other chores) or
 * DSS_CHORE_DONE (if terminating). If \a is_reentrance is true, this is not
 * the first time \a chore is scheduled. A typical implementation shall
 * initialize its internal state variables if \a is_reentrance is false. See
 * dtx_leader_exec_ops_chore for an example.
 */
typedef enum dss_chore_status (*dss_chore_func_t)(struct dss_chore *chore, bool is_reentrance);

/**
 * A simple task (e.g., an I/O forwarding task) that yields by returning
 * DSS_CHORE_YIELD instead of calling ABT_thread_yield. This data structure
 * shall be embedded in the user's own task data structure, which typically
 * also includes arguments and internal state variables for \a cho_func.
 */
struct dss_chore {
	d_list_t              cho_link;
	enum dss_chore_status cho_status;
	dss_chore_func_t      cho_func;
	uint32_t              cho_priority : 1;
	int32_t               cho_credits;
	void                 *cho_hint;
};

int
dss_chore_register(struct dss_chore *chore);
void
dss_chore_deregister(struct dss_chore *chore);
void
dss_chore_diy(struct dss_chore *chore);
bool
engine_in_check(void);

#endif /* __DSS_API_H__ */
