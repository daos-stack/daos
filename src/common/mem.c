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
 * common/mem.c
 *
 * Author: Liang Zhen <liang.zhen@intel.com>
 */
#define D_LOGFAC	DD_FAC(common)

#include <daos/common.h>
#include <daos/mem.h>

#if DAOS_HAS_PMDK
/** persistent memory operations (depends on pmdk) */

static void *
pmem_addr(struct umem_instance *umm, umem_id_t ummid)
{
	return pmemobj_direct(ummid);
}

static bool
pmem_equal(struct umem_instance *umm, umem_id_t ummid1, umem_id_t ummid2)
{
	return OID_EQUALS(ummid1, ummid2);
}

static void
pmem_tx_free(struct umem_instance *umm, umem_id_t ummid)
{
	if (!OID_IS_NULL(ummid))
		pmemobj_tx_free(ummid);
}

static umem_id_t
pmem_tx_alloc(struct umem_instance *umm, size_t size, uint64_t flags,
	      unsigned int type_num)
{
	return pmemobj_tx_xalloc(size, type_num, flags);
}

static int
pmem_tx_add(struct umem_instance *umm, umem_id_t ummid,
	    uint64_t offset, size_t size)
{
	return pmemobj_tx_add_range(ummid, offset, size);
}


static int
pmem_tx_add_ptr(struct umem_instance *umm, void *ptr, size_t size)
{
	return pmemobj_tx_add_range_direct(ptr, size);
}

static int
pmem_tx_abort(struct umem_instance *umm, int err)
{
	/*
	 * obj_tx_abort() may have already been called in the error
	 * handling code of pmemobj APIs.
	 */
	if (pmemobj_tx_stage() != TX_STAGE_ONABORT)
		pmemobj_tx_abort(err);
	return pmemobj_tx_end();
}

static int
pmem_tx_begin(struct umem_instance *umm)
{
	int rc;

	rc = pmemobj_tx_begin(umm->umm_u.pmem_pool, NULL, TX_PARAM_NONE);
	if (rc != 0) {
		/*
		 * pmemobj_tx_end() needs be called to re-initialize the
		 * tx state when pmemobj_tx_begin() failed.
		 */
		pmemobj_tx_end();
		return pmemobj_tx_errno() ? : rc;
	}
	return 0;
}

static int
pmem_tx_commit(struct umem_instance *umm)
{
	pmemobj_tx_commit();
	return pmemobj_tx_end();
}

static umem_id_t
pmem_reserve(struct umem_instance *umm, struct pobj_action *act, size_t size,
	     unsigned int type_num)
{
	return pmemobj_reserve(umm->umm_u.pmem_pool, act, size, type_num);
}

static void
pmem_cancel(struct umem_instance *umm, struct pobj_action *actv, int actv_cnt)
{
	return pmemobj_cancel(umm->umm_u.pmem_pool, actv, actv_cnt);
}

static int
pmem_tx_publish(struct umem_instance *umm, struct pobj_action *actv,
		int actv_cnt)
{
	return pmemobj_tx_publish(actv, actv_cnt);
}

static umem_ops_t	pmem_ops = {
	.mo_addr		= pmem_addr,
	.mo_equal		= pmem_equal,
	.mo_tx_free		= pmem_tx_free,
	.mo_tx_alloc		= pmem_tx_alloc,
	.mo_tx_add		= pmem_tx_add,
	.mo_tx_add_ptr		= pmem_tx_add_ptr,
	.mo_tx_abort		= pmem_tx_abort,
	.mo_tx_begin		= pmem_tx_begin,
	.mo_tx_commit		= pmem_tx_commit,
	.mo_reserve		= pmem_reserve,
	.mo_cancel		= pmem_cancel,
	.mo_tx_publish		= pmem_tx_publish,
};

int
umem_tx_errno(int err)
{
	if (err == 0) {
		err = pmemobj_tx_errno();
		if (err == ENOMEM) /* pmdk returns ENOMEM for out of space */
			err = ENOSPC;
	}

	if (err == 0) {
		D_ERROR("Transaction aborted for unknown reason\n");
		return -DER_UNKNOWN;
	}

	if (err < 0) {
		if (err < -DER_ERR_GURT_BASE)
			return err; /* aborted by DAOS */

		D_ERROR("pmdk returned negative errno %d\n", err);
		err = -err;
	}
	return daos_errno2der(err);
}

