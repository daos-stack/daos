/**
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDK-License-Identifier: BSD-2-Clause-Patent
 */

/*
 * This provides a simple example for how to use the data filter capabilities
 * in DAOS.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <daos.h>
#include <daos_pipeline.h>

/** daos info */
static daos_handle_t	poh; /** pool */
static daos_handle_t	coh; /** container */
static daos_handle_t	oh;  /** object */

#define	ASSERT(cond, ...)			\
do {						\
	if (!(cond)) {				\
		fprintf(stderr, __VA_ARGS__);	\
		exit(1);			\
	}					\
} while (0)

/** DB info */
#define NR_IODS		3
#define STRING_LEN	10
static char	*fields[NR_IODS]	= {"Owner", "Species", "Sex"};


void
insert_example_records(void)
{
	int		rc;
	d_iov_t		dkey;
	d_sg_list_t	sgls[NR_IODS];
	d_iov_t		iovs[NR_IODS];
	daos_iod_t	iods[NR_IODS];
	uint32_t	i, j;

	char	name[8][STRING_LEN]	= {"Slim\0",
					   "Buffy\0",
					   "Claws\0",
					   "Whistler\0",
					   "Chirpy\0",
					   "Browser\0",
					   "Fang\0",
					   "Fluffy\0"};
	char	owner[8][STRING_LEN]	= {"Benny\0",
					   "Harold\0",
					   "GWen\0",
					   "Gwen\0",
					   "Gwen\0",
					   "Diane\0",
					   "Benny\0",
					   "Harold\0"};
	char	species[8][STRING_LEN]	= {"snake\0",
					   "dog\0",
					   "cat\0",
					   "bird\0",
					   "bird\0",
					   "dog\0",
					   "dog\0",
					   "cat\0"};
	char	sex[8][STRING_LEN]	= {"m\0",
					   "f\0",
					   "m\0",
					   "m\0",
					   "f\0",
					   "m\0",
					   "m\0",
					   "f\0"};
	char	**data[NR_IODS];

	data[0] = (char **)owner;
	data[1] = (char **)species;
	data[2] = (char **)sex;

	for (i = 0; i < 8; i++) { /** records */
		/** set dkey for record */
		d_iov_set(&dkey, name[i], STRING_LEN);
		for (j = 0; j < NR_IODS; j++) { /** fields */
			/** akeys */
			sgls[j].sg_nr		= 1;
			sgls[j].sg_nr_out	= 0;
			sgls[j].sg_iovs		= &iovs[0];
			d_iov_set(&iovs[0], data[j][i], STRING_LEN);

			d_iov_set(&iods[j].iod_name, (void *)fields[j],
					strlen(fields[j]));
			iods[j].iod_nr		= 1;
			iods[j].iod_size	= STRING_LEN;
			iods[j].iod_recxs	= NULL;
			iods[j].iod_type	= DAOS_IOD_SINGLE;
		}
		rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &dkey, NR_IODS, iods,
					sgls, NULL);
		ASSERT(rc == 0, "Obj update failed with %d", rc);
	}
}

/**
 * Build filter "Owner == Benny"
 */
