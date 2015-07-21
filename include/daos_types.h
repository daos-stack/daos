/*
 * DAOS Types and Functions Common to Layers/Components
 */

#ifndef DAOS_TYPES_H
#define DAOS_TYPES_H

#include <stdint.h>

/* uuid_t */
#include <uuid/uuid.h>

/* Event and event queue */
/* ... */

/* Offset */
typedef uint64_t	daos_off_t;

/* Size */
typedef uint64_t	daos_size_t;

/* Handle for container, object, etc. */
typedef struct {
	uint64_t	cookie;
} daos_handle_t;

/* Epoch */
typedef uint64_t	daos_epoch_t;

/* Address of a process in a session */
typedef uint32_t	daos_rank_t;

/* ID of an object */
typedef struct {
	uint64_t	body[2];
} daos_obj_id_t;

/* iovec for memory buffer */
typedef struct {
	daos_size_t	iov_len;
	void	       *iov_addr;
} daos_iov_t;

/* Scatter/gather list for memory buffers */
typedef struct {
	unsigned long	sg_num;
	daos_iov_t     *sg_iovs;
} daos_sg_list_t;

/* iovec for object data */
typedef struct {
	daos_size_t	iov_nob;
	daos_off_t	iov_offset;
} daos_obj_iov_t;

/* Scatter/gather list to describe object data */
typedef struct {
	unsigned long	osg_num;
	daos_iov_t     *osg_iovs;
} daos_obj_sg_list_t;

/* Descriptor of a key-value pair */
typedef struct {
	void	       *kv_key;
	void	       *kv_va;
	unsigned int	kv_key_len;
	unsigned int	kv_va_len;
} daos_kv_t;

/*
 * One way to understand this: An array of "session network addresses", each of
 * which consists of a "UUID" part shares with all others, identifying the
 * session, and a "rank" part, uniquely identifies a process within this
 * session.
 */
typedef struct {
	uuid_t		rg_uuid;
	uint32_t	rg_nranks;
	daos_rank_t    *rg_ranks;
} daos_rank_group_t;

#endif /* DAOS_TYPES_H */
