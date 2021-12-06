/**
 * (C) Copyright 2015-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __DAOS_COMMON_H__
#define __DAOS_COMMON_H__

#include <sys/time.h>
#include <sys/types.h>
#include <sys/param.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <byteswap.h>

#include <daos_errno.h>
#include <daos/debug.h>
#include <gurt/hash.h>
#include <gurt/common.h>
#include <cart/api.h>
#include <daos_types.h>
#include <daos_obj.h>
#include <daos_prop.h>
#include <daos_security.h>
#include <daos/profile.h>
#include <daos/dtx.h>
#include <daos/cmd_parser.h>

#define DAOS_ON_VALGRIND D_ON_VALGRIND

#define DF_OID		DF_U64"."DF_U64
#define DP_OID(o)	(o).hi, (o).lo

#define DF_UOID		DF_OID".%u"
#define DP_UOID(uo)	DP_OID((uo).id_pub), (uo).id_shard
#define DF_BOOL "%s"
#define DP_BOOL(b) ((b) ? "true" : "false")
#define DF_IOV "<%p, %zu/%zu>"
#define DP_IOV(i) (i)->iov_buf, (i)->iov_len, (i)->iov_buf_len
#define MAX_TREE_ORDER_INC	7

struct daos_node_overhead {
	/** Node size in bytes for tree with only */
	int	no_size;
	/** Order of node */
	int	no_order;
};

/** Overheads for a tree */
struct daos_tree_overhead {
	/** Overhead for full size of leaf tree node */
	struct daos_node_overhead	to_leaf_overhead;
	/** Overhead for full size intermediate tree node */
	int				to_int_node_size;
	/** Overhead for dynamic tree nodes */
	struct daos_node_overhead	to_dyn_overhead[MAX_TREE_ORDER_INC];
	/** Number of dynamic tree node sizes */
	int				to_dyn_count;
	/** Inline metadata size for each record */
	int				to_node_rec_msize;
	/** Dynamic metadata size of an allocated record. */
	int				to_record_msize;
};

/** Points to a byte in an iov, in an sgl */
struct daos_sgl_idx {
	uint32_t	iov_idx; /** index of iov */
	daos_off_t	iov_offset; /** byte offset of iov buf */
};

#define DF_SGL_IDX "{idx: %d, offset: "DF_U64"}"
#define DP_SGL_IDX(i) (i)->iov_idx, (i)->iov_offset

/*
 * add bytes to the sgl index offset. If the new offset is greater than or
 * equal to the indexed iov len, move the index to the next iov in the sgl.
 */
static inline void
sgl_move_forward(d_sg_list_t *sgl, struct daos_sgl_idx *sgl_idx, uint64_t bytes)
{
	sgl_idx->iov_offset += bytes;
	D_DEBUG(DB_TRACE, "Moving sgl index forward by %lu bytes."
			  "Idx: "DF_SGL_IDX"\n",
		bytes, DP_SGL_IDX(sgl_idx));

	/** move to next iov if necessary */
	if (sgl_idx->iov_offset >= sgl->sg_iovs[sgl_idx->iov_idx].iov_buf_len) {
		sgl_idx->iov_idx++;
		sgl_idx->iov_offset = 0;
		D_DEBUG(DB_TRACE, "Moving to next iov in sgl\n");
	}
	D_DEBUG(DB_TRACE, "Idx: "DF_SGL_IDX"\n", DP_SGL_IDX(sgl_idx));
}

static inline void *
sgl_indexed_byte(d_sg_list_t *sgl, struct daos_sgl_idx *sgl_idx)
{
	D_DEBUG(DB_TRACE, "Idx: "DF_SGL_IDX"\n", DP_SGL_IDX(sgl_idx));
	if (sgl_idx->iov_idx > sgl->sg_nr_out - 1) {
		D_DEBUG(DB_TRACE, "Index too high. Returning NULL\n");
		return NULL;
	}
	return sgl->sg_iovs[sgl_idx->iov_idx].iov_buf + sgl_idx->iov_offset;
}

/*
 * If the byte count will exceed the current indexed iov, then move to
 * the next.
 */
static inline void
sgl_test_forward(d_sg_list_t *sgl, struct daos_sgl_idx *sgl_idx, uint64_t bytes)
{
	D_DEBUG(DB_TRACE, "Before Idx: "DF_SGL_IDX"\n", DP_SGL_IDX(sgl_idx));
	if (sgl_idx->iov_offset + bytes >
	    sgl->sg_iovs[sgl_idx->iov_idx].iov_len) {
		sgl_idx->iov_idx++;
		sgl_idx->iov_offset = 0;
	}
	D_DEBUG(DB_TRACE, "After Idx: "DF_SGL_IDX"\n", DP_SGL_IDX(sgl_idx));
}

