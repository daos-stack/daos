/**
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * daos(8): DAOS Container and Object Management Utility
 */

#define D_LOGFAC	DD_FAC(client)

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <daos.h>
#include <daos/common.h>
#include <daos/checksum.h>
#include <daos/compression.h>
#include <daos/cipher.h>
#include <daos/rpc.h>
#include <daos/debug.h>
#include <daos/object.h>
#include "daos_types.h"
#include "daos_api.h"
#include "daos_uns.h"
#include "daos_fs.h"
#include "daos_hdlr.h"
#include "dfuse_ioctl.h"

const char		*default_sysname = DAOS_DEFAULT_SYS_NAME;

static enum fs_op
filesystem_op_parse(const char *str)
{
	if (strcmp(str, "copy") == 0)
		return FS_COPY;
	return -1;
}

static enum cont_op
cont_op_parse(const char *str)
{
	if (strcmp(str, "create") == 0)
		return CONT_CREATE;
	else if (strcmp(str, "destroy") == 0)
		return CONT_DESTROY;
	else if (strcmp(str, "clone") == 0)
		return CONT_CLONE;
	else if (strcmp(str, "list-objects") == 0)
		return CONT_LIST_OBJS;
	else if (strcmp(str, "list-obj") == 0)
		return CONT_LIST_OBJS;
	else if (strcmp(str, "query") == 0)
		return CONT_QUERY;
	else if (strcmp(str, "stat") == 0)
		return CONT_STAT;
	else if (strcmp(str, "check") == 0)
		return CONT_CHECK;
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
	else if (strcmp(str, "get-acl") == 0)
		return CONT_GET_ACL;
	else if (strcmp(str, "overwrite-acl") == 0)
		return CONT_OVERWRITE_ACL;
	else if (strcmp(str, "update-acl") == 0)
		return CONT_UPDATE_ACL;
	else if (strcmp(str, "delete-acl") == 0)
		return CONT_DELETE_ACL;
	else if (strcmp(str, "set-owner") == 0)
		return CONT_SET_OWNER;
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
	else if (strcmp(str, "ls") == 0)
		return POOL_LIST_CONTAINERS;
	else if (strcmp(str, "query") == 0)
		return POOL_QUERY;
	else if (strcmp(str, "stat") == 0)
		return POOL_STAT;
	else if (strcmp(str, "get-prop") == 0)
		return POOL_GET_PROP;
	else if (strcmp(str, "get-attr") == 0)
		return POOL_GET_ATTR;
	else if (strcmp(str, "set-attr") == 0)
		return POOL_SET_ATTR;
	else if (strcmp(str, "del-attr") == 0)
		return POOL_DEL_ATTR;
	else if (strcmp(str, "list-attrs") == 0)
		return POOL_LIST_ATTRS;
	else if (strcmp(str, "autotest") == 0)
		return POOL_AUTOTEST;
	return -1;
}

static enum obj_op
obj_op_parse(const char *str)
{
	if (strcmp(str, "query") == 0)
		return OBJ_QUERY;
	else if (strcmp(str, "list-keys") == 0)
		return OBJ_LIST_KEYS;
	else if (strcmp(str, "dump") == 0)
		return OBJ_DUMP;
	return -1;
}

static void
cmd_args_print(struct cmd_args_s *ap)
{
	char	oclass[10] = {}, type[10] = {};

	if (ap == NULL)
		return;

	daos_oclass_id2name(ap->oclass, oclass);
	daos_unparse_ctype(ap->type, type);

	D_INFO("\tDAOS system name: %s\n", ap->sysname);
	D_INFO("\tpool UUID: "DF_UUIDF"\n", DP_UUID(ap->p_uuid));
	D_INFO("\tcont UUID: "DF_UUIDF"\n", DP_UUID(ap->c_uuid));

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
	D_INFO("\toid: "DF_OID"\n", DP_OID(ap->oid));
}

