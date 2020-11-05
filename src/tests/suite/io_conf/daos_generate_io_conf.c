/**
 * (C) Copyright 2018 Intel Corporation.
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
 * This file is part of daos, to generate the io test.
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_iotest.h"

static int obj_num = 1;
static int dkey_num = 10;
static int akey_num = 10;
static int rank_size = -1;
static int tgt_size = -1;
static int iod_size = 1;
static char *default_class = "repl_3_small_rw_spec_rank";
static char *obj_class;

enum op {
	UPDATE_ARRAY,
	FETCH,
	PUNCH_ARRAY,
	PUNCH_AKEY,
	MAX_OPS,
};

enum single_record_op {
	UPDATE_SINGLE,
	FETCH_SINGLE,
	PUNCH_AKEY_SINGLE,
	MAX_OPS_SINGLE,
};

enum type {
	SINGLE,
	ARRAY,
};

struct current_status {
	int		cur_obj_num;
	int		cur_dkey_num;
	int		cur_akey_num;
	int		cur_rank;
	uint64_t	cur_tx;
};

enum rec_types {
	ARRAY_REC = 1 << 0,
	PUNCH_REC = 1 << 1,
};

struct extent {
	daos_off_t	start;
	daos_off_t	end;
};

struct array {
	struct extent	extent;
	char		value;
};

struct single {
	char		value;
};

struct record {
	union {
		struct array	array;
		struct single	single;
	};
	int		rec_size;
	int		type;
	bool		snap;
};

struct records {
	uint64_t	eph;
	int		records_num;
	struct record	records[DTS_MAX_EXT_NUM];
};

static struct option long_ops[] = {
	{ "obj_num",	required_argument,	NULL,	'o' },
	{ "dkey_num",	required_argument,	NULL,	'd' },
	{ "akey_num",	required_argument,	NULL,	'a' },
	{ "rec_size",	required_argument,	NULL,	's' },
	{ "rank_size",	required_argument,	NULL,	'g' },
	{ "tgt_size",	required_argument,	NULL,	't' },
	{ "obj_class",	required_argument,	NULL,	'O' },
	{ "help",	no_argument,		NULL,	'h' },
	{ NULL,		0,			NULL,	0   },
};

void print_usage(void)
{
	fprintf(stdout, "daos_generate_io_conf -g <rank_size> -t <tgt_size>"
		"-o [obj_num] -d [dkey_num] -a [akey_num] -s [rec_size]"
		"-O obj_class <file_name>\n");
}

static void
extent_twist(struct extent *input, struct extent *output, int off, bool add)
{
	*output = *input;
	if (add) {
		output->start += off;
		output->end += off;
	} else {
		if (output->start > off) {
			output->start -= off;
			output->end -= off;
		} else {
			output->end = input->end - input->start;
			output->start = 0;
		}
	}
}

static int
update_array_internal(int index, uint64_t eph, struct extent *extents,
		      int extents_num, int rec_size,
		      struct records *records, char *output_buf, bool update)
{
	char rec_buf[512];
	int rec_length = 0;
	bool twist = false;
	int twist_off = 0;
	bool twist_add = false;
	int offset;
	int num;
	int j;
	int i;

	/* Insert extents to records */
	records[index].eph = eph;
	if (index == 0) {
		/* Write the whole extents at first */
		offset = 0;
		num = extents_num;
	} else {
		offset = rand() % extents_num;
		num = rand() % (extents_num - offset) + 1;
	}

	records[index].records_num = num;
	/* Twist the extent if needed */
	if (rand() % 2 == 0) {
		twist = true;
		twist_off = rand() % DTS_MAX_EXTENT_SIZE;
		twist_add = rand() % 2 == 0 ? true : false;
	}

	for (i = 0, j = offset; i < num; i++, j++) {
		struct record *record = &records[index].records[i];
		char rec[32];
		struct extent extent;
		int length;

		if (index != 0 && twist)
			extent_twist(&extents[j], &extent, twist_off,
				     twist_add);
		else
			extent = extents[j];

		/* Only write the whole extents */
		record->array.extent = extent;
		record->rec_size = rec_size;
		record->type = ARRAY;
		if (update) {
			char value = 'a' + rand() % ('z' - 'a');

			records[index].records[i].array.value = value;
			length = sprintf(rec, "["DF_U64", "DF_U64"]%d",
					 extent.start, extent.end, value);
		} else {
			length = sprintf(rec, "["DF_U64", "DF_U64"]",
					 extent.start, extent.end);
		}

		sprintf(rec_buf + rec_length, "%s ", rec);
		rec_length += length + 1;
	}

	if (update) {
		sprintf(output_buf,
			"update --tx %"PRId64" --snap --recx \"%s\"\n",
			eph, rec_buf);
		records[index].records[0].snap = true;
	} else {
		sprintf(output_buf,
			"punch --tx %"PRId64" --recx \"%s\"\n",
			eph, rec_buf);
		records[index].records[0].snap = false;
	}

	return 0;
}

