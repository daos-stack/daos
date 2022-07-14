#define D_LOGFAC DD_FAC(vos)

#define _GNU_SOURCE
#include <stdio.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <uuid/uuid.h>
#include <daos_srv/vos.h>
#include <abt.h>
#include <daos.h>
#include <daos/stack_mmap.h>

static bool current_open;
static uuid_t	current_uuid;
static daos_handle_t poh;
static daos_handle_t coh;

static ABT_xstream	abt_xstream;

static const char hex[] = "0123456789abcdef";
#define MAX_KEY_LEN 255
#define IO_SIZE (8 * 1024 * 1024)
static char		 write_buf[IO_SIZE];

D_LIST_HEAD(free_list);
D_LIST_HEAD(join_list);
D_LIST_HEAD(active_list);

struct ult_info {
	d_list_t		 link;
	struct write_info	*winfo;
	ABT_thread		 thread;
};

struct write_info {
	char		 key[MAX_KEY_LEN + 1];
	uint64_t	 start;
	uint64_t	 length;
	int		 type;
	int		 status;
};

static daos_epoch_t newest_write;

static int
parse_write_info(const char *arg, bool require, struct write_info **winfop, int type)
{
	char *start;
	char *length;
	struct write_info	*winfo;

	D_ALLOC_PTR(winfo);
	if (winfo == NULL)
		return -DER_NOMEM;

	if (arg == NULL)
		goto out;

	strncpy(winfo->key, arg, MAX_KEY_LEN);
	winfo->key[MAX_KEY_LEN] = 0;

	start = strchr(winfo->key, '@');
	if (start == NULL) {
		winfo->start = 0;
		winfo->length = IO_SIZE;
		if (require) {
			D_FREE(winfo);
			printf("Invalid argument, missing start of range\n");
			return -1;
		}
		*winfop = winfo;
		return 0;
	}

	*start = 0;
	start++;

	length = strchr(start, '-');
	if (length == NULL) {
		winfo->start = strtoull(start, NULL, 10);
		winfo->length = IO_SIZE;
		if (require) {
			D_FREE(winfo);
			printf("Invalid argument, missing length of range\n");
			return -1;
		}
		*winfop = winfo;
		return 0;
	}

	*length = 0;
	length++;

	winfo->start = strtoull(start, NULL, 10);
	winfo->length = strtoull(length, NULL, 10);

out:
	winfo->type = type;
	*winfop = winfo;
	return 0;
}

void
create_const_uuid(uuid_t dest_uuid, const char *name)
{
	int len = strlen(name);
	int stridx = 0;
	int i;
	char buf[37] = {0};

	for (i = 0; i < 36; i++) {
		if (i == 8 || i == 13 || i == 18 || i == 23) {
			buf[i] = '-';
			continue;
		}
		buf[i] = hex[((int)name[stridx] * 127) % 16];
		stridx = (stridx + 1) % len;
	}

	uuid_parse(buf, dest_uuid);
}

void
print_usage(const char *prog)
{
	printf("Usage: %s [options]\n", prog);
	printf("\t--create, -c      <name>                 Create new pool and container\n");
	printf("\t--open, -o        <name>                 Open existing pool and container\n");
	printf("\t--close, -d                              Close current pool and container\n");
	printf("\n\tRemaining operations act on the open container\n");
	printf("\t--write, -w       <key>[@start-length]   Write dkey range\n");
	printf("\t--punch_range, -P <key>[@start-length]   Punch dkey range\n");
	printf("\t--remove, -R      <key>[@start-length]   Remove dkey range\n");
	printf("\t--remove-all, -A  <key>[@start-length]   Remove all dkey range\n");
	printf("\t--punch, -p       <key>                  Punch dkey\n");
	printf("\t--randomize, -z   <key>@<start>-<length> Randomize I/O over a range\n");
	printf("\t--iterate, -i                            Iterate\n");
	printf("\t--aggregate, -a                          Aggregate\n");
	printf("\t--discard, -r                            Discard writes\n");
	exit(0);
}

