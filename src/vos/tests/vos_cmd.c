#define D_LOGFAC DD_FAC(tests)

#include <fcntl.h>
#include "vts_io.h"
#include <daos/stack_mmap.h>
#include <gurt/atomic.h>

static pthread_once_t     once_control = PTHREAD_ONCE_INIT;
static struct known_pool *current_open;
D_LIST_HEAD(pool_list);

struct known_pool {
	char         *kp_path;
	char         *kp_name;
	d_list_t      kp_link;
	uuid_t        kp_uuid;
	daos_handle_t kp_poh;
	daos_handle_t kp_coh;
};

static ABT_xstream abt_xstream;

static const char  hex[] = "0123456789abcdef";
#define MAX_KEY_LEN 255
#define IO_SIZE     (16 * 1024 * 1024)
static char write_buf[IO_SIZE];

D_LIST_HEAD(free_list);
D_LIST_HEAD(join_list);
D_LIST_HEAD(active_list);

struct ult_info {
	d_list_t           link;
	struct cmd_info   *cinfo;
	ABT_thread         thread;
	bool               async;
};

struct cmd_info {
	char     key[MAX_KEY_LEN + 1];
	uint64_t start;
	uint64_t length;
	int      type;
	int      status;
};

static daos_epoch_t newest_write;

static int
parse_write_info(const char *arg, bool require, struct cmd_info *cinfo, int type)
{
	char              *start;
	char              *length;

	if (arg == NULL)
		goto out;

	strncpy(cinfo->key, arg, MAX_KEY_LEN);
	cinfo->key[MAX_KEY_LEN] = 0;

	start = strchr(cinfo->key, '@');
	if (start == NULL) {
		cinfo->start  = 0;
		cinfo->length = IO_SIZE;
		if (require) {
			printf("Invalid argument, missing start of range\n");
			return 1;
		}
		return 0;
	}

	*start = 0;
	start++;

	length = strchr(start, '-');
	if (length == NULL) {
		cinfo->start  = strtoull(start, NULL, 10);
		cinfo->length = IO_SIZE;
		if (require) {
			printf("Invalid argument, missing length of range\n");
			return 1;
		}
		return 0;
	}

	*length = 0;
	length++;

	cinfo->start  = strtoull(start, NULL, 10);
	cinfo->length = strtoull(length, NULL, 10);

out:
	cinfo->type = type;
	return 0;
}

void
create_const_uuid(uuid_t dest_uuid, const char *name)
{
	int  len    = strlen(name);
	int  stridx = 0;
	int  i;
	char buf[37] = {0};
	int  rc;

	for (i = 0; i < 36; i++) {
		if (i == 8 || i == 13 || i == 18 || i == 23) {
			buf[i] = '-';
			continue;
		}
		buf[i] = hex[((int)name[stridx] * 127) % 16];
		stridx = (stridx + 1) % len;
	}

	rc = uuid_parse(buf, dest_uuid);
	assert_int_equal(rc, 0);
}

void
print_usage(const char *prog)
{
	printf("Usage: %s -r \"[options]\"\n", prog);
	printf("\t--help, -h                               Print this message and exit\n");
	printf("\t--destroy_all, -D                        Destroy any pools created by test\n");
	printf("\t--create, -c      <name>                 Create new pool and container\n");
	printf("\t--open, -o        <name>                 Open existing pool and container\n");
	printf("\n\tRemaining operations act on the open container\n");
	printf("\t--close, -d                              Close current pool and container\n");
	printf("\t--write, -w       <key>[@start-length]   Write dkey range\n");
	printf("\t--punch_range, -P <key>[@start-length]   Punch dkey range\n");
	printf("\t--remove, -R      <key>[@start-length]   Remove dkey range\n");
	printf("\t--remove_all, -A  <key>[@start-length]   Remove all dkey range\n");
	printf("\t--punch, -p       <key>                  Punch dkey\n");
	printf("\t--randomize, -x   <key>@<start>-<length> Randomize I/O over a range\n");
	printf("\t--iterate, -i                            Iterate\n");
	printf("\t--aggregate, -a                          Aggregate\n");
	printf("\t--discard, -r                            Discard writes\n");
	exit(0);
}

