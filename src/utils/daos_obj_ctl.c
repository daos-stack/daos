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
#include <daos.h>
#include <daos/common.h>
#include <daos/object.h>
#include <daos/tests_lib.h>
#include <daos/cmd_parser.h>
#include "daos_hdlr.h"

/**
 * An example for integer key evtree .
 */
#define CTL_SEP_VAL		'='
#define CTL_SEP		','
#define CTL_BUF_LEN		512

static daos_unit_oid_t		 ctl_oid;
static daos_handle_t		 ctl_oh;	/* object open handle */
static unsigned int		 ctl_abits;	/* see CTL_ARG_* */
static d_rank_t			 ctl_svc_rank;	/* pool service leader */
static struct credit_context	 ctl_ctx;

/* available input parameters */
enum {
	CTL_ARG_OID	= (1 << 1),	/* has OID */
	CTL_ARG_DKEY	= (1 << 2),	/* has dkey */
	CTL_ARG_AKEY	= (1 << 3),	/* has akey */
	CTL_ARG_VAL	= (1 << 4),	/* has value */
	CTL_ARG_ALL	= (CTL_ARG_OID | CTL_ARG_DKEY | CTL_ARG_AKEY |
			   CTL_ARG_VAL),
};

static int
ctl_update(struct io_credit *cred)
{

	return daos_obj_update(ctl_oh, DAOS_TX_NONE, 0, &cred->tc_dkey, 1,
			       &cred->tc_iod, &cred->tc_sgl, NULL);
}

static int
ctl_fetch(struct io_credit *cred)
{
	return daos_obj_fetch(ctl_oh, DAOS_TX_NONE, 0, &cred->tc_dkey, 1,
			      &cred->tc_iod, &cred->tc_sgl, NULL, NULL);
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

	if (!dkey) {
		rc = daos_obj_punch(ctl_oh, DAOS_TX_NONE, 0, NULL);
	} else if (!akey) {
		rc = daos_obj_punch_dkeys(ctl_oh, DAOS_TX_NONE, 0, 1,
					  dkey, NULL);
	} else {
		rc = daos_obj_punch_akeys(ctl_oh, DAOS_TX_NONE, 0, dkey,
					  1, akey, NULL);
	}

	return rc;
}

#define KDS_NR		128

static int
ctl_daos_list(struct io_credit *cred)
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

	rc = daos_obj_open(ctl_ctx.tsc_coh, ctl_oid.id_pub,
			   DAOS_OO_RW, &ctl_oh, NULL);
	D_ASSERT(!rc);
	*opened = true;
}

