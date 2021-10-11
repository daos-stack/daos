/**
 * (C) Copyright 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(tests)

#include <sys/time.h>
#include <sys/resource.h>
#include <malloc.h>
#include <sys/mman.h>
#include <abt.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dml.h>
#include "vts_io.h"

#define DEFAULT_NUM_XSTREAMS 3
#define MIN_OPS (40000)
#define OFFSET	(1024 * 64)
#define BUF_SIZE (1024 * 128)

enum {
	NO_DSA_NO_CSUM,
	NO_DSA_CSUM,
	DSA_NO_CSUM,
	DSA_CSUM,
};

ABT_pool *          pools;
struct vos_test_ctx vtx;

static dml_path_t	path = DML_PATH_SW;
static struct daos_csummer	*csummer;
static bool		gen_csum;
static bool		use_dsa;
static daos_epoch_t	epoch = 1;
static daos_unit_oid_t	oid;
static uint64_t		dkey_val = 0;
static char		akey_val = 'a';
static daos_key_t	dkey = {
	.iov_buf = &dkey_val,
	.iov_len = sizeof(dkey_val),
	.iov_buf_len = sizeof(dkey_val),
};
static daos_key_t	akey = {
	.iov_buf = &akey_val,
	.iov_len = sizeof(akey_val),
	.iov_buf_len = sizeof(akey_val),
};

struct io_op {
	daos_epoch_t	epoch;
	daos_iod_t	iod;
	d_sg_list_t	sgl;
	daos_recx_t	recx;
	d_iov_t		sg_iov;
	struct dcs_iod_csums	*iod_csums;
	char		buf[BUF_SIZE];
};

static int	new_io;
static daos_epoch_t highest_io;

void submit_io(void *arg)
{
	int rank;
	int rc;
	struct io_op	*op = arg;

	ABT_xstream_self_rank(&rank);
	rc = vos_obj_update(vtx.tc_co_hdl, oid, op->epoch, 0, 0, &dkey, 1, &op->iod,
			    op->iod_csums, &op->sgl);
	new_io++;
	if (op->epoch > highest_io)
		highest_io = op->epoch;
	D_ASSERT(rc == 0);
}

void pin_thread(void *arg)
{
	pthread_t	thread;
	cpu_set_t	cpuset;
	uintptr_t	cpu = (uintptr_t)arg;
	int rc;

	CPU_ZERO(&cpuset);
	CPU_SET(cpu, &cpuset);
	thread = pthread_self();
	rc = pthread_setaffinity_np(thread, sizeof(cpuset), &cpuset);
	D_ASSERT(rc == 0);
}

static d_list_t head;
static d_list_t batch_jobs;
#define BATCH_SIZE 8

struct job_entry {
	d_list_t	link;
	d_iov_t		*iovs;
	void 		*batch_ptr;
	dml_job_t	job_ptr[0];
};

static int
init_batch_job(void)
{
	struct job_entry *job = NULL;
	uint32_t	size = 0;
	dml_status_t	status = dml_get_job_size(path, &size);
	if (status != DML_STATUS_OK)
		return -DER_NOMEM;

	job = malloc(sizeof(*job) + size + BATCH_SIZE * sizeof(job->iovs[0]));
	status = dml_init_job(path, &job->job_ptr[0]);
	if (status != DML_STATUS_OK) {
		free(job);
		return -DER_NOMEM;
	}

	job->iovs = (d_iov_t *)((char *)job + size);
	status = dml_get_batch_size(&job->job_ptr[0], BATCH_SIZE, &size);
	if (status != DML_STATUS_OK) {
		dml_finalize_job(&job->job_ptr[0]);
		free(job);
		return -DER_NOMEM;
	}

	job->batch_ptr = malloc(size);
	job->job_ptr->operation = DML_OP_BATCH;
	job->job_ptr->destination_first_ptr = job->batch_ptr;
	job->job_ptr->destination_length = size;

	d_list_add_tail(&job->link, &batch_jobs);
	return 0;
}

static void
init_jobs(void)
{
	int rc, i;

	D_INIT_LIST_HEAD(&head);
	D_INIT_LIST_HEAD(&batch_jobs);

	for (i = 0; i < 32; i++) {
		rc = init_batch_job();
		D_ASSERT(rc == 0);
	}
}

static void
finalize_jobs(void)
{
	struct job_entry	*job;

	while ((job = d_list_pop_entry(&batch_jobs, struct job_entry, link)) != NULL) {
		dml_finalize_job(&job->job_ptr[0]);
		free(job->batch_ptr);
		free(job);
	}
}

void
handle_dsa_batch_write(struct csum_recalc_args *args, char **dest, int index, int count)
{
	struct bio_sglist	*bsgl = args->cra_bsgl;
	struct job_entry	*job;
	dml_job_t		*job_ptr;
	dml_status_t		 status;
	int			 i;

	job = d_list_pop_entry(&batch_jobs, struct job_entry, link);
	D_ASSERT(job != NULL);

	args->cra_rc = 0;

	job_ptr = job->job_ptr;

	for (i = index; i < index + count; i++) {
		struct bio_iov	*biov = &bsgl->bs_iovs[i];
		int batch_idx = i - index;
		d_iov_t	 src;
		d_iov_t *iov = &job->iovs[batch_idx];
		iov->iov_buf = *dest;
		iov->iov_len = iov->iov_buf_len = src.iov_len = src.iov_buf_len = biov->bi_data_len;
		src.iov_buf = bio_addr2ptr(args->bio_ctx, biov->bi_addr);
		*dest += src.iov_len;

		status = dml_batch_set_mem_move_by_index(job_ptr, batch_idx, src.iov_buf, iov->iov_buf, iov->iov_len, 0u);
		if (status != DML_STATUS_OK)
			goto pop;
	}
	for (i = count; i < BATCH_SIZE; i++) {
		status = dml_batch_set_nop_by_index(job_ptr, i, 0u);
		if (status != DML_STATUS_OK)
			goto pop;
	}

	status = dml_submit_job(job_ptr);
	if (status == DML_STATUS_OK) {
		d_list_add_tail(&job->link, &head);
		return;
	}
pop:
	args->cra_rc = -DER_INVAL;
	d_list_add(&job->link, &batch_jobs);
}

void
handle_dsa_write(struct csum_recalc_args *args)
{
	struct bio_sglist	*bsgl = args->cra_bsgl;
	char *dest;
	int count, i;

	dest = bio_addr2ptr(args->bio_ctx, args->cra_ent_in->ei_addr);

	for (i = 0; i < bsgl->bs_nr; i += BATCH_SIZE) {
		count = MIN(BATCH_SIZE, bsgl->bs_nr - i);
		handle_dsa_batch_write(args, &dest, i, count);
		if (args->cra_rc != 0)
			break;
	}
}

void
handle_copy(struct csum_recalc_args *args)
{
}

void
handle_write(struct csum_recalc_args *args)
{
	if (use_dsa)
		handle_dsa_write(args);
	else
		handle_copy(args);
}

int
handle_write_csum(struct csum_recalc_args *args)
{
	return 0;
}

int
handle_csum(struct csum_recalc_args *args)
{
	return 0;

#if 0
	d_sg_list_t		 sgl;
	struct csum_recalc_args *args = recalc_args;
	struct bio_sglist	*bsgl = args->cra_bsgl;
	struct evt_entry_in	*ent_in = args->cra_ent_in;
	struct csum_recalc	*recalcs = args->cra_recalcs;
	struct daos_csummer	*csummer;
	struct dcs_csum_info	 csum_info = args->cra_ent_in->ei_csum;
	unsigned int		 buf_idx = 0;
	unsigned int		 add_idx = 0;
	unsigned int		 i,  add_offset = 0;
	int			 rc = 0;

	/* need at most prefix + buf + suffix in sgl */
	rc = d_sgl_init(&sgl, 3);
	if (rc) {
		args->cra_rc = rc;
		return;
	}
	daos_csummer_init_with_type(&csummer, csum_info.cs_type,
				    csum_info.cs_chunksize, 0);
	for (i = 0; i < args->cra_seg_cnt; i++) {
		bool		is_valid = false;
		unsigned int	this_buf_nr, this_buf_idx;


		/* Number of records in this input segment, include added
		 * segments.
		 */
		this_buf_nr = (bsgl->bs_iovs[i].bi_data_len +
			       recalcs[i].cr_prefix_len +
			       recalcs[i].cr_suffix_len) / ent_in->ei_inob;
		/* Sets up the SGL for the (verification) checksum calculation.
		 * Returns the offset of the next add-on (prefix/suffix)
		 * segment.
		 */
		add_offset = csum_agg_set_sgl(&sgl, bsgl, recalcs,
					      args->cra_buf, args->cra_buf_len,
					      args->cra_seg_cnt,
					      args->cra_seg_size, i, add_offset,
					      &buf_idx, &add_idx);
		D_ASSERT(recalcs[i].cr_log_ext.ex_hi -
			 recalcs[i].cr_log_ext.ex_lo + 1 ==
			 bsgl->bs_iovs[i].bi_data_len / ent_in->ei_inob);

		/* Determines number of checksum entries, and start index, for
		 * calculating verification checksum,
		 */
		this_buf_idx = calc_csum_params(&csum_info, &recalcs[i],
						recalcs[i].cr_prefix_len,
						recalcs[i].cr_suffix_len,
						ent_in->ei_inob);

		/* Ensure buffer is zero-ed. */
		memset(csum_info.cs_csum, 0, csum_info.cs_buf_len);

		/* Calculates the checksums for the input segment. */
		rc = daos_csummer_calc_one(csummer, &sgl, &csum_info,
					   ent_in->ei_inob, this_buf_nr,
					   this_buf_idx);
		if (rc)
			goto out;

		/* Verifies that calculated checksums match prior (input)
		 * checksums, for the appropriate range.
		 */
		is_valid = csum_agg_verify(&recalcs[i], &csum_info,
					   ent_in->ei_inob,
					   recalcs[i].cr_prefix_len);
		if (!is_valid) {
			rc = -DER_CSUM;
			goto out;
		}
	}
