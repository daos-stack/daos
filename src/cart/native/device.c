/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "device.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <ifaddrs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define DEVICE_PATH 1024

static int
read_text(const char *path, char *text, size_t capacity)
{
	FILE *file = fopen(path, "r");
	if (!file)
		return errno;
	int rc = fgets(text, capacity, file) ? 0 : EIO;
	fclose(file);
	if (!rc)
		text[strcspn(text, "\r\n")] = '\0';
	return rc;
}

static bool
number(const char *text, unsigned max, unsigned *out)
{
	if (!text || *text < '0' || *text > '9')
		return false;
	char *end;
	errno               = 0;
	unsigned long value = strtoul(text, &end, 10);
	if (errno || *end || value > max)
		return false;
	*out = value;
	return true;
}

static int
interface_name(const char *requested, const struct in_addr *ipv4, char name[IF_NAMESIZE])
{
	struct in_addr literal;
	if (inet_pton(AF_INET, requested, &literal) != 1) {
		if (!*requested || strlen(requested) >= IF_NAMESIZE || strchr(requested, '/'))
			return EINVAL;
		strcpy(name, requested);
		return 0;
	}
	if (literal.s_addr != ipv4->s_addr)
		return EINVAL;
	struct ifaddrs *list;
	if (getifaddrs(&list))
		return errno;
	int rc = ENODEV;
	for (struct ifaddrs *a = list; a; a = a->ifa_next) {
		if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET ||
		    ((struct sockaddr_in *)a->ifa_addr)->sin_addr.s_addr != ipv4->s_addr ||
		    strlen(a->ifa_name) >= IF_NAMESIZE)
			continue;
		if (!rc && strcmp(name, a->ifa_name)) {
			rc = EADDRNOTAVAIL;
			break;
		}
		strcpy(name, a->ifa_name);
		rc = 0;
	}
	freeifaddrs(list);
	return rc;
}

/* Sysfs GID ndev attributes are authoritative, including VLAN interfaces.
 * Older InfiniBand kernels can fall back to the PCI netdev and dev_port map.
 */
static bool
port_interface(const char *root, const char *device, unsigned port, const char *interface)
{
	char path[DEVICE_PATH], text[64];
	snprintf(path, sizeof(path), "%s/class/infiniband/%s/ports/%u/gid_attrs/ndevs", root,
		 device, port);
	DIR *directory = opendir(path);
	bool mapped    = false;
	if (directory) {
		struct dirent *entry;
		while ((entry = readdir(directory))) {
			unsigned index;
			if (!number(entry->d_name, 255, &index))
				continue;
			snprintf(path, sizeof(path),
				 "%s/class/infiniband/%s/ports/%u/gid_attrs/ndevs/%u", root, device,
				 port, index);
			if (!read_text(path, text, sizeof(text)) && !strcmp(text, interface)) {
				mapped = true;
				break;
			}
		}
		closedir(directory);
	}
	if (mapped)
		return true;
	snprintf(path, sizeof(path), "%s/class/infiniband/%s/device/net/%s", root, device,
		 interface);
	struct stat status;
	if (stat(path, &status) || !S_ISDIR(status.st_mode))
		return false;
	snprintf(path, sizeof(path), "%s/class/net/%s/dev_port", root, interface);
	unsigned dev_port;
	return !read_text(path, text, sizeof(text)) && number(text, 254, &dev_port) &&
	       dev_port + 1 == port;
}

static int
select_gid(const struct crt_native_device_options *options, const char *root, const char *device,
	   unsigned port, const char *interface, const struct in_addr *ipv4, bool ethernet,
	   int *selected)
{
	char path[DEVICE_PATH], text[128];
	snprintf(path, sizeof(path), "%s/class/infiniband/%s/ports/%u/gids", root, device, port);
	DIR *directory = opendir(path);
	if (!directory)
		return 0;
	int            best = 0;
	struct dirent *entry;
	while ((entry = readdir(directory))) {
		unsigned index;
		if (!number(entry->d_name, 255, &index) ||
		    (options->gid_index >= 0 && index != (unsigned)options->gid_index))
			continue;
		snprintf(path, sizeof(path), "%s/class/infiniband/%s/ports/%u/gids/%u", root,
			 device, port, index);
		struct in6_addr gid;
		if (read_text(path, text, sizeof(text)) || inet_pton(AF_INET6, text, &gid) != 1 ||
		    IN6_IS_ADDR_UNSPECIFIED(&gid))
			continue;
		if (options->gid_index < 0 &&
		    (!IN6_IS_ADDR_V4MAPPED(&gid) || memcmp(gid.s6_addr + 12, ipv4, 4)))
			continue;
		int score = 1;
		if (ethernet) {
			snprintf(path, sizeof(path),
				 "%s/class/infiniband/%s/ports/%u/gid_attrs/ndevs/%u", root, device,
				 port, index);
			if (read_text(path, text, sizeof(text)) || strcmp(text, interface))
				continue;
			snprintf(path, sizeof(path),
				 "%s/class/infiniband/%s/ports/%u/gid_attrs/types/%u", root, device,
				 port, index);
			int type_rc = read_text(path, text, sizeof(text));
			if (!type_rc && !strcmp(text, "RoCE v2"))
				score = 2;
			else if ((type_rc || strcmp(text, "IB/RoCE v1")) && options->gid_index < 0)
				continue;
		}
		if (score > best || (score == best && (int)index < *selected)) {
			*selected = index;
			best      = score;
		}
	}
	closedir(directory);
	return best;
}