int
create_pool(const char *name)
{
	int fd;
	int rc = 0;
	char *path;

	create_const_uuid(current_uuid, name);

	D_ASPRINTF(path, "/mnt/daos/%s.vos", name);
	if (path == NULL)
		return -DER_NOMEM;

	fd = open(path, O_CREAT|O_RDWR, 0600);
	if (fd < 0) {
		rc = daos_errno2der(errno);
		D_ERROR("Could not create pool file %s, rc="DF_RC"\n", path, DP_RC(rc));
		goto out;
	}

	rc = fallocate(fd, 0, 0, 4ULL*1024*1024*1024);
	if (rc) {
		rc = daos_errno2der(errno);
		D_ERROR("Could not allocate pool file %s, rc="DF_RC"\n", path, DP_RC(rc));
		goto out;
	}

	close(fd);

	rc = vos_pool_create(path, current_uuid, 0, 0, 0, NULL);
	if (rc != 0) {
		D_ERROR("Could not create vos pool at %s, rc="DF_RC"\n", path, DP_RC(rc));
		goto out;
	}

	rc = vos_pool_open(path, current_uuid, 0, &poh);
	if (rc != 0) {
		D_ERROR("Could not open vos pool at %s, rc="DF_RC"\n", path, DP_RC(rc));
		goto out;
	}

	rc = vos_cont_create(poh, current_uuid);
	if (rc != 0) {
		D_ERROR("Could not create vos container, rc="DF_RC"\n", DP_RC(rc));
		goto out;
	}

	rc = vos_cont_open(poh, current_uuid, &coh);
	if (rc != 0)
		D_ERROR("Could not open vos container, rc="DF_RC"\n", DP_RC(rc));

	D_INFO("Created pool and container at %s, uuid="DF_UUID"\n", path, DP_UUID(current_uuid));
out:
	D_FREE(path);
	return rc;
}

int
open_pool(const char *name)
{
	int rc = 0;
	char *path;

	create_const_uuid(current_uuid, name);
	D_ASPRINTF(path, "/mnt/daos/%s.vos", name);
	if (path == NULL)
		return -DER_NOMEM;

	rc = vos_pool_open(path, current_uuid, 0, &poh);
	if (rc != 0) {
		D_ERROR("Could not open vos pool at %s, rc="DF_RC"\n", path, DP_RC(rc));
		goto out;
	}

	rc = vos_cont_open(poh, current_uuid, &coh);
	if (rc != 0)
		D_ERROR("Could not open vos container, rc="DF_RC"\n", DP_RC(rc));

	D_INFO("Opened pool and container at %s, uuid="DF_UUID"\n", path, DP_UUID(current_uuid));
out:
	D_FREE(path);
	return rc;
}

void
close_pool(void)
{
	vos_cont_close(coh);
	vos_pool_close(poh);

	D_INFO("Closed pool and container uuid="DF_UUID"\n", DP_UUID(current_uuid));
}

void
set_oid(daos_unit_oid_t *oid)
{
	oid->id_pub.lo	= 0xdeadbeefULL << 32;
	oid->id_pub.hi	= 97;
	daos_obj_set_oid(&oid->id_pub, 0, OC_RP_XSF, 0, 0);
	oid->id_shard = 0;
	oid->id_pad_32 = 0;
}

enum { PUNCH, WRITE, REMOVE, REMOVE_ALL, AGGREGATE };
const char *start_strs[] = {"Punching", "Writing", "Removing", "Removing", "Aggregating"};
const char *end_strs[] = {"Punched", "Wrote", "Removed", "Removed", "Aggregated"};

static int iter_cb(daos_handle_t ih, vos_iter_entry_t *entry, vos_iter_type_t type,
		   vos_iter_param_t *param, void *cb_arg, unsigned int *acts)
{
	int	*count = cb_arg;
	switch (type) {
	case VOS_ITER_DKEY:
		printf("dkey="DF_KEY"\n", DP_KEY(&entry->ie_key));
		break;
	case VOS_ITER_AKEY:
		printf("\takey="DF_KEY"\n", DP_KEY(&entry->ie_key));
		break;
	case VOS_ITER_RECX:
		printf("\t\trecx="DF_U64" bytes at "DF_U64" epc="DF_X64".%d hole=%d covered=%d\n",
		       entry->ie_recx.rx_nr, entry->ie_recx.rx_idx, entry->ie_epoch,
		       entry->ie_minor_epc, bio_addr_is_hole(&entry->ie_biov.bi_addr),
		       (entry->ie_vis_flags & VOS_VIS_FLAG_COVERED) != 0);
		(*count)++;
		break;
	default:
		printf("Garbage type %d\n", type);
	}

	*acts = 0;
	return 0;
}

