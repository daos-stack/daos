#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shm_internal.h"

/*
** Architecture-specific bit manipulation routines.
**
** TLSF achieves O(1) cost for malloc and free operations by limiting
** the search for a free block to a free list of guaranteed size
** adequate to fulfill the request, combined with efficient free list
** queries using bitmasks and architecture-specific bit-manipulation
** routines.
**
** Most modern processors provide instructions to count leading zeroes
** in a word, find the lowest and highest set bit, etc. These
** specific implementations will be used when available, falling back
** to a reasonably efficient generic implementation.
**
** NOTE: TLSF spec relies on ffs/fls returning value 0..31.
** ffs/fls return 1-32 by default, returning 0 for error.
*/

/*
** Detect whether or not we are building for a 32- or 64-bit (LP/LLP)
** architecture. There is no reliable portable method at compile-time.
*/

typedef void * pool_t;

static tlsf_t
tlsf_create(void *mem);

/* Add/remove memory pools. */
static pool_t
tlsf_add_pool(tlsf_t tlsf, void *mem, size_t bytes);

/* Overheads/limits of internal structures. */
static size_t
tlsf_size(void);

static inline int
tlsf_ffs(unsigned int word)
{
	return __builtin_ffs(word) - 1;
}

static inline int
tlsf_fls(unsigned int word)
{
	const int bit = word ? 32 - __builtin_clz(word) : 0;

	return bit - 1;
}

static inline int
tlsf_fls_sizet(size_t size)
{
	int high = (int)(size >> 32);

	return high ? (32 + tlsf_fls(high)) : (tlsf_fls((int)size & 0xffffffff));
}

/*
** Constants.
*/

/* Public constants: may be modified. */
enum tlsf_public
{
	/* log2 of number of linear subdivisions of block sizes. Larger
	** values require more memory in the control structure. Values of
	** 4 or 5 are typical.
	*/
	SL_INDEX_COUNT_LOG2 = 5,
};

/* Private constants: do not modify. */
enum tlsf_private
{
	/* All allocation sizes and addresses are aligned to 8 bytes. */
	ALIGN_SIZE_LOG2 = 3,
	ALIGN_SIZE = (1 << ALIGN_SIZE_LOG2),

	/*
	** We support allocations of sizes up to (1 << FL_INDEX_MAX) bits.
	** However, because we linearly subdivide the second-level lists, and
	** our minimum size granularity is 4 bytes, it doesn't make sense to
	** create first-level lists for sizes smaller than SL_INDEX_COUNT * 4,
	** or (1 << (SL_INDEX_COUNT_LOG2 + 2)) bytes, as there we will be
	** trying to split size ranges into more slots than we have available.
	** Instead, we calculate the minimum threshold size, and place all
	** blocks below that size into the 0th first-level list.
	*/

	/*
	** TODO: We can increase this to support larger sizes, at the expense
	** of more overhead in the TLSF structure.
	*/
	FL_INDEX_MAX = 32,
	SL_INDEX_COUNT = (1 << SL_INDEX_COUNT_LOG2),
	FL_INDEX_SHIFT = (SL_INDEX_COUNT_LOG2 + ALIGN_SIZE_LOG2),
	FL_INDEX_COUNT = (FL_INDEX_MAX - FL_INDEX_SHIFT + 1),

	SMALL_BLOCK_SIZE = (1 << FL_INDEX_SHIFT),
};

/*
** Cast and min/max macros.
*/

#define tlsf_cast(t, exp)	((t) (exp))
#define tlsf_min(a, b)		((a) < (b) ? (a) : (b))
#define tlsf_max(a, b)		((a) > (b) ? (a) : (b))

/*
** Set assert macro, if it has not been provided by the user.
*/
#if !defined (tlsf_assert)
#define tlsf_assert assert
#endif

/*
** Static assertion mechanism.
*/

#define _tlsf_glue2(x, y) x ## y
#define _tlsf_glue(x, y) _tlsf_glue2(x, y)
#define tlsf_static_assert(exp) \
	typedef char _tlsf_glue(static_assert, __LINE__) [(exp) ? 1 : -1]