#endif /* DAOS_HAS_PMDK */

/* volatile memroy operations */

static void *
vmem_addr(struct umem_instance *umm, umem_id_t ummid)
{
	return (void *)ummid.off;
}

static bool
vmem_equal(struct umem_instance *umm, umem_id_t ummid1, umem_id_t ummid2)
{
	return ummid1.off == ummid2.off;
}

static void
vmem_free(struct umem_instance *umm, umem_id_t ummid)
{
	if (ummid.off != 0)
		free((void *)ummid.off);
}

umem_id_t
vmem_alloc(struct umem_instance *umm, size_t size, uint64_t flags,
	   unsigned int type_num)
{
	umem_id_t ummid = UMMID_NULL;

	ummid.off = (uint64_t)(flags & POBJ_FLAG_ZERO ?
			       calloc(1, size) : malloc(size));
	return ummid;
}

static umem_ops_t	vmem_ops = {
	.mo_addr	= vmem_addr,
	.mo_equal	= vmem_equal,
	.mo_tx_free	= vmem_free,
	.mo_tx_alloc	= vmem_alloc,
	.mo_tx_add	= NULL,
	.mo_tx_abort	= NULL,
};

/** Unified memory class definition */
struct umem_class {
	umem_class_id_t           umc_id;
	umem_ops_t              *umc_ops;
	char                    *umc_name;
};

/** all defined memory classes */
static struct umem_class umem_class_defined[] = {
	{
		.umc_id		= UMEM_CLASS_VMEM,
		.umc_ops	= &vmem_ops,
		.umc_name	= "vmem",
	},
#if DAOS_HAS_PMDK
	{
		.umc_id		= UMEM_CLASS_PMEM,
		.umc_ops	= &pmem_ops,
		.umc_name	= "pmem",
	},
#endif
	{
		.umc_id		= UMEM_CLASS_UNKNOWN,
		.umc_ops	= NULL,
		.umc_name	= "unknown",
	},
};

/**
 * Instantiate a memory class \a umm by attributes in \a uma
 *
 * \param uma [IN]	Memory attributes to instantiate the memory class.
 * \param umm [OUT]	The instantiated memroy class.
 */
int
umem_class_init(struct umem_attr *uma, struct umem_instance *umm)
{
	struct umem_class *umc;
	bool		   found;

	found = false;
	for (umc = &umem_class_defined[0];
	     umc->umc_id != UMEM_CLASS_UNKNOWN; umc++) {
		if (umc->umc_id == uma->uma_id) {
			found = true;
			break;
		}
	}
	if (!found) {
		D_DEBUG(DB_MEM, "Cannot find memory class %d\n", uma->uma_id);
		return -DER_ENOENT;
	}

	D_DEBUG(DB_MEM, "Instantiate memory class %s\n", umc->umc_name);

	memset(umm, 0, sizeof(*umm));
	umm->umm_id	= umc->umc_id;
	umm->umm_ops	= umc->umc_ops;
	umm->umm_name	= umc->umc_name;
#if DAOS_HAS_PMDK
	umm->umm_u.pmem_pool = uma->uma_u.pmem_pool;
#endif
	return 0;
}

/**
 * Get attributes of a memory class instance.
 */
void
umem_attr_get(struct umem_instance *umm, struct umem_attr *uma)
{
	uma->uma_id = umm->umm_id;
#if DAOS_HAS_PMDK
	uma->uma_u.pmem_pool = umm->umm_u.pmem_pool;
#endif
}

/**
 * Get pmemobj pool uuid
 */
uint64_t
umem_get_uuid(struct umem_instance *umm)
{
	umem_id_t root_oid;

	if (umm->umm_id == UMEM_CLASS_VMEM)
		return 0; /* empty uuid */

	root_oid = pmemobj_root(umm->umm_u.pmem_pool, 0);
	D_ASSERT(!UMMID_IS_NULL(root_oid));
	return root_oid.pool_uuid_lo;
}
