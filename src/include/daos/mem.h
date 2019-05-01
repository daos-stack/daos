/**
 * (C) Copyright 2016-2019 Intel Corporation.
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
/**
 * This file is part of daos
 *
 * src/include/daos/daos_mem.h
 */

#ifndef __DAOS_MEM_H__
#define __DAOS_MEM_H__

#include <daos_types.h>
#include <daos/common.h>

/**
 * Terminologies:
 *
 * pmem		Persistent Memory
 * vmem		Volatile Memory
 *
 * umem		Unified memory abstraction
 * ummid	Unified Memory ID
 */

#include <libpmemobj.h>

/** The offset of an object from the base address of the pool */
typedef uint64_t		umem_off_t;
/** Number of flag bits to reserve for encoding extra information in
 *  a NULL umem_off_t entry.
 */
#define UMOFF_NUM_BITS		(8)
/** The absolute value of a flag mask must be <= this value */
#define UMOFF_MAX_FLAG		(1ULL << UMOFF_NUM_BITS)
/** In theory and offset can be NULL but in practice, pmemobj_root
 *  is not at offset 0 as pmdk reserves some space for its internal
 *  use.   So, use 0 for NULL.   Invalid bits are also considered
 *  NULL.
 */
#define UMOFF_NULL		(0ULL)
/** Check for a NULL value including possible invalid flag bits */
#define UMOFF_IS_NULL(umoff)	(umoff < UMOFF_MAX_FLAG)

/** Up to UMOFF_NUM_BITS bits are available to the user to mark an
 *  invalid offset with extra information.  If these fields are
 *  set, the pointer resulting from such an offset will be
 *  considered NULL.
 *
 *  \param	offset[IN,OUT]	The value is marked invalid with the flags
 *  \param	flags[IN]	Auxilliarly information about the invalid entry
 */
static inline void
umem_off_set_invalid(umem_off_t *offset, uint64_t flags)
{
	D_ASSERTF(flags < UMOFF_MAX_FLAG,
		  "Attempt to set invalid flag bits on umem_off_t\n");
	*offset = flags;
}

/** Retrieves any invalid flags that are set.  If the offset hasn't
 *  been set to invalid, 0 is returned.
 *
 *  \param	offset[IN]	The value from which to get flags
 */
static inline uint64_t
umem_off_get_invalid_flags(umem_off_t umoff)
{
	if (!UMOFF_IS_NULL(umoff))
		return 0;

	return umoff;
}

/** memory ID without type */
typedef PMEMoid			umem_id_t;
#define UMMID_NULL		OID_NULL
#define UMMID_IS_NULL(ummid)	OID_IS_NULL(ummid)

int umem_tx_errno(int err);

/* print format of ummid */
#define UMMID_PF		DF_X64
#define UMMID_P(id)		((id).off)

typedef enum {
	/** volatile memory */
	UMEM_CLASS_VMEM,
	/** persistent memory */
	UMEM_CLASS_PMEM,
	/** persistent memory but ignore PMDK snapshot */
	UMEM_CLASS_PMEM_NO_SNAP,
	/** unknown */
	UMEM_CLASS_UNKNOWN,
} umem_class_id_t;

struct umem_instance;

typedef void (*umem_tx_cb_t)(void *data, bool noop);

#define UMEM_TX_DATA_MAGIC	(0xc01df00d)
#define UMEM_TX_CB_SHIFT_MAX	20	/* 1m callbacks */
#define UMEM_TX_CB_SHIFT_INIT	5	/* 32 callbacks */

struct umem_tx_stage_item;

/**
 * data structure to store all pmem transaction stage callbacks.
 * See pmemobj_tx_begin and pmemobj_tx_end of PMDK for the details.
 */
struct umem_tx_stage_data {
	int				 txd_magic;
	unsigned int			 txd_commit_cnt;
	unsigned int			 txd_commit_max;
	unsigned int			 txd_abort_cnt;
	unsigned int			 txd_abort_max;
	unsigned int			 txd_end_cnt;
	unsigned int			 txd_end_max;
	struct umem_tx_stage_item	*txd_commit_vec;
	struct umem_tx_stage_item	*txd_abort_vec;
	struct umem_tx_stage_item	*txd_end_vec;
};

