/**
 * (C) Copyright 2016 Intel Corporation.
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
#include "common.h"

#define VC_SEP          ','
#define VC_SEP_VAL      ':'

static daos_unit_oid_t	vc_oid = {
	{ .lo = 0, .mid = 1, .hi = 2},
	3,
};

static daos_handle_t	vc_coh;
static daos_epoch_t	vc_epoch = 1;
static bool		vc_zc_mode;

#define VC_STR_SIZE	1024

static int
vc_obj_update(daos_dkey_t *dkey, daos_vec_iod_t *vio, daos_sg_list_t *sgl)
{
	daos_sg_list_t	*vec_sgl;
	daos_iov_t	*vec_iov;
	daos_iov_t	*srv_iov;
	daos_handle_t	 ioh;
	int		 rc;

	if (!vc_zc_mode) {
		rc = vos_obj_update(vc_coh, vc_oid, vc_epoch, dkey, 1, vio,
				    sgl, NULL);
		if (rc != 0)
			D_ERROR("Failed to update: %d\n", rc);

		return rc;
	}

	rc = vos_obj_zc_update_begin(vc_coh, vc_oid, vc_epoch, dkey, 1, vio,
				     &ioh, NULL);
	if (rc != 0) {
		D_ERROR("Failed to prepare ZC update: %d\n", rc);
		return -1;
	}

	srv_iov = &sgl->sg_iovs[0];

	vos_obj_zc_vec2sgl(ioh, 0, &vec_sgl);
	D_ASSERT(vec_sgl->sg_nr.num == 1);
	vec_iov = &vec_sgl->sg_iovs[0];

	D_ASSERT(srv_iov->iov_len == vec_iov->iov_len);
	memcpy(vec_iov->iov_buf, srv_iov->iov_buf, srv_iov->iov_len);

	rc = vos_obj_zc_update_end(ioh, dkey, 1, vio, 0, NULL);
	if (rc != 0)
		D_ERROR("Failed to submit ZC update: %d\n", rc);

	return rc;
}

static int
vc_obj_fetch(daos_dkey_t *dkey, daos_vec_iod_t *vio, daos_sg_list_t *sgl)
{
	daos_sg_list_t	*vec_sgl;
	daos_iov_t	*vec_iov;
	daos_iov_t	*dst_iov;
	daos_handle_t	 ioh;
	int		 rc;

	if (!vc_zc_mode) {
		rc = vos_obj_fetch(vc_coh, vc_oid, vc_epoch, dkey, 1, vio,
				   sgl, NULL);
		if (rc != 0)
			D_ERROR("Failed to fetch: %d\n", rc);

		return rc;
	}

	rc = vos_obj_zc_fetch_begin(vc_coh, vc_oid, vc_epoch, dkey, 1, vio,
				    &ioh, NULL);
	if (rc != 0) {
		D_ERROR("Failed to prepare ZC update: %d\n", rc);
		return -1;
	}

	dst_iov = &sgl->sg_iovs[0];

	vos_obj_zc_vec2sgl(ioh, 0, &vec_sgl);
	D_ASSERT(vec_sgl->sg_nr.num == 1);
	vec_iov = &vec_sgl->sg_iovs[0];

	D_ASSERT(dst_iov->iov_buf_len >= vec_iov->iov_len);
	memcpy(dst_iov->iov_buf, vec_iov->iov_buf, vec_iov->iov_len);
	dst_iov->iov_len = vec_iov->iov_len;

	rc = vos_obj_zc_fetch_end(ioh, dkey, 1, vio, 0, NULL);
	if (rc != 0)
		D_ERROR("Failed to submit ZC update: %d\n", rc);

	return rc;
}
static int
vc_obj_rw_oper(bool update, char *str)
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
		rex.rx_idx	= daos_hash_string_u32(key_str, dkey.iov_len);
		rex.rx_idx	%= 100; /* more readable index */
		if (update) {
			if (val_iov.iov_len == 0)
				D_DEBUG(DF_MISC, "Punch %s\n", key_buf);
			else
				D_DEBUG(DF_MISC, "Update %s : %s\n",
					key_buf, val_buf);

			rex.rx_rsize = val_iov.iov_len;
			rc = vc_obj_update(&dkey, &vio, &sgl);
		} else {
			D_DEBUG(DF_MISC, "Fetch %s\n", key_buf);

			memset(val_buf, 0, sizeof(val_buf));
			rc = vc_obj_fetch(&dkey, &vio, &sgl);
		}

		if (rc != 0) {
			D_ERROR("Failed to %s record %s\n",
				update ? "update" : "fetch", key_buf);
			break;
		}

		if (val_iov.iov_len == 0)
			D_PRINT("%s : [NULL]\n", key_buf);
		else
			D_PRINT("%s : %s\n", key_buf, val_buf);
		count++;
	}
	D_PRINT("Totally %s %d records\n",
	       update ? "updated" : "fetched", count);
	return rc;
}

