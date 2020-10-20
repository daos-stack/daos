/**
 * (C) Copyright 2017-2020 Intel Corporation.
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
#define D_LOGFAC	DD_FAC(tests)

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
#include <mpi.h>
#include <daos/common.h>
#include <daos/tests_lib.h>
#include <daos_srv/vos.h>
#include <daos_test.h>
#include <daos/dts.h>

/**
 * An example for integer key evtree .
 */
#define CTL_SEP_VAL		'='
#define CTL_SEP			','
#define CTL_BUF_LEN		4096

static char			 pmem_file[PATH_MAX];
static bool			 daos_mode = true;

static long			 ctl_epoch;
static daos_unit_oid_t		 ctl_oid;
static daos_handle_t		 ctl_oh;	/* object open handle */
static unsigned int		 ctl_abits;	/* see CTL_ARG_* */
static d_rank_t			 ctl_svc_rank;	/* pool service leader */
static struct dts_context	 ctl_ctx;

/* available input parameters */
enum {
	CTL_ARG_EPOCH	= (1 << 0),	/* has epoch */
	CTL_ARG_OID	= (1 << 1),	/* has OID */
	CTL_ARG_DKEY	= (1 << 2),	/* has dkey */
	CTL_ARG_AKEY	= (1 << 3),	/* has akey */
	CTL_ARG_VAL	= (1 << 4),	/* has value */
	CTL_ARG_ALL	= (CTL_ARG_EPOCH | CTL_ARG_OID | CTL_ARG_DKEY |
			   CTL_ARG_AKEY | CTL_ARG_VAL),
};

static int
ctl_update(struct dts_io_credit *cred)
{
	int	rc;

	if (daos_mode) {
		rc = daos_obj_update(ctl_oh, DAOS_TX_NONE, 0, &cred->tc_dkey, 1,
				     &cred->tc_iod, &cred->tc_sgl, NULL);
	} else {
		rc = vos_obj_update(ctl_ctx.tsc_coh, ctl_oid, ctl_epoch, 0xcafe,
				    0, &cred->tc_dkey, 1, &cred->tc_iod, NULL,
				    &cred->tc_sgl);
	}
	return rc;
}

static int
ctl_fetch(struct dts_io_credit *cred)
{
	int	rc;

	if (daos_mode) {
		rc = daos_obj_fetch(ctl_oh, DAOS_TX_NONE, 0, &cred->tc_dkey, 1,
				    &cred->tc_iod, &cred->tc_sgl, NULL, NULL);
	} else {
		rc = vos_obj_fetch(ctl_ctx.tsc_coh, ctl_oid, ctl_epoch, 0,
				   &cred->tc_dkey, 1, &cred->tc_iod,
				   &cred->tc_sgl);
	}
	return rc;
}

static int
ctl_punch(struct dts_io_credit *cred)
{
	daos_key_t *dkey = NULL;
	daos_key_t *akey = NULL;
	int	    rc;

	if (ctl_abits & CTL_ARG_DKEY) {
		dkey = &cred->tc_dkey;
		if (ctl_abits & CTL_ARG_AKEY)
			akey = &cred->tc_iod.iod_name;
	}

	if (daos_mode) {
		if (!dkey) {
			rc = daos_obj_punch(ctl_oh, DAOS_TX_NONE, 0, NULL);

		} else if (!akey) {
			rc = daos_obj_punch_dkeys(ctl_oh, DAOS_TX_NONE, 0, 1,
						  dkey, NULL);
		} else {
			rc = daos_obj_punch_akeys(ctl_oh, DAOS_TX_NONE, 0, dkey,
						  1, akey, NULL);
		}
	} else {
		int	flags;

		if (ctl_epoch < 0) {
			flags = VOS_OF_REPLAY_PC;
			ctl_epoch *= -1;
		} else {
			flags = 0;
		}

		rc = vos_obj_punch(ctl_ctx.tsc_coh, ctl_oid, ctl_epoch,
				   0, flags, dkey, 1, akey, NULL);
		if (rc == -DER_NO_PERM) {
			D_PRINT("permission denied\n");
			rc = 0; /* ignore it */
		}
	}
	return rc;
}

