/**
 * (C) Copyright 2016-2020 Intel Corporation.
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
/**
 * Simple sliced 1D array example
 *
 * We consider a 1D non-sparse array of ARRAY_SIZE elements. Each element is
 * a fixed-size 64-bit integer and has an index ranging from 0 to ARRAY_SIZE-1.
 * The content of this array is distributed over SHARD_NR shards. Each array
 * shard is associated with a dkey set to the shard ID. A single array (akey
 * set to "data") is used in this example to store the shard content.
 * The array is partitioned into fixed-size (i.e. SLICE_SIZE) slices of
 * contiguous elements which are stored on shards in a round-robin fashion.
 *
 * Each iteration completely overwrites the array by setting each element
 * to the epoch number associated with the iteration. Each MPI task writes a
 * different set of slices at each iteration and has a limited number of
 * I/O requests in flight. Once a task is done with an iteration, it notifies
 * the transaction manager (i.e. rank 0) and moves on to the next iteration by
 * bumping the epoch number. The transaction manager is responsible for
 * flushing and committing the epoch once all tasks have reported completion.
 */
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include <daos/tests_lib.h>
#include <daos.h>
#include "suite/daos_test.h"
#include <mpi.h>

/** local task information */
int			 rank = -1;
int			 rankn = -1;
char			 node[128] = "unknown";

/** Name of the process set associated with the DAOS server */
#define	DSS_PSETID	 "daos_tier0"

/** Event queue */
daos_handle_t	eq;

/** Pool information */
uuid_t			 pool_uuid;	/* only used on rank 0 */
d_rank_list_t		 svcl;		/* only used on rank 0 */
daos_handle_t		 poh;		/* shared pool handle */

/** Container information */
uuid_t			 co_uuid;	/* only used on rank 0 */
daos_handle_t		 coh;		/* shared container handle */
daos_epoch_t		 epoch;		/* epoch in-use */

/**
 * Object information
 * DAOS uses the high 32-bit of the object ID, the rest is supposed to be
 * unique, just set the low bits to 1 in this example
 */
daos_obj_id_t		 oid = {
	.lo = 0x1,
};
/** class identifier */
daos_oclass_id_t	 cid = 0x1;/* class identifier */

/**
 * Array parameters
 * Each task overwrites a different section of the array at every iteration.
 * An epoch number is associated with each iteration. One task can have at
 * most MAX_IOREQS I/O requests in flight and then needs to wait for completion
 * of an request in flight before sending a new one.
 * The actual data written in the array is the epoch number.
 */
#define TEST_ARRAY_SIZE	1000000000 /* 1B entries * 8-byte entry bits =  8GB */
#define SLICE_SIZE	10000	   /* size of an array slice, 10K entries */
#define SHARD_NR	1000	   /* static number of array shards, 1K */
#define ITER_NR		10	   /* number of global iteration */
#define KEY_LEN		10	   /* enough to write the shard ID */
#define	MAX_IOREQS	10	   /* number of concurrent i/o reqs in flight */

/** an i/o request in flight */
struct ioreq {
	char		dstr[KEY_LEN];
	daos_key_t	dkey;

	daos_recx_t	recx;
	daos_iod_t	iod;

	d_iov_t	iov;
	d_sg_list_t	sg;

	daos_event_t	ev;
};

/** a single akey is used in this example and is set to the string "data" */
char astr[] = "data";

/** data buffer */
uint64_t data[SLICE_SIZE];

