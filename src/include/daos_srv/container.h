/**
 * (C) Copyright 2015, 2016 Intel Corporation.
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
 * ds_cont: Container Server API
 */

#ifndef __DAOS_SRV_CONTAINER_H__
#define __DAOS_SRV_CONTAINER_H__

#include <daos_types.h>
#include <daos_srv/pool.h>

/*
 * Target service per-thread container object
 *
 * Stores per-container information, such as the vos container handle, for one
 * service thread.
 */
struct dsms_vcont {
	struct daos_llink	dvc_list;
	daos_handle_t		dvc_hdl;
	uuid_t			dvc_uuid;
};

/*
 * Target service per-thread container handle object
 *
 * Stores per-handle information, such as the container capabilities, for one
 * service thread. Used by container and target services. References the
 * container and the per-thread pool object.
 */
struct tgt_cont_hdl {
	daos_list_t		tch_entry;
	uuid_t			tch_uuid;	/* of the container handle */
	uint64_t		tch_capas;
	struct dsms_vpool      *tch_pool;
	struct dsms_vcont      *tch_cont;
	int			tch_ref;
};

struct tgt_cont_hdl *dsms_tgt_cont_hdl_lookup(const uuid_t uuid);
void dsms_tgt_cont_hdl_put(struct tgt_cont_hdl *hdl);

#endif /* ___DAOS_SRV_CONTAINER_H_ */