static int
ctl_vos_list(struct dts_io_credit *cred)
{
	char			*opstr = "";
	vos_iter_param_t	 param;
	daos_handle_t		 ih;
	vos_iter_type_t		 type;
	int			 n;
	int			 rc;

	D_ASSERT(!daos_mode);

	memset(&param, 0, sizeof(param));
	param.ip_hdl	    = ctl_ctx.tsc_coh;
	param.ip_oid	    = ctl_oid;
	param.ip_dkey	    = cred->tc_dkey;
	param.ip_epr.epr_lo = ctl_epoch;
	param.ip_epr.epr_hi = ctl_epoch;

	if (!(ctl_abits & CTL_ARG_OID))
		type = VOS_ITER_OBJ;
	else if (!(ctl_abits & CTL_ARG_DKEY))
		type = VOS_ITER_DKEY;
	else
		type = VOS_ITER_AKEY;

	rc = vos_iter_prepare(type, &param, &ih, NULL);
	if (rc == -DER_NONEXIST) {
		D_PRINT("No matched object or key\n");
		D_GOTO(out, rc = 0);

	} else if (rc) {
		opstr = "prepare";
		D_GOTO(out, rc);
	}

	n = 0;
	rc = vos_iter_probe(ih, NULL);
	opstr = "probe";
	while (1) {
		vos_iter_entry_t        ent;

		if (rc == -DER_NONEXIST) {
			D_PRINT("Completed, n=%d\n", n);
			D_GOTO(out, rc = 0);
		}

		if (rc == 0) {
			rc = vos_iter_fetch(ih, &ent, NULL);
			opstr = "fetch";
		}

		if (rc)
			D_GOTO(out, rc);

		n++;
		switch (type) {
		case VOS_ITER_OBJ:
			D_PRINT("\t"DF_UOID"\n", DP_UOID(ent.ie_oid));
			break;
		case VOS_ITER_DKEY:
		case VOS_ITER_AKEY:
			D_PRINT("\t%s\n", (char *)ent.ie_key.iov_buf);
			break;
		default:
			D_PRINT("Unsupported\n");
			D_GOTO(out, rc = -1);
		}

		rc = vos_iter_next(ih);
		opstr = "next";
	}
out:
	if (rc)
		D_PRINT("list(%s) failed, rc=%d\n", opstr, rc);
	return rc;
}

#define KDS_NR		128

static int
ctl_daos_list(struct dts_io_credit *cred)
{
	char		*kstr;
	char		 kbuf[CTL_BUF_LEN];
	uint32_t	 knr = KDS_NR;
	daos_key_desc_t	 kds[KDS_NR] = {0};
	daos_anchor_t	 anchor;
	int		 i;
	int		 rc = 0;
	int		 total = 0;

	memset(&anchor, 0, sizeof(anchor));
	while (!daos_anchor_is_eof(&anchor)) {
		memset(kbuf, 0, CTL_BUF_LEN);
		d_iov_set(&cred->tc_val, kbuf, CTL_BUF_LEN);

		if (!(ctl_abits & CTL_ARG_OID)) {
			fprintf(stderr, "Cannot list object for now\n");
			return -DER_INVAL;

		} else if (!(ctl_abits & CTL_ARG_DKEY)) {
			rc = daos_obj_list_dkey(ctl_oh, DAOS_TX_NONE, &knr,
						kds, &cred->tc_sgl, &anchor,
						NULL);

		} else if (!(ctl_abits & CTL_ARG_AKEY)) {
			rc = daos_obj_list_akey(ctl_oh, DAOS_TX_NONE,
						&cred->tc_dkey, &knr, kds,
						&cred->tc_sgl, &anchor, NULL);
		}

		if (rc) {
			fprintf(stderr, "Failed to list keys: "DF_RC"\n",
				DP_RC(rc));
			return rc;
		}

		total += knr;
		for (i = 0, kstr = kbuf; i < knr; i++) {
			D_PRINT("%s\n", kstr);
			kstr += kds[i].kd_key_len;
		}
	}
	D_PRINT("total %d keys\n", total);
	return 0;
}

static inline void
ctl_obj_open(bool *opened)
{
	int rc;

	if (daos_mode) {
		rc = daos_obj_open(ctl_ctx.tsc_coh, ctl_oid.id_pub,
				   DAOS_OO_RW, &ctl_oh, NULL);
		D_ASSERT(!rc);
		*opened = true;
	}
}

static void
ctl_print_usage(void)
{
	printf("daos_ctl -- interactive function testing shell for DAOS\n");
	printf("Usage:\n");
	printf("update\to=...,d=...,a=...,v=...,e=...\n");
	printf("fetch\to=...d=...,a=...,e=...\n");
	printf("list\to=...[,d=...][,e=...]\n");
	printf("punch\to=...,e=...[,d=...][,a=...]\n");
	printf("quit\n");
	fflush(stdout);
}

