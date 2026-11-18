/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "../device.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void
put(const char *root, const char *relative, const char *text)
{
	char path[1024];
	assert(snprintf(path, sizeof(path), "%s/%s", root, relative) < (int)sizeof(path));
	for (char *p = path + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		assert(mkdir(path, 0700) == 0 || errno == EEXIST);
		*p = '/';
	}
	FILE *file = fopen(path, "w");
	assert(file);
	assert(fputs(text, file) >= 0);
	assert(fclose(file) == 0);
}

static void
port(const char *root, const char *device, unsigned number, const char *state, const char *layer)
{
	char path[256];
	snprintf(path, sizeof(path), "class/infiniband/%s/ports/%u/state", device, number);
	put(root, path, state);
	snprintf(path, sizeof(path), "class/infiniband/%s/ports/%u/link_layer", device, number);
	put(root, path, layer);
}

static void
gid(const char *root, const char *device, unsigned port_number, unsigned index, const char *value,
    const char *type, const char *interface)
{
	char path[256];
	snprintf(path, sizeof(path), "class/infiniband/%s/ports/%u/gids/%u", device, port_number,
		 index);
	put(root, path, value);
	snprintf(path, sizeof(path), "class/infiniband/%s/ports/%u/gid_attrs/types/%u", device,
		 port_number, index);
	put(root, path, type);
	snprintf(path, sizeof(path), "class/infiniband/%s/ports/%u/gid_attrs/ndevs/%u", device,
		 port_number, index);
	put(root, path, interface);
}

static void
remove_tree(const char *path)
{
	DIR *directory = opendir(path);
	if (!directory) {
		assert(unlink(path) == 0);
		return;
	}
	struct dirent *entry;
	while ((entry = readdir(directory))) {
		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
			continue;
		char child[1024];
		assert(snprintf(child, sizeof(child), "%s/%s", path, entry->d_name) <
		       (int)sizeof(child));
		remove_tree(child);
	}
	closedir(directory);
	assert(rmdir(path) == 0);
}

