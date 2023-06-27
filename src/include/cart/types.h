/*
 * (C) Copyright 2016-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * \file
 *
 * CaRT (Collective and RPC Transport) basic type definitions.
 */

/** @defgroup CART_TYPES CART Types */

/** @addtogroup CART_TYPES
 * @{
 */
#ifndef __CRT_TYPES_H__
#define __CRT_TYPES_H__

#include <stdint.h>
#include <gurt/types.h>
/**
 * Initialization options passed during crt_init() call.
 *
 * If the same value can be set via ENV variable as well as
 * a field, the field takes the precedence.
 */
typedef struct crt_init_options {
	/**
	 * Global RPC timeout.
	 *
	 * This field is similar in behavior to setting of CRT_TIMEOUT
	 * evnironment variable.
	 */
	int		cio_crt_timeout;
	/**
	 * if cio_sep_override is 0, the two fields following it won't be used.
	 */
	uint32_t	cio_sep_override:1,
			/**
			 * overrides the value of the environment variable
			 * CRT_CTX_SHARE_ADDR
			 */
			cio_use_sep:1,
			/** whether or not to inject faults */
			cio_fault_inject:1,
			/**
			 * whether or not to override credits. When set
			 * overrides CRT_CTX_EP_CREDITS envariable
			 */
			cio_use_credits:1,
			/** whether or not to enable per-context sensors */
			cio_use_sensors:1,

			/** whether or not to use expected sizes */
			cio_use_expected_size:1,
			cio_use_unexpected_size:1;
	/**
	 * overrides the value of the environment variable
	 * CRT_CTX_NUM
	 */
	int		cio_ctx_max_num;

	/** Used with cio_use_credits to set credit limit */
	int		cio_ep_credits;

	/**
	 * Hints to mercury/ofi to use for expected/unexpected
	 * maximum sizes of messages.
	 */
	uint32_t	cio_max_expected_size;
	uint32_t	cio_max_unexpected_size;
			/** swim crt index */
	int		cio_swim_crt_idx;

	/** if set, used as a provider value instead of CRT_PHY_ADDR_STR env */
	char		*cio_provider;

	/** If set, used as an interface setting instead of OFI_INTERFACE env */
	char		*cio_interface;

	/** If set, used as a domain setting instead of OFI_DOMAIN env */
	char		*cio_domain;

	/** If set, used as a port setting instead of OFI_PORT env */
	char		*cio_port;

	/** If set, used as the authentication key instead of D_PROVIDER_AUTH_KEY env */
	char		*cio_auth_key;
} crt_init_options_t;


typedef int		crt_status_t;
/**
 * CRT uses a string as the group ID
 * This string can only contain ASCII printable characters between 0x20 and 0x7E
 * Additionally, this string cannot contain quotation characters ("'`),
 *   backslashes (\), or semicolons (;)
 */
typedef d_string_t	crt_group_id_t;

/** max length of the group ID string including the trailing '\0' */
#define CRT_GROUP_ID_MAX_LEN	(64)

/** max length of the address string / URI including the trailing '\0' */
#define CRT_ADDR_STR_MAX_LEN		(128)

/** default group ID */
#define CRT_DEFAULT_GRPID	"crt_default_group"

/** Indicates rank not being set */
#define CRT_NO_RANK 0xFFFFFFFF

/** Indicates incarnation unavailable or inapplicable */
#define CRT_NO_INCARNATION 0

typedef struct crt_group {
	/** the group ID of this group */
	crt_group_id_t	cg_grpid;
} crt_group_t;

/** transport endpoint identifier */
typedef struct {
	/** group handle, NULL means the primary group */
	crt_group_t	*ep_grp;
	/** rank number within the group */
	d_rank_t	 ep_rank;
	/** tag, now used as the context ID of the target rank */
	uint32_t	 ep_tag;
} crt_endpoint_t;

/** CaRT context handle */
typedef void *crt_context_t;

/** Physical address string, e.g., "bmi+tcp://localhost:3344". */
typedef d_string_t crt_phy_addr_t;
#define CRT_PHY_ADDR_ENV	"CRT_PHY_ADDR_STR"

/**
 * RPC is identified by opcode. All the opcodes with the highest 16 bits as 1
 * are reserved for internal usage, such as group maintenance etc. If user
 * defines its RPC using those reserved opcode, then undefined result is
 * expected.
 */
typedef uint32_t crt_opcode_t;

/**
 * Check if the opcode is reserved by CRT internally.
 *
 * \param[in] opc		opcode to be checked.
 *
 * \retval			zero means legal opcode for user
 * \retval                      non-zero means CRT internally reserved opcode.
 */

typedef void *crt_rpc_input_t;
typedef void *crt_rpc_output_t;

typedef void *crt_bulk_t; /**< abstract bulk handle */
typedef void *crt_bulk_array_t; /**< abstract bulk array handle */

