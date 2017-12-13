#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>

#define DDSUBSYS       DDFAC(tests)

#include <mpi.h>
#include <daos.h>
#include <daos/common.h>
#include <daos/tests_lib.h>
#include <daos_srv/vos.h>
#include <daos_test.h>

/* unused object class to identify VOS (storage only) test mode */
#define DAOS_OC_RAW	 0xBEEF

int			mpi_rank;
int			mpi_size;

/* Test class, can be:
 *	vos  : pure storage
 *	ehco : pure network
 *	daos : full stack
 */
int			 ts_class = DAOS_OC_RAW;

char			*ts_pmem_file	= "/mnt/daos/vos_perf.pmem";
daos_size_t		 ts_pool_size	= (2ULL << 30);

unsigned int		 ts_obj_p_cont	= 1;	/* # objects per container */
unsigned int		 ts_dkey_p_obj	= 1;	/* # dkeys per object */
unsigned int		 ts_akey_p_dkey	= 100;	/* # akeys per dkey */
unsigned int		 ts_recx_p_akey	= 1000;	/* # recxs per akey */
/* value type: single or array */
bool			 ts_single	= true;
/* always overwrite value of an akey */
bool			 ts_overwrite;
/* use zero-copy API for VOS, ignored for "echo" or "daos" */
bool			 ts_zero_copy;
uuid_t			 ts_pool;		/* pool uuid */
uuid_t			 ts_cont;		/* container uuid */
uuid_t			 ts_cookie;		/* update cookie for VOS */
daos_handle_t		 ts_poh;		/* pool open handle */
daos_handle_t		 ts_coh;		/* container open handle */
daos_handle_t		 ts_eqh;		/* EQ open handle */
daos_handle_t		 ts_oh;			/* object open handle */
daos_obj_id_t		 ts_oid;		/* object ID */
daos_unit_oid_t		 ts_uoid;		/* object shard ID (for VOS) */
d_rank_list_t		 ts_svc;		/* pool service */
unsigned int		 ts_val_size	= 32;	/* default value size */

#define TS_KEY_LEN	32

/* I/O credit, the utility can only issue \a ts_credits_avail concurrent I/Os,
 * each credit can carry all parameters for the asynchronous I/O call.
 */
struct ts_io_credit {
	char		*tc_vbuf;		/* value buffer address */
	char		 tc_dbuf[TS_KEY_LEN];	/* dkey buffer */
	char		 tc_abuf[TS_KEY_LEN];	/* akey buffer */
	daos_key_t	 tc_dkey;		/* dkey iov */
	daos_iov_t	 tc_val;		/* value iov */
	daos_sg_list_t	 tc_sgl;		/* sgl for the value iov */
	daos_iod_t	 tc_iod;		/* I/O descriptor */
	daos_recx_t	 tc_recx;		/* recx for the I/O */
	daos_event_t	 tc_ev;			/* daos event for I/O */
	/* points to \a tc_ev in async mode, otherwise it's NULL */
	daos_event_t	*tc_evp;
};

#define TS_CREDITS_MAX	 64
/* # available credits */
int			 ts_credits_avail = -1;
/* # credits occupied by inflight I/Os */
int			 ts_credits_inuse;
/* buffer for all credits */
struct ts_io_credit	 ts_credits_buf[TS_CREDITS_MAX];
/* array of avaiable credits */
struct ts_io_credit	*ts_credits[TS_CREDITS_MAX];

/**
 * examines if there is available credit freed by completed I/O, it will wait
 * until all credits are freed if @drain is true.
 */
