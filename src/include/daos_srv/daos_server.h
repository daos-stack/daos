/**
 * (C) Copyright 2016-2020 Intel Corporation.
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
#include <daos_srv/pool.h>
#include <daos_event.h>
#include <daos_task.h>
#include <pthread.h>
#include <hwloc.h>
#include <abt.h>
#include <cart/iv.h>
#include <daos/checksum.h>

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

/** NVMe mem_size for SPDK memory allocation when using primary mode */
extern int		dss_nvme_mem_size;

/** IO server instance index */
extern unsigned int	dss_instance_idx;

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

/** pthread names are limited to 16 chars */
#define DSS_XS_NAME_LEN		16

struct srv_profile_chunk {
	d_list_t	spc_chunk_list;
	uint32_t	spc_chunk_offset;
	uint32_t	spc_chunk_size;
	uint64_t	*spc_chunks;
};

/* The profile structure to record single operation */
struct srv_profile_op {
	int		pro_op;			/* operation */
	char		*pro_op_name;		/* name of the op */
	int		pro_acc_cnt;		/* total number of val */
	int		pro_acc_val;		/* current total val */
	d_list_t	pro_chunk_list;		/* list of all chunks */
	d_list_t	pro_chunk_idle_list;	/* idle list of profile chunk */
	int		pro_chunk_total_cnt;	/* Count in idle list & list */
	int		pro_chunk_cnt;		/* count in list */
	struct srv_profile_chunk *pro_current_chunk; /* current chunk */
};

/* Holding the total trunk list for a specific profiling module */

#define D_TIME_START(start, op)			\
do {						\
	struct daos_profile *dp;		\
						\
	dp = dss_get_module_info()->dmi_dp;	\
	if ((dp) == NULL)			\
		break;				\
	start = daos_get_ntime();		\
} while (0)

#define D_TIME_END(start, op)			\
do {						\
	struct daos_profile *dp;		\
	int time_msec;				\
						\
	dp = dss_get_module_info()->dmi_dp;	\
	if ((dp) == NULL || start == 0)		\
		break;				\
	time_msec = (daos_get_ntime() - start)/1000; \
	daos_profile_count(dp, op, time_msec);	\
} while (0)

/* Opaque xstream configuration data */
struct dss_xstream;

bool dss_xstream_exiting(struct dss_xstream *dxs);
bool dss_xstream_is_busy(void);

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
	d_list_t		dmi_dtx_batched_list;
	/* the profile information */
	struct daos_profile	*dmi_dp;
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

enum {
	SCHED_REQ_UPDATE	= 0,
	SCHED_REQ_FETCH,
	SCHED_REQ_GC,
	SCHED_REQ_MIGRATE,
	SCHED_REQ_MAX,
};

enum {
	SCHED_REQ_FL_NO_DELAY	= (1 << 0),
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
	uuid_copy(attr->sra_pool_id, *pool_id);
}

struct sched_request;	/* Opaque schedule request */

/**
 * Get A sched request.
 *
 * \param[in] attr	Sched request attributes.
 * \param[in] ult	ULT attached to the sched request,
 *			self ULT will be used when ult == ABT_THREAD_NULL.
 *
 * \retval		Sched request.
 */
struct sched_request *
sched_req_get(struct sched_req_attr *attr, ABT_thread ult);

/**
 * Put A sched request.
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
 * Wakeup a sched request attached ULT terminated.
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

	/* Each module to start/stop the profiling */
	int	(*dms_profile_start)(char *path, int avg);
	int	(*dms_profile_stop)(void);
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
 * DSS_TGT_SELF indicates scheduling ULT on caller's self XS.
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
	/** drpc listener ULT */
	DSS_ULT_DRPC_LISTENER,
	/** drpc handler ULT */
	DSS_ULT_DRPC_HANDLER,
	/** GC & aggregation ULTs */
	DSS_ULT_GC,
	/** miscellaneous ULT */
	DSS_ULT_MISC,
	/** I/O ULT */
	DSS_ULT_IO,
};

int dss_parameters_set(unsigned int key_id, uint64_t value);

typedef ABT_pool (*dss_abt_pool_choose_cb_t)(crt_rpc_t *rpc, ABT_pool *pools);

void dss_abt_pool_choose_cb_register(unsigned int mod_id,
				     dss_abt_pool_choose_cb_t cb);
int dss_ult_create(void (*func)(void *), void *arg, int ult_type, int tgt_id,
		   size_t stack_size, ABT_thread *ult);
int dss_ult_execute(int (*func)(void *), void *arg, void (*user_cb)(void *),
		    void *cb_args, int ult_type, int tgt_id, size_t stack_size);
int dss_ult_create_all(void (*func)(void *), void *arg, int ult_type,
		       bool main);

struct dss_sleep_ult {
	ABT_thread	dsu_thread;
	uint64_t	dsu_expire_time;
	d_list_t	dsu_list;
};

struct dss_sleep_ult *dss_sleep_ult_create(void);
void dss_sleep_ult_destroy(struct dss_sleep_ult *dsu);
void dss_ult_sleep(struct dss_sleep_ult *dsu, uint64_t expire_secs);
void dss_ult_wakeup(struct dss_sleep_ult *dsu);
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
			   struct dss_coll_args *coll_args, int flag,
			   int ult_type);
