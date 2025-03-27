/*
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 *
 * Copyright (c) 2022-2023 Hewlett Packard Enterprise Development LP
 */

/* Compile: cc -o getip getip.c */
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <time.h>

int get_mac_ioctls(char **macs, int count)
{
	struct ifreq ifr, *it, *end;
	struct ifconf ifc;
	char buf[1024];
	int success = 0;
	int sock;
	int i, idx, ret;
	char *mptr;
	unsigned char *sptr;

	// acquire socket identifier
	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (sock < 0)
		return sock;
	// prepare the ifc structure
	ifc.ifc_len = sizeof(buf);
	ifc.ifc_buf = buf;
	// populate ifc structure from kernel
	ret = ioctl(sock, SIOCGIFCONF, &ifc);
	if (ret < 0)
		return ret;
	// walk through the interfaces
	it = ifc.ifc_req;
	end = it + (ifc.ifc_len / sizeof(struct ifreq));
	idx = 0;
	for (; it != end && idx < count; it++) {
		// find only hsn* interfaces
		strcpy(ifr.ifr_name, it->ifr_name);
		if (strncmp(ifr.ifr_name, "hsn", 3))
			continue;
		// acquire flags
		if (ioctl(sock, SIOCGIFFLAGS, &ifr))
			continue;
		// acquire hardware address
		if (ioctl(sock, SIOCGIFHWADDR, &ifr))
			continue;
		// copy hardware address into return pointer
		mptr = macs[idx++];
		sptr = ifr.ifr_hwaddr.sa_data;
		for (i = 0; i < 6; i++) {
			if (i)
				*mptr++ = ':';
			mptr += sprintf(mptr, "%02x", *sptr++);
		}
		*mptr = 0;
	}
	close(sock);
	return idx;
}

int get_mac_sysfile(char **macs, int count)
{
	DIR *dir;
	FILE *fid;
	struct dirent *dent;
	char path[1024];
	int n, idx;

	// open the network sysfs directory
	dir = opendir("/sys/class/net");
	if (!dir)
		return 1;
	// read the directory contents
	idx = 0;
	while ((dent = readdir(dir)) && idx < count) {
		// find only hsn* interfaces
		if (strncmp("hsn", dent->d_name, 3))
			continue;
		// open and read the address file
		sprintf(path, "/sys/class/net/%s/address", dent->d_name);
		fid = fopen(path, "r");
		n = fread(macs[idx++], 32, 1, fid);
		fclose(fid);
	}
	closedir(dir);
	return 0;
}

int main(int argc, char **argv)
{
	struct timespec t0, t1;
	long int count;
	char **macs;
	int i, num;
	int secs = 2;

	macs = calloc(4, sizeof(char *));
	for (i = 0; i < 4; i++)
		macs[i] = malloc(32);

	get_mac_ioctls(macs, 4);
	clock_gettime(CLOCK_MONOTONIC, &t0);
	t0.tv_sec += secs;
	count = 0;
	do {
		get_mac_ioctls(macs, 4);
		count++;
		clock_gettime(CLOCK_MONOTONIC, &t1);
	} while (t1.tv_sec < t0.tv_sec ||
		 (t1.tv_sec == t0.tv_sec &&
		  t1.tv_nsec < t0.tv_nsec));
	printf("direct: %9ld\n", count);

	get_mac_sysfile(macs, 4);
	clock_gettime(CLOCK_MONOTONIC, &t0);
	t0.tv_sec += secs;
	count = 0;
	do {
		get_mac_sysfile(macs, 4);
		count++;
		clock_gettime(CLOCK_MONOTONIC, &t1);
	} while (t1.tv_sec < t0.tv_sec ||
		 (t1.tv_sec == t0.tv_sec &&
		 t1.tv_nsec < t0.tv_nsec));
	printf("sysfs : %9ld\n", count);

	return 0;
}