/*
 * Each thread has DF_UUID_MAX number of thread-local buffers for UUID strings.
 * Each debug message can have at most this many DP_UUIDs.
 *
 * DF_UUID prints the first eight characters of the string representation,
 * while DF_UUIDF prints the full 36-character string representation. DP_UUID()
 * matches both DF_UUID and DF_UUIDF.
 */
#define DF_UUID_MAX	8
#define DF_UUID		"%.8s"
#define DF_UUIDF	"%s"
char *DP_UUID(const void *uuid);

/* For prefixes of error messages about a container */
#define DF_CONT			DF_UUID"/"DF_UUID
#define DP_CONT(puuid, cuuid)	DP_UUID(puuid), DP_UUID(cuuid)
#define DF_CONTF		DF_UUIDF"/"DF_UUIDF

#ifdef DAOS_BUILD_RELEASE
#define DF_KEY			"[%d]"
#define DP_KEY(key)		(int)((key)->iov_len)
#else
char *daos_key2str(daos_key_t *key);

#define DF_KEY			"[%d] '%.*s'"
#define DP_KEY(key)		(int)(key)->iov_len,	\
				(int)(key)->iov_len,	\
				daos_key2str(key)
#endif

#define DF_RECX			"["DF_X64"-"DF_X64"]"
#define DP_RECX(r)		(r).rx_idx, ((r).rx_idx + (r).rx_nr - 1)
#define DF_IOM			"{nr: %d, lo: "DF_RECX", hi: "DF_RECX"}"
#define DP_IOM(m)		(m)->iom_nr, DP_RECX((m)->iom_recx_lo), \
				DP_RECX((m)->iom_recx_hi)

static inline uint64_t
daos_u64_hash(uint64_t val, unsigned int bits)
{
	uint64_t hash = val;

	hash *= DGOLDEN_RATIO_PRIME_64;
	return hash >> (64 - bits);
}

static inline uint32_t
daos_u32_hash(uint64_t key, unsigned int bits)
{
	return (DGOLDEN_RATIO_PRIME_32 * key) >> (32 - bits);
}

#define LOWEST_BIT_SET(x)       ((x) & ~((x) - 1))

static inline uint8_t
isset_range(uint8_t *bitmap, uint32_t start, uint32_t end)
{
	uint32_t index;

	for (index = start; index <= end; ++index)
		if (isclr(bitmap, index))
			return 0;

	return 1;
}

static inline void
clrbit_range(uint8_t *bitmap, uint32_t start, uint32_t end)
{
	uint32_t index;

	for (index = start; index <= end; ++index)
		clrbit(bitmap, index);
}

static inline unsigned int
daos_power2_nbits(unsigned int val)
{
	unsigned int shift;

	for (shift = 1; (val >> shift) != 0; shift++);

	return val == LOWEST_BIT_SET(val) ? shift - 1 : shift;
}

static inline bool
daos_uuid_valid(const uuid_t uuid)
{
	return uuid && !uuid_is_null(uuid);
}

static inline bool
daos_rank_list_valid(const d_rank_list_t *rl)
{
	return rl && rl->rl_ranks && rl->rl_nr;
}

static inline uint64_t
daos_get_ntime(void)
{
	struct timespec	tv;

	d_gettime(&tv);
	return ((uint64_t)tv.tv_sec * NSEC_PER_SEC + tv.tv_nsec);
}

static inline uint64_t
daos_getntime_coarse(void)
{
	struct timespec	tv;

	clock_gettime(CLOCK_MONOTONIC_COARSE, &tv);
	return ((uint64_t)tv.tv_sec * NSEC_PER_SEC + tv.tv_nsec);
}

static inline uint64_t
daos_wallclock_secs(void)
{
	struct timespec         now;
	int                     rc;

	rc = clock_gettime(CLOCK_REALTIME, &now);
	if (rc) {
		D_ERROR("clock_gettime failed, rc: %d, errno %d(%s).\n",
			rc, errno, strerror(errno));
		return 0;
	}

	return now.tv_sec;
}

static inline uint64_t
daos_getmtime_coarse(void)
{
	struct timespec tv;

	clock_gettime(CLOCK_MONOTONIC_COARSE, &tv);
	return ((uint64_t)tv.tv_sec * 1000 + tv.tv_nsec / NSEC_PER_MSEC);
}

static inline uint64_t
daos_getutime(void)
{
	struct timespec tv;

	d_gettime(&tv);
	return d_time2us(tv);
}

static inline int daos_gettime_coarse(uint64_t *time)
{
	struct timespec	now;
	int		rc;

	rc = clock_gettime(CLOCK_MONOTONIC_COARSE, &now);
	if (rc == 0)
		*time = now.tv_sec;

	return rc;
}

/** Function table for combsort and binary search */
typedef struct {
	void    (*so_swap)(void *array, int a, int b);
	/**
	 * For ascending order:
	 * 0	array[a] == array[b]
	 * 1	array[a] > array[b]
	 * -1	array[a] < array[b]
	 */
	int     (*so_cmp)(void *array, int a, int b);
	/** for binary search, returned value is the same as so_cmp() */
	int	(*so_cmp_key)(void *array, int i, uint64_t key);
} daos_sort_ops_t;