/* This code has been tested on 32- and 64-bit (LP/LLP) architectures. */
tlsf_static_assert(sizeof(int) * CHAR_BIT == 32);
tlsf_static_assert(sizeof(size_t) * CHAR_BIT >= 32);
tlsf_static_assert(sizeof(size_t) * CHAR_BIT <= 64);

/* SL_INDEX_COUNT must be <= number of bits in sl_bitmap's storage type. */
tlsf_static_assert(sizeof(unsigned int) * CHAR_BIT >= SL_INDEX_COUNT);

/* Ensure we've properly tuned our sizes. */
tlsf_static_assert(ALIGN_SIZE == SMALL_BLOCK_SIZE / SL_INDEX_COUNT);

/*
** Data structures and associated constants.
*/

/*
** Block header structure.
**
** There are several implementation subtleties involved:
** - The prev_phys_block field is only valid if the previous block is free.
** - The prev_phys_block field is actually stored at the end of the
**   previous block. It appears at the beginning of this structure only to
**   simplify the implementation.
** - The next_free / prev_free fields are only valid if the block is free.
*/
typedef struct block_header_t
{
	/* Points to the previous physical block. */
	off_t  off_prev_phys_block;

	/* The size of this block, excluding the block header. */
	size_t size;

	/* offset to next and previous free blocks. */
	off_t  off_next_free;
	off_t  off_prev_free;
} block_header_t;

/*
** Since block sizes are always at least a multiple of 4, the two least
** significant bits of the size field are used to store the block status:
** - bit 0: whether block is busy or free
** - bit 1: whether previous block is busy or free
*/

#define block_header_free_bit (1 << 0)
#define block_header_prev_free_bit (1 << 1)

/*
** The size of the block header exposed to used blocks is the size field.
** The prev_phys_block field is stored *inside* the previous free block.
*/
#define block_header_overhead (sizeof(size_t))

/*
** Overhead of the TLSF structures in a given memory block passed to
** tlsf_add_pool, equal to the overhead of a free block and the
** sentinel block.
*/
#define pool_overhead (2 * block_header_overhead)

/* User data starts directly after the size field in a used block. */
#define block_start_offset (offsetof(block_header_t, size) + sizeof(size_t))

/*
** A free block must be large enough to store its header minus the size of
** the prev_phys_block field, and no larger than the number of addressable
** bits for FL_INDEX.
*/

#define block_size_min (sizeof(block_header_t) - sizeof(block_header_t*))
#define block_size_max (tlsf_cast(size_t, 1) << FL_INDEX_MAX)

/* The TLSF control structure. */
typedef struct control_t
{
	d_shm_mutex_t  lock;
	/* Empty lists point at this block to indicate they are free. */
	block_header_t block_null;

	/* Bitmaps for free lists. */
	unsigned int   fl_bitmap;
	unsigned int   sl_bitmap[FL_INDEX_COUNT];

	/* offset of the head of free lists. */
	off_t          off_blocks[FL_INDEX_COUNT][SL_INDEX_COUNT];
} control_t;

/* A type used for casting when doing pointer arithmetic. */
typedef ptrdiff_t tlsfptr_t;

/*
** block_header_t member functions.
*/

static inline size_t
block_size(const block_header_t *block)
{
	return block->size & ~(block_header_free_bit | block_header_prev_free_bit);
}

static inline void
block_set_size(block_header_t *block, size_t size)
{
	const size_t oldsize = block->size;

	block->size = size | (oldsize & (block_header_free_bit | block_header_prev_free_bit));
}

static inline int
block_is_last(const block_header_t *block)
{
	return block_size(block) == 0;
}

static inline int
block_is_free(const block_header_t *block)
{
	return tlsf_cast(int, block->size & block_header_free_bit);
}

static inline void
block_set_free(block_header_t *block)
{
	block->size |= block_header_free_bit;
}

static inline void
block_set_used(block_header_t *block)
{
	block->size &= ~block_header_free_bit;
}

static inline int
block_is_prev_free(const block_header_t *block)
{
	return tlsf_cast(int, block->size & block_header_prev_free_bit);
}

