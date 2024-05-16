/**
 * (C) Copyright 2019-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <wordexp.h>
#include <getopt.h>
#include <gurt/common.h>
#include "ddb_common.h"
#include "ddb_parse.h"

void
safe_strcat(char *dst, const char *src, size_t dst_size)
{
	size_t remaining_space = dst_size - strlen(dst) - 1; // Subtract 1 for null terminator

	strncat(dst, src, remaining_space);
}

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
			safe_strcat(vos_file_parts->vf_db_path, "/", DB_PATH_LEN);
			safe_strcat(vos_file_parts->vf_db_path, tok, DB_PATH_LEN);
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

	return 0;
}

void
ddb_str2argv_free(struct argv_parsed *parse_args)
{
	if (parse_args->ap_ctx != NULL) {
		wordfree(parse_args->ap_ctx);
		D_FREE(parse_args->ap_ctx);
	}
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

/*
 * A key can be a string, integer, or arbitrary binary data in hex format. The following functions
 * parse a string input (usually provided in a VOS path) into the appropriate daos_key_t. In order
 * for a string to match when doing a fetch, it must be exactly the same, including the iov_len of
 * the key. The DDB help output explains the expected format of the string.
 *
 * When a string is parsed into a key, the key buffer will be allocated to the appropriate size.
 *
 */

/* These types are for integer or binary types */
enum key_value_type {
	KEY_VALUE_TYPE_UNKNOWN,
	KEY_VALUE_TYPE_UINT8,
	KEY_VALUE_TYPE_UINT16,
	KEY_VALUE_TYPE_UINT32,
	KEY_VALUE_TYPE_UINT64,
	KEY_VALUE_TYPE_BIN,
};

/* Helper function for allocating the memory for a key */
static int
key_alloc(daos_key_t *key, void *value, uint32_t value_len)
{
	int rc;

	rc = daos_iov_alloc(key, value_len, true);
	if (!SUCCESS(rc))
		return rc;

	memcpy(key->iov_buf, value, value_len);
	return 0;
}

/* Helper for setting the type if the input matches the provided type (i.e. uint32) */
#define if_type_is_set(input, type, type_str, type_value) do { \
	if (strncmp(input, type_str, strlen(type_str)) == 0) { \
		type = type_value; \
		input += strlen(type_str); \
	} \
} while (0)

/* Helper for parsing the string into a key  if the input matches the provided type (i.e. uint32) */
#define if_type_is_parse(t, t_enum, type, key_str, key, rc) do { \
	if (t == t_enum) { \
		type value = strtoul(key_str, NULL, 0); \
		rc = key_alloc(key, &value, sizeof(value)); \
	} \
} while (0)

/*
 * The format of the size portion of the key is a number surrounded by the open and close
 * characters, generally '()' or '{}'.
 *
 * For example, a string key can have a size provided to specify the length of the key. This is
 * needed if strlen(key_str) != iov_len. For example if a null terminator is included as part
 * of the key.
 *
 * Return number of chars consumed, or error
 */
static int
key_parse_size(const char *input, size_t *size, char open, char close)
{
	const char	*value_str;
	int		 len = 0;

	if (input[0] != open)
		return -DER_INVAL;

	input++;

	value_str = input;

	while (isdigit(input[len]))
		len++;
	input += len;

	if (input[0] != close)
		return -DER_INVAL;
	*size = strtoul(value_str, NULL, 10);
	if (*size == 0)
		return -DER_INVAL;

	return len + 2; /* +2 for '{', '}' */
}

/* Tests if the string input looks like it could be a hex number (starts with '0x' */
static inline bool
is_hex(const char *input)
{
	if (strlen(input) <= 2)
		return false;
	return (input[0] == '0' && (input[1] == 'x' || input[1] == 'X'));
}

/* Parse a key that is arbitrary binary data represented as hex. */
static int
key_parse_bin(const char *input, daos_key_t *key)
{
	uint8_t	*buf;
	size_t	 len = 0;
	size_t	 data_len;
	int	 i;

	if (!is_hex(input)) {
		D_ERROR("binary data should be represented as hex\n");
		return -DER_INVAL;
	}
	input += 2;
	while (isxdigit(input[len]))
		len++;

	if (len % 2 != 0) {
		D_ERROR("incomplete bytes not supported. Please prepend leading 0\n");
		return -DER_INVAL;
	}

	data_len = len / 2;
	D_ALLOC(buf, data_len);
	if (buf == NULL)
		return -DER_NOMEM;

	for (i = 0; i < len; i += 2) {
		char	tmp[3] = {0};
		uint8_t byte;

		tmp[0] = input[i];
		tmp[1] = input[i + 1];
		byte = strtoul(tmp, NULL, 16);
		buf[i/2] = byte;
	}

	d_iov_set(key, buf, data_len);
	return 0;
}

