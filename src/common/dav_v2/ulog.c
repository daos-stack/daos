/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2015-2024, Intel Corporation */

/*
 * ulog.c -- unified log implementation
 */

#include <inttypes.h>
#include <string.h>

#include "dav_internal.h"
#include "mo_wal.h"
#include "ulog.h"
#include "obj.h"
#include "out.h"
#include "valgrind_internal.h"

/*
 * Operation flag at the three most significant bits
 */
#define ULOG_OPERATION(op)		((uint64_t)(op))
#define ULOG_OPERATION_MASK		((uint64_t)(0b111ULL << 61ULL))
#define ULOG_OPERATION_FROM_OFFSET(off)	\
	((ulog_operation_type) ((off) & ULOG_OPERATION_MASK))
#define ULOG_OFFSET_MASK		(~(ULOG_OPERATION_MASK))

#define CACHELINE_ALIGN(size) ALIGN_UP(size, CACHELINE_SIZE)
#define IS_CACHELINE_ALIGNED(ptr)\
	(((uintptr_t)(ptr) & (CACHELINE_SIZE - 1)) == 0)

/*
 * ulog_next -- retrieves the pointer to the next ulog
 */
struct ulog *
ulog_next(struct ulog *ulog)
{
	return ulog->next;
}

/*
 * ulog_operation -- returns the type of entry operation
 */
ulog_operation_type
ulog_entry_type(const struct ulog_entry_base *entry)
{
	return ULOG_OPERATION_FROM_OFFSET(entry->offset);
}

/*
 * ulog_offset -- returns offset
 */
uint64_t
ulog_entry_offset(const struct ulog_entry_base *entry)
{
	return entry->offset & ULOG_OFFSET_MASK;
}

/*
 * ulog_entry_size -- returns the size of a ulog entry
 */
size_t
ulog_entry_size(const struct ulog_entry_base *entry)
{
	struct ulog_entry_buf *eb;

	switch (ulog_entry_type(entry)) {
#ifdef	WAL_SUPPORTS_AND_OR_OPS
	case ULOG_OPERATION_AND:
	case ULOG_OPERATION_OR:
#else
	case ULOG_OPERATION_CLR_BITS:
	case ULOG_OPERATION_SET_BITS:
#endif
	case ULOG_OPERATION_SET:
		return sizeof(struct ulog_entry_val);
	case ULOG_OPERATION_BUF_SET:
	case ULOG_OPERATION_BUF_CPY:
		eb = (struct ulog_entry_buf *)entry;
		return CACHELINE_ALIGN(
			sizeof(struct ulog_entry_buf) + eb->size);
	default:
		ASSERT(0);
	}

	return 0;
}

/*
 * ulog_entry_valid -- (internal) checks if a ulog entry is valid
 * Returns 1 if the range is valid, otherwise 0 is returned.
 */
static int
ulog_entry_valid(struct ulog *ulog, const struct ulog_entry_base *entry)
{
	if (entry->offset == 0)
		return 0;

	size_t size;
	struct ulog_entry_buf *b;

	switch (ulog_entry_type(entry)) {
	case ULOG_OPERATION_BUF_CPY:
	case ULOG_OPERATION_BUF_SET:
		size = ulog_entry_size(entry);
		b = (struct ulog_entry_buf *)entry;

		uint64_t csum = util_checksum_compute(b, size,
				&b->checksum, 0);
		csum = util_checksum_seq(&ulog->gen_num,
				sizeof(ulog->gen_num), csum);

		if (b->checksum != csum)
			return 0;
		break;
	default:
		break;
	}

	return 1;
}

/*
 * ulog_construct -- initializes the ulog structure
 */
void
ulog_construct_new(struct ulog *ulog, size_t capacity, uint64_t gen_num, uint64_t flags)
{
	ASSERTne(ulog, NULL);

	ulog->capacity = capacity;
	ulog->checksum = 0;
	ulog->next = 0;
	ulog->gen_num = gen_num;
	ulog->flags = flags;
	memset(ulog->unused, 0, sizeof(ulog->unused));

	/* we only need to zero out the header of ulog's first entry */
	size_t zeroed_data = CACHELINE_ALIGN(sizeof(struct ulog_entry_base));
	/*
	 * We want to avoid replicating zeroes for every ulog of every
	 * lane, to do that, we need to use plain old memset.
	 */
	memset(ulog->data, 0, zeroed_data);
}