static inline void
block_set_prev_free(block_header_t *block)
{
	block->size |= block_header_prev_free_bit;
}

static inline void
block_set_prev_used(block_header_t *block)
{
	block->size &= ~block_header_prev_free_bit;
}

static inline block_header_t *
block_from_ptr(const void *ptr)
{
	return tlsf_cast(block_header_t *,
		tlsf_cast(unsigned char *, ptr) - block_start_offset);
}

static inline void *
block_to_ptr(const block_header_t *block)
{
	return tlsf_cast(void *,
		tlsf_cast(unsigned char *, block) + block_start_offset);
}

/* Return location of next block after block of given size. */
static inline block_header_t *
offset_to_block(const void *ptr, size_t size)
{
	return tlsf_cast(block_header_t *, tlsf_cast(tlsfptr_t, ptr) + size);
}

/* Return location of next existing block. */
static inline block_header_t *
block_next(const block_header_t *block)
{
	block_header_t *next = offset_to_block(block_to_ptr(block),
		block_size(block) - block_header_overhead);

	tlsf_assert(!block_is_last(block));
	return next;
}

/* Link a new block with its physical neighbor, return the neighbor. */
static inline block_header_t *
block_link_next(control_t *control, block_header_t *block)
{
	block_header_t *next = block_next(block);

	next->off_prev_phys_block = tlsf_cast(off_t, block) - tlsf_cast(off_t, control);
	return next;
}

static inline void
block_mark_as_free(control_t *control, block_header_t *block)
{
	/* Link the block to the next block, first. */
	block_header_t *next = block_link_next(control, block);

	block_set_prev_free(next);
	block_set_free(block);
}

static inline void
block_mark_as_used(block_header_t *block)
{
	block_header_t *next = block_next(block);

	block_set_prev_used(next);
	block_set_used(block);
}

static inline size_t
align_up(size_t x, size_t align)
{
	tlsf_assert(0 == (align & (align - 1)) && "must align to a power of two");
	return (x + (align - 1)) & ~(align - 1);
}

static inline size_t
align_down(size_t x, size_t align)
{
	tlsf_assert(0 == (align & (align - 1)) && "must align to a power of two");
	return x - (x & (align - 1));
}

static inline void *
align_ptr(const void *ptr, size_t align)
{
	const tlsfptr_t aligned =
		(tlsf_cast(tlsfptr_t, ptr) + (align - 1)) & ~(align - 1);

	tlsf_assert(0 == (align & (align - 1)) && "must align to a power of two");
	return tlsf_cast(void*, aligned);
}

/*
** Adjust an allocation size to be aligned to word size, and no smaller
** than internal minimum.
*/
static inline size_t
adjust_request_size(size_t size, size_t align)
{
	size_t adjust = 0;

	if (size) {
		const size_t aligned = align_up(size, align);

		/* aligned sized must not exceed block_size_max or we'll go out of bounds on
		 * sl_bitmap
		 */
		if (aligned < block_size_max)
			adjust = tlsf_max(aligned, block_size_min);
	}
	return adjust;
}

/*
** TLSF utility functions. In most cases, these are direct translations of
** the documentation found in the white paper.
*/

static inline void
mapping_insert(size_t size, int *fli, int *sli)
{
	if (size < SMALL_BLOCK_SIZE) {
		/* Store small blocks in first list. */
		*fli = 0;
		*sli = tlsf_cast(int, size) / (SMALL_BLOCK_SIZE / SL_INDEX_COUNT);
	} else {
		*fli = tlsf_fls_sizet(size);
		*sli = tlsf_cast(int, size >> (*fli - SL_INDEX_COUNT_LOG2)) ^ (1 <<
				 SL_INDEX_COUNT_LOG2);
		*fli -= (FL_INDEX_SHIFT - 1);
	}
}

/* This version rounds up to the next block size (for allocations) */
static inline void
mapping_search(size_t size, int *fli, int *sli)
{
	if (size >= SMALL_BLOCK_SIZE) {
		const size_t round = (1 << (tlsf_fls_sizet(size) - SL_INDEX_COUNT_LOG2)) - 1;

		size += round;
	}
	mapping_insert(size, fli, sli);
}