static int
ts_credit_poll(bool drain)
{
	daos_event_t	*evs[TS_CREDITS_MAX];
	int		 i;
	int		 rc;

	if (ts_credits_avail < 0 || /* synchronous mode */
	    ts_credits_inuse == 0)  /* nothing inflight */
		return 0;

	while (1) {
		rc = daos_eq_poll(ts_eqh, 0, DAOS_EQ_WAIT, TS_CREDITS_MAX, evs);
		if (rc < 0) {
			fprintf(stderr, "failed to pool event: %d\n", rc);
			return rc;
		}

		for (i = 0; i < rc; i++) {
			int err = evs[i]->ev_error;

			if (err != 0) {
				fprintf(stderr, "failed op: %d\n", err);
				return err;
			}
			ts_credits[ts_credits_avail] =
			   container_of(evs[i], struct ts_io_credit, tc_ev);

			ts_credits_inuse--;
			ts_credits_avail++;
		}

		if (ts_credits_avail == 0)
			continue;

		if (ts_credits_inuse != 0 && drain)
			continue;

		return 0;
	}
}

/** try to obtain a free credit */
static struct ts_io_credit *
ts_credit_take(void)
{
	int	 rc;

	if (ts_credits_avail < 0) /* synchronous mode */
		return &ts_credits_buf[0];

	while (1) {
		if (ts_credits_avail > 0) { /* yes there is free credit */
			ts_credits_avail--;
			ts_credits_inuse++;
			return ts_credits[ts_credits_avail];
		}

		rc = ts_credit_poll(false);
		if (rc)
			return NULL;
	}
}

static int
ts_vos_update(struct ts_io_credit *cred, daos_epoch_t epoch)
{
	int	rc;

	if (!ts_zero_copy) {
		rc = vos_obj_update(ts_coh, ts_uoid, epoch, ts_cookie, 0,
				    &cred->tc_dkey, 1, &cred->tc_iod,
				    &cred->tc_sgl);
		if (rc)
			return -1;

	} else { /* zero-copy */
		daos_sg_list_t	*sgl;
		daos_handle_t	 ioh;

		rc = vos_obj_zc_update_begin(ts_coh, ts_uoid, epoch,
					     &cred->tc_dkey, 1,
					     &cred->tc_iod, &ioh);
		if (rc)
			return rc;

		rc = vos_obj_zc_sgl_at(ioh, 0, &sgl);
		if (rc)
			return rc;

		D__ASSERT(cred->tc_sgl.sg_nr.num == 1);
		D__ASSERT(sgl->sg_nr.num_out == 1);

		memcpy(sgl->sg_iovs[0].iov_buf,
		       cred->tc_sgl.sg_iovs[0].iov_buf,
		       cred->tc_sgl.sg_iovs[0].iov_len);

		rc = vos_obj_zc_update_end(ioh, ts_cookie, 0,
					   &cred->tc_dkey, 1,
					   &cred->tc_iod, 0);
		if (rc)
			return rc;
	}
	return 0;
}

static int
ts_daos_update(struct ts_io_credit *cred, daos_epoch_t epoch)
{
	int	rc;

	rc = daos_obj_update(ts_oh, epoch, &cred->tc_dkey, 1,
			     &cred->tc_iod, &cred->tc_sgl, cred->tc_evp);
	return rc;
}

