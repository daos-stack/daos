/*
 * Copyright(c) 2019-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "../../ocf_priv.h"

#include "nhit_hash.h"

/* Implementation of hashmap-ish structure for tracking core lines in nhit
 * promotion policy. It consists of two arrays:
 * 	- hash_map - indexed by hash formed from core id and core lba pairs,
 * 	contains pointers (indices) to the ring buffer. Each index in this array
 * 	has its own rwsem.
 * 	- ring_buffer - contains per-coreline metadata and collision info for
 * 	open addressing. If we run out of space in this array, we just loop around
 * 	and insert elements from the beggining. So lifetime of a core line varies
 * 	depending on insertion and removal rate.
 *
 * and rb_pointer which is index to ring_buffer element that is going to be used
 * for next insertion.
 *
 * Operations:
 * 	- query(core_id, core_lba):
 *		Check if core line is present in structure, bump up counter and
 *		return its value.
 *
 * 	- insertion(core_id, core_lba):
 * 		Insert new core line into structure
 * 		1. get new slot from ring buffer
 * 			a. check if current slot under rb_pointer is valid
 * 			and if not - exit
 * 			b. set current slot as invalid and increment rb_pointer
 * 		2. lock hash bucket for new item and for ring buffer slot
 * 		(if non-empty) in ascending bucket id order (to avoid deadlock)
 * 		3. insert new data, add to collision
 * 		4. unlock both hash buckets
 * 		5. commit rb_slot (mark it as valid)
 *
 * Insertion explained visually:
 *
 * Suppose that we want to add a new core line with hash value H which already has
 * some colliding core lines
 *
 *		hash(core_id, core_lba)
 *				 +
 *				 |
 *				 v
 *	+--+--+--+--+--+--+--+--++-+--+
 *	|  |  |I |  |  |  |  |  |H |  | hash_map
 *	+--+--++-+--+--+--+--+--++-+--+
 *	     __|   rb_pointer    |        _______
 *	    |        +           |        |     |
 *	    v        v           v        |     v
 *	+--++-+--+---+-+--+--+--++-+--+--++-+--++-+
 *	|  |  |  |  |X |  |  |  |  |  |  |  |  |  | ring_buffer
 *	+--++-+--+---+-+--+--+--++-+--+--++-+--+--+
 *	    |        ^           |        ^
 *	    |________|           |________|
 *
 * We will attempt to insert new element at rb_pointer. Since rb_pointer is
 * pointing to occupied rb slot we need to write-lock hash bucket I associated
 * with this slot and remove it from collision list. We've gained an empty slot
 * and we use slot X for new hash H entry.
 *
 *	+--+--+--+--+--+--+--+--+--+--+
 *	|  |  |I |  |  |  |  |  |H |  | hash_map
 *	+--+--++-+--+--+--+--+--++-+--+
 *	     __|   rb_pointer    |        _______
 *	    |           +        |        |     |
 *	    v           v        v        |     v
 *	+--++-+--+-----++-+--+--++-+--+--++-+--++-+
 *	|  |  |  |  |X |  |  |  |  |  |  |  |  |  | ring_buffer
 *	+--+--+--+---+-+--+--+--++-+--+--++-+--++-+
 *		     ^           |        ^     |
 *		     |           |________|     |
 *		     |__________________________|
 *
 * Valid field in nhit_list_elem is guarded by rb_pointer_lock to make sure we
 * won't try to use the same slot in two threads. That would be possible if in
 * time between removal from collision and insertion into the new one the
 * rb_pointer would go around the whole structure (likeliness depends on size of
 * ring_buffer).
 */

#define HASH_PRIME 4099

struct nhit_list_elem {
	/* Fields are ordered for memory efficiency, not for looks. */
	uint64_t core_lba;
	env_atomic counter;
	ocf_cache_line_t coll_prev;
	ocf_cache_line_t coll_next;
	ocf_core_id_t core_id;
	bool valid;
};