int
dss_thread_collective_reduce(struct dss_coll_ops *ops,
			     struct dss_coll_args *coll_args, int flag,
			     int ult_type);

int dss_task_collective(int (*func)(void *), void *arg, int flag, int ult_type);
int dss_thread_collective(int (*func)(void *), void *arg, int flag,
			  int ult_type);
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
	 * starting wall-clock time, it can be used to calculate average
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

int dsc_obj_fetch(daos_handle_t oh, daos_epoch_t epoch,
		daos_key_t *dkey, unsigned int nr,
		daos_iod_t *iods, d_sg_list_t *sgls,
		daos_iom_t *maps);
int dsc_obj_list_obj(daos_handle_t oh, daos_epoch_range_t *epr,
		     daos_key_t *dkey, daos_key_t *akey, daos_size_t *size,
		     uint32_t *nr, daos_key_desc_t *kds, d_sg_list_t *sgl,
		     daos_anchor_t *anchor, daos_anchor_t *dkey_anchor,
		     daos_anchor_t *akey_anchor, d_iov_t *csum);
int dsc_pool_tgt_exclude(const uuid_t uuid, const char *grp,
			 const d_rank_list_t *svc, struct d_tgt_list *tgts);

int dsc_task_run(tse_task_t *task, tse_task_cb_t retry_cb, void *arg,
		 int arg_size, bool sync);
tse_sched_t *dsc_scheduler(void);

typedef int (*iter_copy_data_cb_t)(daos_handle_t ih,
				   vos_iter_entry_t *it_entry,
				   d_iov_t *iov_out);
struct dss_enum_arg {
	bool			fill_recxs;	/* type == S||R */
	bool			chk_key2big;
	bool			need_punch;	/* need to pack punch epoch */
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
};

struct dtx_handle;
typedef int (*enum_iterate_cb_t)(vos_iter_param_t *param, vos_iter_type_t type,
			    bool recursive, struct vos_iter_anchors *anchors,
			    vos_iter_cb_t pre_cb, vos_iter_cb_t post_cb,
			    void *arg, struct dtx_handle *dth);

int dss_enum_pack(vos_iter_param_t *param, vos_iter_type_t type, bool recursive,
		  struct vos_iter_anchors *anchors, struct dss_enum_arg *arg,
		  enum_iterate_cb_t iter_cb, struct dtx_handle *dth);
typedef int (*obj_enum_process_cb_t)(daos_key_desc_t *kds, void *ptr,
				     unsigned int size, void *arg);
int
obj_enum_iterate(daos_key_desc_t *kdss, d_sg_list_t *sgl, int nr,
		 unsigned int type, obj_enum_process_cb_t cb,
		 void *cb_arg);
/** Maximal number of iods (i.e., akeys) in dss_enum_unpack_io.ui_iods */
#define DSS_ENUM_UNPACK_MAX_IODS 16

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
	daos_unit_oid_t		 ui_oid;	/**< type <= OBJ */
	daos_key_t		 ui_dkey;	/**< type <= DKEY */
	daos_iod_t		*ui_iods;
	struct dcs_iod_csums	*ui_iods_csums;
	/* punched epochs per akey */
	daos_epoch_t		*ui_akey_punch_ephs;
	daos_epoch_t		*ui_rec_punch_ephs;
	int			 ui_iods_cap;
	int			 ui_iods_top;
	int			*ui_recxs_caps;
	/* punched epochs for dkey */
	daos_epoch_t		ui_dkey_punch_eph;
	d_sg_list_t		*ui_sgls;	/**< optional */
	uint32_t		 ui_version;
	uint32_t		 ui_is_array_exist:1;
};

typedef int (*dss_enum_unpack_cb_t)(struct dss_enum_unpack_io *io, void *arg);

int
dss_enum_unpack(daos_unit_oid_t oid, daos_key_desc_t *kds, int kds_num,
		d_sg_list_t *sgl, d_iov_t *csum, dss_enum_unpack_cb_t cb,
		void *cb_arg);

d_rank_t dss_self_rank(void);

unsigned int dss_ctx_nr_get(void);

/* Cache for container root */
struct tree_cache_root {
	struct btr_root	btr_root;
	daos_handle_t	root_hdl;
	unsigned int	count;
};

int
obj_tree_insert(daos_handle_t toh, uuid_t co_uuid, daos_unit_oid_t oid,
		d_iov_t *val_iov);
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
ds_migrate_query_status(uuid_t pool_uuid, uint32_t ver,
			struct ds_migrate_status *dms);
int
ds_object_migrate(struct ds_pool *pool, uuid_t pool_hdl_uuid, uuid_t cont_uuid,
		  uuid_t cont_hdl_uuid, int tgt_id, uint32_t version,
		  uint64_t max_eph, daos_unit_oid_t *oids, daos_epoch_t *ephs,
		  unsigned int *shards, int cnt, int clear_conts);
void
ds_migrate_fini_one(uuid_t pool_uuid, uint32_t ver);

void
ds_migrate_abort(uuid_t pool_uuid, uint32_t ver);

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

int notify_bio_error(int media_err_type, int tgt_id);

bool is_container_from_srv(uuid_t pool_uuid, uuid_t coh_uuid);
bool is_pool_from_srv(uuid_t pool_uuid, uuid_t poh_uuid);
#endif /* __DSS_API_H__ */