int daos_array_sort(void *array, unsigned int len, bool unique,
		    daos_sort_ops_t *ops);
int daos_array_find(void *array, unsigned int len, uint64_t key,
		    daos_sort_ops_t *ops);
int daos_array_find_le(void *array, unsigned int len, uint64_t key,
		       daos_sort_ops_t *ops);
int daos_array_find_ge(void *array, unsigned int len, uint64_t key,
		       daos_sort_ops_t *ops);

void daos_array_shuffle(void *arr, unsigned int len, daos_sort_ops_t *ops);

int daos_sgls_copy_ptr(d_sg_list_t *dst, int dst_nr, d_sg_list_t *src,
		       int src_nr);
int daos_sgls_copy_data_out(d_sg_list_t *dst, int dst_nr, d_sg_list_t *src,
			    int src_nr);
int daos_sgls_copy_all(d_sg_list_t *dst, int dst_nr, d_sg_list_t *src,
		       int src_nr);
int daos_sgl_copy_data_out(d_sg_list_t *dst, d_sg_list_t *src);
int daos_sgl_copy_data(d_sg_list_t *dst, d_sg_list_t *src);
int daos_sgl_alloc_copy_data(d_sg_list_t *dst, d_sg_list_t *src);
int daos_sgls_alloc(d_sg_list_t *dst, d_sg_list_t *src, int nr);
int daos_sgl_merge(d_sg_list_t *dst, d_sg_list_t *src);
daos_size_t daos_sgl_data_len(d_sg_list_t *sgl);
daos_size_t daos_sgl_buf_size(d_sg_list_t *sgl);
daos_size_t daos_sgls_buf_size(d_sg_list_t *sgls, int nr);
daos_size_t daos_sgls_packed_size(d_sg_list_t *sgls, int nr,
				  daos_size_t *buf_size);
int
daos_sgl_buf_extend(d_sg_list_t *sgl, int idx, size_t new_size);

/** Move to next iov, it's caller's responsibility to ensure the idx boundary */
#define daos_sgl_next_iov(iov_idx, iov_off)				\
	do {								\
		(iov_idx)++;						\
		(iov_off) = 0;						\
	} while (0)
/** Get the leftover space in an iov of sgl */
#define daos_iov_left(sgl, iov_idx, iov_off)				\
	((sgl)->sg_iovs[iov_idx].iov_len - (iov_off))
/** get remaining space in an iov, assuming that iov_len is used and
 * iov_buf_len is total in buf
 */
#define daos_iov_remaining(iov) ((iov).iov_buf_len > (iov).iov_len ? \
				(iov).iov_buf_len - (iov).iov_len : 0)
/**
 * Move sgl forward from iov_idx/iov_off, with move_dist distance. It is
 * caller's responsibility to check the boundary.
 */
#define daos_sgl_move(sgl, iov_idx, iov_off, move_dist)			       \
	do {								       \
		uint64_t moved = 0, step, iov_left;			       \
		if ((move_dist) <= 0)					       \
			break;						       \
		while (moved < (move_dist)) {				       \
			iov_left = daos_iov_left(sgl, iov_idx, iov_off);       \
			step = MIN(iov_left, (move_dist) - moved);	       \
			(iov_off) += step;				       \
			moved += step;					       \
			if (daos_iov_left(sgl, iov_idx, iov_off) == 0)	       \
				daos_sgl_next_iov(iov_idx, iov_off);	       \
		}							       \
		D_ASSERT(moved == (move_dist));				       \
	} while (0)

/**
 * Consume buffer of length\a size for \a sgl with \a iov_idx and \a iov_off.
 * The consumed buffer location will be returned by \a iovs and \a iov_nr.
 */
#define daos_sgl_consume(sgl, iov_idx, iov_off, size, iovs, iov_nr)	       \
	do {								       \
		uint64_t consumed = 0, step, iov_left;			       \
		uint32_t consume_idx = 0;				       \
		if ((size) <= 0)					       \
			break;						       \
		while (consumed < (size)) {				       \
			iov_left = daos_iov_left(sgl, iov_idx, iov_off);       \
			step = MIN(iov_left, (size) - consumed);	       \
			iovs[consume_idx].iov_buf =			       \
				(sgl)->sg_iovs[iov_idx].iov_buf + (iov_off);   \
			iovs[consume_idx].iov_len = step;		       \
			iovs[consume_idx].iov_buf_len = step;		       \
			consume_idx++;					       \
			(iov_off) += step;				       \
			consumed += step;				       \
			if (daos_iov_left(sgl, iov_idx, iov_off) == 0)	       \
				daos_sgl_next_iov(iov_idx, iov_off);	       \
		}							       \
		(iov_nr) = consume_idx;					       \
		D_ASSERT(consumed == (size));				       \
	} while (0)