int
crt_native_select_device(const struct crt_native_device_options *options,
			 struct crt_native_device_selection     *out)
{
	if (!options || !out || !options->interface || !options->ipv4 || options->port > 255 ||
	    options->gid_index < -2 || options->gid_index > 255)
		return EINVAL;
	*out = (struct crt_native_device_selection){0};
	struct in_addr ipv4;
	if (inet_pton(AF_INET, options->ipv4, &ipv4) != 1)
		return EINVAL;
	char interface[IF_NAMESIZE];
	int  rc = interface_name(options->interface, &ipv4, interface);
	if (rc)
		return rc;
	const char *root = options->sysfs_root ? options->sysfs_root : "/sys";
	if (strlen(root) > DEVICE_PATH - 256)
		return ENAMETOOLONG;
	const char *hint = options->device;
	if (!hint && options->domain && *options->domain &&
	    strcmp(options->domain, options->interface) && strcmp(options->domain, interface) &&
	    strcmp(options->domain, options->ipv4))
		hint = options->domain;
	char     device_hint[sizeof(out->device)] = {0};
	unsigned port_hint                        = options->port;
	if (hint) {
		if (!*hint || strlen(hint) >= sizeof(device_hint) || strchr(hint, '/'))
			return EINVAL;
		strcpy(device_hint, hint);
		char *colon = strchr(device_hint, ':');
		if (colon) {
			*colon++ = '\0';
			unsigned hinted_port;
			if (!number(colon, 255, &hinted_port) || !hinted_port || !*device_hint)
				return EINVAL;
			if (!port_hint)
				port_hint = hinted_port;
		}
	}
	char path[DEVICE_PATH];
	snprintf(path, sizeof(path), "%s/class/infiniband", root);
	DIR *devices = opendir(path);
	if (!devices)
		return errno;
	int            best      = 0;
	bool           ambiguous = false;
	struct dirent *entry;
	while ((entry = readdir(devices))) {
		const char *device = entry->d_name;
		if (*device == '.' || strlen(device) >= sizeof(out->device) ||
		    (*device_hint && strcmp(device, device_hint)))
			continue;
		snprintf(path, sizeof(path), "%s/class/infiniband/%s/ports", root, device);
		DIR *ports = opendir(path);
		if (!ports)
			continue;
		struct dirent *port_entry;
		while ((port_entry = readdir(ports))) {
			unsigned port;
			if (!number(port_entry->d_name, 255, &port) || !port ||
			    (port_hint && port != port_hint))
				continue;
			char text[64];
			snprintf(path, sizeof(path), "%s/class/infiniband/%s/ports/%u/state", root,
				 device, port);
			if (read_text(path, text, sizeof(text)) || strtol(text, NULL, 10) != 4 ||
			    !port_interface(root, device, port, interface))
				continue;
			snprintf(path, sizeof(path), "%s/class/infiniband/%s/ports/%u/link_layer",
				 root, device, port);
			if (read_text(path, text, sizeof(text)))
				continue;
			bool ethernet = !strcmp(text, "Ethernet");
			if (!ethernet && strcmp(text, "InfiniBand"))
				continue;
			int gid = -1, score = 1;
			if (ethernet || options->gid_index >= 0) {
				if (ethernet && options->gid_index == -1)
					continue;
				score = select_gid(options, root, device, port, interface, &ipv4,
						   ethernet, &gid);
			}
			if (!score || score < best)
				continue;
			if (score == best) {
				ambiguous = true;
				continue;
			}
			best      = score;
			ambiguous = false;
			strcpy(out->device, device);
			strcpy(out->interface, interface);
			out->port      = port;
			out->gid_index = gid;
			out->roce_v2   = ethernet && score == 2;
		}
		closedir(ports);
	}
	closedir(devices);
	return !best ? ENODEV : ambiguous ? EADDRNOTAVAIL : 0;
}