#define CRT_BULK_NULL		(NULL)
/**
 * max size of input/output parameters defined as 64M bytes, for larger length
 * the user should transfer by bulk.
 */
#define CRT_MAX_INPUT_SIZE	(0x4000000)
#define CRT_MAX_OUTPUT_SIZE	(0x4000000)

/** RPC flags enumeration */
enum crt_rpc_flags {
	/** send CORPC to filter_ranks only */
	CRT_RPC_FLAG_FILTER_INVERT	= (1U << 1)
};

struct crt_rpc;

/** Public RPC request/reply, exports to user */
typedef struct crt_rpc {
	crt_context_t		cr_ctx; /**< CRT context of the RPC */
	crt_endpoint_t		cr_ep; /**< endpoint ID */
	crt_opcode_t		cr_opc; /**< opcode of the RPC */
	crt_rpc_input_t		cr_input; /**< input parameter struct */
	crt_rpc_output_t	cr_output; /**< output parameter struct */
	size_t			cr_input_size; /**< size of input struct */
	size_t			cr_output_size; /**< size of output struct */
	/** optional bulk handle for collective RPC */
	crt_bulk_t		cr_co_bulk_hdl;
} crt_rpc_t;

/** Abstraction pack/unpack processor */
typedef void *crt_proc_t;
/** Proc callback for pack/unpack parameters */
typedef int (*crt_proc_cb_t)(crt_proc_t proc, void *data);

struct crt_req_format {
	crt_proc_cb_t		crf_proc_in;
	crt_proc_cb_t		crf_proc_out;
	const size_t		crf_size_in;
	const size_t		crf_size_out;
};

/** server-side RPC handler */
typedef void (*crt_rpc_cb_t)(crt_rpc_t *rpc);

/** specifies a member RPC of a protocol. */
struct crt_proto_rpc_format {
	/** the input/output format of the member RPC */
	struct crt_req_format	*prf_req_fmt;
	/** the RPC handler on the server side */
	crt_rpc_cb_t		 prf_hdlr;
	/** aggregation function for co-rpc */
	struct crt_corpc_ops	*prf_co_ops;
	/**
	 * RPC feature bits to toggle RPC behavior. Two flags are supported
	 * now: \ref CRT_RPC_FEAT_NO_REPLY and \ref CRT_RPC_FEAT_NO_TIMEOUT
	 */
	uint32_t		 prf_flags;
};

/** specify an RPC protocol */
struct crt_proto_format {
	const char			*cpf_name;
	uint32_t			 cpf_ver;
	/** number of RPCs in this protocol, i.e. number of entries in
	 * cpf_prf
	 */
	uint32_t			 cpf_count;
	/** Array of RPC definitions */
	struct crt_proto_rpc_format	*cpf_prf;
	/** protocol base opcode */
	crt_opcode_t			 cpf_base;

};

/**
 * given the base opcode, version of a protocol, and a member RPC index, compute
 * the RPC opcode of that member RPC
 */
#define CRT_PROTO_OPC(base_opc, version, rpc_index)			\
	((uint32_t)(base_opc) |						\
	(uint32_t)(version) << 16 |					\
	(uint32_t)(rpc_index))

/**
 * argument available to the completion callback in crt_proto_query().
 */
struct crt_proto_query_cb_info {
	/** user data passed-in to crt_proto_query() as arg */
	void	*pq_arg;
	/**
	 * contains the highest version supported by the target
	 * when pq_rc == DER_SUCCESS
	 */
	int	 pq_ver;
	/** return falue */
	int	 pq_rc;
};

/**
 * The completion callback to crt_proto_query().
 */
typedef void (*crt_proto_query_cb_t)(struct crt_proto_query_cb_info *cb_info);

/** Bulk transfer modes */
typedef enum {
	CRT_BULK_PUT = 0x68,
	CRT_BULK_GET,
} crt_bulk_op_t;

/* CRT RPC feature bits definitions */

/**
 * Disable/enable the reply of a RPC.  By default one RPC needs to be replied
 * (by calling crt_reply_send within RPC handler at target-side) to complete the
 * RPC request at origin-side.  One-way RPC is a special type that the RPC
 * request need not to be replied, the RPC request is treated as completed after
 * being sent out.
 *
 * Notes for one-way RPC:
 * 1) Need not reply for one-way RPC, calling crt_reply_send() will fail with
 *    -DER_PROTO.
 * 2) For one-way RPC, user needs to disable the reply on both origin and target
 *    side, or undefined result is expected.
 * 3) Corpc musted be replied, disabling the reply of corpc will lead to
 *    undefined result.
 */
#define CRT_RPC_FEAT_NO_REPLY		(1U << 1)