static inline block_header_t *
search_suitable_block(control_t *control, int *fli, int *sli)
{
	/*
	** First, search for a block in the list associated with the given
	** fl/sl index.
	*/
	unsigned int sl_map = control->sl_bitmap[*fli] & (~0U << (*sli));

	if (!sl_map) {
		/* No block exists. Search in the next largest first-level list. */
		const unsigned int fl_map = control->fl_bitmap & (~0U << (*fli + 1));

		if (!fl_map)
			/* No free blocks available, memory has been exhausted. */
			return 0;

		*fli   = tlsf_ffs(fl_map);
		sl_map = control->sl_bitmap[*fli];
	}
	tlsf_assert(sl_map && "internal error - second level bitmap is null");
	*sli = tlsf_ffs(sl_map);

	/* Return the first block in the free list. */
	return tlsf_cast(block_header_t *,
			 tlsf_cast(unsigned char *, control) +
			 control->off_blocks[*fli][*sli]);
}

/* Remove a free block from the free list.*/
static inline void
remove_free_block(control_t *control, block_header_t *block, int fl, int sl)
{
	block_header_t *prev;
	block_header_t *next;

	tlsf_assert(block->off_prev_free && "prev_free field can not be null");
	tlsf_assert(block->off_next_free && "next_free field can not be null");
	prev                = tlsf_cast(block_header_t*, tlsf_cast(unsigned char *, control) +
					block->off_prev_free);
	next                = tlsf_cast(block_header_t*, tlsf_cast(unsigned char *, control) +
					block->off_next_free);
	next->off_prev_free = tlsf_cast(off_t, prev) - tlsf_cast(off_t, control);
	prev->off_next_free = tlsf_cast(off_t, next) - tlsf_cast(off_t, control);

	/* If this block is the head of the free list, set new head. */
	if (control->off_blocks[fl][sl] == (tlsf_cast(off_t, block) - tlsf_cast(off_t, control))) {
		control->off_blocks[fl][sl] = tlsf_cast(off_t, next) - tlsf_cast(off_t, control);

		/* If the new head is null, clear the bitmap. */
		if (next == &control->block_null) {
			control->sl_bitmap[fl] &= ~(1U << sl);

			/* If the second bitmap is now empty, clear the fl bitmap. */
			if (!control->sl_bitmap[fl])
				control->fl_bitmap &= ~(1U << fl);
		}
	}
}

/* Insert a free block into the free block list. */
static inline void
insert_free_block(control_t *control, block_header_t *block, int fl, int sl)
{
	block_header_t *current = tlsf_cast(block_header_t *, tlsf_cast(unsigned char *, control) +
					    control->off_blocks[fl][sl]);

	tlsf_assert(current && "free list cannot have a null entry");
	tlsf_assert(block && "cannot insert a null entry into the free list");
	block->off_next_free   = control->off_blocks[fl][sl];
	block->off_prev_free   = tlsf_cast(off_t, &control->block_null) - tlsf_cast(off_t, control);
	current->off_prev_free = tlsf_cast(off_t, block) - tlsf_cast(off_t, control);

	tlsf_assert(block_to_ptr(block) == align_ptr(block_to_ptr(block), ALIGN_SIZE)
		&& "block not aligned properly");
	/*
	** Insert the new block at the head of the list, and mark the first-
	** and second-level bitmaps appropriately.
	*/
	control->off_blocks[fl][sl] = tlsf_cast(off_t, block) - tlsf_cast(off_t, control);
	control->fl_bitmap |= (1U << fl);
	control->sl_bitmap[fl] |= (1U << sl);
}

/* Remove a given block from the free list. */
static inline void
block_remove(control_t *control, block_header_t *block)
{
	int fl, sl;

	mapping_insert(block_size(block), &fl, &sl);
	remove_free_block(control, block, fl, sl);
}

/* Insert a given block into the free list. */
static inline void
block_insert(control_t *control, block_header_t *block)
{
	int fl, sl;

	mapping_insert(block_size(block), &fl, &sl);
	insert_free_block(control, block, fl, sl);
}