static int
update_array(int index, uint64_t eph, struct extent *extents,
	     int extents_num, int rec_size, struct records *records,
	     char *output_buf)
{
	return update_array_internal(index, eph, extents, extents_num,
				     rec_size, records, output_buf, true);
}

static int
punch_array(int index, uint64_t eph, struct extent *extents,
	    int extents_num, int rec_size, struct records *records,
	    char *output_buf)
{
	return update_array_internal(index, eph, extents, extents_num,
				     0, records, output_buf, false);
}

static int
_punch_akey(int index, uint64_t eph, struct extent *extents,
	    int extents_num, int rec_size, struct records *records,
	    char *output_buf)
{
	records[index].eph = eph;
	records[index].records_num = 0;
	sprintf(output_buf, "punch --tx %" PRId64 "\n", eph);
	return 0;
}

/* Update single record */
static int
update_single(int index, uint64_t eph, struct extent *extents,
	      int extents_num, int rec_size, struct records *records,
	      char *output_buf)
{
	char value = 'a' + rand() % ('z' - 'a');

	/* Insert extents to records */
	records[index].eph = eph;
	records[index].records_num = 1;

	records[index].records[0].type = SINGLE;
	records[index].records[0].single.value = value;

	if (value % 2 != 0) {
		records[index].records[0].snap = true;
		sprintf(output_buf,
			"update --tx %" PRId64 " --snap --single --value %d\n",
			eph, value);
	} else {
		records[index].records[0].snap = false;
		sprintf(output_buf,
			"update --tx %" PRId64 " --single --value %d\n",
			eph, value);
	}

	return 0;
}

static int
fetch_array(int index, uint64_t eph, struct extent *extents,
	    int extents_num, int rec_size, struct records *records,
	    char *output_buf)
{
	struct records *record;
	char            rec_buf[512] = {0};
	int             fetch_index;
	int             rec_length = 0;
	int             i;

	D_ASSERT(index > 0);

	fetch_index = rand() % index;
	record      = &records[fetch_index];
	for (i = 0; i < record->records_num; i++) {
		struct array *array = &record->records[i].array;
		int           length;
		char          rec[64];

		length = sprintf(rec, "[" DF_U64 ", " DF_U64 "]%d",
				 array->extent.start, array->extent.end,
				 array->value);

		sprintf(rec_buf + rec_length, "%s ", rec);
		rec_length += length + 1;
	}

	if (rec_length != 0) {
		if (record->records[0].snap == true)
			sprintf(output_buf,
				"fetch --tx %" PRId64 " -v "
				"--snap --recx \"%s\"\n",
				record->eph, rec_buf);
		else
			sprintf(output_buf,
				"fetch --tx %" PRId64 " --recx \"%s\"\n",
				record->eph, rec_buf);
	}
	return 0;
}

static int fetch_single(int index, uint64_t eph, struct extent *extents,
	int extents_num, int rec_size, struct records *records,
	char *output_buf)
{
	struct records *record;
	char            rec_buf[512] = {0};
	int             fetch_index;
	int             rec_length = 0;
	int             i;

	D_ASSERT(index > 0);

	fetch_index = rand() % index;
	record      = &records[fetch_index];
	for (i = 0; i < record->records_num; i++) {
		struct array *array = &record->records[i].array;
		int           length;
		char          rec[64];

		length = sprintf(rec, "[" DF_U64 ", " DF_U64 "]%d",
				 array->extent.start, array->extent.end,
				 array->value);

		sprintf(rec_buf + rec_length, "%s ", rec);
		rec_length += length + 1;
	}

	if (rec_length != 0) {
		if (record->records[0].snap == true)
			sprintf(output_buf,
				"fetch --tx %" PRId64 " -v --snap"
				" --single --value %d\n",
				record->eph, record->records[0].single.value);
		else
			sprintf(output_buf,
				"fetch --tx %" PRId64 " --single --value %d\n",
			    record->eph, record->records[0].single.value);

	}
	return 0;
}