/** Initialize \a txd which is for attaching pmem transaction stage callbacks */
int  umem_init_txd(struct umem_tx_stage_data *txd);
/** Finalize the txd initialized by \a umem_init_txd */
void umem_fini_txd(struct umem_tx_stage_data *txd);

typedef struct {
	/** convert directly accessible address to ummid */
	umem_id_t	 (*mo_id)(const struct umem_instance *umm, void *addr);
	/** convert ummid to directly accessible address */
	void		*(*mo_addr)(const struct umem_instance *umm,
				    umem_id_t ummid);
	/** check if two ummid are equal */
	bool		 (*mo_equal)(struct umem_instance *umm,
				     umem_id_t ummid1, umem_id_t ummid2);
	/** free ummid */
	int		 (*mo_tx_free)(struct umem_instance *umm,
				       umem_id_t ummid);
	/**
	 * allocate ummid with the specified size & flags
	 *
	 * \param umm	[IN]	umem class instance.
	 * \param size	[IN]	size to allocate.
	 * \param flags	[IN]	flags like zeroing, noflush (for PMDK)
	 * \param type_num [IN]	struct type (for PMDK)
	 */
	umem_id_t	 (*mo_tx_alloc)(struct umem_instance *umm, size_t size,
					uint64_t flags, unsigned int type_num);
	/**
	 * Add the specified range of ummid to current memory transaction.
	 *
	 * \param umm	[IN]	umem class instance.
	 * \param ummid	[IN]	memory ID to be added to transaction.
	 * \param offset [IN]	start offset of \a ummid tracked by the
	 *			transaction.
	 * \param size	[IN]	size of \a ummid tracked by the transaction.
	 */
	int		 (*mo_tx_add)(struct umem_instance *umm,
				      umem_id_t ummid, uint64_t offset,
				      size_t size);
	/**
	 * Add the directly accessible pointer to current memory transaction.
	 *
	 * \param umm	[IN]	umem class instance.
	 * \param ptr [IN]	Directly accessible memory pointer.
	 * \param size	[IN]	size to be tracked by the transaction.
	 */
	int		 (*mo_tx_add_ptr)(struct umem_instance *umm,
					  void *ptr, size_t size);
	/** abort memory transaction */
	int		 (*mo_tx_abort)(struct umem_instance *umm, int error);
	/** start memory transaction */
	int		 (*mo_tx_begin)(struct umem_instance *umm,
					struct umem_tx_stage_data *txd);
	/** commit memory transaction */
	int		 (*mo_tx_commit)(struct umem_instance *umm);

	/**
	 * Reserve space with specified size.
	 *
	 * \param umm	[IN]		umem class instance.
	 * \param act	[IN|OUT]	action used for later cancel/publish.
	 * \param size	[IN]		size to be reserved.
	 * \param type_num [IN]		struct type (for PMDK)
	 */
	umem_id_t	 (*mo_reserve)(struct umem_instance *umm,
				       struct pobj_action *act, size_t size,
				       unsigned int type_num);

	/**
	 * Cancel the reservation.
	 *
	 * \param umm	[IN]	umem class instance.
	 * \param actv	[IN]	action array to be canceled.
	 * \param actv_cnt [IN]	size of action array.
	 */
	void		 (*mo_cancel)(struct umem_instance *umm,
				      struct pobj_action *actv, int actv_cnt);

	/**
	 * Publish the reservation (make it persistent).
	 *
	 * \param umm	[IN]	umem class instance.
	 * \param actv	[IN]	action array to be published.
	 * \param actv_cnt [IN]	size of action array.
	 */
	int		 (*mo_tx_publish)(struct umem_instance *umm,
					  struct pobj_action *actv,
					  int actv_cnt);

	/**
	 * Add one commit or abort callback to current transaction.
	 *
	 * PMDK doesn't provide public API to get stage_callback_arg, so
	 * we have to make @txd as an input parameter.
	 *
	 * \param umm	[IN]	umem class instance.
	 * \param txd	[IN]	transaction stage data.
	 * \param cb	[IN]	commit or abort callback.
	 * \param data	[IN]	callback data.
	 */
	int		 (*mo_tx_add_callback)(struct umem_instance *umm,
					       struct umem_tx_stage_data *txd,
					       int stage, umem_tx_cb_t cb,
					       void *data);
} umem_ops_t;