static int
ts_key_insert(void)
{
	int		*indices;
	char		 dkey_buf[TS_KEY_LEN];
	char		 akey_buf[TS_KEY_LEN];
	int		 i;
	int		 j;
	int		 rc = 0;
	daos_epoch_t	 epoch = 0;

	indices = dts_rand_iarr_alloc(ts_recx_p_akey, 0);
	D__ASSERT(indices != NULL);

	dts_key_gen(dkey_buf, TS_KEY_LEN, "blade");

	for (i = 0; i < ts_akey_p_dkey; i++) {

		dts_key_gen(akey_buf, TS_KEY_LEN, "walker");

		for (j = 0; j < ts_recx_p_akey; j++) {
			struct ts_io_credit *cred;
			daos_iod_t	  *iod;
			daos_sg_list_t	  *sgl;
			daos_recx_t	  *recx;

			cred = ts_credit_take();
			if (!cred) {
				fprintf(stderr, "test failed\n");
				return -1;
			}

			iod  = &cred->tc_iod;
			sgl  = &cred->tc_sgl;
			recx = &cred->tc_recx;

			memset(iod, 0, sizeof(*iod));
			memset(sgl, 0, sizeof(*sgl));
			memset(recx, 0, sizeof(*recx));

			/* setup dkey */
			memcpy(cred->tc_dbuf, dkey_buf, strlen(dkey_buf));
			daos_iov_set(&cred->tc_dkey, cred->tc_dbuf,
				     strlen(cred->tc_dbuf));

			/* setup I/O descriptor */
			memcpy(cred->tc_abuf, akey_buf, strlen(akey_buf));
			daos_iov_set(&iod->iod_name, cred->tc_abuf,
				     strlen(cred->tc_abuf));
			if (ts_single) {
				iod->iod_type = DAOS_IOD_SINGLE;
				iod->iod_size = ts_val_size;
			} else {
				iod->iod_type = DAOS_IOD_ARRAY;
				iod->iod_size = 1;
			}
			if (ts_single) {
				recx->rx_nr = 1;
			} else {
				recx->rx_nr  = ts_val_size;
				recx->rx_idx = ts_overwrite ?
					       0 : indices[j] * ts_val_size;
			}
			iod->iod_nr    = 1;
			iod->iod_recxs = recx;

			/* initialize value buffer and setup sgl */
			cred->tc_vbuf[0] = 'A' + j % 26;
			cred->tc_vbuf[1] = 'a' + j % 26;
			cred->tc_vbuf[2] = cred->tc_vbuf[ts_val_size - 1] = 0;

			daos_iov_set(&cred->tc_val, cred->tc_vbuf, ts_val_size);
			sgl->sg_iovs = &cred->tc_val;
			sgl->sg_nr.num = 1;

			/* overwrite can replace orignal data and reduce space
			 * consumption.
			 */
			if (!ts_overwrite)
				epoch++;

			if (ts_class == DAOS_OC_RAW)
				rc = ts_vos_update(cred, epoch);
			else
				rc = ts_daos_update(cred, epoch);

			if (rc != 0) {
				fprintf(stderr, "Update failed: %d\n", rc);
				D__GOTO(failed, rc);
			}
		}
	}
failed:
	free(indices);
	return rc;
}

static int
ts_write_perf(void)
{
	int	i;
	int	j;
	int	rc;

	for (i = 0; i < ts_obj_p_cont; i++) {
		ts_oid = dts_oid_gen(ts_class, mpi_rank);

		for (j = 0; j < ts_dkey_p_obj; j++) {
			if (ts_class != DAOS_OC_RAW) {
				rc = daos_obj_open(ts_coh, ts_oid, 1,
						   DAOS_OO_RW, &ts_oh, NULL);
				if (rc) {
					fprintf(stderr, "object open failed\n");
					return -1;
				}
			} else {
				memset(&ts_uoid, 0, sizeof(ts_uoid));
				ts_uoid.id_pub = ts_oid;
			}

			rc = ts_key_insert();
			if (rc)
				return rc;

			if (ts_class != DAOS_OC_RAW)
				daos_obj_close(ts_oh, NULL);
		}
	}

	rc = ts_credit_poll(true);
	return rc;
}

static int
ts_vos_prepare(void)
{
	int	fd;
	int	rc;

	fprintf(stdout, "Setup VOS\n");

	rc = vos_init();
	if (rc) {
		fprintf(stderr, "Failed to initialize VOS\n");
		return rc;
	}

	fd = open(ts_pmem_file, O_CREAT | O_TRUNC | O_RDWR, 0666);
	if (fd < 0) {
		fprintf(stderr, "Failed to open file\n");
		return -1;
	}

	rc = posix_fallocate(fd, 0, ts_pool_size);
	D__ASSERTF(!rc, "rc=%d\n", rc);

	rc = vos_pool_create(ts_pmem_file, ts_pool, 0);
	D__ASSERTF(!rc, "rc=%d\n", rc);

	rc = vos_pool_open(ts_pmem_file, ts_pool, &ts_poh);
	D__ASSERTF(!rc, "rc=%d\n", rc);

	rc = vos_cont_create(ts_poh, ts_cont);
	D__ASSERTF(!rc, "rc=%d\n", rc);

	rc = vos_cont_open(ts_poh, ts_cont, &ts_coh);
	D__ASSERT(!rc);

	ts_credits_avail = -1; /* sync mode only */
	return 0;
}

