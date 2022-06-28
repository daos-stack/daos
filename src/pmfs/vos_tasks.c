/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <daos/common.h>
#include <daos_types.h>
#include <pmfs/vos_tasks.h>
#include <pmfs/vos_target_fs.h>

struct spdk_ring *
vos_target_create_tasks(const char *name, size_t count)
{
	struct spdk_ring *ring;

	ring = spdk_ring_create(SPDK_RING_TYPE_MP_MC, count,
				SPDK_ENV_SOCKET_ID_ANY);
	if (ring) {
		struct ring_list ring_list;

		vos_task_bind_ring(name, ring, &ring_list);
	}

	return ring;
}

void
vos_target_free_tasks(struct spdk_ring *tasks)
{
	if (tasks == NULL) {
		return;
	}

	spdk_ring_free(tasks);
}
