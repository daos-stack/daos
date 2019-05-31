/**
 * (C) Copyright 2016-2019 Intel Corporation.
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
 * daos(8): DAOS Container and Object Management Utility
 */

#define D_LOGFAC	DD_FAC(client)

#include <getopt.h>
#include <stdio.h>
#include <daos.h>
#include <daos/common.h>
#include <daos/rpc.h>
#include <daos/debug.h>

#include "daos_types.h"
#include "daos_api.h"

const char		*default_group = DAOS_DEFAULT_GROUP_ID;

enum cont_op {
	CONT_CREATE,
	CONT_DESTROY,
	CONT_LIST_OBJS,
	CONT_QUERY,
	CONT_STAT,
	CONT_GET_PROP,
	CONT_SET_PROP,
	CONT_LIST_ATTRS,
	CONT_DEL_ATTR,
	CONT_GET_ATTR,
	CONT_SET_ATTR,
	CONT_CREATE_SNAP,
	CONT_LIST_SNAPS,
	CONT_DESTROY_SNAP,
	CONT_ROLLBACK
};

enum pool_op {
	POOL_LIST_CONTAINERS,
	POOL_QUERY,
	POOL_STAT,
	POOL_GET_PROP,
	POOL_GET_ATTR,
	POOL_LIST_ATTRS
};

enum obj_op {
	OBJ_GET_LAYOUT,
	OBJ_DUMP
};

static enum cont_op
cont_op_parse(const char *str)
{
	if (strcmp(str, "create") == 0)
		return CONT_CREATE;
	else if (strcmp(str, "destroy") == 0)
		return CONT_DESTROY;
	else if (strcmp(str, "list-objects") == 0)
		return CONT_LIST_OBJS;
	else if (strcmp(str, "list-obj") == 0)
		return CONT_LIST_OBJS;
	else if (strcmp(str, "query") == 0)
		return CONT_QUERY;
	else if (strcmp(str, "stat") == 0)
		return CONT_STAT;
	else if (strcmp(str, "get-prop") == 0)
		return CONT_GET_PROP;
	else if (strcmp(str, "set-prop") == 0)
		return CONT_SET_PROP;
	else if (strcmp(str, "list-attrs") == 0)
		return CONT_LIST_ATTRS;
	else if (strcmp(str, "del-attr") == 0)
		return CONT_DEL_ATTR;
	else if (strcmp(str, "get-attr") == 0)
		return CONT_GET_ATTR;
	else if (strcmp(str, "set-attr") == 0)
		return CONT_SET_ATTR;
	else if (strcmp(str, "create-snap") == 0)
		return CONT_CREATE_SNAP;
	else if (strcmp(str, "list-snaps") == 0)
		return CONT_LIST_SNAPS;
	else if (strcmp(str, "destroy-snap") == 0)
		return CONT_DESTROY_SNAP;
	else if (strcmp(str, "rollback") == 0)
		return CONT_ROLLBACK;
	return -1;
}

/* Pool operations read-only here. See dmg for full pool management */
static enum pool_op
pool_op_parse(const char *str)
{
	if (strcmp(str, "list-containers") == 0)
		return POOL_LIST_CONTAINERS;
	else if (strcmp(str, "list-cont") == 0)
		return POOL_LIST_CONTAINERS;
	else if (strcmp(str, "query") == 0)
		return POOL_QUERY;
	else if (strcmp(str, "stat") == 0)
		return POOL_STAT;
	else if (strcmp(str, "get-prop") == 0)
		return POOL_GET_PROP;
	else if (strcmp(str, "get-attr") == 0)
		return POOL_GET_ATTR;
	else if (strcmp(str, "list-attrs") == 0)
		return POOL_LIST_ATTRS;
	return -1;
}

/* cmd_args_s: consolidated result of parsing command-line arguments
 * for pool, cont, obj commands, much of which is common.
 */