static inline int
block_can_split(block_header_t *block, size_t size)
{
	return block_size(block) >= sizeof(block_header_t) + size;
}

/* Split a block into two, the second of which is free. */
static inline block_header_t *
block_split(control_t *control, block_header_t *block, size_t size)
{
	/* Calculate the amount of space left in the remaining block. */
	block_header_t *remaining   =
		offset_to_block(block_to_ptr(block), size - block_header_overhead);
	const size_t    remain_size = block_size(block) - (size + block_header_overhead);

	tlsf_assert(block_to_ptr(remaining) == align_ptr(block_to_ptr(remaining), ALIGN_SIZE)
		&& "remaining block not aligned properly");

	tlsf_assert(block_size(block) == remain_size + size + block_header_overhead);
	block_set_size(remaining, remain_size);
	tlsf_assert(block_size(remaining) >= block_size_min && "block split with invalid size");

	block_set_size(block, size);
	block_mark_as_free(control, remaining);

	return remaining;
}

/* Absorb a free block's storage into an adjacent previous free block. */
static inline block_header_t *
block_absorb(control_t *control, block_header_t *prev, block_header_t *block)
{
	tlsf_assert(!block_is_last(prev) && "previous block can't be last");
	/* Note: Leaves flags untouched. */
	prev->size += block_size(block) + block_header_overhead;
	block_link_next(control, prev);
	return prev;
}

/* Merge a just-freed block with an adjacent previous free block. */
static inline block_header_t *
block_merge_prev(control_t *control, block_header_t *block)
{
	if (block_is_prev_free(block)) {
		/* Return location of previous block. */
		block_header_t *prev;

		prev = tlsf_cast(block_header_t *,
				 tlsf_cast(unsigned char *, control) + block->off_prev_phys_block);
		tlsf_assert(block_is_free(prev) && "prev block is not free though marked as such");
		block_remove(control, prev);
		block = block_absorb(control, prev, block);
	}

	return block;
}

/* Merge a just-freed block with an adjacent free block. */
static inline block_header_t *
block_merge_next(control_t *control, block_header_t *block)
{
	block_header_t *next = block_next(block);

	tlsf_assert(next && "next physical block can't be null");

	if (block_is_free(next)) {
		tlsf_assert(!block_is_last(block) && "previous block can't be last");
		block_remove(control, next);
		block = block_absorb(control, block, next);
	}

	return block;
}

/* Trim any trailing block space off the end of a block, return to pool. */
static inline void
block_trim_free(control_t *control, block_header_t *block, size_t size)
{
	tlsf_assert(block_is_free(block) && "block must be free");
	if (block_can_split(block, size)) {
		block_header_t *remaining_block = block_split(control, block, size);

		block_link_next(control, block);
		block_set_prev_free(remaining_block);
		block_insert(control, remaining_block);
	}
}

/* Trim any trailing block space off the end of a used block, return to pool. */
static inline void
block_trim_used(control_t *control, block_header_t *block, size_t size)
{
	tlsf_assert(!block_is_free(block) && "block must be used");
	if (block_can_split(block, size)) {
		/* If the next block is free, we must coalesce. */
		block_header_t *remaining_block = block_split(control, block, size);

		block_set_prev_used(remaining_block);

		remaining_block = block_merge_next(control, remaining_block);
		block_insert(control, remaining_block);
	}
}

static inline block_header_t *
block_trim_free_leading(control_t *control, block_header_t *block, size_t size)
{
	block_header_t *remaining_block = block;

	if (block_can_split(block, size)) {
		/* We want the 2nd block. */
		remaining_block = block_split(control, block, size - block_header_overhead);
		block_set_prev_free(remaining_block);
		block_link_next(control, block);
		block_insert(control, block);
	}

	return remaining_block;
}

