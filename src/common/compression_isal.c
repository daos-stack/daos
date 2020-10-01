/**
 * (C) Copyright 2019-2020 Intel Corporation.
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

#define D_LOGFAC	DD_FAC(csum)

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include <isa-l.h>
#include <gurt/types.h>
#include <daos/common.h>
#include <daos/compression.h>

/**
 * ---------------------------------------------------------------------------
 * Algorithms
 * ---------------------------------------------------------------------------
 */

struct compress_ft lz4_algo = {
	.cf_name = "lz4",
	.cf_type = COMPRESS_TYPE_LZ4
};

struct compress_ft gzip1_algo = {
	.cf_level = 1,
	.cf_name = "gzip1",
	.cf_type = COMPRESS_TYPE_GZIP1
};

struct compress_ft gzip2_algo = {
	.cf_level = 2,
	.cf_name = "gzip2",
	.cf_type = COMPRESS_TYPE_GZIP2
};

struct compress_ft gzip3_algo = {
	.cf_level = 3,
	.cf_name = "gzip3",
	.cf_type = COMPRESS_TYPE_GZIP3
};

struct compress_ft gzip4_algo = {
	.cf_level = 4,
	.cf_name = "gzip4",
	.cf_type = COMPRESS_TYPE_GZIP4
};

struct compress_ft gzip5_algo = {
	.cf_level = 5,
	.cf_name = "gzip5",
	.cf_type = COMPRESS_TYPE_GZIP5
};

struct compress_ft gzip6_algo = {
	.cf_level = 6,
	.cf_name = "gzip6",
	.cf_type = COMPRESS_TYPE_GZIP6
};

struct compress_ft gzip7_algo = {
	.cf_level = 7,
	.cf_name = "gzip7",
	.cf_type = COMPRESS_TYPE_GZIP7
};

struct compress_ft gzip8_algo = {
	.cf_level = 8,
	.cf_name = "gzip8",
	.cf_type = COMPRESS_TYPE_GZIP8
};

struct compress_ft gzip9_algo = {
	.cf_level = 9,
	.cf_name = "gzip9",
	.cf_type = COMPRESS_TYPE_GZIP9
};

/** Index to algo table should align with enum DAOS_COMPRESS_TYPE - 1 */
struct compress_ft *isal_compress_algo_table[] = {
	&lz4_algo,
	&gzip1_algo,
	&gzip2_algo,
	&gzip3_algo,
	&gzip4_algo,
	&gzip5_algo,
	&gzip6_algo,
	&gzip7_algo,
	&gzip8_algo,
	&gzip9_algo,
};