struct cmd_args_s {
	enum pool_op	p_op;		/* pool sub-command */
	enum cont_op	c_op;		/* container sub-command */
	const char	*group;		/* --group */
	uuid_t		pool_uuid;	/* --pool */
	uuid_t		cont_uuid;	/* --cont */
	const char	*mdsrv_str;	/* --svc */
	d_rank_list_t	*mdsrv;
	const char	*attrname_str;	/* --attr attribute name */
	const char	*value_str;	/* --value attribute value */
	const char	*path_str;	/* --path container in namespace */
	const char	*type_str;	/* --type container type */
	const char	*oclass_str;	/* --oclass object class */
	daos_size_t	chunk_size;	/* --chunk_size object chunk size */
	const char	*snapname_str;	/* --snap container snapshot name */
	daos_epoch_t	epc;		/* --epc container epoch */
	const char	*epcrange_str;	/* --epcrange container epochs range */
	daos_epoch_t	epcrange_begin;
	daos_epoch_t	epcrange_end;
	FILE		*ostream;	/* help_hdlr(), where to print */
};


typedef int (*command_hdlr_t)(struct cmd_args_s *ap);

static void
cmd_args_init(struct cmd_args_s *ap)
{
	if (ap == NULL)
		return;
	ap->p_op = -1;
	ap->c_op = -1;
	ap->group = default_group;
	uuid_clear(ap->pool_uuid);
	uuid_clear(ap->cont_uuid);
	ap->mdsrv_str = NULL;
	ap->mdsrv = NULL;
	ap->attrname_str = NULL;
	ap->value_str = NULL;
	ap->path_str = NULL;
	ap->type_str = NULL;
	ap->oclass_str = NULL;
	ap->chunk_size = 0;
	ap->snapname_str = NULL;
	ap->epc = 0;
	ap->epcrange_str = NULL;
	ap->epcrange_begin = 0;
	ap->epcrange_end = 0;
}


static void
cmd_args_print(struct cmd_args_s *ap)
{
	if (ap == NULL)
		return;

	D_INFO("\tgroup=%s\n", ap->group);
	D_INFO("\tpool UUID: "DF_UUIDF"\n", DP_UUID(ap->pool_uuid));
	D_INFO("\tcont UUID: "DF_UUIDF"\n", DP_UUID(ap->cont_uuid));

	D_INFO("\tpool svc: parsed %u ranks from input %s\n",
		ap->mdsrv ? ap->mdsrv->rl_nr : 0,
		ap->mdsrv_str ? ap->mdsrv_str : "NULL");

	D_INFO("\tattr: name=%s, value=%s\n",
		ap->attrname_str ? ap->attrname_str : "NULL",
		ap->value_str ? ap->value_str : "NULL");
	D_INFO("\tpath=%s, type=%s, oclass=%s, chunk_size="DF_U64"\n",
		ap->path_str ? ap->path_str : "NULL",
		ap->type_str ? ap->type_str : "NULL",
		ap->oclass_str ? ap->oclass_str : "NULL",
		ap->chunk_size);
	D_INFO("\tsnapshot: name=%s, epoch="DF_U64", epoch range=%s "
		"("DF_U64"-"DF_U64")\n",
		ap->snapname_str ? ap->snapname_str : "NULL",
		ap->epc,
		ap->epcrange_str ? ap->epcrange_str : "NULL",
		ap->epcrange_begin, ap->epcrange_end);
}

static daos_size_t
tobytes(const char *str)
{
	daos_size_t	 size;
	char		*end;

	size = strtoull(str, &end, 0);
	/* Prevent negative numbers from turning into unsigned */
	if (str != NULL && str[0] == '-') {
		fprintf(stderr, "WARNING bytes < 0 (string %s)"
				"converted to "DF_U64" : using 0 instead\n",
				str, size);
		size = 0;
		return size;
	}

	/** no suffix used */
	if (*end == '\0')
		return size;

	/** let's be permissive and allow MB, Mb, mb ...*/
	if (*(end + 1) != '\0' &&
	    ((*(end + 1) != 'b' && *(end + 1) != 'B') || (*(end + 2) != '\0')))
		return 0;

	switch (*end) {
	case 'b':
	case 'B':
		break;
	case 'k':
	case 'K':
		size <<= 10;
		break;
	case 'm':
	case 'M':
		size <<= 20;
		break;
	case 'g':
	case 'G':
		size <<= 30;
		break;
	case 't':
	case 'T':
		size <<= 40;
		break;
	case 'p':
	case 'P':
		size <<= 50;
		break;
	case 'e':
	case 'E':
		size <<= 60;
		break;
	default:
		return 0;
	}

	return size;
}