int
iterate(void)
{
	struct vos_iter_anchors	anchors = {0};
	vos_iter_param_t	param = {0};
	int			count = 0;
	int			rc = 0;

	param.ip_hdl = coh;
	set_oid(&param.ip_oid);
	param.ip_epr.epr_lo = 0;
	param.ip_epr.epr_hi = crt_hlc_get();
	param.ip_flags = VOS_IT_RECX_VISIBLE | VOS_IT_RECX_COVERED;

	rc = vos_iterate(&param, VOS_ITER_DKEY, true, &anchors, iter_cb, NULL, &count, NULL);
	if (rc != 0) {
		printf("Failed to iterate, rc="DF_RC"\n", DP_RC(rc));
		goto out;
	}
	printf("Total recx count is %d\n", count);
out:
	return rc;
}

void
write_key_ult(void *arg)
{
	struct ult_info		*ult_info = arg;
	struct write_info	*winfo = ult_info->winfo;
	int			rc = 0;
	daos_key_t		dkey;
	daos_epoch_range_t	epr;
	daos_recx_t		rex;
	daos_iod_t		iod;
	d_sg_list_t		sgl;
	d_sg_list_t		*sglp = &sgl;
	char			akey_val = '\0';
	daos_unit_oid_t		oid;

	memset(&rex, 0, sizeof(rex));
	memset(&iod, 0, sizeof(iod));

	set_oid(&oid);

	/* Set up dkey and akey */
	d_iov_set(&dkey, &winfo->key[0], strlen(winfo->key));
	d_iov_set(&iod.iod_name, &akey_val, sizeof(akey_val));

	iod.iod_type = DAOS_IOD_ARRAY;
	if (winfo->type == PUNCH) {
		iod.iod_size = 0;
		sglp = NULL;
	} else {
		iod.iod_size = 1;
	}
	iod.iod_recxs = &rex;
	iod.iod_nr = 1;
	/* Allocate memory for the scatter-gather list */
	rc = d_sgl_init(&sgl, 1);
	if (rc != 0)
		goto out;

	rex.rx_idx = winfo->start;

	while (winfo->length > 0) {
		rex.rx_nr = MIN(winfo->length, IO_SIZE);
		winfo->length -= rex.rx_nr;
		d_iov_set(&sgl.sg_iovs[0], (void *)write_buf, rex.rx_nr);

		if (winfo->type == REMOVE_ALL)
			epr.epr_lo = 0;
		else
			epr.epr_lo = newest_write;
		epr.epr_hi = crt_hlc_get();

		D_INFO("%s "DF_U64" bytes from "DF_U64" in %s at "DF_X64" in pool and container uuid="
		       DF_UUID"\n", start_strs[winfo->type], rex.rx_nr, rex.rx_idx, winfo->key,
		       epr.epr_hi, DP_UUID(current_uuid));

		if (winfo->type == REMOVE || winfo->type == REMOVE_ALL) {
			D_INFO("epoch range is "DF_X64"-"DF_X64"\n", epr.epr_lo, epr.epr_hi);
			/* Remove range */
			rc = vos_obj_array_remove(coh, oid, &epr, &dkey, &iod.iod_name, &rex);
		} else {
			/* Write the original value (under) */
			rc = vos_obj_update(coh, oid, epr.epr_hi, 0,
					    0, &dkey, 1, &iod, NULL, sglp);

			if (epr.epr_hi > newest_write)
				newest_write = epr.epr_hi;

			ABT_thread_yield();
		}

		D_INFO("%s "DF_U64" bytes from "DF_U64" in %s at "DF_X64" in pool and container uuid="
		       DF_UUID", rc="DF_RC"\n", end_strs[winfo->type], rex.rx_nr, rex.rx_idx,
		       winfo->key, epr.epr_hi, DP_UUID(current_uuid), DP_RC(rc));

		if (rc != 0)
			break;

		rex.rx_idx += rex.rx_nr;
	}
out:
	d_sgl_fini(&sgl, false);
	winfo->status = rc;
	d_list_del(&ult_info->link); /* remove from active list */
	d_list_add(&ult_info->link, &join_list); /* add to join list */

	return;
}