out:
	/* Eventual set okay, even with no offload (unit test). */
	D_FREE(sgl.sg_iovs);
	args->cra_rc = rc;
	return 0;
#endif
}

void
agg_csum_recalc(void *recalc_args)
{
	struct csum_recalc_args *args = recalc_args;
	int rc;

	if (!use_dsa) {
		ds_csum_agg_recalc(recalc_args);
		return;
	}

	if (args->is_write) {
		if (gen_csum)
			rc = handle_write_csum(args);
		else
			D_ASSERT(0);
	} else {
		rc = handle_csum(args);
	}

	args->cra_rc = rc;
}

int wait_ops(int rc)
{
	struct job_entry	*job;
	dml_status_t	status;

	while ((job = d_list_pop_entry(&head, struct job_entry, link)) != NULL) {
		for (;;) {
			status = dml_check_job(job->job_ptr);

			if (status == DML_STATUS_OK)
				break;

			if (status == DML_STATUS_JOB_CORRUPTED) {
				if (rc == 0)
					rc = -DER_INVAL;
				break;
			}

			/** Let the I/O go */
			ABT_thread_yield();
		}

		d_list_add_tail(&job->link, &batch_jobs);
	}

	return rc;
}

void
csum_recalc(void *args)
{
	struct csum_recalc_args *cs_args = args;
	ABT_pool	target_pool = pools[2];
	ABT_thread	thread;

	if (cs_args->is_write && !gen_csum) {
		handle_write(args);
		return;
	}

	ABT_eventual_create(0, &cs_args->csum_eventual);
	ABT_thread_create(target_pool, agg_csum_recalc, args, ABT_THREAD_ATTR_NULL, &thread);
	ABT_eventual_wait(cs_args->csum_eventual, NULL);
	ABT_eventual_free(&cs_args->csum_eventual);
}

