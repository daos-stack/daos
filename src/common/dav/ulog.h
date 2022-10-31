/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2015-2021, Intel Corporation */

/*
 * ulog.h -- unified log public interface
 */

#ifndef __DAOS_COMMON_ULOG_H
#define __DAOS_COMMON_ULOG_H 1

#include <stddef.h>
#include <stdint.h>

#include "util.h"
#include "vec.h"
#include "mo_wal.h"

struct ulog_entry_base {
	uint64_t offset; /* offset with operation type flag */
};

/*
 * ulog_entry_val -- log entry
 */
struct ulog_entry_val {
	struct ulog_entry_base base;
	uint64_t value; /* value to be applied */
};

/*
 * ulog_entry_buf - ulog buffer entry
 */
struct ulog_entry_buf {
	struct ulog_entry_base base; /* offset with operation type flag */
	uint64_t checksum; /* checksum of the entire log entry */
	uint64_t size; /* size of the buffer to be modified */
	uint8_t data[]; /* content to fill in */
};

#define ULOG_UNUSED ((CACHELINE_SIZE - 40) / 8)
/*
 * This structure *must* be located at a cacheline boundary. To achieve this,
 * the next field is always allocated with extra padding, and then the offset
 * is additionally aligned.
 */
#define ULOG(capacity_bytes) {\
	/* 64 bytes of metadata */\
	uint64_t checksum; /* checksum of ulog header and its entries */\
	struct ulog *next; /* offset of ulog extension */\
	uint64_t capacity; /* capacity of this ulog in bytes */\
	uint64_t gen_num; /* generation counter */\
	uint64_t flags; /* ulog flags */\
	uint64_t unused[ULOG_UNUSED]; /* must be 0 */\
	uint8_t data[capacity_bytes]; /* N bytes of data */\
}

#define SIZEOF_ULOG(base_capacity)\
(sizeof(struct ulog) + base_capacity)

/*
 * Ulog buffer allocated by the user must be marked by this flag.
 * It is important to not free it at the end:
 * what user has allocated - user should free himself.
 */
#define ULOG_USER_OWNED (1U << 0)

/* use this for allocations of aligned ulog extensions */
#define SIZEOF_ALIGNED_ULOG(base_capacity)\
ALIGN_UP(SIZEOF_ULOG(base_capacity + (2 * CACHELINE_SIZE)), CACHELINE_SIZE)

struct ulog ULOG(0);

VEC(ulog_next, struct ulog *);

typedef uint64_t ulog_operation_type;

#define ULOG_OPERATION_SET		(0b000ULL << 61ULL)
#ifdef	WAL_SUPPORTS_AND_OR_OPS
#define ULOG_OPERATION_AND		(0b001ULL << 61ULL)
#define ULOG_OPERATION_OR		(0b010ULL << 61ULL)
#else
#define ULOG_OPERATION_CLR_BITS		(0b001ULL << 61ULL)
#define ULOG_OPERATION_SET_BITS		(0b010ULL << 61ULL)
#endif
#define ULOG_OPERATION_BUF_SET		(0b101ULL << 61ULL)
#define ULOG_OPERATION_BUF_CPY		(0b110ULL << 61ULL)

#ifndef	WAL_SUPPORTS_AND_OR_OPS
#endif

#ifdef	WAL_SUPPORTS_AND_OR_OPS
#define	ULOG_ENTRY_IS_BIT_OP(opc)	((opc == ULOG_OPERATION_AND) || \
					 (opc == ULOG_OPERATION_OR))
#else
#define	ULOG_ENTRY_IS_BIT_OP(opc)	((opc == ULOG_OPERATION_CLR_BITS) || \
					 (opc == ULOG_OPERATION_SET_BITS))
