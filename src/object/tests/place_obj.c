/**
 * (C) Copyright 2016 Intel Corporation.
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
#include <daos/common.h>
#include <daos/placement.h>
#include <daos_api.h>

#define DOM_NR		8
#define	TARGET_PER_DOM	4
#define VOS_PER_TARGET	8

static struct pool_map		*po_map;
static struct pl_map		*pl_map;
static struct pool_component	 comps[DOM_NR + DOM_NR * TARGET_PER_DOM];

static int
plt_obj_place(daos_obj_id_t oid)
{
	struct pl_obj_layout	*layout;
	struct daos_obj_md	 md;
	int			 i;
	int			 rc;

	memset(&md, 0, sizeof(md));
	md.omd_id  = oid;
	md.omd_ver = 1;

	rc = pl_obj_place(pl_map, &md, NULL, &layout);
	D_ASSERT(rc == 0);

	D_PRINT("Layout of object "DF_OID"\n", DP_OID(oid));
	for (i = 0; i < layout->ol_nr; i++)
		D_PRINT("%d ", layout->ol_targets[i]);

	D_PRINT("\n");

	pl_obj_layout_free(layout);
	return 0;
}

int
main(int argc, char **argv)
{
	struct pool_buf		*buf;
	struct pl_map_init_attr	 mia;
	int			 i;
	int			 nr;
	int			 rc;
	struct pool_component	*comp;
	daos_obj_id_t		 oid = {1, 3, 5};

	comp = &comps[0];
	/* fake the pool map */
	for (i = 0; i < DOM_NR; i++, comp++) {
		comp->co_type   = PO_COMP_TP_RACK;
		comp->co_status = PO_COMP_ST_UP;
		comp->co_id	= i;
		comp->co_rank   = i;
		comp->co_ver    = 1;
		comp->co_nr	= TARGET_PER_DOM;
	}

	for (i = 0; i < DOM_NR * TARGET_PER_DOM; i++, comp++) {
		comp->co_type   = PO_COMP_TP_TARGET;
		comp->co_status = PO_COMP_ST_UP;
		comp->co_id	= i;
		comp->co_rank   = i;
		comp->co_ver    = 1;
		comp->co_nr	= VOS_PER_TARGET;
	}

	nr = ARRAY_SIZE(comps);
	buf = pool_buf_alloc(nr);
	D_ASSERT(buf != NULL);

	rc = pool_buf_attach(buf, comps, nr);
	D_ASSERT(rc == 0);

	rc = pool_map_create(buf, 1, &po_map);
	D_ASSERT(rc == 0);

	pool_map_print(po_map);

	mia.ia_type	    = PL_TYPE_RING;
	mia.ia_ver	    = 1;
	mia.ia_ring.ring_nr = 1;
	mia.ia_ring.domain  = PO_COMP_TP_RACK;

	rc = pl_map_create(po_map, &mia, &pl_map);
	D_ASSERT(rc == 0);

	pl_map_print(pl_map);

	daos_obj_id_generate(&oid, DAOS_OC_SMALL_RW);
	rc = plt_obj_place(oid);
	D_ASSERT(rc == 0);

	pl_map_destroy(pl_map);

	pool_map_destroy(po_map);
	pool_buf_free(buf);
	return 0;
}