void
build_filter_one(daos_pipeline_t *pipeline)
{
	daos_pipeline_filter_t	akey_ft, eqfunc_ft, const_ft;
	char			akey_ftype[]    = "DAOS_FILTER_AKEY";
	char			const_ftype[]   = "DAOS_FILTER_CONST";
	char			eqfunc_ftype[]  = "DAOS_FILTER_FUNC_EQ";
	char			constant[STRING_LEN] = "Benny\0";
	daos_pipeline_node_t	comp_eq_node;

	/** akey for filter */
	akey_ft.filter_type  = akey_ftype;
	akey_ft.data_type    = DAOS_FILTER_TYPE_STRING;
	akey_ft.num_operands = 0;
	d_iov_set(&(akey_ft.akey), "Owner", 5);
	akey_ft.data_offset  = 0;
	akey_ft.data_len     = STRING_LEN;

	/** constant for filter */
	const_ft.filter_type     = const_ftype;
	const_ft.data_type       = DAOS_FILTER_TYPE_STRING;
	const_ft.num_operands    = 0;
	const_ft.num_constants   = 1;
	const_ft.constant        = (d_iov_t *) malloc(sizeof(d_iov_t));
	d_iov_set(const_ft.constant, constant, STRING_LEN);
	const_ft.data_offset   = 0;
	const_ft.data_len      = STRING_LEN;

	/** function for filter */
	eqfunc_ft.filter_type   = eqfunc_ftype;
	eqfunc_ft.data_type     = DAOS_FILTER_TYPE_STRING;
	eqfunc_ft.num_operands  = 2;
	eqfunc_ft.data_offset   = 0;
	eqfunc_ft.data_len      = 0;

	/** building pipeline node for the filter:
	 *    the order of operands is prefix:
	 *         "Owner == Benny"  ->  |(func=eq)|(akey=Owner)|(const=Benny)|
	 */
	comp_eq_node.node_type   = DAOS_PIPELINE_CONDITION;
	comp_eq_node.num_filters = 3;
	daos_pipeline_node_push(&comp_eq_node, &eqfunc_ft);
	daos_pipeline_node_push(&comp_eq_node, &akey_ft);
	daos_pipeline_node_push(&comp_eq_node, &const_ft);

	/** adding the node to the pipeline */
	daos_pipeline_push(pipeline, &comp_eq_node);
}

void
run_pipeline(daos_pipeline_t *pipeline)
{
	daos_iod_t	iods[NR_IODS];
	daos_anchor_t	anchor;
	uint32_t	nr_iods;
	uint32_t	nr_kds;
	daos_key_desc_t	*kds;
	d_sg_list_t	*sgl_keys;
	d_iov_t		*iovs_keys;
	char		*buf_keys;
	d_sg_list_t	*sgl_recx;
	d_iov_t		*iovs_recx;
	char		*buf_recx;
	uint32_t	i, j, l;
	int		rc;

	/** iods: information about what akeys to retrieve */
	for (i = 0; i < NR_IODS; i++) {
		iods[i].iod_nr		= 1;
		iods[i].iod_size	= STRING_LEN;
		iods[i].iod_recxs	= NULL;
		iods[i].iod_type	= DAOS_IOD_SINGLE;
		d_iov_set(&iods[i].iod_name, (void *) fields[i],
						strlen(fields[i]));
	}

	/**
	 * reading in chunks of 64 keys (at most) at a time
	 */
	nr_kds   = 64;
	nr_iods  = NR_IODS;

	/** sgl_keys: to store the retrieved dkeys */
	kds		= malloc(sizeof(daos_key_desc_t)*nr_kds);
	sgl_keys	= malloc(sizeof(d_sg_list_t)*nr_kds);
	iovs_keys	= malloc(sizeof(d_iov_t)*nr_kds);
	buf_keys	= malloc(nr_kds*STRING_LEN);
	for (i = 0; i < nr_kds; i++) {
		sgl_keys[i].sg_nr	= 1;
		sgl_keys[i].sg_nr_out	= 0;
		sgl_keys[i].sg_iovs	= &iovs_keys[i];
		d_iov_set(&iovs_keys[i], &(buf_keys[i*STRING_LEN]), STRING_LEN);
	}

	/** sgl_recx: to store the retrieved data for the akeys of each dkey */
	sgl_recx	= malloc(sizeof(d_sg_list_t)*nr_kds*nr_iods);
	iovs_recx	= malloc(sizeof(d_iov_t)*nr_kds*nr_iods);
	buf_recx	= malloc(nr_kds*nr_iods*STRING_LEN);
	for (i = 0; i < nr_kds; i++) {
		for (j = 0; j < nr_iods; j++) {
			l = i*nr_iods+j;
			sgl_recx[l].sg_nr	= 1;
			sgl_recx[l].sg_nr_out	= 0;
			sgl_recx[l].sg_iovs	= &iovs_recx[l];
			d_iov_set(&iovs_recx[l], &(buf_recx[l*STRING_LEN]),
							STRING_LEN);
		}

	}

	/** reset anchor */
	memset(&anchor, 0, sizeof(daos_anchor_t));

	/** reading 64 records at a time */
	printf("records:\n");
	while (!daos_anchor_is_eof(&anchor)) {
		nr_kds = 64; /** trying to read 64 at a time */
		rc = daos_pipeline_run(oh, *pipeline, DAOS_TX_NONE, 0, NULL,
				       &nr_iods, iods, &anchor, &nr_kds,
				       kds, sgl_keys, sgl_recx, NULL,
				       NULL);
		ASSERT(rc == 0, "Pipeline run failed with %d", rc);
		/** process nr_kds fetched records */
		for (i = 0; i < nr_kds; i++) {
			printf("\tname(dkey)=%s  ",
					(char *) sgl_keys[i].sg_iovs->iov_buf);
			for (j = 0; j < nr_iods; j++) {
				l = i*nr_iods+j;
				printf("%.*s(akey)=%s  ",
					 (int)    iods[i].iod_name.iov_buf_len,
					 (char *) iods[i].iod_name.iov_buf,
					 (char *) sgl_recx[l].sg_iovs->iov_buf);
			}
		}
	}
	printf("\n");

	free(kds);
	free(sgl_keys);
	free(iovs_keys);
	free(buf_keys);
	free(sgl_recx);
	free(iovs_recx);
	free(buf_recx);
}

