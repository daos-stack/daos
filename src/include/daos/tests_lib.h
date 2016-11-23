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

#ifndef __DAOS_TESTS_LIB_H__
#define __DAOS_TESTS_LIB_H__

/** Fill in readable random bytes into the buffer */
void dts_buf_render(char *buf, unsigned int buf_len);

/** generate a unique key */
void dts_key_gen(char *key, unsigned int key_len, const char *prefix);

/** generate a random and unique object ID */
daos_obj_id_t dts_oid_gen(uint16_t oclass);

/** generate a random and unique baseline object ID */
daos_unit_oid_t dts_unit_oid_gen(uint16_t oclass, uint32_t shard);

#endif /* __DAOS_TESTS_LIB_H__ */