struct agg_info {
	daos_epoch_range_t epr;
	uint64_t time_nsec;
	int rc;
};

void agg_thread(void *arg)
{
	struct agg_info *agg_info = arg;
	uint64_t	start, end;

	init_jobs();


	start = daos_get_ntime();
	agg_info->rc = vos_aggregate(vtx.tc_co_hdl, &agg_info->epr, csum_recalc, wait_ops, NULL,
				     NULL, true);
	end = daos_get_ntime();
	agg_info->time_nsec = end - start;

	finalize_jobs();
}

struct io_op *
allocate_ops(int op_count, bool csum)
{
	int i;
	uint64_t offset = OFFSET;
	struct io_op *ops;

	ops = malloc(sizeof(*ops) * op_count);

	for (i = 0; i < op_count; i++) {
		ops[i].epoch = epoch++;
		ops[i].iod.iod_name = akey;
		ops[i].iod.iod_type = DAOS_IOD_ARRAY;
		ops[i].iod.iod_size = 1;
		ops[i].iod.iod_recxs = &ops[i].recx;
		ops[i].iod.iod_nr = 1;
		ops[i].sgl.sg_nr_out = 0;
		ops[i].sgl.sg_nr = 1;
		ops[i].sgl.sg_iovs = &ops[i].sg_iov;
		memset(ops[i].buf, (i % 26) + 'A', sizeof(ops[i].buf));
		d_iov_set(&ops[i].sg_iov, ops[i].buf, sizeof(ops[i].buf));
		ops[i].recx.rx_nr = BUF_SIZE;
		ops[i].recx.rx_idx = offset;
		offset += BUF_SIZE;
		if (csum) {
			daos_csummer_calc_iods(csummer, &ops[i].sgl, &ops[i].iod, NULL, 1, false,
					       NULL, 0, &ops[i].iod_csums);
		}
	}

	return ops;
}

