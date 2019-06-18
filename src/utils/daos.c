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
#include "daos_uns.h"
#include "daos_hdlr.h"

const char		*default_group = DAOS_DEFAULT_GROUP_ID;

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

static void
cmd_args_print(struct cmd_args_s *ap)
{
	char	oclass[10], type[10];

	if (ap == NULL)
		return;

	daos_unparse_oclass(ap->oclass, oclass);
	daos_unparse_ctype(ap->type, type);

	D_INFO("\tgroup=%s\n", ap->group);
	D_INFO("\tpool UUID: "DF_UUIDF"\n", DP_UUID(ap->p_uuid));
	D_INFO("\tcont UUID: "DF_UUIDF"\n", DP_UUID(ap->c_uuid));

	D_INFO("\tpool svc: parsed %u ranks from input %s\n",
		ap->mdsrv ? ap->mdsrv->rl_nr : 0,
		ap->mdsrv_str ? ap->mdsrv_str : "NULL");

	D_INFO("\tattr: name=%s, value=%s\n",
		ap->attrname_str ? ap->attrname_str : "NULL",
		ap->value_str ? ap->value_str : "NULL");
	D_INFO("\tpath=%s, type=%s, oclass=%s, chunk_size="DF_U64"\n",
		ap->path ? ap->path : "NULL",
		type, oclass, ap->chunk_size);
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


static int
epoch_range_parse(struct cmd_args_s *ap)
{
	int		rc;
	long long int	parsed_begin = 0;
	long long int	parsed_end = 0;

	rc = sscanf(ap->epcrange_str, "%lld-%lld",
			&parsed_begin, &parsed_end);
	if ((rc != 2) || (parsed_begin < 0) || (parsed_end < 0))
		D_GOTO(out_invalid_format, -1);

	ap->epcrange_begin = parsed_begin;
	ap->epcrange_end = parsed_end;

	return 0;

out_invalid_format:
	fprintf(stderr, "epcrange=%s must be in A-B form\n",
		ap->epcrange_str);
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
	ap->p_op = -1;
	ap->c_op = -1;
	D_STRNDUP(ap->group, default_group, strlen(default_group));
	if (ap->group == NULL)
		return RC_NO_HELP;

	if ((strcmp(argv[1], "container") == 0) ||
	    (strcmp(argv[1], "cont") == 0)) {
		ap->c_op = cont_op_parse(argv[2]);
		if (ap->c_op == -1) {
			fprintf(stderr, "invalid container command: %s\n",
				argv[2]);
			return RC_PRINT_HELP;
		}
	} else if (strcmp(argv[1], "pool") == 0) {
		ap->p_op = pool_op_parse(argv[2]);
		if (ap->p_op == -1) {
			fprintf(stderr, "invalid pool command: %s\n",
				argv[2]);
			return RC_PRINT_HELP;
		}
	} else {
		/* main() may catch error. Keep this code just in case. */
		fprintf(stderr, "resource (%s): must be "
				 "pool or container\n", argv[1]);
		return RC_PRINT_HELP;
	}
	D_STRNDUP(cmdname, argv[2], strlen(argv[2]));
	if (cmdname == NULL)
		return RC_NO_HELP;

	/* Parse command options. Use goto on any errors here
	 * since some options may result in resource allocation.
	 */
	while ((rc = getopt_long(argc, argv, "", options, NULL)) != -1) {
		switch (rc) {
		case 'G':
			D_STRNDUP(ap->group, optarg, strlen(optarg));
			if (ap->group == NULL)
				D_GOTO(out_free, rc = RC_NO_HELP);
			break;
		case 'p':
			if (uuid_parse(optarg, ap->p_uuid) != 0) {
				fprintf(stderr,
					"failed to parse pool UUID: %s\n",
					optarg);
				D_GOTO(out_free, rc = RC_NO_HELP);
			}
			break;
		case 'c':
			if (uuid_parse(optarg, ap->c_uuid) != 0) {
				fprintf(stderr,
					"failed to parse cont UUID: %s\n",
					optarg);
				D_GOTO(out_free, rc = RC_NO_HELP);
			}
			break;
		case 'm':
			D_STRNDUP(ap->mdsrv_str, optarg, strlen(optarg));
			if (ap->mdsrv_str == NULL)
				D_GOTO(out_free, rc = RC_NO_HELP);
			ap->mdsrv = daos_rank_list_parse(ap->mdsrv_str, ",");
			break;

		case 'a':
			D_STRNDUP(ap->attrname_str, optarg, strlen(optarg));
			if (ap->attrname_str == NULL)
				D_GOTO(out_free, rc = RC_NO_HELP);
			break;
		case 'v':
			D_STRNDUP(ap->value_str, optarg, strlen(optarg));
			if (ap->value_str == NULL)
				D_GOTO(out_free, rc = RC_NO_HELP);
			break;
		case 'd':
			D_STRNDUP(ap->path, optarg, strlen(optarg));
			if (ap->path == NULL)
				D_GOTO(out_free, rc = RC_NO_HELP);
			break;
		case 't':
			daos_parse_ctype(optarg, &ap->type);
			if (ap->type == DAOS_PROP_CO_LAYOUT_UNKOWN) {
				fprintf(stderr, "unknown container type %s\n",
						optarg);
				D_GOTO(out_free, rc = RC_PRINT_HELP);
			}
			break;
		case 'o':
			daos_parse_oclass(optarg, &ap->oclass);
			if (ap->oclass == DAOS_OC_UNKNOWN) {
				fprintf(stderr, "unknown object class: %s\n",
						optarg);
				D_GOTO(out_free, rc = RC_PRINT_HELP);
			}
			break;
		case 'z':
			ap->chunk_size = tobytes(optarg);
			if (ap->chunk_size == 0 ||
			    (ap->chunk_size == ULLONG_MAX && errno != 0)) {
				fprintf(stderr, "failed to parse chunk_size:"
					"%s\n", optarg);
				D_GOTO(out_free, rc = RC_NO_HELP);
			}
			break;
		case 's':
			D_STRNDUP(ap->snapname_str, optarg, strlen(optarg));
			if (ap->snapname_str == NULL)
				D_GOTO(out_free, rc = RC_NO_HELP);
			break;
		case 'e':
			ap->epc = strtoull(optarg, NULL, 10);
			if (ap->epc == 0 ||
			    (ap->epc == ULLONG_MAX && errno != 0)) {
				fprintf(stderr, "failed to parse epc: %s\n",
					optarg);
				D_GOTO(out_free, rc = RC_NO_HELP);
			}
			break;
		case 'r':
			D_STRNDUP(ap->epcrange_str, optarg, strlen(optarg));
			if (ap->epcrange_str == NULL)
				D_GOTO(out_free, rc = RC_NO_HELP);
			rc = epoch_range_parse(ap);
			if (rc != 0) {
				fprintf(stderr, "failed to parse epcrange\n");
				D_GOTO(out_free, rc = RC_NO_HELP);
			}
			break;
		default:
			fprintf(stderr, "unknown option : %d\n", rc);
			D_GOTO(out_free, rc = RC_PRINT_HELP);
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
		D_GOTO(out_free, rc = RC_NO_HELP);
	}

	if (ap->c_op != -1 &&
	    (ap->c_op == CONT_LIST_OBJS ||
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
		D_GOTO(out_free, rc = RC_NO_HELP);
	}

	/* Verify pool svc provided */
	ARGS_VERIFY_MDSRV(ap, out_free, rc = RC_PRINT_HELP);

	return 0;

out_free:
	daos_rank_list_free(ap->mdsrv);
	if (ap->group != NULL)
		D_FREE(ap->group);
	if (ap->mdsrv_str != NULL)
		D_FREE(ap->mdsrv_str);
	if (ap->attrname_str != NULL)
		D_FREE(ap->attrname_str);
	if (ap->value_str != NULL)
		D_FREE(ap->value_str);
	if (ap->path != NULL)
		D_FREE(ap->path);
	if (ap->snapname_str != NULL)
		D_FREE(ap->snapname_str);
	if (ap->epcrange_str != NULL)
		D_FREE(ap->epcrange_str);
	D_FREE(cmdname);
	return rc;
}

/* For operations that take <pool_uuid, pool_group, pool_svc_ranks>
 * invoke op-specific handler function.
 */
static int
pool_op_hdlr(struct cmd_args_s *ap)
{
	int			rc = 0;
	enum pool_op		op;
	const int		RC_PRINT_HELP = 2;

	assert(ap != NULL);
	op = ap->p_op;

	ARGS_VERIFY_PUUID(ap, out, rc = RC_PRINT_HELP);

	switch (op) {
	case POOL_QUERY:
		rc = pool_query_hdlr(ap);
		break;

	/* TODO: implement the following ops */
	case POOL_LIST_CONTAINERS:
		/* rc = pool_list_containers_hdlr() */
		break;
	case POOL_STAT:
		/* rc = pool_stat_hdlr(ap); */
		break;
	case POOL_GET_PROP:
		/* rc = pool_get_prop_hdlr(ap); */
		break;
	case POOL_GET_ATTR:
		/* rc = pool_get_attr_hdlr(ap); */
		break;
	case POOL_LIST_ATTRS:
		/* rc = pool_list_attrs_hdlr(ap); */
		break;
	default:
		break;
	}

out:
	return rc;
}

static int
cont_op_hdlr(struct cmd_args_s *ap)
{
	daos_cont_info_t	cont_info;
	int			rc;
	int			rc2;
	enum cont_op		op;
	const int		RC_PRINT_HELP = 2;

	assert(ap != NULL);
	op = ap->c_op;

	/* Container operations require a UUID. Scenarios:
	 * 1) provided directly by user in --cont= argument.
	 * 2) provided indirectly by user in -path= argument
	 *    (UUID generated [create] or looked up from extended attributes
	 *     associated with path).
	 * 3) neither --cont nor --path specified
	 *    (container UUID will be generated).
	 *
	 * If both UUID and path are provided return an error.
	 * TODO: restrict --path use to ops CREATE, DESTROY, QUERY only?
	 */

	/* All container operations require a pool handle, connect here.
	 * Take specified pool UUID or look up through unified namespace.
	 */
	if ((op != CONT_CREATE) && (ap->path != NULL)) {
		struct duns_attr_t dattr = {0};

		ARGS_VERIFY_PATH_NON_CREATE(ap, out, rc = RC_PRINT_HELP);

		/* Resolve pool, container UUIDs from path if needed */
		rc = duns_resolve_path(ap->path, &dattr);
		if (rc) {
			fprintf(stderr, "could not resolve pool, container "
					"by path: %s\n", ap->path);
			D_GOTO(out, rc);
		}
		ap->type = dattr.da_type;
		uuid_copy(ap->p_uuid, dattr.da_puuid);
		uuid_copy(ap->c_uuid, dattr.da_cuuid);
		ap->oclass = dattr.da_oclass;
		ap->chunk_size = dattr.da_chunk_size;
	} else {
		ARGS_VERIFY_PUUID(ap, out, rc = RC_PRINT_HELP);
	}

	rc = daos_pool_connect(ap->p_uuid, ap->group, ap->mdsrv,
			       DAOS_PC_RW, &ap->pool,
			       NULL /* info */, NULL /* ev */);
	if (rc != 0) {
		fprintf(stderr, "failed to connect to pool: %d\n", rc);
		D_GOTO(out, rc);
	}

	/* container UUID: user-provided, generated here or by uns library */

	/* for container lookup ops: if no path specified, require --cont */
	if ((op != CONT_CREATE) && (ap->path == NULL))
		ARGS_VERIFY_CUUID(ap, out, rc = RC_PRINT_HELP);

	/* container create scenarios (generate UUID if necessary):
	 * 1) both --cont, --path : uns library will use specified c_uuid.
	 * 2) --cont only         : use specified c_uuid.
	 * 3) --path only         : uns library will create & return c_uuid
	 *                          (currently c_uuid null / clear).
	 * 4) neither specified   : create a UUID in c_uuid.
	 */
	if ((op == CONT_CREATE) && (ap->path == NULL) && (uuid_is_null(ap->c_uuid)))
		uuid_generate(ap->c_uuid);

	if (op != CONT_CREATE && op != CONT_DESTROY) {
		rc = daos_cont_open(ap->pool, ap->c_uuid, DAOS_COO_RW,
				    &ap->cont, &cont_info, NULL);
		if (rc != 0) {
			fprintf(stderr, "cont open failed: %d\n", rc);
			D_GOTO(out_disconnect, rc);
		}
	}

	switch (op) {
	case CONT_CREATE:
		if (ap->path != NULL)
			rc = cont_create_uns_hdlr(ap);
		else
			rc = cont_create_hdlr(ap);
		break;
	case CONT_DESTROY:
		rc = cont_destroy_hdlr(ap);
		break;

	/* TODO: implement the following ops */
	case CONT_LIST_OBJS:
		/* rc = cont_list_objs_hdlr(ap); */
		break;
	case CONT_QUERY:
		rc = cont_query_hdlr(ap);
		break;
	case CONT_STAT:
		/* rc = cont_stat_hdlr(ap); */
		break;
	case CONT_GET_PROP:
		/* rc = cont_get_prop_hdlr(ap); */
		break;
	case CONT_SET_PROP:
		/* rc = cont_set_prop_hdlr(ap); */
		break;
	case CONT_LIST_ATTRS:
		/* rc = cont_list_attrs_hdlr(ap); */
		break;
	case CONT_DEL_ATTR:
		/* rc = cont_del_attr_hdlr(ap); */
		break;
	case CONT_GET_ATTR:
		/* rc = cont_get_attr_hdlr(ap); */
		break;
	case CONT_SET_ATTR:
		/* rc = cont_set_attr_hdlr(ap); */
		break;
	case CONT_CREATE_SNAP:
		/* rc = cont_create_snap_hdlr(ap); */
		break;
	case CONT_LIST_SNAPS:
		/* rc = cont_list_snaps_hdlr(ap); */
		break;
	case CONT_DESTROY_SNAP:
		/* rc = cont_destroy_snap_hdlr(ap); */
		break;
	case CONT_ROLLBACK:
		/* rc = cont_rollback_hdlr(ap); */
		break;
	default:
		break;
	}

	/* Container close in normal and error flows: preserve rc */
	if (op != CONT_CREATE && op != CONT_DESTROY) {
		rc2 = daos_cont_close(ap->cont, NULL);
		if (rc2 != 0)
			fprintf(stderr, "Container close faile: %d\n", rc2);
		if (rc == 0)
			rc = rc2;
	}

out_disconnect:
	/* Pool disconnect  in normal and error flows: preserve rc */
	rc2 = daos_pool_disconnect(ap->pool, NULL);
	if (rc2 != 0)
		fprintf(stderr, "Pool disconnect failed : %d\n", rc2);
	if (rc == 0)
		rc = rc2;

out:
	return rc;
}

static int
help_hdlr(struct cmd_args_s *ap)
{
	FILE *stream;

	assert(ap != NULL);

	stream = (ap->ostream != NULL) ? ap->ostream : stdout;

	fprintf(stream,
"usage: daos RESOURCE COMMAND [OPTIONS]\n"
"resources:\n"
"	  pool             pool\n"
"	  container (cont) container\n"
"	  help             print this message and exit\n");

	fprintf(stream, "\n"
"pool commands:\n"
"	  list-containers  list all containers in pool\n"
"	  list-cont\n"
"	  query            query a pool\n"
"	  stat             get pool statistics\n"
"	  list-attrs       list pool user-defined attributes\n"
"	  get-attr         get pool user-defined attribute\n");

	fprintf(stream,
"pool options:\n"
"	--pool=UUID        pool UUID\n"
"	--group=STR        pool server process group (\"%s\")\n"
"	--svc=RANKS        pool service replicas like 1,2,3\n"
"	--attr=NAME        pool attribute name to get\n",
	default_group);

	fprintf(stream, "\n"
"container (cont) commands:\n"
"	  create           create a container\n"
"	  destroy          destroy a container\n"
"	  list-objects     list all objects in container\n"
"	  list-obj\n"
"	  query            query a container\n"
"	  stat             get container statistics\n"
"	  list-attrs       list container user-defined attributes\n"
"	  del-attr         delete container user-defined attribute\n"
"	  get-attr         get container user-defined attribute\n"
"	  set-attr         set container user-defined attribute\n"
"	  create-snap      create container snapshot (optional name)\n"
"			   at most recent committed epoch\n"
"	  list-snaps       list container snapshots taken\n"
"	  destroy-snap     destroy container snapshots\n"
"			   by name, epoch or range\n"
"	  rollback         roll back container to specified snapshot\n");

	fprintf(stream,
"container (cont) options:\n"
"	  <pool options>   (--pool, --group, --svc)\n"
"	--cont=UUID        container UUID\n"
"	--attr=NAME        container attribute name to set, get, del\n"
"	--value=VALUESTR   container attribute value to set\n"
"	--path=PATHSTR     container namespace path\n"
"	--type=CTYPESTR    container type (HDF5, POSIX)\n"
"	--oclass=OCLSSTR   container object class\n"
"			   (tiny, small, large, R2, R2S, repl_max)\n"
"	--chunk_size=BYTES chunk size of files created. Supports suffixes:\n"
"			   K (KB), M (MB), G (GB), T (TB), P (PB), E (EB)\n"
"	--snap=NAME        container snapshot (create/destroy-snap, rollback)\n"
"	--epc=EPOCHNUM     container epoch (destroy-snap, rollback)\n"
"	--eprange=B-E      container epoch range (destroy-snap)\n");
	return 0;
}

int
main(int argc, char *argv[])
{
	int			rc = 0;
	command_hdlr_t		hdlr = NULL;
	struct cmd_args_s	dargs = {0};

	/* argv[1] is RESOURCE or "help";
	 * argv[2] if provided is a resource-specific command
	 */
	if (argc <= 2 || strcmp(argv[1], "help") == 0)
		hdlr = help_hdlr;
	else if ((strcmp(argv[1], "container") == 0) ||
		 (strcmp(argv[1], "cont") == 0))
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

	/* Call resource-specific handler function */
	rc = hdlr(&dargs);

	/* Clean up dargs.mdsrv allocated in common_op_parse_hdlr() */
	daos_rank_list_free(dargs.mdsrv);

	daos_fini();

	if (rc < 0)
		return 1;
	else if (rc > 0) {
		printf("rc: %d\n", rc);
		dargs.ostream = stderr;
		help_hdlr(&dargs);
		return 2;
	}

	return 0;
}