static daos_size_t
tobytes(const char *str)
{
	daos_size_t	 size;
	char		*end;

	if (str == NULL) {
		fprintf(stderr, "passed NULL string\n");
		return 0;
	}

	size = strtoull(str, &end, 0);
	/* Prevent negative numbers from turning into unsigned */
	if (str[0] == '-') {
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

/* oid str: oid_hi.oid_lo */
static int
daos_obj_id_parse(const char *oid_str, daos_obj_id_t *oid)
{
	const char *ptr = oid_str;
	char *end;
	uint64_t hi = 0;
	uint64_t lo = 0;

	/* parse hi
	 * errors if: negative numbers, no digits, exceeds maximum value
	 */
	hi = strtoull(ptr, &end, 10);
	if (ptr[0] == '-')
		return -1;
	if ((hi == 0) && (end == ptr))
		return -1;
	if ((hi == ULLONG_MAX) && (errno == ERANGE))
		return -1;

	/* parse lo after the '.' */
	if (*end != '.')
		return -1;

	ptr = end+1;

	lo = strtoull(ptr, &end, 10);
	if (ptr[0] == '-')
		return -1;
	if ((lo == 0) && (end == ptr))
		return -1;
	if ((lo == ULLONG_MAX) && (errno == ERANGE))
		return -1;

	oid->hi = hi;
	oid->lo = lo;

	return 0;
}

/* supported properties names are "label", "cksum" ("off" or <type> in
 * crc[16,32,64], adler32, sha1, sha256 or sha512), "cksum_size", "srv_cksum"
 * (cksum on server, "on"/"off"), "red_factor" (redundancy factor, rf[0-4]).
 */
static int
daos_parse_property(char *name, char *value, daos_prop_t *props)
{
	/* dpp_nr is used to iterate in props here */
	struct daos_prop_entry *entry = &props->dpp_entries[props->dpp_nr];

	if (!strcmp(name, "label")) {
		size_t len = strnlen(value, DAOS_PROP_LABEL_MAX_LEN);

		if (len == DAOS_PROP_LABEL_MAX_LEN) {
			fprintf(stderr, "label string exceed %u bytes\n",
				DAOS_PROP_LABEL_MAX_LEN);
			return -DER_INVAL;
		}
		entry->dpe_type = DAOS_PROP_CO_LABEL;
		entry->dpe_str = strdup(value);
	} else if (!strcmp(name, "cksum")) {
		int csum_type = daos_str2csumcontprop(value);

		if (csum_type < 0) {
			fprintf(stderr,
				"currently supported checksum types are "
				"'off, crc[16,32,64], adler32, "
				"sha[1,256,512]'\n");
			return -DER_INVAL;
		}
		entry->dpe_type = DAOS_PROP_CO_CSUM;
		entry->dpe_val = csum_type;
	} else if (!strcmp(name, "cksum_size")) {
		char *endp;
		long val;

		/* use base 0 to interpret 0/octal or 0x/hex prefixes
		 * no need to check empty value, this is done in
		 * daos_parse_properties()
		 */
		val = strtoull(value, &endp, 0);
		if (*endp != '\0') {
			fprintf(stderr, "cksum_size value has invalid digits (%s)\n",
				value);
			return -DER_INVAL;
		} else if (val == ULLONG_MAX) {
			fprintf(stderr, "cksum_size value is too big (%s)\n",
				value);
			return -DER_INVAL;
		}

		entry->dpe_type = DAOS_PROP_CO_CSUM_CHUNK_SIZE;
		entry->dpe_val = val;
	} else if (!strcmp(name, "srv_cksum")) {
		if (!strcmp(value, "on"))
			entry->dpe_val = DAOS_PROP_CO_CSUM_SV_ON;
		else if (!strcmp(value, "off"))
			entry->dpe_val = DAOS_PROP_CO_CSUM_SV_OFF;
		else {
			fprintf(stderr, "srv_cksum prop value can only be 'on/off'\n");
			return -DER_INVAL;
		}
		entry->dpe_type = DAOS_PROP_CO_CSUM_SERVER_VERIFY;
	} else if (!strcmp(name, "dedup")) {
		if (!strcmp(value, "off"))
			entry->dpe_val = DAOS_PROP_CO_DEDUP_OFF;
		else if (!strcmp(value, "memcmp"))
			entry->dpe_val = DAOS_PROP_CO_DEDUP_MEMCMP;
		else if (!strcmp(value, "hash"))
			entry->dpe_val = DAOS_PROP_CO_DEDUP_HASH;
		else {
			fprintf(stderr, "dedup prop value can only be 'off/memcmp/hash'\n");
			return -DER_INVAL;
		}
		entry->dpe_type = DAOS_PROP_CO_DEDUP;
	} else if (!strcmp(name, "dedup_th") ||
		   !strcmp(name, "dedup_threshold")) {
		char *endp;
		long val;

		/* use base 0 to interpret 0/octal or 0x/hex prefixes
		 * no need to check empty value, this is done in
		 * daos_parse_properties()
		 */
		val = strtoull(value, &endp, 0);
		if (*endp != '\0') {
			fprintf(stderr, "dedup_th value has invalid digits (%s)\n",
				value);
			return -DER_INVAL;
		} else if (val == ULLONG_MAX) {
			fprintf(stderr, "dedup_th value is too big (%s)\n",
				value);
			return -DER_INVAL;
		} else if (val < 4096) {
			fprintf(stderr, "dedup_th value is too small (%s)\n",
				value);
			return -DER_INVAL;
		}
		entry->dpe_type = DAOS_PROP_CO_DEDUP_THRESHOLD;
		entry->dpe_val = val;
	} else if (!strcmp(name, "compress") || !strcmp(name, "compression")) {
		int compression_type = daos_str2compresscontprop(value);

		if (compression_type < 0) {
			fprintf(stderr, "compression prop value can only be "
				"'off/lz4/gzip/gzip[1-9]'\n");
			return -DER_INVAL;
		}
		entry->dpe_type = DAOS_PROP_CO_COMPRESS;
		entry->dpe_val = compression_type;
	} else if (!strcmp(name, "encrypt") ||
		   !strcmp(name, "encryption")) {
		int encryption_type = daos_str2encryptcontprop(value);

		if (encryption_type < 0) {
			fprintf(stderr, "encryption prop value can only be "
				"'off/aes-xts[128,256]/aes-cbc[128,192,256]/"
				"aes-gcm[128,256]'\n");
			return -DER_INVAL;
		}
		entry->dpe_type = DAOS_PROP_CO_ENCRYPT;
		entry->dpe_val = encryption_type;
	} else if (!strcmp(name, "rf")) {
		if (!strcmp(value, "0"))
			entry->dpe_val = DAOS_PROP_CO_REDUN_RF0;
		else if (!strcmp(value, "1"))
			entry->dpe_val = DAOS_PROP_CO_REDUN_RF1;
		else if (!strcmp(value, "2"))
			entry->dpe_val = DAOS_PROP_CO_REDUN_RF2;
		else if (!strcmp(value, "3"))
			entry->dpe_val = DAOS_PROP_CO_REDUN_RF3;
		else if (!strcmp(value, "4"))
			entry->dpe_val = DAOS_PROP_CO_REDUN_RF4;
		else {
			fprintf(stderr, "presently supported redundancy factors (rf) are [0-4]'\n");
			return -DER_INVAL;
		}
		entry->dpe_type = DAOS_PROP_CO_REDUN_FAC;
	} else if (!strcmp(name, "status")) {
		if (!strcmp(value, "healthy")) {
			entry->dpe_val =
				DAOS_PROP_CO_STATUS_VAL(DAOS_PROP_CO_HEALTHY,
							0);
		} else {
			fprintf(stderr, "status prop value can only be "
				"'healthy' to clear UNCLEAN status\n");
			return -DER_INVAL;
		}
		entry->dpe_type = DAOS_PROP_CO_STATUS;
	} else {
		fprintf(stderr, "supported prop names are label/cksum/cksum_size/srv_cksum/dedup/dedup_th/rf\n");
		return -DER_INVAL;
	}

	props->dpp_nr++;
	return 0;
}

/* format for list of properties is "<name>:<value>[,<name>:<value>,...]" */
static int
daos_parse_properties(char *props_string, daos_prop_t *props)
{
	char name[20], value[DAOS_PROP_LABEL_MAX_LEN] /* for label */;
	char *cur = props_string, *comma, *colon;
	size_t len = strlen(props_string);
	int rc = 0;

	while (len > 0) {
		colon = strchr(cur, ':');
		if (colon == NULL) {
			fprintf(stderr, "wrong format for properties\n");
			rc = -DER_INVAL;
			break;
		}
		*colon = '\0';
		if (strlen(cur) >= sizeof(name)) {
			fprintf(stderr, "too long prop name '%s'\n",
				cur);
			*colon = ':';
			rc = -DER_INVAL;
			break;
		}
		strcpy(name, cur);
		*colon = ':';
		comma = strchr(colon + 1, ',');
		if (comma == NULL) {
			if (strlen(colon + 1) < sizeof(value)) {
				strcpy(value, colon + 1);
				/* last property in list */
				/* break; */
				len -= strlen(cur);
			} else {
				fprintf(stderr, "too long prop value '%s'\n",
					colon + 1);
				rc = -DER_INVAL;
				break;
			}
		} else {
			*comma = '\0';
			if (sizeof(value) > strlen(colon + 1)) {
				strcpy(value, colon + 1);
				len = len - (comma - cur + 1);
				cur = comma + 1;
				*comma = ',';
				/* continue; */
			} else {
				fprintf(stderr, "too long prop value '%s'\n",
					colon + 1);
				*comma = ',';
				rc = -DER_INVAL;
				break;
			}
		}
		rc = daos_parse_property(name, value, props);
		if (rc)
			break;
	}

	return rc;
}

/* values to identify options with no small value in getopt_long() */
enum {
	DAOS_PROPERTIES_OPTION = 1,
};

/* resource and command arguments (ie "container create" for example) can be
 * skipped for options evaluation
 */
#define SKIP_RES_AND_CMD_ARGS 2

static int
common_op_parse_hdlr(int argc, char *argv[], struct cmd_args_s *ap)
{
	/* Note: will rely on getopt_long() substring matching for shorter
	 * option variants. Specifically --sys= for --sys-name=
	 */
	struct option		options[] = {
		{"sys-name",	required_argument,	NULL,	'G'},
		{"pool",	required_argument,	NULL,	'p'},
		{"cont",	required_argument,	NULL,	'c'},
		{"attr",	required_argument,	NULL,	'a'},
		{"value",	required_argument,	NULL,	'v'},
		{"path",	required_argument,	NULL,	'd'},
		{"src",		required_argument,	NULL,	'S'},
		{"dst",		required_argument,	NULL,	'D'},
		{"type",	required_argument,	NULL,	't'},
		{"oclass",	required_argument,	NULL,	'o'},
		{"chunk_size",	required_argument,	NULL,	'z'},
		{"snap",	required_argument,	NULL,	's'},
		{"epc",		required_argument,	NULL,	'e'},
		{"epcrange",	required_argument,	NULL,	'r'},
		{"oid",		required_argument,	NULL,	'i'},
		{"force",	no_argument,		NULL,	'f'},
		{"properties",	required_argument,	NULL,	DAOS_PROPERTIES_OPTION},
		{"outfile",	required_argument,	NULL,	'O'},
		{"verbose",	no_argument,		NULL,	'V'},
		{"acl-file",	required_argument,	NULL,	'A'},
		{"entry",	required_argument,	NULL,	'E'},
		{"user",	required_argument,	NULL,	'u'},
		{"group",	required_argument,	NULL,	'g'},
		{"principal",	required_argument,	NULL,	'P'},
		{NULL,		0,			NULL,	0}
	};
	int			rc;
	const int		RC_PRINT_HELP = 2;
	const int		RC_NO_HELP = -2;
	char			*cmdname = NULL;

	assert(ap != NULL);
	ap->p_op  = -1;
	ap->c_op  = -1;
	ap->o_op  = -1;
	ap->fs_op = -1;
	D_STRNDUP(ap->sysname, default_sysname, strlen(default_sysname));
	if (ap->sysname == NULL)
		return RC_NO_HELP;

	if ((strcmp(argv[1], "container") == 0) ||
	    (strcmp(argv[1], "cont") == 0)) {
		ap->c_op = cont_op_parse(argv[2]);
		if (ap->c_op == -1) {
			fprintf(stderr, "invalid container command: %s\n",
				argv[2]);
			return RC_PRINT_HELP;
		}
	} else if ((strcmp(argv[1], "filesystem") == 0) ||
	    (strcmp(argv[1], "fs") == 0)) {
		ap->fs_op = filesystem_op_parse(argv[2]);
		if (ap->fs_op == -1) {
			fprintf(stderr, "invalid filesystem command: %s\n",
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
	} else if ((strcmp(argv[1], "object") == 0) ||
		   (strcmp(argv[1], "obj") == 0)) {
		ap->o_op = obj_op_parse(argv[2]);
		if (ap->o_op == -1) {
			fprintf(stderr, "invalid object command: %s\n",
				argv[2]);
			return RC_PRINT_HELP;
		}
	} else {
		/* main() may catch error. Keep this code just in case. */
		fprintf(stderr, "resource (%s): must be "
				 "pool, container or object\n", argv[1]);
		return RC_PRINT_HELP;
	}
	D_STRNDUP(cmdname, argv[2], strlen(argv[2]));
	if (cmdname == NULL)
		D_GOTO(out_free, rc = RC_NO_HELP);

	/* Parse remaining command-line options (skip resource and command).
	 * Use goto on any errors here since some options may result in
	 * resource allocation.
	 */
	while ((rc = getopt_long(argc - SKIP_RES_AND_CMD_ARGS,
				 &argv[SKIP_RES_AND_CMD_ARGS], "", options,
				 NULL)) != -1) {
		switch (rc) {
		case 'G':
			D_FREE(ap->sysname);
			D_STRNDUP(ap->sysname, optarg, strlen(optarg));
			if (ap->sysname == NULL)
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
		case 'a':
			if (ap->attrname_str != NULL) {
				fprintf(stderr,
					"only one attribute name is allowed\n");
				D_GOTO(out_free, rc = RC_NO_HELP);
			}
			D_STRNDUP(ap->attrname_str, optarg, strlen(optarg));
			if (ap->attrname_str == NULL)
				D_GOTO(out_free, rc = RC_NO_HELP);
			break;
		case 'v':
			if (ap->value_str != NULL) {
				fprintf(stderr,
					"only one attribute value is allowed\n");
				D_GOTO(out_free, rc = RC_NO_HELP);
			}
			D_STRNDUP(ap->value_str, optarg, strlen(optarg));
			if (ap->value_str == NULL)
				D_GOTO(out_free, rc = RC_NO_HELP);
			break;
		case 'd':
			D_STRNDUP(ap->path, optarg, strlen(optarg));
			if (ap->path == NULL)
				D_GOTO(out_free, rc = RC_NO_HELP);
			break;
		case 'S':
			D_STRNDUP(ap->src, optarg, strlen(optarg));
			if (ap->src == NULL)
				D_GOTO(out_free, rc = RC_NO_HELP);
			break;
		case 'D':
			D_STRNDUP(ap->dst, optarg, strlen(optarg));
			if (ap->dst == NULL)
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
			ap->oclass = daos_oclass_name2id(optarg);
			if (ap->oclass == OC_UNKNOWN) {
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
		case 'i':
			rc = daos_obj_id_parse(optarg, &ap->oid);
			if (rc != 0) {
				fprintf(stderr, "oid format should be "
						"oid_hi.oid_lo\n");
				D_GOTO(out_free, rc = RC_NO_HELP);
			}
			break;
		case 'f':
			ap->force = 1;
			break;
		case 'O':
			D_STRNDUP(ap->outfile, optarg, strlen(optarg));
			if (ap->outfile == NULL)
				D_GOTO(out_free, rc = RC_NO_HELP);
			break;
		case 'V':
			ap->verbose = true;
			break;
		case 'A':
			D_STRNDUP(ap->aclfile, optarg, strlen(optarg));
			if (ap->aclfile == NULL)
				D_GOTO(out_free, rc = RC_NO_HELP);
			break;
		case 'E':
			D_STRNDUP(ap->entry, optarg, strlen(optarg));
			if (ap->entry == NULL)
				D_GOTO(out_free, rc = RC_NO_HELP);
			break;
		case 'u':
			D_STRNDUP(ap->user, optarg, strlen(optarg));
			if (ap->user == NULL)
				D_GOTO(out_free, rc = RC_NO_HELP);
			break;
		case 'g':
			D_STRNDUP(ap->group, optarg, strlen(optarg));
			if (ap->group == NULL)
				D_GOTO(out_free, rc = RC_NO_HELP);
			break;
		case 'P':
			D_STRNDUP(ap->principal, optarg, strlen(optarg));
			if (ap->principal == NULL)
				D_GOTO(out_free, rc = RC_NO_HELP);
			break;
		case DAOS_PROPERTIES_OPTION:
			/* parse properties to be set at cont create time */
			/* alloc max */
			ap->props = daos_prop_alloc(DAOS_PROP_ENTRIES_MAX_NR);
			if (ap->props == NULL) {
				fprintf(stderr, "unable to allocate props struct and array\n");
				D_GOTO(out_free, rc = RC_NO_HELP);
			}
			/* fake number of entries in array to be used for
			 * current entry to be filled
			 */
			ap->props->dpp_nr = 0;
			rc = daos_parse_properties(optarg, ap->props);
			if (rc != 0)
				D_GOTO(out_free, rc = RC_NO_HELP);
			break;
		default:
			fprintf(stderr, "unknown option : %d\n", rc);
			D_GOTO(out_free, rc = RC_PRINT_HELP);
		}
	}

	/* unexpected extra arguments not allowed */
	if (optind < argc - SKIP_RES_AND_CMD_ARGS) {
		fprintf(stderr, "unexpected extra argument : '%s'\n",
			argv[optind + SKIP_RES_AND_CMD_ARGS]);
		D_GOTO(out_free, rc = RC_NO_HELP);
	}

	cmd_args_print(ap);

	/* Check for any unimplemented commands, print help */
	if (ap->p_op != -1 &&
	    (ap->p_op == POOL_STAT)) {
		fprintf(stderr,
			"pool %s not yet implemented\n", cmdname);
		D_GOTO(out_free, rc = RC_NO_HELP);
	}

	if (ap->c_op != -1 && ap->c_op == CONT_STAT) {
		fprintf(stderr,
			"container %s not yet implemented\n", cmdname);
		D_GOTO(out_free, rc = RC_NO_HELP);
	}

	if (ap->o_op != -1 &&
	    ((ap->o_op == OBJ_LIST_KEYS) ||
	     (ap->o_op == OBJ_DUMP))) {
		fprintf(stderr,
			"object %s not yet implemented\n", cmdname);
		D_GOTO(out_free, rc = RC_NO_HELP);
	}

	D_FREE(cmdname);
	return 0;

out_free:
	if (ap->sysname != NULL)
		D_FREE(ap->sysname);
	if (ap->attrname_str != NULL)
		D_FREE(ap->attrname_str);
	if (ap->value_str != NULL)
		D_FREE(ap->value_str);
	if (ap->path != NULL)
		D_FREE(ap->path);
	if (ap->src != NULL)
		D_FREE(ap->src);
	if (ap->dst != NULL)
		D_FREE(ap->dst);
	if (ap->snapname_str != NULL)
		D_FREE(ap->snapname_str);
	if (ap->epcrange_str != NULL)
		D_FREE(ap->epcrange_str);
	if (ap->props) {
		/* restore number of entries in array for freeing */
		ap->props->dpp_nr = DAOS_PROP_ENTRIES_MAX_NR;
		daos_prop_free(ap->props);
	}
	if (ap->outfile != NULL)
		D_FREE(ap->outfile);
	if (ap->aclfile != NULL)
		D_FREE(ap->aclfile);
	if (ap->entry != NULL)
		D_FREE(ap->entry);
	if (ap->principal != NULL)
		D_FREE(ap->principal);
	D_FREE(cmdname);
	return rc;
}

/* For operations that take <pool_uuid, pool_sysname, pool_svc_ranks>
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
	case POOL_LIST_CONTAINERS:
		rc = pool_list_containers_hdlr(ap);
		break;

	/* TODO: implement when statistics available */
	case POOL_STAT:
		/* rc = pool_stat_hdlr(ap); */
		break;
	case POOL_GET_PROP:
		rc = pool_get_prop_hdlr(ap);
		break;
	case POOL_GET_ATTR:
		rc = pool_get_attr_hdlr(ap);
		break;
	case POOL_SET_ATTR:
		rc = pool_set_attr_hdlr(ap);
		break;
	case POOL_LIST_ATTRS:
		rc = pool_list_attrs_hdlr(ap);
		break;
	case POOL_DEL_ATTR:
		rc = pool_del_attr_hdlr(ap);
		break;
	case POOL_AUTOTEST:
		rc = pool_autotest_hdlr(ap);
		break;
	default:
		break;
	}

out:
	return rc;
}

static int
call_dfuse_ioctl(char *path, struct dfuse_il_reply *reply)
{
	int fd;
	int rc;

	fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
	if (fd < 0)
		return ENOENT;

	errno = 0;
	rc = ioctl(fd, DFUSE_IOCTL_IL, reply);
	if (rc != 0) {
		int err = errno;

		close(fd);
		return err;
	}
	close(fd);

	if (reply->fir_version != DFUSE_IOCTL_VERSION)
		return EIO;

	return 0;
}

static int
fs_op_hdlr(struct cmd_args_s *ap)
{
	int rc = 0;
	enum fs_op op;

	assert(ap != NULL);
	op = ap->fs_op;

	switch (op) {
	case FS_COPY:
		if (ap->src == NULL || ap->dst == NULL) {
			fprintf(stderr, "a source and destination path "
				"must be provided\n");
		} else {
			rc = fs_copy_hdlr(ap);
		}
		break;
	default:
		break;
	}
	return rc;
}

static int
cont_op_hdlr(struct cmd_args_s *ap)
{
	daos_cont_info_t	cont_info;
	int			rc = 0;
	int			rc2 = 0;
	enum cont_op		op;
	const int		RC_PRINT_HELP = 2;

	assert(ap != NULL);
	op = ap->c_op;

	/* cont_clone uses its own src and dst variables, and does
	 * not use the regular pool and cont ones stored in the
	 * ap struct. This is because for a clone it is necessary
	 * to explicitly specify whether a container is a src or
	 * dst
	 */
	if (op == CONT_CLONE) {
		if (ap->src != NULL && ap->dst != NULL) {
			rc = cont_clone_hdlr(ap);
		} else {
			rc = RC_PRINT_HELP;
		}
		return rc;
	}

	/* All container operations require a pool handle, connect
	 * here. Take specified pool UUID or look up through unified
	 * namespace.
	 */
	if ((op != CONT_CREATE) && (ap->path != NULL)) {
		struct duns_attr_t dattr = {0};
		struct dfuse_il_reply il_reply = {0};

		ARGS_VERIFY_PATH_NON_CREATE(ap, out,
					    rc = RC_PRINT_HELP);

		/* Resolve pool, container UUIDs from path if needed
		 * Firtly check for a unified namespace entry point,
		 * then if that isn't detected then check for dfuse
		 * backing the path, and print pool/container/oid
		 * for the path.
		 */
		rc = duns_resolve_path(ap->path, &dattr);
		if (rc) {
			rc = call_dfuse_ioctl(ap->path, &il_reply);
			if (rc != 0) {
				fprintf(stderr, "could not resolve "
					"pool, container by "
					"path: %d %s %s\n",
					rc, strerror(rc), ap->path);
				D_GOTO(out, rc);
			}

			ap->type = DAOS_PROP_CO_LAYOUT_POSIX;
			uuid_copy(ap->p_uuid, il_reply.fir_pool);
			uuid_copy(ap->c_uuid, il_reply.fir_cont);
			ap->oid = il_reply.fir_oid;
		} else {
			ap->type = dattr.da_type;
			uuid_copy(ap->p_uuid, dattr.da_puuid);
			uuid_copy(ap->c_uuid, dattr.da_cuuid);
		}
	} else {
		ARGS_VERIFY_PUUID(ap, out, rc = RC_PRINT_HELP);
	}

	rc = daos_pool_connect(ap->p_uuid, ap->sysname, DAOS_PC_RW,
			       &ap->pool, NULL /* info */,
			       NULL /* ev */);
	if (rc != 0) {
		fprintf(stderr, "failed to connect to "
			"pool "DF_UUIDF ": %s (%d)\n",
			DP_UUID(ap->p_uuid), d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

	/* container UUID: user-provided, generated here or by uns
	 * library
	 */

	/* for container lookup ops: if no path specified,
	 * require --cont
	 */
	if ((op != CONT_CREATE) && (ap->path == NULL))
		ARGS_VERIFY_CUUID(ap, out, rc = RC_PRINT_HELP);

	/* container create scenarios (generate UUID if necessary):
	 * 1) both --cont, --path : uns library will use specified c_uuid.
	 * 2) --cont only	  : use specified c_uuid.
	 * 3) --path only	  : uns library will create & return c_uuid
	 *			    (currently c_uuid null / clear).
	 * 4) neither specified   : create a UUID in c_uuid.
	 */
	if ((op == CONT_CREATE) && (ap->path == NULL) &&
	    (uuid_is_null(ap->c_uuid)))
		uuid_generate(ap->c_uuid);

	if (op != CONT_CREATE && op != CONT_DESTROY) {
		rc = daos_cont_open(ap->pool, ap->c_uuid,
				    DAOS_COO_RW | DAOS_COO_FORCE,
				    &ap->cont, &cont_info, NULL);
		if (rc != 0) {
			fprintf(stderr, "failed to open "
				"container "DF_UUIDF
				": %s (%d)\n", DP_UUID(ap->c_uuid),
				d_errdesc(rc), rc);
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
	case CONT_LIST_OBJS:
		rc = cont_list_objs_hdlr(ap);
		break;
	case CONT_QUERY:
		rc = cont_query_hdlr(ap);
		break;
	case CONT_STAT:
		/* rc = cont_stat_hdlr(ap); */
		break;
	case CONT_CHECK:
		rc = cont_check_hdlr(ap);
		break;
	case CONT_GET_PROP:
		rc = cont_get_prop_hdlr(ap);
		break;
	case CONT_SET_PROP:
		rc = cont_set_prop_hdlr(ap);
		break;
	case CONT_LIST_ATTRS:
		rc = cont_list_attrs_hdlr(ap);
		break;
	case CONT_DEL_ATTR:
		rc = cont_del_attr_hdlr(ap);
		break;
	case CONT_GET_ATTR:
		rc = cont_get_attr_hdlr(ap);
		break;
	case CONT_SET_ATTR:
		rc = cont_set_attr_hdlr(ap);
		break;
	case CONT_CREATE_SNAP:
		rc = cont_create_snap_hdlr(ap);
		break;
	case CONT_LIST_SNAPS:
		rc = cont_list_snaps_hdlr(ap, NULL, NULL);
		break;
	case CONT_DESTROY_SNAP:
		rc = cont_destroy_snap_hdlr(ap);
		break;
	case CONT_ROLLBACK:
		rc = cont_rollback_hdlr(ap);
		break;
	case CONT_GET_ACL:
		rc = cont_get_acl_hdlr(ap);
		break;
	case CONT_OVERWRITE_ACL:
		rc = cont_overwrite_acl_hdlr(ap);
		break;
	case CONT_UPDATE_ACL:
		rc = cont_update_acl_hdlr(ap);
		break;
	case CONT_DELETE_ACL:
		rc = cont_delete_acl_hdlr(ap);
		break;
	case CONT_SET_OWNER:
		rc = cont_set_owner_hdlr(ap);
		break;
	default:
		break;
	}

	/* Container close in normal and error flows: preserve rc */
	if (op != CONT_CREATE && op != CONT_DESTROY) {
		rc2 = daos_cont_close(ap->cont, NULL);
		if (rc2 != 0)
			fprintf(stderr, "failed to close container "DF_UUIDF
				": %s (%d)\n", DP_UUID(ap->c_uuid),
				d_errdesc(rc2), rc2);
		if (rc == 0)
			rc = rc2;
	}
out_disconnect:
	/* Pool disconnect in normal and error flows: preserve rc */
	rc2 = daos_pool_disconnect(ap->pool, NULL);
	if (rc2 != 0)
		fprintf(stderr, "failed to disconnect from pool "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->p_uuid), d_errdesc(rc2),
			rc2);
	if (rc == 0)
		rc = rc2;

out:
	return rc;
}

/* For operations that take <oid>
 * invoke op-specific handler function.
 */
static int
obj_op_hdlr(struct cmd_args_s *ap)
{
	daos_cont_info_t	cont_info;
	int			rc;
	int			rc2;
	enum obj_op		op;
	const int		RC_PRINT_HELP = 2;

	assert(ap != NULL);
	op = ap->o_op;

	rc = 0;
	ARGS_VERIFY_PUUID(ap, out, rc = RC_PRINT_HELP);
	ARGS_VERIFY_CUUID(ap, out, rc = RC_PRINT_HELP);
	ARGS_VERIFY_OID(ap, out, rc = RC_PRINT_HELP);

	/* TODO: support container lookup by path? */

	rc = daos_pool_connect(ap->p_uuid, ap->sysname,
			       DAOS_PC_RW, &ap->pool,
			       NULL /* info */, NULL /* ev */);
	if (rc != 0) {
		fprintf(stderr, "failed to connect to pool "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->p_uuid), d_errdesc(rc), rc);
		D_GOTO(out, rc);
	}

	rc = daos_cont_open(ap->pool, ap->c_uuid, DAOS_COO_RW,
			&ap->cont, &cont_info, NULL);
	if (rc != 0) {
		fprintf(stderr, "failed to open container "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->c_uuid), d_errdesc(rc), rc);
		D_GOTO(out_disconnect, rc);
	}

	switch (op) {
	case OBJ_QUERY:
		rc = obj_query_hdlr(ap);
		break;
	case OBJ_DUMP:
		break;
	default:
		break;
	}

	/* Container close in normal and error flows: preserve rc */
	rc2 = daos_cont_close(ap->cont, NULL);
	if (rc2 != 0)
		fprintf(stderr, "failed to close container "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->c_uuid), d_errdesc(rc2),
			rc2);
	if (rc == 0)
		rc = rc2;

out_disconnect:
	/* Pool disconnect in normal and error flows: preserve rc */
	rc2 = daos_pool_disconnect(ap->pool, NULL);
	if (rc2 != 0)
		fprintf(stderr, "failed to disconnect from pool "DF_UUIDF
			": %s (%d)\n", DP_UUID(ap->p_uuid), d_errdesc(rc2),
			rc2);
	if (rc == 0)
		rc = rc2;

out:
	return rc;
}

#define OCLASS_NAMES_LIST_SIZE 512

static void
print_oclass_names_list(FILE *stream)
{
	char *str;
	size_t size = OCLASS_NAMES_LIST_SIZE, len;

again:
	str = malloc(size);
	if (str == NULL) {
		fprintf(stderr, "failed to malloc %zu bytes to gather oclass names list\n",
			size);
		return;
	}
	len = daos_oclass_names_list(size, str);
	if (len <= 0)
		goto out;
	if (len < size)
		fprintf(stream, "%s", str);
	else {
		size = len + 1;
		free(str);
		goto again;
	}
out:
	free(str);
}

#define FIRST_LEVEL_HELP() \
do { \
	fprintf(stream, \
	"usage: daos RESOURCE COMMAND [OPTIONS]\n" \
	"resources:\n" \
	"	  pool             pool\n" \
	"	  container (cont) container\n" \
	"	  filesystem (fs)  copy to and from a POSIX filesystem\n" \
	"	  object (obj)     object\n" \
	"	  version          print command version\n" \
	"	  help             print this message and exit\n"); \
	fprintf(stream, "\n"); \
	fprintf(stream, "use 'daos help RESOURCE' for resource specifics\n"); \
} while (0)

#define FS_COPY_CMDS_HELP() \
do { \
	fprintf(stream, "\n" \
	" copy to and from POSIX filesystem\n" \
	" filesystem copy options (copy):\n" \
	"	--src=daos://<pool/cont> | <path>\n" \
	"	--dst=daos://<pool/cont> | <path>\n" \
	"	\t type is daos, only specified if pool/cont used\n"); \
	fprintf(stream, "\n"); \
} while (0)

#define ALL_FS_CMDS_HELP() \
do { \
	fprintf(stream, "\n" \
	"container (cont) commands:\n" \
	"	  copy		copy to/from POSIX filesystem\n"); \
	fprintf(stream, "\n"); \
	fprintf(stream, "use 'daos help fs|filesystem COMMAND' " \
	"		for options\n"); \
} while (0)

#define ALL_CONT_CMDS_HELP() \
do { \
	fprintf(stream, "\n" \
	"container (cont) commands:\n" \
	"	  create           create a container\n" \
	"	  clone            clone a container\n" \
	"	  destroy          destroy a container\n" \
	"	  list-objects     list all objects in container\n" \
	"	  list-obj\n" \
	"	  query            query a container\n" \
	"	  get-prop         get all container's properties\n" \
	"	  set-prop         set container's properties\n" \
	"	  get-acl          get a container's ACL\n" \
	"	  overwrite-acl    replace a container's ACL\n" \
	"	  update-acl       add/modify entries in a container's ACL\n" \
	"	  delete-acl       delete an entry from a container's ACL\n" \
	"	  set-owner        change the user and/or group that own a container\n" \
	"	  stat             get container statistics\n" \
	"	  check            check objects consistency in container\n" \
	"	  list-attrs       list container user-defined attributes\n" \
	"	  del-attr         delete container user-defined attribute\n" \
	"	  get-attr         get container user-defined attribute\n" \
	"	  set-attr         set container user-defined attribute\n" \
	"	  create-snap      create container snapshot (optional name)\n" \
	"			   at most recent committed epoch\n" \
	"	  list-snaps       list container snapshots taken\n" \
	"	  destroy-snap     destroy container snapshots\n" \
	"			   by name, epoch or range\n" \
	"	  rollback         roll back container to specified snapshot\n"); \
	fprintf(stream, "\n"); \
	fprintf(stream, "use 'daos help cont|container COMMAND' for command specific options\n"); \
} while (0)

#define ALL_BUT_CONT_CREATE_OPTS_HELP() \
do { \
	fprintf(stream, \
	"container options (query, and all commands except create):\n" \
	"	  <pool options>   with --cont use: (--pool, --sys-name)\n" \
	"	  <pool options>   with --path use: (--sys-name)\n" \
	"	--cont=UUID        (mandatory, or use --path)\n" \
	"	--path=PATHSTR     (mandatory, or use --cont)\n"); \
} while (0)

static int
help_hdlr(int argc, char *argv[], struct cmd_args_s *ap)
{
	FILE *stream;

	assert(ap != NULL);

	stream = (ap->ostream != NULL) ? ap->ostream : stdout;

	fprintf(stream, "daos command (v%s), libdaos %d.%d.%d\n",
		DAOS_VERSION, DAOS_API_VERSION_MAJOR,
		DAOS_API_VERSION_MINOR, DAOS_API_VERSION_FIX);


	if (argc <= 2) {
		FIRST_LEVEL_HELP();
	} else if (strcmp(argv[2], "filesystem") == 0 ||
			strcmp(argv[2], "fs") == 0) {
		if (argc == 3 || strcmp(argv[3], "copy") == 0) {
			FS_COPY_CMDS_HELP();
		}
	} else if (strcmp(argv[2], "pool") == 0) {
		fprintf(stream, "\n"
		"pool commands:\n"
		"	  list-containers  list all containers in pool\n"
		"	  list-cont\n"
		"	  ls\n"
		"	  query            query a pool\n"
		"	  stat             get pool statistics\n"
		"	  list-attrs       list pool user-defined attributes\n"
		"	  get-attr         get pool user-defined attribute\n"
		"	  set-attr         set pool user-defined attribute\n"
		"	  del-attr         del pool user-defined attribute\n"
		"	  autotest         verify setup with smoke tests\n");

		fprintf(stream,
		"pool options:\n"
		"	--pool=UUID        pool UUID\n"
		"	--sys-name=STR     DAOS system name context for servers (\"%s\")\n"
		"	--sys=STR\n"
		"	--attr=NAME        pool attribute name to get, set, del\n"
		"	--value=VALUESTR   pool attribute name to set\n",
			default_sysname);
	} else if (strcmp(argv[2], "container") == 0 ||
		   strcmp(argv[2], "cont") == 0) {
		if (argc == 3) {
			ALL_CONT_CMDS_HELP();
		} else if (strcmp(argv[3], "create") == 0) {
			fprintf(stream,
			"container options (create by UUID):\n"
			"	  <pool options>   (--pool, --sys-name)\n"
			"	--cont=UUID        (optional) container UUID (or generated)\n"
			"container options (create and link to namespace path):\n"
			"	  <pool/cont opts> (--pool, --sys-name, --cont [optional])\n"
			"	--path=PATHSTR     container namespace path\n"
			"container create common optional options:\n"
			"	--type=CTYPESTR    container type (HDF5, POSIX)\n"
			"	--oclass=OCLSSTR   container object class\n"
			"			   (");
			/* vs hardcoded list like "tiny, small, large, R2, R2S, repl_max" */
			print_oclass_names_list(stream);
			fprintf(stream, ")\n"
			"	--chunk_size=BYTES chunk size of files created. Supports suffixes:\n"
			"			   K (KB), M (MB), G (GB), T (TB), P (PB), E (EB)\n"
			"	--properties=<name>:<value>[,<name>:<value>,...]\n"
			"			   supported prop names are label, cksum,\n"
			"				cksum_size, srv_cksum, dedup, status\n"
			"				dedup_th, compression, encryption\n"
			"			   label value can be any string\n"
			"			   cksum supported values are off, crc[16,32,64],\n"
			"						      adler32, sha[1,256,512]\n"
			"			   cksum_size can be any size < 4GiB\n"
			"			   srv_cksum values can be on, off\n"
			"			   dedup (preview) values can be off, memcmp or hash\n"
			"			   dedup_th (preview) can be any size between 4KiB and 64KiB\n"
			"			   compression (preview) values can be lz4, deflate, deflate[1-4]\n"
			"			   encrypton (preview) values can be aes-xts[128,256],\n"
			"							     aes-cbc[128,192,256],\n"
			"							     aes-gcm[128,256]\n"
			"			   rf supported values are [0-4]\n"
			"	--acl-file=PATH    input file containing ACL\n"
			"	--user=ID          user who will own the container.\n"
			"			   format: username@[domain]\n"
			"			   default is the effective user\n"
			"	--group=ID         group who will own the container.\n"
			"			   format: groupname@[domain]\n"
			"			   default is the effective group\n");
		} else if (strcmp(argv[3], "destroy") == 0) {
			fprintf(stream,
			"container options (destroy):\n"
			"	--force            destroy container regardless of state\n");
			ALL_BUT_CONT_CREATE_OPTS_HELP();
		} else if (strcmp(argv[3], "clone") == 0) {
			fprintf(stream,
				"container options (clone):\n"
			"	--src=</pool/cont | path>\n"
			"	--=</pool/cont | /pool | path>\n");
		} else if (strcmp(argv[3], "get-attr") == 0 ||
			   strcmp(argv[3], "set-attr") == 0 ||
			   strcmp(argv[3], "del-attr") == 0) {
			fprintf(stream,
			"container options (attribute-related):\n"
			"	--attr=NAME        container attribute name to set, get, del\n"
			"	--value=VALUESTR   container attribute value to set\n");
			ALL_BUT_CONT_CREATE_OPTS_HELP();
		} else if (strcmp(argv[3], "create-snap") == 0 ||
			   strcmp(argv[3], "destroy-snap") == 0 ||
			   strcmp(argv[3], "rollback") == 0) {
			fprintf(stream,
			"container options (snapshot and rollback-related):\n"
			"	--snap=NAME        container snapshot (create/destroy-snap, rollback)\n"
			"	--epc=EPOCHNUM     container epoch (destroy-snap, rollback)\n"
			"	--epcrange=B-E     container epoch range (destroy-snap)\n");
			ALL_BUT_CONT_CREATE_OPTS_HELP();
		} else if (strcmp(argv[3], "set-prop") == 0) {
			fprintf(stream,
			"container options (set-prop):\n"
			"	--properties=<name>:<value>[,<name>:<value>,...]\n"
			"			   supported prop names: label\n"
			"			   label value can be any string\n");
			ALL_BUT_CONT_CREATE_OPTS_HELP();
		} else if (strcmp(argv[3], "get-acl") == 0 ||
			   strcmp(argv[3], "overwrite-acl") == 0 ||
			   strcmp(argv[3], "update-acl") == 0 ||
			   strcmp(argv[3], "delete-acl") == 0) {
			fprintf(stream,
			"container options (ACL-related):\n"
			"	--acl-file=PATH    input file containing ACL (overwrite-acl, "
			"			   update-acl)\n"
			"	--entry=ACE        add or modify a single ACL entry (update-acl)\n"
			"	--principal=ID     principal of entry (delete-acl)\n"
			"			   for users: u:name@[domain]\n"
			"			   for groups: g:name@[domain]\n"
			"			   special principals: OWNER@, GROUP@, EVERYONE@\n"
			"	--verbose          verbose mode (get-acl)\n"
			"	--outfile=PATH     write ACL to file (get-acl)\n");
			ALL_BUT_CONT_CREATE_OPTS_HELP();
		} else if (strcmp(argv[3], "set-owner") == 0) {
			fprintf(stream,
			"container options (set-owner):\n"
			"	--user=ID          user who will own the container.\n"
			"			   format: username@[domain]\n"
			"	--group=ID         group who will own the container.\n"
			"			   format: groupname@[domain]\n");
			ALL_BUT_CONT_CREATE_OPTS_HELP();
		} else if (strcmp(argv[3], "list-objects") == 0 ||
			   strcmp(argv[3], "list-obj") == 0 ||
			   strcmp(argv[3], "query") == 0 ||
			   strcmp(argv[3], "get-prop") == 0 ||
			   strcmp(argv[3], "stat") == 0 ||
			   strcmp(argv[3], "check") == 0 ||
			   strcmp(argv[3], "list-attrs") == 0 ||
			   strcmp(argv[3], "list-snaps") == 0) {
			ALL_BUT_CONT_CREATE_OPTS_HELP();
		} else {
			ALL_CONT_CMDS_HELP();
		}
	} else if (strcmp(argv[2], "obj") == 0 ||
		   strcmp(argv[2], "object") == 0) {
		fprintf(stream, "\n"
		"object (obj) commands:\n"
		"	  query            query an object's layout\n"
		"	  list-keys        list an object's keys\n"
		"	  dump             dump an object's contents\n");

		fprintf(stream,
		"object (obj) options:\n"
		"	  <pool options>   (--pool, --sys-name)\n"
		"	  <cont options>   (--cont)\n"
		"	--oid=HI.LO        object ID\n");

	} else {
		FIRST_LEVEL_HELP();
	}

	return 0;
}

int
main(int argc, char *argv[])
{
	int			rc = 0;
	command_hdlr_t		hdlr = NULL;
	struct cmd_args_s	dargs = {0};

	/* argv[1] is RESOURCE or "help" or "version";
	 * argv[2] if provided is a resource-specific command
	 */
	if (argc == 2 && strcmp(argv[1], "version") == 0) {
		fprintf(stdout, "daos version %s, libdaos %d.%d.%d\n",
			DAOS_VERSION, DAOS_API_VERSION_MAJOR,
			DAOS_API_VERSION_MINOR, DAOS_API_VERSION_FIX);
		return 0;
	} else if (argc < 2 || strcmp(argv[1], "help") == 0) {
		dargs.ostream = stdout;
		help_hdlr(argc, argv, &dargs);
		return 0;
	} else if (argc <= 2) {
		dargs.ostream = stdout;
		help_hdlr(argc, argv, &dargs);
		return 2;
	} else if ((strcmp(argv[1], "container") == 0) ||
		 (strcmp(argv[1], "cont") == 0)) {
		hdlr = cont_op_hdlr;
	} else if ((strcmp(argv[1], "filesystem") == 0) ||
		 (strcmp(argv[1], "fs") == 0)) {
		hdlr = fs_op_hdlr;
	} else if (strcmp(argv[1], "pool") == 0) {
		hdlr = pool_op_hdlr;
	} else if ((strcmp(argv[1], "object") == 0) ||
		 (strcmp(argv[1], "obj") == 0)) {
		hdlr = obj_op_hdlr;
	}

	if (hdlr == NULL) {
		dargs.ostream = stderr;
		help_hdlr(argc, argv, &dargs);
		return 2;
	}

	rc = daos_init();
	if (rc != 0) {
		fprintf(stderr, "failed to initialize daos: %s (%d)\n",
			d_errdesc(rc), rc);
		return 1;
	}

	/* Parse resource sub-command, and any options into dargs struct */
	rc = common_op_parse_hdlr(argc, argv, &dargs);
	if (rc != 0) {
		fprintf(stderr, "error parsing command line arguments\n");
		if (rc > 0) {
			dargs.ostream = stderr;
			help_hdlr(argc, argv, &dargs);
		}
		daos_fini();
		return -1;
	}

	/* Call resource-specific handler function */
	rc = hdlr(&dargs);

	D_FREE(dargs.sysname);
	D_FREE(dargs.path);

	daos_fini();

	if (rc < 0)
		return 1;
	else if (rc > 0) {
		printf("rc: %d\n", rc);
		dargs.ostream = stderr;
		help_hdlr(argc, argv, &dargs);
		return 2;
	}

	return 0;
}