/* acopy_str()
 * Allocate dest string and copy source contents.
 * Return NULL if not possible. Caller ensures src is not NULL.
 */
static char *
acopy_str(const char *src)
{
	char	*dst = NULL;

	assert(src != NULL);

	/* TODO: put a reasonable max on strlen(src) */
	D_ALLOC_ARRAY(dst, strlen(src) + 1);
	if (dst != NULL)
		strcpy(dst, src);

	return dst;
}

static int
epoch_range_parse(struct cmd_args_s *ap)
{
	const char	*sep = "-";
	char		*s, *s_saved;
	char		*end_s;
	char		*p;

	s = s_saved = strdup(ap->epcrange_str);
	if (s == NULL) {
		fprintf(stderr, "strdup() failed\n");
		goto out;
	}

	/* Get the first integer in the epoch range
	 * Detect negative number and error out.
	 */
	if (s[0] == '-') {
		fprintf(stderr, "epoch range %s cannot contain "
			"a negative epoch number\n", ap->epcrange_str);
		goto out_invalid_format;
	}
	s = strtok_r(s, sep, &p);
	if (s == NULL)
		goto out_invalid_format;
	ap->epcrange_begin = strtoull(s, &end_s, 10);

	/* Get the second integer in the epoch range */
	s = NULL;
	s = strtok_r(s, sep, &p);
	if (s == NULL)
		goto out_invalid_format;
	ap->epcrange_end = strtoull(s, &end_s, 10);

	free(s_saved);

	return 0;

out_invalid_format:
	fprintf(stderr, "epcrange=%s must be in A%sB form\n",
		ap->epcrange_str, sep);
	free(s_saved);

out:
	return -1;
}