#ifndef roundup
#define roundup(x, y)		((((x) + ((y) - 1)) / (y)) * (y))
#endif
#ifndef rounddown
#define rounddown(x, y)		(((x) / (y)) * (y))
#endif

/**
 * Request a buffer of length \a bytes_needed from the sgl starting at
 * index \a idx. The length of the resulting buffer will be the number
 * of requested bytes if available in the indexed I/O vector or the max bytes
 * that can be taken from the indexed I/O vector. The index will be incremented
 * to point to just after the buffer returned. If the end of an I/O vector is
 * reached then the index will point to the beginning of the next. It is
 * possible for the index reach past the SGL. In this case the function will
 * return true, meaning the end was reached.
 *
 * @param[in]		sgl		sgl to be read from
 * @param[in]		check_buf	if true process on the sgl buf len
					instead of iov_len
 * @param[in/out]	idx		index into the sgl to start reading from
 * @param[in]		buf_len_req	number of bytes requested
 * @param[out]		p_buf		resulting pointer to buffer
 * @param[out]		p_buf_len	length of buffer
 *
 * @return		true if end of SGL was reached
 */
bool daos_sgl_get_bytes(d_sg_list_t *sgl, bool check_buf,
			struct daos_sgl_idx *idx, size_t buf_len_req,
			uint8_t **p_buf, size_t *p_buf_len);

typedef int (*daos_sgl_process_cb)(uint8_t *buf, size_t len, void *args);
/**
 * Process bytes of an SGL. The process callback will be called for
 * each contiguous set of bytes provided in the SGL's I/O vectors.
 *
 * @param sgl		sgl to process
 * @param check_buf	if true process on the sgl buf len instead of iov_len
 * @param idx		index to keep track of what's been processed
 * @param requested_bytes		number of bytes to process
 * @param process_cb	callback function for the processing
 * @param cb_args	arguments for the callback function
 *
 * @return		Result of the callback function.
 *			Expectation is 0 is success.
 */
int daos_sgl_processor(d_sg_list_t *sgl, bool check_buf,
		       struct daos_sgl_idx *idx, size_t requested_bytes,
		       daos_sgl_process_cb process_cb, void *cb_args);

char *daos_str_trimwhite(char *str);
int daos_iov_copy(d_iov_t *dst, d_iov_t *src);
int daos_iov_alloc(d_iov_t *iov, daos_size_t size, bool set_full);
void daos_iov_free(d_iov_t *iov);
bool daos_iov_cmp(d_iov_t *iov1, d_iov_t *iov2);
void daos_iov_append(d_iov_t *iov, void *buf, uint64_t buf_len);

#define daos_key_match(key1, key2)	daos_iov_cmp(key1, key2)

#if !defined(container_of)
/* given a pointer @ptr to the field @member embedded into type (usually
 *  * struct) @type, return pointer to the embedding instance of @type.
 */
# define container_of(ptr, type, member)		\
	 ((type *)((char *)(ptr) - (char *)(&((type *)0)->member)))
#endif

#ifndef offsetof
# define offsetof(typ, memb)	((long)((char *)&(((typ *)0)->memb)))
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

#ifndef MIN
# define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
# define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(x, y) ((x) < (y) ? (x) : (y))
#endif

#ifndef max
#define max(x, y) ((x) > (y) ? (x) : (y))
#endif

#ifndef min_t
#define min_t(type, x, y) \
	     ({ type __x = (x); type __y = (y); __x < __y ? __x : __y; })
#endif
#ifndef max_t
#define max_t(type, x, y) \
	     ({ type __x = (x); type __y = (y); __x > __y ? __x : __y; })
#endif

#define DAOS_UUID_STR_SIZE 37	/* 36 + 1 for '\0' */

/* byte swapper */
#define D_SWAP16(x)	bswap_16(x)
#define D_SWAP32(x)	bswap_32(x)
#define D_SWAP64(x)	bswap_64(x)
#define D_SWAP16S(x)	do { *(x) = D_SWAP16(*(x)); } while (0)
#define D_SWAP32S(x)	do { *(x) = D_SWAP32(*(x)); } while (0)
#define D_SWAP64S(x)	do { *(x) = D_SWAP64(*(x)); } while (0)

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
	if (err < 0) {
		D_ERROR("error < 0 (%d)\n", err);
		return -DER_UNKNOWN;
	}

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
	case EIO:		return -DER_IO;
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
	if (err > 0) {
		D_ERROR("error > 0 (%d)\n", err);
		return EINVAL;
	}

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
	case -DER_TX_RESTART:	return ERESTART;
	default:		return EIO;
	}
};