static int
alloc_pool(const char *name, bool exclusive, struct known_pool **pool)
{
	struct known_pool *known_pool;

	d_list_for_each_entry(known_pool, &pool_list, kp_link) {
		if (strncmp(name, known_pool->kp_name, PATH_MAX) == 0) {
			if (exclusive)
				return -DER_EXIST;
			*pool = known_pool;
			return 0;
		}
	}

	D_ALLOC_PTR(known_pool);
	if (known_pool == NULL)
		return -DER_NOMEM;

	D_STRNDUP(known_pool->kp_name, name, PATH_MAX);
	if (known_pool->kp_name == NULL) {
		D_FREE(known_pool);
		return -DER_NOMEM;
	}

	create_const_uuid(known_pool->kp_uuid, name);

	D_ASPRINTF(known_pool->kp_path, "%s/%s.vos", vos_path, name);
	if (known_pool->kp_path == NULL) {
		D_FREE(known_pool->kp_name);
		D_FREE(known_pool);
		return -DER_NOMEM;
	}

	d_list_add_tail(&known_pool->kp_link, &pool_list);
	*pool = known_pool;
	return 1;
}

static void
free_pool(struct known_pool *pool)
{
	d_list_del(&pool->kp_link);
	D_FREE(pool->kp_path);
	D_FREE(pool->kp_name);
	D_FREE(pool);
}

/* ACTION(name, function, open, randompct) */
#define FOREACH_OP(ACTION)                                                                         \
	ACTION(CREATE_POOL, create_pool, false, 0)                                                 \
	ACTION(OPEN_POOL, open_pool, false, 0)                                                     \
	ACTION(CLOSE_POOL, close_pool, true, 0)                                                    \
	ACTION(PUNCH_KEY, punch_key, true, 2)                                                      \
	ACTION(PUNCH_EXTENT, write_key, true, 18)                                                  \
	ACTION(WRITE, write_key, true, 56)                                                         \
	ACTION(REMOVE_ONE, write_key, true, 18)                                                    \
	ACTION(REMOVE_ALL, write_key, true, 5)                                                     \
	ACTION(AGGREGATE, aggregate, true, 1)                                                      \
	ACTION(DISCARD, discard, true, 0)                                                          \
	ACTION(ITERATE, iterate, true, 0)                                                          \
	ACTION(SIZE_QUERY, print_size, true, 0)                                                    \
	ACTION(RANDOMIZE, run_many_tests, true, 0)

#define DEFINE_OP_ENUM(name, function, open, share) name,
enum {
	FOREACH_OP(DEFINE_OP_ENUM) OP_COUNT,
};

struct op_info {
	const char *oi_str;
	int (*oi_func)(struct cmd_info *);
	bool oi_open;
	int  oi_random_share;
};

static struct op_info op_info[];

static int            shares[100];

static void
init_shares(void)
{
	int op;
	int share;
	int cursor = 0;
	int c;

	for (op = 0; op < OP_COUNT; op++)
		for (share = 0; share < op_info[op].oi_random_share; share++)
			shares[cursor++] = op;
	/** The values defined in FOREACH_OP are percentages and should add up to 100 which should
	 *  also be equal to the size of the array shares.should add up to the number of entries
	 *  in the array.  If we change it to some other shares, they should still add up to the
	 *  number in the array.
	 */
	D_ASSERT(cursor == ARRAY_SIZE(shares));

	for (c = 0; c < sizeof(write_buf); c++)
		write_buf[c] = (c % 26) + 97;
}

