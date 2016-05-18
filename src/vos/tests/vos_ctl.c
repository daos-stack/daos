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
 * Test for KV object creation and destroy.
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
#include <errno.h>
#include <vos_internal.h>
#include <vos_hhash.h>

#define VC_SEP          ','
#define VC_SEP_VAL      ':'

static daos_unit_oid_t	vc_oid = {
	{ .lo = 0, .mid = 1, .hi = 2},
	3,
};

static daos_epoch_t	vc_epoch = 1;

#define VC_STR_SIZE	1024

bool
file_exists(const char *filename)
{
	FILE *fp;

	fp = fopen(filename, "r");
	if (fp) {
		fclose(fp);
		return true;
	}
	return false;
}

static int
vc_obj_operator(bool update, char *str, daos_handle_t vc_coh)
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
				update ? "update" : "lookup", key_buf);
			break;
		}
		D_PRINT("%s : %s\n", key_buf, val_buf);
		count++;
	}
	D_PRINT("Totally %s %d records\n",
	       update ? "updated" : "lookedup", count);
	return rc;
}

static struct option vos_ops[] = {
	{ "update",	required_argument,	NULL,	'u'	},
	{ "lookup",	required_argument,	NULL,	'l'	},
	{ "filename",	required_argument,	NULL,	'f'	},
	{ NULL,		0,			NULL,	 0	},
};

int
main(int argc, char **argv)
{
	int			rc = 0;
	char			*ustr = NULL, *lstr = NULL;
	char			*fname = NULL;
	uuid_t			pool_uuid, co_uuid;
	static daos_handle_t	vc_coh, vp_poh;

	optind = 0;
	while ((rc = getopt_long(argc, argv, "u:l:f:",
				 vos_ops, NULL)) != -1) {
		switch (rc) {
		case 'u':
			ustr = strdup(optarg);
			break;
		case 'l':
			lstr = strdup(optarg);
			break;
		case 'f':
			fname = strdup(optarg);
			break;
		default:
			D_PRINT("Unsupported command %c\n", rc);
			break;
		}
	}
	if (!fname) {
		D_ERROR("Require PMEM File name\n");
		rc = EINVAL;
		goto out;
	}
	if (file_exists(fname))
		remove(fname);

	rc = vos_init();
	if (rc) {
		fprintf(stderr, "VOS init error: %d\n", rc);
		goto out;
	}
	uuid_generate_time_safe(pool_uuid);
	uuid_generate_time_safe(co_uuid);

	rc = vos_pool_create(fname, pool_uuid, PMEMOBJ_MIN_POOL, &vp_poh, NULL);
	if (rc) {
		fprintf(stderr, "vpool create failed with error : %d", rc);
		goto fini;
	}
	fprintf(stdout, "Success creating pool at %s\n", fname);

	rc = vos_co_create(vp_poh, co_uuid, NULL);
	if (rc) {
		fprintf(stderr, "vos container creation error\n");
		goto fini;
	}
	fprintf(stdout, "Success creating container at %s\n", fname);

	rc = vos_co_open(vp_poh, co_uuid, &vc_coh, NULL);
	if (rc) {
		fprintf(stderr, "vos container open error\n");
		goto fini;
	}
	fprintf(stdout, "Success opening container at %s\n", fname);

	if (ustr) {
		rc = vc_obj_operator(true, ustr, vc_coh);
		if (rc) {
			fprintf(stderr, "vos object update error\n");
			goto fini;
		}
		fprintf(stdout, "Success object update at %s\n", fname);
	}

	if (lstr) {
		rc = vc_obj_operator(false, lstr, vc_coh);
		if (rc) {
			fprintf(stderr, "vos object lookup error\n");
			goto fini;
		}
		fprintf(stdout, "Success object lookup at %s\n", fname);
	}

	rc = vos_co_close(vc_coh, NULL);
	if (rc) {
		fprintf(stderr, "vos container close error\n");
		goto fini;
	}
	fprintf(stdout, "Success closing container at %s\n", fname);

	rc = vos_pool_destroy(vp_poh, NULL);
	if (rc)
		fprintf(stderr, "vos pool destroy error\n");
	fprintf(stdout, "Success destroying pool at %s\n", fname);
fini:
	vos_fini();
out:
	if (ustr)
		free(ustr);
	if (lstr)
		free(lstr);
	if (fname) {
		remove(fname);
		free(fname);
	}
	return rc;
}