static void
ts_vos_finish(void)
{
	vos_cont_close(ts_coh);
	vos_cont_destroy(ts_poh, ts_cont);

	vos_pool_close(ts_poh);
	vos_pool_destroy(ts_pmem_file, ts_pool);

	vos_fini();
}

static int
ts_daos_prepare(void)
{
	d_rank_t rank = 0;
	int	 i;
	int	 rc;

	if (mpi_rank == 0)
		fprintf(stdout, "Setup DAOS\n");

	rc = daos_init();
	D__ASSERTF(!rc, "rc=%d\n", rc);

	rc = daos_eq_create(&ts_eqh);
	D__ASSERTF(!rc, "rc=%d\n", rc);

	for (i = 0; i < ts_credits_avail; i++) {
		rc = daos_event_init(&ts_credits_buf[i].tc_ev, ts_eqh, NULL);
		D__ASSERTF(!rc, "rc=%d\n", rc);
		ts_credits_buf[i].tc_evp = &ts_credits_buf[i].tc_ev;
	}

	if (mpi_rank == 0) {
		ts_svc.rl_ranks  = &rank;
		ts_svc.rl_nr.num = 1;

		rc = daos_pool_create(0731, geteuid(), getegid(), NULL, NULL,
				      "pmem", ts_pool_size, &ts_svc, ts_pool,
				      NULL);
		if (rc != 0)
			goto bcast;

		ts_svc.rl_nr.num = ts_svc.rl_nr.num_out;
		rc = daos_pool_connect(ts_pool, NULL, &ts_svc, DAOS_PC_EX,
				       &ts_poh, NULL, NULL);
		if (rc != 0)
			goto bcast;

		/** create container */
		rc = daos_cont_create(ts_poh, ts_cont, NULL);
		if (rc != 0)
			goto bcast;

		/** open container */
		rc = daos_cont_open(ts_poh, ts_cont, DAOS_COO_RW, &ts_coh, NULL,
				    NULL);
		if (rc != 0)
			goto bcast;
	}

bcast:
	if (mpi_size > 1)
		MPI_Bcast(&rc, 1, MPI_INT, 0, MPI_COMM_WORLD);
	if (rc)
		D__ASSERTF(!rc, "rc=%d\n", rc);

	if (mpi_size > 1) {
		handle_share(&ts_poh, HANDLE_POOL, mpi_rank, ts_poh, 0);
		handle_share(&ts_coh, HANDLE_CO, mpi_rank, ts_poh, 0);
	}

	return 0;
}

static void
ts_daos_finish(void)
{
	int	i;
	int	rc;

	if (mpi_rank == 0) {
		rc = daos_pool_destroy(ts_pool, NULL, true, NULL);
		D__ASSERTF(!rc, "rc=%d\n", rc);
	}

	for (i = 0; i < ts_credits_avail; i++)
		daos_event_fini(&ts_credits_buf[i].tc_ev);

	daos_eq_destroy(ts_eqh, DAOS_EQ_DESTROY_FORCE);
	daos_fini();
}

static int
ts_prepare(void)
{
	int	i;
	int	rc;

	if (ts_class == DAOS_OC_RAW && mpi_size > 1) {
		fprintf(stderr, "VOS only tests can run with 1 rank\n");
		return -1;
	}

	rc = daos_debug_init(NULL);
	if (rc) {
		fprintf(stderr, "Failed to initialize debug\n");
		return rc;
	}

	if (mpi_rank == 0 || ts_class == DAOS_OC_RAW) {
		uuid_generate(ts_pool);
		uuid_generate(ts_cont);
	}
	uuid_generate(ts_cookie);

	for (i = 0; i < TS_CREDITS_MAX; i++) {
		struct ts_io_credit *cred = &ts_credits_buf[i];

		memset(cred, 0, sizeof(*cred));
		cred->tc_vbuf = calloc(1, ts_val_size);
		if (!cred->tc_vbuf) {
			fprintf(stderr, "Cannt allocate buffer size=%d\n",
				ts_val_size);
			return -1;
		}
		ts_credits[i] = cred;
	}

	if (ts_class == DAOS_OC_RAW)
		rc = ts_vos_prepare();
	else
		rc = ts_daos_prepare();

	return rc;
}

