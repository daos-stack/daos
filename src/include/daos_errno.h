/**
 * (C) Copyright 2016-2023 Intel Corporation.
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

/* clang-format off */

/** Preprocessor macro defining GURT errno values and internal definition of d_errstr */
#define D_FOREACH_GURT_ERR(ACTION)                                                                 \
	/** no permission */                                                                       \
	ACTION(DER_NO_PERM, Operation not permitted)                                               \
	/** invalid handle */                                                                      \
	ACTION(DER_NO_HDL, Invalid handle)                                                         \
	/** invalid parameters */                                                                  \
	ACTION(DER_INVAL, Invalid parameters)                                                      \
	/** entity already exists */                                                               \
	ACTION(DER_EXIST, Entity already exists)                                                   \
	/** nonexistent entity */                                                                  \
	ACTION(DER_NONEXIST, The specified entity does not exist)                                  \
	/** unreachable node */                                                                    \
	ACTION(DER_UNREACH, Unreachable node)                                                      \
	/** no space on storage target */                                                          \
	ACTION(DER_NOSPACE, No space on storage target)                                            \
	/** already did sth */                                                                     \
	ACTION(DER_ALREADY, Operation already performed)                                           \
	/** NO memory */                                                                           \
	ACTION(DER_NOMEM, Out of memory)                                                           \
	/** Function not implemented */                                                            \
	ACTION(DER_NOSYS, Function not implemented)                                                \
	/** timed out */                                                                           \
	ACTION(DER_TIMEDOUT, Time out)                                                             \
	/** Busy */                                                                                \
	ACTION(DER_BUSY, Device or resource busy)                                                  \
	/** Try again */                                                                           \
	ACTION(DER_AGAIN, Try again)                                                               \
	/** Incompatible protocol */                                                               \
	ACTION(DER_PROTO, Incompatible protocol)                                                   \
	/** not initialized */                                                                     \
	ACTION(DER_UNINIT, Device or resource not initialized)                                     \
	/** buffer too short (larger buffer needed) */                                             \
	ACTION(DER_TRUNC, Buffer too short)                                                        \
	/** data too long for defined data type or buffer size */                                  \
	ACTION(DER_OVERFLOW, Data too long for defined data type or buffer size)                   \
	/** operation canceled */                                                                  \
	ACTION(DER_CANCELED, Operation canceled)                                                   \
	/** Out-Of-Group or member list */                                                         \
	ACTION(DER_OOG, Out of group or member list)                                               \
	/** transport layer mercury error */                                                       \
	ACTION(DER_HG, Transport layer mercury error)                                              \
	/** RPC or protocol version not registered */                                              \
	ACTION(DER_UNREG, RPC or protocol version not registered)                                  \
	/** failed to generate an address string */                                                \
	ACTION(DER_ADDRSTR_GEN, Failed to generate an address string)                              \
	/** PMIx layer error */                                                                    \
	ACTION(DER_PMIX, PMIx layer error)                                                         \
	/** IV callback - cannot handle locally */                                                 \
	ACTION(DER_IVCB_FORWARD, Incast variable unavailable locally. Must forward)                \
	/** miscellaneous error */                                                                 \
	ACTION(DER_MISC, Miscellaneous error)                                                      \
	/** Bad path name */                                                                       \
	ACTION(DER_BADPATH, Bad path name)                                                         \
	/** Not a directory */                                                                     \
	ACTION(DER_NOTDIR, Not a directory)                                                        \
	/** corpc failed */                                                                        \
	ACTION(DER_CORPC_INCOMPLETE, Collective RPC failed)                                        \
	/** no rank is subscribed to RAS */                                                        \
	ACTION(DER_NO_RAS_RANK, No rank is subscribed to RAS)                                      \
	/** service group not attached */                                                          \
	ACTION(DER_NOTATTACH, Service group not attached)                                          \
	/** version mismatch */                                                                    \
	ACTION(DER_MISMATCH, Version mismatch)                                                     \
	/** rank has been excluded */                                                              \
	ACTION(DER_EXCLUDED, Rank has been excluded)                                               \
	/** user-provided RPC handler didn't send reply back */                                    \
	ACTION(DER_NOREPLY, User provided RPC handler did not send reply back)                     \
	/** denial-of-service */                                                                   \
	ACTION(DER_DOS, Denial of service)                                                         \
	/** Incorrect target for the RPC  */                                                       \
	ACTION(DER_BAD_TARGET, Incorrect target for the RPC)                                       \
	/** Group versioning mismatch */                                                           \
	ACTION(DER_GRPVER, Group versioning mismatch)                                              \
	/** HLC synchronization error */                                                           \
	ACTION(DER_HLC_SYNC, HLC synchronization error)                                            \
	/** No shared memory available */                                                          \
	ACTION(DER_NO_SHMEM, Not enough shared memory free)                                        \
	/** Failed to add metric */                                                                \
	ACTION(DER_ADD_METRIC_FAILED, Failed to add the specified metric)                          \
	/** Duration start/end mismatch */                                                         \
	ACTION(DER_DURATION_MISMATCH, Duration end not paired with duration start)                 \
	/** Operation not permitted on metric type*/                                               \
	ACTION(DER_OP_NOT_PERMITTED, Operation not permitted for metric type provided)             \
	/** Metric path name exceeds permitted length*/                                            \
	ACTION(DER_EXCEEDS_PATH_LEN, Path name exceeds permitted length)                           \
	/** Metric was not found.*/                                                                \
	ACTION(DER_METRIC_NOT_FOUND, Read failed because metric not found)                         \
	/** Invalid user/group permissions.*/                                                      \
	ACTION(DER_SHMEM_PERMS, Unable to access shared memory segment due to incompatible user or group permissions) \
	/** Fatal (non-retry-able) transport layer mercury error */                                \
	ACTION(DER_HG_FATAL, Fatal transport layer mercury error)
	/** TODO: add more error numbers */