static inline block_header_t *
block_locate_free(control_t *control, size_t size)
{
	int             fl    = 0;
	int             sl    = 0;
	block_header_t *block = 0;

	if (size) {
		mapping_search(size, &fl, &sl);

		/*
		** mapping_search can futz with the size, so for excessively large sizes it can sometimes wind up
		** with indices that are off the end of the block array.
		** So, we protect against that here, since this is the only callsite of mapping_search.
		** Note that we don't need to check sl, since it comes from a modulo operation that guarantees it's always in range.
		*/
		if (fl < FL_INDEX_COUNT)
			block = search_suitable_block(control, &fl, &sl);
	}

	if (block) {
		tlsf_assert(block_size(block) >= size);
		remove_free_block(control, block, fl, sl);
	}

	return block;
}

static inline void *
block_prepare_used(control_t *control, block_header_t *block, size_t size)
{
	void *p = 0;

	if (block) {
		tlsf_assert(size && "size must be non-zero");
		block_trim_free(control, block, size);
		block_mark_as_used(block);
		p = block_to_ptr(block);
	}
	return p;
}

/* Clear structure and point all empty lists at the null block. */
static inline void
control_construct(control_t *control)
{
	int   i, j;
	off_t off_block_null;

	shm_mutex_init(&control->lock);

	off_block_null                    = offsetof(control_t, block_null);
	control->block_null.off_next_free = off_block_null;
	control->block_null.off_prev_free = off_block_null;

	control->fl_bitmap = 0;
	for (i = 0; i < FL_INDEX_COUNT; ++i) {
		control->sl_bitmap[i] = 0;
		for (j = 0; j < SL_INDEX_COUNT; ++j)
			control->off_blocks[i][j] = off_block_null;
	}
}

/*
** Size of the TLSF structures in a given memory block passed to
** tlsf_create, equal to the size of a control_t
*/
static inline size_t
tlsf_size(void)
{
	return sizeof(control_t);
}

pool_t
tlsf_add_pool(tlsf_t tlsf, void *mem, size_t bytes)
{
	block_header_t *block;
	block_header_t *next;
	const size_t    pool_bytes = align_down(bytes - pool_overhead, ALIGN_SIZE);

	if (((ptrdiff_t)mem % ALIGN_SIZE) != 0) {
		printf("tlsf_add_pool: Memory must be aligned by %u bytes.\n",
			(unsigned int)ALIGN_SIZE);
		return 0;
	}

	if (pool_bytes < block_size_min || pool_bytes > block_size_max) {
		printf("tlsf_add_pool: Memory size must be between 0x%x and 0x%x00 bytes.\n",
			(unsigned int)(pool_overhead + block_size_min),
			(unsigned int)((pool_overhead + block_size_max) / 256));
		return 0;
	}

	/*
	** Create the main free block. Offset the start of the block slightly
	** so that the prev_phys_block field falls outside of the pool -
	** it will never be used.
	*/
	block = offset_to_block(mem, -(tlsfptr_t)block_header_overhead);
	block_set_size(block, pool_bytes);
	block_set_free(block);
	block_set_prev_used(block);
	block_insert(tlsf_cast(control_t*, tlsf), block);

	/* Split the block to create a zero-size sentinel block. */
	next = block_link_next(tlsf_cast(control_t*, tlsf), block);
	block_set_size(next, 0);
	block_set_used(next);
	block_set_prev_free(next);

	return mem;
}

void
tlsf_remove_pool(tlsf_t tlsf, pool_t pool)
{
	control_t      *control = tlsf_cast(control_t *, tlsf);
	block_header_t *block   = offset_to_block(pool, -(int)block_header_overhead);
	int             fl      = 0;
	int             sl      = 0;

	tlsf_assert(block_is_free(block) && "block should be free");
	tlsf_assert(!block_is_free(block_next(block)) && "next block should not be free");
	tlsf_assert(block_size(block_next(block)) == 0 && "next block size should be zero");

	mapping_insert(block_size(block), &fl, &sl);
	remove_free_block(control, block, fl, sl);
}

/*
** TLSF main interface.
*/