/** attributes to initialise an unified memroy class */
struct umem_attr {
	umem_class_id_t		 uma_id;
	PMEMobjpool		*uma_pool;
};

/** instance of an unified memory class */
struct umem_instance {
	umem_class_id_t		 umm_id;
	const char		*umm_name;
	PMEMobjpool		*umm_pool;
	/** Cache the pool id field for umem addresses */
	uint64_t		 umm_pool_uuid_lo;
	/** Cache the base address of the pool */
	uint64_t		 umm_base;
	/** class member functions */
	umem_ops_t		*umm_ops;
};

int  umem_class_init(struct umem_attr *uma, struct umem_instance *umm);
void umem_attr_get(struct umem_instance *umm, struct umem_attr *uma);

static inline void *
umem_id2ptr(const struct umem_instance *umm, umem_id_t ummid)
{
	return umm->umm_ops->mo_addr(umm, ummid);
}

static inline umem_id_t
umem_ptr2id(const struct umem_instance *umm, void *ptr)
{
	return umm->umm_ops->mo_id(umm, ptr);
}

/** Convert an offset to an id.   No invalid flags will be maintained
 *  in the conversion.
 *
 *  \param	umm[IN]		The umem pool instance
 *  \param	umoff[in]	The offset to convert
 *
 *  \return	The mmid.
 */
static inline umem_id_t
umem_off2id(const struct umem_instance *umm, umem_off_t umoff)
{
	umem_id_t	ummid;

	if (UMOFF_IS_NULL(umoff))
		return UMMID_NULL;

	ummid.pool_uuid_lo = umm->umm_pool_uuid_lo;
	ummid.off = umoff;

	return ummid;
}

/** Convert an id to an offset.
 *
 *  \param	umm[IN]		The umem pool instance
 *  \param	ummid[in]	The mmid to convert
 *
 *  \return	The offset in the PMEM pool.
 */
static inline umem_off_t
umem_id2off(const struct umem_instance *umm, umem_id_t ummid)
{
	if (UMMID_IS_NULL(ummid))
		return UMOFF_NULL;

	D_ASSERT(!UMOFF_IS_NULL(ummid.off));

	return ummid.off;
}

/** Convert an offset to pointer.
 *
 *  \param	umm[IN]		The umem pool instance
 *  \param	umoff[in]	The offset to convert
 *
 *  \return	The address in memory
 */
static inline void *
umem_off2ptr(const struct umem_instance *umm, umem_off_t umoff)
{
	if (UMOFF_IS_NULL(umoff))
		return NULL;

	return (void *)(umm->umm_base + umoff);
}

/** Convert pointer to an offset.
 *
 *  \param	umm[IN]		The umem pool instance
 *  \param	ptr[in]		The direct pointer to convert
 *
 *  Returns the umem offset
 */
static inline umem_off_t
umem_ptr2off(const struct umem_instance *umm, void *ptr)
{
	if (ptr == NULL)
		return UMOFF_NULL;

	return (umem_off_t)ptr - umm->umm_base;
}

/**
 * Get pmemobj pool uuid
 *
 * \param	umm[IN]	The umem pool instance
 */
static inline uint64_t
umem_get_uuid(const struct umem_instance *umm)
{
	return umm->umm_pool_uuid_lo;
}

enum {
	UMEM_TYPE_ANY,
};

static inline bool
umem_has_tx(struct umem_instance *umm)
{
	return umm->umm_ops->mo_tx_add != NULL;
}

#define umem_alloc_verb(umm, flags, size)				\
({									\
	umem_id_t	__ummid;					\
									\
	__ummid = (umm)->umm_ops->mo_tx_alloc(umm, size, flags,		\
					      UMEM_TYPE_ANY);		\
	D_ASSERT(__ummid.pool_uuid_lo == (umm)->umm_pool_uuid_lo);	\
	D_ASSERT(!UMOFF_IS_NULL(__ummid.off));				\
	D_DEBUG(DB_MEM, "allocate %s mmid "UMMID_PF" size %zu\n",	\
		(umm)->umm_name, UMMID_P(__ummid), (size_t)(size));	\
	__ummid;							\
})

#define umem_alloc(umm, size)						\
	umem_alloc_verb(umm, 0, size)