static int in_agg = 0;
void
aggregate_ult(void *arg)
{
	struct ult_info	*ult_info = arg;
	daos_epoch_range_t epr = {0, crt_hlc_get()};
	int rc;

	if (in_agg) {
		ult_info->winfo->status = 0;
		return;
	}
	in_agg = 1;

	rc = vos_aggregate(coh, &epr, NULL, NULL, 0);

	in_agg = 0;

	D_INFO("Aggregate pool and container uuid="DF_UUID", rc="DF_RC"\n", DP_UUID(current_uuid),
	       DP_RC(rc));

	ult_info->winfo->status = rc;
	d_list_del(&ult_info->link); /* remove from active list */
	d_list_add(&ult_info->link, &join_list); /* add to join list */
}

int
handle_op(struct write_info *winfo, bool wait)
{
	struct ult_info	*ult_info;
	int		rc = 0;
	void		(*func)(void *) = write_key_ult;

	ult_info = d_list_pop_entry(&free_list, struct ult_info, link);
	if (ult_info == NULL) {
		D_ALLOC_PTR(ult_info);
		if (ult_info == NULL)
			return -DER_NOMEM;
	}

	if (winfo->type == AGGREGATE)
		func = aggregate_ult;

	ult_info->winfo = winfo;
	d_list_add(&ult_info->link, &active_list);

	rc = daos_abt_thread_create_on_xstream(NULL, NULL, abt_xstream,
					       func, ult_info, ABT_THREAD_ATTR_NULL,
					       &ult_info->thread);
	if (rc != ABT_SUCCESS)
		return rc;

	if (wait) {
		ABT_thread_join(ult_info->thread);
		if (rc != ABT_SUCCESS)
			return rc;
		d_list_del(&ult_info->link);
		ABT_thread_free(&ult_info->thread);
		d_list_add(&ult_info->link, &free_list);
		rc = winfo->status;
		D_FREE(ult_info->winfo);
	} /** Join in caller */

	return rc;
}

static int done;
static void
intHandler(int dummy)
{
	done = 1;
}

int
run_tests(struct write_info *pinfo) {
	int			 scratch;
	struct ult_info		*ult_info;
	struct write_info	*winfo;
	int			 rc;
	printf("Starting test...hit Ctrl-C to stop the test");
	signal(SIGINT, intHandler);

	srand(time(0));

	done = 0;
	while (!atomic_load(&done)) {
		/* First free ults that are done */
		while ((ult_info = d_list_pop_entry(&join_list, struct ult_info, link)) != NULL) {
			rc = ABT_thread_join(ult_info->thread);
			if (rc != ABT_SUCCESS)
				return rc;
			d_list_del(&ult_info->link);
			ABT_thread_free(&ult_info->thread);
			winfo = ult_info->winfo;
			d_list_add(&ult_info->link, &free_list);
			if (winfo->status != 0)
				printf("An operation failed "DF_RC"\n", DP_RC(winfo->status));
			D_FREE(ult_info->winfo);
		}

		D_ALLOC_PTR(winfo);
		if (winfo == NULL) {
			printf("out of memory\n");
			break;
		}

		scratch = rand() % 13;
		if (scratch < 1)
			winfo->type = AGGREGATE;
		else if (scratch < 3)
			winfo->type = REMOVE;
		else if (scratch < 5)
			winfo->type = REMOVE_ALL;
		else if (scratch < 8)
			winfo->type = PUNCH;
		else
			winfo->type = WRITE;

		memcpy(&winfo->key[0], &pinfo->key[0], sizeof(pinfo->key));

		winfo->start = pinfo->start + rand() % pinfo->length;
		winfo->length = rand() % (pinfo->start + pinfo->length - winfo->start) + 1;

		if (winfo->type != AGGREGATE || !in_agg) {
			rc = handle_op(winfo, false);
			if (rc != 0) {
				printf("kick off op failed "DF_RC"\n", DP_RC(rc));
				break;
			}
		}

		if ((rand() % 10) == 0)
			ABT_thread_yield();
	}

	while ((ult_info = d_list_pop_entry(&active_list, struct ult_info, link)) != NULL) {
		ABT_thread_join(ult_info->thread);
		if (rc != ABT_SUCCESS)
			return rc;
		d_list_del(&ult_info->link);
		ABT_thread_free(&ult_info->thread);
		winfo = ult_info->winfo;
		if (winfo->status != 0)
			printf("An operation failed "DF_RC"\n", DP_RC(winfo->status));
		D_FREE(ult_info->winfo);
		D_FREE(ult_info);
	}

	while ((ult_info = d_list_pop_entry(&join_list, struct ult_info, link)) != NULL) {
		ABT_thread_join(ult_info->thread);
		if (rc != ABT_SUCCESS)
			return rc;
		d_list_del(&ult_info->link);
		ABT_thread_free(&ult_info->thread);
		winfo = ult_info->winfo;
		if (winfo->status != 0)
			printf("An operation failed "DF_RC"\n", DP_RC(winfo->status));
		D_FREE(ult_info->winfo);
		D_FREE(ult_info);
	}

	printf("Done\n");

	return 0;
}