int
ctl_cmd_run(char opc, char *args)
{
	struct dts_io_credit	*cred;
	char			*dkey = NULL;
	char			*akey = NULL;
	char			*val = NULL;
	char			*str;
	char			 buf[CTL_BUF_LEN];
	int			 rc;
	bool			 opened = false;

	if (args) {
		strncpy(buf, args, CTL_BUF_LEN);
		buf[CTL_BUF_LEN - 1] = '\0';
		str = daos_str_trimwhite(buf);
	} else {
		str = NULL;
	}

	cred = dts_credit_take(&ctl_ctx);
	D_ASSERT(cred);

	ctl_abits = 0;
	memset(&ctl_oid, 0, sizeof(ctl_oid));
	memset(&cred->tc_sgl, 0, sizeof(cred->tc_sgl));
	memset(&cred->tc_iod, 0, sizeof(cred->tc_iod));
	memset(&cred->tc_recx, 0, sizeof(cred->tc_recx));

	while (str && !isspace(*str) && *str != '\0') {
		if (str[1] != CTL_SEP_VAL)
			D_GOTO(out, rc = -1);

		switch (str[0]) {
		case 'e':
		case 'E':
			ctl_abits |= CTL_ARG_EPOCH;
			ctl_epoch = strtol(&str[2], NULL, 0);
			break;
		case 'o':
		case 'O':
			ctl_abits |= CTL_ARG_OID;
			ctl_oid.id_pub.lo = strtoul(&str[2], NULL, 0);
			if (!daos_mode)
				break;

			daos_obj_generate_id(&ctl_oid.id_pub, 0,
					     OC_S1, 0);
			break;
		case 'd':
		case 'D':
			ctl_abits |= CTL_ARG_DKEY;
			dkey = &str[2];
			break;
		case 'a':
		case 'A':
			ctl_abits |= CTL_ARG_AKEY;
			akey = &str[2];
			break;
		case 'v':
		case 'V':
			ctl_abits |= CTL_ARG_VAL;
			val = &str[2];
			break;
		}

		str = strchr(str, CTL_SEP);
		if (str != NULL) {
			*str = '\0';
			str++;
		}
	}

	if ((ctl_abits & CTL_ARG_DKEY) && dkey != NULL) {
		strncpy(cred->tc_dbuf, dkey, DTS_KEY_LEN);
		cred->tc_dbuf[DTS_KEY_LEN - 1] = '\0';
		d_iov_set(&cred->tc_dkey, cred->tc_dbuf,
			     strlen(cred->tc_dbuf) + 1);
	}

	if ((ctl_abits & CTL_ARG_AKEY) && akey != NULL) {
		strncpy(cred->tc_abuf, akey, DTS_KEY_LEN);
		cred->tc_abuf[DTS_KEY_LEN - 1] = '\0';
		d_iov_set(&cred->tc_iod.iod_name, cred->tc_abuf,
			     strlen(cred->tc_abuf) + 1);

		cred->tc_iod.iod_type	= DAOS_IOD_SINGLE;
		cred->tc_iod.iod_size	= -1; /* overwrite by CTL_ARG_VAL */
		cred->tc_iod.iod_nr	= 1;	/* one recx */
		cred->tc_iod.iod_recxs	= &cred->tc_recx;
		cred->tc_recx.rx_nr	= 1;
	}

	if ((ctl_abits & CTL_ARG_VAL) && val != NULL) {
		cred->tc_iod.iod_size = strlen(val) + 1;
		strncpy(cred->tc_vbuf, val, ctl_ctx.tsc_cred_vsize);
		cred->tc_vbuf[ctl_ctx.tsc_cred_vsize - 1] = '\0';
		d_iov_set(&cred->tc_val, cred->tc_vbuf,
			     strlen(cred->tc_vbuf) + 1);
	} else {
		memset(cred->tc_vbuf, 0, ctl_ctx.tsc_cred_vsize);
		d_iov_set(&cred->tc_val, cred->tc_vbuf,
			     ctl_ctx.tsc_cred_vsize);
	}
	cred->tc_sgl.sg_nr = 1;
	cred->tc_sgl.sg_iovs = &cred->tc_val;

	switch (opc) {
	case 'u':
		if (ctl_abits != CTL_ARG_ALL) {
			ctl_print_usage();
			D_GOTO(out, rc = -1);
		} else {
			ctl_obj_open(&opened);
		}

		rc = ctl_update(cred);
		break;
	case 'f':
		if (ctl_abits != (CTL_ARG_ALL & ~CTL_ARG_VAL)) {
			ctl_print_usage();
			D_GOTO(out, rc = -1);
		} else {
			ctl_obj_open(&opened);
		}

		rc = ctl_fetch(cred);
		if (rc == 0) {
			D_PRINT("%s\n", strlen(cred->tc_vbuf) ?
					 cred->tc_vbuf : "<NULL>");
		}
		break;
	case 'p':
		if (!(ctl_abits & CTL_ARG_EPOCH) ||
		    !(ctl_abits & CTL_ARG_OID)) {
			ctl_print_usage();
			D_GOTO(out, rc = -1);
		} else {
			ctl_obj_open(&opened);
		}

		rc = ctl_punch(cred);
		break;
	case 'l':
		if (!(ctl_abits & CTL_ARG_OID)) {
			ctl_print_usage();
			D_GOTO(out, rc = -1);
		} else {
			if (!(ctl_abits & CTL_ARG_EPOCH))
				ctl_epoch = DAOS_EPOCH_MAX;
			ctl_obj_open(&opened);
		}

		if (daos_mode)
			rc = ctl_daos_list(cred);
		else
			rc = ctl_vos_list(cred);
		break;
	case 'h':
		ctl_print_usage();
		rc = 0;
		break;
	case 'q':
		printf("quitting ...\n");
		rc = -ESHUTDOWN;
		break;
	default:
		D_GOTO(out, rc = -1);
	}
	if (rc && rc != -ESHUTDOWN)
		D_GOTO(out, rc = -2);
out:
	if (opened)
		daos_obj_close(ctl_oh, NULL);

	switch (rc) {
	case -2: /* real failure */
		D_PRINT("Operation failed, rc="DF_RC"\n",
			DP_RC(rc));
		break;

	case -1: /* invalid input */
		D_PRINT("Invalid command or parameter string: %c, %s\n",
			opc, args);
		rc = 0; /* ignore input error */
		break;
	}
	return rc;
}

