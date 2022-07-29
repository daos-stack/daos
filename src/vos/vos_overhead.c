#include <daos/common.h>
#include <daos/btree_class.h>
#include <daos/btree.h>
#include <daos_srv/vos.h>
#include <daos_srv/evtree.h>
#include "vos_internal.h"

int
vos_pool_get_msize(void)
{
	return sizeof(struct vos_pool_df);
}

int
vos_container_get_msize(void)
{
	return sizeof(struct vos_cont_df);
}

int
vos_pool_get_scm_cutoff(void)
{
	return VOS_BLK_SZ;
}

int
vos_tree_get_overhead(int alloc_overhead, enum VOS_TREE_CLASS tclass,
		      uint64_t otype, struct daos_tree_overhead *ovhd)
{
	int	rc = 0;
	int	btr_class = 0;
	int	tree_order = 0;

	D_ASSERT(ovhd != NULL);
	memset(ovhd, 0, sizeof(*ovhd));

	if (tclass == VOS_TC_ARRAY) {
		rc = evt_overhead_get(alloc_overhead, VOS_EVT_ORDER, ovhd);
		goto out;
	}

	switch (tclass) {
	case VOS_TC_CONTAINER:
		btr_class = VOS_BTR_CONT_TABLE;
		tree_order = VOS_CONT_ORDER;
		break;
	case VOS_TC_OBJECT:
		btr_class = VOS_BTR_OBJ_TABLE;
		tree_order = VOS_OBJ_ORDER;
		break;
	case VOS_TC_DKEY:
		btr_class = VOS_BTR_DKEY;
		tree_order = VOS_KTR_ORDER;
		break;
	case VOS_TC_AKEY:
		tree_order = VOS_KTR_ORDER;
		btr_class = VOS_BTR_AKEY;
		break;
	case VOS_TC_SV:
		tree_order = VOS_SVT_ORDER;
		btr_class = VOS_BTR_SINGV;
		break;
	case VOS_TC_VEA:
		tree_order = VEA_TREE_ODR;
		btr_class = DBTREE_CLASS_IV;
		break;
	default:
		D_ASSERT(0);
		break;
	};

	rc = dbtree_overhead_get(alloc_overhead, btr_class, otype, tree_order,
				 ovhd);
out:
	return rc;
}