/**
 * Do not fail RPC with -CER_TIMEDOUT. Callback is only invoked on errors,
 * completion, or target eviction. This differs from an RPC with an infinite
 * timeout as the internal timer is still used to check for target eviction.
 */
#define CRT_RPC_FEAT_NO_TIMEOUT		(1U << 2)

/**
 * If RPC ends up being queued due to exceeding in-flight rpc limit, queue
 * at the front of the queue. If not set, queues at the end
 */
#define CRT_RPC_FEAT_QUEUE_FRONT	(1U << 3)

typedef void *crt_bulk_opid_t;

/** Bulk transfer permissions */
typedef enum {
	/** read/write */
	CRT_BULK_RW = 0x88,
	/** read-only */
	CRT_BULK_RO,
	/** write-only */
	CRT_BULK_WO,
} crt_bulk_perm_t;

/** bulk transferring descriptor */
struct crt_bulk_desc {
	crt_rpc_t	*bd_rpc; /**< original RPC request */
	crt_bulk_op_t	 bd_bulk_op; /**< CRT_BULK_PUT or CRT_BULK_GET */
	crt_bulk_t	 bd_remote_hdl; /**< remote bulk handle */
	off_t		 bd_remote_off; /**< offset within remote bulk buffer */
	crt_bulk_t	 bd_local_hdl; /**< local bulk handle */
	off_t		 bd_local_off; /**< offset within local bulk buffer */
	size_t		 bd_len; /**< length of the bulk transferring */
};

/** Callback info structure */
struct crt_cb_info {
	crt_rpc_t		*cci_rpc; /**< rpc struct */
	void			*cci_arg; /**< User passed in arg */
	/**
	 * Return code of the operation.
	 *
	 * This value will be set as:
	 * 0                     for succeed RPC request,
	 * -DER_TIMEDOUT         for timed out request,
	 * other negative value  for other possible failure.
	 */
	int			 cci_rc;
};

/** Bulk callback info structure */
struct crt_bulk_cb_info {
	struct crt_bulk_desc	*bci_bulk_desc; /**< bulk descriptor */
	void			*bci_arg; /**< User passed in arg */
	int			 bci_rc; /**< return code */
};

/**
 * completion callback for crt_req_send
 *
 * \param[in] cb_info		pointer to call back info.
 *				If an error occurred on the server outside the
 *				user RPC handler, the server will send back a
 *				CART level error code. This error code is
 *				available in the completion callback as
 *				cb_info->cci_rc. Currently supported CART level
 *				error codes are:
 *				DER_UNREG	The opcode is not registered or
 *						registered without an RPC
 *						handler on the target.
 *				DER_NOREPLY	The RPC handler on the server
 *						forgot to call crt_reply_send()
 *				DER_DOS		There's not enough resource on
 *						the server to service this
 *						request. (memory allocation
 *						error)
 *				DER_MISC	All other errors outside the
 *						user RPC handler
 *
 */
typedef void (*crt_cb_t)(const struct crt_cb_info *cb_info);

typedef void (*crt_generic_cb_t)(void *cb_info);

/** completion callback for bulk transferring, i.e. crt_bulk_transfer()
 *
 * \param[in] cb_info	Callback info structure
 */
typedef int (*crt_bulk_cb_t)(const struct crt_bulk_cb_info *cb_info);

/**
 * Progress condition callback, see \ref crt_progress().
 *
 * \param[in] arg		argument to cond_cb.
 *
 * \retval			zero means continue progressing
 * \retval			>0 means stopping progress and return success
 * \retval			<0 means failure
 */
typedef int (*crt_progress_cond_cb_t)(void *arg);

/**
 * CRT Initialization flag bits.
 *
 * Zero or more OR-ed flags could be passed to crt_init().
 */
enum crt_init_flag_bits {
	/**
	 * When set enables the server mode which listens
	 * for incoming requests. Clients should not set this flag
	 */
	CRT_FLAG_BIT_SERVER		= 1U << 0,

	/**
	 * When set, disables automatic SWIM start-up at init time.
	 * Instead SWIM needs to be enabled via \ref crt_swim_init()
	 * call.
	 */
	CRT_FLAG_BIT_AUTO_SWIM_DISABLE	= 1U << 1,
};

/**
 * Operations for \ref crt_group_primary_modify and
 * \ref crt_group_secondary_modify APIs. See \ref crt_group_primary_modify
 * for description of operation behavior.
 */
typedef enum {
	/** Replace operation */
	CRT_GROUP_MOD_OP_REPLACE = 0,

	/** Addition operation */
	CRT_GROUP_MOD_OP_ADD,

	/** Removal operation */
	CRT_GROUP_MOD_OP_REMOVE,

	/** Total count of supported operations */
	CRT_GROUP_MOD_OP_COUNT,
} crt_group_mod_op_t;

/** @}
 */
#endif /* __CRT_TYPES_H__ */