static void
ts_finish(void)
{
	int	i;

	if (ts_class == DAOS_OC_RAW)
		ts_vos_finish();
	else
		ts_daos_finish();

	for (i = 0; i < TS_CREDITS_MAX; i++)
		free(ts_credits_buf[i].tc_vbuf);

	daos_debug_fini();
}

static uint64_t
ts_val_factor(uint64_t val, char factor)
{
	switch (factor) {
	default:
		return val;
	case 'k':
		val *= 1000;
		return val;
	case 'm':
		val *= 1000 * 1000;
		return val;
	case 'g':
		val *= 1000 * 1000 * 1000;
		return val;
	case 'K':
		val *= 1024;
		return val;
	case 'M':
		val *= 1024 * 1024;
		return val;
	case 'G':
		val *= 1024 * 1024 * 1024;
		return val;
	}
}

static const char *
ts_class_name(void)
{
	switch (ts_class) {
	default:
		return "unknown";
	case DAOS_OC_RAW:
		return "VOS (storage only)";
	case DAOS_OC_ECHO_RW:
		return "ECHO (network only)";
	case DAOS_OC_TINY_RW:
		return "DAOS (full stack)";
	}
}

static const char *
ts_val_type(void)
{
	return ts_single ? "single" : "array";
}

static const char *
ts_yes_or_no(bool value)
{
	return value ? "yes" : "no";
}

static void
ts_print_usage(void)
{
	printf("daos_perf -- performance benchmark tool for DAOS\n\
\n\
Description:\n\
	The daos_perf utility benchmarks point-to-point I/O performance of\n\
	different layers of the DAOS stack.\n\
\n\
The options are as follows:\n\
-P number\n\
	Pool size, which can have M (megatbytes)or G (gigabytes) as postfix\n\
	of number. E.g. -P 512M, -P 8G.\n\
\n\
-T vos|echo|daos\n\
	Tyes of test, it can be 'vos', 'echo' and 'daos'.\n\
	vos  : run directly on top of Versioning Object Store (VOS).\n\
	echo : I/O traffic generated by the utility only goes through the\n\
	       network stack and never lands to storage.\n\
	daos : I/O traffic goes through the full DAOS stack, including both\n\
	       network and storage.\n\
	The default value is 'vos'\n\
\n\
-C number\n\
	Credits for concurrently asynchronous I/O. It can be value between 1\n\
	and 64. The utility runs in synchronous mode if credits is set to 0.\n\
	This option is ignored for mode 'vos'.\n\
\n\
-o number\n\
	Number of objects are used by the utility.\n\
\n\
-d number\n\
	Number of dkeys per object. The number can have 'k' or 'm' as postfix\n\
	which stands for kilo or million.\n\
\n\
-a number\n\
	Number of akeys per dkey. The number can have 'k' or 'm' as postfix\n\
	which stands for kilo or million.\n\
\n\
-r number\n\
	Number of records per akey. The number can have 'k' or 'm' as postfix\n\
	which stands for kilo or million\n\
\n\
-A	Use array value of akey, single value is selected by default.\n\
\n\
-s number\n\
	Size of single value, or extent size of array value. The number can\n\
	have 'K' or 'M' as postfix which stands for kilobyte or megabytes.\n\
\n\
-z	Use zero copy API, this option is only valid for 'vos'\n\
\n\
-t	Instead of using different indices and epochs, all I/Os land to the\n\
	same extent in the same epoch. This option can reduce usage of\n\
	storage space.\n");
}

