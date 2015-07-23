/************************************************************************
 * DAOS Error numbers
 *
 * Author: Liang Zhen <liang.zhen@intel.com>
 *
 * Version 0.1
 */
#ifndef __DAOS_ERRNO_H__
#define __DAOS_ERRNO_H__

typedef enum {
	DER_ERR_BASE		= 2000,
	/** no permission */
	DER_NO_PERM		= (DER_ERR_BASE + 1),
	/** invalid handle */
	DER_NO_HDL		= (DER_ERR_BASE + 2),
	/** invalid parameters */
	DER_INVAL		= (DER_ERR_BASE + 3),
	/** container exisited */
	DER_EXIST		= (DER_ERR_BASE + 4),
	/** nonexistent container, shard, or target */
	DER_NONEXIST		= (DER_ERR_BASE + 5),
	/** unreachable node */
	DER_UNREACH		= (DER_ERR_BASE + 6),
	/** no space on storage target */
	DER_NOSPACE		= (DER_ERR_BASE + 7),
	/** unknown object type */
	DER_NOTYPE		= (DER_ERR_BASE + 8),
	/** unknown object schema */
	DER_NOSCHEMA		= (DER_ERR_BASE + 9),
	/** epoch is read-only */
	DER_EP_RO		= (DER_ERR_BASE + 200),
	/** epoch is too old, all data have been recycled */
	DER_EP_OLD		= (DER_ERR_BASE + 201),
	/** key is too large */
	DER_KV_K2BIG		= (DER_ERR_BASE + 250),
	/** value is too large */
	DER_KV_V2BIG		= (DER_ERR_BASE + 251),
	/** IO buffers can't match object extents */
	DER_IO_INVAL		= (DER_ERR_BASE + 300),
	/** event queue is busy */
	DER_EQ_BUSY		= (DER_ERR_BASE + 400),
	/** domain of cluster component can't match */
	DER_DOMAIN		= (DER_ERR_BASE + 500),
	/** TODO: add more error numbers */
} daos_errno_t;

const char *daos_errstr(daos_errno_t errno);

#endif /*  __DAOS_ERRNO_H__ */
