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
 *
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
 * This file is part of dsm
 *
 * dsm/tests/dsm_test
 *
 * Author: Wang Di <di.wang@intel.com>
 */
#include <getopt.h>

#include <daos_mgmt.h>
#include <daos_m.h>

#include <daos/common.h>

static struct option opts[] = {
	{ "pool-connect",	0,	NULL,   'c'},
	{ "obj-update",		0,	NULL,	'u'},
	{  NULL,		0,	NULL,	 0 }
};

static int
pool_create(uuid_t uuid)
{
	char		uuid_str[64] = {'\0'};
	daos_rank_list_t	svc;
	int		rc;
	daos_rank_t	ranks[8];

	printf("Creating pool ...\n");

	svc.rl_nr.num = 1;
	svc.rl_nr.num_out = 8;
	svc.rl_ranks = ranks;

	rc = dmg_pool_create(0 /* mode */, 0 /* uid */, 0 /* gid */,
			     "srv_grp" /* grp */, NULL /* tgts */,
			     "pmem" /* dev */, 1024 * 1024 * 1024 /* size */,
			     &svc /* svc */, uuid, NULL /* ev */);
	if (rc == 0) {
		uuid_unparse_lower(uuid, uuid_str);
		printf("Created pool %s.\n", uuid_str);
	} else {
		D_ERROR("dmg_pool_create failed, rc: %d.\n", rc);
	}

	return 0;
}

#define UPDATE_DKEY_SIZE	32
#define UPDATE_DKEY "test_update dkey"
#define UPDATE_AKEY_SIZE	32
#define UPDATE_AKEY "test_update akey"
#define UPDATE_BUF_SIZE 64
#define UPDATE_BUF "test_update string"
#define UPDATE_EPOCH 1
#define UPDATE_CSUM_SIZE	32

static int
do_update(daos_handle_t dh)
{
	daos_iov_t	  val_iov;
	char		  dkey_buf[UPDATE_DKEY_SIZE];
	char		  akey_buf[UPDATE_AKEY_SIZE];
	char		  val_buf[UPDATE_BUF_SIZE];
	char		  csum_buf[UPDATE_CSUM_SIZE];
	daos_dkey_t	  dkey;
	daos_akey_t	  akey;
	daos_recx_t	  rex;
	daos_epoch_range_t erange;
	daos_csum_buf_t	  csum;
	daos_vec_iod_t	  vio;
	daos_sg_list_t	  sgl;
	int		  rc;

	memset(&vio, 0, sizeof(vio));
	memset(&rex, 0, sizeof(rex));
	memset(&sgl, 0, sizeof(sgl));
	memset(&dkey, 0, sizeof(dkey));
	memset(&akey, 0, sizeof(akey));

	daos_iov_set(&dkey, &dkey_buf[0], UPDATE_DKEY_SIZE);
	daos_iov_set(&akey, &akey_buf[0], UPDATE_AKEY_SIZE);
	daos_iov_set(&val_iov, &val_buf[0], UPDATE_BUF_SIZE);
	daos_csum_set(&csum, &csum_buf[0], UPDATE_BUF_SIZE);

	sgl.sg_nr.num = 1;
	sgl.sg_iovs = &val_iov;

	dkey.iov_len = strlen(UPDATE_DKEY);
	strncpy(dkey_buf, UPDATE_DKEY, strlen(UPDATE_DKEY));
	akey.iov_len = strlen(UPDATE_AKEY);
	strncpy(akey_buf, UPDATE_AKEY, strlen(UPDATE_AKEY));

	val_iov.iov_len = strlen(UPDATE_BUF);
	strncpy(val_buf, UPDATE_BUF, strlen(UPDATE_BUF));

	erange.epr_lo = 0;
	erange.epr_hi = DAOS_EPOCH_MAX;

	vio.vd_name	= akey;
	vio.vd_recxs	= &rex;
	vio.vd_csums	= &csum;
	vio.vd_eprs	= &erange;
	vio.vd_nr	= 1;
	rex.rx_nr	= 1;

	vio.vd_recxs	= &rex;
	vio.vd_nr	= 1;
	rex.rx_nr	= 1;

	D_DEBUG(DF_MISC, "Update %s : %s\n", dkey_buf, val_buf);
	rex.rx_rsize = val_iov.iov_len;

	rc = dsm_obj_update(dh, UPDATE_EPOCH, &dkey, 1, &vio, &sgl, NULL);
	if (rc != 0)
		D_ERROR("Failed to record %s:%s\n", akey_buf, val_buf);

	D_DEBUG(DF_MISC, "Fetch %s\n", dkey_buf);
	memset(val_buf, 0, sizeof(val_buf));
	rc = dsm_obj_fetch(dh, UPDATE_EPOCH, &dkey, 1, &vio, &sgl, NULL, NULL);
	if (rc != 0)
		D_ERROR("get record %s:%d\n", dkey_buf, rc);

	D_DEBUG(DF_MISC, "read %s\n", val_buf);

	return rc;
}