static int
common_op_parse_hdlr(int argc, char *argv[], struct cmd_args_s *ap)
{
	struct option		options[] = {
		{"group",	required_argument,	NULL,	'G'},
		{"pool",	required_argument,	NULL,	'p'},
		{"svc",		required_argument,	NULL,	'm'},
		{"cont",	required_argument,	NULL,	'c'},
		{"attr",	required_argument,	NULL,	'a'},
		{"value",	required_argument,	NULL,	'v'},
		{"path",	required_argument,	NULL,	'd'},
		{"type",	required_argument,	NULL,	't'},
		{"oclass",	required_argument,	NULL,	'o'},
		{"chunk_size",	required_argument,	NULL,	'z'},
		{"snap",	required_argument,	NULL,	's'},
		{"epc",		required_argument,	NULL,	'e'},
		{"epcrange",	required_argument,	NULL,	'r'},
		{NULL,		0,			NULL,	0}
	};
	int			rc;
	const int		RC_PRINT_HELP = 2;
	const int		RC_NO_HELP = -2;
	char			*cmdname = NULL;

	assert(ap != NULL);

	cmd_args_init(ap);

	if ((strcmp(argv[1], "container") == 0) ||
	    (strcmp(argv[1], "cont") == 0)) {
		ap->c_op = cont_op_parse(argv[2]);
		if (ap->c_op == -1) {
			fprintf(stderr, "invalid container command: %s\n",
				argv[2]);
			fflush(stderr);
			return RC_PRINT_HELP;
		}
	} else if (strcmp(argv[1], "pool") == 0) {
		ap->p_op = pool_op_parse(argv[2]);
		if (ap->p_op == -1) {
			fprintf(stderr, "invalid pool command: %s\n",
				argv[2]);
			fflush(stderr);
			return RC_PRINT_HELP;
		}
	} else {
		/* main() may catch error. Keep this code just in case. */
		fprintf(stderr, "resource (%s): must be "
				 "pool or container\n", argv[1]);
		fflush(stderr);
		return RC_PRINT_HELP;
	}
	cmdname = acopy_str(argv[2]);

	while ((rc = getopt_long(argc, argv, "", options, NULL)) != -1) {
		switch (rc) {
		case 'G':
			ap->group = acopy_str(optarg);
			break;
		case 'p':
			if (uuid_parse(optarg, ap->pool_uuid) != 0) {
				fprintf(stderr,
					"failed to parse pool UUID: %s\n",
					optarg);
				return RC_NO_HELP;
			}
			break;
		case 'c':
			if (uuid_parse(optarg, ap->cont_uuid) != 0) {
				fprintf(stderr,
					"failed to parse cont UUID: %s\n",
					optarg);
				return RC_NO_HELP;
			}
			break;
		case 'm':
			ap->mdsrv_str = acopy_str(optarg);
			ap->mdsrv = daos_rank_list_parse(ap->mdsrv_str, ",");
			break;

		case 'a':
			ap->attrname_str = acopy_str(optarg);
			break;
		case 'v':
			ap->value_str = acopy_str(optarg);
			break;
		case 'd':
			ap->path_str = acopy_str(optarg);
			break;
		case 't':
			ap->type_str = acopy_str(optarg);
			break;
		case 'o':
			ap->oclass_str = acopy_str(optarg);
			break;
		case 'z':
			ap->chunk_size = tobytes(optarg);
			if (ap->chunk_size == 0 ||
			    (ap->chunk_size == ULLONG_MAX && errno != 0)) {
				fprintf(stderr, "failed to parse chunk_size:"
					"%s\n", optarg);
				return RC_NO_HELP;
			}
			break;
		case 's':
			ap->snapname_str = acopy_str(optarg);
			break;
		case 'e':
			ap->epc = strtoull(optarg, NULL, 10);
			if (ap->epc == 0 ||
			    (ap->epc == ULLONG_MAX && errno != 0)) {
				fprintf(stderr, "failed to parse epc: %s\n",
					optarg);
				return RC_NO_HELP;
			}
			break;
		case 'r':
			ap->epcrange_str = acopy_str(optarg);
			rc = epoch_range_parse(ap);
			if (rc != 0) {
				fprintf(stderr, "failed to parse epcrange\n");
				return RC_NO_HELP;
			}
			break;
		default:
			printf("unknown option : %d\n", rc);
			return RC_PRINT_HELP;
		}
	}

	cmd_args_print(ap);

	/* Check for any unimplemented commands, print help */
	if (ap->p_op != -1 &&
	    (ap->p_op == POOL_LIST_CONTAINERS ||
	     ap->p_op == POOL_STAT ||
	     ap->p_op == POOL_GET_PROP ||
	     ap->p_op == POOL_GET_ATTR ||
	     ap->p_op == POOL_LIST_ATTRS)) {
		fprintf(stderr,
			"pool %s not yet implemented\n", cmdname);
		return RC_NO_HELP;
	}

	if (ap->c_op != -1 &&
	    (ap->c_op == CONT_LIST_OBJS ||
	     ap->c_op == CONT_QUERY ||
	     ap->c_op == CONT_STAT ||
	     ap->c_op == CONT_GET_PROP ||
	     ap->c_op == CONT_SET_PROP ||
	     ap->c_op == CONT_LIST_ATTRS ||
	     ap->c_op == CONT_DEL_ATTR ||
	     ap->c_op == CONT_GET_ATTR ||
	     ap->c_op == CONT_SET_ATTR ||
	     ap->c_op == CONT_CREATE_SNAP ||
	     ap->c_op == CONT_LIST_SNAPS ||
	     ap->c_op == CONT_DESTROY_SNAP ||
	     ap->c_op == CONT_ROLLBACK)) {
		fprintf(stderr,
			"container %s not yet implemented\n", cmdname);
		return RC_NO_HELP;
	}

	/* Check the pool UUID (required) */
	if (uuid_is_null(ap->pool_uuid)) {
		fprintf(stderr, "pool UUID required\n");
		fflush(stderr);
		return RC_PRINT_HELP;
	}

	/* Check the pool service ranks.(required) */
	if (ap->mdsrv_str == NULL) {
		fprintf(stderr, "--svc must be specified\n");
		fflush(stderr);
		return RC_PRINT_HELP;
	}

	if (ap->mdsrv == NULL) {
		fprintf(stderr, "failed to parse --svc=%s\n", ap->mdsrv_str);
		return RC_NO_HELP;
	}

	if (ap->mdsrv->rl_nr == 0) {
		fprintf(stderr, "--svc must not be empty\n");
		fflush(stderr);
		rc = RC_PRINT_HELP;
		goto bad_opt_free_mdsrv;
	}

	/* TODO: decide if for container operations the code should
	 * check that a container UUID or path has been provided,
	 * and if a path, that container UUID can be looked up (UNS).
	 * Or, if that checking should be in container command handling.
	 */
	return 0;

bad_opt_free_mdsrv:
	daos_rank_list_free(ap->mdsrv);
	return rc;
}