int
main(void)
{
	char root[] = "/tmp/cart-native-device-XXXXXX";
	assert(mkdtemp(root));
	struct crt_native_device_options options = {
	    .interface = "eth1", .ipv4 = "10.78.217.101", .sysfs_root = root, .gid_index = -2};
	struct crt_native_device_selection selected;
	port(root, "mlx5_2", 1, "4: ACTIVE\n", "Ethernet\n");
	gid(root, "mlx5_2", 1, 0, "fe80::9e63:c0ff:fe3c:fb82\n", "IB/RoCE v1\n", "eth1\n");
	gid(root, "mlx5_2", 1, 1, "fe80::9e63:c0ff:fe3c:fb82\n", "RoCE v2\n", "eth1\n");
	gid(root, "mlx5_2", 1, 2, "::ffff:10.78.217.101\n", "IB/RoCE v1\n", "eth1\n");
	gid(root, "mlx5_2", 1, 3, "::ffff:10.78.217.101\n", "RoCE v2\n", "eth1\n");
	port(root, "mlx5_3", 1, "2: INIT\n", "Ethernet\n");
	gid(root, "mlx5_3", 1, 3, "::ffff:10.78.217.101\n", "RoCE v2\n", "eth2\n");
	assert(crt_native_select_device(&options, &selected) == 0);
	assert(!strcmp(selected.device, "mlx5_2") && selected.port == 1 &&
	       selected.gid_index == 3 && selected.roce_v2);
	/* Reject unspecified GIDs and do not interpret an unknown type as v1. */
	gid(root, "mlx5_2", 1, 4, "::\n", "RoCE v2\n", "eth1\n");
	gid(root, "mlx5_2", 1, 3, "::ffff:10.78.217.101\n", "unknown\n", "eth1\n");
	assert(crt_native_select_device(&options, &selected) == 0 && selected.gid_index == 2);
	gid(root, "mlx5_2", 1, 3, "::ffff:10.78.217.101\n", "RoCE v2\n", "eth1\n");
	/* A second active HCA with the same mapped interface is ambiguous. */
	port(root, "mlx5_3", 1, "4: ACTIVE\n", "Ethernet\n");
	gid(root, "mlx5_3", 1, 3, "::ffff:10.78.217.101\n", "RoCE v2\n", "eth1\n");
	assert(crt_native_select_device(&options, &selected) == EADDRNOTAVAIL);
	options.domain = "mlx5_2";
	assert(crt_native_select_device(&options, &selected) == 0 &&
	       !strcmp(selected.device, "mlx5_2"));
	options.domain = NULL;
	gid(root, "mlx5_3", 1, 3, "::ffff:10.78.217.101\n", "RoCE v2\n", "eth2\n");
	assert(crt_native_select_device(&options, &selected) == 0 &&
	       !strcmp(selected.device, "mlx5_2"));
	/* Agent returns an HCA domain; server may use the netdev as its domain. */
	options.domain = "mlx5_2";
	assert(crt_native_select_device(&options, &selected) == 0 && selected.gid_index == 3);
	options.domain = "eth1";
	assert(crt_native_select_device(&options, &selected) == 0);
	options.domain = "mlx5_2:1";
	assert(crt_native_select_device(&options, &selected) == 0);
	options.domain = "missing_hca";
	assert(crt_native_select_device(&options, &selected) == ENODEV);
	options.device = "mlx5_2";
	assert(crt_native_select_device(&options, &selected) == 0); /* Explicit override wins. */
	options.gid_index = 2;
	assert(crt_native_select_device(&options, &selected) == 0 && selected.gid_index == 2 &&
	       !selected.roce_v2);
	options.gid_index = 1;
	assert(crt_native_select_device(&options, &selected) == 0 && selected.gid_index == 1);
	options.gid_index = -1;
	assert(crt_native_select_device(&options, &selected) == ENODEV); /* RoCE needs a GID. */
	options.gid_index = -2;
	options.port      = 2;
	assert(crt_native_select_device(&options, &selected) == ENODEV);
	port(root, "mlx5_2", 2, "4: ACTIVE\n", "Ethernet\n");
	gid(root, "mlx5_2", 2, 7, "::ffff:10.78.217.101\n", "RoCE v2\n", "eth1\n");
	assert(crt_native_select_device(&options, &selected) == 0 && selected.port == 2 &&
	       selected.gid_index == 7);
	options.port = 0;
	assert(crt_native_select_device(&options, &selected) == EADDRNOTAVAIL);
	/* Domain suffix selects the physical port unless explicitly overridden. */
	options.device = NULL;
	options.domain = "mlx5_2:1";
	assert(crt_native_select_device(&options, &selected) == 0 && selected.port == 1);
	options.port = 2;
	assert(crt_native_select_device(&options, &selected) == 0 && selected.port == 2);
	/* Wrong-netdev and wrong-address entries cannot satisfy automatic selection. */
	gid(root, "mlx5_2", 2, 7, "::ffff:10.78.217.101\n", "RoCE v2\n", "eth2\n");
	assert(crt_native_select_device(&options, &selected) == ENODEV);
	gid(root, "mlx5_2", 2, 7, "::ffff:10.78.217.102\n", "RoCE v2\n", "eth1\n");
	assert(crt_native_select_device(&options, &selected) == ENODEV);
	options.gid_index = 7;
	assert(crt_native_select_device(&options, &selected) == 0); /* Explicit alternate GID. */
	options.gid_index = 8;
	assert(crt_native_select_device(&options, &selected) == ENODEV);
	/* Native IB defaults to LID addressing and discovers ports beyond port 1. */
	port(root, "mlx5_ib", 2, "4: ACTIVE\n", "InfiniBand\n");
	put(root, "class/infiniband/mlx5_ib/device/net/ib0/placeholder", "\n");
	put(root, "class/net/ib0/dev_port", "1\n");
	options = (struct crt_native_device_options){
	    .interface = "ib0", .ipv4 = "192.0.2.1", .sysfs_root = root, .gid_index = -2};
	assert(crt_native_select_device(&options, &selected) == 0);
	assert(!strcmp(selected.device, "mlx5_ib") && selected.port == 2 &&
	       selected.gid_index == -1);
	options.device = "../mlx5_ib";
	assert(crt_native_select_device(&options, &selected) == EINVAL);
	options.device = "";
	assert(crt_native_select_device(&options, &selected) == EINVAL);
	options.device = NULL;
	options.port   = 256;
	assert(crt_native_select_device(&options, &selected) == EINVAL);
	remove_tree(root);
	puts("Native device selection: local interface/HCA/port, IPv4 RoCEv2, overrides, "
	     "ambiguity and InfiniBand passed");
	return 0;
}
