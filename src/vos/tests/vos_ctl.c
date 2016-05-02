/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2016 Intel Corporation.
 */
/**
 * Test for container creation and destroy.
 * vos/tests/vos_ctl.c
 */

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <daos_srv/vos.h>
#include <daos/common.h>
#include "vos_internal.h"

#define VC_SEP          ','
#define VC_SEP_VAL      ':'

static daos_unit_oid_t	vc_oid = {
	{ .lo = 0, .mid = 1, .hi = 2},
	3,
};

static daos_epoch_t	vc_epoch = 1;
static daos_handle_t	vc_coh;

#define VC_STR_SIZE	1024

static int
vc_obj_operator(bool update, char *str)
{
	daos_iov_t	  val_iov;
	char		  key_buf[VC_STR_SIZE];
	char		  val_buf[VC_STR_SIZE];
	daos_dkey_t	  dkey;
	daos_recx_t	  rex;
	int		  count = 0;
	int		  rc = 0;

	memset(&dkey, 0, sizeof(dkey));
	daos_iov_set(&dkey, &key_buf[0], VC_STR_SIZE); /* dkey only */
	daos_iov_set(&val_iov, &val_buf[0], VC_STR_SIZE);

	while (str != NULL && !isspace(*str) && *str != '\0') {
		char		 *key_str = NULL;
		char		 *val_str = NULL;
		daos_vec_iod_t	  vio;
		daos_sg_list_t	  sgl;

		memset(&vio, 0, sizeof(vio));
		memset(&rex, 0, sizeof(rex));
		memset(&sgl, 0, sizeof(sgl));

		sgl.sg_nr.num = 1;
		sgl.sg_iovs = &val_iov;

		key_str = str;
		if (update) {
			val_str = strchr(str, VC_SEP_VAL);
			if (val_str == NULL) {
				D_ERROR("Invalid parameters %s\n", str);
				return -1;
			}
			*val_str = '\0';
			str = ++val_str;
		}

		str = strchr(str, VC_SEP);
		if (str != NULL) {
			*str = '\0';
			str++;
		}

		dkey.iov_len = strlen(key_str);
		strcpy(key_buf, key_str);

		if (val_str != NULL) {
			val_iov.iov_len = strlen(val_str);
			strcpy(val_buf, val_str);
		}

		vio.vd_recxs	= &rex;
		vio.vd_nr	= 1;
		rex.rx_nr	= 1;

		if (update) {
			D_DEBUG(DF_MISC, "Update %s : %s\n", key_buf, val_buf);
			rex.rx_rsize = val_iov.iov_len;
			rc = vos_obj_update(vc_coh, vc_oid, vc_epoch, &dkey,
					    1, &vio, &sgl, NULL);
		} else {
			D_DEBUG(DF_MISC, "Fetch %s\n", key_buf);
			memset(val_buf, 0, sizeof(val_buf));
			rc = vos_obj_fetch(vc_coh, vc_oid, vc_epoch, &dkey,
					   1, &vio, &sgl, NULL);
		}

		if (rc != 0) {
			D_ERROR("Failed to %s record %s\n",
				update ? "update" : "fetch", key_buf);
			break;
		}
		D_PRINT("%s : %s\n", key_buf, val_buf);
		count++;
	}
	D_PRINT("Totally %s %d records\n",
		update ? "updated" : "fetched", count);
	return rc;
}

static struct option vos_ops[] = {
	{ "update",	required_argument,	NULL,	'u'	},
	{ "find",	required_argument,	NULL,	'f'	},
	{ NULL,		0,			NULL,	0	},
};

int
main(int argc, char **argv)
{
	int	rc;

	rc = vos_obj_tree_register(NULL);
	if (rc != 0) {
		D_ERROR("Failed to register vos trees\n");
		return rc;
	}

	optind = 0;
	while ((rc = getopt_long(argc, argv, "u:f:",
				 vos_ops, NULL)) != -1) {
		switch (rc) {
		case 'u':
			rc = vc_obj_operator(true, optarg);
			break;
		case 'f':
			rc = vc_obj_operator(false, optarg);
			break;
		default:
			D_PRINT("Unsupported command %c\n", rc);
			break;
		}
	}
	return 0;
}
