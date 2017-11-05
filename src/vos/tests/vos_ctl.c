/**
 * (C) Copyright 2017 Intel Corporation.
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
#define DDSUBSYS	DDFAC(tests)

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <uuid/uuid.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>

#include <daos_srv/vos.h>
#include <daos/tests_lib.h>
#include <vos_internal.h>
#include <vts_common.h>

/**
 * An example for integer key evtree .
 */

#define CTL_SEP_VAL		'='
#define CTL_SEP			','

static struct vos_test_ctx	 ctl_tcx;
static daos_epoch_t		 ctl_epoch;
static daos_unit_oid_t		 ctl_oid;
static char			*ctl_dkey;
static char			*ctl_akey;
static char			*ctl_val;
static uuid_t			 ctl_cookie;
static daos_iov_t		 ctl_dkey_iov;
static daos_iov_t		 ctl_val_iov;
static daos_sg_list_t		 ctl_sgl;
static daos_iod_t		 ctl_iod;
static daos_recx_t		 ctl_recx;
static int			 ctl_abits;

enum {
	CTL_ARG_EPOCH	= (1 << 0),
	CTL_ARG_OID	= (1 << 1),
	CTL_ARG_DKEY	= (1 << 2),
	CTL_ARG_AKEY	= (1 << 3),
	CTL_ARG_VAL	= (1 << 4),
	CTL_ARG_ALL	= (CTL_ARG_EPOCH | CTL_ARG_OID | CTL_ARG_DKEY |
			   CTL_ARG_AKEY | CTL_ARG_VAL),
};

static int
ctl_list(void)
{
	char			*opstr = "";
	vos_iter_param_t	 param;
	daos_handle_t		 ih;
	vos_iter_type_t		 type;
	int			 n;
	int			 rc;

	memset(&param, 0, sizeof(param));
	param.ip_hdl	    = ctl_tcx.tc_co_hdl;
	param.ip_oid	    = ctl_oid;
	param.ip_dkey	    = ctl_dkey_iov;
	param.ip_epr.epr_lo = ctl_epoch;
	param.ip_epr.epr_hi = ctl_epoch;

	if (!(ctl_abits & CTL_ARG_OID))
		type = VOS_ITER_OBJ;
	else if (!(ctl_abits & CTL_ARG_DKEY))
		type = VOS_ITER_DKEY;
	else
		type = VOS_ITER_AKEY;

	rc = vos_iter_prepare(type, &param, &ih);
	if (rc == -DER_NONEXIST) {
		D__PRINT("No matched object or key\n");
		D__GOTO(out, rc = 0);

	} else if (rc) {
		opstr = "prepare";
		D__GOTO(out, rc);
	}

	n = 0;
	rc = vos_iter_probe(ih, NULL);
	opstr = "probe";
	while (1) {
		vos_iter_entry_t        ent;

		if (rc == -DER_NONEXIST) {
			D__PRINT("Completed, n=%d\n", n);
			D__GOTO(out, rc = 0);
		}

		if (rc == 0) {
			rc = vos_iter_fetch(ih, &ent, NULL);
			opstr = "fetch";
		}

		if (rc)
			D__GOTO(out, rc);

		n++;
		switch (type) {
		case VOS_ITER_OBJ:
			D__PRINT("\t"DF_UOID"\n", DP_UOID(ent.ie_oid));
			break;
		case VOS_ITER_DKEY:
		case VOS_ITER_AKEY:
			D__PRINT("\t%s\n", (char *)ent.ie_key.iov_buf);
			break;
		default:
			D__PRINT("Unsupported\n");
			D__GOTO(out, rc = -1);
		}

		rc = vos_iter_next(ih);
		opstr = "next";
	}
out:
	if (rc)
		D__PRINT("list(%s) failed, rc=%d\n", opstr, rc);
	return rc;
}

#define CTL_BUF_LEN	1024

