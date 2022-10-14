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
vos_path_parse(const char *path, struct vos_file_parts *vos_file_parts)
{
	uint32_t	 path_len = strlen(path) + 1;
	char		*path_copy;
	char		*tok;
	int		 rc = -DER_INVAL;

	D_ASSERT(path != NULL && vos_file_parts != NULL);

	D_ALLOC(path_copy, path_len);
	if (path_copy == NULL)
		return -DER_NOMEM;
	strcpy(path_copy, path);

	tok = strtok(path_copy, "/");
	while (tok != NULL && rc != 0) {
		rc = uuid_parse(tok, vos_file_parts->vf_pool_uuid);
		if (!SUCCESS(rc)) {
			strcat(vos_file_parts->vf_db_path, "/");
			strcat(vos_file_parts->vf_db_path, tok);
		}
		tok = strtok(NULL, "/");
	}

	if (rc != 0 || tok == NULL) {
		D_ERROR("Incomplete path: %s\n", path);
		D_GOTO(done, rc = -DER_INVAL);
	}

	strncpy(vos_file_parts->vf_vos_file, tok, ARRAY_SIZE(vos_file_parts->vf_vos_file) - 1);

	/*
	 * file name should be vos-N ... split on "-"
	 * If not, might be test, just assume target of 0
	 */
	strtok(tok, "-");
	tok = strtok(NULL, "-");
	if (tok != NULL) {
		D_WARN("vos file name not in correct format: %s\n", vos_file_parts->vf_vos_file);
		vos_file_parts->vf_target_idx = atoi(tok);
	}

done:
	if (!SUCCESS(rc)) {
		/* Reset to if not valid */
		memset(vos_file_parts, 0, sizeof(*vos_file_parts));
	}
	D_FREE(path_copy);
	return rc;
}


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
		{ "help", required_argument, NULL,	'h' },
		{ NULL }
	};
	int		index = 0, opt;

	optind = 0; /* Reinitialize getopt */
	opterr = 0;
	while ((opt = getopt_long(argc, argv, "wR:f:h", program_options, &index)) != -1) {
		switch (opt) {
		case 'w':
			pa->pa_write_mode = true;
			break;
		case 'R':
			pa->pa_r_cmd_run = optarg;
			break;
		case 'f':
			pa->pa_cmd_file = optarg;
			break;
		case 'h':
			pa->pa_get_help = true;
			break;
		case '?':
			ddb_errorf(ctx, "'%c'(0x%x) is unknown\n", optopt, optopt);
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
	uint32_t key_buf_len;

	key_buf_len = strlen(tok) + 1; /* + 1 for '\0' */

	D_ALLOC(*key_buf, key_buf_len);
	if (*key_buf == NULL)
		return -DER_NOMEM;
	memcpy(*key_buf, tok, key_buf_len);
	(*key_buf)[key_buf_len - 1] = '\0';
	d_iov_set(key, *key_buf, key_buf_len);
	key->iov_len = key_buf_len - 1;

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

int
ddb_parse_dtx_id(const char *dtx_id_str, struct dtx_id *dtx_id)
{
	char	 cpy[128] = {0};
	char	 validate_buf[128] = {0};
	char	*tok;

	if (dtx_id_str == NULL)
		return -DER_INVAL;

	strncpy(cpy, dtx_id_str, sizeof(cpy) - 1);

	tok = strtok(cpy, ".");
	if (tok == NULL)
		return -DER_INVAL;
	if (uuid_parse(tok, dtx_id->dti_uuid) < 0)
		return -DER_INVAL;

	tok = strtok(NULL, ".");
	dtx_id->dti_hlc = strtoll(tok, NULL, 16);

	/* Validate input was complete and in correct format */
	snprintf(validate_buf, 128, DF_DTIF, DP_DTI(dtx_id));
	if (strncmp(dtx_id_str, validate_buf, 128) != 0)
		return -DER_INVAL;

	return DER_SUCCESS;
}