static int
create_pool(struct cmd_info *cinfo)
{
	struct known_pool *known_pool;
	int                fd;
	int                rc = 0;
	int                rc2;

	rc = alloc_pool(&cinfo->key[0], true, &known_pool);
	if (rc < 0) {
		D_ERROR("Could not create pool: rc=" DF_RC "\n", DP_RC(rc));
		return rc;
	}

	fd = open(known_pool->kp_path, O_CREAT | O_RDWR, 0600);
	if (fd < 0) {
		rc = daos_errno2der(errno);
		D_ERROR("Could not create pool file %s, rc=" DF_RC "\n", known_pool->kp_path,
			DP_RC(rc));
		goto free_mem;
	}

	rc = fallocate(fd, 0, 0, 4ULL * 1024 * 1024 * 1024);
	if (rc) {
		rc = daos_errno2der(errno);
		D_ERROR("Could not allocate pool file %s, rc=" DF_RC "\n", known_pool->kp_path,
			DP_RC(rc));
		close(fd);
		goto free_mem;
	}

	close(fd);

	rc = vos_pool_create(known_pool->kp_path, known_pool->kp_uuid, 0, 0, 0, NULL);
	if (rc != 0) {
		D_ERROR("Could not create vos pool at %s, rc=" DF_RC "\n", known_pool->kp_path,
			DP_RC(rc));
		goto free_mem;
	}

	rc = vos_pool_open(known_pool->kp_path, known_pool->kp_uuid, 0, &known_pool->kp_poh);
	if (rc != 0) {
		D_ERROR("Could not open vos pool at %s, rc=" DF_RC "\n", known_pool->kp_path,
			DP_RC(rc));
		goto destroy_pool;
	}

	rc = vos_cont_create(known_pool->kp_poh, known_pool->kp_uuid);
	if (rc != 0) {
		D_ERROR("Could not create vos container, rc=" DF_RC "\n", DP_RC(rc));
		goto close_pool;
	}

	rc = vos_cont_open(known_pool->kp_poh, known_pool->kp_uuid, &known_pool->kp_coh);
	if (rc != 0) {
		goto close_pool;
		D_ERROR("Could not open vos container, rc=" DF_RC "\n", DP_RC(rc));
	}

	D_INFO("Created pool and container at %s, uuid=" DF_UUID "\n", known_pool->kp_path,
	       DP_UUID(known_pool->kp_uuid));

	current_open = known_pool;
	return 0;

close_pool:
	vos_pool_close(known_pool->kp_poh);
destroy_pool:
	rc2 = vos_pool_destroy(known_pool->kp_path, known_pool->kp_uuid);
	if (rc2 != 0)
		printf("Could not destroy pool: " DF_RC "\n", DP_RC(rc));
free_mem:
	free_pool(known_pool);

	return rc;
}

static int
open_pool(struct cmd_info *cinfo)
{
	struct known_pool *known_pool;
	int   rc = 0;
	bool               created;

	rc = alloc_pool(&cinfo->key[0], false, &known_pool);
	if (rc < 0) {
		D_ERROR("Could not open pool: rc=" DF_RC "\n", DP_RC(rc));
		return rc;
	}

	created = rc;

	rc = vos_pool_open(known_pool->kp_path, known_pool->kp_uuid, 0, &known_pool->kp_poh);
	if (rc != 0) {
		D_ERROR("Could not open vos pool at %s, rc=" DF_RC "\n", known_pool->kp_path,
			DP_RC(rc));
		goto out;
	}

	rc = vos_cont_open(known_pool->kp_poh, known_pool->kp_uuid, &known_pool->kp_coh);
	if (rc != 0) {
		D_ERROR("Could not open vos container, rc=" DF_RC "\n", DP_RC(rc));
		goto close_pool;
	}

	D_INFO("Opened pool and container at %s, uuid=" DF_UUID "\n", known_pool->kp_path,
	       DP_UUID(known_pool->kp_uuid));
	current_open = known_pool;
	return 0;

close_pool:
	vos_pool_close(known_pool->kp_poh);
	known_pool->kp_poh = DAOS_HDL_INVAL;
out:
	if (created)
		free_pool(known_pool);
	return rc;
}

static int
close_pool(struct cmd_info *cinfo)
{
	vos_cont_close(current_open->kp_coh);
	vos_pool_close(current_open->kp_poh);
	current_open->kp_coh = DAOS_HDL_INVAL;
	current_open->kp_poh = DAOS_HDL_INVAL;

	D_INFO("Closed pool and container uuid=" DF_UUID "\n", DP_UUID(current_open->kp_uuid));
	current_open = NULL;
	return 0;
}

void
set_oid(daos_unit_oid_t *oid)
{
	oid->id_pub.lo = 0xdeadbeefULL << 32;
	oid->id_pub.hi = 97;
	daos_obj_set_oid(&oid->id_pub, 0, OC_RP_XSF, 0, 0);
	oid->id_shard  = 0;
	oid->id_layout_ver = 0;
	oid->id_padding = 0;
}

static int
punch_key(struct cmd_info *cinfo)
{
	int             rc = 0;
	daos_key_t      dkey;
	daos_unit_oid_t oid;

	set_oid(&oid);

	/* Set up dkey */
	d_iov_set(&dkey, &cinfo->key[0], strlen(&cinfo->key[0]));

	/* Write the original value (under) */
	rc = vos_obj_punch(current_open->kp_coh, oid, d_hlc_get(), 0, 0, &dkey, 0, NULL, NULL);

	D_INFO("Punch %s in pool and container uuid=" DF_UUID ", rc=" DF_RC "\n", cinfo->key,
	       DP_UUID(current_open->kp_uuid), DP_RC(rc));
	return rc;
}

