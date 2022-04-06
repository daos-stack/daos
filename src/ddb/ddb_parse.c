/**
 * (C) Copyright 2019-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <wordexp.h>
#include <getopt.h>
#include <gurt/common.h>
#include "ddb_common.h"
#include "ddb_parse.h"

int
ddb_str2argv_create(const char *buf, struct argv_parsed *parse_args)
{
	wordexp_t *we;
	int rc;

	D_ALLOC_PTR(we);
	if (we == NULL)
		return -DER_NOMEM;

	rc = wordexp(buf, we, WRDE_SHOWERR | WRDE_UNDEF);
	if (rc != 0) {
		D_FREE(we);
		return -DER_INVAL;
	}

	parse_args->ap_argc = we->we_wordc;
	parse_args->ap_argv = we->we_wordv;
	parse_args->ap_ctx = we;

	return rc;
}

void
ddb_str2argv_free(struct argv_parsed *parse_args)
{
	wordfree(parse_args->ap_ctx);
	D_FREE(parse_args->ap_ctx);
}

int
ddb_parse_program_args(struct ddb_ctx *ctx, uint32_t argc, char **argv, struct program_args *pa)
{
	struct option	program_options[] = {
		{ "write_mode", no_argument, NULL,	'w' },
		{ "run_cmd", required_argument, NULL,	'R' },
		{ "cmd_file", required_argument, NULL,	'f' },
		{ "pool", required_argument, NULL,	'p' },
		{ NULL }
	};
	int		index = 0, opt;

	optind = 1;
	opterr = 0;
	while ((opt = getopt_long(argc, argv,
				  "wR:f:p:", program_options, &index)) != -1) {
		switch (opt) {
		case 'w':
			break;
		case 'R':
			pa->pa_r_cmd_run = optarg;
			break;
		case 'f':
			pa->pa_cmd_file = optarg;
			break;
		case 'p':
			pa->pa_pool_uuid = optarg;
			break;
		case '?':
			ddb_errorf(ctx, "'%c' is unknown\n", optopt);
			return -DER_INVAL;
		default:
			return -DER_INVAL;
		}
	}

	if (argc - optind > 1) {
		ddb_error(ctx, "Too many commands\n");
		return -DER_INVAL;
	}
	if (argc - optind == 1)
		pa->pa_pool_path = argv[optind];

	return 0;
}

/* parse the string to a bracketed index "[123]" */
static bool
is_idx(char *str, uint32_t *idx)
{
	uint32_t str_len;

	D_ASSERT(idx && str);
	str_len = strlen(str);

	if (str_len < 3) /* must be at least 3 chars */
		return false;

	if (str[0] == '[' && str[str_len - 1] == ']') {
		*idx = atol(str + 1);
		return true;
	}
	return false;
}

static int
process_key(const char *tok, uint8_t **key_buf, daos_key_t *key)
{
	if (tok[0] != '\'' || tok[strlen(tok) - 1] != '\'') {
		D_ERROR("Keys must be surrounded by '\n");
		return -DER_INVAL;
	}

	D_ALLOC(*key_buf, strlen(tok) - 2);
	if (*key_buf == NULL)
		return -DER_NOMEM;
	memcpy(*key_buf, tok + 1, strlen(tok) - 1);
	d_iov_set(key, *key_buf, strlen(tok) - 2);

	return 0;
}

int
ddb_vtp_init(daos_handle_t poh, const char *path, struct dv_tree_path_builder *vt_path)
{
	char		*path_copy;
	char		*path_idx;
	char		*oid_str = NULL;
	char		*recx_str = NULL;
	char		*tok;
	char		*hi;
	char		*lo;
	uint32_t	 path_len;
	int		 rc;
	uint32_t	 recx_str_len;

	/* Setup vt_path */
	D_ASSERT(vt_path);
	memset(vt_path, 0, sizeof(*vt_path));
	vt_path->vtp_poh = poh;
	ddb_vos_tree_path_setup(vt_path);

	/* If there is no path, leave it empty */
	if (path == NULL)
		return 0;

	path_len = strlen(path) + 1; /* +1 for '\0' */
	if (path_len == 0)
		return 0;

	D_ALLOC(path_copy, path_len);
	if (path_copy == NULL)
		return -DER_NOMEM;

	strcpy(path_copy, path);
	path_idx = path_copy;

	/* Look for container */
	tok = strtok(path_idx, "/");
	if (tok == NULL) {
		D_FREE(path_copy);
		return 0;
	}

	if (!is_idx(tok, &vt_path->vtp_cont_idx)) {
		rc = uuid_parse(tok, vt_path->vtp_path.vtp_cont);
		if (rc != 0) {
			D_FREE(path_copy);
			return -DER_INVAL;
		}
	}

	/* look for oid */
	tok = strtok(NULL, "/");

	if (tok != NULL)
		oid_str = tok;

	/* look for dkey */
	tok = strtok(NULL, "/");
	if (tok != NULL && !is_idx(tok, &vt_path->vtp_dkey_idx)) {
		rc = process_key(tok, &vt_path->vtp_dkey_buf, &vt_path->vtp_path.vtp_dkey);
		if (!SUCCESS(rc))
			D_GOTO(error, rc);
	}

	/* look for akey */
	tok = strtok(NULL, "/");
	if (tok != NULL && !is_idx(tok, &vt_path->vtp_akey_idx)) {
		rc = process_key(tok, &vt_path->vtp_akey_buf, &vt_path->vtp_path.vtp_akey);
		if (!SUCCESS(rc))
			D_GOTO(error, rc);
	}

	/* look for recx */
	tok = strtok(NULL, "/");
	if (tok != NULL)
		recx_str = tok;

	/* parse oid */
	if (oid_str != NULL && strlen(oid_str) > 0 && !is_idx(oid_str, &vt_path->vtp_oid_idx)) {
		hi = strtok(oid_str, ".");
		lo = strtok(NULL, ".");
		if (hi == NULL || lo == NULL)
			D_GOTO(error, rc = -DER_INVAL);
		vt_path->vtp_path.vtp_oid.id_pub.hi = atoll(hi);
		vt_path->vtp_path.vtp_oid.id_pub.lo = atoll(lo);
	}

	if (recx_str != NULL && strlen(recx_str) > 0 && !is_idx(tok, &vt_path->vtp_recx_idx)) {
		recx_str_len = strlen(recx_str);

		if (recx_str[0] == '{' && recx_str[recx_str_len - 1] == '}') {
			recx_str++;
		} else {
			D_FREE(path_copy);
			return -DER_INVAL;
		}

		lo = strtok(recx_str, "-");
		hi = strtok(NULL, "-");

		if (hi == NULL || lo == NULL) {
			D_FREE(path_copy);
			return -DER_INVAL;
		}

		vt_path->vtp_path.vtp_recx.rx_idx = atoll(lo);
		vt_path->vtp_path.vtp_recx.rx_nr = atoll(hi) -
						   vt_path->vtp_path.vtp_recx.rx_idx + 1;
	}

	D_FREE(path_copy);
	return 0;

error:
	D_FREE(path_copy);
	ddb_vtp_fini(vt_path);
	return rc;
}

void
ddb_vtp_fini(struct dv_tree_path_builder *vt_path)
{
	D_FREE(vt_path->vtp_dkey_buf);
	D_FREE(vt_path->vtp_akey_buf);
}
