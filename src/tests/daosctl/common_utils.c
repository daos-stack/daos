/**
 * (C) Copyright 2018 Intel Corporation.
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

#include "common_utils.h"

int
parse_rank_list(char *str_rank_list, d_rank_list_t *num_rank_list)
{
	/* note the presumption that no more than 1000, it just throws
	 * them away if there are, that should be be plenty for
	 * any currently imaginable situation.
	 */
	const uint32_t MAX = 1000;
	uint32_t rank_list[MAX];
	int i = 0;
	char *token;

	token = strtok(str_rank_list, ",");
	while (token != NULL && i < MAX) {
		rank_list[i++] = atoi(token);
		token = strtok(NULL, ",");
	}
	if (i >= MAX)
		printf("rank list exceeded maximum, threw some away");
	num_rank_list->rl_nr = i;
	D_ALLOC_ARRAY(num_rank_list->rl_ranks, num_rank_list->rl_nr);
	if (num_rank_list->rl_ranks == NULL) {
		printf("failed allocating pool service rank list");
		return -1;
	}
	for (i = 0; i < num_rank_list->rl_nr; i++)
		num_rank_list->rl_ranks[i] = rank_list[i];
	return 0;
}

int
parse_oid(char *oid_str, daos_obj_id_t *oid)
{
	char *token;

	token = strtok(oid_str, "-");
	oid->hi = strtoul(token, NULL, 0);
	token = strtok(NULL, "-");
	oid->lo = strtoul(token, NULL, 0);
	return 0;
}

int
parse_size(char *arg, uint64_t *size)
{
	char *unit;
	*size = strtoul(arg, &unit, 0);

	switch (*unit) {
	case '\0':
		break;
	case 'k':
	case 'K':
		*size <<= 10;
		break;
	case 'm':
	case 'M':
		*size <<= 20;
		break;
	case 'g':
	case 'G':
		*size <<= 30;
		break;
	}
	return 0;
}