static int
ctl_cmd_run(char opc, char *args)
{
	char		*str;
	daos_key_t	*dkey;
	daos_key_t	*akey;
	char		 abuf[CTL_BUF_LEN];
	char		 vbuf[CTL_BUF_LEN];
	int		 rc;

	if (args) {
		strcpy(abuf, args);
		str = daos_str_trimwhite(abuf);
	} else {
		str = NULL;
	}

	ctl_abits = 0;
	memset(&ctl_oid, 0, sizeof(ctl_oid));
	memset(&ctl_iod, 0, sizeof(ctl_iod));
	memset(&ctl_recx, 0, sizeof(ctl_recx));
	memset(&ctl_sgl, 0, sizeof(ctl_sgl));

	while (str && !isspace(*str) && *str != '\0') {
		if (str[1] != CTL_SEP_VAL)
			D__GOTO(failed, rc = -1);

		switch (str[0]) {
		case 'e':
		case 'E':
			ctl_abits |= CTL_ARG_EPOCH;
			ctl_epoch = strtoul(&str[2], NULL, 0);
			break;
		case 'o':
		case 'O':
			ctl_abits |= CTL_ARG_OID;
			ctl_oid.id_pub.lo = strtoul(&str[2], NULL, 0);
			break;
		case 'd':
		case 'D':
			ctl_abits |= CTL_ARG_DKEY;
			ctl_dkey = &str[2];
			break;
		case 'a':
		case 'A':
			ctl_abits |= CTL_ARG_AKEY;
			ctl_akey = &str[2];
			break;
		case 'v':
		case 'V':
			ctl_abits |= CTL_ARG_VAL;
			ctl_val = &str[2];
			break;
		}

		str = strchr(str, CTL_SEP);
		if (str != NULL) {
			*str = '\0';
			str++;
		}
	}

	if (ctl_abits & CTL_ARG_DKEY) {
		daos_iov_set(&ctl_dkey_iov, ctl_dkey, strlen(ctl_dkey) + 1);
		dkey = &ctl_dkey_iov;
	} else {
		dkey = NULL;
	}

	if (ctl_abits & CTL_ARG_AKEY) {
		ctl_recx.rx_nr	  = 1;

		daos_iov_set(&ctl_iod.iod_name, ctl_akey, strlen(ctl_akey) + 1);
		ctl_iod.iod_type  = DAOS_IOD_SINGLE;
		ctl_iod.iod_size  = -1; /* overwrite by CTL_ARG_VAL */
		ctl_iod.iod_nr	  = 1;	/* one recx */
		ctl_iod.iod_recxs = &ctl_recx;

		akey = &ctl_iod.iod_name;
	} else {
		akey = NULL;
	}

	if (ctl_abits & CTL_ARG_VAL) {
		ctl_iod.iod_size = strlen(ctl_val) + 1;
		daos_iov_set(&ctl_val_iov, ctl_val, strlen(ctl_val) + 1);
	} else {
		memset(vbuf, 0, CTL_BUF_LEN);
		daos_iov_set(&ctl_val_iov, vbuf, CTL_BUF_LEN);
	}
	ctl_sgl.sg_nr.num = 1;
	ctl_sgl.sg_iovs = &ctl_val_iov;

	switch (opc) {
	case 'u':
		if (ctl_abits != CTL_ARG_ALL)
			D__GOTO(failed, rc = -1);

		rc = vos_obj_update(ctl_tcx.tc_co_hdl, ctl_oid, ctl_epoch,
				    ctl_cookie, 0xcafe, &ctl_dkey_iov, 1,
				    &ctl_iod, &ctl_sgl);
		break;
	case 'f':
		if (ctl_abits != (CTL_ARG_ALL & ~CTL_ARG_VAL))
			D__GOTO(failed, rc = -1);

		rc = vos_obj_fetch(ctl_tcx.tc_co_hdl, ctl_oid, ctl_epoch,
				   &ctl_dkey_iov, 1, &ctl_iod, &ctl_sgl);
		if (rc == 0)
			D__PRINT("%s\n", strlen(vbuf) ? vbuf : "<NULL>");
		break;
	case 'p':
		if (!(ctl_abits & CTL_ARG_EPOCH) || !(ctl_abits & CTL_ARG_OID))
			D__GOTO(failed, rc = -1);

		rc = vos_obj_punch(ctl_tcx.tc_co_hdl, ctl_oid, ctl_epoch,
				   ctl_cookie, 0, dkey, 1, akey);
		break;
	case 'l':
		if (!(ctl_abits & CTL_ARG_EPOCH))
			ctl_epoch = DAOS_EPOCH_MAX;

		rc = ctl_list();
		break;
	default:
		D__PRINT("Unsupported command %c\n", opc);
		rc = -1;
		break;
	}
	if (rc)
		D__GOTO(failed, rc = -2);

	return rc;
failed:
	if (rc == -1) {
		D__PRINT("Invalid command or parameter string: %c, %s\n",
			opc, args);
		rc = 0;
	} else {
		D__PRINT("Operation failed, rc=%d\n", rc);
	}
	return rc;
}

static struct option ctl_ops[] = {
	{ "update",	required_argument,	NULL,	'u'	},
	{ "fetch",	required_argument,	NULL,	'f'	},
	{ "punch",	required_argument,	NULL,	'p'	},
	{ "list",	required_argument,	NULL,	'l'	},
	{ NULL,		0,			NULL,	0	},
};

int
main(int argc, char **argv)
{
	int	rc;

	rc = daos_debug_init(NULL);
	if (rc != 0)
		return rc;

	rc = vos_init();
	if (rc) {
		fprintf(stderr, "Failed to initialize VOS\n");
		D__GOTO(out_debug, rc);
	}

	rc = vts_ctx_init(&ctl_tcx, (64UL << 20)); /* 64MB */
	if (rc)
		D__GOTO(out_init, rc);

	uuid_generate(ctl_cookie);
	rc = dts_cmd_parser(ctl_ops, "$ > ", ctl_cmd_run);
	if (rc)
		D__GOTO(out_ctx, rc);

	D_EXIT;
 out_init:
	vos_fini();
 out_ctx:
	vts_ctx_fini(&ctl_tcx);
 out_debug:
	daos_debug_fini();
	return rc;
}