static int
discard(struct cmd_info *cinfo)
{
	daos_epoch_range_t epr = {0, d_hlc_get()};
	int                rc;

	rc = vos_discard(current_open->kp_coh, NULL, &epr, NULL, NULL);

	D_INFO("Discard pool and container uuid=" DF_UUID ", rc=" DF_RC "\n",
	       DP_UUID(current_open->kp_uuid), DP_RC(rc));
	return rc;
}

static int
print_size(struct cmd_info *cinfo)
{
	int             rc;
	vos_pool_info_t pinfo;

	rc = vos_pool_query(current_open->kp_poh, &pinfo);

	if (rc != 0) {
		D_ERROR("Could not query pool uuid=" DF_UUID ", rc=" DF_RC "\n",
			DP_UUID(current_open->kp_uuid), DP_RC(rc));
		return rc;
	}

	D_INFO("Size query for pool uuid=" DF_UUID " got"
	       " scm={sys=" DF_U64 ",free=" DF_U64 ",total=" DF_U64 "}\n",
	       DP_UUID(current_open->kp_uuid), SCM_SYS(&pinfo.pif_space),
	       SCM_FREE(&pinfo.pif_space), SCM_TOTAL(&pinfo.pif_space));

	return 0;
}

static int
iter_cb(daos_handle_t ih, vos_iter_entry_t *entry, vos_iter_type_t type, vos_iter_param_t *param,
	void *cb_arg, unsigned int *acts)
{
	int *count = cb_arg;
	const char *val;
	int         len;

	switch (type) {
	case VOS_ITER_DKEY:
		printf("dkey=" DF_KEY "\n", DP_KEY(&entry->ie_key));
		break;
	case VOS_ITER_AKEY:
		printf("\takey=" DF_KEY "\n", DP_KEY(&entry->ie_key));
		break;
	case VOS_ITER_RECX:
		val = vos_pool_biov2addr(current_open->kp_poh, &entry->ie_biov);
		if (val == NULL) {
			len = 3;
			val = "N/A";
		} else {
			len = min(entry->ie_recx.rx_nr, 16);
		}

		printf("\t\trecx=" DF_U64 " bytes at " DF_U64 " epc=" DF_X64
		       ".%d hole=%d covered=%d val=%.*s\n",
		       entry->ie_recx.rx_nr, entry->ie_recx.rx_idx, entry->ie_epoch,
		       entry->ie_minor_epc, bio_addr_is_hole(&entry->ie_biov.bi_addr),
		       (entry->ie_vis_flags & VOS_VIS_FLAG_COVERED) != 0, len, val);
		(*count)++;
		break;
	default:
		printf("Garbage type %d\n", type);
	}

	*acts = 0;
	return 0;
}

static int
iterate(struct cmd_info *cinfo)
{
	struct vos_iter_anchors anchors = {0};
	vos_iter_param_t        param   = {0};
	int                     count   = 0;
	int                     rc      = 0;

	param.ip_hdl = current_open->kp_coh;
	set_oid(&param.ip_oid);
	param.ip_epr.epr_lo = 0;
	param.ip_epr.epr_hi = d_hlc_get();
	param.ip_flags      = VOS_IT_RECX_VISIBLE | VOS_IT_RECX_COVERED;

	rc = vos_iterate(&param, VOS_ITER_DKEY, true, &anchors, iter_cb, NULL, &count, NULL);
	if (rc != 0) {
		printf("Failed to iterate, rc=" DF_RC "\n", DP_RC(rc));
		goto out;
	}
	printf("Total recx count is %d\n", count);
out:
	return rc;
}

