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
 * This file is part of daos, to generate the epoch io test.
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_iotest.h"

static int obj_num = 1;
static int dkey_num = 10;
static int akey_num = 10;
static int rank_size = 8;
static int tgt_size = 8;
static int iod_size = 1;
static char *default_class = "repl_3_small_rw_spec_rank";
static char *obj_class;

#define MAX_EXT_NUM		5
#define MAX_DISTANCE		10
#define MAX_EXTENT_SIZE		50
#define MAX_OFFSET		1048576
#define SINGLE_REC_RATE		20
#define MAX_EPOCH_TIMES		20

enum op {
	UPDATE_ARRAY,
	PUNCH_ARRAY,
	UPDATE_SINGLE,
	PUNCH_AKEY,
	FETCH,
	MAX_OPS,
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
	daos_epoch_t	cur_eph;
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
};

struct records {
	daos_epoch_t	eph;
	int		records_num;
	struct record	records[MAX_EXT_NUM];
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
		"<file_name>\n");
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
update_array_internal(int index, daos_epoch_t eph, struct extent *extents,
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
		twist_off = rand() % MAX_EXTENT_SIZE;
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

	if (update)
		sprintf(output_buf, "update --epoch "DF_U64" --recx \"%s\"\n",
			eph, rec_buf);
	else
		sprintf(output_buf, "punch --epoch "DF_U64" --recx \"%s\"\n",
			eph, rec_buf);

	return 0;
}

static int
update_array(int index, daos_epoch_t eph, struct extent *extents,
	     int extents_num, int rec_size, struct records *records,
	     char *output_buf)
{
	return update_array_internal(index, eph, extents, extents_num,
				     rec_size, records, output_buf, true);
}

static int
punch_array(int index, daos_epoch_t eph, struct extent *extents,
	    int extents_num, int rec_size, struct records *records,
	    char *output_buf)
{
	return update_array_internal(index, eph, extents, extents_num,
				     0, records, output_buf, false);
}

static int
_punch_akey(int index, daos_epoch_t eph, struct extent *extents,
	    int extents_num, int rec_size, struct records *records,
	    char *output_buf)
{
	records[index].eph = eph;
	records[index].records_num = 0;
	sprintf(output_buf, "punch --epoch "DF_U64"\n", eph);
	return 0;
}

/* Update single record */
static int
update_single(int index, daos_epoch_t eph, struct extent *extents,
	      int extents_num, int rec_size, struct records *records,
	      char *output_buf)
{
	char value = 'a' + rand() % ('z' - 'a');

	/* Insert extents to records */
	records[index].eph = eph;
	records[index].records_num = 1;

	records[index].records[0].type = SINGLE;
	records[index].records[0].single.value = value;

	sprintf(output_buf, "update --epoch "DF_U64" --single --value %d\n",
		eph, value);

	return 0;
}

static int
fetch_array(int index, daos_epoch_t eph, struct extent *extents,
	    int extents_num, int rec_size, struct records *records,
	    char *output_buf)
{
	struct records *record;
	char rec_buf[512] = { 0 };
	int fetch_index;
	int rec_length = 0;
	int i;

	D_ASSERT(index > 0);

	fetch_index = rand() % index;
	record = &records[fetch_index];
	for (i = 0; i < record->records_num; i++) {
		struct array *array = &record->records[i].array;
		int length;
		char rec[64];

		length = sprintf(rec, "["DF_U64", "DF_U64"]%d",
				 array->extent.start,
				 array->extent.end, array->value);

		sprintf(rec_buf + rec_length, "%s ", rec);
		rec_length += length + 1;
	}

	if (rec_length == 0) {
		sprintf(output_buf, "fetch --epoch "DF_U64" -s -v --value 0\n",
			record->eph);
	} else {
		if (record->records[0].type == SINGLE)
			sprintf(output_buf, "fetch --epoch "DF_U64" -v --single"
				" --value %d\n", record->eph,
				record->records[0].single.value);
		else
			sprintf(output_buf, "fetch --epoch "DF_U64" -v --recx"
				" \"%s\"\n", record->eph, rec_buf);
	}

	return 0;
}

int choose_op(int index)
{
	if (index == 0)
		return UPDATE_ARRAY;

	/* FIXME: it should be able to specify the percentage
	 * of each operation to generate the special workload.
	 */
	return rand() % MAX_OPS;
}

struct operation {
	int (*op)(int index, daos_epoch_t eph, struct extent *extents,
		  int extents_num, int rec_size, struct records *records,
		  char *output_buf);
};

struct operation operations[] = {
	[UPDATE_ARRAY] = {
		.op = &update_array,
	},
	[PUNCH_ARRAY] = {
		.op = &punch_array,
	},
	[UPDATE_SINGLE] = {
		.op = &update_single,
	},
	[PUNCH_AKEY] = {
		.op = &_punch_akey,
	},
	[FETCH] = {
		.op = &fetch_array,
	},
};

/* Generate the ioconf
 *
 * update --epoch 1 --recx "[0, 2] [3, 8] [12, 18]"
 * update --epoch 1 --single
 * update --epoch 2 --recx "[1, 3] [5, 10] [12, 14] [100, 108]"
 * update --epoch 3 --recx "[0, 8] [13, 17] [90, 104]"
 * update --epoch 4 --recx "[1, 20] [80, 96] [110, 120]"
 * update --epoch 4 --single
 *
 * fail --rank %d --tgt %d
 * fetch --epoch 1 --recx "[0, 2] [3, 8] [12, 18]"
 * fetch --epoch 2 --recx "[0, 4] [5, 7] [13, 15] [100, 108]"
 * fetch --epoch 2 --single
 */
int
generate_io_conf_rec(int fd, struct current_status *status)
{
	char		line[256];
	unsigned int	offset = 0;
	struct extent	extents[MAX_EXT_NUM] = { 0 };
	struct records recs[MAX_EPOCH_TIMES] = { 0 };
	daos_epoch_t	eph;
	int		dist;
	int		extent_num;
	int		extent_size;
	int		epoch_times;
	int		inject_fail_idx;
	int		i;
	int		rc1;
	int		rc = 0;
	int		tgt;

	sprintf(line, "iod_size %d\n", iod_size);
	rc1 = write(fd, line, strlen(line));
	if (rc1 <= 0)
		return -1;

	extent_num = rand() % MAX_EXT_NUM + 1;
	dist = rand() % MAX_DISTANCE;
	extent_size = (rand() % MAX_EXTENT_SIZE + 1) * MAX_EXTENT_SIZE;
	offset = rand() % MAX_OFFSET;
	epoch_times = rand() % MAX_EPOCH_TIMES + 1;

	/* create rec string */
	for (i = 0; i < extent_num; i++) {
		extents[i].start = offset;
		extents[i].end = offset + extent_size;
		offset += extent_size + dist;
	}

	eph = status->cur_eph;
	inject_fail_idx = rand() % epoch_times;
	tgt = rand() % tgt_size;
	for (i = 0; i < epoch_times; i++) {
		char	buffer[512];
		int	op = choose_op(i);

		rc = (*operations[op].op)(i, eph + i, extents, extent_num,
					  1, recs, buffer);
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

	status->cur_eph += i;
out:
	return rc;
}

int
generate_io_conf_akey(int fd, struct current_status *status)
{
	char akey[64];
	int rc = 0;

	while (status->cur_akey_num < akey_num) {
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
