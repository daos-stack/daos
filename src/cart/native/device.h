/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#ifndef CRT_NATIVE_DEVICE_H
#define CRT_NATIVE_DEVICE_H

#include <sys/types.h>
#include <net/if.h>
#include <stdbool.h>

/* Linux RDMA sysfs discovery, performed only at context initialization.
 * An explicit device overrides the agent/provider domain. Port 0 and GID -2
 * request discovery; GID -1 explicitly selects InfiniBand LID addressing.
 * sysfs_root is NULL for /sys; an alternate root is for fixture tests only.
 */
struct crt_native_device_options {
	const char *interface, *ipv4, *domain, *device, *sysfs_root;
	unsigned    port;
	int         gid_index;
};

struct crt_native_device_selection {
	char     device[64], interface[IF_NAMESIZE];
	unsigned port;
	int      gid_index;
	bool     roce_v2;
};

/* Positive errno. ENODEV means no usable mapping; EADDRNOTAVAIL means multiple
 * equally suitable device/port mappings. No global environment is modified.
 */
int
crt_native_select_device(const struct crt_native_device_options *,
			 struct crt_native_device_selection *);

#endif