int
write_key(struct cmd_info *cinfo)
{
	int                rc       = 0;
	daos_key_t         dkey;
	daos_epoch_range_t epr;
	daos_recx_t        rex;
	daos_iod_t         iod;
	d_sg_list_t        sgl;
	d_sg_list_t       *sglp     = &sgl;
	char               akey_val = '\0';
	daos_unit_oid_t    oid;

	memset(&rex, 0, sizeof(rex));
	memset(&iod, 0, sizeof(iod));

	set_oid(&oid);

	/* Set up dkey and akey */
	d_iov_set(&dkey, &cinfo->key[0], strlen(cinfo->key));
	d_iov_set(&iod.iod_name, &akey_val, sizeof(akey_val));

	iod.iod_type = DAOS_IOD_ARRAY;
	if (cinfo->type == PUNCH_EXTENT) {
		iod.iod_size = 0;
		sglp         = NULL;
	} else {
		iod.iod_size = 1;
	}
	iod.iod_recxs = &rex;
	iod.iod_nr    = 1;
	/* Allocate memory for the scatter-gather list */
	rc = d_sgl_init(&sgl, 1);
	if (rc != 0)
		goto out;

	rex.rx_idx = cinfo->start;

	while (cinfo->length > 0) {
		rex.rx_nr = MIN(cinfo->length, IO_SIZE);
		cinfo->length -= rex.rx_nr;
		d_iov_set(&sgl.sg_iovs[0], (void *)write_buf, rex.rx_nr);

		if (cinfo->type == REMOVE_ALL)
			epr.epr_lo = 0;
		else
			epr.epr_lo = newest_write;
		epr.epr_hi = d_hlc_get();

		D_INFO("begin %s " DF_U64 " bytes from " DF_U64 " in %s at " DF_X64
		       " in pool and container uuid=" DF_UUID "\n",
		       op_info[cinfo->type].oi_str, rex.rx_nr, rex.rx_idx, cinfo->key, epr.epr_hi,
		       DP_UUID(current_open->kp_uuid));

		if (cinfo->type == REMOVE_ONE || cinfo->type == REMOVE_ALL) {
			D_INFO("epoch range is " DF_X64 "-" DF_X64 "\n", epr.epr_lo, epr.epr_hi);
			/* Remove range */
			rc = vos_obj_array_remove(current_open->kp_coh, oid, &epr, &dkey,
						  &iod.iod_name, &rex);
		} else {
			/* Write the original value (under) */
			rc = vos_obj_update(current_open->kp_coh, oid, epr.epr_hi, 0, 0, &dkey, 1,
					    &iod, NULL, sglp);

			if (epr.epr_hi > newest_write)
				newest_write = epr.epr_hi;

			ABT_thread_yield();
		}

		D_INFO("end   %s " DF_U64 " bytes from " DF_U64 " in %s at " DF_X64
		       " in pool and container uuid=" DF_UUID ", rc=" DF_RC "\n",
		       op_info[cinfo->type].oi_str, rex.rx_nr, rex.rx_idx, cinfo->key, epr.epr_hi,
		       DP_UUID(current_open->kp_uuid), DP_RC(rc));

		if (rc != 0)
			break;

		rex.rx_idx += rex.rx_nr;
	}
out:
	d_sgl_fini(&sgl, false);
	return rc;
}

static int in_agg;
int
aggregate(struct cmd_info *cinfo)
{
	daos_epoch_range_t epr      = {0, d_hlc_get()};
	int                rc;

	if (in_agg)
		return 0;

	in_agg = 1;

	rc = vos_aggregate(current_open->kp_coh, &epr, NULL, NULL, 0);

	in_agg = 0;

	D_INFO("Aggregate pool and container uuid=" DF_UUID ", rc=" DF_RC "\n",
	       DP_UUID(current_open->kp_uuid), DP_RC(rc));

	return rc;
}

static void
ult_func(void *arg)
{
	struct ult_info *ult_info = arg;
	struct cmd_info *cinfo    = ult_info->cinfo;
	int              rc;

	rc = op_info[cinfo->type].oi_func(cinfo);

	ult_info->cinfo->status = rc;
	if (ult_info->async) {
		d_list_del(&ult_info->link);             /* remove from active list */
		d_list_add(&ult_info->link, &join_list); /* add to join list */
	}
}

int
handle_op(struct cmd_info *cinfo, bool async)
{
	struct ult_info *ult_info;
	int              rc = 0;

	ult_info = d_list_pop_entry(&free_list, struct ult_info, link);
	if (ult_info == NULL) {
		D_ALLOC_PTR(ult_info);
		if (ult_info == NULL)
			return -DER_NOMEM;
	}

	ult_info->cinfo = cinfo;
	ult_info->async = async;
	if (async)
		d_list_add(&ult_info->link, &active_list);

	rc = daos_abt_thread_create_on_xstream(NULL, NULL, abt_xstream, ult_func, ult_info,
					       ABT_THREAD_ATTR_NULL, &ult_info->thread);
	assert_int_equal(rc, ABT_SUCCESS);

	if (!async) {
		rc = ABT_thread_join(ult_info->thread);
		assert_int_equal(rc, ABT_SUCCESS);
		ABT_thread_free(&ult_info->thread);
		d_list_add(&ult_info->link, &free_list);
		rc = cinfo->status;
	} /** Join in caller */

	return rc;
}