static struct option ts_ops[] = {
	{ "pool",	required_argument,	NULL,	'P' },
	{ "type",	required_argument,	NULL,	'T' },
	{ "credits",	required_argument,	NULL,	'C' },
	{ "obj",	required_argument,	NULL,	'o' },
	{ "dkey",	required_argument,	NULL,	'd' },
	{ "akey",	required_argument,	NULL,	'a' },
	{ "recx",	required_argument,	NULL,	'r' },
	{ "array",	no_argument,		NULL,	'A' },
	{ "size",	required_argument,	NULL,	's' },
	{ "zcopy",	no_argument,		NULL,	'z' },
	{ "overwrite",	no_argument,		NULL,	't' },
	{ "help",	no_argument,		NULL,	'h' },
	{ NULL,		0,			NULL,	0   },
};

int
main(int argc, char **argv)
{
	double	then;
	double	now;
	int	rc;

	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
	MPI_Comm_size(MPI_COMM_WORLD, &mpi_size);

	while ((rc = getopt_long(argc, argv, "P:T:C:o:d:a:r:As:zth",
				 ts_ops, NULL)) != -1) {
		char	*endp;

		switch (rc) {
		default:
			fprintf(stderr, "Unknown option %c\n", rc);
			return -1;
		case 'T':
			if (!strcasecmp(optarg, "echo")) {
				/* just network, no storage */
				ts_class = DAOS_OC_ECHO_RW;

			} else if (!strcasecmp(optarg, "daos")) {
				/* full stack: network + storage */
				ts_class = DAOS_OC_TINY_RW;

			} else if (!strcasecmp(optarg, "vos")) {
				/* pure storage */
				ts_class = DAOS_OC_RAW;

			} else {
				if (mpi_rank == 0)
					ts_print_usage();
				return -1;
			}
			break;
		case 'C':
			ts_credits_avail = strtoul(optarg, &endp, 0);
			if (ts_credits_avail > TS_CREDITS_MAX)
				ts_credits_avail = TS_CREDITS_MAX;
			else if (ts_credits_avail == 0)
				ts_credits_avail = -1; /* synchronous */
			break;
		case 'P':
			ts_pool_size = strtoul(optarg, &endp, 0);
			ts_pool_size = ts_val_factor(ts_pool_size, *endp);
			break;
		case 'o':
			ts_obj_p_cont = strtoul(optarg, &endp, 0);
			ts_obj_p_cont = ts_val_factor(ts_obj_p_cont, *endp);
			break;
		case 'd':
			ts_dkey_p_obj = strtoul(optarg, &endp, 0);
			ts_dkey_p_obj = ts_val_factor(ts_dkey_p_obj, *endp);
			break;
		case 'a':
			ts_akey_p_dkey = strtoul(optarg, &endp, 0);
			ts_akey_p_dkey = ts_val_factor(ts_akey_p_dkey, *endp);
			break;
		case 'r':
			ts_recx_p_akey = strtoul(optarg, &endp, 0);
			ts_recx_p_akey = ts_val_factor(ts_recx_p_akey, *endp);
			break;
		case 'A':
			ts_single = false;
			break;
		case 's':
			ts_val_size = strtoul(optarg, &endp, 0);
			ts_val_size = ts_val_factor(ts_val_size, *endp);
			break;
		case 't':
			ts_overwrite = true;
			break;
		case 'z':
			ts_zero_copy = true;
			break;
		case 'h':
			if (mpi_rank == 0)
				ts_print_usage();
			return 0;
		}
	}

	if (ts_dkey_p_obj == 0 || ts_akey_p_dkey == 0 ||
	    ts_recx_p_akey == 0 || ts_val_size == 0) {
		fprintf(stderr, "Invalid arguments %d/%d/%d/%d\\n",
			ts_akey_p_dkey, ts_recx_p_akey,
			ts_recx_p_akey, ts_val_size);
		if (mpi_rank == 0)
			ts_print_usage();
		return -1;
	}

	rc = ts_prepare();
	if (rc)
		return -1;

	if (mpi_rank == 0)
		fprintf(stdout,
			"Test :\n\t%s\n"
			"Parameters :\n"
			"\tpool size     : %u MB\n"
			"\tcredits       : %d (sync I/O for -ve)\n"
			"\tobj_per_cont  : %u x %d (procs)\n"
			"\tdkey_per_obj  : %u\n"
			"\takey_per_dkey : %u\n"
			"\trecx_per_akey : %u\n"
			"\tvalue type    : %s\n"
			"\tvalue size    : %u\n"
			"\tzero copy     : %s\n"
			"\toverwrite     : %s\n",
			ts_class_name(),
			(unsigned int)(ts_pool_size >> 20),
			ts_credits_avail,
			ts_obj_p_cont,
			mpi_size,
			ts_dkey_p_obj,
			ts_akey_p_dkey,
			ts_recx_p_akey,
			ts_val_type(),
			ts_val_size,
			ts_yes_or_no(ts_zero_copy),
			ts_yes_or_no(ts_overwrite));

	if (mpi_rank == 0)
		fprintf(stdout, "Started...\n");
	MPI_Barrier(MPI_COMM_WORLD);

	then = dts_time_now();
	/* TODO: add fetch performance test */
	rc = ts_write_perf();
	now = dts_time_now();

	if (mpi_size > 1) {
		int rc_g;

		MPI_Allreduce(&rc, &rc_g, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
		rc = rc_g;
	}

	if (rc) {
		fprintf(stderr, "Failed: %d\n", rc);
	} else {
		double		duration, agg_duration;
		double		first_start;
		double		last_end;
		double		duration_max;
		double		duration_min;
		double		duration_sum;

		duration = now - then;

		if (mpi_size > 1) {
			MPI_Reduce(&then, &first_start, 1, MPI_DOUBLE,
				   MPI_MIN, 0, MPI_COMM_WORLD);
			MPI_Reduce(&now, &last_end, 1, MPI_DOUBLE,
				   MPI_MAX, 0, MPI_COMM_WORLD);
		} else {
			first_start = then;
			last_end = now;
		}

		agg_duration = last_end - first_start;

		if (mpi_size > 1) {
			MPI_Reduce(&duration, &duration_max, 1, MPI_DOUBLE,
				   MPI_MAX, 0, MPI_COMM_WORLD);
			MPI_Reduce(&duration, &duration_min, 1, MPI_DOUBLE,
				   MPI_MIN, 0, MPI_COMM_WORLD);
			MPI_Reduce(&duration, &duration_sum, 1, MPI_DOUBLE,
				   MPI_SUM, 0, MPI_COMM_WORLD);
		} else {
			duration_max = duration_min = duration_sum = duration;
		}

		if (mpi_rank == 0) {
			unsigned long	total;
			double		bandwidth;
			double		latency;
			double		rate;

			total = mpi_size * ts_obj_p_cont * ts_dkey_p_obj *
				ts_akey_p_dkey * ts_recx_p_akey;

			rate = total / agg_duration;
			latency = (agg_duration * 1000 * 1000) / total;
			bandwidth = (rate * ts_val_size) / (1024 * 1024);

			fprintf(stdout, "Successfully completed:\n"
				"\tduration : %-10.6f sec\n"
				"\tbandwith : %-10.3f MB/sec\n"
				"\trate     : %-10.2f IO/sec\n"
				"\tlatency  : %-10.3f us (nonsense if credits > 1)\n",
				agg_duration, bandwidth, rate, latency);

			fprintf(stdout, "Duration across processes:\n");
			fprintf(stdout, "MAX duration : %-10.6f sec\n",
				duration_max);
			fprintf(stdout, "MIN duration : %-10.6f sec\n",
				duration_min);
			fprintf(stdout, "Average duration : %-10.6f sec\n",
				duration_sum / mpi_size);
		}
	}

	ts_finish();
	daos_debug_fini();
	MPI_Finalize();

	return 0;
}
