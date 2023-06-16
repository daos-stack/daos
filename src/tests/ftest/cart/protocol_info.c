/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <cart/api.h>

#include <stdio.h>
#include <stdlib.h>

#define NWIDTH 20

static int
print_info(const char *info_string)
{
	struct crt_protocol_info *protocol_infos = NULL, *protocol_info;
	int                       ret;

	ret = crt_protocol_info_get(info_string, &protocol_infos);
	if (ret != DER_SUCCESS) {
		fprintf(stderr, "crt_protocol_info_get() failed (%d)\n", ret);
		return ret;
	}
	if (protocol_infos == NULL) {
		fprintf(stderr, "No protocol found for \"%s\"\n", info_string);
		return -DER_NOTSUPPORTED;
	}

	printf("--------------------------------------------------\n");
	printf("%-*s%*s%*s\n", 10, "Class", NWIDTH, "Protocol", NWIDTH, "Device");
	printf("--------------------------------------------------\n");
	for (protocol_info = protocol_infos; protocol_info != NULL;
	     protocol_info = protocol_info->next)
		printf("%-*s%*s%*s\n", 10, protocol_info->class_name, NWIDTH,
		       protocol_info->protocol_name, NWIDTH, protocol_info->device_name);

	crt_protocol_info_free(protocol_infos);

	return DER_SUCCESS;
}

int
main(int argc, char *argv[])
{
	const char *info_string = NULL;
	int         crt_ret;

	if (argc == 1) {
		printf("Retrieving protocol info for all protocols...\n");
	} else if (argc == 2) {
		info_string = argv[1];
		printf("Retrieving protocol info for \"%s\"...\n", info_string);
	} else {
		printf("usage: %s [<class+protocol>]\n", argv[0]);
		goto err;
	}

	crt_ret = print_info(info_string);
	if (crt_ret != DER_SUCCESS)
		goto err;

	return EXIT_SUCCESS;

err:
	return EXIT_FAILURE;
}