#define assert_status(cinfo)                                                                       \
	do {                                                                                       \
		int __status = (cinfo)->status;                                                    \
		int __type   = (cinfo)->type;                                                      \
		D_FREE(cinfo);                                                                     \
		if (__status == 0)                                                                 \
			break;                                                                     \
		printf("%s operation failed rc=" DF_RC "\n", op_info[__type].oi_str,               \
		       DP_RC(__status));                                                           \
		assert_rc_equal(__status, 0);                                                      \
	} while (0)

int
run_many_tests(struct cmd_info *pinfo)
{
	struct timespec    end_time;
	struct ult_info   *ult_info;
	struct cmd_info   *cinfo = NULL;
	unsigned int       seed  = (unsigned int)(time(NULL) & 0x0FFFFFFFFULL);
	int               *run_counts;
	int                rc;
	int                i;

	D_ALLOC_ARRAY(run_counts, OP_COUNT);
	assert_non_null(run_counts);

	daos_fail_loc_set(DAOS_VOS_AGG_RANDOM_YIELD | DAOS_FAIL_ALWAYS);

	printf("Starting randomized test with seed = %x\n", seed);
	rc = d_gettime(&end_time);
	assert_rc_equal(rc, 0);

	/* Run for 30 seconds */
	end_time.tv_sec += 30;

	while (d_timeleft_ns(&end_time) != 0) {
		/* First free ults that are done */
		while ((ult_info = d_list_pop_entry(&join_list, struct ult_info, link)) != NULL) {
			rc = ABT_thread_join(ult_info->thread);
			assert_int_equal(rc, ABT_SUCCESS);
			ABT_thread_free(&ult_info->thread);
			cinfo = ult_info->cinfo;
			d_list_add(&ult_info->link, &free_list);
			assert_status(cinfo);
		}

		D_ALLOC_PTR(cinfo);
		assert_non_null(cinfo);

		do {
			cinfo->type = shares[rand_r(&seed) % ARRAY_SIZE(shares)];
		} while (cinfo->type == AGGREGATE && in_agg);

		memcpy(&cinfo->key[0], &pinfo->key[0], sizeof(pinfo->key));

		cinfo->start  = pinfo->start + rand_r(&seed) % pinfo->length;
		cinfo->length = rand_r(&seed) % (pinfo->start + pinfo->length - cinfo->start) + 1;

		rc = handle_op(cinfo, true);
		if (rc != 0) {
			cinfo->status = rc;
			assert_status(cinfo);
			break;
		}
		run_counts[cinfo->type]++;

		if ((rand_r(&seed) % 10) == 0)
			ABT_thread_yield();
	}

	for (;;) {
		while ((ult_info = d_list_pop_entry(&join_list, struct ult_info, link)) != NULL) {
			ABT_thread_join(ult_info->thread);
			if (rc != ABT_SUCCESS)
				return rc;
			ABT_thread_free(&ult_info->thread);
			cinfo = ult_info->cinfo;
			assert_status(cinfo);
			D_FREE(ult_info);
		}

		if (d_list_empty(&active_list))
			break;

		ABT_thread_yield();
	}

	printf("Operation         Runs\n");
	for (i = 0; i < OP_COUNT; i++) {
		if (run_counts[i] == 0)
			continue;
		printf("%-12s%10d\n", op_info[i].oi_str, run_counts[i]);
	}

	D_FREE(run_counts);

	return 0;
}

#define DEFINE_OP_INFO(name, function, open, share) {#name, function, open, share},

static struct op_info op_info[] = {FOREACH_OP(DEFINE_OP_INFO)};

static int
abit_start(void)
{
	int rc;

	rc = ABT_init(0, NULL);
	if (rc != ABT_SUCCESS) {
		fprintf(stderr, "ABT init failed: %d\n", rc);
		return -1;
	}

	rc = ABT_xstream_self(&abt_xstream);
	if (rc != ABT_SUCCESS) {
		ABT_finalize();
		printf("ABT get self xstream failed: %d\n", rc);
		return -1;
	}

	return 0;
}