static inline bool
daos_crt_network_error(int err)
{
	return err == -DER_HG || err == -DER_ADDRSTR_GEN ||
	       err == -DER_PMIX || err == -DER_UNREG ||
	       err == -DER_UNREACH || err == -DER_CANCELED ||
	       err == -DER_NOREPLY || err == -DER_OOG;
}

/** See crt_quiet_error. */
static inline bool
daos_quiet_error(int err)
{
	return crt_quiet_error(err);
}

#define daos_rank_list_dup		d_rank_list_dup
#define daos_rank_list_dup_sort_uniq	d_rank_list_dup_sort_uniq
#define daos_rank_list_filter		d_rank_list_filter
#define daos_rank_list_alloc		d_rank_list_alloc
#define daos_rank_list_copy		d_rank_list_copy
#define daos_rank_list_shuffle		d_rank_list_shuffle
#define daos_rank_list_sort		d_rank_list_sort
#define daos_rank_list_find		d_rank_list_find
#define daos_rank_list_identical	d_rank_list_identical
#define daos_rank_in_rank_list		d_rank_in_rank_list
#define daos_rank_list_append		d_rank_list_append

void
daos_fail_loc_set(uint64_t id);
void
daos_fail_loc_reset(void);
void
daos_fail_value_set(uint64_t val);
void
daos_fail_num_set(uint64_t num);
uint64_t
daos_shard_fail_value(uint16_t *shards, int nr);
bool
daos_shard_in_fail_value(uint16_t shard);
int
daos_fail_check(uint64_t id);

uint64_t
daos_fail_value_get(void);

int
daos_fail_init(void);
void
daos_fail_fini(void);

/**
 * DAOS FAIL Mask
 *
 * fail loc 0-24
 *      [0-16] fail id
 *      [16-24] fail group id used to index in cart injection array.
 * fail mode 24-32
 * unused 32-64
 **/

#define DAOS_FAIL_MASK_LOC	(0x000000ffff)

/* fail mode */
#define DAOS_FAIL_ONCE		0x1000000
#define DAOS_FAIL_SOME		0x2000000
#define DAOS_FAIL_ALWAYS	0x4000000

#define DAOS_FAIL_ID_MASK    0xffffff
#define DAOS_FAIL_GROUP_MASK 0xff0000
#define DAOS_FAIL_GROUP_SHIFT 16

enum {
	DAOS_FAIL_UNIT_TEST_GROUP = 1,
	DAOS_FAIL_SYS_TEST_GROUP = 2,
	DAOS_FAIL_MAX_GROUP
};

#define DAOS_FAIL_ID_GET(fail_loc)	(fail_loc & DAOS_FAIL_ID_MASK)

#define DAOS_FAIL_UNIT_TEST_GROUP_LOC	\
		(DAOS_FAIL_UNIT_TEST_GROUP << DAOS_FAIL_GROUP_SHIFT)

#define DAOS_FAIL_SYS_TEST_GROUP_LOC \
		(DAOS_FAIL_SYS_TEST_GROUP << DAOS_FAIL_GROUP_SHIFT)

#define DAOS_FAIL_GROUP_GET(fail_loc)	\
		((fail_loc & DAOS_FAIL_GROUP_MASK) >> DAOS_FAIL_GROUP_SHIFT)

#define DAOS_SHARD_OBJ_UPDATE_TIMEOUT	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x01)
#define DAOS_SHARD_OBJ_FETCH_TIMEOUT	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x02)
#define DAOS_SHARD_OBJ_FAIL		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x03)
#define DAOS_OBJ_UPDATE_NOSPACE		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x04)
#define DAOS_SHARD_OBJ_RW_CRT_ERROR	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x05)
#define DAOS_OBJ_REQ_CREATE_TIMEOUT	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x06)
#define DAOS_SHARD_OBJ_UPDATE_TIMEOUT_SINGLE	\
					(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x07)
#define DAOS_OBJ_SPECIAL_SHARD		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x08)
#define DAOS_OBJ_TGT_IDX_CHANGE		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x09)

#define DAOS_REBUILD_DROP_SCAN	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x0a)
#define DAOS_REBUILD_NO_HDL	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x0b)
#define DAOS_REBUILD_DROP_OBJ	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x0c)
#define DAOS_REBUILD_UPDATE_FAIL (DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x0d)
#define DAOS_REBUILD_STALE_POOL (DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x0e)
#define DAOS_REBUILD_TGT_IV_UPDATE_FAIL (DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x0f)
#define DAOS_REBUILD_TGT_START_FAIL (DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x10)
#define DAOS_REBUILD_DISABLE	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x11)
#define DAOS_REBUILD_TGT_SCAN_HANG (DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x12)
#define DAOS_REBUILD_TGT_REBUILD_HANG (DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x13)
#define DAOS_REBUILD_HANG (DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x14)
#define DAOS_REBUILD_TGT_SEND_OBJS_FAIL (DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x15)
#define DAOS_REBUILD_NO_REBUILD (DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x16)
#define DAOS_REBUILD_NO_UPDATE (DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x17)
#define DAOS_REBUILD_TGT_NOSPACE (DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x18)
#define DAOS_REBUILD_DELAY	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x19)

