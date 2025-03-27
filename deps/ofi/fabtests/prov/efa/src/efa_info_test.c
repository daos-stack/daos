/*
 * Copyright (c) 2022, Amazon.com, Inc.  All rights reserved.
 *
 * This software is available to you under the BSD license
 * below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * This test ensures that the fabric id we get from the efa provider will
 * always be "efa".
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <shared.h>

int main(int argc, char **argv)
{
	int ret;
	struct fi_info *info;

	hints = fi_allocinfo();
	if (!hints)
		return EXIT_FAILURE;
	hints->fabric_attr->prov_name=strdup("efa");
	ret = ft_init();
	if (ret) {
		FT_PRINTERR("ft_init", -ret);
		goto out;
	}
	ret = ft_getinfo(hints, &fi);
	if (ret) {
		FT_PRINTERR("ft_getinfo", -ret);
		goto out;
	}

	info = fi;
	while (NULL != info) {
		if (0 != strcmp(info->fabric_attr->name, "efa")) {
			ret = EXIT_FAILURE;
			goto out;
		}
		info = info->next;
	}

out:
	fi_freeinfo(hints);
	fi_freeinfo(fi);
	return ft_exit_code(ret);
}