struct nhit_hash {
	env_spinlock rb_pointer_lock;
	ocf_cache_line_t hash_entries;
	uint64_t rb_entries;

	ocf_cache_line_t *hash_map;
	env_rwsem *hash_locks;

	struct nhit_list_elem *ring_buffer;
	uint64_t rb_pointer;
};

static uint64_t calculate_hash_buckets(uint64_t hash_size)
{
	return OCF_DIV_ROUND_UP(hash_size / 4, HASH_PRIME) * HASH_PRIME - 1;
}

uint64_t nhit_hash_sizeof(uint64_t hash_size)
{
	uint64_t size = 0;
	uint64_t n_buckets = calculate_hash_buckets(hash_size);

	size += sizeof(struct nhit_hash);

	size += n_buckets * sizeof(ocf_cache_line_t);
	size += n_buckets * sizeof(env_rwsem);

	size += hash_size * sizeof(struct nhit_list_elem);

	return size;
}

ocf_error_t nhit_hash_init(uint64_t hash_size, nhit_hash_t *ctx)
{
	int result = 0;
	struct nhit_hash *new_ctx;
	uint32_t i;
	int64_t i_locks;

	new_ctx = env_vzalloc(sizeof(*new_ctx));
	if (!new_ctx) {
		result = -OCF_ERR_NO_MEM;
		goto exit;
	}

	new_ctx->rb_entries = hash_size;
	new_ctx->hash_entries = calculate_hash_buckets(hash_size);

	new_ctx->hash_map = env_vzalloc(
			new_ctx->hash_entries * sizeof(*new_ctx->hash_map));
	if (!new_ctx->hash_map) {
		result = -OCF_ERR_NO_MEM;
		goto dealloc_ctx;
	}
	for (i = 0; i < new_ctx->hash_entries; i++)
		new_ctx->hash_map[i] = new_ctx->rb_entries;

	new_ctx->hash_locks = env_vzalloc(
			new_ctx->hash_entries * sizeof(*new_ctx->hash_locks));
	if (!new_ctx->hash_locks) {
		result = -OCF_ERR_NO_MEM;
		goto dealloc_hash;
	}

	for (i_locks = 0; i_locks < new_ctx->hash_entries; i_locks++) {
		if (env_rwsem_init(&new_ctx->hash_locks[i_locks])) {
			result = -OCF_ERR_UNKNOWN;
			goto dealloc_locks;
		}
	}

	new_ctx->ring_buffer = env_vzalloc(
			new_ctx->rb_entries * sizeof(*new_ctx->ring_buffer));
	if (!new_ctx->ring_buffer) {
		result = -OCF_ERR_NO_MEM;
		goto dealloc_locks;
	}
	for (i = 0; i < new_ctx->rb_entries; i++) {
		new_ctx->ring_buffer[i].core_id = OCF_CORE_ID_INVALID;
		new_ctx->ring_buffer[i].valid = true;
		env_atomic_set(&new_ctx->ring_buffer[i].counter, 0);
	}

	result = env_spinlock_init(&new_ctx->rb_pointer_lock);
	if (result)
		goto dealloc_locks;

	new_ctx->rb_pointer = 0;

	*ctx = new_ctx;
	return 0;

dealloc_locks:
	while (i_locks--)
		ENV_BUG_ON(env_rwsem_destroy(&new_ctx->hash_locks[i_locks]));
	env_vfree(new_ctx->hash_locks);
dealloc_hash:
	env_vfree(new_ctx->hash_map);
dealloc_ctx:
	env_vfree(new_ctx);
exit:
	return result;
}

void nhit_hash_deinit(nhit_hash_t ctx)
{
	ocf_cache_line_t i;

	env_spinlock_destroy(&ctx->rb_pointer_lock);
	for (i = 0; i < ctx->hash_entries; i++)
		ENV_BUG_ON(env_rwsem_destroy(&ctx->hash_locks[i]));

	env_vfree(ctx->ring_buffer);
	env_vfree(ctx->hash_locks);
	env_vfree(ctx->hash_map);
	env_vfree(ctx);
}

