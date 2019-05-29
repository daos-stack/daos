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
 * Layout definition for VOS root object
 * vos/vos_internal.h
 *
 * Author: Vishwanath Venkatesan <vishwanath.venkatesan@intel.com>
 */

#ifndef __VOS_META_H__
#define __VOS_META_H__

enum vos_subtree_type {
	VOS_ST_NONE,
	VOS_ST_BTREE,
	VOS_ST_EVTREE,
};

struct vos_tree_meta {
	size_t			tm_root_size;
	size_t			tm_rec_size;
	size_t			tm_hkey_size;
	size_t			tm_tree_order;
	enum vos_subtree_type	tm_subtree_type;
};

int dbtree_meta_get(int class);
int vos_meta_container_get(struct vos_tree_meta *cont_size);
int vos_meta_object_get(struct vos_tree_meta *cont_size);
int vos_meta_key_get(struct vos_tree_meta *cont_size);
int vea_meta_free_get(struct vos_tree_meta *cont_size);
int vea_meta_alloc_get(struct vos_tree_meta *cont_size);
int vos_meta_dtx_get(struct vos_tree_meta *cont_size);
int vos_meta_sv_get(struct vos_tree_meta *cont_size);
int vos_meta_evtree_get(struct vos_tree_meta *cont_size);
int vos_meta_btree_get(struct vos_tree_meta *cont_size);

#endif /* __VOS_META_H__ */