#define umem_zalloc(umm, size)						\
	umem_alloc_verb(umm, POBJ_FLAG_ZERO, size)

#define umem_alloc_noflush(umm, size)					\
	umem_alloc_verb(umm, POBJ_FLAG_NO_FLUSH, size)

#define umem_alloc_off_verb(umm, flags, size)				\
({									\
	umem_id_t	___ummid;					\
									\
	___ummid = umem_alloc_verb(umm, flags, size);			\
	umem_id2off(umm, ___ummid);					\
})

#define umem_alloc_off(umm, size)					\
	umem_alloc_off_verb(umm, 0, size)

#define umem_zalloc_off(umm, size)					\
	umem_alloc_off_verb(umm, POBJ_FLAG_ZERO, size)

#define umem_alloc_off_noflush(umm, size)				\
	umem_alloc_off_verb(umm, POBJ_FLAG_NO_FLUSH, size)

#define umem_free(umm, ummid)						\
({									\
	D_DEBUG(DB_MEM, "Free %s mmid "UMMID_PF"\n",			\
		(umm)->umm_name, UMMID_P(ummid));			\
									\
	(umm)->umm_ops->mo_tx_free(umm, ummid);				\
})

#define umem_free_off(umm, umoff)					\
	umem_free(umm, umem_off2id(umm, umoff))

static inline int
umem_tx_add_range(struct umem_instance *umm, umem_id_t ummid, uint64_t offset,
		  size_t size)
{
	if (umm->umm_ops->mo_tx_add)
		return umm->umm_ops->mo_tx_add(umm, ummid, offset, size);
	else
		return 0;
}

static inline int
umem_tx_add_ptr(struct umem_instance *umm, void *ptr, size_t size)
{
	if (umm->umm_ops->mo_tx_add_ptr)
		return umm->umm_ops->mo_tx_add_ptr(umm, ptr, size);
	else
		return 0;
}

#define umem_tx_add(umm, ummid, size)					\
	umem_tx_add_range(umm, ummid, 0, size)

#define umem_tx_add_off(umm, off, size)					\
	umem_tx_add(umm, umem_off2id(umm, off), size)

static inline int
umem_tx_begin(struct umem_instance *umm, struct umem_tx_stage_data *txd)
{
	if (umm->umm_ops->mo_tx_begin)
		return umm->umm_ops->mo_tx_begin(umm, txd);
	else
		return 0;
}

static inline int
umem_tx_commit(struct umem_instance *umm)
{
	if (umm->umm_ops->mo_tx_commit)
		return umm->umm_ops->mo_tx_commit(umm);
	else
		return 0;
}

static inline int
umem_tx_abort(struct umem_instance *umm, int err)
{
	if (umm->umm_ops->mo_tx_abort)
		return umm->umm_ops->mo_tx_abort(umm, err);
	else
		return 0;
}

static inline bool
umem_id_equal(struct umem_instance *umm, umem_id_t ummid_1, umem_id_t ummid_2)
{
	return umm->umm_ops->mo_equal(umm, ummid_1, ummid_2);
}

static inline umem_id_t
umem_reserve(struct umem_instance *umm, struct pobj_action *act, size_t size)
{
	if (umm->umm_ops->mo_reserve)
		return umm->umm_ops->mo_reserve(umm, act, size,
						UMEM_TYPE_ANY);
	return UMMID_NULL;
}

static inline void
umem_cancel(struct umem_instance *umm, struct pobj_action *actv, int actv_cnt)
{
	if (umm->umm_ops->mo_cancel)
		return umm->umm_ops->mo_cancel(umm, actv, actv_cnt);
}

static inline int
umem_tx_publish(struct umem_instance *umm, struct pobj_action *actv,
		int actv_cnt)
{
	if (umm->umm_ops->mo_tx_publish)
		return umm->umm_ops->mo_tx_publish(umm, actv, actv_cnt);
	return 0;
}

static inline int
umem_tx_add_callback(struct umem_instance *umm, struct umem_tx_stage_data *txd,
		     int stage, umem_tx_cb_t cb, void *data)
{
	D_ASSERT(umm->umm_ops->mo_tx_add_callback != NULL);
	return umm->umm_ops->mo_tx_add_callback(umm, txd, stage, cb, data);
}

#endif /* __DAOS_MEM_H__ */
