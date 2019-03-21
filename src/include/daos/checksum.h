/**
 * (C) Copyright 2015-2018 Intel Corporation.
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
 * or disclose this software are subject to the terms of the Apache License
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

#ifndef __DAOS_CHECKSUM_H
#define __DAOS_CHECKSUM_H

#include <daos/common.h>
#if defined(__x86_64__)
#include <isa-l.h>
#else
#include <mchecksum.h>
#endif

#define DAOS_CSUM_SIZE 64

enum {
	DAOS_CS_CRC32 = 0,
	DAOS_CS_CRC64 = 1,
	DAOS_CS_MAX,
	DAOS_CS_UNKNOWN,
};

struct daos_csum {
	int			dc_init:1;
#if defined(__x86_64__)
	int			dc_csum;
#else
	mchecksum_object_t	dc_csum;
#endif
	char			dc_buf[DAOS_CSUM_SIZE];

};

typedef struct daos_csum daos_csum_t;
int		daos_csum_init(const char *cs_name, daos_csum_t *checksum);
int		daos_csum_free(daos_csum_t *csum);
int		daos_csum_reset(daos_csum_t *csum);
int		daos_csum_compute(daos_csum_t *csum, daos_sg_list_t *sgl);
daos_size_t	daos_csum_get_size(const daos_csum_t *csum);
int		daos_csum_get(daos_csum_t *csum, daos_csum_buf_t *csum_buf);
int		daos_csum_compare(daos_csum_t *csum, daos_csum_t *csum_src);
#endif