#define DAOS_RDB_SKIP_APPENDENTRIES_FAIL (DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x1a)
#define DAOS_FORCE_REFRESH_POOL_MAP	  (DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x1b)

#define DAOS_FORCE_CAPA_FETCH		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x1e)
#define DAOS_FORCE_PROP_VERIFY		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x1f)

/** These faults simulate corruption over the network. Can be set on the client
 * or server side.
 */
#define DAOS_CSUM_CORRUPT_UPDATE	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x20)
#define DAOS_CSUM_CORRUPT_FETCH		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x21)
#define DAOS_CSUM_CORRUPT_UPDATE_AKEY	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x22)
#define DAOS_CSUM_CORRUPT_FETCH_AKEY	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x23)
#define DAOS_CSUM_CORRUPT_UPDATE_DKEY	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x24)
#define DAOS_CSUM_CORRUPT_FETCH_DKEY	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x25)

/** This fault simulates corruption on disk. Must be set on server side. */
#define DAOS_CSUM_CORRUPT_DISK		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x26)
/**
 * This fault simulates shard open failure. Can be used to test EC degraded
 * update/fetch.
 */
#define DAOS_FAIL_SHARD_OPEN		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x27)
/**
 * This fault simulates the EC aggregation boundary (agg_eph_boundry) moved
 * ahead, in that case need to redo the degraded fetch.
 */
#define DAOS_FAIL_AGG_BOUNDRY_MOVED	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x28)
/**
 * This fault simulates the EC parity epoch difference in EC data recovery.
 */
#define DAOS_FAIL_PARITY_EPOCH_DIFF	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x29)
#define DAOS_FAIL_SHARD_NONEXIST	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x2a)

#define DAOS_DTX_COMMIT_SYNC		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x30)
#define DAOS_DTX_LEADER_ERROR		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x31)
#define DAOS_DTX_NONLEADER_ERROR	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x32)
#define DAOS_DTX_LOST_RPC_REQUEST	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x33)
#define DAOS_DTX_LOST_RPC_REPLY		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x34)
#define DAOS_DTX_LONG_TIME_RESEND	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x35)
#define DAOS_DTX_RESTART		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x36)
#define DAOS_DTX_NO_READ_TS		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x37)
#define DAOS_DTX_SPEC_EPOCH		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x38)
#define DAOS_DTX_STALE_PM		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x39)
#define DAOS_DTX_FAIL_IO		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x3a)
#define DAOS_DTX_START_EPOCH		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x3b)
#define DAOS_DTX_NO_BATCHED_CMT		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x3d)
#define DAOS_DTX_NO_COMMITTABLE		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x3e)
#define DAOS_DTX_MISS_COMMIT		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x3f)

#define DAOS_VC_DIFF_REC		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x40)
#define DAOS_VC_DIFF_DKEY		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x41)
#define DAOS_VC_LOST_DATA		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x42)
#define DAOS_VC_LOST_REPLICA		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x43)

#define DAOS_DTX_MISS_ABORT		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x44)
#define DAOS_DTX_SPEC_LEADER		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x45)
#define DAOS_DTX_SRV_RESTART		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x46)
#define DAOS_DTX_NO_RETRY		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x47)
#define DAOS_DTX_RESEND_DELAY1		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x48)
#define DAOS_DTX_UNCERTAIN		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x49)

#define DAOS_NVME_FAULTY		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x50)
#define DAOS_NVME_WRITE_ERR		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x51)
#define DAOS_NVME_READ_ERR		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x52)

#define DAOS_POOL_CREATE_FAIL_CORPC	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x60)
#define DAOS_POOL_DESTROY_FAIL_CORPC	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x61)
#define DAOS_POOL_CONNECT_FAIL_CORPC	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x62)
#define DAOS_POOL_DISCONNECT_FAIL_CORPC	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x63)
#define DAOS_POOL_QUERY_FAIL_CORPC	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x64)
#define DAOS_CONT_DESTROY_FAIL_CORPC	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x65)
#define DAOS_CONT_CLOSE_FAIL_CORPC	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x66)
#define DAOS_CONT_QUERY_FAIL_CORPC	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x67)
#define DAOS_CONT_OPEN_FAIL		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x68)
#define DAOS_POOL_FAIL_MAP_REFRESH	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x69)

/** interoperability failure inject */
#define FLC_SMD_DF_VER			(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x70)
#define FLC_POOL_DF_VER			(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x71)

#define DAOS_FAIL_LOST_REQ		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x72)

