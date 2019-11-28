#include "DaosJNI.h"
#include <jni.h>
#include <daos.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <mpi.h>
#include <math.h>
#include <uuid/uuid.h>
#include <gurt/debug.h>
#include <gurt/common.h>
#include <daos_fs.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <libgen.h>



typedef struct _ioreq {
	daos_key_t	dkey;
	daos_recx_t	recx;
	daos_iod_t	iod;
	d_iov_t	sg_iov;
	d_sg_list_t	sgl;
	daos_event_t	ev;
    char keys[];
} ioreq;

extern jclass JC_String;
extern jclass JC_Exception;

#define STR_BUFFER_LEN 128

static char ERROR_MESSAGE[80];

// used for error message in exception thrown in C
#define THROW_EXCEPTION(fmt, ...)                       \
do {                                                    \
    sprintf(ERROR_MESSAGE, fmt, ## __VA_ARGS__);        \
    (*env)->ThrowNew(env, JC_Exception, ERROR_MESSAGE); \
} while(0)

/**
 * Convert system errno to DER_* variant. Default error code for any non-defined
 * system errnos is DER_MISC (miscellaneous error).
 *
 * \param[in] err	System error code
 *
 * \return		Corresponding DER_* error code
 */
static inline int
daos_errno2der(int err)
{
	if(err < 0 ) return err;
	switch (err) {
	case 0:			return -DER_SUCCESS;
	case EPERM:
	case EACCES:		return -DER_NO_PERM;
	case ENOMEM:		return -DER_NOMEM;
	case EDQUOT:
	case ENOSPC:		return -DER_NOSPACE;
	case EEXIST:		return -DER_EXIST;
	case ENOENT:		return -DER_NONEXIST;
	case ECANCELED:		return -DER_CANCELED;
	case EBUSY:		return -DER_BUSY;
	case EOVERFLOW:		return -DER_OVERFLOW;
	case EBADF:		return -DER_NO_HDL;
	case ENOSYS:		return -DER_NOSYS;
	case ETIMEDOUT:		return -DER_TIMEDOUT;
	case EWOULDBLOCK:	return -DER_AGAIN;
	case EPROTO:		return -DER_PROTO;
	case EINVAL:		return -DER_INVAL;
	case ENOTDIR:		return -DER_NOTDIR;
	case EFAULT:
	case ENXIO:
	case ENODEV:
	default:		return -DER_MISC;
	}
}

/**
 * Convert DER_ errno to system variant. Default error code for any non-defined
 * DER_ errnos is EIO (Input/Output error).
 *
 * \param[in] err	DER_ error code
 *
 * \return		Corresponding system error code
 */
static inline int
daos_der2errno(int err)
{
	if(err > 0 ) return err;
	switch (err) {
	case -DER_SUCCESS:	return 0;
	case -DER_NO_PERM:
	case -DER_EP_RO:
	case -DER_EP_OLD:	return EPERM;
	case -DER_ENOENT:
	case -DER_NONEXIST:	return ENOENT;
	case -DER_INVAL:
	case -DER_NOTYPE:
	case -DER_NOSCHEMA:
	case -DER_NOLOCAL:
	case -DER_NO_HDL:
	case -DER_IO_INVAL:	return EINVAL;
	case -DER_KEY2BIG:
	case -DER_REC2BIG:	return E2BIG;
	case -DER_EXIST:	return EEXIST;
	case -DER_UNREACH:	return EHOSTUNREACH;
	case -DER_NOSPACE:	return ENOSPC;
	case -DER_ALREADY:	return EALREADY;
	case -DER_NOMEM:	return ENOMEM;
	case -DER_TIMEDOUT:	return ETIMEDOUT;
	case -DER_BUSY:
	case -DER_EQ_BUSY:	return EBUSY;
	case -DER_AGAIN:	return EAGAIN;
	case -DER_PROTO:	return EPROTO;
	case -DER_IO:		return EIO;
	case -DER_CANCELED:	return ECANCELED;
	case -DER_OVERFLOW:	return EOVERFLOW;
	case -DER_BADPATH:
	case -DER_NOTDIR:	return ENOTDIR;
	case -DER_STALE:	return ESTALE;
	default:		return EIO;
	}
};