static int
vc_obj_iter_oper(bool test_anchor)
{
	vos_iter_param_t  param;
	daos_handle_t	  ih;
	int		  rc;
	int		  dkey_nr = 0;

	memset(&param, 0, sizeof(param));
	param.ip_hdl	= vc_coh;
	param.ip_oid	= vc_oid;
	param.ip_epr.epr_lo = vc_epoch;

	rc = vos_iter_prepare(VOS_ITER_DKEY, &param, &ih);
	if (rc != 0) {
		D_ERROR("Failed to prepare d-key iterator\n");
		return rc;
	}

	rc = vos_iter_probe(ih, NULL);
	if (rc != 0) {
		D_ERROR("Failed to set iterator cursor: %d\n", rc);
		goto out;
	}

	if (test_anchor)
		D_PRINT("Start to iterate with setting anchor\n");
	else
		D_PRINT("Start to iterate\n");

	dkey_nr = 0;
	while (1) {
		vos_iter_entry_t  dkey_ent;
		vos_iter_entry_t  recx_ent;
		daos_handle_t	  recx_ih;
		daos_hash_out_t	  anchor;
		int		  recx_nr = 0;

		rc = vos_iter_fetch(ih, &dkey_ent, NULL);
		if (rc == -DER_NONEXIST) {
			D_PRINT("Finishing d-key iteration\n");
			break;
		}

		if (rc != 0) {
			D_ERROR("Failed to fetch dkey: %d\n", rc);
			goto out;
		}

		param.ip_dkey = dkey_ent.ie_dkey;
		rc = vos_iter_prepare(VOS_ITER_RECX, &param, &recx_ih);
		if (rc != 0) {
			D_ERROR("Failed to create recx iterator: %d\n", rc);
			goto out;
		}

		rc = vos_iter_probe(recx_ih, NULL);
		if (rc != 0 && rc != -DER_NONEXIST) {
			D_ERROR("Failed to set iterator cursor: %d\n", rc);
			goto out;
		}

		while (rc == 0) {
			rc = vos_iter_fetch(recx_ih, &recx_ent, NULL);
			if (rc != 0) {
				D_ERROR("Failed to fetch recx: %d\n", rc);
				goto out;
			}

			recx_nr++;
			if (recx_nr == 1) {
				/* output dkey only if it has matched recx */
				D_PRINT("dkey[%d]: %s\n", dkey_nr,
					 (char *)dkey_ent.ie_dkey.iov_buf);
				dkey_nr++;
			}

			D_PRINT("\trecx %u : %s\n",
				(unsigned int)recx_ent.ie_recx.rx_idx,
				recx_ent.ie_iov.iov_len == 0 ? "[NULL]" :
				(char *)recx_ent.ie_iov.iov_buf);

			rc = vos_iter_next(recx_ih);
			if (rc != 0 && rc != -DER_NONEXIST) {
				D_ERROR("Failed to move cursor: %d\n", rc);
				goto out;
			}
		}
		vos_iter_finish(recx_ih);

		rc = vos_iter_next(ih);
		if (rc == -DER_NONEXIST)
			break;

		if (rc != 0) {
			D_ERROR("Failed to move cursor: %d\n", rc);
			goto out;
		}

		if (!test_anchor)
			continue;

		/* vos_iter_next() has already moved cursor to the next entry,
		 * this piece of code is only for testing funcationality of
		 * fetching anchor and setting anchor.
		 */
		rc = vos_iter_fetch(ih, &dkey_ent, &anchor);
		if (rc != 0) {
			D_ASSERT(rc != -DER_NONEXIST);
			D_ERROR("Failed to fetch anchor: %d\n", rc);
			goto out;
		}

		rc = vos_iter_probe(ih, &anchor);
		if (rc != 0) {
			D_ASSERT(rc != -DER_NONEXIST);
			D_ERROR("Failed to probe anchor: %d\n", rc);
			goto out;
		}
	}
 out:
	D_PRINT("Enumerated %d dkeys\n", dkey_nr);
	vos_iter_finish(ih);
	return 0;
}

static struct option vos_ops[] = {
	{ "update",	required_argument,	NULL,	'u'	},
	{ "fetch",	required_argument,	NULL,	'f'	},
	{ "epoch",	required_argument,	NULL,	'e'	},
	{ "itr",	no_argument,		NULL,	'i'	},
	{ "itr_anchor",	no_argument,		NULL,	'I'	},
	{ "zc",		required_argument,	NULL,	'z'	},
	{ NULL,		0,			NULL,	 0	},
};

int
main(int argc, char **argv)
{
	struct vos_test_ctx	tcx;
	int			rc;

	rc = vts_ctx_init(&tcx);
	if (rc != 0) {
		D_ERROR("Failed to initialise test context\n");
		return rc;
	}

	vc_coh = tcx.tc_co_hdl;
	optind = 0;

	while ((rc = getopt_long(argc, argv, "u:f:e:z:iI",
				 vos_ops, NULL)) != -1) {
		bool it_anchor = false;

		switch (rc) {
		case 'u':
			rc = vc_obj_rw_oper(true, optarg);
			if (rc != 0) {
				D_ERROR("Update failed\n");
				goto failed;
			}
			break;
		case 'f':
			rc = vc_obj_rw_oper(false, optarg);
			if (rc != 0) {
				D_ERROR("Fetch failed\n");
				goto failed;
			}
			break;
		case 'I':
			it_anchor = true;
			/* fallthrough */
		case 'i':
			rc = vc_obj_iter_oper(it_anchor);
			if (rc != 0) {
				D_ERROR("Iterate failed\n");
				goto failed;
			}
			break;
		case 'e':
			vc_epoch = strtoul(optarg, NULL, 0);
			D_PRINT("Set epoch to "DF_U64"\n", vc_epoch);
			break;
		case 'z':
			if (strcasecmp(optarg, "on") == 0)
				vc_zc_mode = true;
			else if (strcasecmp(optarg, "off") == 0)
				vc_zc_mode = false;

			D_PRINT("Turn %s zero-copy\n",
				vc_zc_mode ? "on" : "off");
			break;
		default:
			D_PRINT("Unsupported command %c\n", rc);
			break;
		}
		D_PRINT("\n");
	}
	D_PRINT("All tests are successful!\n");
failed:
	vts_ctx_fini(&tcx);
	return rc;
}