#if _DEBUG
static int
test_ffs_fls()
{
	/* Verify ffs/fls work properly. */
	int rv = 0;

	rv += (tlsf_ffs(0) == -1) ? 0 : 0x1;
	rv += (tlsf_fls(0) == -1) ? 0 : 0x2;
	rv += (tlsf_ffs(1) == 0) ? 0 : 0x4;
	rv += (tlsf_fls(1) == 0) ? 0 : 0x8;
	rv += (tlsf_ffs(0x80000000) == 31) ? 0 : 0x10;
	rv += (tlsf_ffs(0x80008000) == 15) ? 0 : 0x20;
	rv += (tlsf_fls(0x80000008) == 31) ? 0 : 0x40;
	rv += (tlsf_fls(0x7FFFFFFF) == 30) ? 0 : 0x80;

	rv += (tlsf_fls_sizet(0x80000000) == 31) ? 0 : 0x100;
	rv += (tlsf_fls_sizet(0x100000000) == 32) ? 0 : 0x200;
	rv += (tlsf_fls_sizet(0xffffffffffffffff) == 63) ? 0 : 0x400;

	if (rv)
		printf("test_ffs_fls: %x ffs/fls tests failed.\n", rv);
	return rv;
}
#endif

tlsf_t
tlsf_create(void *mem)
{
#if _DEBUG
	if (test_ffs_fls())
		return 0;
#endif

	if (((tlsfptr_t)mem % ALIGN_SIZE) != 0) {
		printf("tlsf_create: Memory must be aligned to %u bytes.\n",
			(unsigned int)ALIGN_SIZE);
		return 0;
	}

	control_construct(tlsf_cast(control_t*, mem));

	return tlsf_cast(tlsf_t, mem);
}

tlsf_t
tlsf_create_with_pool(void *mem, size_t bytes)
{
	tlsf_t tlsf = tlsf_create(mem);

	tlsf_add_pool(tlsf, (char*)mem + tlsf_size(), bytes - tlsf_size());
	return tlsf;
}

void *
tlsf_malloc(tlsf_t tlsf, size_t size)
{
	void           *buf;
	control_t      *control = tlsf_cast(control_t *, tlsf);
	block_header_t *block;
	const size_t    adjust  = adjust_request_size(size, ALIGN_SIZE);

	shm_mutex_lock(&control->lock, NULL);
	block = block_locate_free(control, adjust);
	buf   = block_prepare_used(control, block, adjust);
	shm_mutex_unlock(&control->lock);
	return buf;
}

static inline void *
tlsf_malloc_nolock(tlsf_t tlsf, size_t size)
{
	control_t      *control = tlsf_cast(control_t *, tlsf);
	const size_t    adjust  = adjust_request_size(size, ALIGN_SIZE);
	block_header_t *block   = block_locate_free(control, adjust);

	return block_prepare_used(control, block, adjust);
}

void *
tlsf_memalign(tlsf_t tlsf, size_t align, size_t size)
{
	void        *buf;
	control_t   *control = tlsf_cast(control_t *, tlsf);
	const size_t adjust  = adjust_request_size(size, ALIGN_SIZE);

	/*
	** We must allocate an additional minimum block size bytes so that if
	** our free block will leave an alignment gap which is smaller, we can
	** trim a leading free block and release it back to the pool. We must
	** do this because the previous physical block is in use, therefore
	** the prev_phys_block field is not valid, and we can't simply adjust
	** the size of that block.
	*/
	const size_t gap_minimum   = sizeof(block_header_t);
	const size_t size_with_gap = adjust_request_size(adjust + align + gap_minimum, align);

	/*
	** If alignment is less than or equals base alignment, we're done.
	** If we requested 0 bytes, return null, as tlsf_malloc(0) does.
	*/
	const size_t    aligned_size = (adjust && align > ALIGN_SIZE) ? size_with_gap : adjust;
	block_header_t *block;
	void           *ptr, *aligned;
	size_t          gap;

	shm_mutex_lock(&control->lock, NULL);
	block = block_locate_free(control, aligned_size);
	if (block == NULL) {
		shm_mutex_unlock(&control->lock);
		return NULL;
	}

	/* This can't be a static assert. */
	tlsf_assert(sizeof(block_header_t) == block_size_min + block_header_overhead);

	ptr     = block_to_ptr(block);
	aligned = align_ptr(ptr, align);
	gap     = tlsf_cast(size_t, tlsf_cast(tlsfptr_t, aligned) - tlsf_cast(tlsfptr_t, ptr));

	/* If gap size is too small, offset to next aligned boundary. */
	if (gap && gap < gap_minimum) {
		const size_t gap_remain   = gap_minimum - gap;
		const size_t offset       = tlsf_max(gap_remain, align);
		const void  *next_aligned = tlsf_cast(void*,
			tlsf_cast(tlsfptr_t, aligned) + offset);

		aligned = align_ptr(next_aligned, align);
		gap = tlsf_cast(size_t, tlsf_cast(tlsfptr_t, aligned) - tlsf_cast(tlsfptr_t, ptr));
	}

	if (gap) {
		tlsf_assert(gap >= gap_minimum && "gap size too small");
		block = block_trim_free_leading(control, block, gap);
	}

	buf = block_prepare_used(control, block, adjust);
	shm_mutex_unlock(&control->lock);
	return buf;
}