static struct option ctl_ops[] = {
	{ "update",	required_argument,	NULL,	'u'	},
	{ "fetch",	required_argument,	NULL,	'f'	},
	{ "punch",	required_argument,	NULL,	'p'	},
	{ "list",	required_argument,	NULL,	'l'	},
	{ "help",	no_argument,		NULL,	'h'	},
	{ "quit",	no_argument,		NULL,	'q'	},
	{ NULL,		0,			NULL,	0	},
};

int
shell(int argc, char *argv[])
{
	int	rc;

	if (argc < 3)
		goto out_usage;

	uuid_generate(ctl_ctx.tsc_pool_uuid);
	uuid_generate(ctl_ctx.tsc_cont_uuid);

	ctl_ctx.tsc_scm_size	= (128 << 20); /* small one should be enough */
	ctl_ctx.tsc_nvme_size	= (8ULL << 30);
	ctl_ctx.tsc_cred_vsize	= 1024;	/* long enough for console input */
	ctl_ctx.tsc_cred_nr	= -1;	/* sync mode all the time */
	ctl_ctx.tsc_mpi_rank	= 0;
	ctl_ctx.tsc_mpi_size	= 1;	/* just one rank */

	if (!strcasecmp(argv[2], "vos")) {
		daos_mode = false;
		if (argc == 4)
			strncpy(pmem_file, argv[3], PATH_MAX - 1);
		else
			strcpy(pmem_file, "/mnt/daos/vos_ctl.pmem");

		ctl_ctx.tsc_pmem_file = pmem_file;

	} else if (!strcasecmp(argv[2], "daos")) {
		ctl_ctx.tsc_svc.rl_ranks = &ctl_svc_rank;
		ctl_ctx.tsc_svc.rl_nr = 1;

	} else {
		fprintf(stderr, "Unknown test mode %s\n", argv[2]);
		goto out_usage;
	}

	rc = dts_ctx_init(&ctl_ctx);
	if (rc != 0) {
		fprintf(stderr, "Failed to initialize utility: "DF_RC"\n",
			DP_RC(rc));
		return rc;
	}

	rc = dts_cmd_parser(ctl_ops, "$ > ", ctl_cmd_run);
	if (rc)
		D_GOTO(out_ctx, rc);

 out_ctx:
	dts_ctx_fini(&ctl_ctx);
	return rc;

 out_usage:
	fprintf(stderr, "%s %s daos|vos [pmem_file]\n", argv[0], argv[1]);
	return -DER_INVAL;
}