/*
 * ulog_foreach_entry -- iterates over every existing entry in the ulog
 */
int
ulog_foreach_entry(struct ulog *ulog, ulog_entry_cb cb, void *arg, const struct mo_ops *ops)
{
	struct ulog_entry_base *e;
	int ret = 0;

	for (struct ulog *r = ulog; r != NULL; r = ulog_next(r)) {
		for (size_t offset = 0; offset < r->capacity; ) {
			e = (struct ulog_entry_base *)(r->data + offset);
			if (!ulog_entry_valid(ulog, e))
				return ret;

			ret = cb(e, arg, ops);
			if (ret != 0)
				return ret;

			offset += ulog_entry_size(e);
		}
	}

	return ret;
}

/*
 * ulog_capacity -- (internal) returns the total capacity of the ulog
 */
size_t
ulog_capacity(struct ulog *ulog, size_t ulog_base_bytes)
{
	size_t capacity = ulog_base_bytes;

	ulog = ulog_next(ulog);
	/* skip the first one, we count it in 'ulog_base_bytes' */
	while (ulog != NULL) {
		capacity += ulog->capacity;
		ulog = ulog_next(ulog);
	}

	return capacity;
}

/*
 * ulog_rebuild_next_vec -- rebuilds the vector of next entries
 */
void
ulog_rebuild_next_vec(struct ulog *ulog, struct ulog_next *next)
{
	do {
		if (ulog->next != 0)
			VEC_PUSH_BACK(next, ulog->next);
	} while ((ulog = ulog_next(ulog)) != NULL);
}

/*
 * ulog_reserve -- reserves new capacity in the ulog
 */
int
ulog_reserve(struct ulog *ulog,
	size_t ulog_base_nbytes, size_t gen_num,
	int auto_reserve, size_t *new_capacity,
	ulog_extend_fn extend, struct ulog_next *next)
{
	if (!auto_reserve) {
		D_CRIT("cannot auto reserve next ulog\n");
		return -1;
	}

	size_t capacity = ulog_base_nbytes;

	VEC_FOREACH(ulog, next) {
		ASSERTne(ulog, NULL);
		capacity += ulog->capacity;
	}

	while (capacity < *new_capacity) {
		if (extend(&ulog->next, gen_num) != 0)
			return -1;
		VEC_PUSH_BACK(next, ulog->next);
		ulog = ulog_next(ulog);
		ASSERTne(ulog, NULL);

		capacity += ulog->capacity;
	}
	*new_capacity = capacity;

	return 0;
}

/*
 * ulog_checksum -- (internal) calculates ulog checksum
 */
static int
ulog_checksum(struct ulog *ulog, size_t ulog_base_bytes, int insert)
{
	return util_checksum(ulog, SIZEOF_ULOG(ulog_base_bytes),
		&ulog->checksum, insert, 0);
}

/*
 * ulog_entry_val_create -- creates a new log value entry in the ulog
 *
 * This function requires at least a cacheline of space to be available in the
 * ulog.
 */
struct ulog_entry_val *
ulog_entry_val_create(struct ulog *ulog, size_t offset, uint64_t *dest,
		      uint64_t value, ulog_operation_type type, const struct mo_ops *p_ops)
{
	struct ulog_entry_val *e =
		(struct ulog_entry_val *)(ulog->data + offset);

	struct {
		struct ulog_entry_val v;
		struct ulog_entry_base zeroes;
	} data;
	COMPILE_ERROR_ON(sizeof(data) != sizeof(data.v) + sizeof(data.zeroes));

	/*
	 * Write a little bit more to the buffer so that the next entry that
	 * resides in the log is erased. This will prevent leftovers from
	 * a previous, clobbered, log from being incorrectly applied.
	 */
	data.zeroes.offset = 0;
	data.v.base.offset =
	    p_ops->base ? umem_cache_ptr2off(p_ops->umem_store, dest) : (uint64_t)dest;
	data.v.base.offset |= ULOG_OPERATION(type);
	data.v.value = value;

	memcpy(e, &data, sizeof(data));

	return e;
}

/*
 * ulog_clobber_entry -- zeroes out a single log entry header
 */
void
ulog_clobber_entry(const struct ulog_entry_base *e)
{
	static const size_t aligned_entry_size =
		CACHELINE_ALIGN(sizeof(struct ulog_entry_base));

	memset((char *)e, 0, aligned_entry_size);
}