static inline void
tlsf_free_nolock(tlsf_t tlsf, void *ptr)
{
	control_t      *control = tlsf_cast(control_t *, tlsf);
	block_header_t *block   = block_from_ptr(ptr);

	tlsf_assert(!block_is_free(block) && "block already marked as free");
	block_mark_as_free(control, block);
	block = block_merge_prev(control, block);
	block = block_merge_next(control, block);
	block_insert(control, block);
}

void
tlsf_free(tlsf_t tlsf, void *ptr)
{
	/* Don't attempt to free a NULL pointer. */
	if (ptr) {
		control_t *control = tlsf_cast(control_t *, tlsf);

		shm_mutex_lock(&control->lock, NULL);
		tlsf_free_nolock(tlsf, ptr);
		shm_mutex_unlock(&control->lock);
	}
}

/*
** The TLSF block information provides us with enough information to
** provide a reasonably intelligent implementation of realloc, growing or
** shrinking the currently allocated block as required.
**
** This routine handles the somewhat esoteric edge cases of realloc:
** - a non-zero size with a null pointer will behave like malloc
** - a zero size with a non-null pointer will behave like free
** - a request that cannot be satisfied will leave the original buffer
**   untouched
** - an extended buffer size will leave the newly-allocated area with
**   contents undefined
*/
void *
tlsf_realloc(tlsf_t tlsf, void *ptr, size_t size)
{
	control_t      *control = tlsf_cast(control_t *, tlsf);
	void           *p       = 0;
	block_header_t *block   = block_from_ptr(ptr);
	block_header_t *next;
	size_t          cursize, combined, adjust;

	/* Zero-size requests are treated as free. */
	if (ptr && size == 0) {
		tlsf_free(tlsf, ptr);
		return p;
	} else if (!ptr) {
		/* Requests with NULL pointers are treated as malloc. */
		p = tlsf_malloc(tlsf, size);
		return p;
	}

	shm_mutex_lock(&control->lock, NULL);

	next     = block_next(block);
	cursize  = block_size(block);
	combined = cursize + block_size(next) + block_header_overhead;
	adjust   = adjust_request_size(size, ALIGN_SIZE);
	tlsf_assert(!block_is_free(block) && "block already marked as free");

	/*
	** If the next block is used, or when combined with the current
	** block, does not offer enough space, we must reallocate and copy.
	*/
	if (adjust > cursize && (!block_is_free(next) || adjust > combined)) {
		p = tlsf_malloc_nolock(tlsf, size);
		if (p) {
			const size_t minsize = tlsf_min(cursize, size);
			memcpy(p, ptr, minsize);
			tlsf_free_nolock(tlsf, ptr);
		}
	} else {
		/* Do we need to expand to the next block? */
		if (adjust > cursize) {
			block_merge_next(control, block);
			block_mark_as_used(block);
		}

		/* Trim the resulting block and return the original pointer. */
		block_trim_used(control, block, adjust);
		p = ptr;
	}

	shm_mutex_unlock(&control->lock);

	return p;
}
