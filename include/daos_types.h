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

/** hash output of key */
typedef struct {
	uint64_t	body[2];
} daos_hash_out_t;

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
} daos_sg_iov_t;

/* Scatter/gather list for memory buffers */
typedef struct {
	unsigned long	 sg_num;
	daos_sg_iov_t	*sg_iovs;
} daos_sg_list_t;

/** extent for bype-array object */
typedef struct {
	daos_off_t	e_offset;
	daos_size_t	e_nob;
} daos_ext_t;

/** a list of object extents */
typedef struct {
	unsigned long	 el_num;
	daos_ext_t	*el_exts;
} daos_ext_list_t;

/** Descriptor of a key-value pair */
typedef struct {
	void		*kv_key;
	void		*kv_val;
	unsigned int	 kv_delete:1;
	unsigned int	 kv_key_len:30;
	unsigned int	 kv_val_len;
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

/** Type of storage target */
typedef enum {
	DAOS_TP_UNKNOWN,
	DAOS_TP_HDD,
	DAOS_TP_SSD,
	DAOS_TP_NVM,
} daos_target_type_t;

typedef enum {
	DAOS_TS_UNKNOWN,
	DAOS_TS_UP,
	DAOS_TS_DOWN,
	/* TODO: add more states? */
} daos_target_state_t;

typedef struct {
	/* TODO: storage/network bandwidth, latency etc */
} daos_target_perf_t;

#endif /* DAOS_TYPES_H */