/*
 * ulog_entry_buf_create -- atomically creates a buffer entry in the log
 */
struct ulog_entry_buf *
ulog_entry_buf_create(struct ulog *ulog, size_t offset, uint64_t gen_num,
		uint64_t *dest, const void *src, uint64_t size,
		ulog_operation_type type, const struct mo_ops *p_ops)
{
	struct ulog_entry_buf *e =
		(struct ulog_entry_buf *)(ulog->data + offset);

	/*
	 * Depending on the size of the source buffer, we might need to perform
	 * up to three separate copies:
	 *	1. The first cacheline, 24b of metadata and 40b of data
	 * If there's still data to be logged:
	 *	2. The entire remainder of data data aligned down to cacheline,
	 *	for example, if there's 150b left, this step will copy only
	 *	128b.
	 * Now, we are left with between 0 to 63 bytes. If nonzero:
	 *	3. Create a stack allocated cacheline-sized buffer, fill in the
	 *	remainder of the data, and copy the entire cacheline.
	 *
	 * This is done so that we avoid a cache-miss on misaligned writes.
	 */

	struct ulog_entry_buf *b = alloca(CACHELINE_SIZE);

	ASSERT(p_ops->base != NULL);
	b->base.offset = umem_cache_ptr2off(p_ops->umem_store, dest);
	b->base.offset |= ULOG_OPERATION(type);
	b->size = size;
	b->checksum = 0;

	size_t bdatasize = CACHELINE_SIZE - sizeof(struct ulog_entry_buf);
	size_t ncopy = MIN(size, bdatasize);

	memcpy(b->data, src, ncopy);
	memset(b->data + ncopy, 0, bdatasize - ncopy);

	size_t remaining_size = ncopy > size ? 0 : size - ncopy;

	char *srcof = (char *)src + ncopy;
	size_t rcopy = ALIGN_DOWN(remaining_size, CACHELINE_SIZE);
	size_t lcopy = remaining_size - rcopy;

	uint8_t last_cacheline[CACHELINE_SIZE];

	if (lcopy != 0) {
		memcpy(last_cacheline, srcof + rcopy, lcopy);
		memset(last_cacheline + lcopy, 0, CACHELINE_SIZE - lcopy);
	}

	if (rcopy != 0) {
		void *rdest = e->data + ncopy;

		ASSERT(IS_CACHELINE_ALIGNED(rdest));
		memcpy(rdest, srcof, rcopy);
	}

	if (lcopy != 0) {
		void *ldest = e->data + ncopy + rcopy;

		ASSERT(IS_CACHELINE_ALIGNED(ldest));

		memcpy(ldest, last_cacheline, CACHELINE_SIZE);
	}

	b->checksum = util_checksum_seq(b, CACHELINE_SIZE, 0);
	if (rcopy != 0)
		b->checksum = util_checksum_seq(srcof, rcopy, b->checksum);
	if (lcopy != 0)
		b->checksum = util_checksum_seq(last_cacheline,
			CACHELINE_SIZE, b->checksum);

	b->checksum = util_checksum_seq(&gen_num, sizeof(gen_num),
			b->checksum);

	ASSERT(IS_CACHELINE_ALIGNED(e));

	memcpy(e, b, CACHELINE_SIZE);

	/*
	 * Allow having uninitialized data in the buffer - this requires marking
	 * data as defined so that comparing checksums is not reported as an
	 * error by memcheck.
	 */
	VALGRIND_DO_MAKE_MEM_DEFINED(e->data, ncopy + rcopy + lcopy);
	VALGRIND_DO_MAKE_MEM_DEFINED(&e->checksum, sizeof(e->checksum));

	ASSERT(ulog_entry_valid(ulog, &e->base));

	return e;
}

/*
 * ulog_entry_apply -- applies modifications of a single ulog entry
 */