int
punch_key(char *key)
{
	int		rc = 0;
	daos_key_t	dkey;
	daos_unit_oid_t	oid;

	set_oid(&oid);

	/* Set up dkey and akey */
	d_iov_set(&dkey, &key[0], strlen(key));

	/* Write the original value (under) */
	rc = vos_obj_punch(coh, oid, crt_hlc_get(), 0,
			    0, &dkey, 0, NULL, NULL);

	D_INFO("Punch %s in pool and container uuid="DF_UUID", rc="DF_RC"\n", key,
	       DP_UUID(current_uuid), DP_RC(rc));
	return rc;
}


int
discard(void)
{
	daos_epoch_range_t epr = {0, crt_hlc_get()};
	int rc;

	rc = vos_discard(coh, NULL, &epr, NULL, NULL);

	D_INFO("Discard pool and container uuid="DF_UUID", rc="DF_RC"\n", DP_UUID(current_uuid),
	       DP_RC(rc));
	return rc;
}

int
print_size(void)
{
	int rc;
	vos_pool_info_t pinfo;

	rc = vos_pool_query(poh, &pinfo);

	if (rc != 0) {
		D_ERROR("Could not query pool uuid="DF_UUID", rc="DF_RC"\n", DP_UUID(current_uuid),
			DP_RC(rc));
		return rc;
	}

	D_INFO("Size query for pool uuid="DF_UUID" got"
	       " scm={sys="DF_U64",free="DF_U64",total="DF_U64"}\n", DP_UUID(current_uuid),
	       SCM_SYS(&pinfo.pif_space), SCM_FREE(&pinfo.pif_space), SCM_TOTAL(&pinfo.pif_space));

	return 0;
}

