/**
 * (C) Copyright 2015-2019 Intel Corporation.
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

#include <daos/debug.h>
#include <gurt/hash.h>
#include <gurt/common.h>
#include <cart/api.h>
#include <daos_types.h>
#include <daos/checksum.h>

#define DF_OID		DF_U64"."DF_U64
#define DP_OID(o)	(o).hi, (o).lo

#define DF_UOID		DF_OID".%u"
#define DP_UOID(uo)	DP_OID((uo).id_pub), (uo).id_shard

#define MAX_TREE_ORDER_INC	7

struct daos_node_overhead {
	/** Node size in bytes for tree with only */
	int	no_size;
	/** Order of node */
	int	no_order;
};

/** Overheads for a tree */
struct daos_tree_overhead {
	/** Overhead for full size tree node */
	struct daos_node_overhead	to_node_overhead;
	/** Overhead for dynamic tree nodes */
	struct daos_node_overhead	to_dyn_overhead[MAX_TREE_ORDER_INC];
	/** Number of dynamic tree node sizes */
	int				to_dyn_count;
	/** Inline metadata size for each record */
	int				to_node_rec_msize;
	/** Dynamic metadata size of an allocated record. */
	int				to_record_msize;
};

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
	return (tv.tv_sec * NSEC_PER_SEC + tv.tv_nsec); /* nano seconds */
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
	/** for binary search */
	int	(*so_cmp_key)(void *array, int i, uint64_t key);
} daos_sort_ops_t;

int daos_array_sort(void *array, unsigned int len, bool unique,
		    daos_sort_ops_t *ops);
int daos_array_find(void *array, unsigned int len, uint64_t key,
		    daos_sort_ops_t *ops);
void daos_array_shuffle(void *arr, unsigned int len, daos_sort_ops_t *ops);

int  daos_sgl_init(d_sg_list_t *sgl, unsigned int nr);
void daos_sgl_fini(d_sg_list_t *sgl, bool free_iovs);
int daos_sgl_copy_ptr(d_sg_list_t *dst, d_sg_list_t *src);
int daos_sgls_copy_data_out(d_sg_list_t *dst, int dst_nr, d_sg_list_t *src,
			    int src_nr);
int daos_sgl_copy_data_out(d_sg_list_t *dst, d_sg_list_t *src);
int daos_sgl_copy_data(d_sg_list_t *dst, d_sg_list_t *src);
int daos_sgl_alloc_copy_data(d_sg_list_t *dst, d_sg_list_t *src);
daos_size_t daos_sgl_data_len(d_sg_list_t *sgl);
daos_size_t daos_sgl_buf_size(d_sg_list_t *sgl);
daos_size_t daos_sgls_buf_size(d_sg_list_t *sgls, int nr);
daos_size_t daos_sgls_packed_size(d_sg_list_t *sgls, int nr,
				  daos_size_t *buf_size);
daos_size_t daos_iods_len(daos_iod_t *iods, int nr);
int daos_iod_copy(daos_iod_t *dst, daos_iod_t *src);
void daos_iods_free(daos_iod_t *iods, int nr, bool free);

char *daos_str_trimwhite(char *str);
int daos_iov_copy(d_iov_t *dst, d_iov_t *src);
void daos_iov_free(d_iov_t *iov);
bool daos_key_match(daos_key_t *key1, daos_key_t *key2);

/* The DAOS BITS is composed by uint32_t[x] */
#define DAOS_BITS_SIZE  (sizeof(uint32_t) * NBBY)
int daos_first_unset_bit(uint32_t *bits, unsigned int size);

#if !defined(container_of)
/* given a pointer @ptr to the field @member embedded into type (usually
 *  * struct) @type, return pointer to the embedding instance of @type. */
# define container_of(ptr, type, member)		\
	        ((type *)((char *)(ptr)-(char *)(&((type *)0)->member)))
#endif

#ifndef offsetof
# define offsetof(typ,memb)	((long)((char *)&(((typ *)0)->memb)))
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

#ifndef MIN
# define MIN(a,b) (((a)<(b)) ? (a): (b))
#endif
#ifndef MAX
# define MAX(a,b) (((a)>(b)) ? (a): (b))
#endif

#ifndef min
#define min(x,y) ((x)<(y) ? (x) : (y))
#endif

#ifndef max
#define max(x,y) ((x)>(y) ? (x) : (y))
#endif

#ifndef min_t
#define min_t(type,x,y) \
	        ({ type __x = (x); type __y = (y); __x < __y ? __x: __y; })
