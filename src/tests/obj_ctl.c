/**
 * (C) Copyright 2017-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
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
#include <daos/cmd_parser.h>

/**
 * An example for integer key evtree .
 */
#define CTL_SEP_VAL		'='
#define CTL_SEP			','
#define CTL_BUF_LEN		4096

static char			 pmem_file[PATH_MAX];

static long			 ctl_epoch;
static daos_unit_oid_t		 ctl_oid;
static unsigned int		 ctl_abits;	/* see CTL_ARG_* */
static struct credit_context	 ctl_ctx;

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
ctl_update(struct io_credit *cred)
{
	return vos_obj_update(ctl_ctx.tsc_coh, ctl_oid, ctl_epoch, 0xcafe,
			      0, &cred->tc_dkey, 1, &cred->tc_iod, NULL,
			      &cred->tc_sgl);
}

static int
ctl_fetch(struct io_credit *cred)
{
	return vos_obj_fetch(ctl_ctx.tsc_coh, ctl_oid, ctl_epoch, 0,
			     &cred->tc_dkey, 1, &cred->tc_iod,
			     &cred->tc_sgl);
}

static int
ctl_punch(struct io_credit *cred)
{
	daos_key_t *dkey = NULL;
	daos_key_t *akey = NULL;
	int	    rc;

	if (ctl_abits & CTL_ARG_DKEY) {
		dkey = &cred->tc_dkey;
		if (ctl_abits & CTL_ARG_AKEY)
			akey = &cred->tc_iod.iod_name;
	}

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

	return rc;
}

static int
ctl_vos_list(struct io_credit *cred)
{
	char			*opstr = "";
	vos_iter_param_t	 param;
	daos_handle_t		 ih;
	vos_iter_type_t		 type;
	int			 n;
	int			 rc;

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

		rc = vos_iter_next(ih, NULL);
		opstr = "next";
	}
out:
	if (rc)
		D_PRINT("list(%s) failed, rc=%d\n", opstr, rc);
	return rc;
}

static void
ctl_print_usage(void)
{
	printf("obj_ctl -- interactive function testing shell for VOS\n");
	printf("Usage:\n");
	printf("update\to=...,d=...,a=...,v=...,e=...\n");
	printf("fetch\to=...d=...,a=...,e=...\n");
	printf("list\to=...[,d=...][,e=...]\n");
	printf("punch\to=...,e=...[,d=...][,a=...]\n");
	printf("quit\n");
	fflush(stdout);
}

static int
ctl_cmd_run(char opc, char *args)
{
	struct io_credit	*cred;
	char			*dkey = NULL;
	char			*akey = NULL;
	char			*val = NULL;
	char			*str;
	char			 buf[CTL_BUF_LEN];
	int			 rc;

	if (args) {
		strncpy(buf, args, CTL_BUF_LEN);
		buf[CTL_BUF_LEN - 1] = '\0';
		str = daos_str_trimwhite(buf);
	} else {
		str = NULL;
	}

	cred = credit_take(&ctl_ctx);
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
			daos_obj_generate_oid(ctl_ctx.tsc_coh, &ctl_oid.id_pub,
					      0, OC_S1, 0, 0);
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
		}

		rc = ctl_update(cred);
		break;
	case 'f':
		if (ctl_abits != (CTL_ARG_ALL & ~CTL_ARG_VAL)) {
			ctl_print_usage();
			D_GOTO(out, rc = -1);
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
		}

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
main(int argc, char *argv[])
{
	int	rc;

	if (argc == 2)
		goto out_usage;

	uuid_generate(ctl_ctx.tsc_pool_uuid);
	uuid_generate(ctl_ctx.tsc_cont_uuid);

	ctl_ctx.tsc_scm_size	= (128 << 20); /* small one should be enough */
	ctl_ctx.tsc_nvme_size	= (8ULL << 30);
	ctl_ctx.tsc_cred_vsize	= 1024;	/* long enough for console input */
	ctl_ctx.tsc_cred_nr	= -1;	/* sync mode all the time */
	ctl_ctx.tsc_mpi_rank	= 0;
	ctl_ctx.tsc_mpi_size	= 1;	/* just one rank */

	if (argc == 3)
		strncpy(pmem_file, argv[2], PATH_MAX - 1);
	else
		strcpy(pmem_file, "/mnt/daos/vos_ctl.pmem");

	ctl_ctx.tsc_pmem_file = pmem_file;

	rc = dts_ctx_init(&ctl_ctx, &vos_engine);
	if (rc != 0) {
		fprintf(stderr, "Failed to initialize utility: "DF_RC"\n",
			DP_RC(rc));
		return rc;
	}

	rc = cmd_parser(ctl_ops, "$ > ", ctl_cmd_run);
	if (rc)
		dts_ctx_fini(&ctl_ctx);

	return rc;

 out_usage:
	printf("%s [pmem_file]\n", argv[0]);
	return -1;
}