int choose_op(int index, int max_operation)
{
	if (index == 0)
		return UPDATE_ARRAY;

	/* FIXME: it should be able to specify the percentage
	 * of each operation to generate the special workload.
	 */
	return rand() % max_operation;
}

struct operation {
	int (*op)(int index, uint64_t eph, struct extent *extents,
		  int extents_num, int rec_size, struct records *records,
		  char *output_buf);
};

struct operation operations[] = {
	[UPDATE_ARRAY] = {
		.op = &update_array,
	},
	[FETCH] = {
		.op = &fetch_array,
	},
	[PUNCH_ARRAY] = {
		.op = &punch_array,
	},
	[PUNCH_AKEY] = {
		.op = &_punch_akey,
	},
};

struct operation single_operations[] = {
	[UPDATE_SINGLE] = {
	    .op = &update_single,
	},
	[FETCH_SINGLE] = {
	    .op = &fetch_single,
	},
	[PUNCH_AKEY_SINGLE] = {
	    .op = &_punch_akey,
	},
};

/* Generate the ioconf
 *
 * update --tx 1 --snap --recx "[0, 2] [3, 8] [12, 18]"
 * update --tx 2 --snap --recx "[1, 3] [5, 10] [12, 14] [100, 108]"
 * update --tx 3 --snap --recx "[0, 8] [13, 17] [90, 104]"
 * update --tx 4 --snap --recx "[1, 20] [80, 96] [110, 120]"
  *
 * fail --rank %d --tgt %d
 * fetch --tx 1 --snap --recx "[0, 2] [3, 8] [12, 18]"
 * fetch --tx 2 --snap --recx "[1, 3] [5, 10] [12, 14] [100, 108]"
  */
int
generate_io_conf_rec(int fd, struct current_status *status)
{
	char		line[256];
	unsigned int	offset = 0;
	struct extent	extents[DTS_MAX_EXT_NUM] = { 0 };
	struct records  recs[DTS_MAX_EPOCH_TIMES] = { 0 };
	enum type	record_type;
	uint64_t	eph;
	int		dist;
	int		extent_num;
	int		extent_size;
	int		epoch_times;
	int		inject_fail_idx;
	int		i;
	int		rc1;
	int		rc = 0;
	int		tgt;
	int		op;

	sprintf(line, "iod_size %d\n", iod_size);
	rc1 = write(fd, line, strlen(line));
	if (rc1 <= 0)
		return -1;

	extent_num = rand() % DTS_MAX_EXT_NUM + 1;
	dist = rand() % DTS_MAX_DISTANCE;
	extent_size = (rand() % DTS_MAX_EXTENT_SIZE + 1) * DTS_MAX_EXTENT_SIZE;
	offset = rand() % DTS_MAX_OFFSET;
	epoch_times = rand() % DTS_MAX_EPOCH_TIMES + 1;
	/* create rec string */
	for (i = 0; i < extent_num; i++) {
		extents[i].start = offset;
		extents[i].end = offset + extent_size;
		offset += extent_size + dist;
	}

	eph = status->cur_tx;
	inject_fail_idx = rand() % epoch_times;
	tgt = rand() % tgt_size;
	if (tgt_size != -1) {
		tgt = rand() % tgt_size;
	} else {
		tgt = tgt_size;
	}

	if (rank_size != -1) {
		status->cur_rank = rand() % rank_size;
	}
	record_type = rand() % 2;

	for (i = 0; i < epoch_times; i++) {
		char	buffer[512];

		if (record_type == ARRAY) {
			op = choose_op(i, MAX_OPS);
			rc = (*operations[op].op)(i, eph + i, extents,
						  extent_num, 1, recs, buffer);
		} else {
			op = choose_op(i, MAX_OPS_SINGLE);
			rc = (*single_operations[op].op)(
			    i, eph + i, extents, extent_num, 1, recs, buffer);
		}
		if (rc)
			goto out;

		rc = write(fd, buffer, strlen(buffer));
		if (rc <= 0) {
			rc = -1;
			goto out;
		}

		if (inject_fail_idx == i) {
			sprintf(line, "exclude --rank %d --tgt %d\n",
				status->cur_rank, tgt);
			rc = write(fd, line, strlen(line));
			if (rc <= 0) {
				rc = -1;
				goto out;
			}
		}
	}

	/* Add back the target */
	sprintf(line, "add --rank %d --tgt %d\n", status->cur_rank, tgt);
	rc1 = write(fd, line, strlen(line));
	if (rc1 <= 0) {
		rc = -1;
		goto out;
	}

	sprintf(line, "pool --query\n");
	rc = write(fd, line, strlen(line));
	if (rc <= 0) {
		rc = -1;
		goto out;
	}
	rc = 0;

	status->cur_tx += i;
out:
	return rc;
}