/* For operations that take <pool_uuid, pool_group, pool_svc_ranks>. */
static int
pool_op_hdlr(struct cmd_args_s *ap)
{
	daos_handle_t		pool;
	int			rc = 0;
	int			rc2;
	enum pool_op		op;

	assert(ap != NULL);
	op = ap->p_op;

	/* TODO: create functions per pool op */

	if (op == POOL_QUERY) {
		daos_pool_info_t		 pinfo;
		struct daos_pool_space		*ps = &pinfo.pi_space;
		struct daos_rebuild_status	*rstat = &pinfo.pi_rebuild_st;
		int				 i;

		rc = daos_pool_connect(ap->pool_uuid, ap->group,
				       ap->mdsrv, DAOS_PC_RO, &pool,
				       NULL /* info */, NULL /* ev */);
		if (rc != 0) {
			fprintf(stderr, "failed to connect to pool: %d\n", rc);
			goto bad_pool_connect;
		}

		pinfo.pi_bits = DPI_ALL;
		rc = daos_pool_query(pool, NULL, &pinfo, NULL, NULL);
		if (rc != 0) {
			fprintf(stderr, "pool query failed: %d\n", rc);
			goto bad_pool_query;
		}
		D_PRINT("Pool "DF_UUIDF", ntarget=%u, disabled=%u\n",
			DP_UUID(pinfo.pi_uuid), pinfo.pi_ntargets,
			pinfo.pi_ndisabled);

		D_PRINT("Pool space info:\n");
		D_PRINT("- Target(VOS) count:%d\n", ps->ps_ntargets);
		for (i = DAOS_MEDIA_SCM; i < DAOS_MEDIA_MAX; i++) {
			D_PRINT("- %s:\n",
				i == DAOS_MEDIA_SCM ? "SCM" : "NVMe");
			D_PRINT("  Total size: "DF_U64"\n",
				ps->ps_space.s_total[i]);
			D_PRINT("  Free: "DF_U64", min:"DF_U64", max:"DF_U64", "
				"mean:"DF_U64"\n", ps->ps_space.s_free[i],
				ps->ps_free_min[i], ps->ps_free_max[i],
				ps->ps_free_mean[i]);
		}

		if (rstat->rs_errno == 0) {
			char	*sstr;

			if (rstat->rs_version == 0)
				sstr = "idle";
			else if (rstat->rs_done)
				sstr = "done";
			else
				sstr = "busy";

			D_PRINT("Rebuild %s, "DF_U64" objs, "DF_U64" recs\n",
				sstr, rstat->rs_obj_nr, rstat->rs_rec_nr);
		} else {
			D_PRINT("Rebuild failed, rc=%d, status=%d\n",
				rc, rstat->rs_errno);
		}
	}

	/* TODO implement the following:
	 * op == POOL_LIST_CONTAINERS
	 * op == POOL_STAT
	 * op == POOL_GET_PROP
	 * op == POOL_GET_ATTR
	 * op == POOL_LIST_ATTRS
	 */

bad_pool_query:
	/* Pool disconnect  in normal and error flows: preserve rc */
	if (op == POOL_QUERY) {
		rc2 = daos_pool_disconnect(pool, NULL);
		if (rc2 != 0) {
			fprintf(stderr, "Pool disconnect failed : %d\n", rc2);
		}
		if (rc == 0)
			rc = rc2;
	}
bad_pool_connect:
	return rc;
}

