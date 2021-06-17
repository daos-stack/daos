/**
 * (C) Copyright 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos. It implements some miscellaneous policy functions
 */

#include "policy.h"
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

#define POLICY_BAD_PARAM	(-1)

static const char *POLICY_NAMES[DAOS_MEDIA_POLICY_MAX] = {
	"io_size",
	"write_intensivity" };

static const char *POLICY_PARAMS[DAOS_MEDIA_POLICY_MAX]
				[DAOS_MEDIA_POLICY_PARAMS_MAX] =
{
{"th1", "th2", "", ""},				/* io_size */
{"wr_size", "hot1", "hot2", ""}			/* write_intensivity */
};

static const char *PARAM_DELIM = ",";
static const char *VALUE_DELIM = "=";

bool
daos_policy_try_parse(const char *policy_str, struct policy_desc_t *out_pd)
{
	char *tok		= NULL;
	const char *delim	= PARAM_DELIM;
	const char *delim2	= VALUE_DELIM;
	char *saveptr		= NULL;
	char *saveptr2		= NULL;
	char *str2		= NULL;
	bool type_found		= false;
	int param_idx		= POLICY_BAD_PARAM;
	int tok_len		= 0;
	bool ret_val		= false;

	int i,j,k;

	size_t len = strlen(policy_str);

	char *str;
	D_STRNDUP(str, policy_str, len); /* for strtok_r */

	unsigned int policy_idx = DAOS_MEDIA_POLICY_MAX;

	/* tokenize input to "name=value" format */
	for (i = 0; ;i++, str = NULL) {
		tok = strtok_r(str, delim, &saveptr);
		if (tok == NULL) {
			break;
		}
		/* tokenize left and right items of "name=value" */
		str2 = tok;

		for (j = 0; ; j++, str2 = NULL) {
			tok = strtok_r(str2, delim2, &saveptr2);
			if (tok == NULL) {
				break;
			}

			if (strncmp(tok, "type", 5) == 0) {
				type_found = true;
				continue;
			}

			tok_len = strlen(tok);
			if (type_found) {
				for (k = 0; k < DAOS_MEDIA_POLICY_MAX; k++) {
					if (strncmp(tok, POLICY_NAMES[k],
					    tok_len) == 0) {
						policy_idx = k;
						if (out_pd != NULL)
							out_pd->policy = k;

						break;
					}
				}
				if (policy_idx == DAOS_MEDIA_POLICY_MAX)
					goto out;

				type_found = false;
				continue;
			} else if (policy_idx != DAOS_MEDIA_POLICY_MAX) {
				if (param_idx == POLICY_BAD_PARAM) {
					for (k = 0;
					     k < DAOS_MEDIA_POLICY_PARAMS_MAX;
					     k++) {
						if (strncmp(tok,
						  POLICY_PARAMS[policy_idx][k],
						  tok_len) == 0) {
							param_idx = k;
							break;
						}
					}
					if (param_idx == POLICY_BAD_PARAM)
						goto out;

					continue;
				} else {
					char *endptr;
					int val = strtol(tok, &endptr, 10);

					if (*tok != '\0' && *endptr == '\0') {
						if (out_pd != NULL) {
							out_pd->
							params[param_idx] = val;
						}
					} else {
						goto out;
					}

					param_idx = POLICY_BAD_PARAM;
					continue;
				}
			}
		}
	}
	ret_val = true;
out:
	free(str);
	return ret_val;
}