void
pin_threads(void)
{
	ABT_thread	 child;
	int		 i;

	pin_thread((void *)(uintptr_t)0);
	for (i = 1; i < DEFAULT_NUM_XSTREAMS; i++) {
		ABT_thread_create(pools[i], pin_thread, (void *)(uintptr_t)i, ABT_THREAD_ATTR_NULL, &child);
		ABT_thread_free(&child);
	}
}

void run_bench(int num_init, int num_ops)
{
	int         i;
	ABT_pool    target_pool = pools[1];
	ABT_thread *children    = malloc(sizeof(*children) * (num_ops + 1));
	struct agg_info	 agg_info = {0};
	struct io_op	*args;
	int		rc;
	uint64_t	start, end;
	double bw;

	oid = dts_unit_oid_gen(0, DAOS_OF_DKEY_UINT64, 0);

	if (gen_csum) {
		rc = daos_csummer_init_with_type(&csummer, HASH_TYPE_CRC32, 1 << 12, 0);
		D_ASSERT(rc == 0);
	}

	args = allocate_ops(num_ops, gen_csum);

	for (i = 0; i < num_init; i++) {
		ABT_thread_create(target_pool, submit_io, &args[i], ABT_THREAD_ATTR_NULL,
				  &children[i]);
	}

	for (i = 0; i < num_init; i++) {
		ABT_thread_free(&children[i]);
	}

	agg_info.epr.epr_lo = 0;
	agg_info.epr.epr_hi = highest_io;

	start = daos_get_ntime();
	ABT_thread_create(target_pool, agg_thread, &agg_info, ABT_THREAD_ATTR_NULL,
			  &children[num_ops]);

	for (i = num_init; i < num_ops; i++) {
		ABT_thread_create(target_pool, submit_io, &args[i], ABT_THREAD_ATTR_NULL,
				  &children[i]);
	}

	end = start;
	for (i = num_init; i < num_ops + 1; i++) {
		if (i == num_ops)
			end = daos_get_ntime();
		ABT_thread_free(&children[i]);
	}

	bw = ((double)num_init * BUF_SIZE * NSEC_PER_SEC) / ((1024 * 1024) * agg_info.time_nsec);
	printf("agg_time = %10.3lf ms, BW %10.5lf MB/s\n",
	       (double)agg_info.time_nsec / NSEC_PER_MSEC, bw);
	bw = ((double)(num_ops - num_init) * BUF_SIZE * NSEC_PER_SEC) /
		(1024 * 1024 * (end - start));
	printf("io_time  = %10.3lf ms, BW %10.5lf MB/s\n", (double)(end - start) / NSEC_PER_MSEC,
	       bw);

	if (gen_csum) {
		daos_csummer_destroy(&csummer);
	}
}

void
print_usage(const char *name)
{
	printf("Usage: %s [opts]\n", name);
	printf("\t-h            Print help and exit\n");
	printf("\t-o count      Set number of operations to perform\n");
	printf("\t-t d|D|n|N    Aggregation with(d) or without DSA(d), capitalize for csum, default is 'n'\n");
	printf("\t-r ratio      Integer between 1 and 100 indicating percentage of total writes to aggregate\n");
	printf("\t-p s|h	s is software DSA, h is hardware DSA\n");
}