#define FAIL(fmt, ...)						\
do {								\
	fprintf(stderr, "Process %d(%s): " fmt " aborting\n",	\
		rank, node, ## __VA_ARGS__);			\
	MPI_Abort(MPI_COMM_WORLD, 1);				\
} while (0)

#define	ASSERT(cond, ...)					\
do {								\
	if (!(cond))						\
		FAIL(__VA_ARGS__);				\
} while (0)

void
pool_create(void)
{
	int	rc;

	/**
	 * allocate list of service nodes, returned as output parameter of
	 * dmg_pool_create() and used to connect
	 */

	/** create pool over all the storage targets */
	svcl.rl_nr = 3;
	D_ALLOC_ARRAY(svcl.rl_ranks, svcl.rl_nr);
	ASSERT(svcl.rl_ranks);
	rc = dmg_pool_create(NULL /* config file */,
			     geteuid() /* user owner */,
			     getegid() /* group owner */,
			     DSS_PSETID /* daos server process set ID */,
			     NULL /* list of targets, NULL = all */,
			     10ULL << 30 /* target SCM size, 10G */,
			     40ULL << 30 /* target NVMe size, 40G */,
			     NULL /* pool props */,
			     &svcl /* pool service nodes, used for connect */,
			     pool_uuid /* the uuid of the pool created */);
	ASSERT(rc == 0, "pool create failed with %d", rc);
}

void
pool_destroy(void)
{
	int	rc;

	/** destroy the pool created in pool_create */
	rc = dmg_pool_destroy(NULL, pool_uuid, DSS_PSETID, 1 /* force */);
	ASSERT(rc == 0, "pool destroy failed with %d", rc);
	D_FREE(svcl.rl_ranks);
}

static inline void
ioreqs_init(struct ioreq *reqs) {
	int rc;
	int j;

	for (j = 0; j < MAX_IOREQS; j++) {
		struct ioreq	*req = &reqs[j];

		/** initialize event */
		rc = daos_event_init(&req->ev, eq, NULL);
		ASSERT(rc == 0, "event init failed with %d", rc);

		/** initialize dkey */
		req->dkey = (daos_key_t) {
			.iov_buf	= &req->dstr,
			.iov_buf_len	= KEY_LEN,
		};

		/** initialize i/o descriptor */
		req->iod.iod_name = (daos_key_t) {
			.iov_buf	= astr,
			.iov_buf_len	= strlen(astr),
			.iov_len	= strlen(astr),
		};
		req->iod.iod_nr	= 1;
		req->iod.iod_size = sizeof(uint64_t);
		req->recx = (daos_recx_t) {
			.rx_nr		= SLICE_SIZE,

		};
		req->iod.iod_recxs	= &req->recx;

		/** initialize scatter/gather */
		req->iov = (d_iov_t) {
			.iov_buf	= &data,
			.iov_buf_len	= SLICE_SIZE * sizeof(data[0]),
			.iov_len	= SLICE_SIZE * sizeof(data[0]),
		};
		req->sg.sg_nr		= 1;
		req->sg.sg_iovs		= &req->iov;
	}
}

void
array(void)
{
	daos_handle_t	 oh;
	struct ioreq	*reqs;
	int		 rc;
	int		 iter;
	int		 k;

	/** allocate and initialize I/O requests */
	D_ALLOC_ARRAY(reqs, MAX_IOREQS);
	ASSERT(reqs != NULL, "malloc of reqs failed");
	ioreqs_init(reqs);

	/** open DAOS object */
	rc = daos_obj_open(coh, oid, DAOS_OO_RW, &oh, NULL);
	ASSERT(rc == 0, "object open failed with %d", rc);

	/** Transactional overwrite of the array at each iteration */
	for (iter = 0; iter < ITER_NR; iter++) {
		MPI_Request	 request;
		daos_event_t	*evp[MAX_IOREQS];
		uint64_t	 sid; /* slice ID */
		int		 submitted = 0;
		struct ioreq	*req = &reqs[0];

		/** store very basic array data */
		for (k = 0; k < SLICE_SIZE; k++)
			data[k] = epoch;

		/**
		 * For testing purpose, each thread starts with a different
		 * slice at each epoch and then skips the next rankn - 1
		 * slices (rank 0 is the transaction manager and does not
		 * perform any I/O operations).
		 */
		for  (sid = (rank - 1 + epoch) % (rankn - 1);
		      sid < TEST_ARRAY_SIZE / SLICE_SIZE;
		      sid += rankn - 1) {

			/**
			 * dkey is set to the shard ID which is equal
			 * to the slice ID % SHARD_NR
			 */
			rc = snprintf(req->dstr, KEY_LEN, "%lud",
				      sid % SHARD_NR);
			ASSERT(rc < KEY_LEN, "increase KEY_LEN");
			req->dkey.iov_len = strlen(req->dstr);

			/**
			 * Index inside the array to write this slice.
			 * Two options here:
			 * - use the logical array index, which means that there
			 *   will be a gap in the array index between each
			 *   slice.
			 * - write all slices in contiguous indexes inside the
			 *   array.
			 * For simplicity, the former approach is implemented
			 * in this example which means that the index inside the
			 * array/shard matches the logical array index.
			 */
			req->recx.rx_idx = sid * SLICE_SIZE;

			/** submit I/O operation */
			rc = daos_obj_update(oh, DAOS_TX_NONE, 0, &req->dkey, 1,
					    &req->iod, &req->sg,
					    &req->ev);
			ASSERT(rc == 0, "object update failed with %d", rc);

			submitted++;
			if (submitted < MAX_IOREQS) {
				/** haven't reached max request in flight yet */
				req++;
			} else {
				/**
				 * max request request in flight reached, wait
				 * for one i/o to complete to reuse the slot
				 */
				rc = daos_eq_poll(eq, 1, DAOS_EQ_WAIT, 1, evp);
				ASSERT(rc == 1, "eq poll failed with %d", rc);
				/** check for any I/O operation error */
				ASSERT(evp[0]->ev_error == 0,
				       "I/O operation failed with %d",
				       evp[0]->ev_error);

				submitted--;
				req = container_of(evp[0], struct ioreq, ev);
			}
		}

		/** poll all remaining I/O requests */
		rc = daos_eq_poll(eq, 1, DAOS_EQ_WAIT, submitted, evp);
		ASSERT(rc == submitted, "eq poll failed with %d", rc);
		/** check for any I/O error */
		for (k = 0; k < submitted; k++)
			ASSERT(evp[k]->ev_error == 0,
			       "I/O operation failed with %d",
			       evp[k]->ev_error);

		/**
		 * notify rank 0 that we are done with this epoch
		 * tried first with MPI_IBarrier() with no luck, rewrote with
		 * MPI_Isend/Irecv.
		 */
		rc = MPI_Isend(&epoch, 1, MPI_UINT64_T, 0, epoch,
			       MPI_COMM_WORLD, &request);
		ASSERT(rc == MPI_SUCCESS, "ISend failed");

		MPI_Wait(&request, MPI_STATUS_IGNORE);

		/**
		 * rank 0 will flush & commit once everyone is done.
		 * meanwhile, move on to the next epoch
		 */
		epoch++;
	}

	/** close DAOS object */
	rc = daos_obj_close(oh, NULL);
	ASSERT(rc == 0, "object cloase failed with %d", rc);

	/** release events */
	for (k = 0; k < MAX_IOREQS; k++) {
		rc = daos_event_fini(&reqs[k].ev);
		ASSERT(rc == 0, "event fini failed with %d", rc);
	}

	D_FREE(reqs);
}

/** states of the epoch state machine executed by the transaction manager */
typedef enum {
	EP_NONE,     /* nothing interesting yet */
	EP_WR_DONE,  /* all tasks reported completion, next step is flush */
	EP_FLUSHED,  /* epoch flushed, next step is commit */
	EP_COMMITTED,/* epoch committed, no further work required */
} ep_phase_t;

/** per-epoch information */
struct ep_state {
	int		ref;   /* #tasks that already reported completion */
	ep_phase_t	state; /* epoch state, see above */
};

/** Main routine of the transaction manager */
void
committer()
{
	struct ep_state	ep_track[ITER_NR];
	daos_epoch_t	ep_start;
	daos_epoch_t	ep_rcv;
	daos_event_t	ev;
	MPI_Request	request;
	int		rc;
	int		j;

	rc = daos_event_init(&ev, eq, NULL);
	ASSERT(rc == 0, "event init failed with %d", rc);

	ep_start = epoch;
	for (j = 0; j < ITER_NR; j++) {
		ep_track[j].ref = 0;
		ep_track[j].state = EP_NONE;
	}

	/** post an initial buffer */
	rc = MPI_Irecv(&ep_rcv, 1, MPI_UINT64_T, MPI_ANY_SOURCE, MPI_ANY_TAG,
		       MPI_COMM_WORLD, &request);
	ASSERT(rc == MPI_SUCCESS, "Irecv failed");

	for (;;) {
		MPI_Status	 status;
		daos_event_t	*evp;
		int		 daos_comp = 0;
		int		 mpi_comp = 0;

		/** poll for incoming message or commit/flush completion */
		do {
			daos_comp = daos_eq_poll(eq, 0, DAOS_EQ_NOWAIT, 1,
						 &evp);
			MPI_Test(&request, &mpi_comp, &status);
		} while (mpi_comp == 0 && daos_comp == 0);

		/** message received */
		if (mpi_comp) {
			int count;

			MPI_Get_count(&status, MPI_UINT64_T, &count);
			ASSERT(count == 1, "Irecv test failed");

			/** bump ref count */
			ep_track[ep_rcv - ep_start].ref++;

			/** post a new buffer */
			rc = MPI_Irecv(&ep_rcv, 1, MPI_UINT64_T, MPI_ANY_SOURCE,
				       MPI_ANY_TAG, MPI_COMM_WORLD, &request);
			ASSERT(rc == MPI_SUCCESS, "Irecv failed");
		}

		/** DAOS flush or commit completed */
		if (daos_comp) {
			ep_phase_t *state = &ep_track[epoch - ep_start].state;

			ASSERT(daos_comp == 1, "eq pool failed with %d",
			       daos_comp);
			ASSERT(&ev == evp, "events mismatch");
			ASSERT(ep_track[epoch - ep_start].ref == rankn - 1,
			       "event completion while some tasks haven't "
			       "reported epoch completion yet");

			if (*state == EP_WR_DONE) {
				/** flush completed */
				ASSERT(ev.ev_error == 0,
				       "flush failed with %d", ev.ev_error);
				*state = EP_FLUSHED;
			} else if (*state == EP_FLUSHED) {
				/** commit completed */
				ASSERT(ev.ev_error == 0,
				       "commit failed with %d", ev.ev_error);
				*state = EP_COMMITTED;
				/** successful commit, bump epoch */
				epoch++;

				if (epoch - ep_start == ITER_NR)
					/**
					 * all epochs are committed,
					 * we are done
					 */
					break;
			} else {
				ASSERT(0, "invalid state %d", *state);
			}
		}

		/** everybody is done with this epoch */
		if (ep_track[epoch - ep_start].ref == rankn - 1) {
			ep_phase_t *state = &ep_track[epoch - ep_start].state;

			ASSERT(*state == EP_NONE, "invalid epoch state");
			*state = EP_WR_DONE;
		}
	}

	/** we posted one extra buffer, let's cancel it */
	MPI_Cancel(&request);

	rc = daos_event_fini(&ev);
	ASSERT(rc == 0, "event fini failed with %d", rc);
}

int
main(int argc, char **argv)
{
	int	rc;

	rc = gethostname(node, sizeof(node));
	ASSERT(rc == 0, "buffer for hostname too small");

	rc = MPI_Init(&argc, &argv);
	ASSERT(rc == MPI_SUCCESS, "MPI_Init failed with %d", rc);

	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &rankn);

	/** initialize the local DAOS stack */
	rc = daos_init();
	ASSERT(rc == 0, "daos_init failed with %d", rc);

	/** create event queue */
	rc = daos_eq_create(&eq);
	ASSERT(rc == 0, "eq create failed with %d", rc);

	if (rank == 0) {
		/** create a test pool and container for this test */
		pool_create();

		/** connect to the just created DAOS pool */
		rc = daos_pool_connect(pool_uuid, DSS_PSETID, &svcl,
				       DAOS_PC_EX /* exclusive access */,
				       &poh /* returned pool handle */,
				       NULL /* returned pool info */,
				       NULL /* event */);
		ASSERT(rc == 0, "pool connect failed with %d", rc);
	}

	/** share pool handle with peer tasks */
	handle_share(&poh, HANDLE_POOL, rank, poh, 1);

	if (rank == 0) {
		/** generate uuid for container */
		uuid_generate(co_uuid);

		/** create container */
		rc = daos_cont_create(poh, co_uuid, NULL /* properties */,
				      NULL /* event */);
		ASSERT(rc == 0, "container create failed with %d", rc);

		/** open container */
		rc = daos_cont_open(poh, co_uuid, DAOS_COO_RW, &coh, NULL,
				    NULL);
		ASSERT(rc == 0, "container open failed with %d", rc);
	}

	/** share container handle with peer tasks */
	handle_share(&coh, HANDLE_CO, rank, poh, 1);

	/** generate objid */
	daos_obj_generate_id(&oid, 0, cid, 0);

	if (rank == 0) {
		struct daos_oclass_attr	cattr = {
			.ca_schema		= DAOS_OS_STRIPED,
			.ca_resil_degree	= 0 /* TBD */,
			.ca_resil		= DAOS_RES_REPL,
			.ca_grp_nr		= 4,
			.u.rp			= {
				.r_proto	= 0 /* TBD */,
				.r_num		= 2 /* TBD */,
			},
		};

		/** register a default object class */
		rc = daos_obj_register_class(coh, cid, &cattr, NULL);
		ASSERT(rc == 0, "class register failed with %d", rc);

	}

	/** broadcast current LHE to all peers */
	rc = MPI_Bcast(&epoch, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);
	ASSERT(rc == MPI_SUCCESS, "LHE broadcast failed with %d", rc);

	/** start real work */
	if (rank == 0)
		/** rank 0 is the transaction manager */
		committer();
	else
		/** the other tasks write the array */
		array();

	/** close container */
	daos_cont_close(coh, NULL);

	/** disconnect from pool & destroy it */
	daos_pool_disconnect(poh, NULL);
	if (rank == 0)
		/** free allocated storage */
		pool_destroy();

	/** destroy event queue */
	rc = daos_eq_destroy(eq, 0);
	ASSERT(rc == 0, "eq destroy failed with %d", rc);

	/** shutdown the local DAOS stack */
	rc = daos_fini();
	ASSERT(rc == 0, "daos_fini failed with %d", rc);

	MPI_Finalize();
	return rc;
}