static void
abit_fini(void)
{
	ABT_xstream_join(abt_xstream);
	ABT_xstream_free(&abt_xstream);
	ABT_finalize();
}

struct args {
	char            *a_argbuf;
	char            *a_cfg;
	char           **a_argv;
	struct cmd_info *a_cmds;
	bool             a_clean_all;
	int              a_nr;
	int              a_cmd_nr;
	int              a_size;
};

static struct args args;

static int
split_cmd_args(const char *arg0, const char *cmd)
{
	char  *token;
	char  *dest;
	char  *src;
	char   last;
	char  *saveptr = NULL;
	char **newptr;

	memset(&args, 0, sizeof(args));
	D_ASPRINTF(args.a_argbuf, "%s %s", arg0, cmd);
	if (args.a_argbuf == NULL)
		return -DER_NOMEM;

	D_STRNDUP(args.a_cfg, cmd, PATH_MAX);
	if (args.a_cfg == NULL) {
		D_FREE(args.a_argbuf);
		return -DER_NOMEM;
	}

	D_ALLOC_ARRAY(args.a_argv, 64);
	if (args.a_argv == NULL) {
		D_FREE(args.a_argbuf);
		D_FREE(args.a_cfg);
		return -DER_NOMEM;
	}
	args.a_size = 64;

	token = strtok_r(args.a_argbuf, " ", &saveptr);
	while (token != NULL) {
		if (args.a_nr == args.a_size) {
			D_REALLOC_ARRAY(newptr, args.a_argv, args.a_size, args.a_size * 2);
			if (newptr == NULL) {
				D_FREE(args.a_argv);
				D_FREE(args.a_argbuf);
				D_FREE(args.a_cfg);
				return -DER_NOMEM;
			}
			args.a_argv = newptr;
			args.a_size *= 2;
		}
		args.a_argv[args.a_nr++] = token;
		token                    = strtok_r(NULL, " ", &saveptr);
	}

	/** Allocate enough space for parsed commands */
	D_ALLOC_ARRAY(args.a_cmds, args.a_nr);
	if (args.a_cmds == NULL) {
		D_FREE(args.a_argv);
		D_FREE(args.a_argbuf);
		D_FREE(args.a_cfg);
		return -DER_NOMEM;
	}

	last = 0;
	src = dest = args.a_cfg;
	while (*src != 0) {
		if (*src == '@' || *src == '-' || *src == ' ') {
			if (last == '_') {
				src++;
				continue;
			}
			*dest = '_';
		} else {
			*dest = *src;
		}
		last = *dest;
		dest++;
		src++;
	}

	*dest = 0;

	return 0;
}

static void
free_args(void)
{
	D_FREE(args.a_argv);
	D_FREE(args.a_cfg);
	D_FREE(args.a_argbuf);
	D_FREE(args.a_cmds);
}

static void
cmd_test(void **state)
{
	int             i;
	int             rc;
	struct op_info *oinfo;

	for (i = 0; i < args.a_cmd_nr; i++) {
		oinfo = &op_info[args.a_cmds[i].type];
		if (oinfo->oi_open)
			assert_non_null(current_open);
		else
			assert_null(current_open);

		rc = handle_op(&args.a_cmds[i], false);
		assert_rc_equal(rc, 0);
	}
}

static const struct CMUnitTest cmd_tests[] = {
    {"VOS999: Command line test", cmd_test, NULL, NULL},
};

static void
free_pools(void)
{
	struct known_pool *known_pool;
	struct known_pool *tmp;
	int                rc;

	d_list_for_each_entry_safe(known_pool, tmp, &pool_list, kp_link) {
		if (daos_handle_is_valid(known_pool->kp_coh))
			vos_cont_close(known_pool->kp_coh);
		if (daos_handle_is_valid(known_pool->kp_poh))
			vos_pool_close(known_pool->kp_poh);
		if (args.a_clean_all) {
			rc = vos_pool_destroy(known_pool->kp_path, known_pool->kp_uuid);
			if (rc != 0)
				printf("Failed to destroy pool: rc=" DF_RC "\n", DP_RC(rc));
		}
		free_pool(known_pool);
	}
}