#endif
#ifndef max_t
#define max_t(type,x,y) \
	        ({ type __x = (x); type __y = (y); __x > __y ? __x: __y; })
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
	switch (err) {
	case -DER_SUCCESS:	return 0;
	case -DER_NO_PERM:
	case -DER_EP_RO:
	case -DER_EP_OLD:	return EPERM;
	case -DER_NO_HDL:
	case -DER_ENOENT:
	case -DER_NONEXIST:	return ENOENT;
	case -DER_INVAL:
	case -DER_NOTYPE:
	case -DER_NOSCHEMA:
	case -DER_NOLOCAL:
	case -DER_KEY2BIG:
	case -DER_REC2BIG:
	case -DER_IO_INVAL:	return EINVAL;
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

static inline bool
daos_crt_network_error(int err)
{
	return err == -DER_HG || err == -DER_ADDRSTR_GEN ||
	       err == -DER_PMIX || err == -DER_UNREG ||
	       err == -DER_UNREACH || err == -DER_CANCELED ||
	       err == -DER_NOREPLY;
}

#define daos_rank_list_dup		d_rank_list_dup
#define daos_rank_list_dup_sort_uniq	d_rank_list_dup_sort_uniq
#define daos_rank_list_filter		d_rank_list_filter
#define daos_rank_list_alloc		d_rank_list_alloc
#define daos_rank_list_copy		d_rank_list_copy
#define daos_rank_list_sort		d_rank_list_sort
#define daos_rank_list_find		d_rank_list_find
#define daos_rank_list_identical	d_rank_list_identical
#define daos_rank_in_rank_list		d_rank_in_rank_list
#define daos_rank_list_append		d_rank_list_append

/* the key of various type of parameters, used by DAOS client to set
 * different parameters globally on all servers.
 */
enum {
	DSS_KEY_FAIL_LOC = 0,
	DSS_KEY_FAIL_VALUE,
	DSS_KEY_FAIL_NUM,
	DSS_REBUILD_RES_PERCENTAGE,
	DSS_KEY_NUM,
};

void
daos_fail_loc_set(uint64_t id);
void
daos_fail_loc_reset(void);
void
daos_fail_value_set(uint64_t val);
void
daos_fail_num_set(uint64_t num);

int
daos_fail_check(uint64_t id);

uint64_t
daos_fail_value_get(void);

int
daos_fail_init(void);
void
daos_fail_fini();

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

#define DAOS_FAIL_GROUP_MASK 0xff0000
#define DAOS_FAIL_GROUP_SHIFT 16

enum {
	DAOS_FAIL_UNIT_TEST_GROUP = 1,
	DAOS_FAIL_MAX_GROUP
};

#define DAOS_FAIL_UNIT_TEST_GROUP_LOC	\
		(DAOS_FAIL_UNIT_TEST_GROUP << DAOS_FAIL_GROUP_SHIFT)

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

#define DAOS_RDB_SKIP_APPENDENTRIES_FAIL (DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x19)
#define DAOS_FORCE_REFRESH_POOL_MAP	  (DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x1a)

#define DAOS_VOS_AGG_RANDOM_YIELD	(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x1b)
#define DAOS_VOS_AGG_MW_THRESH		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x1c)
#define DAOS_VOS_NON_LEADER		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x1d)

#define DAOS_FORCE_CAPA_FETCH		(DAOS_FAIL_UNIT_TEST_GROUP_LOC | 0x1e)

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

/* NVMe shared constants */
#define DAOS_NVME_SHMID_NONE	-1

crt_init_options_t *daos_crt_init_opt_get(bool server, int crt_nr);

int crt_proc_daos_prop_t(crt_proc_t proc, daos_prop_t **data);

bool daos_prop_valid(daos_prop_t *prop, bool pool, bool input);
daos_prop_t *daos_prop_dup(daos_prop_t *prop, bool pool);
struct daos_prop_entry *daos_prop_entry_get(daos_prop_t *prop, uint32_t type);
int daos_prop_copy(daos_prop_t *prop_req, daos_prop_t *prop_reply);

static inline void
daos_parse_oclass(const char *string, daos_oclass_id_t *objectClass)
{
	if (strcasecmp(string, "tiny") == 0)
		*objectClass = DAOS_OC_TINY_RW;
	else if (strcasecmp(string, "small") == 0)
		*objectClass = DAOS_OC_SMALL_RW;
	else if (strcasecmp(string, "large") == 0)
		*objectClass = DAOS_OC_LARGE_RW;
	else if (strcasecmp(string, "R2") == 0)
		*objectClass = DAOS_OC_R2_RW;
	else if (strcasecmp(string, "R2S") == 0)
		*objectClass = DAOS_OC_R2S_RW;
	else if (strcasecmp(string, "repl_max") == 0)
		*objectClass = DAOS_OC_REPL_MAX_RW;
	else
		*objectClass = DAOS_OC_UNKNOWN;
}

static inline void
daos_unparse_oclass(daos_oclass_id_t objectClass, char *string)
{
	switch (objectClass) {
	case DAOS_OC_TINY_RW:
		strcpy(string, "tiny");
		break;
	case DAOS_OC_SMALL_RW:
		strcpy(string, "small");
		break;
	case DAOS_OC_LARGE_RW:
		strcpy(string, "large");
		break;
	case DAOS_OC_R2_RW:
		strcpy(string, "R2");
		break;
	case DAOS_OC_R2S_RW:
	       strcpy(string, "R2S");
	       break;
	case DAOS_OC_REPL_MAX_RW:
	       strcpy(string, "repl_max");
	       break;
	default:
		strcpy(string, "unknown");
		break;
	}
}

static inline void
daos_parse_ctype(const char *string, daos_cont_layout_t *type)
{
	if (strcasecmp(string, "HDF5") == 0)
		*type = DAOS_PROP_CO_LAYOUT_HDF5;
	else if (strcasecmp(string, "POSIX") == 0)
		*type = DAOS_PROP_CO_LAYOUT_POSIX;
	else
		*type = DAOS_PROP_CO_LAYOUT_UNKOWN;
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
	default:
		D_ASSERT(0);
	}
}

#endif /* __DAOS_COMMON_H__ */
