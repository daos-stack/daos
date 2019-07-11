/* Copyright (C) 2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * Common functions to be shared among tests
 */
#ifndef __TESTS_COMMON_H__
#define __TESTS_COMMON_H__
#include <semaphore.h>
#include <cart/api.h>
#include <gurt/common.h>

int
tc_load_group_from_file(const char *grp_cfg_file, crt_group_t *grp,
		int num_contexts,
		d_rank_t my_rank, bool delete_file)
{
	FILE		*f;
	int		parsed_rank;
	char		parsed_addr[255];
	int		parsed_port;
	char		full_uri[511];
	crt_node_info_t node_info;
	int		i;
	int		rc = 0;

	f = fopen(grp_cfg_file, "r");
	if (!f) {
		D_ERROR("Failed to open %s for reading\n", grp_cfg_file);
		D_GOTO(out, rc = DER_NONEXIST);
	}

	while (1) {
		rc = fscanf(f, "%d %s %d", &parsed_rank,
			parsed_addr, &parsed_port);
		if (rc == EOF) {
			rc = 0;
			break;
		}

		if (parsed_rank == my_rank)
			continue;

		for (i = 0; i < num_contexts; i++) {
			sprintf(full_uri, "%s:%d", parsed_addr,
				parsed_port + i);

			node_info.uri = full_uri;
			rc = crt_group_node_add(grp, parsed_rank, i,
						node_info);
			if (rc != 0) {
				D_ERROR("Failed to add %d %s; rc=%d\n",
					parsed_rank, full_uri, rc);
				break;
			}
		}
	}

	fclose(f);

	if (delete_file)
		unlink(grp_cfg_file);

out:
	return rc;
}

static inline void
tc_sem_timedwait(sem_t *sem, int sec, int line_number)
{
	struct timespec	deadline;
	int		rc;

	rc = clock_gettime(CLOCK_REALTIME, &deadline);
	D_ASSERTF(rc == 0, "clock_gettime() failed at line %d rc: %d\n",
		  line_number, rc);

	deadline.tv_sec += sec;
	rc = sem_timedwait(sem, &deadline);
	D_ASSERTF(rc == 0, "sem_timedwait() failed at line %d rc: %d\n",
		  line_number, rc);
}
#endif /* __TESTS_COMMON_H__ */
