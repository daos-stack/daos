/**
 * (C) Copyright 2016 Intel Corporation.
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
 * tmmid	Typedef ummid.
 */

/************************** PMDK MACROS **************************************/
#if DAOS_HAS_PMDK

#include <libpmemobj.h>

/** memory ID without type */
typedef PMEMoid			umem_id_t;
#define UMMID_NULL		OID_NULL
#define UMMID_IS_NULL(ummid)	OID_IS_NULL(ummid)

/** typedef memory ID */
#define TMMID(type)		TOID(type)
#define TMMID_DECLARE(t, i)	TOID_DECLARE(t, i)

#define TMMID_NULL(t)		TOID_NULL(t)
#define TMMID_IS_NULL(tmmid)	UMMID_IS_NULL((tmmid).oid)

#define TMMID_TYPE_NUM(t)	TOID_TYPE_NUM(t)

int umem_tx_errno(int err);

/************************** NON-PMDK MACROS **********************************/
#else /* !DAOS_HAS_PMDK */

#define _mmid_struct
#define _mmid_union
#define _mmid_enum

typedef struct {
	uint64_t	off;
} umem_id_t;

struct pobj_action {
	int		type;
	uint64_t	off;
};
#define POBJ_MAX_ACTIONS	0
#define POBJ_FLAG_ZERO		(((uint64_t)1) << 0)
#define POBJ_FLAG_NO_FLUSH	(((uint64_t)1) << 1)

#define UMMID_NULL		((umem_id_t){0})
#define UMMID_IS_NULL(ummid)	(ummid.off == 0)

#define TMMID_TYPE_NUM(t)	0

#define TMMID(t)					\
union _mmid_##t##_mmid

#define TMMID_DECLARE(t, i)				\
TMMID(t)						\
{							\
	umem_id_t oid;					\
	t *_type;					\
}

#define TMMID_NULL(t)		((TMMID(t))UMMID_NULL)
#define TMMID_IS_NULL(tmmid)	UMMID_IS_NULL((tmmid).oid)

/************************** PMDK MACROS END *********************************/
#endif /* !DAOS_HAS_PMDK */

/* print format of ummid and tmmid */
#define UMMID_PF		DF_X64
#define UMMID_P(id)		((id).off)

#define TMMID_PF		DF_X64
#define TMMID_P(id)		((id).oid.off)

typedef enum {
	/** volatile memory */
	UMEM_CLASS_VMEM,
	/** persistent memory */
	UMEM_CLASS_PMEM,
	/** unknown */
	UMEM_CLASS_UNKNOWN,
} umem_class_id_t;

struct umem_instance;