static ocf_cache_line_t hash_function(ocf_core_id_t core_id, uint64_t core_lba,
		uint64_t limit)
{
	if (core_id == OCF_CORE_ID_INVALID)
		return limit;

	return (ocf_cache_line_t) ((core_lba * HASH_PRIME + core_id) % limit);
}

static ocf_cache_line_t core_line_lookup(nhit_hash_t ctx,
		ocf_core_id_t core_id, uint64_t core_lba)
{
	ocf_cache_line_t hash = hash_function(core_id, core_lba,
			ctx->hash_entries);
	ocf_cache_line_t needle = ctx->rb_entries;
	ocf_cache_line_t cur;

	for (cur = ctx->hash_map[hash]; cur != ctx->rb_entries;
			cur = ctx->ring_buffer[cur].coll_next) {
		struct nhit_list_elem *cur_elem = &ctx->ring_buffer[cur];

		if (cur_elem->core_lba == core_lba &&
				cur_elem->core_id == core_id) {
			needle = cur;
			break;
		}
	}

	return needle;
}

static inline bool get_rb_slot(nhit_hash_t ctx, uint64_t *slot)
{
	bool result = true;

	OCF_CHECK_NULL(slot);

	env_spinlock_lock(&ctx->rb_pointer_lock);

	*slot = ctx->rb_pointer;
	result = ctx->ring_buffer[*slot].valid;

	ctx->ring_buffer[*slot].valid = false;

	ctx->rb_pointer = (*slot + 1) % ctx->rb_entries;

	env_spinlock_unlock(&ctx->rb_pointer_lock);

	return result;
}

static inline void commit_rb_slot(nhit_hash_t ctx, uint64_t slot)
{
	env_spinlock_lock(&ctx->rb_pointer_lock);

	ctx->ring_buffer[slot].valid = true;

	env_spinlock_unlock(&ctx->rb_pointer_lock);
}

static void collision_remove(nhit_hash_t ctx, uint64_t slot_id)
{
	struct nhit_list_elem *slot = &ctx->ring_buffer[slot_id];
	ocf_cache_line_t hash = hash_function(slot->core_id, slot->core_lba,
			ctx->hash_entries);

	if (slot->core_id == OCF_CORE_ID_INVALID)
		return;

	slot->core_id = OCF_CORE_ID_INVALID;

	if (slot->coll_prev != ctx->rb_entries)
		ctx->ring_buffer[slot->coll_prev].coll_next = slot->coll_next;

	if (slot->coll_next != ctx->rb_entries)
		ctx->ring_buffer[slot->coll_next].coll_prev = slot->coll_prev;

	if (ctx->hash_map[hash] == slot_id)
		ctx->hash_map[hash] = slot->coll_next;
}

static void collision_insert_new(nhit_hash_t ctx,
		uint64_t slot_id, ocf_core_id_t core_id,
		uint64_t core_lba)
{
	ocf_cache_line_t hash = hash_function(core_id, core_lba,
			ctx->hash_entries);
	struct nhit_list_elem *slot = &ctx->ring_buffer[slot_id];

	slot->core_id = core_id;
	slot->core_lba = core_lba;
	slot->coll_next = ctx->hash_map[hash];
	slot->coll_prev = ctx->rb_entries;
	env_atomic_set(&slot->counter, 1);

	if (ctx->hash_map[hash] != ctx->rb_entries)
		ctx->ring_buffer[ctx->hash_map[hash]].coll_prev = slot_id;

	ctx->hash_map[hash] = slot_id;
}

