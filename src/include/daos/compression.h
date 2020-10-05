/**
 * (C) Copyright 2020 Intel Corporation.
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

#ifndef __DAOS_COMPRESSION_H
#define __DAOS_COMPRESSION_H

#include <daos_prop.h>

/**
 * -----------------------------------------------------------
 * Container Property Knowledge
 * -----------------------------------------------------------
 */

/** Convert a string into a property value for compression property */
int
daos_str2compresscontprop(const char *value);

/**
 * -----------------------------------------------------------
 * DAOS Compressor
 * -----------------------------------------------------------
 */
/**
 * Type of compression algorithm supported by DAOS.
 * Primarily used for looking up the appropriate algorithm functions to be used
 * for the compressor.
 */
enum DAOS_COMPRESS_TYPE {
	COMPRESS_TYPE_UNKNOWN = 0,

	COMPRESS_TYPE_LZ4	= 1,
	COMPRESS_TYPE_GZIP1	= 2,
	COMPRESS_TYPE_GZIP2	= 3,
	COMPRESS_TYPE_GZIP3	= 4,
	COMPRESS_TYPE_GZIP4	= 5,
	COMPRESS_TYPE_GZIP5	= 6,
	COMPRESS_TYPE_GZIP6	= 7,
	COMPRESS_TYPE_GZIP7	= 8,
	COMPRESS_TYPE_GZIP8	= 9,
	COMPRESS_TYPE_GZIP9	= 10,

	COMPRESS_TYPE_END	= 11,
};

/** Lookup the appropriate COMPRESS_TYPE given daos container property */
enum DAOS_COMPRESS_TYPE daos_contprop2compresstype(int contprop_compress_val);

struct compress_ft {
	uint16_t	 cf_level;
	char		*cf_name;
	enum DAOS_COMPRESS_TYPE	cf_type;
};

#endif /** __DAOS_COMPRESSION_H */