typedef struct {
	/** convert ummid to directly accessible address */
	void		*(*mo_addr)(struct umem_instance *umm,
				    umem_id_t ummid);
	/** check if two ummid are equal */
	bool		 (*mo_equal)(struct umem_instance *umm,
				     umem_id_t ummid1, umem_id_t ummid2);
	/** free ummid */
	void		 (*mo_tx_free)(struct umem_instance *umm,
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
	/** reserved: start memory transaction */
	int		 (*mo_tx_begin)(struct umem_instance *umm);
	/** reserved: stop memory transaction */
	int		 (*mo_tx_end)(struct umem_instance *umm);

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
} umem_ops_t;

/** attributes to initialise an unified memroy class */
struct umem_attr {
	umem_class_id_t		 uma_id;
	union {
#if DAOS_HAS_PMDK
		PMEMobjpool	*pmem_pool;
#endif
	}			 uma_u;
};

/** instance of an unified memory class */
struct umem_instance {
	umem_class_id_t		 umm_id;
	const char		*umm_name;
	union {
#if DAOS_HAS_PMDK
		PMEMobjpool	*pmem_pool;
#endif
	}			 umm_u;
	/** class member functions */
	umem_ops_t		*umm_ops;
};

int  umem_class_init(struct umem_attr *uma, struct umem_instance *umm);
void umem_attr_get(struct umem_instance *umm, struct umem_attr *uma);

enum {
	UMEM_TYPE_ANY,
};

static inline bool
umem_has_tx(struct umem_instance *umm)
{
	return umm->umm_ops->mo_tx_add != NULL;
}

static inline umem_id_t
umem_alloc_verb(struct umem_instance *umm, uint64_t flags, size_t size)
{
	return umm->umm_ops->mo_tx_alloc(umm, size, flags, UMEM_TYPE_ANY);
}

#define umem_alloc(umm, size)						\
	umem_alloc_verb(umm, 0, size)

#define umem_zalloc(umm, size)						\
	umem_alloc_verb(umm, POBJ_FLAG_ZERO, size)

#define umem_alloc_noflush(umm, size)					\
	umem_alloc_verb(umm, POBJ_FLAG_NO_FLUSH, size)

#define									\
umem_alloc_typed_verb(umm, type, flags, size)				\
({									\
	umem_id_t   __ummid;						\
	TMMID(type) __tmmid;						\
									\
	__ummid = (umm)->umm_ops->mo_tx_alloc(umm, size, flags,		\
					   TMMID_TYPE_NUM(type));	\
	D__DEBUG(DB_MEM, "allocate %s mmid "UMMID_PF"\n",		\
		(umm)->umm_name, UMMID_P(__ummid));			\
	__tmmid.oid = __ummid;						\
	__tmmid;							\
})

#define umem_alloc_typed(umm, type, size)				\
	umem_alloc_typed_verb(umm, type, 0, size)

#define umem_zalloc_typed(umm, type, size)				\
	umem_alloc_typed_verb(umm, type, POBJ_FLAG_ZERO, size)

#define umem_new_typed(umm, type)					\
	umem_alloc_typed_verb(umm, type, 0, sizeof(type))

#define umem_znew_typed(umm, type)					\
	umem_alloc_typed_verb(umm, type, POBJ_FLAG_ZERO, sizeof(type))

static inline void
umem_free(struct umem_instance *umm, umem_id_t ummid)
{
	D__DEBUG(DB_MEM, "Free %s mmid "UMMID_PF"\n",
		umm->umm_name, UMMID_P(ummid));

	umm->umm_ops->mo_tx_free(umm, ummid);
}

#define umem_free_typed(umm, tmmid)					\
	umem_free(umm, (tmmid).oid)

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

#define	umem_tx_add_range_typed(umm, tmmid, off, size)			\
	umem_tx_add_range(umm, (tmmid).oid, off, size)

#define	umem_tx_add_typed(umm, tmmid, size)				\
	umem_tx_add_range(umm, (tmmid).oid, 0, size)

#define umem_tx_add_mmid_typed(umm, tmmid)				\
	umem_tx_add_typed(umm, tmmid, sizeof(*(tmmid)._type))

static inline int
umem_tx_begin(struct umem_instance *umm)
{
	if (umm->umm_ops->mo_tx_begin)
		return umm->umm_ops->mo_tx_begin(umm);
	else
		return 0;
}

static inline int
umem_tx_end(struct umem_instance *umm)
{
	if (umm->umm_ops->mo_tx_end)
		return umm->umm_ops->mo_tx_end(umm);
	else
		return 0;
}

static inline int
umem_tx_abort(struct umem_instance *umm, int err)
{
	if (umm->umm_ops->mo_tx_end)
		return umm->umm_ops->mo_tx_abort(umm, err);
	else
		return 0;
}

static inline void *
umem_id2ptr(struct umem_instance *umm, umem_id_t ummid)
{
	return umm->umm_ops->mo_addr(umm, ummid);
}

#define umem_id2ptr_typed(umm, tmmid)					\
((__typeof__(*(tmmid)._type) *)((umm)->umm_ops->mo_addr(umm, (tmmid).oid)))

static inline bool
umem_id_equal(struct umem_instance *umm, umem_id_t ummid_1, umem_id_t ummid_2)
{
	return umm->umm_ops->mo_equal(umm, ummid_1, ummid_2);
}
#define umem_id_equal_typed(umm, tmmid_1, tmmid_2)			\
	umem_id_equal(umm, (tmmid_1).oid, (tmmid_2).oid)

#define umem_id_u2t(ummid, type)					\
({									\
	TMMID(type)	__tmmid;					\
									\
	__tmmid.oid = ummid;						\
	__tmmid;							\
})

#define umem_id_t2u(tmmid)	((tmmid).oid)

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

#endif /* __DAOS_MEM_H__ */
