/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * DAOS Error numbers
 */

#ifndef __DAOS_ERRNO_H__
#define __DAOS_ERRNO_H__

#if defined(__cplusplus)
extern "C" {
#endif

/** @addtogroup GURT
 * @{
 */

/*
 * This preprocessor machinery defines the errno values but also
 * enables the internal definition of d_errstr.  A new macro should
 * be defined for each non-contiguous range
 */

#define D_FOREACH_GURT_ERR(ACTION)					\
	/** no permission */						\
	ACTION(DER_NO_PERM,		(DER_ERR_GURT_BASE + 1),	\
	       Operation not permitted)					\
	/** invalid handle */						\
	ACTION(DER_NO_HDL,		(DER_ERR_GURT_BASE + 2),	\
	       Invalid handle)						\
	/** invalid parameters */					\
	ACTION(DER_INVAL,		(DER_ERR_GURT_BASE + 3),	\
	       Invalid parameters)					\
	/** entity already exists */					\
	ACTION(DER_EXIST,		(DER_ERR_GURT_BASE + 4),	\
	       Entity already exists)					\
	/** nonexistent entity */					\
	ACTION(DER_NONEXIST,		(DER_ERR_GURT_BASE + 5),	\
	       The specified entity does not exist)			\
	/** unreachable node */						\
	ACTION(DER_UNREACH,		(DER_ERR_GURT_BASE + 6),	\
	       Unreachable node)					\
	/** no space on storage target */				\
	ACTION(DER_NOSPACE,		(DER_ERR_GURT_BASE + 7),	\
	       No space on storage target)				\
	/** already did sth */						\
	ACTION(DER_ALREADY,		(DER_ERR_GURT_BASE + 8),	\
	       Operation already performed)				\
	/** NO memory */						\
	ACTION(DER_NOMEM,		(DER_ERR_GURT_BASE + 9),	\
	       Out of memory)						\
	/** Function not implemented */					\
	ACTION(DER_NOSYS,		(DER_ERR_GURT_BASE + 10),	\
	       Function not implemented)				\
	/** timed out */						\
	ACTION(DER_TIMEDOUT,		(DER_ERR_GURT_BASE + 11),	\
	       Time out)						\
	/** Busy */							\
	ACTION(DER_BUSY,		(DER_ERR_GURT_BASE + 12),	\
	       Device or resource busy)					\
	/** Try again */						\
	ACTION(DER_AGAIN,		(DER_ERR_GURT_BASE + 13),	\
	       Try again)						\
	/** incompatible protocol */					\
	ACTION(DER_PROTO,		(DER_ERR_GURT_BASE + 14),	\
	       Incompatible protocol)					\
	/** not initialized */						\
	ACTION(DER_UNINIT,		(DER_ERR_GURT_BASE + 15),	\
	       Device or resource not initialized)			\
	/** buffer too short (larger buffer needed) */			\
	ACTION(DER_TRUNC,		(DER_ERR_GURT_BASE + 16),	\
	       Buffer too short)					\
	/** data too long for defined data type or buffer size */	\
	ACTION(DER_OVERFLOW,		(DER_ERR_GURT_BASE + 17),	\
	       Data too long for defined data type or buffer size)	\
	/** operation canceled */					\
	ACTION(DER_CANCELED,		(DER_ERR_GURT_BASE + 18),	\
	       Operation canceled)					\
	/** Out-Of-Group or member list */				\
	ACTION(DER_OOG,			(DER_ERR_GURT_BASE + 19),	\
	       Out of group or member list)				\
	/** transport layer mercury error */				\
	ACTION(DER_HG,			(DER_ERR_GURT_BASE + 20),	\
	       Transport layer mercury error)				\
	/** RPC or protocol version not registered */			\
	ACTION(DER_UNREG,		(DER_ERR_GURT_BASE + 21),	\
	       RPC or protocol version not registered)			\
	/** failed to generate an address string */			\
	ACTION(DER_ADDRSTR_GEN,		(DER_ERR_GURT_BASE + 22),	\
	       Failed to generate an address string)			\
	/** PMIx layer error */						\
	ACTION(DER_PMIX,		(DER_ERR_GURT_BASE + 23),	\
	       PMIx layer error)					\
	/** IV callback - cannot handle locally */			\
	ACTION(DER_IVCB_FORWARD,	(DER_ERR_GURT_BASE + 24),	\
	       Incast variable unavailable locally. Must forward)	\
	/** miscellaneous error */					\
	ACTION(DER_MISC,		(DER_ERR_GURT_BASE + 25),	\
	       Miscellaneous error)					\
	/** Bad path name */						\
	ACTION(DER_BADPATH,		(DER_ERR_GURT_BASE + 26),	\
	       Bad path name)						\
	/** Not a directory */						\
	ACTION(DER_NOTDIR,		(DER_ERR_GURT_BASE + 27),	\
	       Not a directory)						\
	/** corpc failed */						\
	ACTION(DER_CORPC_INCOMPLETE,	(DER_ERR_GURT_BASE + 28),	\
	       Collective RPC failed)					\
	/** no rank is subscribed to RAS */				\
	ACTION(DER_NO_RAS_RANK,		(DER_ERR_GURT_BASE + 29),	\
	       No rank is subscribed to RAS)				\
	/** service group not attached */				\
	ACTION(DER_NOTATTACH,		(DER_ERR_GURT_BASE + 30),	\
	       Service group not attached)				\
	/** version mismatch */						\
	ACTION(DER_MISMATCH,		(DER_ERR_GURT_BASE + 31),	\
	       Version mismatch)					\
	/** rank has been excluded */					\
	ACTION(DER_EXCLUDED,		(DER_ERR_GURT_BASE + 32),	\
	       Rank has been excluded)					\
	/** user-provided RPC handler didn't send reply back */		\
	ACTION(DER_NOREPLY,		(DER_ERR_GURT_BASE + 33),	\
	       User provided RPC handler did not send reply back)	\
	/** denial-of-service */					\
	ACTION(DER_DOS,			(DER_ERR_GURT_BASE + 34),       \
	       Denial of service)					\
	/** Incorrect target for the RPC  */				\
	ACTION(DER_BAD_TARGET,		(DER_ERR_GURT_BASE + 35),	\
	       Incorrect target for the RPC)				\
	/** Group versioning mismatch */				\
	ACTION(DER_GRPVER,		(DER_ERR_GURT_BASE + 36),	\
	       Group versioning mismatch)				\
	/** HLC synchronization error */				\
	ACTION(DER_HLC_SYNC,		(DER_ERR_GURT_BASE + 37),	\
	       HLC synchronization error)				\
	/** No shared memory available */				\
	ACTION(DER_NO_SHMEM,		(DER_ERR_GURT_BASE + 38),	\
	       Not enough shared memory free)				\
	/** Failed to add metric */					\
	ACTION(DER_ADD_METRIC_FAILED,   (DER_ERR_GURT_BASE + 39),	\
	       Failed to add the specified metric)			\
	/** Duration start/end mismatch */				\
	ACTION(DER_DURATION_MISMATCH,   (DER_ERR_GURT_BASE + 40),	\
	       Duration end not paired with duration start)		\
	/** Operation not permitted on metric type*/			\
	ACTION(DER_OP_NOT_PERMITTED,    (DER_ERR_GURT_BASE + 41),	\
	       Operation not permitted for metric type provided)	\
	/** Metric path name exceeds permitted length*/			\
	ACTION(DER_EXCEEDS_PATH_LEN,    (DER_ERR_GURT_BASE + 42),	\
	       Path name exceeds permitted length)			\
	/** Metric was not found.*/					\
	ACTION(DER_METRIC_NOT_FOUND,    (DER_ERR_GURT_BASE + 43),	\
	       Read failed because metric not found)			\
	/** Invalid user/group permissions.*/				\
	ACTION(DER_SHMEM_PERMS,         (DER_ERR_GURT_BASE + 44),	\
	       Unable to access shared memory segment due to		\
	       incompatible user or group permissions)
	/** TODO: add more error numbers */

#define D_FOREACH_DAOS_ERR(ACTION)					\
	/** Generic I/O error */					\
	ACTION(DER_IO,			(DER_ERR_DAOS_BASE + 1),	\
	       I/O error)						\
	/** Memory free error */					\
	ACTION(DER_FREE_MEM,		(DER_ERR_DAOS_BASE + 2),	\
	       Memory free error)					\
	/** Entry not found */						\
	ACTION(DER_ENOENT,		(DER_ERR_DAOS_BASE + 3),	\
	       Entity not found)					\
	/** Unknown object type */					\
	ACTION(DER_NOTYPE,		(DER_ERR_DAOS_BASE + 4),	\
	       Unknown object type)					\
	/** Unknown object schema */					\
	ACTION(DER_NOSCHEMA,		(DER_ERR_DAOS_BASE + 5),	\
	       Unknown object schema)					\
	/** Object is not local */					\
	ACTION(DER_NOLOCAL,		(DER_ERR_DAOS_BASE + 6),	\
	       Object is not local)					\
	/** stale pool map version */					\
	ACTION(DER_STALE,		(DER_ERR_DAOS_BASE + 7),	\
	       Stale pool map version)					\
	/** Not service leader */					\
	ACTION(DER_NOTLEADER,		(DER_ERR_DAOS_BASE + 8),	\
	       Not service leader)					\
	/** * Target create error */					\
	ACTION(DER_TGT_CREATE,		(DER_ERR_DAOS_BASE + 9),	\
	       Target create error)					\
	/** Epoch is read-only */					\
	ACTION(DER_EP_RO,		(DER_ERR_DAOS_BASE + 10),	\
	       Epoch is read only)					\
	/** Epoch is too old, all data have been recycled */		\
	ACTION(DER_EP_OLD,		(DER_ERR_DAOS_BASE + 11),	\
	       Epoch is too old. All data have been recycled)		\
	/** Key is too large */						\
	ACTION(DER_KEY2BIG,		(DER_ERR_DAOS_BASE + 12),	\
	       Key is too large)					\
	/** Record is too large */					\
	ACTION(DER_REC2BIG,		(DER_ERR_DAOS_BASE + 13),	\
	       Record is too large)					\
	/** IO buffers can't match object extents */			\
	ACTION(DER_IO_INVAL,		(DER_ERR_DAOS_BASE + 14),	\
	       I/O buffers do not match object extents)			\
	/** Event queue is busy */					\
	ACTION(DER_EQ_BUSY,		(DER_ERR_DAOS_BASE + 15),	\
	       Event queue is busy)					\
	/** Domain of cluster component can't match */			\
	ACTION(DER_DOMAIN,		(DER_ERR_DAOS_BASE + 16),	\
	       Domain of cluster component do not match)		\
	/** Service should shut down */					\
	ACTION(DER_SHUTDOWN,		(DER_ERR_DAOS_BASE + 17),	\
	       Service should shut down)				\
	/** Operation now in progress */				\
	ACTION(DER_INPROGRESS,		(DER_ERR_DAOS_BASE + 18),	\
	       Operation now in progress)				\
	/** Not applicable. */						\
	ACTION(DER_NOTAPPLICABLE,	(DER_ERR_DAOS_BASE + 19),	\
	       Not applicable)						\
	/** Not a service replica */					\
	ACTION(DER_NOTREPLICA,		(DER_ERR_DAOS_BASE + 20),	\
	       Not a service replica)					\
	/** Checksum error */						\
	ACTION(DER_CSUM,		(DER_ERR_DAOS_BASE + 21),	\
	       Checksum error)						\
	/** Unsupported durable format */				\
	ACTION(DER_DF_INVAL,		(DER_ERR_DAOS_BASE + 22),	\
	       Unsupported durable format)				\
	/** Incompatible durable format version */			\
	ACTION(DER_DF_INCOMPT,		(DER_ERR_DAOS_BASE + 23),	\
	       Incompatible durable format version)			\
	/** Record size error */					\
	ACTION(DER_REC_SIZE,		(DER_ERR_DAOS_BASE + 24),	\
	       Record size error)					\
	/** Used to indicate a transaction should restart */		\
	ACTION(DER_TX_RESTART,		(DER_ERR_DAOS_BASE + 25),	\
	       Transaction should restart)				\
	/** Data lost or not recoverable */				\
	ACTION(DER_DATA_LOSS,		(DER_ERR_DAOS_BASE + 26),	\
	       Data lost or not recoverable)				\
	/** Operation canceled (non-crt) */				\
	ACTION(DER_OP_CANCELED,		(DER_ERR_DAOS_BASE + 27),	\
	       Operation canceled)					\
	/** TX is not committed, not sure whether committable or not */	\
	ACTION(DER_TX_BUSY,		(DER_ERR_DAOS_BASE + 28),	\
	       TX is not committed)					\
	/** Agent is incompatible with libdaos */			\
	ACTION(DER_AGENT_INCOMPAT,	(DER_ERR_DAOS_BASE + 29),	\
	       Agent is incompatible with libdaos)			\
	/** Multiple shards locate on the same target */		\
	ACTION(DER_SHARDS_OVERLAP,	(DER_ERR_DAOS_BASE + 30),	\
	       Shards overlap)						\
	/** #failures exceed RF(Redundancy Factor), data possibly lost */ \
	ACTION(DER_RF,			(DER_ERR_DAOS_BASE + 31),	\
	       Failures exceed RF)					\
	/** Re-fetch again, an internal error code used in EC deg-fetch */ \
	ACTION(DER_FETCH_AGAIN,		(DER_ERR_DAOS_BASE + 32),	\
	       Fetch again)						\
	/** Hit uncertain DTX, may need to try with other replica. */	\
	ACTION(DER_TX_UNCERTAIN,	(DER_ERR_DAOS_BASE + 33),	\
	       TX status is uncertaion)					\
	/** Communicatin issue with agent. */				\
	ACTION(DER_AGENT_COMM,		(DER_ERR_DAOS_BASE + 34),	\
	       Agent communication error)				\
	/** ID mismatch */						\
	ACTION(DER_ID_MISMATCH,		(DER_ERR_DAOS_BASE + 35),	\
	       ID mismatch)						\

/** Defines the gurt error codes */
#define D_FOREACH_ERR_RANGE(ACTION)	\
	ACTION(GURT,	1000)		\
	ACTION(DAOS,	2000)

#define D_DEFINE_ERRNO(name, value, desc) name = value,
#define D_DEFINE_ERRSTR(name, value, desc) #name,
#define D_DEFINE_ERRDESC(name, value, desc) #desc,

#define D_DEFINE_RANGE_ERRNO(name, base)			\
	enum {							\
		DER_ERR_##name##_BASE		=	(base),	\
		D_FOREACH_##name##_ERR(D_DEFINE_ERRNO)		\
		DER_ERR_##name##_LIMIT,				\
	};

#define D_DEFINE_RANGE_ERRSTR(name)				\
	static const char * const g_##name##_error_strings[] = {\
		D_FOREACH_##name##_ERR(D_DEFINE_ERRSTR)		\
	}; \
	static const char * const g_##name##_strerror[] = {	\
		D_FOREACH_##name##_ERR(D_DEFINE_ERRDESC)	\
	};