/** Preprocessor macro defining DAOS errno values and internal definition of d_errstr */
#define D_FOREACH_DAOS_ERR(ACTION)                                                                 \
	/** Generic I/O error */                                                                   \
	ACTION(DER_IO, I / O error)                                                                \
	/** Memory free error */                                                                   \
	ACTION(DER_FREE_MEM, Memory free error)                                                    \
	/** Entry not found */                                                                     \
	ACTION(DER_ENOENT, Entity not found)                                                       \
	/** Unknown object type */                                                                 \
	ACTION(DER_NOTYPE, Unknown object type)                                                    \
	/** Unknown object schema */                                                               \
	ACTION(DER_NOSCHEMA, Unknown object schema)                                                \
	/** Object is not local */                                                                 \
	ACTION(DER_NOLOCAL, Object is not local)                                                   \
	/** stale pool map version */                                                              \
	ACTION(DER_STALE, Stale pool map version)                                                  \
	/** Not service leader */                                                                  \
	ACTION(DER_NOTLEADER, Not service leader)                                                  \
	/** * Target create error */                                                               \
	ACTION(DER_TGT_CREATE, Target create error)                                                \
	/** Epoch is read-only */                                                                  \
	ACTION(DER_EP_RO, Epoch is read only)                                                      \
	/** Epoch is too old, all data have been recycled */                                       \
	ACTION(DER_EP_OLD, Epoch is too old. All data have been recycled)                          \
	/** Key is too large */                                                                    \
	ACTION(DER_KEY2BIG, Key is too large)                                                      \
	/** Record is too large */                                                                 \
	ACTION(DER_REC2BIG, Record is too large)                                                   \
	/** IO buffers can't match object extents */                                               \
	ACTION(DER_IO_INVAL, I / O buffers do not match object extents)                            \
	/** Event queue is busy */                                                                 \
	ACTION(DER_EQ_BUSY, Event queue is busy)                                                   \
	/** Domain of cluster component can't match */                                             \
	ACTION(DER_DOMAIN, Domain of cluster component do not match)                               \
	/** Service should shut down */                                                            \
	ACTION(DER_SHUTDOWN, Service should shut down)                                             \
	/** Operation now in progress */                                                           \
	ACTION(DER_INPROGRESS, Operation now in progress)                                          \
	/** Not applicable. */                                                                     \
	ACTION(DER_NOTAPPLICABLE, Not applicable)                                                  \
	/** Not a service replica */                                                               \
	ACTION(DER_NOTREPLICA, Not a service replica)                                              \
	/** Checksum error */                                                                      \
	ACTION(DER_CSUM, Checksum error)                                                           \
	/** Unsupported durable format */                                                          \
	ACTION(DER_DF_INVAL, Unsupported durable format)                                           \
	/** Incompatible durable format version */                                                 \
	ACTION(DER_DF_INCOMPT, Incompatible durable format version)                                \
	/** Record size error */                                                                   \
	ACTION(DER_REC_SIZE, Record size error)                                                    \
	/** Used to indicate a transaction should restart */                                       \
	ACTION(DER_TX_RESTART, Transaction should restart)                                         \
	/** Data lost or not recoverable */                                                        \
	ACTION(DER_DATA_LOSS, Data lost or not recoverable)                                        \
	/** Operation canceled (non-crt) */                                                        \
	ACTION(DER_OP_CANCELED, Operation canceled)                                                \
	/** TX is not committed, not sure whether committable or not */                            \
	ACTION(DER_TX_BUSY, TX is not committed)                                                   \
	/** Agent is incompatible with libdaos */                                                  \
	ACTION(DER_AGENT_INCOMPAT, Agent is incompatible with libdaos)                             \
	/** Needs to be handled via distributed transaction. */                                    \
	ACTION(DER_NEED_TX, To be handled via distributed transaction)                             \
	/** #failures exceed RF(Redundancy Factor), data possibly lost */                          \
	ACTION(DER_RF, Failures exceed RF)                                                         \
	/** Re-fetch again, an internal error code used in EC deg-fetch */                         \
	ACTION(DER_FETCH_AGAIN, Fetch again)                                                       \
	/** Hit uncertain DTX, may need to try with other replica. */                              \
	ACTION(DER_TX_UNCERTAIN, TX status is uncertain)                                           \
	/** Communicatin issue with agent. */                                                      \
	ACTION(DER_AGENT_COMM, Agent communication error)                                          \
	/** ID mismatch */                                                                         \
	ACTION(DER_ID_MISMATCH, ID mismatch)                                                       \
	/** Retry with other target, an internal error code used in EC deg-fetch. */               \
	ACTION(DER_TGT_RETRY, Retry with other target)                                             \
	ACTION(DER_NOTSUPPORTED, Operation not supported)                                          \
	ACTION(DER_CONTROL_INCOMPAT, One or more control plane components are incompatible)        \
	/** No service available */                                                                \
	ACTION(DER_NO_SERVICE, No service available)                                               \
	/** The TX ID may be reused. */                                                            \
	ACTION(DER_TX_ID_REUSED, TX ID may be reused)                                              \
	/** Re-update again */                                                                     \
	ACTION(DER_UPDATE_AGAIN, update again)                                                     \
	ACTION(DER_NVME_IO, NVMe I / O error)                                                      \
	ACTION(DER_NO_CERT, Unable to access one or more certificates)                             \
	ACTION(DER_BAD_CERT, Invalid x509 certificate)                                             \
	ACTION(DER_VOS_PARTIAL_UPDATE, VOS partial update error)                                   \
	ACTION(DER_CHKPT_BUSY, Page is temporarily read only due to checkpointing)                 \
	ACTION(DER_DIV_BY_ZERO,	Division by zero)