void
ulog_entry_apply(const struct ulog_entry_base *e, int persist,
		 const struct mo_ops *p_ops)
{
	ulog_operation_type    t = ulog_entry_type(e);
	uint64_t               offset   = ulog_entry_offset(e);
	size_t                 dst_size = sizeof(uint64_t);
	struct ulog_entry_val *ev;
	struct ulog_entry_buf *eb;
	uint16_t               nbits;
	uint32_t               pos;
	uint64_t               bmask;
	uint64_t              *dst;

	dst = p_ops->base ? umem_cache_off2ptr(p_ops->umem_store, offset) : (uint64_t *)offset;

	SUPPRESS_UNUSED(persist);

	switch (t) {
#ifdef	WAL_SUPPORTS_AND_OR_OPS
	case ULOG_OPERATION_AND:
		ev = (struct ulog_entry_val *)e;

		VALGRIND_ADD_TO_TX(dst, dst_size);
		*dst &= ev->value;
		break;
	case ULOG_OPERATION_OR:
		ev = (struct ulog_entry_val *)e;

		VALGRIND_ADD_TO_TX(dst, dst_size);
		*dst |= ev->value;
		break;
#else
	case ULOG_OPERATION_CLR_BITS:
		ev = (struct ulog_entry_val *)e;
		pos = ULOG_ENTRY_VAL_TO_POS(ev->value);
		nbits = ULOG_ENTRY_VAL_TO_BITS(ev->value);
		if (nbits == RUN_BITS_PER_VALUE)
			bmask = UINT64_MAX;
		else
			bmask = ((1ULL << nbits) - 1ULL) << pos;

		VALGRIND_ADD_TO_TX(dst, dst_size);
		*dst &= ~bmask;
		break;
	case ULOG_OPERATION_SET_BITS:
		ev = (struct ulog_entry_val *)e;
		pos = ULOG_ENTRY_VAL_TO_POS(ev->value);
		nbits = ULOG_ENTRY_VAL_TO_BITS(ev->value);
		if (nbits == RUN_BITS_PER_VALUE)
			bmask = UINT64_MAX;
		else
			bmask = ((1ULL << nbits) - 1ULL) << pos;

		VALGRIND_ADD_TO_TX(dst, dst_size);
		*dst |= bmask;
		break;
#endif
	case ULOG_OPERATION_SET:
		ev = (struct ulog_entry_val *)e;

		VALGRIND_ADD_TO_TX(dst, dst_size);
		*dst = ev->value;
		break;
	case ULOG_OPERATION_BUF_CPY:
		eb = (struct ulog_entry_buf *)e;

		dst_size = eb->size;
		VALGRIND_ADD_TO_TX(dst, dst_size);
		mo_wal_memcpy(p_ops, dst, eb->data, eb->size, 0);
		break;
	case ULOG_OPERATION_BUF_SET:
	default:
		ASSERT(0);
	}
	VALGRIND_REMOVE_FROM_TX(dst, dst_size);
}

/*
 * ulog_process_entry -- (internal) processes a single ulog entry
 */
static int
ulog_process_entry(struct ulog_entry_base *e, void *arg,
		   const struct mo_ops *p_ops)
{
	/* suppress unused-parameter errors */
	SUPPRESS_UNUSED(arg);

	ulog_entry_apply(e, 0, p_ops);

	return 0;
}
/*
 * ulog_inc_gen_num -- (internal) increments gen num in the ulog
 */
static void
ulog_inc_gen_num(struct ulog *ulog)
{
	ulog->gen_num++;
}

/*
 * ulog_free_next -- free all ulogs starting from the indicated one.
 * Function returns 1 if any ulog have been freed or unpinned, 0 otherwise.
 */
int
ulog_free_next(struct ulog *u, ulog_free_fn ulog_free)
{
	int ret = 0;

	if (u == NULL)
		return ret;

	VEC(, struct ulog **) ulogs_internal_except_first;
	VEC_INIT(&ulogs_internal_except_first);

	while (u->next != 0) {
		if (VEC_PUSH_BACK(&ulogs_internal_except_first,
			&u->next) != 0) {
			/* this is fine, it will just use more memory */
			DAV_DBG("unable to free transaction logs memory");
			goto out;
		}
		u = u->next;
	}

	/* free non-user defined logs */
	struct ulog **ulog_ptr;

	VEC_FOREACH_REVERSE(ulog_ptr, &ulogs_internal_except_first) {
		ulog_free(*ulog_ptr);
		*ulog_ptr = NULL;
		ret = 1;
	}

out:
	VEC_DELETE(&ulogs_internal_except_first);
	return ret;
}

/*
 * ulog_clobber -- zeroes the metadata of the ulog
 */
void
ulog_clobber(struct ulog *dest, struct ulog_next *next)
{
	struct ulog empty;

	memset(&empty, 0, sizeof(empty));

	if (next != NULL)
		empty.next = VEC_SIZE(next) == 0 ? 0 : VEC_FRONT(next);
	else
		empty.next = dest->next;

	memcpy(dest, &empty, sizeof(empty));
}