D_FOREACH_ERR_RANGE(D_DEFINE_RANGE_ERRNO)

/** Macro to register a range defined using D_DEFINE_RANGE macros */
#define D_REGISTER_RANGE(name)				\
	d_errno_register_range(DER_ERR_##name##_BASE,	\
			       DER_ERR_##name##_LIMIT,	\
			       g_##name##_error_strings,\
			       g_##name##_strerror)

/** Macro to deregister a range defined using D_DEFINE_RANGE macros */
#define D_DEREGISTER_RANGE(name)			\
	d_errno_deregister_range(DER_ERR_##name##_BASE)

#define DER_SUCCESS	0
#define DER_UNKNOWN	(DER_ERR_GURT_BASE + 500000)

/** Return a string associated with a registered gurt errno
 *
 * \param[in]	rc	The error code
 *
 * \return	String value for error code or DER_UNKNOWN
 */
const char *d_errstr(int rc);

/** Register error codes with gurt.  Use D_REGISTER_RANGE.
 *
 * \param[in]	start		Start of error range. Actual errors start at
 *				\p start + 1
 * \param[in]	end		End of range. All error codes should be less
 *				than \p end
 * \param[in]	error_strings	Array of strings. Must be one per
 *				code in the range
 * \param[in]	strerror	Array of strings. Must be one per
 *				code in the range
 *
 * \return	0 on success, otherwise error code
 */
int d_errno_register_range(int start, int end,
			   const char * const *error_strings,
			   const char * const *strerror);

/** De-register error codes with gurt.  Use D_DEREGISTER_RANGE.
 *
 * \param[in]	start	Start of error range
 */
void d_errno_deregister_range(int start);

/** Return an error description string associated with a registered gurt errno.
 *
 * \param[in]	errnum	The error code
 *
 * \return		The error description string, or an "Unknown
 *			error nnn" message if the error number is unknown.
 */
const char *d_errdesc(int errnum);

/** @}
 */

#define DO_PRAGMA(str)	_Pragma(#str)
#define DEPRECATE_ERROR(olde, newe)				\
({								\
	DO_PRAGMA(message(#olde " is deprecated, use " #newe)); \
	newe;							\
})
#define DER_EVICTED DEPRECATE_ERROR(DER_EVICTED, DER_EXCLUDED)

#ifndef DF_RC
#define DF_RC "%s(%d): '%s'"
#define DP_RC(rc) d_errstr(rc), rc, d_errdesc(rc)
#endif /* DF_RC */

#if defined(__cplusplus)
}
#endif

#endif /*  __DAOS_ERRNO_H__ */