static void
ctl_print_usage(void)
{
	printf("daos shell -- interactive function testing shell for DAOS\n");
	printf("Usage:\n");
	printf("update\to=...,d=...,a=...,v=...\n");
	printf("fetch\to=...d=...,a=...\n");
	printf("list\to=...[,d=...]\n");
	printf("punch\to=...[,d=...][,a=...]\n");
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
	bool			 opened = false;

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
		if (!(ctl_abits & CTL_ARG_OID)) {
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
			ctl_obj_open(&opened);
		}

		rc = ctl_daos_list(cred);
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

enum {
	CTL_INIT_MODULE,	/* modules have been loaded */
	CTL_INIT_POOL,		/* pool has been created */
	CTL_INIT_CONT,		/* container has been created */
	CTL_INIT_CREDITS,	/* I/O credits have been initialized */
};

static int
cont_init(struct credit_context *tsc)
{
	daos_handle_t	coh = DAOS_HDL_INVAL;
	int		rc;
	char            uuid_str[37];

	rc = daos_cont_create(tsc->tsc_poh, &(tsc->tsc_cont_uuid), NULL,
			      NULL);
	if (rc != 0)
		goto out;

	uuid_unparse(tsc->tsc_cont_uuid, uuid_str);
	rc = daos_cont_open(tsc->tsc_poh, uuid_str, DAOS_COO_RW, &coh, NULL, NULL);

	tsc->tsc_coh = coh;

out:
	return rc;
}

static void
cont_fini(struct credit_context *tsc)
{
	daos_cont_close(tsc->tsc_coh, NULL);

	/* NB: no container destroy at here, it will be destroyed by pool
	 * destroy later. This is because container destroy could be too
	 * expensive after performance tests.
	 */
}

static void
ctx_fini(struct credit_context *tsc)
{
	switch (tsc->tsc_init) {
	case CTL_INIT_CREDITS:	/* finalize credits */
		credits_fini(tsc);
		/* fall through */
	case CTL_INIT_CONT:	/* close and destroy container */
		cont_fini(tsc);
	}
}

static int
ctx_init(struct credit_context *tsc)
{
	int	rc;
	char	pool_str[37];

	tsc->tsc_init = CTL_INIT_MODULE;

	uuid_unparse((unsigned char *)(tsc->tsc_pool_uuid), pool_str);
	rc = daos_pool_connect(pool_str, NULL,
			       DAOS_PC_RW, &tsc->tsc_poh,
			       NULL /* info */, NULL /* ev */);

	if (rc != 0) {
		fprintf(stderr, "failed to connect to pool "DF_UUIDF
			": %s (%d)\n", DP_UUID(tsc->tsc_pool_uuid),
			d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

	tsc->tsc_init = CTL_INIT_POOL;

	rc = cont_init(tsc);
	if (rc)
		goto out;
	tsc->tsc_init = CTL_INIT_CONT;

	/* initialize I/O credits, which include EQ, event, I/O buffers... */
	rc = credits_init(tsc);
	if (rc)
		goto out;
	tsc->tsc_init = CTL_INIT_CREDITS;

	return 0;
 out:
	fprintf(stderr, "Failed to initialize step=%d, rc=%d\n",
		tsc->tsc_init, rc);
	ctx_fini(tsc);
	return rc;
}

static int
daos_shell(struct cmd_args_s *ap)
{
	int rc;

	assert(ap != NULL);

	uuid_copy(ctl_ctx.tsc_pool_uuid, ap->p_uuid);

	if (uuid_is_null(ap->c_uuid)) {
		uuid_generate(ctl_ctx.tsc_cont_uuid);
	} else {
		uuid_copy(ctl_ctx.tsc_cont_uuid, ap->c_uuid);
	}

	D_INFO("\tDAOS system name: %s\n", ap->sysname);
	D_INFO("\tpool UUID: "DF_UUIDF"\n", DP_UUID(ctl_ctx.tsc_pool_uuid));
	D_INFO("\tcont UUID: "DF_UUIDF"\n", DP_UUID(ctl_ctx.tsc_cont_uuid));

	ctl_ctx.tsc_cred_vsize	= 1024;	/* long enough for console input */
	ctl_ctx.tsc_cred_nr	= -1;	/* sync mode all the time */
	ctl_ctx.tsc_mpi_size	= 1;	/* just one rank */
	ctl_ctx.tsc_mpi_rank	= 0;
	ctl_ctx.tsc_svc.rl_ranks = &ctl_svc_rank;
	ctl_ctx.tsc_svc.rl_nr = 1;

	rc = ctx_init(&ctl_ctx);
	if (rc != 0) {
		fprintf(stderr, "Failed to initialize utility: "DF_RC"\n",
			DP_RC(rc));
		return rc;
	}

	rc = cmd_parser(ctl_ops, "$ > ", ctl_cmd_run);

	if (rc) {
		ctx_fini(&ctl_ctx);
  }
	return rc;
}

int
obj_ctl_shell(struct cmd_args_s *ap)
{
	int	rc;

	switch (ap->sh_op) {
	case SH_DAOS:
		rc = daos_shell(ap);
		break;
	case SH_VOS:
		fprintf(stdout, "Shell 'vos' option not yet implemented\n");
		rc = -DER_NOSYS;
		break;
	default:
		fprintf(stdout, "Shell unknown option\n");
		rc = -DER_INVAL;
	}

	return rc;
}