#define DAOS_SHARD_OBJ_RW_DROP_REPLY (DAOS_FAIL_SYS_TEST_GROUP_LOC | 0x80)
#define DAOS_OBJ_FETCH_DATA_LOST	(DAOS_FAIL_SYS_TEST_GROUP_LOC | 0x81)
#define DAOS_OBJ_TRY_SPECIAL_SHARD	(DAOS_FAIL_SYS_TEST_GROUP_LOC | 0x82)

#define DAOS_VOS_AGG_RANDOM_YIELD	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x90)
#define DAOS_VOS_AGG_MW_THRESH		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x91)
#define DAOS_VOS_NON_LEADER		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x92)
#define DAOS_VOS_AGG_BLOCKED		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x93)

#define DAOS_VOS_GC_CONT		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x94)
#define DAOS_VOS_GC_CONT_NULL		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x95)

#define DAOS_OBJ_SKIP_PARITY		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x96)
#define DAOS_OBJ_FORCE_DEGRADE		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x97)
#define DAOS_FORCE_EC_AGG		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x98)
#define DAOS_FORCE_EC_AGG_FAIL		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x99)
#define DAOS_FORCE_EC_AGG_PEER_FAIL	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x9a)

#define DAOS_DTX_SKIP_PREPARE		DAOS_DTX_SPEC_LEADER

#define DAOS_FAIL_CHECK(id) daos_fail_check(id)

static inline int __is_po2(unsigned long long val)
{
	return !(val & (val - 1));
}

#define IS_PO2(val)	__is_po2((unsigned long long)(val))

bool daos_file_is_dax(const char *pathname);

/* daos handle hash table */
struct daos_hhash_table {
	struct d_hhash  *dht_hhash;
};

extern struct daos_hhash_table daos_ht;

/* daos handle hash table helpers */
int daos_hhash_init(void);
int daos_hhash_fini(void);
struct d_hlink *daos_hhash_link_lookup(uint64_t key);
void daos_hhash_link_insert(struct d_hlink *hlink, int type);
void daos_hhash_link_getref(struct d_hlink *hlink);
void daos_hhash_link_putref(struct d_hlink *hlink);
bool daos_hhash_link_delete(struct d_hlink *hlink);
#define daos_hhash_hlink_init(hlink, ops)	d_hhash_hlink_init(hlink, ops)
#define daos_hhash_link_empty(hlink)		d_hhash_link_empty(hlink)
#define daos_hhash_link_key(hlink, key)		d_hhash_link_key(hlink, key)

/* daos_recx_t overlap detector */
#define DAOS_RECX_OVERLAP(recx_1, recx_2)				\
	(((recx_1).rx_idx < (recx_2).rx_idx + (recx_2).rx_nr) &&	\
	 ((recx_2).rx_idx < (recx_1).rx_idx + (recx_1).rx_nr))
#define DAOS_RECX_PTR_OVERLAP(recx_1, recx_2)				\
	(((recx_1)->rx_idx < (recx_2)->rx_idx + (recx_2)->rx_nr) &&	\
	 ((recx_2)->rx_idx < (recx_1)->rx_idx + (recx_1)->rx_nr))

#define DAOS_RECX_ADJACENT(recx_1, recx_2)				\
	(((recx_1).rx_idx == (recx_2).rx_idx + (recx_2).rx_nr) ||	\
	 ((recx_2).rx_idx == (recx_1).rx_idx + (recx_1).rx_nr))
#define DAOS_RECX_PTR_ADJACENT(recx_1, recx_2)				\
	(((recx_1)->rx_idx == (recx_2)->rx_idx + (recx_2)->rx_nr) ||	\
	 ((recx_2)->rx_idx == (recx_1)->rx_idx + (recx_1)->rx_nr))

#define DAOS_RECX_END(recx)	((recx).rx_idx + (recx).rx_nr)
#define DAOS_RECX_PTR_END(recx)	((recx)->rx_idx + (recx)->rx_nr)

/**
 * Merge \a src recx to \a dst recx.
 */
static inline void
daos_recx_merge(daos_recx_t *src, daos_recx_t *dst)
{
	uint64_t	end;

	end = max(DAOS_RECX_PTR_END(src), DAOS_RECX_PTR_END(dst));
	dst->rx_idx = min(src->rx_idx, dst->rx_idx);
	dst->rx_nr = end - dst->rx_idx;
}

/* NVMe shared constants */
#define DAOS_NVME_SHMID_NONE	-1
#define DAOS_NVME_MEM_PRIMARY	0

/** Size of (un)expected Mercury buffers */
#define DAOS_RPC_SIZE  (20480) /* 20KiB */
/**
 * Threshold for inline vs bulk transfer
 * If the data size is smaller or equal to this limit, it will be transferred
 * inline in the request/reply. Otherwise, a RDMA transfer will be used.
 * Based on RPC size above and reserve 1KiB for RPC fields and cart/HG headers.
 */
#define DAOS_BULK_LIMIT	(DAOS_RPC_SIZE - 1024) /* Reserve 1KiB for headers */

