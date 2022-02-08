/**
 * (C) Copyright 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos. It implements some miscellaneous policy functions
 */

#include <daos_srv/policy.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

#define POLICY_BAD_PARAM	(-1)

static const char *POLICY_NAMES[DAOS_MEDIA_POLICY_MAX] = {
	"io_size",
	"write_intensivity" };

static const char *POLICY_PARAMS[DAOS_MEDIA_POLICY_MAX]
				[DAOS_MEDIA_POLICY_PARAMS_MAX] = {
{"th1", "th2", "", ""},				/* io_size */
{"wr_size", "hot1", "hot2", ""}			/* write_intensivity */
};

static const char *PARAM_DELIM = "/";
static const char *VALUE_DELIM = "=";

static bool
parse_param_val(int param_idx, char *tok, struct policy_desc_t *out_pd)
{
	char *endptr;
	unsigned long val = strtoul(tok, &endptr, 10);

	if (*tok != '\0' && *endptr == '\0' && val <= UINT32_MAX) {
		if (out_pd != NULL)
			out_pd->params[param_idx] = val;
		return true;
	}

	return false;
}

static int
get_param_idx_for_policy(int policy_idx, char *name, int len)
{
	int param_idx = POLICY_BAD_PARAM;
	int i;

	for (i = 0; i < DAOS_MEDIA_POLICY_PARAMS_MAX; i++) {
		if (strncmp(name, POLICY_PARAMS[policy_idx][i], len) == 0) {
			param_idx = i;
			break;
		}
	}

	return param_idx;
}

static int
policy_name_to_idx(char *name, int len)
{
	int policy_idx = DAOS_MEDIA_POLICY_MAX;
	int i;

	for (i = 0; i < DAOS_MEDIA_POLICY_MAX; i++) {
		if (strncmp(name, POLICY_NAMES[i], len) == 0) {
			policy_idx = i;
			break;
		}
	}

	return policy_idx;
}

bool
daos_policy_try_parse(const char *policy_str, struct policy_desc_t *out_pd)
{
	char		*tok = NULL;
	const char	*delim = PARAM_DELIM;
	const char	*delim2 = VALUE_DELIM;
	char		*saveptr = NULL;
	char		*saveptr2 = NULL;
	char		*str2 = NULL;
	bool		type_found = false;
	int		param_idx = POLICY_BAD_PARAM;
	int		tok_len = 0;
	bool		ret_val = false;
	int		i, j;
	size_t		len;
	char		*str, *orig_str;
	unsigned int	policy_idx = DAOS_MEDIA_POLICY_MAX;

	len = strlen(policy_str);
	D_STRNDUP(str, policy_str, len); /* for strtok_r */
	if (str == NULL)
		return ret_val;
	orig_str = str;	/* save the dup'd string ptr to free it later */

	/* tokenize input to "name=value" format */
	for (i = 0; ; i++, str = NULL) {
		tok = strtok_r(str, delim, &saveptr);
		if (tok == NULL)
			break;

		/* tokenize left and right items of "name=value" */
		str2 = tok;

		for (j = 0; ; j++, str2 = NULL) {
			tok = strtok_r(str2, delim2, &saveptr2);
			if (tok == NULL)
				break;

			tok_len = strlen(tok);

			/* type should always be the first parameter */
			if (i == 0 && strncmp(tok, "type", 5) == 0) {
				type_found = true;
				continue;
			}

			if (type_found) {
				policy_idx = policy_name_to_idx(tok, tok_len);
				if (policy_idx == DAOS_MEDIA_POLICY_MAX)
					goto out;

				if (out_pd != NULL)
					out_pd->policy = policy_idx;

				type_found = false;
				continue;
			}
			if (policy_idx != DAOS_MEDIA_POLICY_MAX) {
				if (param_idx == POLICY_BAD_PARAM) {
					param_idx = get_param_idx_for_policy(
						    policy_idx, tok, tok_len);
					if (param_idx == POLICY_BAD_PARAM)
						goto out;

					continue;
				}
				bool ret = parse_param_val(param_idx, tok,
							   out_pd);
				if (!ret)
					goto out;

				param_idx = POLICY_BAD_PARAM;
			}
		}
	}
	if (policy_idx != DAOS_MEDIA_POLICY_MAX)
		ret_val = true;
out:
	D_FREE(orig_str);
	return ret_val;
}