/* Workaround for CART-890 */
static void
mem_pin_workaround(void)
{
        struct rlimit   rlim;
        int             rc = 0;

        /* Note: mallopt() returns 1 on success */
        /* Prevent malloc from releasing memory via sbrk syscall */
        rc = mallopt(M_TRIM_THRESHOLD, -1);
        if (rc != 1)
                D_WARN("Failed to disable malloc trim: %d\n", errno);

        /* Disable fastbins; this option is not available on all systems */
        rc = mallopt(M_MXFAST, 0); 
        if (rc != 1)
                D_WARN("Failed to disable malloc fastbins: %d (%s)\n",
                       errno, strerror(errno));

        rc = getrlimit(RLIMIT_MEMLOCK, &rlim);
        if (rc != 0) {
                D_WARN("getrlimit() failed; errno=%d (%s)\n",
                       errno, strerror(errno));
                goto exit;
        }   

        if (rlim.rlim_cur == RLIM_INFINITY &&
            rlim.rlim_max == RLIM_INFINITY) {
                D_INFO("Infinite rlimit detected; performing mlockall()\n");

                /* Lock all pages */
                rc = mlockall(MCL_CURRENT | MCL_FUTURE);
                if (rc)
                        D_WARN("Failed to mlockall(); errno=%d (%s)\n",
                               errno, strerror(errno));

        } else {
                D_INFO("mlockall() skipped\n");
        }   

        D_DEBUG(DB_ALL, "Memory pinning workaround enabled\n");
exit:
        return;
}


int main(int argc, char **argv)
{
	int i, rc, num_ops = 0, num_init, ratio = 22;
	/* Read arguments. */
	int num_xstreams = DEFAULT_NUM_XSTREAMS;

	while (1) {
		int opt = getopt(argc, argv, "hr:o:t:p:");
		if (opt == -1)
			break;
		switch (opt) {
		case 'p':
			switch (optarg[0]) {
			case 's':
				path = DML_PATH_SW;
				break;
			case 'h':
				path = DML_PATH_HW;
				break;
			default:
				print_usage(argv[0]);
				return -1;
			}
			break;
		case 'o':
			num_ops = atoi(optarg);
			break;
		case 'r':
			ratio = atoi(optarg);
			break;
		case 't':
			switch (optarg[0]) {
			case 'D':
				gen_csum = true;
				/* fallthrough */
			case 'd':
				use_dsa = true;
				break;
			case 'N':
				gen_csum = true;
				/* fallthrough */
			case 'n':
				break;
			default:
				print_usage(argv[0]);
				return -1;
			}
			break;
		case 'h':
		default:
			print_usage(argv[0]);
			return -1;
		}
	}

	if (ratio < 0)
		ratio = 5;
	else if (ratio > 90)
		ratio = 90;

	if (num_ops < MIN_OPS)
		num_ops = MIN_OPS;

	num_init = num_ops * ratio / 100;

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc) {
		print_error("Error initializing debug system\n");
		return rc;
	}
	mem_pin_workaround();

	rc = vos_self_init("/mnt/daos");
	if (rc) {
		print_error("Error initializing VOS instance\n");
		goto exit_0;
	}
	vts_ctx_init(&vtx, VPOOL_10G);

	/* Allocate memory. */
	ABT_xstream *xstreams = (ABT_xstream *)malloc(sizeof(ABT_xstream) * num_xstreams);
	pools                 = (ABT_pool *)malloc(sizeof(ABT_pool) * num_xstreams);
	ABT_sched *scheds     = (ABT_sched *)malloc(sizeof(ABT_sched) * num_xstreams);

	/* Initialize Argobots. */
	ABT_init(argc, argv);

	/* Create pools. */
	for (i = 0; i < num_xstreams; i++) {
		ABT_pool_create_basic(ABT_POOL_FIFO, ABT_POOL_ACCESS_MPMC, ABT_TRUE, &pools[i]);
	}

	/* Create schedulers. */
	for (i = 0; i < num_xstreams; i++) {
		ABT_sched_create_basic(ABT_SCHED_DEFAULT, 1, &pools[i], ABT_SCHED_CONFIG_NULL,
				       &scheds[i]);
	}

	/* Set up a primary execution stream. */
	ABT_xstream_self(&xstreams[0]);
	ABT_xstream_set_main_sched(xstreams[0], scheds[0]);

	/* Create secondary execution streams. */
	for (i = 1; i < num_xstreams; i++) {
		ABT_xstream_create(scheds[i], &xstreams[i]);
	}

	pin_threads();
	run_bench(num_init, num_ops);

	/* Join secondary execution streams. */
	for (i = 1; i < num_xstreams; i++) {
		ABT_xstream_join(xstreams[i]);
		ABT_xstream_free(&xstreams[i]);
	}

	/* Finalize Argobots. */
	ABT_finalize();

	/* Free allocated memory. */
	free(xstreams);
	free(pools);
	free(scheds);

	vts_ctx_fini(&vtx);
	vos_self_fini();
exit_0:
	daos_debug_fini();

	return 0;
}