/*
 * ulog_clobber_data -- zeroes out 'nbytes' of data in the logs
 */
int
ulog_clobber_data(struct ulog *ulog_first,
	struct ulog_next *next, ulog_free_fn ulog_free,
	unsigned flags)
{
	ASSERTne(ulog_first, NULL);

	/* In case of abort we need to increment counter in the first ulog. */
	if (flags & ULOG_INC_FIRST_GEN_NUM)
		ulog_inc_gen_num(ulog_first);

	/*
	 * In the case of abort or commit, we are not going to free all ulogs,
	 * but rather increment the generation number to be consistent in the
	 * first two ulogs.
	 */
	struct ulog *ulog_second = VEC_SIZE(next) == 0 ? 0 : *VEC_GET(next, 0);

	if (ulog_second && !(flags & ULOG_FREE_AFTER_FIRST))
		/*
		 * We want to keep gen_nums consistent between ulogs.
		 * If the transaction will commit successfully we'll reuse the
		 * second buffer (third and next ones will be freed anyway).
		 * If the application will crash we'll free 2nd ulog on
		 * recovery, which means we'll never read gen_num of the
		 * second ulog in case of an ungraceful shutdown.
		 */
		ulog_inc_gen_num(ulog_second);

	struct ulog *u;

	/*
	 * To make sure that transaction logs do not occupy too
	 * much of space, all of them, expect for the first one,
	 * are freed at the end of the operation. The reasoning for
	 * this is that pmalloc() is a relatively cheap operation for
	 * transactions where many hundreds of kilobytes are being
	 * snapshot, and so, allocating and freeing the buffer for
	 * each transaction is an acceptable overhead for the average
	 * case.
	 */
	if (flags & ULOG_FREE_AFTER_FIRST)
		u = ulog_first;
	else
		u = ulog_second;

	if (u == NULL)
		return 0;

	return ulog_free_next(u, ulog_free);
}

/*
 * ulog_process -- process ulog entries
 */
void
ulog_process(struct ulog *ulog, ulog_check_offset_fn check,
	     const struct mo_ops *p_ops)
{
	/* suppress unused-parameter errors */
	SUPPRESS_UNUSED(check);

#ifdef DAV_EXTRA_DEBUG
	if (check)
		ulog_check(ulog, check, p_ops);
#endif

	ulog_foreach_entry(ulog, ulog_process_entry, NULL, p_ops);
	mo_wal_drain(p_ops);
}

/*
 * ulog_base_nbytes -- (internal) counts the actual of number of bytes
 *	occupied by the ulog
 */
size_t
ulog_base_nbytes(struct ulog *ulog)
{
	size_t offset = 0;
	struct ulog_entry_base *e;

	for (offset = 0; offset < ulog->capacity; ) {
		e = (struct ulog_entry_base *)(ulog->data + offset);
		if (!ulog_entry_valid(ulog, e))
			break;

		offset += ulog_entry_size(e);
	}

	return offset;
}

/*
 * ulog_recovery_needed -- checks if the logs needs recovery
 */
int
ulog_recovery_needed(struct ulog *ulog, int verify_checksum)
{
	size_t nbytes = MIN(ulog_base_nbytes(ulog), ulog->capacity);

	if (nbytes == 0)
		return 0;

	if (verify_checksum && !ulog_checksum(ulog, nbytes, 0))
		return 0;

	return 1;
}

/*
 * ulog_check_entry --
 *	(internal) checks consistency of a single ulog entry
 */
static int
ulog_check_entry(struct ulog_entry_base *e, void *arg, const struct mo_ops *p_ops)
{
	uint64_t offset = ulog_entry_offset(e);
	ulog_check_offset_fn check = arg;

	if (!check(p_ops->base, offset)) {
		DAV_DBG("ulog %p invalid offset %" PRIu64,
				e, e->offset);
		return -1;
	}

	return offset == 0 ? -1 : 0;
}

/*
 * ulog_check -- (internal) check consistency of ulog entries
 */
int
ulog_check(struct ulog *ulog, ulog_check_offset_fn check, const struct mo_ops *p_ops)
{
	DAV_DBG("ulog %p", ulog);

	return ulog_foreach_entry(ulog,
			ulog_check_entry, check, p_ops);
}