int
main(int argc, char **argv)
{
	uuid_t			pool_uuid;
	uuid_t			co_uuid;
	daos_obj_id_t		oid;
	int			rc;
	daos_pipeline_t		pipeline;

	if (argc != 2) {
		fprintf(stderr, "args: pool\n");
		exit(1);
	}

	/** Initialize the local DAOS stack */
	rc = daos_init();
	ASSERT(rc == 0, "daos_init failed with %d", rc);

	/** Parse the pool information and connect to the pool */
	rc = uuid_parse(argv[1], pool_uuid);
	ASSERT(rc == 0, "Failed to parse 'Pool uuid': %s", argv[1]);

	/** Call connect */
	rc = daos_pool_connect(pool_uuid, NULL, DAOS_PC_RW, &poh, NULL, NULL);
	ASSERT(rc == 0, "pool connect failed with %d", rc);

	/*
	 * Create and open a container.
	 * Alternatively, one could create the container outside of this
	 * program using the daos utility: daos cont create --pool=puuid
	 * and pass the uuid to the app.
	 */
	rc = daos_cont_create(poh, &co_uuid, NULL, NULL);
	ASSERT(rc == 0, "container create failed with %d", rc);
	rc = daos_cont_open(poh, co_uuid, DAOS_COO_RW, &coh, NULL,
			    NULL);
	ASSERT(rc == 0, "container open failed with %d", rc);

	/** create/open object */
	oid.hi = 0;
	oid.lo = 4;
	daos_obj_generate_oid(coh, &oid, DAOS_OF_KV_FLAT, OC_SX, 0, 0);
	rc = daos_obj_open(coh, oid, DAOS_OO_RW, &oh, NULL);
	ASSERT(rc == 0, "Obj open failed with %d", rc);

	/** Insert some example records */
	insert_example_records();

	/** Get records filtering by "Owner == Benny" */
	build_filter_one(&pipeline);

	/** checking that the pipe is well constructed */
	rc = daos_pipeline_check(&pipeline);
	ASSERT(rc == 0, "Pipeline check failed with %d", rc);

	/** Running pipeline */
	run_pipeline(&pipeline);

	return 0;
}