/* clang-format on */

/** Defines the gurt error codes */
#define D_FOREACH_ERR_RANGE(ACTION)	\
	ACTION(GURT,	1000)		\
	ACTION(DAOS,	2000)

/** Preprocessor machinery for defining error numbers */
#define D_DEFINE_ERRNO(name, desc)   name,
/** Preprocessor machinery for defining error number strings */
#define D_DEFINE_ERRSTR(name, desc)  #name,
/** Preprocessor machinery for defining error descriptions */
#define D_DEFINE_ERRDESC(name, desc) #desc,

/** Preprocessor machinery to define a consecutive range of error numbers */
#define D_DEFINE_RANGE_ERRNO(name, base)                                                           \
	DER_ERR_##name##_BASE = (base), D_FOREACH_##name##_ERR(D_DEFINE_ERRNO)

/** The actual error codes */
enum daos_errno {
	/** Return value representing success */
	DER_SUCCESS = 0,
	D_FOREACH_ERR_RANGE(D_DEFINE_RANGE_ERRNO)
	/** Unknown error value */
	DER_UNKNOWN = (DER_ERR_GURT_BASE + 500000),
};

/** Return a string associated with a registered gurt errno
 *
 * \param[in]	errnum	The error code
 *
 * \return	String value for error code or DER_UNKNOWN
 */
const char *
d_errstr(int errnum);

/** Return an error description string associated with a registered gurt errno.
 *
 * \param[in]	errnum	The error code
 *
 * \return		The error description string, or an "Unknown error nnn" message if the error
 * 			number is unknown.
 */
const char *
d_errdesc(int errnum);

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