static int
cont_op_hdlr(struct cmd_args_s *ap)
{
	daos_handle_t		pool;
	daos_handle_t		coh;
	daos_cont_info_t	cont_info;
	int			rc;
	int			rc2;
	enum cont_op		op;
	const int		RC_PRINT_HELP = 2;

	assert(ap != NULL);
	op = ap->c_op;

	/* TODO: create functions per container op */

	if (uuid_is_null(ap->cont_uuid)) {
		fprintf(stderr, "valid cont uuid required\n");
		fflush(stderr);
		rc = RC_PRINT_HELP;
		goto bad_opt_free_mdsrv;
	}

	/*
	 * all cont operations require a pool handle, lets make a
	 * pool connection
	 */
	rc = daos_pool_connect(ap->pool_uuid, ap->group, ap->mdsrv,
			       DAOS_PC_RW, &pool,
			       NULL /* info */, NULL /* ev */);
	if (rc != 0) {
		fprintf(stderr, "failed to connect to pool: %d\n", rc);
		goto bad_pool_connect;
	}

	if (op == CONT_CREATE) {
		/* TODO: add use case: create by pool UUID + path (UNS) */
		rc = daos_cont_create(pool, ap->cont_uuid, NULL, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to create container: %d\n", rc);
			goto bad_cont_create;
		}
		fprintf(stdout, "Successfully created container "DF_UUIDF"\n",
			DP_UUID(ap->cont_uuid));
	}

	if (op != CONT_DESTROY) {
		rc = daos_cont_open(pool, ap->cont_uuid, DAOS_COO_RW, &coh,
				    &cont_info, NULL);
		if (rc != 0) {
			fprintf(stderr, "cont open failed: %d\n", rc);
			goto bad_cont_open;
		}
	}


	if (op != CONT_DESTROY) {
		rc = daos_cont_close(coh, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to close container: %d\n", rc);
			goto bad_cont_close;
		}
	}

	if (op == CONT_DESTROY) {
		rc = daos_cont_destroy(pool, ap->cont_uuid, 1, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to destroy container: %d\n",
				rc);
			goto bad_cont_destroy;
		}
		fprintf(stdout, "Successfully destroyed container "DF_UUIDF"\n",
			DP_UUID(ap->cont_uuid));
	}

	/* TODO implement the following:
	 * op == CONT_LIST_OBJS
	 * op == CONT_QUERY
	 * op == CONT_STAT
	 * op == CONT_GET_PROP
	 * op == CONT_SET_PROP
	 * op == CONT_LIST_ATTRS
	 * op == CONT_DEL_ATTR
	 * op == CONT_GET_ATTR
	 * op == CONT_SET_ATTR
	 * op == CONT_CREATE_SNAP
	 * op == CONT_LIST_SNAPS
	 * op == CONT_DESTROY_SNAP
	 * op == CONT_ROLLBACK
	 */

bad_cont_destroy:
bad_cont_close:
bad_cont_open:
bad_cont_create:
	/* Pool disconnect  in normal and error flows: preserve rc */
	rc2 = daos_pool_disconnect(pool, NULL);
	if (rc2 != 0) {
		fprintf(stderr, "Pool disconnect failed : %d\n", rc2);
	}
	if (rc == 0)
		rc = rc2;
bad_pool_connect:
bad_opt_free_mdsrv:
	daos_rank_list_free(ap->mdsrv);
	return rc;
}

