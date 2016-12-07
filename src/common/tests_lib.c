/**
 * (C) Copyright 2015, 2016 Intel Corporation.
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
#define DD_SUBSYS	DD_FAC(tests)

/**
 * Test suite helper functions.
 */
#include <daos_types.h>
#include <daos_api.h>
#include <daos/common.h>
#include <daos/tests_lib.h>

#define DTS_OCLASS_DEF		DAOS_OC_REPL_MAX_RW

static uint64_t obj_id_gen	= 1;
static uint64_t int_key_gen	= 1;

daos_obj_id_t
dts_oid_gen(uint16_t oclass, unsigned seed)
{
	daos_obj_id_t	oid;

	srand(time(NULL));

	if (oclass == 0)
		oclass = DTS_OCLASS_DEF;

	/* generate an unique and not scary long object ID */
	oid.lo	= obj_id_gen++;
	oid.mid	= seed;
	oid.hi	= rand() % 100;
	daos_obj_id_generate(&oid, oclass);

	return oid;
}

daos_unit_oid_t
dts_unit_oid_gen(uint16_t oclass, uint32_t shard)
{
	daos_unit_oid_t	uoid;

	uoid.id_pub	= dts_oid_gen(oclass, time(NULL));
	uoid.id_shard	= shard;
	uoid.id_pad_32	= 0;

	return uoid;
}

void
dts_key_gen(char *key, unsigned int key_len, const char *prefix)
{
	memset(key, 0, key_len);
	if (prefix == NULL)
		snprintf(key, key_len, DF_U64, int_key_gen);
	else
		snprintf(key, key_len, "%s-"DF_U64, prefix, int_key_gen);
	int_key_gen++;
}

void
dts_buf_render(char *buf, unsigned int buf_len)
{
	int	nr = 'z' - 'a' + 1;
	int	i;

	srand(time(NULL));
	for (i = 0; i < buf_len - 1; i++) {
		int randv = rand() % (2 * nr);

		if (randv < nr)
			buf[i] = 'a' + randv;
		else
			buf[i] = 'A' + (randv - nr);
	}
	buf[i] = '\0';
}