#define ULOG_ENTRY_OPS_POS		16 /* bits' pos at value:16 */
#define ULOG_ENTRY_OPS_BITS_MASK	((1ULL << ULOG_ENTRY_OPS_POS) - 1)
#define ULOG_ENTRY_VAL_TO_BITS(val)	((val) & ULOG_ENTRY_OPS_BITS_MASK)
#define ULOG_ENTRY_VAL_TO_POS(val)	((val) >> ULOG_ENTRY_OPS_POS)
#define ULOG_ENTRY_OPS_POS_MASK		(RUN_BITS_PER_VALUE - 1ULL)
#define ULOG_ENTRY_TO_VAL(pos, nbits)	(((uint64_t)(nbits) & ULOG_ENTRY_OPS_BITS_MASK) | \
					 ((pos) & ULOG_ENTRY_OPS_POS_MASK) << ULOG_ENTRY_OPS_POS)
#endif

/* immediately frees all associated ulog structures */
#define ULOG_FREE_AFTER_FIRST (1U << 0)
/* increments gen_num of the first, preallocated, ulog */
#define ULOG_INC_FIRST_GEN_NUM (1U << 1)

typedef int (*ulog_check_offset_fn)(void *ctx, uint64_t offset);
typedef int (*ulog_extend_fn)(struct ulog **, uint64_t);
typedef int (*ulog_entry_cb)(struct ulog_entry_base *e, void *arg,
	const struct mo_ops *p_ops);
typedef void (*ulog_free_fn)(struct ulog *ptr);

struct ulog *ulog_next(struct ulog *ulog);

void ulog_construct(uint64_t offset, size_t capacity, uint64_t gen_num,
		    int flush, uint64_t flags, const struct mo_ops *p_ops);
void ulog_construct_new(struct ulog *ulog, size_t capacity, uint64_t gen_num,
			uint64_t flags);

size_t ulog_capacity(struct ulog *ulog, size_t ulog_base_bytes);
void ulog_rebuild_next_vec(struct ulog *ulog, struct ulog_next *next);

int ulog_foreach_entry(struct ulog *ulog,
		       ulog_entry_cb cb, void *arg, const struct mo_ops *ops);

int ulog_reserve(struct ulog *ulog,
		 size_t ulog_base_nbytes, size_t gen_num,
		 int auto_reserve, size_t *new_capacity_bytes,
		 ulog_extend_fn extend, struct ulog_next *next);

int ulog_free_next(struct ulog *u, ulog_free_fn ulog_free);
void ulog_clobber(struct ulog *dest, struct ulog_next *next);
int ulog_clobber_data(struct ulog *dest,
		      struct ulog_next *next, ulog_free_fn ulog_free, unsigned flags);
void ulog_clobber_entry(const struct ulog_entry_base *e);

void ulog_process(struct ulog *ulog, ulog_check_offset_fn check,
		  const struct mo_ops *p_ops);

size_t ulog_base_nbytes(struct ulog *ulog);
int ulog_recovery_needed(struct ulog *ulog, int verify_checksum);

uint64_t ulog_entry_offset(const struct ulog_entry_base *entry);
ulog_operation_type ulog_entry_type(const struct ulog_entry_base *entry);

struct ulog_entry_val *
ulog_entry_val_create(struct ulog *ulog, size_t offset, uint64_t *dest, uint64_t value,
		      ulog_operation_type type, const struct mo_ops *p_ops);

struct ulog_entry_buf *
ulog_entry_buf_create(struct ulog *ulog, size_t offset, uint64_t gen_num,
		      uint64_t *dest, const void *src, uint64_t size,
		      ulog_operation_type type, const struct mo_ops *p_ops);

void ulog_entry_apply(const struct ulog_entry_base *e, int persist,
		      const struct mo_ops *p_ops);

size_t ulog_entry_size(const struct ulog_entry_base *entry);

int ulog_check(struct ulog *ulog, ulog_check_offset_fn check,
	       const struct mo_ops *p_ops);

#endif /* __DAOS_COMMON_ULOG_H */