static int
test_update(int argc, char *argv[])
{
	char	        uuid_str[64];
	uuid_t		pool_uuid;
	uuid_t		co_uuid;
	daos_handle_t	poh = DAOS_HDL_INVAL;
	daos_handle_t	coh = DAOS_HDL_INVAL;
	daos_handle_t	do_oh = DAOS_HDL_INVAL;
	daos_unit_oid_t	do_oid = {{ .lo = 0, .mid = 1, .hi = 2}, 3};
	int		rc;

	rc = pool_create(pool_uuid);
	if (rc != 0) {
		D_ERROR("failed to create pool %d\n", rc);
		return rc;
	}

	uuid_unparse_lower(pool_uuid, uuid_str);
	D_DEBUG(DF_DSMC, "connecting to pool %s\n", uuid_str);

	rc = dsm_pool_connect(pool_uuid, NULL /* grp */, NULL /* tgts */,
			      DAOS_PC_RW, NULL /* failed */, &poh,
			      NULL /* ev */);
	if (rc != 0) {
		D_ERROR("failed to connect to pool %d\n", rc);
		return rc;
	}

	D_DEBUG(DF_DSMC, "connected to pool %s: "DF_X64"\n", uuid_str,
		poh.cookie);

	/* container uuid */
	uuid_generate(co_uuid);

	rc = dsm_co_create(poh, co_uuid, NULL /* ev */);
	if (rc != 0) {
		D_ERROR("failed to create container %d\n", rc);
		D_GOTO(disconnect, rc);
	}

	rc = dsm_co_open(poh, co_uuid, 0, NULL, &coh, NULL, NULL);
	if (rc != 0) {
		D_ERROR("failed to open container %d\n", rc);
		D_GOTO(co_destroy, rc);
	}

	rc = dsm_obj_open(coh, do_oid, 0, &do_oh, NULL);
	if (rc != 0) {
		D_ERROR("failed to open object %d\n", rc);
		D_GOTO(co_close, rc);
	}

	rc = do_update(do_oh);
	if (rc != 0) {
		D_ERROR("update failed: rc = %d\n", rc);
		D_GOTO(obj_close, rc);
	}

obj_close:
	rc = dsm_obj_close(do_oh, NULL);
	if (rc != 0) {
		D_ERROR("failed to close object %d\n", rc);
		D_GOTO(co_close, rc);
	}
co_close:
	rc = dsm_co_close(coh, NULL);
	if (rc != 0) {
		D_ERROR("failed to close container %d\n", rc);
		D_GOTO(co_destroy, rc);
	}
co_destroy:
	rc = dsm_co_destroy(poh, co_uuid, 1 /* force */, NULL /* ev */);
	if (rc != 0) {
		D_ERROR("failed to destroy container %d\n", rc);
		D_GOTO(disconnect, rc);
	}

disconnect:
	rc = dsm_pool_disconnect(poh, NULL /* ev */);
	if (rc != 0) {
		D_ERROR("failed to disconnect pool %d\n", rc);
		return rc;
	}

	return 0;
}

static int
test_pool_connect(int argc, char *argv[])
{
	char	       *uuid_str = argv[argc - 1];
	uuid_t		uuid;
	daos_handle_t	poh;
	daos_handle_t	coh;
	daos_co_info_t	info;
	int		rc;

	D_DEBUG(DF_DSMC, "connecting to pool %s\n", argv[argc - 1]);

	rc = uuid_parse(uuid_str, uuid);
	if (rc != 0) {
		D_ERROR("invalid pool uuid: %s\n", uuid_str);
		return rc;
	}

	rc = dsm_pool_connect(uuid, NULL /* grp */, NULL /* tgts */,
			      DAOS_PC_RW, NULL /* failed */, &poh,
			      NULL /* ev */);
	if (rc != 0)
		return rc;

	D_DEBUG(DF_DSMC, "connected to pool %s: "DF_X64"\n", uuid_str,
		poh.cookie);

	/* container uuid */
	uuid_generate(uuid);

	rc = dsm_co_create(poh, uuid, NULL /* ev */);
	if (rc != 0)
		return rc;

	rc = dsm_co_open(poh, uuid, DAOS_COO_RW, NULL /* failed */, &coh, &info,
			 NULL /* ev */);
	if (rc != 0)
		return rc;

	printf("container info:\n");
	printf("  hce: "DF_U64"\n", info.ci_epoch_state.es_hce);
	printf("  lre: "DF_U64"\n", info.ci_epoch_state.es_lre);
	printf("  lhe: "DF_U64"\n", info.ci_epoch_state.es_lhe);
	printf("  ghce: "DF_U64"\n", info.ci_epoch_state.es_glb_hce);
	printf("  glre: "DF_U64"\n", info.ci_epoch_state.es_glb_lre);
	printf("  ghpce: "DF_U64"\n", info.ci_epoch_state.es_glb_hpce);

	rc = dsm_co_close(coh, NULL /* ev */);
	if (rc != 0)
		return rc;

	rc = dsm_co_destroy(poh, uuid, 1 /* force */, NULL /* ev */);
	if (rc != 0)
		return rc;

	rc = dsm_pool_disconnect(poh, NULL /* ev */);
	if (rc != 0)
		return rc;

	return 0;
}

int
main(int argc, char **argv)
{
	int	rc = 0;
	int	option;

	/* use full debug dy default for now */
	rc = setenv("DAOS_DEBUG", "-1", false);
	if (rc)
		D_ERROR("failed to enable full debug, %d\n", rc);

	rc = dmg_init();
	if (rc != 0) {
		D_ERROR("dmg init fails: rc = %d\n", rc);
		return rc;
	}

	rc = dsm_init();
	if (rc != 0) {
		D_ERROR("dsm init fails: rc = %d\n", rc);
		D_GOTO(fini_dmg, rc);
	}


	while ((option = getopt_long(argc, argv, "cu", opts, NULL)) != -1) {
		switch (option) {
		default:
			dsm_fini();
			return -EINVAL;
		case 'c':
			rc = test_pool_connect(argc, argv);
			break;
		case 'u':
			rc = test_update(argc, argv);
			break;
		}
		if (rc < 0) {
			D_ERROR("fails on %d: rc = %d\n", option, rc);
			break;
		}
	}

	dsm_fini();
fini_dmg:
	dmg_fini();
	return rc;
}