static int
abit_start(void)
{
	int	rc;

	rc = ABT_init(0, NULL);
        if (rc != ABT_SUCCESS) {
                fprintf(stderr, "ABT init failed: %d\n", rc);
                return -1; 
        }

        rc = ABT_xstream_self(&abt_xstream);
        if (rc != ABT_SUCCESS) {
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
}

int
main(int argc, char **argv)
{
	int c;
	int rc;
	int option_index = 0;
	struct write_info	*winfo;
	struct ult_info		*ult_info;
	static struct option long_options[] = {
		{"create",	required_argument,	0, 'c'},
		{"open",	required_argument,	0, 'o'},
		{"close",	no_argument,		0, 'd'},
		{"tx",		no_argument,		0, 't'},
		{"write",	required_argument,	0, 'w'},
		{"punch_range",	required_argument,	0, 'P'},
		{"iterate",	required_argument,	0, 'i'},
		{"remove",	required_argument,	0, 'R'},
		{"punch",	required_argument,	0, 'p'},
		{"aggregate",	no_argument,		0, 'a'},
		{"discard",	no_argument,		0, 'r'},
		{"remove-all",	no_argument,		0, 'A'},
		{"size",	no_argument,		0, 's'},
		{"help",	no_argument,		0, 'h'},
		{0,		0,			0, 0},
	};

	if (abit_start() != 0) {
		printf("Failed to init abt\n");
		return -1;
	}

	memset(write_buf, 'b', sizeof(write_buf));

	rc = daos_debug_init("/tmp/vos.log");
	if (rc != 0) {
		printf("Failed to init debug: "DF_RC"\n", DP_RC(rc));
		exit(-1);
	}

	rc = vos_self_init("/mnt/daos", false, -1);

	daos_fail_loc_set(DAOS_VOS_AGG_RANDOM_YIELD | DAOS_FAIL_ALWAYS);

	while ((c = getopt_long(argc, argv, "c:o:dw:p:ahrsP:R:ix:A:",
				long_options, &option_index)) != -1) {
		switch (c) {
		case '?':
			printf("Invalid argument %s\n", argv[optind]);
			/* fallthrough */
		case 'h':
			print_usage(argv[0]);
			break;
		case 'i':
			if (!current_open) {
				D_ERROR("Must have pool/container open before iterate\n");
				exit(0);
			}
			rc = iterate();
			if (rc != 0)
				exit(-1);
			break;
		case 'c':
			if (current_open) {
				D_ERROR("Must close pool/container before create/open new one\n");
				exit(0);
			}
			rc = create_pool(optarg);
			if (rc != 0)
				exit(-1);
			current_open = true;
			break;
		case 'o':
			if (current_open) {
				D_ERROR("Must close pool/container before opening new one\n");
				exit(0);
			}
			rc = open_pool(optarg);
			if (rc != 0)
				exit(-1);
			current_open = true;
			break;
		case 'd':
			if (!current_open) {
				D_ERROR("Must have pool/container open to close\n");
				exit(0);
			}
			close_pool();
			current_open = false;
			break;
		case 'w':
			if (!current_open) {
				D_ERROR("Must have pool/container open to write\n");
				exit(0);
			}
			rc = parse_write_info(optarg, false, &winfo, WRITE);
			if (rc != 0)
				return -1;
			rc = handle_op(winfo, true);
			if (rc != 0)
				exit(-1);
			break;
		case 'P':
			if (!current_open) {
				D_ERROR("Must have pool/container open to punch\n");
				exit(0);
			}
			rc = parse_write_info(optarg, false, &winfo, PUNCH);
			if (rc != 0)
				return -1;
			rc = handle_op(winfo, true);
			if (rc != 0)
				exit(-1);
			break;
		case 'R':
			if (!current_open) {
				D_ERROR("Must have pool/container open to remove\n");
				exit(0);
			}
			rc = parse_write_info(optarg, false, &winfo, REMOVE);
			if (rc != 0)
				return -1;
			rc = handle_op(winfo, true);
			if (rc != 0)
				exit(-1);
			break;
		case 'A':
			if (!current_open) {
				D_ERROR("Must have pool/container open to remove all\n");
				exit(0);
			}
			rc = parse_write_info(optarg, false, &winfo, REMOVE_ALL);
			if (rc != 0)
				return -1;
			rc = handle_op(winfo, true);
			if (rc != 0)
				exit(-1);
			break;
		case 'x':
			if (!current_open) {
				D_ERROR("Must have pool/container open to randomize\n");
				exit(0);
			}
			rc = parse_write_info(optarg, true, &winfo, 0 /* don't care */);
			if (rc != 0) {
				print_usage(argv[0]);
				return -1;
			}
			rc = run_tests(winfo);
			if (rc != 0)
				exit(-1);
			break;
		case 'p':
			if (!current_open) {
				D_ERROR("Must have pool/container open to punch\n");
				exit(0);
			}
			rc = punch_key(optarg);
			if (rc != 0)
				exit(-1);
			break;
		case 'a':
			if (!current_open) {
				D_ERROR("Must have pool/container open to aggregate\n");
				exit(0);
			}
			rc = parse_write_info(optarg, false, &winfo, AGGREGATE);
			if (rc != 0)
				return -1;
			rc = handle_op(winfo, true);
			if (rc != 0)
				exit(-1);
			break;
		case 'r':
			if (!current_open) {
				D_ERROR("Must have pool/container open to discard\n");
				exit(0);
			}
			rc = discard();
			if (rc != 0)
				exit(-1);
			break;
		case 's':
			if (!current_open) {
				D_ERROR("Must have pool/container open to get size\n");
				exit(0);
			}
			rc = print_size();
			if (rc != 0)
				exit(-1);
			break;
		}
	}

	while ((ult_info = d_list_pop_entry(&free_list, struct ult_info, link)) != NULL)
		D_FREE(ult_info);

	vos_self_fini();
	daos_debug_fini();

	abit_fini();

	return 0;
}