static inline void write_lock_hashes(nhit_hash_t ctx, ocf_core_id_t core_id1,
		uint64_t core_lba1, ocf_core_id_t core_id2, uint64_t core_lba2)
{
	ocf_cache_line_t hash1 = hash_function(core_id1, core_lba1,
			ctx->hash_entries);
	ocf_cache_line_t hash2 = hash_function(core_id2, core_lba2,
			ctx->hash_entries);
	ocf_cache_line_t lock_order[2] = {
		OCF_MIN(hash1, hash2),
		OCF_MAX(hash1, hash2)};

	if (lock_order[0] != ctx->hash_entries)
		env_rwsem_down_write(&ctx->hash_locks[lock_order[0]]);

	if ((lock_order[1] != ctx->hash_entries) && (lock_order[0] != lock_order[1]))
		env_rwsem_down_write(&ctx->hash_locks[lock_order[1]]);
}

static inline void write_unlock_hashes(nhit_hash_t ctx, ocf_core_id_t core_id1,
		uint64_t core_lba1, ocf_core_id_t core_id2, uint64_t core_lba2)
{
	ocf_cache_line_t hash1 = hash_function(core_id1, core_lba1,
			ctx->hash_entries);
	ocf_cache_line_t hash2 = hash_function(core_id2, core_lba2,
			ctx->hash_entries);

	if (hash1 != ctx->hash_entries)
		env_rwsem_up_write(&ctx->hash_locks[hash1]);

	if ((hash2 != ctx->hash_entries) && (hash1 != hash2))
		env_rwsem_up_write(&ctx->hash_locks[hash2]);
}

void nhit_hash_insert(nhit_hash_t ctx, ocf_core_id_t core_id, uint64_t core_lba)
{
	uint64_t slot_id;
	struct nhit_list_elem *slot;
	ocf_core_id_t slot_core_id;
	uint64_t slot_core_lba;

	if (!get_rb_slot(ctx, &slot_id))
		return;

	slot = &ctx->ring_buffer[slot_id];
	slot_core_id = slot->core_id;
	slot_core_lba = slot->core_lba;

	write_lock_hashes(ctx, core_id, core_lba, slot_core_id, slot_core_lba);

	collision_remove(ctx, slot_id);
	collision_insert_new(ctx, slot_id, core_id, core_lba);

	write_unlock_hashes(ctx, core_id, core_lba, slot_core_id, slot_core_lba);

	commit_rb_slot(ctx, slot_id);
}

bool nhit_hash_query(nhit_hash_t ctx, ocf_core_id_t core_id, uint64_t core_lba,
		int32_t *counter)
{
	ocf_cache_line_t hash = hash_function(core_id, core_lba,
			ctx->hash_entries);
	uint64_t rb_idx;

	OCF_CHECK_NULL(counter);

	env_rwsem_down_read(&ctx->hash_locks[hash]);
	rb_idx = core_line_lookup(ctx, core_id, core_lba);

	if (rb_idx == ctx->rb_entries) {
		env_rwsem_up_read(&ctx->hash_locks[hash]);
		return false;
	}

	*counter = env_atomic_inc_return(&ctx->ring_buffer[rb_idx].counter);

	env_rwsem_up_read(&ctx->hash_locks[hash]);

	return true;
}

void nhit_hash_set_occurences(nhit_hash_t ctx, ocf_core_id_t core_id,
		uint64_t core_lba, int32_t occurences)
{
	ocf_cache_line_t hash = hash_function(core_id, core_lba,
			ctx->hash_entries);
	uint64_t rb_idx;

	env_rwsem_down_read(&ctx->hash_locks[hash]);
	rb_idx = core_line_lookup(ctx, core_id, core_lba);

	if (rb_idx == ctx->rb_entries) {
		env_rwsem_up_read(&ctx->hash_locks[hash]);
		return;
	}

	env_atomic_set(&ctx->ring_buffer[rb_idx].counter, occurences);

	env_rwsem_up_read(&ctx->hash_locks[hash]);
}