static int
help_hdlr(struct cmd_args_s *ap)
{
	FILE *stream;

	assert(ap != NULL);

	stream = (ap->ostream != NULL) ? ap->ostream : stdout;

	fprintf(stream, "\
usage: daos RESOURCE COMMAND [OPTIONS]\n\
resources:\n\
	  pool             pool\n\
	  container (cont) container\n\
	  help             print this message and exit\n");

	fprintf(stream, "\n\
pool commands:\n\
	  list-containers  list all containers in pool\n\
	  list-cont\n\
	  query            query a pool\n\
	  stat             get pool statistics\n\
	  list-attrs       list pool user-defined attributes\n\
	  get-attr         get pool user-defined attribute\n");

	fprintf(stream, "\
pool options:\n\
	--pool=UUID        pool UUID \n\
	--group=STR        pool server process group (\"%s\")\n\
	--svc=RANKS        pool service replicas like 1,2,3\n\
	--attr=NAME        pool attribute name to get\n",
	default_group);

	fprintf(stream, "\n\
container (cont) commands:\n\
	  create           create a container\n\
	  destroy          destroy a container\n\
	  list-objects     list all objects in container\n\
	  list-obj\n\
	  query            query a container\n\
	  stat             get container statistics\n\
	  list-attrs       list container user-defined attributes\n\
	  del-attr         delete container user-defined attribute\n\
	  get-attr         get container user-defined attribute\n\
	  set-attr         set container user-defined attribute\n\
	  create-snap      create container snapshot (optional name)\n\
			   at most recent committed epoch\n\
	  list-snaps       list container snapshots taken\n\
	  destroy-snap     destroy container snapshots\n\
			   by name, epoch or range\n\
	  rollback         roll back container to specified snapshot\n");

	fprintf(stream, "\
container (cont) options:\n\
	  <pool options>   (--pool, --group, --svc)\n\
	--cont=UUID        container UUID\n\
	--attr=NAME        container attribute name to set, get, del\n\
	--value=VALUESTR   container attribute value to set\n\
	--snap=NAME        container snapshot (create/destroy-snap, rollback)\n\
	--epc=EPOCHNUM     container epoch (destroy-snap, rollback)\n\
	--eprange=B-E      container epoch range (destroy-snap)\n");
	return 0;
}

int
main(int argc, char *argv[])
{
	int			rc = 0;
	command_hdlr_t		hdlr = NULL;
	struct cmd_args_s	dargs;

	/* argv[1] is RESOURCE or "help";
	 * argv[2] if provided is a resource-specific command
	 */
	if (argc <= 2 || strcmp(argv[1], "help") == 0)
		hdlr = help_hdlr;
	else if (strcmp(argv[1], "container") == 0)
		hdlr = cont_op_hdlr;
	else if (strcmp(argv[1], "cont") == 0)
		hdlr = cont_op_hdlr;
	else if (strcmp(argv[1], "pool") == 0)
		hdlr = pool_op_hdlr;

	if (hdlr == NULL) {
		dargs.ostream = stderr;
		help_hdlr(&dargs);
		return 2;
	}

	if (hdlr == help_hdlr) {
		dargs.ostream = stdout;
		help_hdlr(&dargs);
		return 0;
	}

	rc = daos_init();
	if (rc != 0) {
		fprintf(stderr, "failed to initialize daos: %d\n", rc);
		return 1;
	}

	/* Parse resource sub-command, and any options into dargs struct */
	rc = common_op_parse_hdlr(argc, argv, &dargs);
	if (rc != 0) {
		fprintf(stderr, "error parsing command line arguments\n");
		if (rc > 0) {
			dargs.ostream = stderr;
			help_hdlr(&dargs);
		}
		daos_fini();
		return -1;
	}

	rc = hdlr(&dargs);

	daos_fini();

	if (rc < 0) {
		return 1;
	} else if (rc > 0) {
		printf("rc: %d\n", rc);
		dargs.ostream = stderr;
		help_hdlr(&dargs);
		return 2;
	}

	return 0;
}