/* Parse a key that is an int.  */
static int
key_parse_int(enum key_value_type type, const char *input, daos_key_t *key)
{
	int rc = -DER_INVAL;

	if_type_is_parse(type, KEY_VALUE_TYPE_UINT8, uint8_t, input, key, rc);
	if_type_is_parse(type, KEY_VALUE_TYPE_UINT16, uint16_t, input, key, rc);
	if_type_is_parse(type, KEY_VALUE_TYPE_UINT32, uint32_t, input, key, rc);
	if_type_is_parse(type, KEY_VALUE_TYPE_UINT64, uint64_t, input, key, rc);

	return rc;
}

/*
 * Parse a non-string key (integer or binary).
 *
 * Both integers and binary keys have similar format: "{type: value}", where type is the last part
 * of the key_value_type enum (as lowercase). Binary can also include a size: "{bin(size): 0x1234}"
 *
 * Return number of chars consumed, or error
 */
static int
key_parse_typed(const char *key_str, daos_key_t *key)
{
	enum key_value_type	 type = KEY_VALUE_TYPE_UNKNOWN;
	const char		*value_str;
	size_t			 size = 0;
	int			 rc;
	const char		*key_str_idx;

	key_str_idx = key_str;
	if (key_str_idx[0] != '{')
		return -DER_INVAL;

	key_str_idx++;

	/* get the specific type */
	if_type_is_set(key_str_idx, type, "uint8", KEY_VALUE_TYPE_UINT8);
	if_type_is_set(key_str_idx, type, "uint16", KEY_VALUE_TYPE_UINT16);
	if_type_is_set(key_str_idx, type, "uint32", KEY_VALUE_TYPE_UINT32);
	if_type_is_set(key_str_idx, type, "uint64", KEY_VALUE_TYPE_UINT64);
	if_type_is_set(key_str_idx, type, "bin", KEY_VALUE_TYPE_BIN);
	if (type == KEY_VALUE_TYPE_UNKNOWN)
		return -DER_INVAL;

	/* is there a size */
	if (key_str_idx[0] == '(') {
		rc = key_parse_size(key_str_idx, &size, '(', ')');
		if (rc < 0)
			return rc;
		key_str_idx += rc;
	}

	if (key_str_idx[0] != ':') /* ':' should separate the type and value */
		return -DER_INVAL;

	key_str_idx++;

	value_str = key_str_idx;

	/* have key value ... just verifying the rest is valid number */
	if (is_hex(key_str_idx)) {
		key_str_idx += 2;
		while (isxdigit(key_str_idx[0]))
			key_str_idx++;
	} else {
		while (isdigit(key_str_idx[0]))
			key_str_idx++;
	}

	if (key_str_idx[0] != '}')
		return -DER_INVAL;
	key_str_idx++;

	if (type == KEY_VALUE_TYPE_BIN)
		rc = key_parse_bin(value_str, key);
	else
		rc = key_parse_int(type, value_str, key);

	if (!SUCCESS(rc))
		return rc;
	return key_str_idx - key_str;
}

/*
 * Parse a string key.
 * String keys need to be able to support specifying size of the key and to escape special
 * characters ('{', '}', '/').
 *
 * Return number of chars consumed, or error
 */
static int
key_parse_str(const char *input, daos_key_t *key)
{
	size_t		 key_len = 0;
	size_t		 size = 0;
	uint32_t	 escaped_chars = 0;
	int		 i, j;
	int		 rc;
	const char	*ptr;

	/* size_open char can't be curly brace */
	if (input[0] == '{' || input[0] == '}')
		return -DER_INVAL;

	ptr = input;
	while (ptr[0] != '\0' && ptr[0] != '/') {
		if (ptr[0] == '\\') {
			ptr += 1; /* move past escape character */
			if (ptr[0] == '\0') /* escape character can't be last */
				return -DER_INVAL;
			/* don't really care what escaping as long as not the end */
			ptr += 1;
			escaped_chars++;
			key_len++;

		} else  if (ptr[0] == '}') {
			return -DER_INVAL; /* should never see this here */
		} else  if (ptr[0] == '{') {
			rc = key_parse_size(ptr, &size, '{', '}');
			if (rc < 0)
				return rc;
			ptr += rc;
			if (ptr[0] != '\0' && ptr[0] != '/') /* size should be last thing */
				return -DER_INVAL;
		} else {
			ptr++;
			key_len++;
		}
	}
	if (size == 0)
		size = key_len;
	if (size < key_len)
		return -DER_INVAL;

	rc = daos_iov_alloc(key, size, true);
	if (!SUCCESS(rc))
		return -DER_NOMEM;

	for (i = 0, j = 0; i < key_len + escaped_chars; ++i) {
		if (input[i] != '\\')
			((char *)key->iov_buf)[j++] = input[i];
	}

	return (int)(ptr - input);
}

/*
 * Parse string input into a daos_key_t. The buffer for the key will be allocated. The caller
 * is expected to call daos_iov_free() to free the memory.
 *
 * Return number of chars consumed, or error
 */
int
ddb_parse_key(const char *input, daos_key_t *key)
{
	if (input == NULL || strlen(input) == 0)
		return -DER_INVAL;

	return input[0] == '{' ?
	       key_parse_typed(input, key) :
	       key_parse_str(input, key);
}