int
generate_io_conf_akey(int fd, struct current_status *status)
{
	char akey[64];
	int rc = 0;

	while (status->cur_akey_num < akey_num) {
		status->cur_tx = 0;
		sprintf(akey, "akey akey_%d\n",
			status->cur_akey_num);
		rc = write(fd, akey, strlen(akey));
		if (rc <= 0) {
			rc = -1;
			break;
		}
		rc = generate_io_conf_rec(fd, status);
		if (rc)
			break;
		status->cur_akey_num++;
	}

	return rc;
}

int
generate_io_conf_dkey(int fd, struct current_status *status)
{
	char dkey[64];
	int rc = 0;

	while (status->cur_dkey_num < dkey_num) {
		sprintf(dkey, "dkey dkey_%d\n",
			status->cur_dkey_num);
		rc = write(fd, dkey, strlen(dkey));
		if (rc <= 0) {
			rc = -1;
			break;
		}
		rc = generate_io_conf_akey(fd, status);
		if (rc)
			break;
		status->cur_akey_num = 0;
		status->cur_dkey_num++;
	}

	return rc;
}

int
generate_io_conf_obj(int fd, struct current_status *status)
{
	char oid_buf[64];
	int rc = 0;

	while (status->cur_obj_num < obj_num) {
		int	rank = -1;

		/* Fill the dkey first */
		sprintf(oid_buf, "oid --type %s --rank %d\n", obj_class, rank);
		rc = write(fd, oid_buf, strlen(oid_buf));
		if (rc <= 0) {
			rc = -1;
			break;
		}

		status->cur_rank = rank;

		rc = generate_io_conf_dkey(fd, status);
		if (rc)
			break;

		status->cur_dkey_num = 0;
		status->cur_akey_num = 0;
		status->cur_obj_num++;
	}

	return rc;
}

int
main(int argc, char **argv)
{
	char	*fname = NULL;
	struct current_status status = { 0 };
	int	fd;
	int	rc;

	while ((rc = getopt_long(argc, argv, "a:d:o:s:g:t:O:h",
				 long_ops, NULL)) != -1) {
		char	*endp;

		switch (rc) {
		case 'a':
			akey_num = strtoul(optarg, &endp, 0);
			break;
		case 'd':
			dkey_num = strtoul(optarg, &endp, 0);
			break;
		case 'o':
			obj_num = strtoul(optarg, &endp, 0);
			break;
		case 's':
			iod_size = strtoul(optarg, &endp, 0);
			break;
		case 'g':
			rank_size = strtoul(optarg, &endp, 0);
			break;
		case 't':
			tgt_size = strtoul(optarg, &endp, 0);
			break;
		case 'O':
			D_STRNDUP(obj_class, optarg, strlen(optarg));
			break;
		case 'h':
			print_usage();
			return 0;
		default:
			fprintf(stderr, "Unknown option %c\n", rc);
			print_usage();
			return -1;
		}
	}

	if (optind == argc) {
		fprintf(stderr, "Bad parameters.\n");
		print_usage();
		return -1;
	}

	if (obj_class == NULL)
		obj_class = default_class;

	fname = argv[optind];
	fd = open(fname, O_RDWR|O_TRUNC|O_CREAT, 0666);
	if (fd < 0) {
		rc = -1;
		goto out;
	}

	/* Prepare the header, only support daos */
	rc = write(fd, "test_lvl daos\n", strlen("test_lvl daos\n"));
	if (rc <= 0) {
		rc = -1;
		goto out;
	}

	rc = generate_io_conf_obj(fd, &status);
	if (rc)
		goto out;
out:
	if (obj_class && obj_class != default_class)
		D_FREE(obj_class);

	close(fd);
	return rc;
}