int
run_vos_command(const char *arg0, const char *cmd)
{
	char                 test_name[DTS_CFG_MAX];
	struct cmd_info     *cinfo;
	struct ult_info     *ult_info;
	static struct option long_options[] = {
	    {"create", required_argument, 0, 'c'},
	    {"open", required_argument, 0, 'o'},
	    {"close", no_argument, 0, 'd'},
	    {"write", required_argument, 0, 'w'},
	    {"punch_range", required_argument, 0, 'P'},
	    {"iterate", required_argument, 0, 'i'},
	    {"remove", required_argument, 0, 'R'},
	    {"punch", required_argument, 0, 'p'},
	    {"aggregate", no_argument, 0, 'a'},
	    {"discard", no_argument, 0, 'r'},
	    {"remove_all", no_argument, 0, 'A'},
	    {"size", no_argument, 0, 's'},
	    {"destroy_all", no_argument, 0, 'D'},
	    {"randomize", required_argument, 0, 'x'},
	    {"help", no_argument, 0, 'h'},
	    {0, 0, 0, 0},
	};
	int rc;
	int c;
	int old_optind   = optind;
	int option_index = 0;

	pthread_once(&once_control, init_shares);

	rc = split_cmd_args(arg0, cmd);
	if (rc != 0)
		return 1;

	dts_create_config(test_name, "Command-line %s", args.a_cfg);

	if (abit_start() != 0) {
		free_args();
		D_ERROR("Failed to init abt\n");
		return 1;
	}

	optind = 1;

	while ((c = getopt_long(args.a_nr, args.a_argv, "c:o:dw:p:ahrsP:R:ix:A:D", long_options,
				&option_index)) != -1) {
		cinfo = &args.a_cmds[args.a_cmd_nr];
		switch (c) {
		case '?':
			printf("Invalid argument %s\n", args.a_argv[optind]);
			/* fallthrough */
		case 'h':
			print_usage(args.a_argv[0]);
			break;
		case 'i':
			cinfo->type = ITERATE;
			args.a_cmd_nr++;
			break;
		case 'c':
			cinfo->type = CREATE_POOL;
			strncpy(cinfo->key, optarg, MAX_KEY_LEN);
			cinfo->key[MAX_KEY_LEN] = 0;
			args.a_cmd_nr++;
			break;
		case 'o':
			cinfo->type = OPEN_POOL;
			strncpy(cinfo->key, optarg, MAX_KEY_LEN);
			cinfo->key[MAX_KEY_LEN] = 0;
			args.a_cmd_nr++;
			break;
		case 'd':
			cinfo->type = CLOSE_POOL;
			args.a_cmd_nr++;
			break;
		case 'D':
			args.a_clean_all = true;
			break;
		case 'w':
			rc = parse_write_info(optarg, false, cinfo, WRITE);
			if (rc != 0)
				return 1;
			args.a_cmd_nr++;
			break;
		case 'P':
			rc = parse_write_info(optarg, false, cinfo, PUNCH_EXTENT);
			if (rc != 0)
				return 1;
			args.a_cmd_nr++;
			break;
		case 'R':
			rc = parse_write_info(optarg, false, cinfo, REMOVE_ONE);
			if (rc != 0)
				return 1;
			args.a_cmd_nr++;
			break;
		case 'A':
			rc = parse_write_info(optarg, false, cinfo, REMOVE_ALL);
			if (rc != 0)
				return 1;
			args.a_cmd_nr++;
			break;
		case 'x':
			rc = parse_write_info(optarg, true, cinfo, RANDOMIZE);
			if (rc != 0) {
				print_usage(args.a_argv[0]);
				return 1;
			}
			args.a_cmd_nr++;
			break;
		case 'p':
			cinfo->type = PUNCH_KEY;
			strncpy(cinfo->key, optarg, MAX_KEY_LEN);
			cinfo->key[MAX_KEY_LEN] = 0;
			args.a_cmd_nr++;
			break;
		case 'a':
			cinfo->type = AGGREGATE;
			args.a_cmd_nr++;
			break;
		case 'r':
			cinfo->type = DISCARD;
			args.a_cmd_nr++;
			break;
		case 's':
			cinfo->type = SIZE_QUERY;
			args.a_cmd_nr++;
			break;
		}
	}

	rc = cmocka_run_group_tests_name(test_name, cmd_tests, NULL, NULL);

	while ((ult_info = d_list_pop_entry(&free_list, struct ult_info, link)) != NULL)
		D_FREE(ult_info);

	daos_fail_loc_set(0);

	abit_fini();

	free_pools();

	free_args();
	optind = old_optind;

	return rc;
}
