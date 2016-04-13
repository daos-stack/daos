/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/**
 * This file is part of daos
 *
 * src/include/daos/daos_mem.h
 *
 * Author: Liang Zhen <liang.zhen@intel.com>
 */
#ifndef __DAOS_MEM_H__
#define __DAOS_MEM_H__

#include <daos/daos_types.h>
#include <daos/daos_common.h>

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

/************************** NVML MACROS **************************************/
#if DAOS_HAS_NVML

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

/************************** NON-NVML MACROS **********************************/
#else /* !DAOS_HAS_NVML */

#define _mmid_struct
#define _mmid_union
#define _mmid_enum

typedef struct {
	uint64_t	off;
} umem_id_t;

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

/************************** NVML MACROS END *********************************/
#endif /* !DAOS_HAS_NVML */

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
	void		 (*mo_free)(struct umem_instance *umm,
				    umem_id_t ummid);
	/**
	 * allocate ummid with the specified size
	 *
	 * \param umm	[IN]	umem class instance.
	 * \param size	[IN]	size to allocate.
	 * \param zero	[IN]	zero new allocated buffer.
	 * \param type_num [IN]	struct type (for nvml)
	 */
	umem_id_t	 (*mo_alloc)(struct umem_instance *umm, size_t size,
				     bool zero, unsigned int type_num);
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
	/** abort memory transaction */
	int		 (*mo_tx_abort)(struct umem_instance *umm, int error);
	/** reserved: start memory transaction */
	int		 (*mo_tx_begin)(struct umem_instance *umm);
	/** reserved: stop memory transaction */
	int		 (*mo_tx_end)(struct umem_instance *umm);
} umem_ops_t;

/** attributes to initialise an unified memroy class */
struct umem_attr {
	umem_class_id_t		 uma_id;
	union {
#if DAOS_HAS_NVML
		PMEMobjpool	*pmem_pool;
#endif
	}			 uma_u;
};

/** instance of an unified memory class */
struct umem_instance {
	umem_class_id_t		 umm_id;
	const char		*umm_name;
	union {
#if DAOS_HAS_NVML
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
umem_alloc_verb(struct umem_instance *umm, bool zero, size_t size)
{
	return umm->umm_ops->mo_alloc(umm, size, false, UMEM_TYPE_ANY);
}

#define umem_alloc(umm, size)						\
	umem_alloc_verb(umm, false, size)

#define umem_zalloc(umm, size)						\
	umem_alloc_verb(umm, true, size)

#define									\
umem_alloc_typed_verb(umm, type, zero, size)				\
({									\
	umem_id_t   __ummid;						\
	TMMID(type) __tmmid;						\
									\
	__ummid = (umm)->umm_ops->mo_alloc(umm, size, false,		\
					   TMMID_TYPE_NUM(type));	\
	D_DEBUG(DF_MEM, "allocate %s mmid "UMMID_PF"\n",		\
		(umm)->umm_name, UMMID_P(__ummid));			\
	__tmmid.oid = __ummid;						\
	__tmmid;							\
})

#define umem_alloc_typed(umm, type, size)				\
	umem_alloc_typed_verb(umm, type, false, size)

#define umem_zalloc_typed(umm, type, size)				\
	umem_alloc_typed_verb(umm, type, true, size)

#define umem_new_typed(umm, type)					\
	umem_alloc_typed_verb(umm, type, false, sizeof(type))

#define umem_znew_typed(umm, type)					\
	umem_alloc_typed_verb(umm, type, true, sizeof(type))

static inline void
umem_free(struct umem_instance *umm, umem_id_t ummid)
{
	D_DEBUG(DF_MEM, "Free %s mmid "UMMID_PF"\n",
		umm->umm_name, UMMID_P(ummid));

	umm->umm_ops->mo_free(umm, ummid);
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

#endif /* __DAOS_MEM_H__ */