crt_init_options_t *daos_crt_init_opt_get(bool server, int crt_nr);

int crt_proc_struct_dtx_id(crt_proc_t proc, crt_proc_op_t proc_op,
			   struct dtx_id *dti);
int crt_proc_daos_prop_t(crt_proc_t proc, crt_proc_op_t proc_op,
			 daos_prop_t **data);
int crt_proc_struct_daos_acl(crt_proc_t proc, crt_proc_op_t proc_op,
			     struct daos_acl **data);

bool daos_prop_valid(daos_prop_t *prop, bool pool, bool input);
daos_prop_t *daos_prop_dup(daos_prop_t *prop, bool pool, bool input);
int daos_prop_copy(daos_prop_t *prop_req, daos_prop_t *prop_reply);
void daos_prop_fini(daos_prop_t *prop);

int daos_prop_entry_copy(struct daos_prop_entry *entry,
			 struct daos_prop_entry *entry_dup);
daos_recx_t *daos_recx_alloc(uint32_t nr);
void daos_recx_free(daos_recx_t *recx);

static inline void
daos_parse_ctype(const char *string, daos_cont_layout_t *type)
{
	if (strcasecmp(string, "HDF5") == 0)
		*type = DAOS_PROP_CO_LAYOUT_HDF5;
	else if (strcasecmp(string, "POSIX") == 0)
		*type = DAOS_PROP_CO_LAYOUT_POSIX;
	else if (strcasecmp(string, "PYTHON") == 0)
		*type = DAOS_PROP_CO_LAYOUT_PYTHON;
	else if (strcasecmp(string, "SPARK") == 0)
		*type = DAOS_PROP_CO_LAYOUT_SPARK;
	else if (strcasecmp(string, "DATABASE") == 0 ||
		 strcasecmp(string, "DB") == 0)
		*type = DAOS_PROP_CO_LAYOUT_DATABASE;
	else if (strcasecmp(string, "ROOT") == 0 ||
		 strcasecmp(string, "RNTuple") == 0)
		*type = DAOS_PROP_CO_LAYOUT_ROOT;
	else if (strcasecmp(string, "SEISMIC") == 0 ||
		 strcasecmp(string, "DSG") == 0)
		*type = DAOS_PROP_CO_LAYOUT_SEISMIC;
	else if (strcasecmp(string, "METEO") == 0 ||
		 strcasecmp(string, "FDB") == 0)
		*type = DAOS_PROP_CO_LAYOUT_METEO;
	else
		*type = DAOS_PROP_CO_LAYOUT_UNKNOWN;
}

static inline void
daos_unparse_ctype(daos_cont_layout_t ctype, char *string)
{
	switch (ctype) {
	case DAOS_PROP_CO_LAYOUT_POSIX:
		strcpy(string, "POSIX");
		break;
	case DAOS_PROP_CO_LAYOUT_HDF5:
		strcpy(string, "HDF5");
		break;
	case DAOS_PROP_CO_LAYOUT_PYTHON:
		strcpy(string, "PYTHON");
		break;
	case DAOS_PROP_CO_LAYOUT_SPARK:
		strcpy(string, "SPARK");
		break;
	case DAOS_PROP_CO_LAYOUT_DATABASE:
		strcpy(string, "DATABASE");
		break;
	case DAOS_PROP_CO_LAYOUT_ROOT:
		strcpy(string, "ROOT");
		break;
	case DAOS_PROP_CO_LAYOUT_SEISMIC:
		strcpy(string, "SEISMIC");
		break;
	case DAOS_PROP_CO_LAYOUT_METEO:
		strcpy(string, "METEO");
		break;
	default:
		strcpy(string, "unknown");
		break;
	}
}

static inline void
daos_anchor_set_flags(daos_anchor_t *anchor, uint32_t flags)
{
	anchor->da_flags = flags;
}

static inline uint32_t
daos_anchor_get_flags(daos_anchor_t *anchor)
{
	return anchor->da_flags;
}

static inline void
daos_anchor_set_eof(daos_anchor_t *anchor)
{
	anchor->da_type = DAOS_ANCHOR_TYPE_EOF;
}

static inline void
daos_anchor_set_zero(daos_anchor_t *anchor)
{
	anchor->da_type = DAOS_ANCHOR_TYPE_ZERO;
}

static inline bool
daos_anchor_is_zero(daos_anchor_t *anchor)
{
	return anchor->da_type == DAOS_ANCHOR_TYPE_ZERO;
}

/* default debug log file */
#define DAOS_LOG_DEFAULT	"/tmp/daos.log"

#ifdef NEED_EXPLICIT_BZERO
/* Secure memory scrub */
static inline void
explicit_bzero(void *s, size_t count) {
	memset(s, 0, count);
	asm volatile("" :  : "r"(s) : "memory");
}
#endif /* NEED_EXPLICIT_BZERO */

#endif /* __DAOS_COMMON_H__ */
